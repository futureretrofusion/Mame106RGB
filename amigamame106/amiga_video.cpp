/*
 * amiga_video.cpp
 * Purpose: Main video system coordinator and abstraction layer
 *
 * ╔════════════════════════════════════════════════════════════════════════╗
 * ║                 🖥️  VIDEO SYSTEM COORDINATOR 📺                        ║
 * ║  ┌──────────────────────────────────────────────────────────────┐    ║
 * ║  │                                                               │    ║
 * ║  │   MAME Video ──► [Video Manager] ──┬──► CGX (RTG)           │    ║
 * ║  │                                     ├──► OS3 (AGA/ECS)       │    ║
 * ║  │                                     └──► Intuition           │    ║
 * ║  │                                                               │    ║
 * ║  │   Handles:                                                    │    ║
 * ║  │   • Screen mode selection                                     │    ║
 * ║  │   • Palette remapping                                         │    ║
 * ║  │   • Scaling & filtering                                       │    ║
 * ║  │   • Multiple backend support                                  │    ║
 * ║  └──────────────────────────────────────────────────────────────┘    ║
 * ║         Your window to arcade perfection!                             ║
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

// Amiga includes, proto manages __cplusplus__
//#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/graphics.h>
//#include <proto/cybergraphics.h>
extern "C" {
#include "input.h" /* FRF89I_TRANSACTIONAL_HAM_EHB_RGB32_GREY_HOTKEY C_LINKAGE */
}
#include <proto/intuition.h>

#include <proto/utility.h>

extern "C" {
    #include <exec/types.h>
    #include <exec/memory.h>

    #include <graphics/gfxbase.h>
    #include <graphics/rastport.h>
    #include <graphics/modeid.h>

    #include <intuition/intuition.h>
    #include <intuition/screens.h>
}
#include "intuiuncollide.h"
// from mame
extern "C" {
    #include "osdepend.h"
    #include "video.h"
    // for logerror
    #include "mame.h"
    // for orientation flags
    #include "driver.h"
    #include "memory.h"
}
#include "amiga_inputs.h"
#include "amiga_inputs_kbd_ll.h"
#include "amiga_config.h"
#include "amiga_video.h"
#include "amiga_video_intuition.h"

/** some abstact display management */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <graphics/displayinfo.h> /* FRF89I_TRANSACTIONAL_HAM_EHB_RGB32_GREY_HOTKEY */
extern "C" {
#include "usrintrf.h" /* FRF91B_5MODE_BIDIRECTIONAL_OVERLAY C_LINKAGE */
}

AbstractDisplay::AbstractDisplay() {
}
AbstractDisplay::~AbstractDisplay(){}

// - - - - from driver.h
/* is the video hardware raser or vector base? */
#define	VIDEO_TYPE_RASTER				0x0000
#define	VIDEO_TYPE_VECTOR				0x0001

/* should VIDEO_UPDATE by called at the start of VBLANK or at the end? */
#define	VIDEO_UPDATE_BEFORE_VBLANK		0x0000
#define	VIDEO_UPDATE_AFTER_VBLANK		0x0002

/* set this to use a direct RGB bitmap rather than a palettized bitmap */
#define VIDEO_RGB_DIRECT	 			0x0004

//static void waitsec(int s)
//{
//    for(int j=0;j<s;j++)
//    for(int i=0;i<50;i++)
//    {
//        WaitTOF();
//    }
//}
AbstractDisplay *g_pMameDisplay=NULL;

//struct ledBitmap {
//    ledBitmap(int nbleds,int ledwidth) {
//        _ledwidth = ledwidth;
//        _ledmarge = ledwidth>>1;
//        _width = (nbleds+1)*_ledmarge + ledwidth*nbleds;
//        _height = 2*_ledmarge + ledwidth;
//        _nbleds = nbleds;
//        int mem = _width*_height;
//        _bm.reserve(mem);
//        _bm.resize(mem,1); // fill with BG
//        //
//    }
//    std::vector<int> rgbpalette={0,0x00505050,
//                0x00d00000,0x0000d000,0x000000d0};
//    void update(int ledbits) {
//        if(_bm.size()==0) return;
//        // 0black, 1bg, red,green,blue
//        int bmofs = _ledmarge*_width+_ledmarge;
//        for(int i=0;i<_nbleds;i++)
//        {
//            int ledcolor = (ledbits&1)?(2+i):0;
//            int bmofs2 = bmofs;
//            for(int y=0;y<_ledwidth;y++)
//            {
//                for(int x=0;x<_ledwidth;x++)
//                {
//                    _bm[bmofs2+x] = ledcolor;
//                }
//                bmofs2 +=_width;
//            }
//            bmofs +=_ledwidth+_ledmarge;
//            ledbits>>=1;
//        }
//    }
//    int _nbleds;
//    int _ledwidth;
//    int _ledmarge;

//};
static ULONG FrameCounterUpdate=0;
static INT64 FrameCounter=0;
INT64 StartTime = 0;
ULONG GetStartTime=0;
static int frameSkipConfiguration = false;

/* FRF87_OUTRUN_STEADY_MK_DENSE */
static int frf87_out_run_force_realtime = 0;

/* FRF88_HOTKEY_AB_CRASH_SAFE */
/* FRF90B1_PAUSED_COLOR_SWITCH
 * MAME 0.106 core pause API.
 *
 * Two-phase screen switch:
 *   OSD update N   -> mame_pause(1), arm transition, do NOT close display.
 *   Main loop      -> paused path skips cpuexec_timeslice().
 *   OSD update N+1 -> paused updatescreen() performs recreate, then resumes.
 */
extern "C" {
    void mame_pause(int pause);
    int mame_is_paused(void);
}

static int frf90b1_switch_stage = 0;
static int frf90b1_saved_pause = 0;

/* FRF91C2_WALLCLOCK_PAUSE_ENVELOPE
 * Delays are measured from the host monotonic MAME timer. The paused main loop
 * may call updatescreen() very quickly, so update counts are deliberately NOT
 * used as elapsed-time measurements.
 */
static cycles_t frf91c2_deadline = 0;
static int frf91c3_overlay_pending = 0; /* FRF91C3_RELIABLE_POST_RECREATE_OVERLAY */
static const int FRF91C2_PRE_HOLD_MS = 220;
static const int FRF91C2_POST_HOLD_MS = 120;

static cycles_t frf91c2_deadline_after_ms(int ms)
{
    const cycles_t now = osd_cycles();
    const cycles_t cps = osd_cycles_per_second();
    if(cps <= 0 || ms <= 0)
        return now;
    return now + (cps * (cycles_t)ms) / (cycles_t)1000;
}

static int frf91c2_deadline_reached(void)
{
    return osd_cycles() >= frf91c2_deadline;
}

int frf88_opt_enabled = 0;


/* FRF89I_TRANSACTIONAL_HAM_EHB_RGB32_GREY_HOTKEY
 * Shift+F9 posts a request. Display ownership is changed only at the start of
 * the following OSD update, after the previous renderer call has returned.
 *
 * Modes:
 *   0 HAM6
 *   1 EHB64
 *   2 RGB32      (native 5-plane / 32-colour)
 *   3 GREYSCALE  (native 5-plane / 32-grey palette)
 */
int frf89_color_mode = 0;
unsigned long frf89_effective_modeid = 0xffffffffUL;
int frf89_effective_depth = 0;
int frf89_modeid_failed = 0;
static int frf89_color_switch_pending = 0;
static int frf89_f9_latched = 0;
static int frf91_color_switch_direction = 1; /* FRF91B_5MODE_BIDIRECTIONAL_OVERLAY */
static int frf89_saved_create_valid = 0;
static int frf89_recreating_display = 0;
static _osd_create_params frf89_saved_create_params;
static UINT32 frf89_reopen_rgb_components[3] = { 0, 0, 0 };

static int frf91_wrap_color_mode(int mode)
{
    mode %= 5;
    if(mode < 0) mode += 5;
    return mode;
}

static const char *frf89_color_mode_name(int mode)
{
    switch(frf91_wrap_color_mode(mode))
    {
        case 0: return "HAM6";
        case 1: return "EHB64";
        case 2: return "RGB32";
        case 3: return "GREYSCALE";
        default: return "RGB16";
    }
}

static const char *frf91_color_mode_overlay(int mode)
{
    switch(frf91_wrap_color_mode(mode))
    {
        case 0: return "HAM6 - 6 PLANES / HOLD-AND-MODIFY";
        case 1: return "EHB64 - 64 COLOURS / 6 PLANES";
        case 2: return "RGB32 - 32 COLOURS / 5 PLANES";
        case 3: return "GREYSCALE - 32 GREYS / 5 PLANES";
        default: return "RGB16 - 16 COLOURS / 4 PLANES";
    }
}

static int frf89_display_matches_mode(int mode)
{
    if(frf89_modeid_failed || frf89_effective_modeid == 0xffffffffUL)
        return 0;

    switch(frf91_wrap_color_mode(mode))
    {
        case 0:
            return (frf89_effective_modeid & HAM_KEY) != 0 &&
                   frf89_effective_depth == 6;

        case 1:
            return (frf89_effective_modeid & EXTRAHALFBRITE_KEY) != 0 &&
                   (frf89_effective_modeid & HAM_KEY) == 0 &&
                   frf89_effective_depth == 6;

        case 2:
        case 3:
            return (frf89_effective_modeid &
                    (HAM_KEY | EXTRAHALFBRITE_KEY)) == 0 &&
                   frf89_effective_depth == 5;

        case 4:
            return (frf89_effective_modeid &
                    (HAM_KEY | EXTRAHALFBRITE_KEY)) == 0 &&
                   frf89_effective_depth == 4;
    }
    return 0;
}
static int frf88_tuned_game = 0;
static int frf88_is_outrun = 0;
static int frf88_last_throttle_state = -1;
static cycles_t frf88_cycle_baseline = 0;
static cycles_t frf88_cycle_100 = 0;

/* FRF_FIX74_OSD_PIPELINE_COUNTERS
 * Diagnostic only. All counters are written by the single MAME thread.
 * One line is printed per elapsed second; the renderer is otherwise unchanged.
 */
extern "C" {
volatile UINT32 frf74_core_updatescreen_calls = 0;
volatile UINT32 frf74_core_draw_screen_calls = 0;
volatile UINT32 frf74_core_skip_calls = 0;
volatile UINT32 frf74_core_update_video_calls = 0;
volatile UINT32 frf74_osd_update_calls = 0;
volatile UINT32 frf74_display_draw_calls = 0;
volatile UINT32 frf74_ham_draw_calls = 0;
volatile UINT32 frf74_ham_render_calls = 0;
volatile UINT32 frf74_ham_rendered_frames = 0;
volatile UINT32 frf74_waitframe_calls = 0;

volatile cycles_t frf74_core_draw_screen_ticks = 0;
volatile cycles_t frf74_core_update_video_ticks = 0;
volatile cycles_t frf74_display_draw_ticks = 0;
volatile cycles_t frf74_ham_draw_ticks = 0;
volatile cycles_t frf74_ham_render_ticks = 0;
volatile cycles_t frf74_waitframe_ticks = 0;

volatile UINT32 frf74_ham_chip_bytes = 0;
volatile UINT32 frf74_ham_changed_lines = 0;
volatile UINT32 frf74_ham_reused_lines = 0;

extern UINT32 _throttleIsOn;
}

static cycles_t frf74_report_start = 0;

static ULONG frf74_ticks_to_us(cycles_t ticks, cycles_t cps)
{
    if(ticks <= 0 || cps <= 0)
        return 0;

    return (ULONG)((ticks * 1000000LL) / cps);
}

static void frf74_report_pipeline(void)
{
    cycles_t now = osd_cycles();
    cycles_t cps = osd_cycles_per_second();
    ULONG averageChip = 0;

    if(frf74_report_start == 0)
    {
        frf74_report_start = now;
        return;
    }

    if(cps <= 0 || now - frf74_report_start < cps)
        return;

    if(frf74_ham_rendered_frames)
        averageChip =
            frf74_ham_chip_bytes / frf74_ham_rendered_frames;

    printf(
        "FRF FIX74 PIPE: core=%lu draw=%lu skip=%lu "
        "osd=%lu disp=%lu ham=%lu render=%lu shown=%lu wait=%lu "
        "us[gfx=%lu present=%lu disp=%lu ham=%lu render=%lu wait=%lu] "
        "chip=%lu avg=%lu lines=%lu/%lu fs=%d throttle=%lu\n",
        (unsigned long)frf74_core_updatescreen_calls,
        (unsigned long)frf74_core_draw_screen_calls,
        (unsigned long)frf74_core_skip_calls,
        (unsigned long)frf74_osd_update_calls,
        (unsigned long)frf74_display_draw_calls,
        (unsigned long)frf74_ham_draw_calls,
        (unsigned long)frf74_ham_render_calls,
        (unsigned long)frf74_ham_rendered_frames,
        (unsigned long)frf74_waitframe_calls,
        (unsigned long)frf74_ticks_to_us(
            frf74_core_draw_screen_ticks, cps),
        (unsigned long)frf74_ticks_to_us(
            frf74_core_update_video_ticks, cps),
        (unsigned long)frf74_ticks_to_us(
            frf74_display_draw_ticks, cps),
        (unsigned long)frf74_ticks_to_us(
            frf74_ham_draw_ticks, cps),
        (unsigned long)frf74_ticks_to_us(
            frf74_ham_render_ticks, cps),
        (unsigned long)frf74_ticks_to_us(
            frf74_waitframe_ticks, cps),
        (unsigned long)frf74_ham_chip_bytes,
        (unsigned long)averageChip,
        (unsigned long)frf74_ham_changed_lines,
        (unsigned long)frf74_ham_reused_lines,
        frameSkipConfiguration,
        (unsigned long)_throttleIsOn);

    frf74_core_updatescreen_calls = 0;
    frf74_core_draw_screen_calls = 0;
    frf74_core_skip_calls = 0;
    frf74_core_update_video_calls = 0;
    frf74_osd_update_calls = 0;
    frf74_display_draw_calls = 0;
    frf74_ham_draw_calls = 0;
    frf74_ham_render_calls = 0;
    frf74_ham_rendered_frames = 0;
    frf74_waitframe_calls = 0;

    frf74_core_draw_screen_ticks = 0;
    frf74_core_update_video_ticks = 0;
    frf74_display_draw_ticks = 0;
    frf74_ham_draw_ticks = 0;
    frf74_ham_render_ticks = 0;
    frf74_waitframe_ticks = 0;

    frf74_ham_chip_bytes = 0;
    frf74_ham_changed_lines = 0;
    frf74_ham_reused_lines = 0;
    frf74_report_start = now;
}

static int frfFixedSkip1Configuration = false;
static UINT32 frfPendingDisplayFlags = 0;

static int frfReadBoolEnvDefaultOff(const char *name)
{
    const char *value = getenv(name);

    if(!value || !value[0])
        return 0;

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
//ledBitmap _ledBitmap(3,4); // nbleds, ledwidth
bool SwitchWindowFullscreen()
{
    if(!g_pMameDisplay) return false;
    bool didit = g_pMameDisplay->switchFullscreen();
    if(!didit)
    {   // fail, try to get back.
        didit = g_pMameDisplay->switchFullscreen();
    }
    return didit;
}
void ResetWatchTimer()
{
    FrameCounterUpdate = 0;
    FrameCounter = 0;
    StartTime = 0;
    GetStartTime = 1;
}

// get a swapxy/flipx/flipy bits configuration and return rotated versions.
static ULONG shiftRotationBits(ULONG orientation, int rotationShift)
{
    const int nbrots=4;
    const int nborigconfigs=8;
    // the killer table !
    static const UBYTE rots[nborigconfigs][nbrots]={
        //                                      -> ROT0
        {ROT0,ROT90,ROT180,ROT270},
        // ORIENTATION_FLIP_X (used on othunder)->VALIDATED
        {1,ROT90^2,ORIENTATION_FLIP_Y,ROT270^2},
        // ORIENTATION_FLIP_Y (used on startrek)->VALIDATED
        {2,ROT270^2,ROT180^2, ROT90^2},
        // ORIENTATION_FLIP_X ORIENTATION_FLIP_Y -> ROT180
        {ROT180,ROT270,ROT0,ROT90},

        // ORIENTATION_SWAP_XY
        {4,0,ORIENTATION_SWAP_XY,0}, // **** TODO ****   -> no game known ???
        // ORIENTATION_SWAP_XY ORIENTATION_FLIP_X -> ROT90
        {ROT90,ROT180,ROT270,ROT0},
        // ORIENTATION_SWAP_XY ORIENTATION_FLIP_Y -> ROT270
        {ROT270,ROT0,ROT90,ROT180},
        // ORIENTATION_SWAP_XY ORIENTATION_FLIP_X ORIENTATION_FLIP_Y
        // used on: tacscan ->VALIDATED
        {7,ORIENTATION_FLIP_Y ,ORIENTATION_SWAP_XY ,ORIENTATION_FLIP_X}
    };

    orientation &= ORIENTATION_MASK;
    rotationShift &= 3;
    ULONG rotated = (ULONG)rots[orientation][rotationShift];
    return rotated;

}


cycles_t gameCyclePerFrame=-1;
/*
  Create a display screen, or window, of the given dimensions (or larger). It is
  acceptable to create a smaller display if necessary, in that case the user must
  have a way to move the visibility window around.

  The params contains all the information the
  Attributes are the ones defined in driver.h, they can be used to perform
  optimizations, e.g. dirty rectangle handling if the game supports it, or faster
  blitting routines with fixed palette if the game doesn't change the palette at
  run time. The VIDEO_PIXEL_ASPECT_RATIO flags should be honored to produce a
  display of correct proportions.
  Orientation is the screen orientation (as defined in driver.h) which will be done
  by the core. This can be used to select thinner screen modes for vertical games
  (ORIENTATION_SWAP_XY set), or even to ask the user to rotate the monitor if it's
  a pivot model. Note that the OS dependent code must NOT perform any rotation,
  this is done entirely in the core.
  Depth can be 8 or 16 for palettized modes, meaning that the core will store in the
  bitmaps logical pens which will have to be remapped through a palette at blit time,
  and 15 or 32 for direct mapped modes, meaning that the bitmaps will contain RGB
  triplets (555 or 888). For direct mapped modes, the VIDEO_RGB_DIRECT flag is set
  in the attributes field.

  Returns 0 on success.
*/
int osd_create_display(const _osd_create_params *pparams, UINT32 *rgb_components)
{
    /* FRF89I_TRANSACTIONAL_HAM_EHB_RGB32_GREY_HOTKEY: retain a value-copy of the core create contract. */
    if(!frf89_recreating_display && pparams)
    {
        frf89_saved_create_params = *pparams;
        frf89_saved_create_valid = 1;
        frf89_color_mode = 0;
        frf89_effective_modeid = 0xffffffffUL;
        frf89_effective_depth = 0;
        frf89_modeid_failed = 0;
        frf89_color_switch_pending = 0;
        frf89_f9_latched = 0;
    }

    if(g_pMameDisplay) osd_close_display();
    if(!pparams || !Machine || !Machine->gamedrv) return 1; // fail

    MameConfig &mainConfig = getMainConfig();
    MameConfig::Display &config = mainConfig.display();
    MameConfig::Controls &controls = mainConfig.controls();
    MameConfig::Misc &misc = mainConfig.misc();

    MameConfig::Display_PerScreenMode &screenModeConf = config.getActiveMode();

    {
        AbstractDisplay::params params={0};

        // get the 3 swapxy/flipx/flipy screen bits, and may apply rotation from config.
        params._flags = shiftRotationBits(Machine->gamedrv->flags,(int)screenModeConf._rotateMode);

        if( /*screenModeConf._ScreenModeChoice == MameConfig::ScreenModeChoice::Choose
            &&*/ screenModeConf._modeid._modeId != INVALID_ID )
        {
            params._forcedModeID = (ULONG) screenModeConf._modeid._modeId;
            params._forcedDepth = (ULONG) screenModeConf._modeid._depth; // only used in AGA/OCS
           // printf("screenModeConf._modeid._depth:%d\n",params._forcedDepth);
        }
        else
        {
           params._forcedModeID = ~0; // undefined.
           params._forcedDepth = 24;
        }

        params._width = pparams->width;
        params._height = pparams->height;
        params._colorsIndexLength = pparams->colors;
        params._video_attributes = pparams->video_attributes;
        params._driverDepth = pparams->depth;

        params._wingeo._window_posx = screenModeConf._window_posx;
        params._wingeo._window_posy = screenModeConf._window_posy;
        params._wingeo._window_width = screenModeConf._window_width;
        params._wingeo._window_height = screenModeConf._window_height;
        params._wingeo._valid = screenModeConf._window_validpos;

        // this will decide video implemntation against available hardware and config.
        g_pMameDisplay = new IntuitionDisplay();

        if(config._flags & CONFDISPLAYFLAGS_ONWORKBENCH )
            params._flags |= DISPFLAG_STARTWITHWINDOW;

        if(config._flags & CONFDISPLAYFLAGS_FORCEDEPTH16 )
            params._flags |= DISPFLAG_FORCEDEPTH16;

        if((misc._Optims & OPTIMFLAGS_USEP96CGXBESTMODE)==0 )
            params._flags |= DISPFLAG_USEOWNCGXBESTMODE;

        if((controls._llPort_Player[0]>0 && controls._llPort_Type[0]==PORT_TYPE_LIGHTGUN) ||
           (controls._llPort_Player[1]>0 && controls._llPort_Type[1]==PORT_TYPE_LIGHTGUN) )
           {
               params._flags |= DISPFLAG_LIGHTGUN;
           }

        // these 2 are exclusive:
        if(config._buffering == MameConfig::ScreenBufferMode::TripleBufferCSB )
             params._flags |= DISPFLAG_USETRIPLEBUFFER;
        if(config._buffering == MameConfig::ScreenBufferMode::DoubleBufferSVP )
             params._flags |= DISPFLAG_USEHEIGHTBUFFER;

        if(config._drawEngine == MameConfig::DrawEngine::CgxScalePixelArray)
                params._flags |= DISPFLAG_USESCALEPIXARRAY;

        bool screenok = g_pMameDisplay->open(params);
        if(!screenok) {
            loginfo(2,"couldn't open screen.");
            return 1; // fail.
        }
    } // end if bitmap

    if(!g_pMameDisplay || !g_pMameDisplay->good())
    {
        //
        loginfo(2,"couldn't find a graphic mode.");
        return 1; // fail.
    }
    // for drivers with RGB modes, we have to describe our pixel format
    if((pparams->video_attributes & VIDEO_RGB_DIRECT) && (rgb_components))
    {
        g_pMameDisplay->init_rgb_components(rgb_components);
    }


//    AllocInputs(); // input object depends of screen or window.

    FrameCounterUpdate = 0;
    FrameCounter = 0;
    StartTime = 0;
    GetStartTime = 1;

    MameConfig::Misc &configMisc = getMainConfig().misc();

 //   printf("osd_create_display fps:%f\n",pparams->fps);

    frf88_is_outrun =
        Machine && Machine->gamedrv && Machine->gamedrv->name &&
        strcmp(Machine->gamedrv->name, "outrun") == 0;
    frf88_tuned_game =
        Machine && Machine->gamedrv && Machine->gamedrv->name &&
        (frf88_is_outrun ||
         strcmp(Machine->gamedrv->name, "mk") == 0);
    frf87_out_run_force_realtime = frf88_is_outrun;

    /* Every launch begins in the known baseline behaviour. */
    frf88_opt_enabled = 0;
    frf88_last_throttle_state = -1;

    float speedlimit = configMisc._speedlimit;
    if(speedlimit<80.0f ) speedlimit = 80.0f;
    else if(speedlimit>125.0f) speedlimit = 125.0f;
    speedlimit = 100.0f/speedlimit;

    frf88_cycle_baseline =
        (cycles_t)(1000000.0 * speedlimit / pparams->fps);
    frf88_cycle_100 =
        (cycles_t)(1000000.0 / pparams->fps);
    gameCyclePerFrame = frf88_cycle_baseline;

    if(frf88_tuned_game)
        printf("FRF88 A/B: OPT OFF at start; Shift+F10 toggles experimental per-game tuning\n");

    frameSkipConfiguration = config.frameSkip();
    frfFixedSkip1Configuration =
        frfReadBoolEnvDefaultOff("FRF_FIXED_SKIP1");
    frfPendingDisplayFlags = 0;

    if(frfFixedSkip1Configuration)
        loginfo(2,"FRF fixed skip1 active: exact alternating output frames");

//    printf("gameCyclePerFrame fps:%d\n",(int)gameCyclePerFrame);
    return 0; // success

}

/* FRF89L_AUDIO_FADED_COLOR_SWITCH_AND_QUIT
 *
 * Small quality-of-life envelope around native screen recreation and shutdown.
 * No AHI/device ownership changes and no allocations: only MAME's existing
 * OSD master-volume attenuation plus short WaitTOF timing steps.
 */
static int frf89l_audio_transition_active = 0;
static int frf89l_audio_saved_attenuation = 0;

static void frf89l_audio_wait_vblanks(int count)
{
    while(count-- > 0)
        WaitTOF();
}

static void frf89l_audio_fade_down(const char *reason)
{
    int base;
    int i;

    if(frf89l_audio_transition_active)
        return;

    base = osd_get_mastervolume();
    frf89l_audio_saved_attenuation = base;
    frf89l_audio_transition_active = 1;

    printf("FRF89L AUDIO: fade down (%s) from %d dB\n",
        reason ? reason : "transition", base);

    /* Four short steps: ~80 ms on a 50 Hz display. */
    for(i = 1; i <= 4; ++i)
    {
        int attenuation = base + ((-32 - base) * i) / 4;
        osd_set_mastervolume(attenuation);
        frf89l_audio_wait_vblanks(1);
    }
    osd_set_mastervolume(-32);
}

static void frf89l_audio_settle_and_fade_up(void)
{
    int base;
    int i;

    if(!frf89l_audio_transition_active)
        return;

    base = frf89l_audio_saved_attenuation;

    /* New native screen gets a short quiet settle before audio returns. */
    frf89l_audio_wait_vblanks(10);

    for(i = 1; i <= 4; ++i)
    {
        int attenuation = -32 + ((base + 32) * i) / 4;
        osd_set_mastervolume(attenuation);
        frf89l_audio_wait_vblanks(1);
    }

    osd_set_mastervolume(base);
    frf89l_audio_transition_active = 0;
    printf("FRF89L AUDIO: restored %d dB\n", base);
}

static void frf89l_audio_quit_fade(void)
{
    if(frf89l_audio_transition_active)
        return;

    frf89l_audio_fade_down("quit");
    frf89l_audio_wait_vblanks(2);
}

void osd_close_display(void)
{
    /* FRF89L_AUDIO_FADED_COLOR_SWITCH_AND_QUIT: fade only on a real shutdown, never a live recreate. */
    if(!frf89_recreating_display)
    {
        frf89l_audio_quit_fade();
        Inputs_Free();
    }
    if(g_pMameDisplay) {
        WindowGeo wgeo = g_pMameDisplay->getWindowGeometry();

        MameConfig &mainConfig = getMainConfig();
        MameConfig::Display &config = mainConfig.display();
        MameConfig::Display_PerScreenMode &screenmodeprefs = config.getActiveMode();

        screenmodeprefs._window_posx = wgeo._window_posx;
        screenmodeprefs._window_posy = wgeo._window_posy;
        screenmodeprefs._window_width = wgeo._window_width;
        screenmodeprefs._window_height = wgeo._window_height;
        screenmodeprefs._window_validpos = wgeo._valid;

        delete g_pMameDisplay;
        g_pMameDisplay = NULL;
    }
}


/*
  Update video and audio. game_bitmap contains the game display, while
  debug_bitmap an image of the debugger window (if the debugger is active; NULL
  otherwise). They can be shown one at a time, or in two separate windows,
  depending on the OS limitations. If only one is shown, the user must be able
  to toggle between the two by pressing IPT_UI_TOGGLE_DEBUG; moreover,
  osd_debugger_focus() will be used by the core to force the display of a
  specific bitmap, e.g. the debugger one when the debugger becomes active.

  leds_status is a bitmask of lit LEDs, usually player start lamps. They can be
  simulated using the keyboard LEDs, or in other ways e.g. by placing graphics
  on the window title bar.
*/
// this counter is from mame and take account of resets.

extern "C" {
    extern ULONG _frame;
    extern ULONG _bootframeskip;
	extern UINT32 _throttleIsOn; // with shift+f10 key
}

void osd_update_video_and_audio(struct _mame_display *display)
{

    /* FIX74: OSD update entry */
    frf74_osd_update_calls++;


    if(GetStartTime)
    {
        StartTime = osd_cycles();
        GetStartTime = 0;
        FrameCounter = 0;
    }
// printf("osd_update_video_and_audio\n");
    if(!g_pMameDisplay) return;

    // apply eventual hard beam waiting (if too fast) just before draw.
    {
        // no more 64b division !
        cycles_t cyclethatShouldBeNow = StartTime + (gameCyclePerFrame * FrameCounter );
        cycles_t cnow = osd_cycles();

        // if OS paused (window moving, menu bt, intuition hogs, reset timer)
        //if(FrameCounter+(igamefps>>1)<framesThatShouldbeNow)
        if(cnow>(cyclethatShouldBeNow+(1000000LL>>1))) // + half a second.
        {
            ResetWatchTimer();
        }
        static int lastwaitskipped = 0;
        if(frf88_tuned_game)
        {
            if(frf88_last_throttle_state < 0)
            {
                frf88_last_throttle_state = _throttleIsOn;
            }
            else if(_throttleIsOn != frf88_last_throttle_state)
            {
                /* Consume Shift+F10 as the A/B switch for tuned games. */
                _throttleIsOn = frf88_last_throttle_state;
                frf88_opt_enabled = !frf88_opt_enabled;

                if(frf88_is_outrun)
                    gameCyclePerFrame = frf88_opt_enabled
                        ? frf88_cycle_100 : frf88_cycle_baseline;

                printf("FRF88 OPT %s: %s\n",
                    Machine && Machine->gamedrv && Machine->gamedrv->name
                        ? Machine->gamedrv->name : "?",
                    frf88_opt_enabled ? "ON" : "OFF");
            }
        }

        if(_frame>=_bootframeskip &&
           (_throttleIsOn==0 ||
            (frf87_out_run_force_realtime && frf88_opt_enabled)))
        {
            if(lastwaitskipped) {
                ResetWatchTimer();
                lastwaitskipped = 0;
                cnow = cyclethatShouldBeNow;
            }

            while(cnow<cyclethatShouldBeNow)
            {
                // some functions that knowns how to actually pass priority to other tasks.
                // Graphics/WaitTOF() does but is 50 or 60Hz, and will wait a random time on first call.
                // Graphics/WaitBOVP(vp) should fit exact screen frame but is commonly bad implemented and hogs cpu.
                /* FIX74: WaitFrame timing */
                {
                    cycles_t frf74WaitStart = osd_cycles();
                    g_pMameDisplay->WaitFrame();
                    frf74_waitframe_ticks +=
                        osd_cycles() - frf74WaitStart;
                    frf74_waitframe_calls++;
                }

                cnow = osd_cycles();
            }

        } else lastwaitskipped = 1;

    }

    /*
     * FRF V3C:
     * The old FrameSkip skipped MAME's game renderer but still called the
     * complete Amiga remap/C2P/Chip-RAM output path on every emulated frame.
     */
    if(osd_skip_this_frame())
    {
        frfPendingDisplayFlags |= display->changed_flags;
    }
    else
    {
        display->changed_flags |= frfPendingDisplayFlags;
        frfPendingDisplayFlags = 0;
        /* FIX74: display draw timing */
    {
        cycles_t frf74DisplayStart = osd_cycles();
        frf74_display_draw_calls++;
        g_pMameDisplay->draw(display);
        frf74_display_draw_ticks +=
            osd_cycles() - frf74DisplayStart;
    }
    frf74_report_pipeline();
    }

    MsgPort *userport = g_pMameDisplay->userPort();
    if(userport) Inputs_Keyboard_ll_Update(userport);
    Inputs_FrameUpdate();


    /* FRF89K_INPUT_PRESERVING_END_FRAME_SWITCH
     * Detect the chord after the normal input update. Queue on press, but do
     * not destroy the current Intuition window until F9+Shift have both been
     * released through that window. Live display recreation leaves the MAME
     * input subsystem allocated; next frame binds keyboard polling to the new
     * display's UserPort dynamically.
     */
    {
        const int frf89_f8 = code_pressed(KEYCODE_F8) ? 1 : 0;
        const int frf89_f9 = code_pressed(KEYCODE_F9) ? 1 : 0;
        const int frf89_shift =
            (code_pressed(KEYCODE_LSHIFT) || code_pressed(KEYCODE_RSHIFT)) ? 1 : 0;
        const int frf89_chord =
            frf89_shift && (frf89_f8 || frf89_f9);

        if(frf89_chord && !frf89_f9_latched && !frf89_recreating_display)
        {
            frf91_color_switch_direction = frf89_f8 ? -1 : 1;
            const int frf89_next_mode = frf91_wrap_color_mode(
                frf89_color_mode + frf91_color_switch_direction);
            printf("FRF91B COLOR: %s -> %s; waiting for release/pause transaction\\n",
                frf91_color_switch_direction > 0 ? "NEXT" : "PREVIOUS",
                frf89_color_mode_name(frf89_next_mode));

            if(frf89_saved_create_valid)
            {
                /* FRF90B1_PAUSED_COLOR_SWITCH: native MAME pause replaces screen fade-down. */
                frf89_color_switch_pending = 1;
                printf("FRF90B COLOR: queued -> %s; pause/recreate after key release\n",
                    frf89_color_mode_name(frf89_next_mode));
            }
            else
            {
                printf("FRF89L COLOR: request ignored; no saved display-create state\n");
            }
        }

        if(frf89_chord)
            frf89_f9_latched = 1;
        else
            frf89_f9_latched = 0;

        if(frf89_color_switch_pending && frf89_saved_create_valid &&
           !frf89_recreating_display && !frf89_chord &&
           frf90b1_switch_stage == 0)
        {
            /* FRF90B1_PAUSED_COLOR_SWITCH phase 1:
             * Save the user's pause state and enter MAME's real paused loop.
             * Display ownership is intentionally untouched in this update.
             */
            frf90b1_saved_pause = mame_is_paused() ? 1 : 0;
            frf90b1_switch_stage = 1;
            frf91c3_overlay_pending = 0;
            frf91c2_deadline =
                frf91c2_deadline_after_ms(FRF91C2_PRE_HOLD_MS);

            if(!frf90b1_saved_pause)
            {
                printf("FRF90B COLOR: core/audio paused; switch on next paused update\n");
                mame_pause(1);
            }
            else
            {
                printf("FRF90B COLOR: already paused; switch on next paused update\n");
            }
        }
        else if(frf89_color_switch_pending && frf89_saved_create_valid &&
                !frf89_recreating_display && !frf89_chord &&
                frf90b1_switch_stage == 1)
        {
            /* FRF91C2_WALLCLOCK_PAUSE_ENVELOPE PRE-SWITCH WALL-CLOCK HOLD */
            if(frf91c2_deadline_reached())
            {
                frf90b1_switch_stage = 2;
                printf("FRF91C2 COLOR: pre-switch wall-clock hold complete; recreate next paused update\n");
            }
        }
        else if(frf89_color_switch_pending && frf89_saved_create_valid &&
                !frf89_recreating_display && !frf89_chord &&
                frf90b1_switch_stage == 2)
        {
            const int frf89_previous_mode = frf89_color_mode;
            const int frf89_target_mode =
                frf91_wrap_color_mode(
                    frf89_previous_mode + frf91_color_switch_direction);
            const int frf89_saved_opt = frf88_opt_enabled;
            int frf89_open_rc;
            int frf89_verified;
            MsgPort *frf89_new_userport;

            frf89_color_switch_pending = 0;
            frf89_color_mode = frf89_target_mode;
            frf89_effective_modeid = 0xffffffffUL;
            frf89_effective_depth = 0;
            frf89_modeid_failed = 0;
            frf89_recreating_display = 1;

            /* osd_close_display sees frf89_recreating_display and therefore
               closes ONLY display ownership; Inputs_Free() is skipped. */
            osd_close_display();
            frf89_open_rc = osd_create_display(
                &frf89_saved_create_params,
                frf89_reopen_rgb_components);

            frf88_opt_enabled = frf89_saved_opt;
            if(frf88_opt_enabled && frf88_is_outrun)
                gameCyclePerFrame = frf88_cycle_100;

            frf89_new_userport = g_pMameDisplay ? g_pMameDisplay->userPort() : NULL;
            frf89_verified =
                (frf89_open_rc == 0) &&
                frf89_display_matches_mode(frf89_target_mode) &&
                (frf89_new_userport != NULL);

            if(!frf89_verified)
            {
                int frf89_rollback_rc;
                MsgPort *frf89_rollback_userport;

                printf("FRF89K COLOR: target %s failed "
                       "(open=%d mode=%08lx depth=%d idfail=%d port=%s); rollback\n",
                    frf89_color_mode_name(frf89_target_mode),
                    frf89_open_rc,
                    frf89_effective_modeid,
                    frf89_effective_depth,
                    frf89_modeid_failed,
                    frf89_new_userport ? "OK" : "NULL");

                osd_close_display();
                frf89_color_mode = frf89_previous_mode;
                frf89_effective_modeid = 0xffffffffUL;
                frf89_effective_depth = 0;
                frf89_modeid_failed = 0;

                frf89_rollback_rc = osd_create_display(
                    &frf89_saved_create_params,
                    frf89_reopen_rgb_components);

                frf88_opt_enabled = frf89_saved_opt;
                if(frf88_opt_enabled && frf88_is_outrun)
                    gameCyclePerFrame = frf88_cycle_100;

                frf89_rollback_userport =
                    g_pMameDisplay ? g_pMameDisplay->userPort() : NULL;
                frf89_verified =
                    (frf89_rollback_rc == 0) &&
                    frf89_display_matches_mode(frf89_previous_mode) &&
                    (frf89_rollback_userport != NULL);

                if(!frf89_verified)
                {
                    frf89_recreating_display = 0;
                    printf("FRF89K COLOR FATAL: rollback %s failed; exiting cleanly\n",
                        frf89_color_mode_name(frf89_previous_mode));
                    exit(89);
                }

                printf("FRF89K COLOR: rollback restored %s mode=%08lx depth=%d "
                       "port=OK OPT=%s\n",
                    frf89_color_mode_name(frf89_previous_mode),
                    frf89_effective_modeid,
                    frf89_effective_depth,
                    frf88_opt_enabled ? "ON" : "OFF");
            }
            else
            {
                printf("FRF89K COLOR: %s mode=%08lx depth=%d port=OK OPT=%s\n",
                    frf89_color_mode_name(frf89_color_mode),
                    frf89_effective_modeid,
                    frf89_effective_depth,
                    frf88_opt_enabled ? "ON" : "OFF");
            }

            frf89_recreating_display = 0;
            frf90b1_switch_stage = 3;
            frf91c2_deadline =
                frf91c2_deadline_after_ms(FRF91C2_POST_HOLD_MS);

            /* FRF91C3_RELIABLE_POST_RECREATE_OVERLAY
             * Queue the popup on the first clean paused update of the newly
             * verified display, rather than during the handover callback.
             */
            frf91c3_overlay_pending = 1;

            printf("FRF91C2 COLOR: screen verified; holding new screen for %d ms\n",
                FRF91C2_POST_HOLD_MS);

            /* Target or rollback has a valid UserPort and is now stable. */
            /* FRF90B1_PAUSED_COLOR_SWITCH: native MAME resume replaces VBlank settle/fade-up. */
        }
        else if(!frf89_recreating_display &&
                frf90b1_switch_stage == 3)
        {
            /* FRF91C2_WALLCLOCK_PAUSE_ENVELOPE POST-SWITCH WALL-CLOCK HOLD */
            if(frf91c3_overlay_pending)
            {
                frf91c3_overlay_pending = 0;
                ui_popup_time(1, "SCREEN MODE: %s",
                    frf91_color_mode_overlay(frf89_color_mode));
                printf("FRF91C3 COLOR: screen-mode overlay queued on clean new-screen update\n");
            }
            if(frf91c2_deadline_reached())
            {
                frf90b1_switch_stage = 0;

                if(!frf90b1_saved_pause)
                {
                    mame_pause(0);
                    printf("FRF91C2 COLOR: post-switch wall-clock hold complete; core/audio resumed\n");
                }
                else
                {
                    printf("FRF91C2 COLOR: post-switch wall-clock hold complete; preserving user pause\n");
                }
            }
        }
    }

    // - - - -
    FrameCounterUpdate++;
    FrameCounter++;

    if((FrameCounter & 31) == 0) // from times to times check ctrl-c in the mame loop.
    {
            /* Get current state of signals */
        ULONG signals = SetSignal(0L, 0L);
        if(signals & SIGBREAKF_CTRL_C)
        {
            SetSignal(0L, SIGBREAKF_CTRL_C); // clear because we manage.
            exit(0);
        }
    }


}
// for progressbar pass
static void checkExitSimple(MsgPort *userport )
{
  struct IntuiMessage *im;
    int doExit=0;
    while((im = (struct IntuiMessage *) GetMsg(userport)))
    {
        ULONG imclass = im->Class;
        UWORD imcode  = im->Code;
        UWORD imqual  = im->Qualifier;

        ReplyMsg((struct Message *) im); // the faster the better.

        switch(imclass)
        {
            case IDCMP_RAWKEY:
            if(!(imqual & IEQUALIFIER_REPEAT) )
            {
              if( (imcode & 0x037f)==0x45) doExit = 1; // esc key.
            }
            break;
        case IDCMP_CLOSEWINDOW:
            doExit = 1;
        break;
         default:
            break;
        }
    }
    if(doExit) fatalerror("User cancelled."); // setjmp to end of mame loop

    {
        /* Get current state of signals */
        ULONG signals = SetSignal(0L, 0L);
        if(signals & SIGBREAKF_CTRL_C)
        {
            SetSignal(0L, SIGBREAKF_CTRL_C); // clear because we manage.
            exit(0);
        }
    }
}

// - -  update screen before boot.
void osd_update_boot_progress(int per256, int enm)
{
    if(!g_pMameDisplay) return;
    // like main thread is under the rungame init ,
    // game is not started yet,
    // still we can test possible exit.
    MsgPort *userport = g_pMameDisplay->userPort();
    if(!userport) return ;
    checkExitSimple(userport);

    g_pMameDisplay->drawProgress(per256,enm);

}

extern ULONG _bootframeskip;
/*
  osd_skip_this_frame() must return 0 if the current frame will be displayed.
  This can be used by drivers to skip cpu intensive processing for skipped
  frames, so the function must return a consistent result throughout the
  current frame. The function MUST NOT check timers and dynamically determine
  whether to display the frame: such calculations must be done in
  osd_update_video_and_audio(), and they must affect the FOLLOWING frames, not
  the current one. At the end of osd_update_video_and_audio(), the code must
  already know exactly whether the next frame will be skipped or not.
*/
int osd_skip_this_frame(void)
{
    if(_frame < _bootframeskip && _frame > 0)
        return FrameCounterUpdate & 1;

    if(frfFixedSkip1Configuration ||
       frameSkipConfiguration)
        return FrameCounterUpdate & 1;

    return 0;
}

/*
  Provides a hook to allow the OSD system to override processing of a
  snapshot.  This function will either return a new bitmap, for which the
  caller is responsible for freeing.
*/
mame_bitmap *osd_override_snapshot(mame_bitmap *bitmap, rectangle *bounds)
{
    return NULL;
}

/*
  Returns a pointer to the text to display when the FPS display is toggled.
  This normally includes information about the frameskip, FPS, and percentage
  of full game speed.
*/
static char perfo_line[28];
extern "C" {
//extern int testvv;
//extern int levervt[4];
};
const char *osd_get_fps_text(const performance_info *performance)
{
     snprintf(perfo_line,27,"speed:%.01f%% fps:%.03f",performance->game_speed_percent,
              performance->frames_per_second);
              // usefull for debug:
        // snprintf(perfo_line,27,"lv%d  ",testvv);
    perfo_line[27]=0;
    return perfo_line;
}
// memory.c 2569
// #define WRITEBYTE8(name,spacenum)
//  WRITEBYTE8(program_write_byte_8,     ADDRESS_SPACE_PROGRAM)
//  WRITEBYTE8(data_write_byte_8,     ADDRESS_SPACE_DATA)

//#define ADDRESS_SPACES			3						/* maximum number of address spaces */
//#define ADDRESS_SPACE_PROGRAM	0						/* program address space */
//#define ADDRESS_SPACE_DATA		1						/* data address space */
//#define ADDRESS_SPACE_IO		2						/* I/O address space */
//struct _address_space
//{
//	offs_t				addrmask;			/* address mask */
//	UINT8 *				readlookup;			/* read table lookup */
//	UINT8 *				writelookup;		/* write table lookup */
//	handler_data *		readhandlers;		/* read handlers */
//	handler_data *		writehandlers;		/* write handlers */
//	data_accessors *	accessors;			/* pointers to the data access handlers */
//};
//extern "C" {
//void mywrite8(UINT32 adress,UINT8 data)
//{
//    struct _address_space &space = [ADDRESS_SPACE_PROGRAM];
//    /* perform lookup */
//    // PERFORM_LOOKUP(lookup,space,extraand)
//    // PERFORM_LOOKUP(writelookup,active_address_space[spacenum],~0);
//	address &= space.addrmask & extraand;
//	entry = space.lookup[LEVEL1_INDEX(address)];
//	if (entry >= SUBTABLE_BASE)
//		entry = space.lookup[LEVEL2_INDEX(entry,address)];

//}

//}

/*
 * Pixels pushed, colors perfect, frames flowing! 🖼️
 *     ___
 *    (• ◡•)  <- This peacock displays the most beautiful colors!
 *     ╰─╯
 *    ╱▔▔▔╲
 */
