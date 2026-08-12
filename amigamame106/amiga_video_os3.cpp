/*
 * amiga_video_os3.cpp
 * Purpose: AmigaOS 3.x native video implementation (OCS/ECS/AGA chipsets)
 *
 * ╔════════════════════════════════════════════════════════════════════════╗
 * ║            🖥️  AMIGA OS3 VIDEO DRIVER - NATIVE CHIPSETS 🎮           ║
 * ║  ┌──────────────────────────────────────────────────────────────┐    ║
 * ║  │                                                               │    ║
 * ║  │   MAME Display ──► Palette Remap ──► Chunky-to-Planar       │    ║
 * ║  │                                                               │    ║
 * ║  │   ┌──────────────┐    ┌──────────────┐    ┌────────────┐   │    ║
 * ║  │   │ WPA8         │───►│  C2P Asm     │───►│  Screen    │   │    ║
 * ║  │   │ ChunkyPixels │    │  Conversion  │    │  BitMap    │   │    ║
 * ║  │   └──────────────┘    └──────────────┘    └────────────┘   │    ║
 * ║  │                                                               │    ║
 * ║  │   Supports: OCS (4-bit), ECS (6-bit), AGA (8-bit)           │    ║
 * ║  │   Features: Light pen, triple buffering, palette remapping  │    ║
 * ║  └──────────────────────────────────────────────────────────────┘    ║
 * ║      Classic Amiga graphics - hardware sprites and copper lists!      ║
 * ╚════════════════════════════════════════════════════════════════════════╝
 *
 * Author: krb
 * Copyright (C) 2025
 * Licensed under GPL v2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "amiga_video_os3.h"
#include "amiga_video_ham6.h"
#include "amiga_video_tracers_clut16.h"
#include "amiga_video_remap.h"
#include "frf_perf_profile.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "amiga_inputs_lightgun.h"
extern "C" {
    // from mame
    #include "mame.h"
    #include "video.h"
    #include "mamecore.h"
    #include "osdepend.h"
    #include "palette.h"
}
extern "C" {
    // from amigaos but not proto
    #include "hardware/custom.h"
    #include "graphics/gfxbase.h"
    #include <graphics/displayinfo.h>
}


//#include <stdio.h>
//#include <stdlib.h>
#include <string.h>

extern int frf89_color_mode; /* FRF89I_TRANSACTIONAL_HAM_EHB_RGB32_GREY_HOTKEY */

//void waitsecs(int s)
//{
//    for(int i=0;i<50*s;i++)
//    {
//        WaitTOF();
//    }
//}

template<typename T> void doSwap(T&a,T&b) { T c=a; a=b; b=c; }


/* FRF_NATIVE_EHB_PAIR_LUT_V6
 *
 * Convert two 8-bit MAME palette indices to two final EHB pen bytes with
 * one UWORD lookup. The 128 KiB table is built once and then updated only
 * for palette entries whose final pen changed.
 */
static std::vector<UWORD> frf_ehb_pair_lut;
static UBYTE frf_ehb_pair_map[256];
static bool frf_ehb_pair_valid = false;

static UWORD *frf_prepare_ehb_pair_lut(Paletted *remap)
{
    if(!remap) return NULL;

    UBYTE current[256];
    const size_t clutSize = remap->_clut8.size();

    for(int i=0;i<256;i++)
    {
        if(clutSize == 0)
            current[i] = (UBYTE)(i & 0x3f);
        else if((size_t)i < clutSize)
            current[i] = (UBYTE)(remap->_clut8[i] & 0x3f);
        else
            current[i] = 0;
    }

    int changed[256];
    int changedCount = 0;

    if(!frf_ehb_pair_valid)
    {
        for(int i=0;i<256;i++) changed[changedCount++] = i;
    }
    else
    {
        for(int i=0;i<256;i++)
            if(current[i] != frf_ehb_pair_map[i])
                changed[changedCount++] = i;
    }

    if(frf_ehb_pair_lut.size() != 65536)
    {
        frf_ehb_pair_lut.resize(65536);
        frf_ehb_pair_valid = false;
        changedCount = 256;
        for(int i=0;i<256;i++) changed[i] = i;
    }

    if(!frf_ehb_pair_valid || changedCount > 32)
    {
        UWORD *table = frf_ehb_pair_lut.data();
        for(int a=0;a<256;a++)
        {
            const UWORD hi = ((UWORD)current[a]) << 8;
            UWORD *row = table + (a << 8);
            for(int b=0;b<256;b++)
                row[b] = hi | current[b];
        }
    }
    else
    {
        UWORD *table = frf_ehb_pair_lut.data();

        for(int n=0;n<changedCount;n++)
        {
            const int i = changed[n];
            const UWORD hi = ((UWORD)current[i]) << 8;
            UWORD *row = table + (i << 8);

            for(int b=0;b<256;b++)
                row[b] = hi | current[b];

            for(int a=0;a<256;a++)
                table[(a << 8) | i] =
                    (((UWORD)current[a]) << 8) | current[i];
        }
    }

    for(int i=0;i<256;i++)
        frf_ehb_pair_map[i] = current[i];

    frf_ehb_pair_valid = true;
    return frf_ehb_pair_lut.data();
}

/* FRF_NATIVE_EHB_U16LUT_HELPER_V7
 * Direct high-colour source-index to EHB-pen table. After Burner's 0x2000
 * source entries use only 8 KiB.
 */
static std::vector<UBYTE> frf_ehb_u16_lut;

static UBYTE *frf_prepare_ehb_u16_lut(Paletted *remap, int sourceColours)
{
    if(!remap) return NULL;
    if(sourceColours <= 256 || sourceColours > 32768) return NULL;
    const ULONG clutSize = (ULONG)remap->_clut8.size();
    if(clutSize < (ULONG)sourceColours) return NULL;
    if(frf_ehb_u16_lut.size() != (ULONG)sourceColours)
        frf_ehb_u16_lut.resize(sourceColours);
    for(int i=0;i<sourceColours;i++)
        frf_ehb_u16_lut[i] = (UBYTE)(remap->_clut8[i] & 0x3f);
    return frf_ehb_u16_lut.data();
}




Drawable_OS3::Drawable_OS3(IntuitionDrawable &drawable)
 : _drawable(drawable),_pRemap(NULL),_useIntuitionPalette(false)
, _colorsIndexLength(0),_video_attributes(0)
{
   // wpa8temprastport _trp;
    memset(&_trp,0,sizeof(_trp));
}
Drawable_OS3::~Drawable_OS3()
{
    close();
}
void Drawable_OS3::draw_WPA8(_mame_display *display)
{
    RastPort *pRPort = _drawable.rastPort();
    if(!pRPort) return;
    mame_bitmap *bitmap = display->game_bitmap;

    // - - update palette if exist and is needed.
    if(_pRemap && ((display->changed_flags & GAME_PALETTE_CHANGED) !=0 || _pRemap->needRemap()))
    {
        _pRemap->updatePaletteRemap(display);
    }
    int sourcewidth,sourceheight;
    int cenx,ceny,ww,hh;
    _drawable.getGeometry(display,cenx,ceny,ww,hh,sourcewidth,sourceheight);

    // align on 16
    int wwal = (ww+15)&0xfffffff0;
    const int bmsize = wwal*(hh+4); // +4 because zoom trick in draswcale.
    if(bmsize != _wpatempbm.size())
    {
        _wpatempbm.resize(bmsize);
    }
    checkWpa8TmpRp(pRPort,wwal);
    directDrawScreen ddscreen={
        _wpatempbm.data(),
        wwal, // bpr
        0,0,ww,hh // clip rect
    };

    directDrawSource ddsource={bitmap->base,bitmap->rowbytes,
        display->game_visible_area.min_x,display->game_visible_area.min_y,
        display->game_visible_area.max_x+1,display->game_visible_area.max_y+1,
        _drawable.flags()
    };
    //     if(_PixelFmt == PIXFMT_LUT8) _flags|= DISPFLAG_INTUITIONPALETTE;

    directDrawParams p{&ddscreen,&ddsource,0,0,ww,hh};
    if(_pRemap) _pRemap->directDraw(&p);

    //WritePixelArray8(rp,xstart,ystart,xstop,ystop,array,temprp)

  if(_trp._rp.BitMap)
  {
        WritePixelArray8(pRPort,cenx,ceny,cenx+ww-1,ceny+hh-1,
        _wpatempbm.data(), &_trp._rp
        );
  }

}
void Drawable_OS3::checkWpa8TmpRp(RastPort *rp,int linewidth)
{
    // sizex = the width (in pixels) desired for the bitmap data.
    if(linewidth==0) return;
    int pixelwidth = ((linewidth+15)&0xfffffff0); // align to 16
    if(pixelwidth ==  _trp._checkw && _trp._rp.BitMap != NULL) return;
    if(_trp._rp.BitMap) FreeBitMap(_trp._rp.BitMap);

    memcpy(&_trp,rp,sizeof(RastPort) );
    _trp._rp.Layer = NULL;
    _trp._rp.BitMap =AllocBitMap(
                pixelwidth,2, // it's one but...
            /*rp->BitMap->Depth*/8,BMF_CLEAR,/*rp->BitMap*/ NULL);
    if(!_trp._rp.BitMap) return;
    _trp._checkw=pixelwidth;

}
void Drawable_OS3::draw_WriteChunkyPixels(_mame_display *display)
{
    RastPort *pRPort = _drawable.rastPort();
    if(!pRPort) return;
    mame_bitmap *bitmap = display->game_bitmap;

    // - - update palette if exist and is needed.
    if(_pRemap && ((display->changed_flags & GAME_PALETTE_CHANGED) !=0 || _pRemap->needRemap()))
    {
        _pRemap->updatePaletteRemap(display);
    }
    int sourcewidth,sourceheight;
    int cenx,ceny,ww,hh;
    _drawable.getGeometry(display,cenx,ceny,ww,hh,sourcewidth,sourceheight);

    // - - get a 8bit bitmap from the 16b one, we know there's only 256 color max. - -
    const int bmsize = ww*(hh+4); // +2 becaus of zoom trick but well.
    if(bmsize != _wpatempbm.size()) _wpatempbm.resize(bmsize);

    directDrawScreen ddscreen={
        _wpatempbm.data(),
        ww, // bpr
        0,0,ww,hh // clip rect
    };

    directDrawSource ddsource={bitmap->base,bitmap->rowbytes,
        display->game_visible_area.min_x,display->game_visible_area.min_y,
        display->game_visible_area.max_x+1,display->game_visible_area.max_y+1,
        _drawable.flags()
    };

    directDrawParams p{&ddscreen,&ddsource,0,0,ww,hh};

    if(_pRemap) _pRemap->directDraw(&p);

//	WriteChunkyPixels(rp,xstart,ystart,xstop,ystop,array,bytesperrow)
//	                  A0 D0     D1     D2    D3    A2     D4


    WriteChunkyPixels(pRPort,cenx,ceny,cenx+ww-1,ceny+hh-1,_wpatempbm.data(),ww );
}
extern "C" {
#ifdef __GNUC__
#define REG(r) __asm(#r)
#else
#define REG(r)
#endif

    void c2p(UBYTE *chunkyscreen REG(a0),
             struct BitMap *bm REG(a1),
            WORD chunkyx REG(d0),
            WORD chunkyy REG(d1),
            WORD offsx REG(d2),
            WORD offsy REG(d3),
            LONG chunkybpr REG(d4)
             );

    /* FRF_NATIVE_C2P56_DECLS_V3 */
    void frf_c2p5(UBYTE *chunkyscreen REG(a0),
                  struct BitMap *bm REG(a1),
                  WORD chunkyx REG(d0),
                  WORD chunkyy REG(d1),
                  WORD offsx REG(d2),
                  WORD offsy REG(d3),
                  LONG chunkybpr REG(d4));

    void frf_c2p6(UBYTE *chunkyscreen REG(a0),
                  struct BitMap *bm REG(a1),
                  WORD chunkyx REG(d0),
                  WORD chunkyy REG(d1),
                  WORD offsx REG(d2),
                  WORD offsy REG(d3),
                  LONG chunkybpr REG(d4));

    void c2p5plane(UBYTE *chunkyscreen REG(a0),
                   struct BitMap *bm REG(a1),
                   WORD chunkyx REG(d0),
                   WORD chunkyy REG(d1),
                   WORD offsx REG(d2),
                   WORD offsy REG(d3),
                   LONG chunkybpr REG(d4)
                  );

}

static int frfV3EnabledDefaultOn(const char *name)
{
    const char *value = getenv(name);
    if(!value || !value[0]) return 1;

    if(strcmp(value,"0") == 0 ||
       strcmp(value,"OFF") == 0 || strcmp(value,"off") == 0 ||
       strcmp(value,"NO") == 0 || strcmp(value,"no") == 0 ||
       strcmp(value,"FALSE") == 0 || strcmp(value,"false") == 0)
        return 0;

    return 1;
}



extern "C" {
    /* FRF_NATIVE_EHB_FUSED_DECL_V6 */
    void frf_c2p6_u16pair(UWORD *source REG(a0),
                          struct BitMap *bm REG(a1),
                          UWORD *pairLut REG(a2),
                          WORD width REG(d0),
                          WORD height REG(d1),
                          WORD offsx REG(d2),
                          WORD offsy REG(d3),
                          LONG sourcebpr REG(d4));

    /* FRF_NATIVE_EHB_U16LUT_DECL_V7 */
    void frf_c2p6_u16lut(UBYTE *source REG(a0),
                         struct BitMap *bm REG(a1),
                         UBYTE *indexLut REG(a2),
                         WORD width REG(d0), WORD height REG(d1),
                         WORD offsx REG(d2), WORD offsy REG(d3),
                         LONG sourcebpr REG(d4));
}

#ifdef ACTIVATE_OWN_C2P

/* -------------------------------------------------------------------------
 * FRF Mame106RGB V3C ACTIVE30
 * ------------------------------------------------------------------------- */
static int frfV3CEnvDefaultOn(const char *name)
{
    const char *value = getenv(name);

    if(!value || !value[0])
        return 1;

    if(strcmp(value,"0") == 0 ||
       strcmp(value,"OFF") == 0 ||
       strcmp(value,"off") == 0 ||
       strcmp(value,"NO") == 0 ||
       strcmp(value,"no") == 0 ||
       strcmp(value,"FALSE") == 0 ||
       strcmp(value,"false") == 0)
        return 0;

    return 1;
}

struct FrfV3CClearRecord
{
    struct BitMap *bitmap;
    ULONG signature;
};

static void frfV3CClearPlanarIfNeeded(
    struct BitMap *bitmap,
    ULONG signature)
{
    static FrfV3CClearRecord records[8] = {{0,0}};
    int slot = -1;

    if(!bitmap)
        return;

    for(int i=0; i<8; ++i)
    {
        if(records[i].bitmap == bitmap)
        {
            slot = i;
            break;
        }

        if(slot < 0 && records[i].bitmap == NULL)
            slot = i;
    }

    if(slot < 0)
        slot = 0;

    if(records[slot].bitmap == bitmap &&
       records[slot].signature == signature)
        return;

    ULONG planeBytes =
        (ULONG)bitmap->BytesPerRow * (ULONG)bitmap->Rows;

    for(int plane=0;
        plane<(int)bitmap->Depth && plane<8;
        ++plane)
    {
        if(bitmap->Planes[plane])
            BltClear(bitmap->Planes[plane],planeBytes,0);
    }

    WaitBlit();

    records[slot].bitmap = bitmap;
    records[slot].signature = signature;
}


extern "C" {
    void c2p6plane(
        UBYTE *chunkyscreen REG(a0),
        struct BitMap *bm REG(a1),
        WORD chunkyx REG(d0),
        WORD chunkyy REG(d1),
        WORD offsx REG(d2),
        WORD offsy REG(d3),
        LONG chunkybpr REG(d4));
}

void Drawable_OS3::draw_c2p(_mame_display *display)
{
    /* FRF_NATIVE_ASM_C2P_ALIGNED_V1
     * c2p.s requires a width divisible by 32 and destination X divisible by 8.
     * Build a genuinely aligned chunky row and place padding around the image.
     */
    RastPort *pRPort = _drawable.rastPort();
    if(!pRPort || !pRPort->BitMap || !display || !display->game_bitmap) return;

    if(_pRemap && (((display->changed_flags & GAME_PALETTE_CHANGED) != 0) ||
                   _pRemap->needRemap()))
        _pRemap->updatePaletteRemap(display);

    if(!_pRemap)
    {
        Drawable_OS3::draw_WriteChunkyPixels(display);
        return;
    }

    int sourcewidth,sourceheight;
    int cenx,ceny,ww,hh;
    _drawable.getGeometry(display,cenx,ceny,ww,hh,sourcewidth,sourceheight);
    if(ww <= 0 || hh <= 0) return;

    const int physw = _drawable.widthPhys();
    const int physh = _drawable.heightPhys();
    if(ceny < 0) { hh += ceny; ceny = 0; }
    if(ceny + hh > physh) hh = physh - ceny;
    if(hh <= 0) return;

    const int c2pww = (ww + 31) & ~31;
    const int totalPad = c2pww - ww;
    int leftAvailable = cenx;
    int rightAvailable = physw - (cenx + ww);
    if(leftAvailable < 0) leftAvailable = 0;
    if(rightAvailable < 0) rightAvailable = 0;

    if(totalPad > leftAvailable + rightAvailable)
    {
        Drawable_OS3::draw_WriteChunkyPixels(display);
        return;
    }

    int minPadLeft = totalPad - rightAvailable;
    if(minPadLeft < 0) minPadLeft = 0;
    int maxPadLeft = totalPad;
    if(maxPadLeft > leftAvailable) maxPadLeft = leftAvailable;

    int padLeft = -1;
    int bestError = 0x7fffffff;
    const int centredPad = totalPad >> 1;
    for(int candidate = minPadLeft; candidate <= maxPadLeft; candidate++)
    {
        if(((cenx - candidate) & 7) != 0) continue;
        int error = candidate - centredPad;
        if(error < 0) error = -error;
        if(error < bestError) { bestError = error; padLeft = candidate; }
    }

    if(padLeft < 0)
    {
        Drawable_OS3::draw_WriteChunkyPixels(display);
        return;
    }

    const int padRight = totalPad - padLeft;
    const int destx = cenx - padLeft;
    if(destx < 0 || (destx & 7) != 0 || destx + c2pww > physw)
    {
        Drawable_OS3::draw_WriteChunkyPixels(display);
        return;
    }

    const int bmsize = c2pww * hh;
    if((int)_wpatempbm.size() != bmsize) _wpatempbm.resize(bmsize);
    UBYTE *chunky = _wpatempbm.data();

    if(padLeft != 0 || padRight != 0)
    {
        for(int y = 0; y < hh; y++)
        {
            UBYTE *row = chunky + y * c2pww;
            if(padLeft != 0) memset(row,0,padLeft);
            if(padRight != 0) memset(row + padLeft + ww,0,padRight);
        }
    }

    directDrawScreen ddscreen={chunky,c2pww,0,0,c2pww,hh};
    mame_bitmap *bitmap = display->game_bitmap;
    directDrawSource ddsource={bitmap->base,bitmap->rowbytes,
        display->game_visible_area.min_x,display->game_visible_area.min_y,
        display->game_visible_area.max_x+1,display->game_visible_area.max_y+1,
        _drawable.flags()};
    directDrawParams p{&ddscreen,&ddsource,padLeft,0,ww,hh};

        /* FRF_NATIVE_EHB_U16LUT_SELECT_V7
     * High-colour fused EHB path for indexed UWORD games such as After Burner.
     */
    const bool frfEhbU16LutEligible =
        (pRPort->BitMap->Depth == 6) &&
        ((_video_attributes & VIDEO_RGB_DIRECT) == 0) &&
        (_colorsIndexLength > 256) &&
        (_colorsIndexLength <= 32768) &&
        ((_drawable.flags() & ORIENTATION_MASK) == 0) &&
        (ww == sourcewidth) && (hh == sourceheight) &&
        (c2pww == ww) && ((ww & 31) == 0) &&
        (bitmap->rowbytes >= (ULONG)(ww * (int)sizeof(UWORD)));
    
    if(frfEhbU16LutEligible)
    {
        UBYTE *frfEhbIndexLut = frf_prepare_ehb_u16_lut(_pRemap, _colorsIndexLength);
        if(frfEhbIndexLut)
        {
            UBYTE *sourceBase = ((UBYTE *)bitmap->base) +
                ((LONG)display->game_visible_area.min_y * (LONG)bitmap->rowbytes) +
                ((LONG)display->game_visible_area.min_x * (LONG)sizeof(UWORD));
            frf_c2p6_u16lut(sourceBase, pRPort->BitMap, frfEhbIndexLut,
                (WORD)ww, (WORD)hh, (WORD)destx, (WORD)ceny,
                (LONG)bitmap->rowbytes);
            return;
        }
    }
    
/* FRF_NATIVE_EHB_FUSED_SELECT_V6
     *
     * Fused EHB is an early-return path. The original V5 remap and C2P
     * below remain byte-for-byte intact and handle every fallback case.
     */
    const bool frfEhbFusedEligible =
        (pRPort->BitMap->Depth == 6) &&
        ((_video_attributes & VIDEO_RGB_DIRECT) == 0) &&
        (_colorsIndexLength > 0) &&
        (_colorsIndexLength <= 256) &&
        ((_drawable.flags() & ORIENTATION_MASK) == 0) &&
        (ww == sourcewidth) &&
        (hh == sourceheight) &&
        (c2pww == ww) &&
        ((ww & 31) == 0) &&
        (bitmap->rowbytes >=
            (ULONG)(ww * (int)sizeof(UWORD)));
    
    if(frfEhbFusedEligible)
    {
        UWORD *frfEhbPairLut =
            frf_prepare_ehb_pair_lut(_pRemap);
    
        if(frfEhbPairLut)
        {
            UBYTE *sourceBase =
                ((UBYTE *)bitmap->base) +
                ((LONG)display->game_visible_area.min_y *
                    (LONG)bitmap->rowbytes) +
                ((LONG)display->game_visible_area.min_x *
                    (LONG)sizeof(UWORD));
    
            frf_c2p6_u16pair((UWORD *)sourceBase,
                pRPort->BitMap,
                frfEhbPairLut,
                (WORD)ww,
                (WORD)hh,
                (WORD)destx,
                (WORD)ceny,
                (LONG)bitmap->rowbytes);
            return;
        }
    }
    _pRemap->directDraw(&p);

    /* FRF_NATIVE_C2P56_BLOCK_COHERENT_V3
     *
     * Depth 5 and 6 now use one full-frame assembly call. The new
     * converter commits every active plane for each 32-pixel block
     * before advancing. The proven V2 line path remains the fallback.
     */
    const UBYTE nativeDepth = pRPort->BitMap->Depth;
    if(nativeDepth == 5)
    {
        frf_c2p5(chunky,
                 pRPort->BitMap,
                 (WORD)c2pww,
                 (WORD)hh,
                 (WORD)destx,
                 (WORD)ceny,
                 (LONG)c2pww);
    }
    else if(nativeDepth == 6)
    {
        frf_c2p6(chunky,
                 pRPort->BitMap,
                 (WORD)c2pww,
                 (WORD)hh,
                 (WORD)destx,
                 (WORD)ceny,
                 (LONG)c2pww);
    }
    else
    {
        for(int line = 0; line < hh; line++)
        {
            c2p(chunky + (line * c2pww),
                pRPort->BitMap,
                (WORD)c2pww,
                (WORD)1,
                (WORD)destx,
                (WORD)(ceny + line),
                (LONG)c2pww);
        }
    }
}
#endif


static int frfV3LIsEhbScreen(struct Screen *screen)
{
    if(!screen) return 0;
    ULONG modeId = GetVPModeID(&(screen->ViewPort));
    if(modeId == INVALID_ID) return 0;
    struct DisplayInfo displayInfo;
    LONG got = GetDisplayInfoData(
        NULL,(UBYTE *)&displayInfo,sizeof(displayInfo),DTAG_DISP,modeId);
    if(got <= 0) return 0;
    return (displayInfo.PropertyFlags & DIPF_IS_EXTRAHALFBRITE) ? 1 : 0;
}

void Drawable_OS3::initRemapTable()
{
    if(_useIntuitionPalette) // cases where we set a private screen palette with LOADRGB32.
    {
        // 8-bit screen colors will be managed with LoadRGB32 and direct pixel copy (no CLUT).
        // if(_colorsIndexLength<=258)
        //     _pRemap = new Paletted_Screen8(_drawable.screen());
        // 8-bit screens will have a fixed 256c palette and 16b index color remap to this.
        if(_video_attributes & VIDEO_RGB_DIRECT)
        {
            if(_video_attributes & VIDEO_NEEDS_6BITS_PER_GUN)
            {
                _pRemap = new Paletted_Screen8ForcePalette_32b(_drawable.screen());
            } else
            {
                _pRemap = new Paletted_Screen8ForcePalette_15b(_drawable.screen());
            }
        } else
        {
            // use exact palette index and update if nb color fits.
            Screen *pScreen = _drawable.screen();
            int privScreenNbColors = 1<<(pScreen->RastPort.BitMap->Depth);
            if(_colorsIndexLength<=privScreenNbColors)
            {
                     _pRemap = new Paletted_Screen8(pScreen);
            }
            // force fixed palette and manage large index to this index at palette change.
            else
            {
             if(privScreenNbColors <= 64 &&
                    (frfV3EnabledDefaultOn("FRF_OPT_PALETTE") ||
                     frfV3LIsEhbScreen(pScreen)))
                {
                    _pRemap = new Paletted_Screen8Optimized(pScreen);
                }
                else
                {
                    _pRemap = new Paletted_Screen8ForcePalette(pScreen);
                }
            }


        }
    } else // case where we re-use an existing intuition screen indexed palette. (like on a 8Bit Workbench)
    {
        //printf("_colorsIndexLength:%d\n",_colorsIndexLength);
        // windows on Workbench 8Bit will remap 8&16bits to the palette given by workbench.
        if(_video_attributes & VIDEO_RGB_DIRECT)
        {
            if(_video_attributes & VIDEO_NEEDS_6BITS_PER_GUN)
            {
                _pRemap = new Paletted_Pens8_src32b(_drawable.screen());
            } else
            {
                _pRemap = new Paletted_Pens8_src15b(_drawable.screen());
            }
        } else // most games not RGB
        {
            if(_colorsIndexLength>0)
            {
                _pRemap = new Paletted_Pens8(_drawable.screen()); // same case for <258 colors or more.
            }
        }
    }
}
void Drawable_OS3::close()
{
    if(_pRemap) delete _pRemap;
    _pRemap = NULL;
    _wpatempbm.clear();
     if(_trp._rp.BitMap) FreeBitMap(_trp._rp.BitMap);
     _trp._rp.BitMap=NULL;
     _trp._checkw=0;
}


/*
 * FRF Mame106RGB V3J HAM6 STATIC ENGINE
 *
 * This follows the OCS/ECS HAM6 interpretation used by E-UAE:
 *   00iiii = select one of 16 base colours
 *   01bbbb = modify blue from the previous pixel
 *   10rrrr = modify red from the previous pixel
 *   11gggg = modify green from the previous pixel
 *
 * The target is deliberately static-screen output.  It defaults to one
 * conversion per second, uses a Fast-RAM RGB444 staging frame, writes six
 * single-buffered CHIP bitplanes directly, and skips identical frames.
 */
static int frfHam6EnvInt(const char *name,int defaultValue,int minimum,int maximum)
{
    const char *value = getenv(name);
    int result = defaultValue;
    if(value && value[0]) result = atoi(value);
    if(result < minimum) result = minimum;
    if(result > maximum) result = maximum;
    return result;
}

static ULONG frfHam6Distance(UWORD a,UWORD b)
{
    LONG dr = (LONG)((a >> 8) & 15) - (LONG)((b >> 8) & 15);
    LONG dg = (LONG)((a >> 4) & 15) - (LONG)((b >> 4) & 15);
    LONG db = (LONG)(a & 15) - (LONG)(b & 15);
    return (ULONG)(dr*dr*3 + dg*dg*6 + db*db*2);
}

static UWORD frfHam6Rgb24To444(ULONG colour)
{
    return (UWORD)(
        ((colour >> 12) & 0x0f00) |
        ((colour >> 8)  & 0x00f0) |
        ((colour >> 4)  & 0x000f));
}

static UWORD frfHam6ReadSource444(
    _mame_display *display,
    int sourceX,
    int sourceY)
{
    mame_bitmap *bitmap = display ? display->game_bitmap : NULL;
    if(!bitmap || !bitmap->line) return 0;
    if(sourceX < 0 || sourceX >= bitmap->width ||
       sourceY < 0 || sourceY >= bitmap->height) return 0;

    ULONG colour = 0;
    ULONG index = 0;

    if(bitmap->depth <= 8)
    {
        index = ((const UBYTE *)bitmap->line[sourceY])[sourceX];
        if(display->game_palette && index < (ULONG)display->game_palette_entries)
            colour = ((ULONG)display->game_palette[index]) & 0x00ffffffUL;
    }
    else if(bitmap->depth <= 16)
    {
        index = ((const UWORD *)bitmap->line[sourceY])[sourceX];
        if(display->game_palette && index < (ULONG)display->game_palette_entries)
        {
            colour = ((ULONG)display->game_palette[index]) & 0x00ffffffUL;
        }
        else
        {
            ULONG red = (index >> 10) & 31;
            ULONG green = (index >> 5) & 31;
            ULONG blue = index & 31;
            colour = (((red << 3) | (red >> 2)) << 16) |
                     (((green << 3) | (green >> 2)) << 8) |
                     ((blue << 3) | (blue >> 2));
        }
    }
    else
    {
        colour = ((const ULONG *)bitmap->line[sourceY])[sourceX] & 0x00ffffffUL;
    }

    return frfHam6Rgb24To444(colour);
}

static void frfHam6BuildBasePalette(
    const std::vector<UWORD> &pixels,
    UWORD base[16])
{
    std::vector<ULONG> histogram(4096,0);
    for(size_t i=0;i<pixels.size();++i)
        histogram[pixels[i] & 0x0fff]++;

    base[0] = 0;

    for(int slot=1;slot<16;++slot)
    {
        unsigned long long bestScore = 0;
        int bestColour = -1;

        for(int colour=0;colour<4096;++colour)
        {
            if(!histogram[colour]) continue;

            ULONG nearest = 0xffffffffUL;
            for(int selected=0;selected<slot;++selected)
            {
                ULONG distance = frfHam6Distance((UWORD)colour,base[selected]);
                if(distance < nearest) nearest = distance;
            }

            unsigned long long score =
                (unsigned long long)(nearest + 1) *
                (unsigned long long)histogram[colour];

            if(score > bestScore)
            {
                bestScore = score;
                bestColour = colour;
            }
        }

        if(bestColour >= 0)
            base[slot] = (UWORD)bestColour;
        else
        {
            int shade = (slot * 15) / 15;
            base[slot] = (UWORD)((shade << 8) | (shade << 4) | shade);
        }
    }

    for(int pass=0;pass<3;++pass)
    {
        unsigned long long sumR[16]={0};
        unsigned long long sumG[16]={0};
        unsigned long long sumB[16]={0};
        unsigned long long sumW[16]={0};

        for(int colour=0;colour<4096;++colour)
        {
            ULONG weight = histogram[colour];
            if(!weight) continue;

            int nearestIndex = 0;
            ULONG nearestDistance = frfHam6Distance((UWORD)colour,base[0]);

            for(int index=1;index<16;++index)
            {
                ULONG distance = frfHam6Distance((UWORD)colour,base[index]);
                if(distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestIndex = index;
                }
            }

            sumR[nearestIndex] += ((colour >> 8) & 15) * weight;
            sumG[nearestIndex] += ((colour >> 4) & 15) * weight;
            sumB[nearestIndex] += (colour & 15) * weight;
            sumW[nearestIndex] += weight;
        }

        base[0] = 0;
        for(int index=1;index<16;++index)
        {
            if(sumW[index])
            {
                ULONG red = (ULONG)(sumR[index] / sumW[index]);
                ULONG green = (ULONG)(sumG[index] / sumW[index]);
                ULONG blue = (ULONG)(sumB[index] / sumW[index]);
                base[index] = (UWORD)((red << 8) | (green << 4) | blue);
            }
        }
    }
}

static UBYTE frfHam6EncodePixel(UWORD target,UWORD previous,const UWORD base[16],UWORD *result)
{
    int bestIndex = 0;
    ULONG bestDistance = frfHam6Distance(target,base[0]);

    for(int index=1;index<16;++index)
    {
        ULONG distance = frfHam6Distance(target,base[index]);
        if(distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    UBYTE bestCode = (UBYTE)bestIndex;
    UWORD bestColour = base[bestIndex];

    UWORD blueColour = (UWORD)((previous & 0x0ff0) | (target & 0x000f));
    ULONG blueDistance = frfHam6Distance(target,blueColour);
    if(blueDistance < bestDistance)
    {
        bestDistance = blueDistance;
        bestCode = (UBYTE)(0x10 | (target & 15));
        bestColour = blueColour;
    }

    UWORD redColour = (UWORD)((previous & 0x00ff) | (target & 0x0f00));
    ULONG redDistance = frfHam6Distance(target,redColour);
    if(redDistance < bestDistance)
    {
        bestDistance = redDistance;
        bestCode = (UBYTE)(0x20 | ((target >> 8) & 15));
        bestColour = redColour;
    }

    UWORD greenColour = (UWORD)((previous & 0x0f0f) | (target & 0x00f0));
    ULONG greenDistance = frfHam6Distance(target,greenColour);
    if(greenDistance < bestDistance)
    {
        bestCode = (UBYTE)(0x30 | ((target >> 4) & 15));
        bestColour = greenColour;
    }

    *result = bestColour;
    return bestCode;
}

static ULONG frfHam6Hash(const std::vector<UWORD> &pixels)
{
    ULONG hash = 2166136261UL;
    for(size_t i=0;i<pixels.size();++i)
    {
        hash ^= (ULONG)pixels[i];
        hash *= 16777619UL;
    }
    return hash;
}

Intuition_Screen_OS3::Intuition_Screen_OS3(const AbstractDisplay::params &params)
    : Intuition_Screen(params), Drawable_OS3((IntuitionDrawable&)*this)
    , _lightpen_inited(0)
    , _fix70Ham6Mode(false)
    , _ham6Output(NULL)
    , _ham6Mode(0)
    , _ehb6Mode(0)
    , _ham6LastHash(0)
    , _ham6LastUpdate(0)
    , _ham6ClearSignature(0)
{
    /* FRF_FIX75_V4_CONSTRUCTOR_INIT */
    _frfFix75V4HamOutput = NULL;

    _colorsIndexLength = params._colorsIndexLength;
    _video_attributes = params._video_attributes;
    int width = params._width;
    int height = params._height;
    if(params._flags & ORIENTATION_SWAP_XY) doSwap(width,height);

    _useIntuitionPalette = true;
    _ScreenDepthAsked = params._forcedDepth; // used by OpenSCreen(), AGA max, default.
    if(_ScreenModeId == INVALID_ID)
    {
        // FRF RGBFAST V1: request the shallowest native depth that can hold
        // the indexed game palette. Direct-RGB games still require 8 planes
        // and are quantised by the existing remapper.
        int preferredDepth = 8;
        if((_video_attributes & VIDEO_RGB_DIRECT) == 0)
        {
            if(_colorsIndexLength <= 16) preferredDepth = 4;
            else if(_colorsIndexLength <= 32) preferredDepth = 5;
            else if(_colorsIndexLength <= 64) preferredDepth = 6;
        }

        int depthsToTest[4];
        if(preferredDepth == 4)
        {
            depthsToTest[0]=4; depthsToTest[1]=5; depthsToTest[2]=6; depthsToTest[3]=8;
        }
        else if(preferredDepth == 5)
        {
            depthsToTest[0]=5; depthsToTest[1]=6; depthsToTest[2]=8; depthsToTest[3]=4;
        }
        else if(preferredDepth == 6)
        {
            depthsToTest[0]=6; depthsToTest[1]=8; depthsToTest[2]=5; depthsToTest[3]=4;
        }
        else
        {
            depthsToTest[0]=8; depthsToTest[1]=6; depthsToTest[2]=5; depthsToTest[3]=4;
        }

        for(int idepth=0; idepth<4 && _ScreenModeId == INVALID_ID; ++idepth)
        {
            _ScreenModeId = BestModeID(
                    BIDTAG_Depth,depthsToTest[idepth],
                    BIDTAG_NominalWidth,width,
                    BIDTAG_NominalHeight,height,
                    TAG_DONE );
            if(_ScreenModeId != INVALID_ID) _ScreenDepthAsked = depthsToTest[idepth];
        }
        if(_ScreenModeId == INVALID_ID)
        {
            loginfo(2," **** Can't find screen mode for w%d h%d",width,height);
            return;
        }
    } // end if no mode decided at first

    /* FRF V3L: detect native OCS/ECS HAM6 and EHB6 separately. */
    if(_ScreenModeId != INVALID_ID)
    {
        struct DisplayInfo displayInfo;
        LONG gotDisplay = GetDisplayInfoData(
            NULL,(UBYTE *)&displayInfo,sizeof(displayInfo),DTAG_DISP,_ScreenModeId);

        if(gotDisplay > 0)
        {
            int rejected = 0;
#ifdef DIPF_IS_AA
            if(displayInfo.PropertyFlags & DIPF_IS_AA) rejected = 1;
#endif
#ifdef DIPF_IS_FOREIGN
            if(displayInfo.PropertyFlags & DIPF_IS_FOREIGN) rejected = 1;
#endif
            if(!rejected && (displayInfo.PropertyFlags & DIPF_IS_HAM))
            {
                _ham6Mode = 1;
                _ScreenDepthAsked = 6;
                _flags &= ~(DISPFLAG_USETRIPLEBUFFER | DISPFLAG_USEHEIGHTBUFFER);
                loginfo(2,"FRF HAM6 OCS/ECS mode selected: $%08lx",_ScreenModeId);
            }
            else if(!rejected &&
                    (displayInfo.PropertyFlags & DIPF_IS_EXTRAHALFBRITE))
            {
                _ehb6Mode = 1;
                _ScreenDepthAsked = 6;
                _flags &= ~(DISPFLAG_USETRIPLEBUFFER | DISPFLAG_USEHEIGHTBUFFER);
                loginfo(2,"FRF EHB6 OCS/ECS mode selected: $%08lx",_ScreenModeId);
            }
        }
    }

    // inquire mode metrics
    {
        LONG v;
        struct DimensionInfo dims;
        v = GetDisplayInfoData(NULL, (UBYTE *) &dims, sizeof(struct DimensionInfo),
                     DTAG_DIMS, _ScreenModeId);
        if(v>0)
        {
            //_screenDepthAsked = (int)dims.MaxDepth; // if mode was
            _fullscreenWidth = (int)(dims.Nominal.MaxX - dims.Nominal.MinX)+1;
            _fullscreenHeight = (int)(dims.Nominal.MaxY - dims.Nominal.MinY)+1;
            // if game screen big, try some oversan conf.
            // if(_fullscreenWidth< width )
            // {
            //     _fullscreenWidth = (int)(dims.MaxOScan.MaxX - dims.MaxOScan.MinX)+1;
            // }
            if(_fullscreenHeight< height )
            {
                _fullscreenHeight = (int)(dims.MaxOScan.MaxY - dims.MaxOScan.MinY)+1;
            }

        // printf("aga mode $%08x w:%d h:%d\n",
        //       (int)_ScreenModeId, _fullscreenWidth,_fullscreenHeight);

        } else
        {   // shouldnt happen, fallback
            _fullscreenWidth = width;
            _fullscreenHeight = height;
        }
    }
}
Intuition_Screen_OS3::~Intuition_Screen_OS3()
{

}

bool Intuition_Screen_OS3::open()
{
    /* FRF89I_RUNTIME_SCREEN_STATE
     * Recreated objects start from the original HAM display params. Make the
     * legacy mode members describe the transaction target before old routing
     * branches inspect them.
     */
    if((frf89_color_mode % 5) == 1)
    {
        _ham6Mode = 0;
        _ehb6Mode = 1;
        _ScreenDepthAsked = 6;
    }
    else if((frf89_color_mode % 5) == 2 ||
            (frf89_color_mode % 5) == 3)
    {
        _ham6Mode = 0;
        _ehb6Mode = 0;
        _ScreenDepthAsked = 5;
    }
    else if((frf89_color_mode % 5) == 4)
    {
        _ham6Mode = 0;
        _ehb6Mode = 0;
        _ScreenDepthAsked = 4;
    }
    else
    {
        _ehb6Mode = 0;
    }

    /* FRF_FIX70_COMPLETE_HAM6_ROUTE */
    _fix70Ham6Mode = ((frf89_color_mode % 5) == 0) && Ham6EuaeOutput::isHam6ModeId(_ScreenModeId);
    if(_fix70Ham6Mode)
    {
        /* HAM6 owns its two ScreenBuffers. Never combine it with
         * MAME's normal triple or vertical height buffering. */
        _flags &= ~(DISPFLAG_USETRIPLEBUFFER |
                    DISPFLAG_USEHEIGHTBUFFER);
    }

    //printf("Intuition_Screen_OS3::open\n");
    _fullscreenWidth = (_fullscreenWidth+31) & 0xffffffe0; // 32pixel align for c2p
    bool ok = Intuition_Screen::open();
    if(!ok) return false;
    // after Screen is open, create the normal remapper or HAM6 engine.
    if(_ham6Mode)
    {
        struct BitMap *hamBitmap = bitmap();
        if(!hamBitmap || hamBitmap->Depth != 6)
        {
            loginfo(2,"FRF HAM6 ERROR: selected HAM screen did not open at depth 6");
            Intuition_Screen::close();
            return false;
        }

        for(int pen=0;pen<16;++pen)
            SetRGB4(&(_pScreen->ViewPort),pen,0,0,0);

        loginfo(2,"FRF HAM6 engine active: OCS/ECS, single-buffer, default 1 fps");
    }
    else
    {
        // after Screen is open, choose exactly one native output engine.
    if(_fix70Ham6Mode)
    {
        _ham6Output = new Ham6EuaeOutput(*this, _video_attributes);
        if(!_ham6Output || !_ham6Output->open())
        {
            if(_ham6Output)
            {
                delete _ham6Output;
                _ham6Output = NULL;
            }
            Intuition_Screen::close();
            _fix70Ham6Mode = false;
            return false;
        }
    }
    else
    {
        initRemapTable();
        if((frf89_color_mode % 5) == 3)
            printf("FRF89I COLOR: native GREYSCALE remap active\n");
    }

        if(_ehb6Mode)
        {
            struct BitMap *ehbBitmap = bitmap();
            if(!ehbBitmap || ehbBitmap->Depth != 6)
            {
                loginfo(2,"FRF EHB6 ERROR: selected EHB screen did not open at depth 6");
                Intuition_Screen::close();
                return false;
            }
            loginfo(2,"FRF EHB6 engine active: 32 base + 32 hardware half-bright pens");
        }
    }

    if(_flags & DISPFLAG_LIGHTGUN)
    {
    	GfxBase->system_bplcon0 |= LP_ENABLE;
    	RemakeDisplay();
    	_lightpen_inited = 1;
    }

        /* FRF_FIX75_V4_FINAL_OPEN_ROUTE
     *
     * Independent late route: do not depend on FIX70 member names.
     * The base/native open and legacy static HAM setup have completed before the final outer-level return.
     */
    if(_frfFix75V4HamOutput)
    {
        _frfFix75V4HamOutput->close();
        delete _frfFix75V4HamOutput;
        _frfFix75V4HamOutput = NULL;
    }

    const bool frfFix75FinalHam =
        Ham6EuaeOutput::isHam6ModeId(_ScreenModeId) ||
        (_ham6Mode != 0);

    printf(
        "FRF FIX75 V4 ROUTE: finalmode=%08lx depth=%lu "
        "legacyham=%d request=%d\n",
        (unsigned long)_ScreenModeId,
        (unsigned long)_ScreenDepthAsked,
        (int)((_ham6Mode != 0)),
        frfFix75FinalHam ? 1 : 0);

    if((frf89_color_mode == 0) && (frfFix75FinalHam))
    {
        _frfFix75V4HamOutput =
            new Ham6EuaeOutput(*this, _video_attributes);

        if(!_frfFix75V4HamOutput ||
           !_frfFix75V4HamOutput->open())
        {
            if(_frfFix75V4HamOutput)
            {
                _frfFix75V4HamOutput->close();
                delete _frfFix75V4HamOutput;
                _frfFix75V4HamOutput = NULL;
            }

            printf(
                "FRF FIX75 V4 ROUTE FALLBACK: "
                "E-UAE backend open failed; static renderer retained\n");
        }
        else
        {
            printf(
                "FRF FIX75 V4 ROUTE ACTIVE: "
                "E-UAE HAM backend opened; "
                "static 1-FPS draw path bypassed\n");
        }
    }

return ok;
}
void Intuition_Screen_OS3::close()
{
    /* FRF_FIX75_V4_CLOSE_CLEANUP */
    if(_frfFix75V4HamOutput)
    {
        _frfFix75V4HamOutput->close();
        delete _frfFix75V4HamOutput;
        _frfFix75V4HamOutput = NULL;
    }

    if(_ham6Output)
    {
        _ham6Output->close();
        delete _ham6Output;
        _ham6Output = NULL;
    }
    _fix70Ham6Mode = false;

    if(_lightpen_inited)
    {
    	GfxBase->system_bplcon0 &= ~LP_ENABLE;
    	RemakeDisplay();
    	_lightpen_inited = 0;
    }
    _ham6Rgb444.clear();
    _ham6LastHash = 0;
    _ham6LastUpdate = 0;
    _ham6ClearSignature = 0;
    Intuition_Screen::close();
    Drawable_OS3::close();
}


void Intuition_Screen_OS3::drawHAM6(_mame_display *display)
{
    if(!display || !display->game_bitmap || !_pScreen) return;

    struct BitMap *destination = bitmap();
    if(!destination || destination->Depth != 6) return;

    int requestedFps = frfHam6EnvInt("FRF_HAM6_FPS",1,1,10);
    int staticSkip = frfHam6EnvInt("FRF_HAM6_STATIC",1,0,1);
    cycles_t now = osd_cycles();
    cycles_t cps = osd_cycles_per_second();
    cycles_t interval = cps / requestedFps;

    if(_ham6LastUpdate && now - _ham6LastUpdate < interval)
        return;
    _ham6LastUpdate = now;

    int sourceWidth,sourceHeight;
    int destinationX,destinationY,width,height;
    getGeometry(
        display,
        destinationX,destinationY,
        width,height,
        sourceWidth,sourceHeight);

    destinationX &= ~15;
    if(destinationX < 0) destinationX = 0;
    if(destinationY < 0) destinationY = 0;
    if(destinationX >= _widthphys || destinationY >= _heightphys) return;

    if(width > _widthphys - destinationX)
        width = _widthphys - destinationX;
    if(height > _heightphys - destinationY)
        height = _heightphys - destinationY;

    width &= ~15;
    if(width <= 0 || height <= 0) return;

    const rectangle &visible = display->game_visible_area;
    int rawWidth = visible.max_x - visible.min_x + 1;
    int rawHeight = visible.max_y - visible.min_y + 1;
    int orientedWidth = (_flags & ORIENTATION_SWAP_XY) ? rawHeight : rawWidth;
    int orientedHeight = (_flags & ORIENTATION_SWAP_XY) ? rawWidth : rawHeight;

    if(rawWidth <= 0 || rawHeight <= 0 ||
       orientedWidth <= 0 || orientedHeight <= 0) return;

    size_t pixelCount = (size_t)width * (size_t)height;
    _ham6Rgb444.resize(pixelCount);

    for(int y=0;y<height;++y)
    {
        int oy = (int)(((LONG)y * orientedHeight) / height);
        if(oy >= orientedHeight) oy = orientedHeight - 1;

        for(int x=0;x<width;++x)
        {
            int ox = (int)(((LONG)x * orientedWidth) / width);
            if(ox >= orientedWidth) ox = orientedWidth - 1;

            int orientedX = ox;
            int orientedY = oy;

            if(_flags & ORIENTATION_FLIP_X)
                orientedX = orientedWidth - 1 - orientedX;
            if(_flags & ORIENTATION_FLIP_Y)
                orientedY = orientedHeight - 1 - orientedY;

            int sourceX;
            int sourceY;
            if(_flags & ORIENTATION_SWAP_XY)
            {
                sourceX = orientedY;
                sourceY = orientedX;
            }
            else
            {
                sourceX = orientedX;
                sourceY = orientedY;
            }

            sourceX += visible.min_x;
            sourceY += visible.min_y;

            _ham6Rgb444[(size_t)y * width + x] =
                frfHam6ReadSource444(display,sourceX,sourceY);
        }
    }

    ULONG hash = frfHam6Hash(_ham6Rgb444);
    if(staticSkip && _ham6LastHash && hash == _ham6LastHash)
        return;
    _ham6LastHash = hash;

    UWORD base[16];
    frfHam6BuildBasePalette(_ham6Rgb444,base);

    ULONG clearSignature =
        ((ULONG)(destinationX & 0xffff) << 16) ^
        (ULONG)(destinationY & 0xffff) ^
        ((ULONG)(width & 0xffff) << 8) ^
        (ULONG)(height & 0xffff);

    if(clearSignature != _ham6ClearSignature)
    {
        ULONG planeBytes = (ULONG)destination->BytesPerRow * destination->Rows;
        for(int plane=0;plane<6;++plane)
            if(destination->Planes[plane])
                memset(destination->Planes[plane],0,planeBytes);
        _ham6ClearSignature = clearSignature;
    }

    ULONG bytesPerRow = destination->BytesPerRow;
    ULONG destinationByteX = (ULONG)destinationX >> 3;

    for(int y=0;y<height;++y)
    {
        UWORD previous = base[0];

        for(int blockX=0;blockX<width;blockX+=16)
        {
            UWORD words[6]={0,0,0,0,0,0};

            for(int pixel=0;pixel<16;++pixel)
            {
                UWORD resolved;
                UWORD target = _ham6Rgb444[(size_t)y * width + blockX + pixel];
                UBYTE code = frfHam6EncodePixel(target,previous,base,&resolved);
                UWORD mask = (UWORD)(0x8000U >> pixel);
                previous = resolved;

                for(int plane=0;plane<6;++plane)
                    if(code & (1U << plane)) words[plane] |= mask;
            }

            ULONG offset =
                (ULONG)(destinationY + y) * bytesPerRow +
                destinationByteX +
                ((ULONG)blockX >> 3);

            for(int plane=0;plane<6;++plane)
                *((UWORD *)(destination->Planes[plane] + offset)) = words[plane];
        }
    }

    for(int pen=0;pen<16;++pen)
    {
        UWORD colour = base[pen];
        SetRGB4(
            &(_pScreen->ViewPort),
            pen,
            (colour >> 8) & 15,
            (colour >> 4) & 15,
            colour & 15);
    }

    static int logged = 0;
    if(!logged)
    {
        logged = 1;
        printf(
            "FRF HAM6 STATIC engine: %dx%d, %d fps, single buffer, RGB444 -> 6 planes\n",
            width,height,requestedFps);
    }
}

void Intuition_Screen_OS3::draw(_mame_display *display)
{
    /* FRF_FIX75_V4_DRAW_ROUTE_FIRST */
    if(_frfFix75V4HamOutput &&
       _frfFix75V4HamOutput->active())
    {
        _frfFix75V4HamOutput->draw(display);
        return;
    }

    if(_ham6Output && _ham6Output->active())
    {
        _ham6Output->draw(display);
        return;
    }

    if(_ham6Mode)
    {
        drawHAM6(display);
        return;
    }

    // WritePixelArrays is OS3.0, We could use WriteChunkyPixels which is OS3.1.
    // note: all (even OS3.2.x) AGA/ECS versions are damn slow, must use patch blazewcp (not new WPA)


#ifdef ACTIVATE_OWN_C2P
    ULONG destflags = GetBitMapAttr(_drawable.bitmap(),BMA_FLAGS);
    if( (destflags & BMF_STANDARD) != 0 &&
        (_drawable.flags() & DISPFLAG_USEHEIGHTBUFFER) == 0 )
    {
        // Native fullscreen only. RTG and SVP height-buffer modes keep their
        // established paths in V1.
        Drawable_OS3::draw_c2p(display);
    } else
#endif
    if(GfxBase->LibNode.lib_Version>=40)
    {
        Drawable_OS3::draw_WriteChunkyPixels(display);
    } else
    if(GfxBase->LibNode.lib_Version>=36)
    {
        Drawable_OS3::draw_WPA8(display);
    } else {
        return;
    }

   if(_pTripleBufferImpl) _pTripleBufferImpl->afterBufferDrawn();

   // double buffer that use scroll is patched here:
   if(_flags & DISPFLAG_USEHEIGHTBUFFER) {
        if(_pScreen)
        {
            _pScreen->ViewPort.DyOffset = ((_heightBufferSwitch)?_heightBufferSwitchApplied:0);
            ScrollVPort(&(_pScreen->ViewPort));
        }
        _heightBufferSwitch^=1;
   }

}

// - - -- - - -- - - -
Intuition_Window_OS3::Intuition_Window_OS3(const AbstractDisplay::params &params)
    : Intuition_Window(params), Drawable_OS3((IntuitionDrawable&)*this)
{
    _colorsIndexLength = params._colorsIndexLength;
    _video_attributes = params._video_attributes;
}
Intuition_Window_OS3::~Intuition_Window_OS3()
{

}
// open() is  Intuition_Window::open()
bool Intuition_Window_OS3::open()
{
    if(_pWbWindow) return true; // already ok
    bool ok = Intuition_Window::open();
    if(!ok) return false;

    // if any clut or RGB15 only:
    initRemapTable();

    return true;
}

void Intuition_Window_OS3::close()
{
    Intuition_Window::close();
    Drawable_OS3::close();
}
void Intuition_Window_OS3::draw(_mame_display *display)
{
   // printf("Intuition_Window_OS3::draw:\n");
     if(!_pWbWindow) return;
    // would draw a OS3 window on workbench possibly AGA or CGX 8Bit.
    // will draw to the current size.
    _widthtarget = (int)(_pWbWindow->GZZWidth);
    _heighttarget = (int)(_pWbWindow->GZZHeight);

    _screenshiftx = 0; // because rastport is inside window
    _screenshifty = 0;
    // will draw on friend bitmap _sWbWinSBitmap.
    //printf("GfxBase->LibNode.lib_Version %d\n",GfxBase->LibNode.lib_Version);
    if(GfxBase->LibNode.lib_Version>=40)
    {
        Drawable_OS3::draw_WriteChunkyPixels(display);
    } else
    if(GfxBase->LibNode.lib_Version>=36)
    {
        Drawable_OS3::draw_WPA8(display);
    } else {
        return;
    }

}

/*
 * Classic Amiga graphics at their finest! 🖥️
 *      ___
 *     /   \
 *    | O O |  <- This dolphin swims through planar bitmaps!
 *     \___/
 *      / \
 */
