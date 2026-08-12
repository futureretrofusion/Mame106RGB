/*
 * amiga_video_remap.cpp
 * Purpose: Palette remapping and color space conversion
 *
 * ╔════════════════════════════════════════════════════════════════════════╗
 * ║              🎨 PALETTE REMAPPER & COLOR CONVERTER 🌈                 ║
 * ║  ┌──────────────────────────────────────────────────────────────┐    ║
 * ║  │  MAME Palette ──► Remap ──► Amiga Palette                    │    ║
 * ║  │  [R8 G8 B8] ─────────────► [Native Format]                   │    ║
 * ║  │                                                               │    ║
 * ║  │  Handles CLUT8, CLUT16, and true-color conversions            │    ║
 * ║  └──────────────────────────────────────────────────────────────┘    ║
 * ╚════════════════════════════════════════════════════════════════════════╝
 *
 * Author: krb
 * Copyright (C) 2025
 * Licensed under GPL v2
 */

//#define LOADPALETTE 1
#include "amiga_video_remap.h"
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <proto/graphics.h>

#include "amiga_video_tracers_clut16.h"
extern "C" {
    // for pixel formats
    #include <cybergraphx/cybergraphics.h>
    #include <intuition/screens.h>
    #include <graphics/displayinfo.h>
}

extern "C" {
    // for pixel formats
    #include "mamecore.h"
    #include "video.h"
}
#include <vector>


/* FRF89I_RUNTIME_GREYSCALE_REMAP
 * Native 5-plane greyscale is implemented at the remap boundary. This keeps
 * the existing c2p5/c2p6 assembly and pen packing untouched.
 */
static ULONG frf89_gray24(ULONG colour)
{
    ULONG r = (colour >> 16) & 255UL;
    ULONG g = (colour >> 8) & 255UL;
    ULONG b = colour & 255UL;
    ULONG y = (3UL*r + 6UL*g + b + 5UL) / 10UL;
    return (y << 16) | (y << 8) | y;
}

extern int frf89_color_mode; /* FRF89I_TRANSACTIONAL_HAM_EHB_RGB32_GREY_HOTKEY */
using namespace std;

Paletted::Paletted() : _needFirstRemap(1)
{

}

Paletted_CGX::Paletted_CGX(int colorsIndexLength, int screenPixFmt, int bytesPerPix)
    : Paletted(), _pixFmt(screenPixFmt),_bytesPerPix(bytesPerPix)
{
    switch(bytesPerPix){
        case 1: _clut8.reserve(colorsIndexLength); break;
        case 2: _clut16.reserve(colorsIndexLength); break;
        case 3: case 4: _clut32.reserve(colorsIndexLength); break;
    }
}
Paletted_CGX::~Paletted_CGX(){}
// almost true color, used by neogeo
void Paletted_CGX::updatePaletteRemap15b()
{
    if(!_needFirstRemap) return;  // done once for all.

    const int nbremap = 32768;
    if(_needFirstRemap)
    {
        switch(_bytesPerPix){
            case 1: if(_clut8.size()<nbremap) _clut8.resize(nbremap); break;
            case 2: if(_clut16.size()<nbremap) _clut16.resize(nbremap); break;
            case 3: case 4: if(_clut32.size()<nbremap) _clut32.resize(nbremap);
             break;
        }
    }
    _needFirstRemap =0;
    USHORT *p16a= _clut16.data();

    if(_pixFmt == PIXFMT_RGB15)
    {
        // special case: same format, should be a copy...
        for(UWORD i=0;i<32768;i++) *p16a++ = i;
    }else
    if(_pixFmt == PIXFMT_RGB15PC)
    {
        // special case,almost same format
        for(UWORD i=0;i<32768;i++) *p16a++ = ((i>>8)&0x00ff)|((i<<8)&0xff00);
    }else
    {   // other formats 16b
        if(_bytesPerPix == 2)
        for(UWORD i=0;i<32768;i++)
        {
            UWORD r = i>>10;
            UWORD g= (i>>5)& 0x1f;
            UWORD b= i& 0x1f;

            switch(_pixFmt)
            {
                case PIXFMT_BGR15:
                     *p16a++ =(b<<10)|(g<<5)|(r);
                break;
                case PIXFMT_BGR15PC:
                    {
                        UWORD d=(b<<10)|(g<<5)|(r);
                        *p16a++ =((d>>8)&0x00ff)|((d<<8)&0xff00);
                    }
                break;
                case PIXFMT_RGB16:
                     *p16a++ =(r<<11)|(g<<6)|((g<<1)&0x0020)|(b);
                break;
                case PIXFMT_BGR16:
                     *p16a++ =(b<<11)|(g<<6)|((g<<1)&0x0020)|(r);
                break;
                case PIXFMT_RGB16PC:
                    {
                        UWORD d=(r<<11)|(g<<6)|((g<<1)&0x0020)|(b);
                        *p16a++ =((d>>8)&0x00ff)|((d<<8)&0xff00);
                    }
                break;
                case PIXFMT_BGR16PC:
                    {
                        UWORD d=(b<<11)|(g<<6)|((g<<1)&0x0020)|(r);
                        *p16a++ =((d>>8)&0x00ff)|((d<<8)&0xff00);
                    }
                break;
                case PIXFMT_LUT8: // no sense, should just use RGB32 os palette, do not select Display_CGX_Paletted
                            // TODO for aga lut8
                break;
            }
        } else // end loop
            // 24 and 32 bits
        if(_bytesPerPix == 3 || _bytesPerPix ==4)
        {
            ULONG *p32a= _clut32.data();

            for(ULONG i=0;i<32768;i++)
            {
                // extends component 5 to 8 the right way
                ULONG r = (ULONG)((i>>7)|(i>>12));
                ULONG g = (ULONG)((i>>2)& 0x00f8)|((i>>7)& 0x0007);
                ULONG b = (ULONG)((i<<3)& 0x00f8)|((i>>2)& 0x0007);

                switch(_pixFmt)
                {
                    case PIXFMT_BGR24: //todo but no drawer -> yes now there is.
                        break;
                    // - - -32b cases
                    case PIXFMT_RGB24:
                    case PIXFMT_ARGB32:
                        *p32a++= (r<<16)|(g<<8)|b;
                       break;
                    case PIXFMT_BGRA32:
                        *p32a++= (b<<24)|(g<<16)|(r<<8);
                       break;
                    case PIXFMT_RGBA32:
                         *p32a++= (r<<24)|(g<<16)|(b<<8);
                        break;
                    case PIXFMT_LUT8: // no sense, should just use RGB32 os palette, do not select Display_CGX_Paletted
                                // TODO for aga lut8
                    break;
                } // end switch

            } // end loop all 24 and 32 bits
        }

    } // end else not 16bspecial

}

void Paletted_CGX::updatePaletteRemap(_mame_display *display)
{
    const rgb_t *gpal1 = display->game_palette;
    const int nbc = display->game_palette_entries;
    UINT32 *pdirtrybf =	display->game_palette_dirty;
    if(_needFirstRemap)
    {
        switch(_bytesPerPix){
            case 1: if(_clut8.size()<nbc) _clut8.resize(nbc); break;
            case 2: if(_clut16.size()<nbc) _clut16.resize(nbc); break;
            case 3: case 4: if(_clut32.size()<nbc) _clut32.resize(nbc);
             break;
        }
        // on first force all dirty to have all done once.
        int nbdirstybf = (nbc+31)>>5;
        for(int i=0;i<nbdirstybf;i++) pdirtrybf[i]=~0;
    }

    USHORT *p16a= _clut16.data();
    ULONG *p32a= _clut32.data();
  //  UBYTE *pb=(UBYTE *)_clut16.data();

    for(int j=0;j<nbc;j+=32)
    {
        UINT32 dirtybf = *pdirtrybf++;
        if(!dirtybf) continue; // superfast escape.

        int i=0;
        int iend = 32;
        if(iend>(nbc-j)) iend=(nbc-j);

        const rgb_t *gpal = gpal1+j;
        USHORT *p16= p16a+j;
        ULONG *p32= p32a+j;

        switch(_pixFmt)
        {
        // - - - - -15b cases
         case PIXFMT_RGB15:
            for(;i<iend;i++) { ULONG c = *gpal++; *p16++ = ((c>>9)&0x7c00)|((c>>6)&0x03e0)|((c>>3)&0x001f); }
            break;
         case PIXFMT_BGR15:
            for(;i<iend;i++) { ULONG c = *gpal++; *p16++ = ((c<<7)&0x7c00)|((c>>6)&0x03e0)|((c>>19)&0x001f); }
            break;
         case PIXFMT_RGB15PC:
            for(;i<iend;i++) { ULONG c = *gpal++; USHORT d = ((c>>9)&0x7c00)|((c>>6)&0x03e0)|((c>>3)&0x001f);
              //  *pb++ = (UBYTE)d; d>>=8;  *pb++ = (UBYTE)d;
                *p16++ = ((d>>8)&0x00ff)|((d<<8)&0xff00);
            }
            break;
         case PIXFMT_BGR15PC:
            for(;i<iend;i++) { ULONG c = *gpal++; USHORT d = ((c<<7)&0x7c00)|((c>>6)&0x03e0)|((c>>19)&0x001f);
               // *pb++ = (UBYTE)d; d>>=8;  *pb++ = (UBYTE)d;
                *p16++ = ((d>>8)&0x00ff)|((d<<8)&0xff00);
            }
            break;
         // - -- - - - 16b cases
         case PIXFMT_RGB16:
            for(;i<iend;i++) { ULONG c = *gpal++; *p16++ = ((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&0x001f); }
            break;
         case PIXFMT_BGR16:

            for(;i<iend;i++) { ULONG c = *gpal++; USHORT d = ((c>>8)&0xf800)|(((USHORT)c>>5)&0x07e0)|(((USHORT)c>>3)&0x001f);
                *p16++ = d;
            }
            break;
         case PIXFMT_RGB16PC:
            for(;i<iend;i++)
            {
                ULONG c = *gpal++; USHORT d = (((c>>8))&0xf800)|(((USHORT)c>>5)&0x07e0)|(((USHORT)c>>3)&0x001f);
                *p16++ = ((d>>8)&0x00ff)|((d<<8)&0xff00);
            }
            break;
         case PIXFMT_BGR16PC:
            for(;i<iend;i++) { ULONG c = *gpal++; USHORT d =  ((c<<8)&0xf800)|((c>>5)&0x07e0)|((c>>19)&0x001f);

                *p16++ = ((d>>8)&0x00ff)|((d<<8)&0xff00);
            }
            break;

         case PIXFMT_BGR24:
            for(;i<iend;i++) { ULONG c = *gpal++; ULONG d = ((c>>16)&0x000000ff)|((c)&0x0000ff00)|((c<<16)&0x00ff0000);
                *p32++= d;
                }
             break;
         // - - -32b cases
         case PIXFMT_RGB24:
         case PIXFMT_ARGB32:
            // this is the id one, no need for table, direct palette use.
            for(;i<iend;i++) { *p32++= (*gpal++);}
            break;
         case PIXFMT_BGRA32:
            for(;i<iend;i++) { ULONG c = *gpal++; ULONG d = ((c>>8)&0x0000ff00)|((c<<8)&0x00ff0000)|((c<<24)&0xff000000);            *p32++= d;
            }
            break;
         case PIXFMT_RGBA32:
            for(;i<iend;i++) { *p32++= (*gpal++)<<8;}
            break;
         case PIXFMT_LUT8: // no sense, should just use RGB32 os palette, do not select Display_CGX_Paletted
            // let's consider a 2/3/3 RGB
            // no: let's consider intuition pens.
    //        for(;i<nbc;i++) { ULONG c = *gpal++; ULONG d = ((c>>16)&0x0000ff00)|((c<<8)&0x00ff0000)|((c<<24)&0xff000000);
    //            *p32++= d;
    //        }
        default:
            break;
        }
    }
    _needFirstRemap = 0;
}
// - - - - -

Paletted_Screen8::Paletted_Screen8(struct Screen *pScreen)
    : Paletted(), _pScreen(pScreen) {
}
void Paletted_Screen8::updatePaletteRemap(_mame_display *display)
{
    if(!_pScreen) return;
    //    printf("Paletted_Screen8::updatePaletteRemap\n");
    const rgb_t *gpal1 = display->game_palette;
    USHORT nbc = (USHORT)display->game_palette_entries;
    UINT32 *pdirtrybf =	display->game_palette_dirty;
    bool ignominiousColorTrick = false;
//    if(nbc==256)
//    {
//        // ignomous trick to add 2 colors for interface :( (slap fight, ...)
//        ignominiousColorTrick = true;
//        rgb_t *g  =const_cast<rgb_t *>(gpal1);
//        g[1] = 0x00ffffff;
//        nbc = 256;
//    }

    if(_needFirstRemap)
    {
        //printf("Paletted_Screen8::updatePaletteRemap _needFirstRemap\n");
        // on first force all dirty to have all done once.
        int nbdirstybf = (nbc+31)>>5;
        for(int i=0;i<nbdirstybf;i++) pdirtrybf[i]=~0;
    }
     _needFirstRemap = 0;

     int nbc1 = nbc;
     if(nbc1>256) nbc1=256;
     // the 256 first colors are used as intuition color palette.
     int j=0;
     for(;j<nbc1;j+=32)
     {
        UINT32 dirtybf = *pdirtrybf++;
        if(!dirtybf) continue; // superfast escape.

        USHORT iend = 32;
        if(iend>(nbc1-j)) iend=(nbc1-j);
        const rgb_t *gpal = gpal1+j;
        ULONG *pc = &_palette[0];
        *pc++ = (((ULONG)iend)<<16) | j; // nbumber of colors to change / palette shift.

        for(USHORT i=0;i<iend;i++) {
            ULONG c = (*gpal++);
            /* FRF89I_GREY_EXACT_INDEX */
            if((frf89_color_mode % 5) == 3)
                c = frf89_gray24(c);
            *pc++= (c<<8) & 0xff000000;
            *pc++= (c<<16) & 0xff000000;
            *pc++= (c<<24) & 0xff000000;
        }
        *pc = 0; // term.
            //    printf("LoadRGB32\n");
        LoadRGB32(&(_pScreen->ViewPort),(ULONG *) &_palette[0]); // change 1 to 32 colors at a time.
     }
}
void Paletted_Screen8::directDraw(directDrawParams *p)
{
    directDraw_UBYTE_UBYTE_UWORD(p);
}


// - - - - - - --
// remap clut with nbc  [1,256+[ to forced 8bit palette , for workbench.
Paletted_Pens8::Paletted_Pens8(struct Screen *pScreen )
    : Paletted(), _pScreen(pScreen), _screenNbc(1<<(pScreen->RastPort.BitMap->Depth))
    , _use15BitPrecision(true)
{
}
Paletted_Pens8::~Paletted_Pens8()
{
}
void Paletted_Pens8::updatePaletteRemap(_mame_display *display)
{
    if(!_pScreen) return;

    const rgb_t *gpal1 = display->game_palette;
    USHORT nbc = (USHORT)display->game_palette_entries;
    UINT32 *pdirtrybf =	display->game_palette_dirty;

    if(_needFirstRemap)
    {
        // on first force all dirty to have all done once.
        int nbdirstybf = (nbc+31)>>5;
        for(int i=0;i<nbdirstybf;i++) pdirtrybf[i]=~0;
        // also that,
        initRemapCube();
        _needFirstRemap = 0;
    }
    if(_clut8.size()<nbc) _clut8.resize(nbc,0);
    UBYTE *pclut = _clut8.data();

    if(_use15BitPrecision)
    {
        for(int j=0;j<nbc;j+=32)
        {
            UINT32 dirtybf = *pdirtrybf++;
            if(!dirtybf) continue; // superfast escape.

            USHORT iend = j+32;
            if(iend>nbc) iend=nbc;
            const rgb_t *gpal = gpal1+j;
            for(USHORT i=j;i<iend;i++) {

                if(dirtybf&1)
                {
                    ULONG c = *gpal;
                    UWORD rgb5 =  ((c>>9) & 0x7c00) | // >>16 >>3 <<10 -> >>9
                                  ((c>>6) & 0x03e0) | // >>8  >>3 <<5  -> >>6
                                  ((c>>3) & 0x001f) ; // >>0  >>3 <<0  -> >>3
                    pclut[i] =_rgbcube[rgb5];
                } // end if dirty
                dirtybf>>=1;
                gpal++;
            } // end loop per 32
         }
    } else
    {   // 12b precision
        for(int j=0;j<nbc;j+=32)
        {
            UINT32 dirtybf = *pdirtrybf++;
            if(!dirtybf) continue; // superfast escape.

            USHORT iend = j+32;
            if(iend>nbc) iend=nbc;
            const rgb_t *gpal = gpal1+j;
            for(USHORT i=j;i<iend;i++) {

                if(dirtybf&1)
                {
                    ULONG c = *gpal;
                    UWORD rgb4 = ((c>>12) & 0x0f00) |
                                  ((c>>8) & 0x00f0) |
                                  ((c>>4) & 0x000f) ;
                    pclut[i] =_rgbcube[rgb4];
                } // end if dirty
                dirtybf>>=1;
                gpal++;
            } // end loop per 32
         }
    }



}
void Paletted_Pens8::directDraw(directDrawParams *p)
{
    if(_clut8.size()==0)
    {
        return;
    }
    directDrawClut_UBYTE_UBYTE_UWORD(p,_clut8.data());
}
void Paletted_Pens8::initRemapCube()
{
    // should lock -> no, the window locks the WB.
    struct	ColorMap *pColorMap = _pScreen->ViewPort.ColorMap;

    if(!pColorMap) return;

    if(_use15BitPrecision)
    {
        _rgbcube.resize(32768);
        int colorcount = 1<<(_pScreen->RastPort.BitMap->Depth);
        if(pColorMap->Count<colorcount) colorcount = pColorMap->Count;
       // printf("initRemapCube pColorMap->Count:%d\n",pColorMap->Count);

        vector<UBYTE> pal(colorcount*3*4);
        GetRGB32(pColorMap,0,colorcount,(ULONG *)pal.data());
        // make it 32b->5b
        for(int i=0;i<colorcount*3;i++)
        {
            UBYTE v = pal[i<<2];
            pal[i] = v>>3;
        }
        // this is obviously 8 times longer than 4b precision.
        for(UWORD rgbi=0;rgbi<32768;rgbi++)
        {
            UBYTE isbest=0;
            ULONG isbesterr=0x0fffffff;
            WORD r = rgbi>>10;
            WORD g = (rgbi>>5) & 0x1f;
            WORD b = rgbi & 0x1f;

            for(UWORD i=0;i<colorcount;i++)
            {
                WORD rs = pal[i*3];
                WORD gs = pal[i*3+1];
                WORD bs = pal[i*3+2];
                // error between color is better with greater error
                LONG err = (rs-r)*(rs-r); // that makes a abs without test.
                LONG err_g = (gs-g)*(gs-g);
                LONG err_b = (bs-b)*(bs-b);
                if(err_g>err) err = err_g;
                if(err_b>err) err = err_b;

                if(err<isbesterr) {
                    isbest = i;
                    isbesterr = err;
                    if(isbesterr ==0) break;
                }
            }
            _rgbcube[rgbi] = isbest;
        }
    } else
    {
        // 16*16*16
        _rgbcube.resize(4096);

        vector<UWORD> pal(pColorMap->Count);
        for(LONG i=0;i<pColorMap->Count ; i++)
        {
           pal[i] = GetRGB4(pColorMap,i);
        }
        for(UWORD rgbi=0;rgbi<4096;rgbi++)
        {
            UBYTE isbest=0;
            ULONG isbesterr=0x0fffffff;
            WORD r = rgbi>>8;
            WORD g = (rgbi>>4) & 0x0f;
            WORD b = rgbi & 0x0f;

            for(UWORD i=0;i<pColorMap->Count;i++)
            {
                UWORD c =pal[i];
                WORD rs = c>>8;
                WORD gs = (c>>4) & 0x0f;
                WORD bs = c & 0x0f;
                // error between color is better with greater error
                LONG err = (rs-r)*(rs-r); // that makes a abs without test.
                LONG err_g = (gs-g)*(gs-g);
                LONG err_b = (bs-b)*(bs-b);
                if(err_g>err) err = err_g;
                if(err_b>err) err = err_b;

                if(err<isbesterr) {
                    isbest = i;
                    isbesterr = err;
                    if(isbesterr ==0) break;
                }
            }
            _rgbcube[rgbi] = isbest;
        }
    }
}
// - - - -
// RGB15 remap to workbench 8bit, external palette.
Paletted_Pens8_src15b::Paletted_Pens8_src15b(struct Screen *pScreen)
    :Paletted_Pens8(pScreen)
{
    initRemapCube();
    if(_use15BitPrecision)
    {
        _clut8 = _rgbcube; //lol
    } else
    {
        int nbc = 32*32*32;
        if(_clut8.size()<nbc) _clut8.resize(nbc,0);
        UBYTE *pclut = _clut8.data();
        for(int j=0;j<nbc;j++)
        {
            UWORD rgb4 = ((j>>3) & 0x0f00) |
                         ((j>>2) & 0x00f0) |
                         ((j>>1) & 0x000f) ;
            pclut[j] =_rgbcube[rgb4];
        }
    }
}
Paletted_Pens8_src15b::~Paletted_Pens8_src15b(){}
void Paletted_Pens8_src15b::updatePaletteRemap(_mame_display *display) {} // does nothing
// - - - - - - - - -
// RGB32 to remap to workbench 8bit, external palette.
Paletted_Pens8_src32b::Paletted_Pens8_src32b(struct Screen *pScreen)
    : Paletted_Pens8_src15b(pScreen)
{}
Paletted_Pens8_src32b::~Paletted_Pens8_src32b(){}
void Paletted_Pens8_src32b::directDraw(directDrawParams *p)
{
    if(_clut8.size()==0) return;
    // same as 15 bit, but use this function that does RGB32 to RGB15 to 8Bit(clut) conversion.
    directDrawClut_UBYTE_UBYTE_ARGB32(p,_clut8.data());
}
// - - - - - - - - - - -
#ifdef LOADPALETTE
#ifdef LSB_FIRST
//todo, but no need (aros ? :) Hello you.
#else
inline ULONG lbe32(const UBYTE*pbin) { return *((const ULONG *)pbin);  }
inline UWORD lbe16(const UBYTE*pbin) { return *((const UWORD *)pbin);  }
#endif
#define TAGID(a,b,c,d) ((ULONG)  ((((ULONG)a)<<24) | (((ULONG)b)<<16) | (((ULONG)c)<<8) | (((ULONG)d)) ))

const UBYTE *searchIlbmTag(const UBYTE *pbin,const UBYTE *pbinend, ULONG ID,ULONG &bsize)
{
    while(pbin<pbinend)
    {
        ULONG tagid = lbe32(pbin);
        bsize = lbe32(pbin+4);
        printf("tagid %c %c %c %c\n",((tagid>>24) & 0x0ff),((tagid>>16) & 0x0ff),((tagid>>8) & 0x0ff),tagid & 0x0ff);
        if (tagid == ID) return pbin+8;
        pbin += (bsize+8);
    }
    return NULL;

}


int loadPaletteIlbm()
{
    printf("a\n");
    FILE *fh = fopen("PROGDIR:optimumpalette.ilbm","rb");
    if(!fh) return 1;
    fseek(fh, 0, SEEK_END);
     ULONG filesize = ftell(fh);
    if(filesize ==0)
    {
         fclose(fh);
         return 1;
    }
    printf("b\n");
    fseek(fh, 0, SEEK_SET);

    std::vector<UBYTE> v(filesize);
    fread(v.data(),filesize,1,fh);
    fclose(fh);
    const UBYTE *pbin = v.data();
    ULONG formid = lbe32(pbin);
    printf("c\n");
    if(formid != TAGID('F','O','R','M')) return 1;
    pbin +=4;
    ULONG formidsize = lbe32(pbin);
    pbin +=4;
    const UBYTE *pbinend = pbin + formidsize;
    ULONG ilbm = lbe32(pbin);
    pbin +=4;
    if(ilbm != TAGID('I','L','B','M')) return 1;
    printf("d\n");
    ULONG cmapSize=0;
    pbin = searchIlbmTag(pbin,pbinend,TAGID('C','M','A','P'),cmapSize);
    if(!pbin) return 1;
    printf("nbc3size:%d\n",cmapSize);
    if(cmapSize>768) cmapSize=768; // brillance can adds work colors.

    int nb=0;
    for(ULONG i=0;i<cmapSize;i++)
    {
        UBYTE b = *pbin++;
        cout << (int) b << ",";
        nb++;
        if(nb>=48) {
            cout << "\n";
            nb=0;
        }
    }
    cout << endl;


    return 0;
}
#endif
// when screen8 and nbc>258, force our palette and use Paletted_Screen8 like on WB.
Paletted_Screen8ForcePalette::Paletted_Screen8ForcePalette(struct Screen *pScreen)
    : Paletted_Pens8(pScreen) {
}
extern "C" { extern const unsigned char fixedpal8[768]; }
void Paletted_Screen8ForcePalette::initRemapCube()
{
    // set fixed palette

    initFixedPalette(fixedpal8,_screenNbc);
    // then... like for 8bit WB windows, use 12bit precision remap. could be 15.-
    Paletted_Pens8::initRemapCube();

}
void Paletted_Screen8ForcePalette::initFixedPalette(const UBYTE *prgb,ULONG nbc)
{
    if(nbc==0) return;
    // to RGB32
    ULONG paletteRGB32[3*32+2]; // LOADRGB32 format, used to load colors per 32.
    if(nbc>256) nbc=256;
    // the 256 first colors are used as intuition color palette.
    int stride = (256*3/nbc);

    int j=0;
    for(;j<nbc;j+=32)
    {
        USHORT iend = 32;
        if(iend>(nbc-j)) iend=(nbc-j);
        const UBYTE *gpal = prgb+(j*stride);
        ULONG *pc = &paletteRGB32[0];
        *pc++ = (((ULONG)iend)<<16) | j; // number of colors to change / palette shift.

        for(USHORT i=0;i<iend;i++) {
            /* FRF89I_GREY_FORCEPALETTE */
            ULONG rr = (ULONG)gpal[0];
            ULONG gg = (ULONG)gpal[1];
            ULONG bb = (ULONG)gpal[2];
            if((frf89_color_mode % 5) == 3)
            {
                ULONG yy = (3UL*rr + 6UL*gg + bb + 5UL) / 10UL;
                rr = yy; gg = yy; bb = yy;
            }
            *pc++ = rr << 24;
            *pc++ = gg << 24;
            *pc++ = bb << 24;
            gpal += stride;
        }
        *pc = 0; // term.
        LoadRGB32(&(_pScreen->ViewPort),(ULONG *) &paletteRGB32[0]); // change 1 to 32 colors at a time.
    }
}

Paletted_Screen8ForcePalette_15b::Paletted_Screen8ForcePalette_15b(struct Screen *pScreen)
    :Paletted_Screen8ForcePalette(pScreen)
{
}
void Paletted_Screen8ForcePalette_15b::initRemapCube()
{
    Paletted_Screen8ForcePalette::initRemapCube();
    if(_use15BitPrecision)
    {
        _clut8 = _rgbcube; //lol
    } else
    {
        int nbc = 32*32*32;
        if(_clut8.size()<nbc) _clut8.resize(nbc,0);
        UBYTE *pclut = _clut8.data();
        for(int j=0;j<nbc;j++)
        {
            UWORD rgb4 = ((j>>3) & 0x0f00) |
                         ((j>>2) & 0x00f0) |
                         ((j>>1) & 0x000f) ;
            pclut[j] =_rgbcube[rgb4];
        }
    }
}
void Paletted_Screen8ForcePalette_15b::directDraw(directDrawParams *p)
{
    if(_clut8.size()==0) initRemapCube(); // will loadrgb32 at first draw.
    if(_clut8.size()==0)  return;

    directDrawClut_UBYTE_UBYTE_UWORD(p,_clut8.data());
}
// - - - -

// for 15bit RGB just does clut to
// void Paletted_Screen8ForcePalette_15b::updatePaletteRemap(_mame_display *display)
// {   // finnaly, init in constructor, no color will change, updatePaletteRemap not used.
// }
// --------------------------------------
Paletted_Screen8ForcePalette_32b::Paletted_Screen8ForcePalette_32b(struct Screen *pScreen)
    : Paletted_Screen8ForcePalette_15b(pScreen)
{
}

void Paletted_Screen8ForcePalette_32b::directDraw(directDrawParams *p)
{
    if(_clut8.size()==0)
    {
     initRemapCube(); // will loadrgb32 at first draw.
    }
    if(_clut8.size()==0) return;
    // same as 15 bit, but uses this function that does RGB32 to RGB15 conversion.
    directDrawClut_UBYTE_UBYTE_ARGB32(p,_clut8.data());
}


// ---------------------------------------------------------------------------
// FRF Mame106RGB V3: weighted game-derived private-screen palette.
// ---------------------------------------------------------------------------
static ULONG frfOptPaletteDistance(ULONG a, ULONG b)
{
    LONG dr = (LONG)((a >> 16) & 0xff) - (LONG)((b >> 16) & 0xff);
    LONG dg = (LONG)((a >> 8) & 0xff) - (LONG)((b >> 8) & 0xff);
    LONG db = (LONG)(a & 0xff) - (LONG)(b & 0xff);

    return (ULONG)(3 * dr * dr + 6 * dg * dg + 2 * db * db);
}

static int frfOptNearest(const std::vector<ULONG> &centres, ULONG color)
{
    int best = 0;
    ULONG bestDistance = 0xffffffffUL;

    for(int i=0; i<(int)centres.size(); ++i)
    {
        ULONG d = frfOptPaletteDistance(color,centres[i]);
        if(d < bestDistance)
        {
            bestDistance = d;
            best = i;
            if(d == 0) break;
        }
    }
    return best;
}


// ---------------------------------------------------------------------------
// FRF Mame106RGB V3L EHB + PALETTE LAB
// ---------------------------------------------------------------------------
static int frfOptPaletteModeOverride = -1;
static ULONG frfOptPaletteGeneration = 1;

static const char *frfOptPaletteModeName(int mode)
{
    static const char *names[4] =
    {
        "Balanced",
        "Even Coverage",
        "Current Scene",
        "Vivid Arcade"
    };
    if(mode < 0 || mode > 3) mode = 0;
    return names[mode];
}

static int frfOptPaletteRequestedMode(void)
{
    if(frfOptPaletteModeOverride >= 0)
        return frfOptPaletteModeOverride;

    int mode = 0;
    const char *value = getenv("FRF_OPT_PALETTE_MODE");
    if(value && value[0]) mode = atoi(value);
    if(mode < 0) mode = 0;
    if(mode > 3) mode = 3;
    return mode;
}

extern "C" void frfOptPaletteCycle(int direction)
{
    int mode = frfOptPaletteRequestedMode();
    if(direction < 0)
        mode = (mode + 3) & 3;
    else
        mode = (mode + 1) & 3;

    frfOptPaletteModeOverride = mode;
    frfOptPaletteGeneration++;
    if(!frfOptPaletteGeneration) frfOptPaletteGeneration = 1;

    printf(
        "FRF PALETTE MODE %d: %s -- rebuilding native palette\\n",
        mode,
        frfOptPaletteModeName(mode));
}

static ULONG frfOptQuantize444(ULONG colour)
{
    ULONG r = (colour >> 20) & 15;
    ULONG g = (colour >> 12) & 15;
    ULONG b = (colour >> 4) & 15;
    r = (r << 4) | r;
    g = (g << 4) | g;
    b = (b << 4) | b;
    return (r << 16) | (g << 8) | b;
}

static ULONG frfOptHalfBright444(ULONG colour)
{
    ULONG r = ((colour >> 20) & 15) >> 1;
    ULONG g = ((colour >> 12) & 15) >> 1;
    ULONG b = ((colour >> 4) & 15) >> 1;
    r = (r << 4) | r;
    g = (g << 4) | g;
    b = (b << 4) | b;
    return (r << 16) | (g << 8) | b;
}

static ULONG frfOptLiftForHalf(ULONG colour)
{
    ULONG r = (colour >> 20) & 15;
    ULONG g = (colour >> 12) & 15;
    ULONG b = (colour >> 4) & 15;
    r = r < 8 ? r << 1 : 15;
    g = g < 8 ? g << 1 : 15;
    b = b < 8 ? b << 1 : 15;
    r = (r << 4) | r;
    g = (g << 4) | g;
    b = (b << 4) | b;
    return (r << 16) | (g << 8) | b;
}

static int frfOptRgb555(ULONG colour)
{
    return
        (int)(((colour >> 19) & 31) << 10) |
        (int)(((colour >> 11) & 31) << 5) |
        (int)((colour >> 3) & 31);
}

static ULONG frfOptFromRgb555(int value)
{
    ULONG r = (value >> 10) & 31;
    ULONG g = (value >> 5) & 31;
    ULONG b = value & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

static ULONG frfOptChroma(ULONG colour)
{
    ULONG r = (colour >> 16) & 255;
    ULONG g = (colour >> 8) & 255;
    ULONG b = colour & 255;
    ULONG maximum = r;
    ULONG minimum = r;
    if(g > maximum) maximum = g;
    if(b > maximum) maximum = b;
    if(g < minimum) minimum = g;
    if(b < minimum) minimum = b;
    return maximum - minimum;
}

static ULONG frfOptBaseWeight(int mode,ULONG colour)
{
    if(mode == 1) return 64;
    if(mode == 2) return 1;
    if(mode == 3) return 8 + (frfOptChroma(colour) >> 3);
    return 8;
}

static ULONG frfOptSceneWeight(int mode,ULONG colour)
{
    if(mode == 1) return 0;
    if(mode == 2) return 64;
    if(mode == 3) return 12 + (frfOptChroma(colour) >> 4);
    return 8;
}

static void frfOptAddWeight(std::vector<ULONG> &histogram,int bin,ULONG amount)
{
    if(bin < 0 || bin >= (int)histogram.size() || !amount) return;
    ULONG old = histogram[bin];
    ULONG value = old + amount;
    if(value < old || value > 0x7fffffffUL) value = 0x7fffffffUL;
    histogram[bin] = value;
}

static int frfOptNearestEffective(
    const std::vector<ULONG> &base,
    ULONG colour,
    int ehb)
{
    int best = 0;
    ULONG bestDistance = 0xffffffffUL;
    int count = (int)base.size();

    for(int i=0;i<count;++i)
    {
        ULONG d = frfOptPaletteDistance(colour,base[i]);
        if(d < bestDistance)
        {
            bestDistance = d;
            best = i;
            if(!d) break;
        }
    }

    if(ehb && bestDistance)
    {
        for(int i=0;i<count;++i)
        {
            ULONG half = frfOptHalfBright444(base[i]);
            ULONG d = frfOptPaletteDistance(colour,half);
            if(d < bestDistance)
            {
                bestDistance = d;
                best = count + i;
                if(!d) break;
            }
        }
    }

    return best;
}

static void frfOptBuildEffective(
    const std::vector<ULONG> &base,
    int ehb,
    std::vector<ULONG> &effective)
{
    effective = base;
    if(ehb)
    {
        effective.reserve(base.size() * 2);
        for(int i=0;i<(int)base.size();++i)
            effective.push_back(frfOptHalfBright444(base[i]));
    }
}

static int frfOptContains(
    const std::vector<ULONG> &colours,
    ULONG colour)
{
    for(int i=0;i<(int)colours.size();++i)
        if(colours[i] == colour) return 1;
    return 0;
}

static void frfOptLoadBasePalette(
    struct Screen *screen,
    const std::vector<ULONG> &base)
{
    if(!screen) return;
    for(int first=0;first<(int)base.size();first+=32)
    {
        ULONG paletteRGB32[3*32+2];
        int count = (int)base.size() - first;
        if(count > 32) count = 32;
        ULONG *output = paletteRGB32;
        *output++ = (((ULONG)count) << 16) | (ULONG)first;
        for(int i=0;i<count;++i)
        {
            ULONG colour = base[first+i];
            *output++ = (colour << 8) & 0xff000000UL;
            *output++ = (colour << 16) & 0xff000000UL;
            *output++ = (colour << 24) & 0xff000000UL;
        }
        *output = 0;
        LoadRGB32(&(screen->ViewPort),paletteRGB32);
    }
}

Paletted_Screen8Optimized::Paletted_Screen8Optimized(
    struct Screen *pScreen)
    : Paletted_Pens8(pScreen)
    , _frfStableReady(0)
    , _frfStableRelinkLogged(0)
    , _frfEhbMode(0)
    , _frfPaletteGenerationSeen(0)
    , _frfPaletteModeSeen(-1)
{
    if(pScreen)
    {
        ULONG modeId = GetVPModeID(&(pScreen->ViewPort));
        if(modeId != INVALID_ID)
        {
            struct DisplayInfo displayInfo;
            LONG got = GetDisplayInfoData(
                NULL,(UBYTE *)&displayInfo,sizeof(displayInfo),
                DTAG_DISP,modeId);
            if(got > 0 &&
               (displayInfo.PropertyFlags & DIPF_IS_EXTRAHALFBRITE))
            {
                int rejected = 0;
#ifdef DIPF_IS_AA
                if(displayInfo.PropertyFlags & DIPF_IS_AA) rejected = 1;
#endif
#ifdef DIPF_IS_FOREIGN
                if(displayInfo.PropertyFlags & DIPF_IS_FOREIGN) rejected = 1;
#endif
                if(!rejected) _frfEhbMode = 1;
            }
        }
    }
}

int Paletted_Screen8Optimized::needRemap() const
{
    return _needFirstRemap ||
           _frfPaletteGenerationSeen != frfOptPaletteGeneration;
}

/*
 * FRF Mame106RGB V3L EHB PALETTE LAB
 *
 * Ordinary 16/32/64-colour modes receive one of four selectable palette
 * distributions. EHB receives 32 base colours plus the corresponding 32
 * hardware half-bright colours, and source pixels are mapped to all 64 pens.
 */
void Paletted_Screen8Optimized::updatePaletteRemap(
    _mame_display *display)
{
    if(!_pScreen || !display || !display->game_palette)
        return;

    const int sourceCount = display->game_palette_entries;
    if(sourceCount <= 0 || sourceCount > 65536)
    {
        printf(
            "FRF OPTPAL ERROR: invalid source palette count %d\\n",
            sourceCount);
        _needFirstRemap = 0;
        return;
    }

    int baseCount = _frfEhbMode ? 32 : _screenNbc;
    if(baseCount > 256) baseCount = 256;
    if(baseCount < 2) baseCount = 2;
    const int effectiveCount = _frfEhbMode ? baseCount * 2 : baseCount;

    int paletteMode = frfOptPaletteRequestedMode();
    ULONG generation = frfOptPaletteGeneration;

    if(_frfPaletteGenerationSeen != generation ||
       _frfPaletteModeSeen != paletteMode)
    {
        _frfStableReady = 0;
        _frfStableRelinkLogged = 0;
    }

    const char *lockValue = getenv("FRF_OPT_PALETTE_LOCK");
    /*
     * FRF Mame106RGB V3L FIX3 EXACT V3F DYNAMICSAFE
     *
     * EHB always keeps its calculated native 32+32 palette fixed.
     * MAME dirty palette entries are relinked to those fixed pens exactly
     * as in V3F. Do not freeze a source index forever and do not rebuild or
     * reorder the native hardware palette between frames.
     */
    const bool lockNativePalette =
        _frfEhbMode ? true :
        !(lockValue && lockValue[0] == '0');

    std::vector<ULONG> sourceColours(sourceCount);
    for(int i=0;i<sourceCount;++i)
        sourceColours[i] =
            ((ULONG)display->game_palette[i]) & 0x00ffffffUL;
    /* FRF89I_GREY_OPTIMIZED */
    if((frf89_color_mode % 5) == 3)
    {
        for(int i=0;i<sourceCount;++i)
            sourceColours[i] = frf89_gray24(sourceColours[i]);
    }


    if(lockNativePalette &&
       _frfStableReady &&
       (int)_frfStableCentres.size() == effectiveCount)
    {
        const int oldClutSize = (int)_clut8.size();
        if(oldClutSize < sourceCount) _clut8.resize(sourceCount,0);

        const UINT32 *dirtyWords = display->game_palette_dirty;
        const bool rebuildAll = oldClutSize < sourceCount || dirtyWords == NULL;
        const int wordCount = (sourceCount + 31) >> 5;
        int changedEntries = 0;

        for(int word=0;word<wordCount;++word)
        {
            UINT32 dirtyBits = rebuildAll ? 0xffffffffUL : dirtyWords[word];
            if(!dirtyBits) continue;
            int first = word << 5;
            int count = sourceCount - first;
            if(count > 32) count = 32;
            for(int bit=0;bit<count;++bit)
            {
                if(dirtyBits & (1UL << bit))
                {
                    int index = first + bit;
                    int nearest = frfOptNearest(
                        _frfStableCentres,
                        sourceColours[index]);
                    if(nearest < 0 || nearest >= effectiveCount) nearest = 0;
                    _clut8[index] = (UBYTE)nearest;
                    changedEntries++;
                }
            }
        }

        _needFirstRemap = 0;
        _frfPaletteGenerationSeen = generation;
        _frfPaletteModeSeen = paletteMode;

        if(!_frfStableRelinkLogged && changedEntries)
        {
            _frfStableRelinkLogged = 1;
            printf(
                "FRF OPTPAL V3L dirty relink entries=%d palette=%d\\n",
                changedEntries,sourceCount);
        }
        return;
    }

    std::vector<ULONG> histogram(32768,0);
    for(int i=0;i<sourceCount;++i)
    {
        int bin = frfOptRgb555(sourceColours[i]);
        if(!histogram[bin])
            histogram[bin] = frfOptBaseWeight(paletteMode,sourceColours[i]);
    }

    ULONG sampledPixels = 0;
    mame_bitmap *bitmap = display->game_bitmap;
    if(bitmap && bitmap->line && bitmap->width > 0 && bitmap->height > 0)
    {
        int minx = display->game_visible_area.min_x;
        int maxx = display->game_visible_area.max_x;
        int miny = display->game_visible_area.min_y;
        int maxy = display->game_visible_area.max_y;
        if(minx < 0) minx = 0;
        if(miny < 0) miny = 0;
        if(maxx >= bitmap->width) maxx = bitmap->width - 1;
        if(maxy >= bitmap->height) maxy = bitmap->height - 1;

        int step = 2;
        const char *stepValue = getenv("FRF_OPT_PALETTE_SAMPLE_STEP");
        if(stepValue && stepValue[0]) step = atoi(stepValue);
        if(step < 1) step = 1;
        if(step > 8) step = 8;

        if(bitmap->depth <= 8)
        {
            for(int y=miny;y<=maxy;y+=step)
            {
                const UBYTE *row = (const UBYTE *)bitmap->line[y];
                for(int x=minx;x<=maxx;x+=step)
                {
                    ULONG index = row[x];
                    if(index < (ULONG)sourceCount)
                    {
                        ULONG colour = sourceColours[index];
                        frfOptAddWeight(
                            histogram,
                            frfOptRgb555(colour),
                            frfOptSceneWeight(paletteMode,colour));
                        sampledPixels++;
                    }
                }
            }
        }
        else if(bitmap->depth <= 16)
        {
            for(int y=miny;y<=maxy;y+=step)
            {
                const UWORD *row = (const UWORD *)bitmap->line[y];
                for(int x=minx;x<=maxx;x+=step)
                {
                    ULONG index = row[x];
                    if(index < (ULONG)sourceCount)
                    {
                        ULONG colour = sourceColours[index];
                        frfOptAddWeight(
                            histogram,
                            frfOptRgb555(colour),
                            frfOptSceneWeight(paletteMode,colour));
                        sampledPixels++;
                    }
                }
            }
        }
        else
        {
            for(int y=miny;y<=maxy;y+=step)
            {
                const ULONG *row = (const ULONG *)bitmap->line[y];
                for(int x=minx;x<=maxx;x+=step)
                {
                    ULONG colour = row[x] & 0x00ffffffUL;
                    frfOptAddWeight(
                        histogram,
                        frfOptRgb555(colour),
                        frfOptSceneWeight(paletteMode,colour));
                    sampledPixels++;
                }
            }
        }
    }

    std::vector<ULONG> colours;
    std::vector<ULONG> weights;
    colours.reserve(32768);
    weights.reserve(32768);
    for(int bin=0;bin<32768;++bin)
    {
        if(histogram[bin])
        {
            colours.push_back(frfOptFromRgb555(bin));
            weights.push_back(histogram[bin]);
        }
    }
    if(colours.empty())
    {
        colours.push_back(0);
        weights.push_back(1);
    }

    std::vector<ULONG> base;
    base.reserve(baseCount);
    base.push_back(0x00000000UL);
    if(baseCount > 1) base.push_back(0x00ffffffUL);

    while((int)base.size() < baseCount)
    {
        unsigned long long bestScore = 0;
        int bestIndex = -1;
        ULONG bestCandidate = 0;

        for(int i=0;i<(int)colours.size();++i)
        {
            int nearest = frfOptNearestEffective(base,colours[i],_frfEhbMode);
            ULONG represented = nearest < (int)base.size() ?
                base[nearest] :
                frfOptHalfBright444(base[nearest-(int)base.size()]);
            ULONG distance = frfOptPaletteDistance(colours[i],represented);
            ULONG importance = 1 + (weights[i] >> 4);
            if(importance > 4096) importance = 4096;
            unsigned long long score =
                (unsigned long long)(distance + 1) * importance;

            ULONG candidate = frfOptQuantize444(colours[i]);
            if(_frfEhbMode)
            {
                ULONG r = (colours[i] >> 16) & 255;
                ULONG g = (colours[i] >> 8) & 255;
                ULONG b = colours[i] & 255;
                ULONG luminance = (3*r + 6*g + b) / 10;
                if(luminance < 144)
                    candidate = frfOptLiftForHalf(colours[i]);
                if(frfOptContains(base,candidate))
                    candidate = frfOptQuantize444(colours[i]);
            }

            if(!frfOptContains(base,candidate) && score > bestScore)
            {
                bestScore = score;
                bestIndex = i;
                bestCandidate = candidate;
            }
        }

        if(bestIndex >= 0)
            base.push_back(bestCandidate);
        else
        {
            ULONG shade = (ULONG)(((int)base.size() * 255) / (baseCount - 1));
            base.push_back(frfOptQuantize444(
                (shade << 16) | (shade << 8) | shade));
        }
    }

    for(int pass=0;pass<5;++pass)
    {
        std::vector<unsigned long long> sumR(baseCount,0);
        std::vector<unsigned long long> sumG(baseCount,0);
        std::vector<unsigned long long> sumB(baseCount,0);
        std::vector<unsigned long long> sumW(baseCount,0);

        for(int i=0;i<(int)colours.size();++i)
        {
            int effective = frfOptNearestEffective(base,colours[i],_frfEhbMode);
            int slot = effective % baseCount;
            ULONG target = effective >= baseCount ?
                frfOptLiftForHalf(colours[i]) :
                frfOptQuantize444(colours[i]);
            unsigned long long weight = weights[i];
            sumR[slot] += ((target >> 16) & 255) * weight;
            sumG[slot] += ((target >> 8) & 255) * weight;
            sumB[slot] += (target & 255) * weight;
            sumW[slot] += weight;
        }

        for(int slot=2;slot<baseCount;++slot)
        {
            if(sumW[slot])
            {
                ULONG r = (ULONG)(sumR[slot] / sumW[slot]);
                ULONG g = (ULONG)(sumG[slot] / sumW[slot]);
                ULONG b = (ULONG)(sumB[slot] / sumW[slot]);
                base[slot] = frfOptQuantize444((r << 16) | (g << 8) | b);
            }
        }
    }

    std::vector<ULONG> effective;
    frfOptBuildEffective(base,_frfEhbMode,effective);
    frfOptLoadBasePalette(_pScreen,base);

    _clut8.resize(sourceCount,0);
    for(int i=0;i<sourceCount;++i)
    {
        int nearest = frfOptNearest(effective,sourceColours[i]);
        if(nearest < 0 || nearest >= effectiveCount) nearest = 0;
        _clut8[i] = (UBYTE)nearest;
    }

    _frfStableCentres = effective;
    _frfStableReady = 1;
    _frfStableRelinkLogged = 0;
    _frfPaletteGenerationSeen = generation;
    _frfPaletteModeSeen = paletteMode;
    _needFirstRemap = 0;

    printf(
        "FRF OPTPAL V3L %s mode=%d:%s source=%d base=%d effective=%d sampled=%lu\\n",
        _frfEhbMode ? "EHB6" : (lockNativePalette ? "LOCKED" : "DYNAMIC"),
        paletteMode,
        frfOptPaletteModeName(paletteMode),
        sourceCount,
        baseCount,
        effectiveCount,
        sampledPixels);
}


/*
 * Colors remapped perfectly! Every shade matched! 🎨
 *      ,___,
 *     [O.o]  <- This parrot knows all the colors!
 *     /)__)
 *    -"--"-
 */

