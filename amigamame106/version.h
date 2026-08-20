#ifndef MAME_AMIGA_VERSION
#define MAME_AMIGA_VERSION

#include "osdepend.h"
#include "mame_ver.h"

/*
 * FRF Mame106RGB SYSTEM VERSION
 *
 * This is the single source of truth for all visible Amiga-port branding.
 * Edit FRF_VERSION.conf and re-run the version kit instead of editing these
 * macros separately.
 */
#define APP_PRODUCT_NAME "Mame106RGB"
#define APP_CORE_VERSION "0.106"
#define APP_PLATFORM     "AmigaOS 3.x"
#define APP_RELEASE      "V103"
#define APP_AUTHOR       "Future Retro Fusion"
#define APP_CONTRIBUTION  "Future Retro Fusion AmigaOS 68k adaptation"
#define APP_BUILD_DATE   "2026-08-20"
#define APP_CPU_LABEL    "68060 FPU"

#define APPNAMEA \
    APP_PRODUCT_NAME " " APP_CORE_VERSION " " APP_PLATFORM " " APP_RELEASE

#define APPVERNUM APP_RELEASE

#define APPABOUT_TEXT \
    APPNAMEA "\n" APP_AUTHOR " - " APP_BUILD_DATE

#define APPVERSTRING \
    "$VER: " APP_PRODUCT_NAME " " APP_CORE_VERSION " " \
    APP_PLATFORM " " APP_RELEASE " (" APP_BUILD_DATE ") - " APP_AUTHOR

#define APPFULLVERSION \
    APPNAMEA " - " APP_AUTHOR " - " APP_BUILD_DATE

#endif
