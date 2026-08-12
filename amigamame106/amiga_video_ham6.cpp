/******************************************************************************
 * amiga_video_ham6.cpp
 *
 * FIX70: complete E-UAE-derived native HAM6 output engine for MamAmiga.
 *
 * Pipeline:
 *   MAME framebuffer
 *     -> palette/direct-RGB quantisation to the donor's RGB15 domain
 *     -> E-UAE HAM6 decision tables
 *     -> fused HAM command + six-plane packing
 *     -> per-ScreenBuffer source and planar shadows in Fast RAM
 *     -> changed contiguous UWORD runs copied to Chip RAM
 *     -> SafeMessage-controlled non-blocking ChangeScreenBuffer
 *
 * GPL v2 or later.
 *****************************************************************************/

#include "amiga_video_ham6.h"
#include "amiga_video_intuition.h"

#include <proto/exec.h>
#include <proto/graphics.h>

#include <proto/intuition.h>

/*
 * AmigaOS intuition/intuition.h defines several KEYCODE_* macros.
 * MAME106 uses the same names for its own input keycode enumeration.
 *
 * Do not allow the Amiga macros to leak into MAME headers.
 */
#ifdef KEYCODE_Q
#undef KEYCODE_Q
#endif
#ifdef KEYCODE_Z
#undef KEYCODE_Z
#endif
#ifdef KEYCODE_X
#undef KEYCODE_X
#endif
#ifdef KEYCODE_V
#undef KEYCODE_V
#endif
#ifdef KEYCODE_B
#undef KEYCODE_B
#endif
#ifdef KEYCODE_N
#undef KEYCODE_N
#endif
#ifdef KEYCODE_M
#undef KEYCODE_M
#endif
#ifdef KEYCODE_LESS
#undef KEYCODE_LESS
#endif
#ifdef KEYCODE_GREATER
#undef KEYCODE_GREATER
#endif

#include <proto/dos.h>

extern "C" {
    #include <exec/memory.h>
    #include <dos/dos.h>
    #include <graphics/displayinfo.h>
    #include <graphics/gfx.h>
    #include <graphics/modeid.h>
    #include <intuition/screens.h>

    #include "mame.h"
    #include "mamecore.h"
    #include "driver.h"
    #include "osdepend.h"
    #include "palette.h"
    #include "video.h"
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * FRF86B_MK_OUTRUN_DENSE_SCHEDULER
 *
 * mk:     measured dense bursts cluster around 220..254 converted rows.
 * outrun: heavy scrolling begins around 200 converted rows.
 *
 * Tuned games classify heavy frames using actual converted rows.
 * Untuned games retain the original FIX82 >20 ms test.
 *
 * FRF85 probe recording defaults OFF.
 * main.cpp can override it with -frflog 0/1.
 */
int frf86b_runtime_log_override = -1;

/* FRF87_OUTRUN_STEADY_MK_DENSE */
static int frf87_mk_dense_budget = 0;

/* FRF88_HOTKEY_AB_CRASH_SAFE */
extern int frf88_opt_enabled;
/* FRF88B_OUTRUN_BOUNDED_RECOVERY */

static int frf86b_log_enabled(void)
{
    const char *v;

    if(frf86b_runtime_log_override >= 0)
        return frf86b_runtime_log_override != 0;

    v = getenv("FRFLOG");
    if(!v || !*v ||
       strcmp(v, "0") == 0 ||
       strcmp(v, "off") == 0 ||
       strcmp(v, "OFF") == 0 ||
       strcmp(v, "false") == 0 ||
       strcmp(v, "FALSE") == 0 ||
       strcmp(v, "no") == 0 ||
       strcmp(v, "NO") == 0)
        return 0;

    return 1;
}

static int frf86b_game_mode(void)
{
    const char *name = 0;

    if(Machine && Machine->gamedrv)
        name = Machine->gamedrv->name;

    if(!name)
        return 0;
    if(strcmp(name, "mk") == 0)
        return 1;
    if(strcmp(name, "outrun") == 0)
        return 2;
    return 0;
}


/* FRF_FIX74_HAM_PIPELINE_COUNTERS
 * Two osd_cycles() reads per HAM draw and per renderFrame call.
 * No timers are placed inside pixel, line or Chip RAM copy loops.
 */
extern "C" {
extern volatile UINT32 frf74_ham_draw_calls;
extern volatile UINT32 frf74_ham_render_calls;
extern volatile UINT32 frf74_ham_rendered_frames;
extern volatile cycles_t frf74_ham_draw_ticks;
extern volatile cycles_t frf74_ham_render_ticks;
extern volatile UINT32 frf74_ham_chip_bytes;
extern volatile UINT32 frf74_ham_changed_lines;
extern volatile UINT32 frf74_ham_reused_lines;
}

/* FRF_FIX85_PER_GAME_OPTIMIZATION_PROBE
 *
 * Zero-I/O gameplay profiler. One compact record is stored in Fast RAM per
 * HAM draw opportunity. Nothing is printed while gameplay is active; the
 * ring is dumped only from close(), after the run is over.
 */
#define FRF85_PROBE_CAPACITY 16384UL
#define FRF85_FLAG_HEAVY     0x01UL
#define FRF85_FLAG_REPEAT    0x02UL
#define FRF85_FLAG_INDEXED   0x04UL

struct FrfFix85ProbeSample
{
    cycles_t tick;
    cycles_t renderTicks;
    ULONG drawCall;
    ULONG chipBytes;
    ULONG dirtyRuns;
    ULONG changedLines;
    ULONG reusedLines;
    ULONG paletteChanges;
    ULONG flags;
};

class FrfFix74HamDrawTimer
{
public:
    FrfFix74HamDrawTimer(
        ULONG &renderedFrames,
        ULONG &chipBytes,
        ULONG &convertedLines,
        ULONG &reusedLines)
        : _start(osd_cycles())
        , _renderedFrames(renderedFrames)
        , _chipBytes(chipBytes)
        , _convertedLines(convertedLines)
        , _reusedLines(reusedLines)
        , _startRendered(renderedFrames)
        , _startChip(chipBytes)
        , _startConverted(convertedLines)
        , _startReused(reusedLines)
    {
        frf74_ham_draw_calls++;
    }

    ~FrfFix74HamDrawTimer()
    {
        frf74_ham_draw_ticks += osd_cycles() - _start;

        if(_renderedFrames > _startRendered)
        {
            frf74_ham_rendered_frames +=
                _renderedFrames - _startRendered;
            frf74_ham_chip_bytes +=
                _chipBytes - _startChip;
            frf74_ham_changed_lines +=
                _convertedLines - _startConverted;
            frf74_ham_reused_lines +=
                _reusedLines - _startReused;
        }
    }

private:
    cycles_t _start;
    ULONG &_renderedFrames;
    ULONG &_chipBytes;
    ULONG &_convertedLines;
    ULONG &_reusedLines;
    ULONG _startRendered;
    ULONG _startChip;
    ULONG _startConverted;
    ULONG _startReused;
};

class FrfFix74HamRenderTimer
{
public:
    FrfFix74HamRenderTimer()
        : _start(osd_cycles())
    {
        frf74_ham_render_calls++;
    }

    ~FrfFix74HamRenderTimer()
    {
        frf74_ham_render_ticks += osd_cycles() - _start;
    }

private:
    cycles_t _start;
};


#ifndef VIDEO_RGB_DIRECT
#define VIDEO_RGB_DIRECT 0x0004
#endif

Ham6EuaeOutput::Ham6EuaeOutput(
    IntuitionDrawable &drawable,
    int videoAttributes)
    : _drawable(drawable)
    , _videoAttributes(videoAttributes)
    , _screen(NULL)
    , _safePort(NULL)
    , _front(0)
    , _back(1)
    , _active(0)
    , _directFront(0)
    , _paletteValid(0)
    , _reportedIncompatibleFrame(0)
    , _rgbLine(NULL)
    , _palette15(NULL)
    , _directError(NULL)
    , _directCommand(NULL)
    , _holdChoice(NULL)
    , _swaps(0)
    , _swapMisses(0)
    , _busyFrames(0)
    , _renderedFrames(0)
    , _renderFailures(0)
    , _chipBytes(0)
    , _dirtyRuns(0)
    , _convertedLines(0)
    , _reusedLines(0)
    , _zeroWriteFrames(0)
    , _fullishFrames(0)
    , _lastFrameBytes(0)
    , _drawCalls(0)
    , _directFrontFrames(0)
    , _indexedFastFrames(0)
    , _indexedChangedLines(0)
    , _indexedReusedLines(0)
    , _paletteUpdateEvents(0)
    , _paletteActualChanges(0)
    , _interleavedBurstFrames(0)
    , _interleavedBurstLines(0)
    , _interleavedBurstBytes(0)
    , _indexedFastReported(0)
    , _interleavedBurstReported(0)
    , _rotatedIndexedReported(0)
    , _wideIndexedReported(0)
    , _smoothHeavyStreak(0)
    , _smoothSkipPending(0)
    , _smoothCooldown(0)
    , _smoothHeavyFrames(0)
    , _smoothSkippedFrames(0)
    , _probeSamples(NULL)
    , _probeWrite(0)
    , _probeCount(0)
    , _probeTotal(0)
{
    memset(_buffers, 0, sizeof(_buffers));
    memset(_safe, 0, sizeof(_safe));
    memset(_forceFull, 0, sizeof(_forceFull));
    memset(_rowStride, 0, sizeof(_rowStride));
    memset(_interleaved, 0, sizeof(_interleaved));
    memset(_sourceShadow, 0, sizeof(_sourceShadow));
    memset(_rawIndexShadow, 0, sizeof(_rawIndexShadow));
    memset(_planarShadow, 0, sizeof(_planarShadow));
    memset(_interleavedRowShadow, 0, sizeof(_interleavedRowShadow));
    memset(_palettePending, 0, sizeof(_palettePending));
    memset(_palettePendingAny, 0, sizeof(_palettePendingAny));
    memset(_probeGameName, 0, sizeof(_probeGameName));
    memset(_probeGameDescription, 0, sizeof(_probeGameDescription));
    _paletteEntries = 0;
    _paletteError = NULL;
    _paletteCommand = NULL;
}

Ham6EuaeOutput::~Ham6EuaeOutput()
{
    close();
}

bool Ham6EuaeOutput::isHam6ModeId(ULONG modeId)
{
#ifdef DIPF_IS_HAM
    struct DisplayInfo displayInfo;

    if(modeId == INVALID_ID)
        return false;

    memset(&displayInfo, 0, sizeof(displayInfo));
    if(GetDisplayInfoData(
            NULL,
            (UBYTE *)&displayInfo,
            sizeof(displayInfo),
            DTAG_DISP,
            modeId) <= 0)
        return false;

    return (displayInfo.PropertyFlags & DIPF_IS_HAM) != 0;
#else
    (void)modeId;
    return false;
#endif
}

void *Ham6EuaeOutput::allocFast(ULONG bytes, const char *name)
{
    APTR memory = AllocVec(bytes, MEMF_FAST | MEMF_CLEAR);
    ULONG memoryType;

    if(!memory)
        memory = AllocVec(bytes, MEMF_PUBLIC | MEMF_CLEAR);

    if(!memory)
    {
        printf(
            "FRF FIX70 HAM6: unable to allocate %s (%lu bytes)\n",
            name,
            (unsigned long)bytes);
        return NULL;
    }

    memoryType = TypeOfMem(memory);
    printf(
        "FRF FIX70 HAM6: %s %lu bytes%s%s\n",
        name,
        (unsigned long)bytes,
        (memoryType & MEMF_FAST) ? " FAST" : "",
        (memoryType & MEMF_CHIP) ? " CHIP" : "");
    return memory;
}

void Ham6EuaeOutput::resetState()
{
    _screen = NULL;
    _safePort = NULL;
    _front = 0;
    _back = 1;
    _safe[0] = 0;
    _safe[1] = 1;
    _forceFull[0] = 1;
    _forceFull[1] = 1;
    _active = 0;
    _directFront = 0;
    _paletteValid = 0;
    _reportedIncompatibleFrame = 0;
    _swaps = 0;
    _swapMisses = 0;
    _busyFrames = 0;
    _renderedFrames = 0;
    _renderFailures = 0;
    _chipBytes = 0;
    _dirtyRuns = 0;
    _convertedLines = 0;
    _reusedLines = 0;
    _zeroWriteFrames = 0;
    _fullishFrames = 0;
    _lastFrameBytes = 0;
    _drawCalls = 0;
    _directFrontFrames = 0;
    _indexedFastFrames = 0;
    _indexedChangedLines = 0;
    _indexedReusedLines = 0;
    _paletteUpdateEvents = 0;
    _paletteActualChanges = 0;
    _interleavedBurstFrames = 0;
    _interleavedBurstLines = 0;
    _interleavedBurstBytes = 0;
    _indexedFastReported = 0;
    _interleavedBurstReported = 0;
    _rotatedIndexedReported = 0;
    _wideIndexedReported = 0;
    _smoothHeavyStreak = 0;
    _smoothSkipPending = 0;
    _smoothCooldown = 0;
    _smoothHeavyFrames = 0;
    _smoothSkippedFrames = 0;
    _probeWrite = 0;
    _probeCount = 0;
    _probeTotal = 0;
    _paletteEntries = 0;
    _palettePendingAny[0] = 0;
    _palettePendingAny[1] = 0;

    memset(_buffers, 0, sizeof(_buffers));
    memset(_rowStride, 0, sizeof(_rowStride));
    memset(_interleaved, 0, sizeof(_interleaved));
}

void Ham6EuaeOutput::freeBuffers()
{
    int i;

    for(i = 0; i < 2; i++)
    {
        if(_sourceShadow[i])
        {
            FreeVec(_sourceShadow[i]);
            _sourceShadow[i] = NULL;
        }
        if(_rawIndexShadow[i])
        {
            FreeVec(_rawIndexShadow[i]);
            _rawIndexShadow[i] = NULL;
        }
        if(_planarShadow[i])
        {
            FreeVec(_planarShadow[i]);
            _planarShadow[i] = NULL;
        }
        if(_interleavedRowShadow[i])
        {
            FreeVec(_interleavedRowShadow[i]);
            _interleavedRowShadow[i] = NULL;
        }
        if(_palettePending[i])
        {
            FreeVec(_palettePending[i]);
            _palettePending[i] = NULL;
        }
    }

    if(_probeSamples)
    {
        FreeVec(_probeSamples);
        _probeSamples = NULL;
    }

    if(_rgbLine)
    {
        FreeVec(_rgbLine);
        _rgbLine = NULL;
    }
    if(_palette15)
    {
        FreeVec(_palette15);
        _palette15 = NULL;
    }
    if(_directError)
    {
        FreeVec(_directError);
        _directError = NULL;
    }
    if(_directCommand)
    {
        FreeVec(_directCommand);
        _directCommand = NULL;
    }
    if(_holdChoice)
    {
        FreeVec(_holdChoice);
        _holdChoice = NULL;
    }
    if(_paletteError)
    {
        FreeVec(_paletteError);
        _paletteError = NULL;
    }
    if(_paletteCommand)
    {
        FreeVec(_paletteCommand);
        _paletteCommand = NULL;
    }
}

int Ham6EuaeOutput::distance4(LONG rgb1, LONG rgb2)
{
    int distance = 0;
    int difference;

    difference = (rgb1 & 0xF00) - (rgb2 & 0xF00);
    difference >>= 8;
    distance += difference < 0 ? -difference : difference;

    difference = (rgb1 & 0x0F0) - (rgb2 & 0x0F0);
    difference >>= 4;
    distance += difference < 0 ? -difference : difference;

    difference = (rgb1 & 0x00F) - (rgb2 & 0x00F);
    distance += difference < 0 ? -difference : difference;

    return distance;
}

bool Ham6EuaeOutput::initialiseEncoderTables()
{
    int rgb12;
    int i;

    _directError = (UBYTE *)allocFast(DIRECT_TABLE_SIZE, "direct-error");
    _directCommand = (UBYTE *)allocFast(DIRECT_TABLE_SIZE, "direct-command");
    _holdChoice = (UBYTE *)allocFast(HOLD_TABLE_SIZE, "hold-choice");

    if(!_directError || !_directCommand || !_holdChoice)
        return false;

    /*
     * This is the original E-UAE HAM6 precalculation. The RGB12 value is
     * expanded into three five-bit fields whose values remain 0..15.
     */
    for(rgb12 = 0; rgb12 < 4096; rgb12++)
    {
        int bestColour = 50;
        int bestDistance = 50;
        int tableIndex;

        for(i = 0; i < 16; i++)
        {
            int distance = distance4(i * 0x111, rgb12);
            if(distance < bestDistance)
            {
                bestDistance = distance;
                bestColour = i;
            }
        }

        tableIndex =
            (rgb12 & 0x00F) |
            ((rgb12 & 0x0F0) << 1) |
            ((rgb12 & 0xF00) << 2);

        _directError[tableIndex] =
            (UBYTE)((bestDistance << 2) | 3);
        _directCommand[tableIndex] = (UBYTE)bestColour;
    }

    for(i = 0; i < HOLD_TABLE_SIZE; i++)
    {
        int dr = ((i >> 10) & 0x1F) - 0x10;
        int dg = ((i >> 5) & 0x1F) - 0x10;
        int db = (i & 0x1F) - 0x10;
        int bestCommand = 0;
        int bestDistance = 50;
        int test;

        if(dr < 0) dr = -dr;
        if(dg < 0) dg = -dg;
        if(db < 0) db = -db;

        test = distance4(0, dg * 16 + db);
        if(test < bestDistance)
        {
            bestDistance = test;
            bestCommand = 0;
        }

        test = distance4(0, dr * 256 + db);
        if(test < bestDistance)
        {
            bestDistance = test;
            bestCommand = 1;
        }

        test = distance4(0, dr * 256 + dg * 16);
        if(test < bestDistance)
        {
            bestDistance = test;
            bestCommand = 2;
        }

        _holdChoice[i] =
            (UBYTE)((bestDistance << 2) | bestCommand);
    }

    return true;
}

bool Ham6EuaeOutput::installBasePalette()
{
    ULONG palette[16 * 3 + 2];
    ULONG *destination = palette;
    int i;

    if(!_screen)
        return false;

    *destination++ = (16UL << 16) | 0UL;

    for(i = 0; i < 16; i++)
    {
        ULONG component = (ULONG)(i * 17) << 24;
        *destination++ = component;
        *destination++ = component;
        *destination++ = component;
    }

    *destination = 0;
    LoadRGB32(&_screen->ViewPort, palette);
    return true;
}

bool Ham6EuaeOutput::bitmapLayout(
    BitMap *bitmap,
    ULONG *rowStride,
    int *interleaved)
{
    ULONG bytesPerRow;
    ULONG planeSpan;
    int p;
    int q;
    int exactInterleaved = 1;

    if(!bitmap || !rowStride || !interleaved ||
       bitmap->Depth < PLANES ||
       bitmap->BytesPerRow < 40 ||
       bitmap->Rows < HEIGHT)
        return false;

    for(p = 0; p < PLANES; p++)
        if(!bitmap->Planes[p])
            return false;

    bytesPerRow = (ULONG)bitmap->BytesPerRow;

    for(p = 1; p < PLANES; p++)
    {
        if((ULONG)bitmap->Planes[p] !=
           (ULONG)bitmap->Planes[0] + (ULONG)p * bytesPerRow)
        {
            exactInterleaved = 0;
            break;
        }
    }

    if(exactInterleaved)
    {
        *rowStride = bytesPerRow * (ULONG)bitmap->Depth;
        *interleaved = 1;
        return true;
    }

    planeSpan = bytesPerRow * (ULONG)bitmap->Rows;
    for(p = 0; p < PLANES; p++)
    {
        ULONG firstAddress = (ULONG)bitmap->Planes[p];

        for(q = p + 1; q < PLANES; q++)
        {
            ULONG secondAddress = (ULONG)bitmap->Planes[q];

            if(firstAddress < secondAddress + planeSpan &&
               secondAddress < firstAddress + planeSpan)
                return false;
        }
    }

    *rowStride = bytesPerRow;
    *interleaved = 0;
    return true;
}

void Ham6EuaeOutput::clearScreenBuffers()
{
    RastPort rastPort;
    int i;

    InitRastPort(&rastPort);

    for(i = 0; i < 2; i++)
    {
        if(_buffers[i] && _buffers[i]->sb_BitMap)
        {
            rastPort.BitMap = _buffers[i]->sb_BitMap;
            SetRast(&rastPort, 0);
        }

        if(_sourceShadow[i])
            memset(
                _sourceShadow[i],
                0,
                SOURCE_WORDS * sizeof(UWORD));
        if(_rawIndexShadow[i])
            memset(
                _rawIndexShadow[i],
                0,
                SOURCE_WORDS * sizeof(UWORD));
        if(_palettePending[i])
            memset(
                _palettePending[i],
                0,
                PALETTE_DIRTY_WORDS * sizeof(ULONG));
        _palettePendingAny[i] = 0;
        if(_planarShadow[i])
            memset(
                _planarShadow[i],
                0,
                PLANAR_WORDS * sizeof(UWORD));
        if(_interleavedRowShadow[i])
            memset(
                _interleavedRowShadow[i],
                0,
                PLANAR_WORDS * sizeof(UWORD));

        _forceFull[i] = 1;
    }

    WaitBlit();
}

bool Ham6EuaeOutput::open()
{
    BitMap *displayBitmap;
    RastPort clearPort;
    int i;

    if(_active)
        return true;

    _screen = _drawable.screen();
    if(!_screen || !_screen->RastPort.BitMap ||
       _screen->RastPort.BitMap->Depth != PLANES ||
       _screen->Width != WIDTH || _screen->Height != HEIGHT ||
       !isHam6ModeId(GetVPModeID(&_screen->ViewPort)))
    {
        printf("FRF FIX73 HAM6: genuine 320x256 six-plane HAM required\n");
        resetState();
        return false;
    }

    _palette15 = (UWORD *)allocFast(PALETTE_TABLE_SIZE * sizeof(UWORD), "MAME-palette-to-RGB15");
    _paletteError = (UBYTE *)allocFast(PALETTE_TABLE_SIZE, "indexed-direct-error");
    _paletteCommand = (UBYTE *)allocFast(PALETTE_TABLE_SIZE, "indexed-direct-command");
    _rgbLine = (UWORD *)allocFast(WIDTH * sizeof(UWORD), "RGB15-line");
    _probeSamples = allocFast(
        FRF85_PROBE_CAPACITY * sizeof(FrfFix85ProbeSample),
        "FIX85 per-game probe ring");
    _probeWrite = 0;
    _probeCount = 0;
    _probeTotal = 0;

    memset(_probeGameName, 0, sizeof(_probeGameName));
    memset(_probeGameDescription, 0, sizeof(_probeGameDescription));
    if(Machine && Machine->gamedrv)
    {
        if(Machine->gamedrv->name)
        {
            strncpy(
                _probeGameName,
                Machine->gamedrv->name,
                sizeof(_probeGameName) - 1);
            _probeGameName[sizeof(_probeGameName) - 1] = 0;
        }
        if(Machine->gamedrv->description)
        {
            strncpy(
                _probeGameDescription,
                Machine->gamedrv->description,
                sizeof(_probeGameDescription) - 1);
            _probeGameDescription[sizeof(_probeGameDescription) - 1] = 0;
        }
    }
    if(!_probeGameName[0])
        strcpy(_probeGameName, "unknown");

    for(i = 0; i < 2; i++)
    {
        _sourceShadow[i] = (UWORD *)allocFast(SOURCE_WORDS * sizeof(UWORD), i ? "source-shadow-1" : "source-shadow-0");
        _rawIndexShadow[i] = (UWORD *)allocFast(SOURCE_WORDS * sizeof(UWORD), i ? "raw-index-shadow-1" : "raw-index-shadow-0");
        _palettePending[i] = (ULONG *)allocFast(PALETTE_DIRTY_WORDS * sizeof(ULONG), i ? "palette-pending-1" : "palette-pending-0");
        _planarShadow[i] = (UWORD *)allocFast(PLANAR_WORDS * sizeof(UWORD), i ? "planar-shadow-1" : "planar-shadow-0");
        _interleavedRowShadow[i] = (UWORD *)allocFast(PLANAR_WORDS * sizeof(UWORD), i ? "interleaved-row-shadow-1" : "interleaved-row-shadow-0");
    }

    if(!_palette15 || !_paletteError || !_paletteCommand || !_rgbLine ||
       !_sourceShadow[0] || !_sourceShadow[1] ||
       !_rawIndexShadow[0] || !_rawIndexShadow[1] ||
       !_palettePending[0] || !_palettePending[1] ||
       !_planarShadow[0] || !_planarShadow[1] ||
       !_interleavedRowShadow[0] || !_interleavedRowShadow[1] ||
       !initialiseEncoderTables())
    {
        freeBuffers();
        resetState();
        return false;
    }

    if(!installBasePalette())
    {
        freeBuffers();
        resetState();
        return false;
    }

    displayBitmap = _screen->RastPort.BitMap;
    if(!bitmapLayout(displayBitmap, &_rowStride[0], &_interleaved[0]))
    {
        printf("FRF FIX73 HAM6: unsafe displayed bitmap layout\n");
        freeBuffers();
        resetState();
        return false;
    }

    _rowStride[1] = _rowStride[0];
    _interleaved[1] = _interleaved[0];
    _buffers[0] = NULL;
    _buffers[1] = NULL;
    _safePort = NULL;
    _front = _back = 0;
    _safe[0] = _safe[1] = 1;
    _forceFull[0] = _forceFull[1] = 1;
    _paletteValid = 0;

    InitRastPort(&clearPort);
    clearPort.BitMap = displayBitmap;
    SetRast(&clearPort, 0);
    WaitBlit();

    for(i = 0; i < 2; i++)
    {
        memset(_sourceShadow[i], 0, SOURCE_WORDS * sizeof(UWORD));
        memset(_rawIndexShadow[i], 0, SOURCE_WORDS * sizeof(UWORD));
        memset(_palettePending[i], 0, PALETTE_DIRTY_WORDS * sizeof(ULONG));
        memset(_planarShadow[i], 0, PLANAR_WORDS * sizeof(UWORD));
        memset(_interleavedRowShadow[i], 0, PLANAR_WORDS * sizeof(UWORD));
        _palettePendingAny[i] = 0;
    }

    _directFront = 1;
    _active = 1;
    printf("FRF FIX73 HAM6 DIRECT: displayed Chip RAM bitmap; no ScreenBuffer, no SafeMessage, no ChangeScreenBuffer; layout=%s rowstride=%lu\n",
           _interleaved[0] ? "interleaved" : "planar",
           (unsigned long)_rowStride[0]);
    printf("FRF FIX84 HAM6 CHIP: inline 1-8 word dirty runs; CopyMem for 9+ words\n");
    printf("FRF FIX85 PROBE: Fast-RAM per-draw capture active; capacity=%lu samples; gameplay I/O=0\n",
           (unsigned long)(_probeSamples ? FRF85_PROBE_CAPACITY : 0));
    printf(
        "FRF FIX85B AUTOLOG: game=%s; each session saves to "
        "PROGDIR:HAMProbeLogs/<game>_<session>.log\n",
        _probeGameName);
    return true;
}

void Ham6EuaeOutput::drainSafeMessages()
{
    Message *message;
    int i;

    if(!_safePort)
        return;

    while((message = GetMsg(_safePort)) != NULL)
    {
        for(i = 0; i < 2; i++)
        {
            if(_buffers[i] &&
               _buffers[i]->sb_DBufInfo &&
               message ==
                   &_buffers[i]->sb_DBufInfo->dbi_SafeMessage)
            {
                _safe[i ^ 1] = 1;
                _buffers[i]->sb_DBufInfo->
                    dbi_SafeMessage.mn_ReplyPort = NULL;
                break;
            }
        }
    }
}

void Ham6EuaeOutput::close()
{
    Screen *screen = _screen;

    if(!_active &&
       !_safePort &&
       !_buffers[0] &&
       !_buffers[1] &&
       !_sourceShadow[0] &&
       !_sourceShadow[1] &&
       !_rawIndexShadow[0] &&
       !_rawIndexShadow[1] &&
       !_interleavedRowShadow[0] &&
       !_interleavedRowShadow[1])
        return;

    printf(
        "FRF FIX70 HAM6: swaps=%lu misses=%lu busy=%lu rendered=%lu "
        "fail=%lu\n",
        (unsigned long)_swaps,
        (unsigned long)_swapMisses,
        (unsigned long)_busyFrames,
        (unsigned long)_renderedFrames,
        (unsigned long)_renderFailures);

    printf(
        "FRF FIX70 HAM6: chipbytes=%lu avg=%lu runs=%lu zero=%lu "
        "fullish=%lu converted-lines=%lu reused-lines=%lu\n",
        (unsigned long)_chipBytes,
        (unsigned long)(_renderedFrames
            ? _chipBytes / _renderedFrames
            : 0),
        (unsigned long)_dirtyRuns,
        (unsigned long)_zeroWriteFrames,
        (unsigned long)_fullishFrames,
        (unsigned long)_convertedLines,
        (unsigned long)_reusedLines);
    printf(
        "FRF FIX71 HAM6 FAST: indexed-frames=%lu changed-lines=%lu "
        "reused-lines=%lu palette-events=%lu actual-palette-changes=%lu\n",
        (unsigned long)_indexedFastFrames,
        (unsigned long)_indexedChangedLines,
        (unsigned long)_indexedReusedLines,
        (unsigned long)_paletteUpdateEvents,
        (unsigned long)_paletteActualChanges);
    printf(
        "FRF FIX72 HAM6 BURST: frames=%lu lines=%lu bytes=%lu avg=%lu\n",
        (unsigned long)_interleavedBurstFrames,
        (unsigned long)_interleavedBurstLines,
        (unsigned long)_interleavedBurstBytes,
        (unsigned long)(_interleavedBurstFrames
            ? _interleavedBurstBytes / _interleavedBurstFrames
            : 0));

    printf("FRF FIX73 HAM6 DIRECT: drawcalls=%lu directframes=%lu rendered=%lu fail=%lu chipbytes=%lu avg=%lu\n",
           (unsigned long)_drawCalls,
           (unsigned long)_directFrontFrames,
           (unsigned long)_renderedFrames,
           (unsigned long)_renderFailures,
           (unsigned long)_chipBytes,
           (unsigned long)(_directFrontFrames ? _chipBytes / _directFrontFrames : 0));

    printf(
        "FRF FIX82 HAM6 SMOOTH: heavy=%lu repeated=%lu "
        "repeat-rate=%lu/1000 cooldown=%d\n",
        (unsigned long)_smoothHeavyFrames,
        (unsigned long)_smoothSkippedFrames,
        (unsigned long)(_drawCalls
            ? (_smoothSkippedFrames * 1000UL) / _drawCalls
            : 0),
        _smoothCooldown);

    /* FRF_FIX85B_AUTO_PER_GAME_LOGS
     *
     * All gameplay capture remained in Fast RAM. Only now, after the game
     * backend is closing, write one self-contained log named after the MAME
     * shortname. No gameplay-time disk I/O is introduced.
     */
    if(_probeSamples && _probeCount > 0)
    {
        FrfFix85ProbeSample *samples =
            (FrfFix85ProbeSample *)_probeSamples;
        ULONG count = _probeCount;
        ULONG oldest =
            count == FRF85_PROBE_CAPACITY ? _probeWrite : 0;
        ULONG n;
        cycles_t cps = osd_cycles_per_second();
        cycles_t baseTick = samples[oldest].tick;
        struct DateStamp ds;
        char logPath[192];
        FILE *probeFile = NULL;
        BPTR logDir = 0;

        memset(&ds, 0, sizeof(ds));
        DateStamp(&ds);

        logDir = Lock((STRPTR)"PROGDIR:HAMProbeLogs", ACCESS_READ);
        if(!logDir)
            logDir = CreateDir((STRPTR)"PROGDIR:HAMProbeLogs");
        if(logDir)
            UnLock(logDir);

        sprintf(
            logPath,
            "PROGDIR:HAMProbeLogs/%s_%ld_%ld_%ld.log",
            _probeGameName[0] ? _probeGameName : "unknown",
            (long)ds.ds_Days,
            (long)ds.ds_Minute,
            (long)ds.ds_Tick);

        probeFile = fopen(logPath, "w");

        if(probeFile)
        {
            fprintf(
                probeFile,
                "FRF85B GAME shortname=%s description=%s\n",
                _probeGameName[0] ? _probeGameName : "unknown",
                _probeGameDescription[0]
                    ? _probeGameDescription
                    : "(unknown)");
            fprintf(
                probeFile,
                "FRF85B SESSION days=%ld minute=%ld tick=%ld cps=%lu\n",
                (long)ds.ds_Days,
                (long)ds.ds_Minute,
                (long)ds.ds_Tick,
                (unsigned long)cps);
            fprintf(
                probeFile,
                "FRF85B SUMMARY drawcalls=%lu rendered=%lu "
                "chipbytes=%lu chipavg=%lu runs=%lu "
                "converted=%lu reused=%lu\n",
                (unsigned long)_drawCalls,
                (unsigned long)_renderedFrames,
                (unsigned long)_chipBytes,
                (unsigned long)(_renderedFrames
                    ? _chipBytes / _renderedFrames
                    : 0),
                (unsigned long)_dirtyRuns,
                (unsigned long)_convertedLines,
                (unsigned long)_reusedLines);
            fprintf(
                probeFile,
                "FRF85B INDEXED frames=%lu changed=%lu reused=%lu "
                "palette-events=%lu palette-changes=%lu\n",
                (unsigned long)_indexedFastFrames,
                (unsigned long)_indexedChangedLines,
                (unsigned long)_indexedReusedLines,
                (unsigned long)_paletteUpdateEvents,
                (unsigned long)_paletteActualChanges);
            fprintf(
                probeFile,
                "FRF85B SMOOTH heavy=%lu repeated=%lu "
                "repeat-rate=%lu/1000\n",
                (unsigned long)_smoothHeavyFrames,
                (unsigned long)_smoothSkippedFrames,
                (unsigned long)(_drawCalls
                    ? (_smoothSkippedFrames * 1000UL) / _drawCalls
                    : 0));
            fprintf(
                probeFile,
                "FRF85 PROBE HEADER game=%s count=%lu total=%lu "
                "retained=%lu wrapped=%lu cps=%lu\n",
                _probeGameName,
                (unsigned long)count,
                (unsigned long)_probeTotal,
                (unsigned long)count,
                (unsigned long)(_probeTotal > count
                    ? _probeTotal - count
                    : 0),
                (unsigned long)cps);
            fprintf(
                probeFile,
                "FRF85 PROBE COLUMNS seq t_ms draw render_us chip runs "
                "changed reused palette flags\n");

            for(n = 0; n < count; n++)
            {
                ULONG index =
                    (oldest + n) % FRF85_PROBE_CAPACITY;
                FrfFix85ProbeSample *s = &samples[index];
                cycles_t elapsed = s->tick - baseTick;
                ULONG tMs = 0;
                ULONG renderUs = 0;

                if(cps > 0)
                {
                    tMs = (ULONG)(
                        (elapsed / cps) * 1000UL +
                        ((elapsed % cps) * 1000UL) / cps);
                    renderUs = (ULONG)(
                        (s->renderTicks / cps) * 1000000UL +
                        ((s->renderTicks % cps) * 1000000UL) / cps);
                }

                fprintf(
                    probeFile,
                    "FRF85 PROBE %lu %lu %lu %lu %lu %lu "
                    "%lu %lu %lu %lu\n",
                    (unsigned long)n,
                    (unsigned long)tMs,
                    (unsigned long)s->drawCall,
                    (unsigned long)renderUs,
                    (unsigned long)s->chipBytes,
                    (unsigned long)s->dirtyRuns,
                    (unsigned long)s->changedLines,
                    (unsigned long)s->reusedLines,
                    (unsigned long)s->paletteChanges,
                    (unsigned long)s->flags);
            }

            fprintf(probeFile, "FRF85 PROBE END\n");
            fclose(probeFile);

            printf(
                "FRF FIX85B AUTOLOG SAVED: %s (%lu samples)\n",
                logPath,
                (unsigned long)count);
        }
        else
        {
            printf(
                "FRF FIX85B AUTOLOG ERROR: could not create %s; "
                "probe remained in memory until close\n",
                logPath);
        }
    }

    if(screen)
    {
        if(_buffers[0] && _front != 0)
        {
            int guard = 8;

            while(!ChangeScreenBuffer(screen, _buffers[0]) &&
                  guard-- > 0)
                WaitTOF();

            _front = 0;
            _back = 1;
        }

        WaitTOF();
        drainSafeMessages();

        if(_buffers[0] && _buffers[0]->sb_DBufInfo)
        {
            _buffers[0]->sb_DBufInfo->
                dbi_SafeMessage.mn_ReplyPort = NULL;
            _buffers[0]->sb_DBufInfo->
                dbi_DispMessage.mn_ReplyPort = NULL;
        }
        if(_buffers[1] && _buffers[1]->sb_DBufInfo)
        {
            _buffers[1]->sb_DBufInfo->
                dbi_SafeMessage.mn_ReplyPort = NULL;
            _buffers[1]->sb_DBufInfo->
                dbi_DispMessage.mn_ReplyPort = NULL;
        }

        if(_buffers[1])
            FreeScreenBuffer(screen, _buffers[1]);
        if(_buffers[0])
            FreeScreenBuffer(screen, _buffers[0]);
    }

    _buffers[0] = NULL;
    _buffers[1] = NULL;

    if(_safePort)
    {
        drainSafeMessages();
        DeleteMsgPort(_safePort);
    }
    _safePort = NULL;

    freeBuffers();
    resetState();
}

bool Ham6EuaeOutput::updatePalette(_mame_display *display)
{
    int entries;
    int previousEntries;
    int i;
    int firstLoad;
    ULONG actualChanges = 0;

    if(!display)
        return false;

    if((_videoAttributes & VIDEO_RGB_DIRECT) != 0)
        return true;

    if(!display->game_palette)
        return false;

    firstLoad = !_paletteValid;
    if(!firstLoad &&
       (display->changed_flags & GAME_PALETTE_CHANGED) == 0)
        return true;

    _paletteUpdateEvents++;

    entries = (int)display->game_palette_entries;
    if(entries > PALETTE_TABLE_SIZE)
        entries = PALETTE_TABLE_SIZE;
    if(entries < 0)
        entries = 0;

    previousEntries = _paletteEntries;

    if(firstLoad)
    {
        memset(
            _palette15,
            0,
            PALETTE_TABLE_SIZE * sizeof(UWORD));
        memset(
            _paletteCommand,
            _directCommand[0],
            PALETTE_TABLE_SIZE);
        memset(
            _paletteError,
            _directError[0],
            PALETTE_TABLE_SIZE);
    }

    for(i = 0; i < entries; i++)
    {
        ULONG colour;
        UWORD rgb15;
        int isDirty = firstLoad;

        if(!isDirty)
        {
            if(display->game_palette_dirty)
            {
                isDirty =
                    (display->game_palette_dirty[i >> 5] &
                     (1UL << (i & 31))) != 0;
            }
            else
            {
                isDirty = 1;
            }
        }

        if(!isDirty)
            continue;

        colour = display->game_palette[i];
        rgb15 = (UWORD)(
            (((colour >> 20) & 0x0f) << 10) |
            (((colour >> 12) & 0x0f) << 5) |
            ((colour >> 4) & 0x0f));

        if(firstLoad || _palette15[i] != rgb15)
        {
            _palette15[i] = rgb15;
            _paletteCommand[i] = _directCommand[rgb15];
            _paletteError[i] = _directError[rgb15];
            actualChanges++;

            if(!firstLoad)
            {
                ULONG mask = 1UL << (i & 31);
                _palettePending[0][i >> 5] |= mask;
                _palettePending[1][i >> 5] |= mask;
                _palettePendingAny[0] = 1;
                _palettePendingAny[1] = 1;
            }
        }
    }

    if(!firstLoad && previousEntries > entries)
    {
        for(i = entries; i < previousEntries; i++)
        {
            if(_palette15[i] != 0)
            {
                ULONG mask = 1UL << (i & 31);

                _palette15[i] = 0;
                _paletteCommand[i] = _directCommand[0];
                _paletteError[i] = _directError[0];
                _palettePending[0][i >> 5] |= mask;
                _palettePending[1][i >> 5] |= mask;
                _palettePendingAny[0] = 1;
                _palettePendingAny[1] = 1;
                actualChanges++;
            }
        }
    }

    _paletteEntries = entries;
    _paletteValid = 1;
    _paletteActualChanges += actualChanges;

    if(firstLoad)
    {
        _forceFull[0] = 1;
        _forceFull[1] = 1;
    }

    return true;
}

UWORD Ham6EuaeOutput::quantiseArgb32(ULONG colour) const
{
    return (UWORD)(
        ((((colour >> 16) & 0xff) >> 4) << 10) |
        ((((colour >> 8) & 0xff) >> 4) << 5) |
        ((colour & 0xff) >> 4));
}

UWORD Ham6EuaeOutput::quantiseRgb15(UWORD pixel) const
{
    return (UWORD)(
        ((((pixel >> 10) & 0x1f) >> 1) << 10) |
        ((((pixel >> 5) & 0x1f) >> 1) << 5) |
        ((pixel & 0x1f) >> 1));
}

UWORD Ham6EuaeOutput::quantiseRgb16(UWORD pixel) const
{
    return (UWORD)(
        ((((pixel >> 11) & 0x1f) >> 1) << 10) |
        ((((pixel >> 5) & 0x3f) >> 2) << 5) |
        ((pixel & 0x1f) >> 1));
}

UBYTE Ham6EuaeOutput::encodePixel(
    UWORD pixel,
    ULONG *rgbState) const
{
    static const UBYTE holdCommand[3] = {
        32 + 10,
        48 + 5,
        16 + 0
    };

    ULONG rgb = *rgbState;
    ULONG requested = (ULONG)pixel;
    UBYTE command = _directCommand[requested];
    UBYTE hold =
        _holdChoice[16912UL + requested - rgb];

    if(hold <= _directError[requested])
    {
        ULONG mask;
        int high;

        hold &= 3;
        high = holdCommand[hold];
        mask = 0x1FUL << (high & 15);
        rgb = (rgb & ~mask) | (requested & mask);
        command = (UBYTE)(
            (high & ~15) |
            ((requested & mask) >> (high & 15)));
    }
    else
    {
        rgb = command;
        rgb = (rgb << 5) | command;
        rgb = (rgb << 5) | command;
    }

    *rgbState = rgb;
    return command;
}

inline __attribute__((always_inline))
UBYTE Ham6EuaeOutput::encodeIndexedPixel(
    UWORD pen,
    ULONG *rgbState) const
{
    static const UBYTE holdCommand[3] = {
        32 + 10,
        48 + 5,
        16 + 0
    };

    ULONG rgb = *rgbState;
    ULONG requested = (ULONG)_palette15[pen];
    UBYTE command = _paletteCommand[pen];
    UBYTE hold =
        _holdChoice[16912UL + requested - rgb];

    if(hold <= _paletteError[pen])
    {
        ULONG mask;
        int high;

        hold &= 3;
        high = holdCommand[hold];
        mask = 0x1FUL << (high & 15);
        rgb = (rgb & ~mask) | (requested & mask);
        command = (UBYTE)(
            (high & ~15) |
            ((requested & mask) >> (high & 15)));
    }
    else
    {
        rgb = command;
        rgb = (rgb << 5) | command;
        rgb = (rgb << 5) | command;
    }

    *rgbState = rgb;
    return command;
}


void Ham6EuaeOutput::encodeAndPackLine(
    const UWORD *source,
    UWORD packed[PLANES][WORDS_PER_ROW]) const
{
    ULONG rgbState = 0;
    int wordIndex;

    for(wordIndex = 0;
        wordIndex < WORDS_PER_ROW;
        wordIndex++)
    {
        UWORD word0 = 0;
        UWORD word1 = 0;
        UWORD word2 = 0;
        UWORD word3 = 0;
        UWORD word4 = 0;
        UWORD word5 = 0;
        int pair;

        for(pair = 0; pair < 8; pair++)
        {
            UBYTE command0 =
                encodePixel(*source++, &rgbState);
            UBYTE command1 =
                encodePixel(*source++, &rgbState);

            word0 = (UWORD)(
                (word0 << 2) |
                ((command0 & 0x01) << 1) |
                (command1 & 0x01));
            word1 = (UWORD)(
                (word1 << 2) |
                (command0 & 0x02) |
                ((command1 & 0x02) >> 1));
            word2 = (UWORD)(
                (word2 << 2) |
                ((command0 & 0x04) >> 1) |
                ((command1 & 0x04) >> 2));
            word3 = (UWORD)(
                (word3 << 2) |
                ((command0 & 0x08) >> 2) |
                ((command1 & 0x08) >> 3));
            word4 = (UWORD)(
                (word4 << 2) |
                ((command0 & 0x10) >> 3) |
                ((command1 & 0x10) >> 4));
            word5 = (UWORD)(
                (word5 << 2) |
                ((command0 & 0x20) >> 4) |
                ((command1 & 0x20) >> 5));
        }

        packed[0][wordIndex] = word0;
        packed[1][wordIndex] = word1;
        packed[2][wordIndex] = word2;
        packed[3][wordIndex] = word3;
        packed[4][wordIndex] = word4;
        packed[5][wordIndex] = word5;
    }
}

void Ham6EuaeOutput::encodeAndPackIndexedLine(
    const UWORD *source,
    UWORD packed[PLANES][WORDS_PER_ROW]) const
{
    ULONG rgbState = 0;
    int wordIndex;

    for(wordIndex = 0;
        wordIndex < WORDS_PER_ROW;
        wordIndex++)
    {
        UWORD word0 = 0;
        UWORD word1 = 0;
        UWORD word2 = 0;
        UWORD word3 = 0;
        UWORD word4 = 0;
        UWORD word5 = 0;
        int pair;

        for(pair = 0; pair < 8; pair++)
        {
            UBYTE command0 =
                encodeIndexedPixel(*source++, &rgbState);
            UBYTE command1 =
                encodeIndexedPixel(*source++, &rgbState);

            word0 = (UWORD)(
                (word0 << 2) |
                ((command0 & 0x01) << 1) |
                (command1 & 0x01));
            word1 = (UWORD)(
                (word1 << 2) |
                (command0 & 0x02) |
                ((command1 & 0x02) >> 1));
            word2 = (UWORD)(
                (word2 << 2) |
                ((command0 & 0x04) >> 1) |
                ((command1 & 0x04) >> 2));
            word3 = (UWORD)(
                (word3 << 2) |
                ((command0 & 0x08) >> 2) |
                ((command1 & 0x08) >> 3));
            word4 = (UWORD)(
                (word4 << 2) |
                ((command0 & 0x10) >> 3) |
                ((command1 & 0x10) >> 4));
            word5 = (UWORD)(
                (word5 << 2) |
                ((command0 & 0x20) >> 4) |
                ((command1 & 0x20) >> 5));
        }

        packed[0][wordIndex] = word0;
        packed[1][wordIndex] = word1;
        packed[2][wordIndex] = word2;
        packed[3][wordIndex] = word3;
        packed[4][wordIndex] = word4;
        packed[5][wordIndex] = word5;
    }
}

/* FRF_FIX81_ROTATED_ACTIVE_SPAN
 * Encode only the word-aligned game span. The left border is direct black,
 * so rgbState=0 is identical to encoding the unchanged black border first.
 */
void Ham6EuaeOutput::encodeAndPackIndexedSpan(
    const UWORD *source,
    int pixelCount,
    int firstWord,
    UWORD packed[PLANES][WORDS_PER_ROW]) const
{
    ULONG rgbState = 0;
    int spanWords = pixelCount >> 4;
    int localWord;
    for(localWord = 0; localWord < spanWords; localWord++)
    {
        UWORD w0=0,w1=0,w2=0,w3=0,w4=0,w5=0;
        int pair;
        for(pair = 0; pair < 8; pair++)
        {
            UBYTE c0 = encodeIndexedPixel(*source++, &rgbState);
            UBYTE c1 = encodeIndexedPixel(*source++, &rgbState);
            w0=(UWORD)((w0<<2)|((c0&1)<<1)|(c1&1));
            w1=(UWORD)((w1<<2)|(c0&2)|((c1&2)>>1));
            w2=(UWORD)((w2<<2)|((c0&4)>>1)|((c1&4)>>2));
            w3=(UWORD)((w3<<2)|((c0&8)>>2)|((c1&8)>>3));
            w4=(UWORD)((w4<<2)|((c0&16)>>3)|((c1&16)>>4));
            w5=(UWORD)((w5<<2)|((c0&32)>>4)|((c1&32)>>5));
        }
        {
            int wi = firstWord + localWord;
            packed[0][wi]=w0; packed[1][wi]=w1; packed[2][wi]=w2;
            packed[3][wi]=w3; packed[4][wi]=w4; packed[5][wi]=w5;
        }
    }
}



/* FRF_FIX84_SHORT_RUN_CHIP_WRITER
 *
 * Mortal Kombat's measured HAM output averaged only about five 16-bit words
 * per dirty run. Calling Exec CopyMem() for runs this small pays library-call
 * setup repeatedly while still performing the same Chip-RAM stores.
 *
 * Keep long copies on the proven Exec path, but perform 1..8 word runs as
 * direct UWORD stores. This changes neither the amount nor the contents of
 * video data written; it only removes short-copy call overhead.
 */
void Ham6EuaeOutput::copyWordRun(
    UWORD *destination,
    const UWORD *source,
    int words)
{
    if(words <= 0)
        return;

    if(words <= 8)
    {
        while(words-- > 0)
            *destination++ = *source++;
        return;
    }

    CopyMem(
        (APTR)source,
        (APTR)destination,
        words * sizeof(UWORD));
}

ULONG Ham6EuaeOutput::copyChangedPlaneRow(
    UWORD *destination,
    UWORD *shadow,
    const UWORD *packed,
    ULONG *runCounter)
{
    ULONG changedMask = 0;
    int changedCount = 0;
    int i;

    for(i = 0; i < WORDS_PER_ROW; i++)
    {
        if(packed[i] != shadow[i])
        {
            shadow[i] = packed[i];
            changedMask |= 1UL << i;
            changedCount++;
        }
    }

    if(changedMask == 0)
        return 0;

    /*
     * A heavily changed scrolling row is cheaper as one 40-byte CopyMem
     * than as many short Exec calls.
     */
    if(changedCount >= 12)
    {
        CopyMem(
            (APTR)shadow,
            (APTR)destination,
            WORDS_PER_ROW * sizeof(UWORD));
        (*runCounter)++;
        return WORDS_PER_ROW * sizeof(UWORD);
    }

    {
        ULONG bytes = 0;
        int word = 0;

        while(word < WORDS_PER_ROW)
        {
            int start;
            int words;

            while(word < WORDS_PER_ROW &&
                  (changedMask & (1UL << word)) == 0)
                word++;

            if(word >= WORDS_PER_ROW)
                break;

            start = word;
            while(word < WORDS_PER_ROW &&
                  (changedMask & (1UL << word)) != 0)
                word++;

            words = word - start;
            copyWordRun(
                destination + start,
                shadow + start,
                words);
            bytes += words * sizeof(UWORD);
            (*runCounter)++;
        }

        return bytes;
    }
}

/* E-UAE-style contiguous changed runs restricted to the active game words. */
ULONG Ham6EuaeOutput::copyChangedPlaneSpan(
    UWORD *destination, UWORD *shadow, const UWORD *packed,
    int firstWord, int wordCount, ULONG *runCounter)
{
    ULONG bytes = 0;
    int endWord = firstWord + wordCount;
    int word = firstWord;
    while(word < endWord)
    {
        int start, words;
        while(word < endWord && packed[word] == shadow[word]) word++;
        if(word >= endWord) break;
        start = word;
        while(word < endWord && packed[word] != shadow[word])
        {
            shadow[word] = packed[word];
            word++;
        }
        words = word - start;
        copyWordRun(destination + start, shadow + start, words);
        bytes += words * sizeof(UWORD);
        (*runCounter)++;
    }
    return bytes;
}



void Ham6EuaeOutput::copyAlignedBurst(
    void *destination,
    const void *source,
    ULONG bytes)
{
    if((((ULONG)destination | (ULONG)source | bytes) & 3UL) == 0)
    {
        CopyMemQuick((APTR)source, (APTR)destination, bytes);
    }
    else
    {
        CopyMem((APTR)source, (APTR)destination, bytes);
    }
}


bool Ham6EuaeOutput::lineUsesPendingPalette(
    const UWORD *source,
    const ULONG *pending) const
{
    int i;

    for(i = 0; i < WIDTH; i++)
    {
        UWORD pen = source[i];

        if(pending[pen >> 5] &
           (1UL << (pen & 31)))
            return true;
    }

    return false;
}

bool Ham6EuaeOutput::spanUsesPendingPalette(
    const UWORD *source, int pixelCount, const ULONG *pending) const
{
    int i;
    for(i = 0; i < pixelCount; i++)
    {
        UWORD pen = source[i];
        if(pending[pen >> 5] & (1UL << (pen & 31))) return true;
    }
    return false;
}



bool Ham6EuaeOutput::prepareRgbLine(
    _mame_display *display,
    int outputY,
    int sourceWidth,
    int sourceHeight,
    int offsetX,
    int offsetY)
{
    /* FRF_FIX77_WIDE_FRAME_HANDOFF
     *
     * Read MAME's raw visible bitmap in final display orientation. If the
     * oriented image is wider than the 320-pixel HAM surface, resample only
     * the horizontal axis to 320. Heights above 256 remain unsupported.
     * For <=320-pixel images targetWidth == orientedWidth, making the mapping
     * one-to-one and preserving the proven Neo Geo and FIX76 rotation paths.
     */
    mame_bitmap *bitmap = display->game_bitmap;
    const rectangle &visible = display->game_visible_area;
    int orientation = _drawable.flags() & ORIENTATION_MASK;
    int orientedWidth =
        (orientation & ORIENTATION_SWAP_XY)
            ? sourceHeight
            : sourceWidth;
    int orientedHeight =
        (orientation & ORIENTATION_SWAP_XY)
            ? sourceWidth
            : sourceHeight;
    int targetWidth = orientedWidth > WIDTH ? WIDTH : orientedWidth;
    int relativeY;
    int outputX;

    memset(_rgbLine, 0, WIDTH * sizeof(UWORD));

    if(outputY < offsetY ||
       outputY >= offsetY + orientedHeight)
        return true;

    relativeY = outputY - offsetY;

    for(outputX = 0; outputX < targetWidth; outputX++)
    {
        int orientedX =
            targetWidth == orientedWidth
                ? outputX
                : (int)(((ULONG)outputX * (ULONG)orientedWidth) /
                        (ULONG)targetWidth);
        int orientedY = relativeY;
        int sourceX;
        int sourceY;
        UWORD rgb444;

        if(orientation & ORIENTATION_FLIP_X)
            orientedX = orientedWidth - 1 - orientedX;
        if(orientation & ORIENTATION_FLIP_Y)
            orientedY = orientedHeight - 1 - orientedY;

        if(orientation & ORIENTATION_SWAP_XY)
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

        if(bitmap->depth == 8)
        {
            const UINT8 *line =
                (const UINT8 *)bitmap->line[sourceY];
            rgb444 = _palette15[line[sourceX]];
        }
        else if(bitmap->depth == 15)
        {
            const UINT16 *line =
                (const UINT16 *)bitmap->line[sourceY];
            UWORD pixel = line[sourceX];
            rgb444 = ((_videoAttributes & VIDEO_RGB_DIRECT) != 0)
                ? quantiseRgb15(pixel)
                : _palette15[pixel];
        }
        else if(bitmap->depth == 16)
        {
            const UINT16 *line =
                (const UINT16 *)bitmap->line[sourceY];
            UWORD pixel = line[sourceX];
            rgb444 = ((_videoAttributes & VIDEO_RGB_DIRECT) != 0)
                ? quantiseRgb16(pixel)
                : _palette15[pixel];
        }
        else if(bitmap->depth == 32)
        {
            const UINT32 *line =
                (const UINT32 *)bitmap->line[sourceY];
            rgb444 = quantiseArgb32(line[sourceX]);
        }
        else
        {
            return false;
        }

        _rgbLine[offsetX + outputX] = rgb444;
    }

    return true;
}

bool Ham6EuaeOutput::renderIndexed16Frame(
    _mame_display *display,
    BitMap *bitmap,
    int bufferIndex,
    int sourceHeight,
    int offsetY)
{
    mame_bitmap *gameBitmap = display->game_bitmap;
    UWORD *rawShadow = _rawIndexShadow[bufferIndex];
    UWORD *planarShadow = _planarShadow[bufferIndex];
    UWORD *rowShadow = _interleavedRowShadow[bufferIndex];
    ULONG *pending = _palettePending[bufferIndex];
    ULONG frameBytes = 0;
    ULONG frameRuns = 0;
    ULONG changedLines = 0;
    ULONG reusedLines = 0;
    UWORD packed[PLANES][WORDS_PER_ROW] __attribute__((aligned(4)));
    int y;
    int plane;

    if(!updatePalette(display))
        return false;

    if((display->changed_flags & GAME_VISIBLE_AREA_CHANGED) != 0)
    {
        _forceFull[0] = 1;
        _forceFull[1] = 1;
    }

    if(_interleaved[bufferIndex] && !_interleavedBurstReported)
    {
        printf(
            "FRF FIX72 HAM6 BURST: layout interleaved, "
            "240-byte changed-line CopyMemQuick path active\n");
        _interleavedBurstReported = 1;
    }

    for(y = 0; y < HEIGHT; y++)
    {
        const UWORD *source = NULL;
        UWORD *oldSource = rawShadow + y * WIDTH;
        int visible = y >= offsetY && y < offsetY + sourceHeight;
        int needsEncode = _forceFull[bufferIndex] != 0;

        if(visible)
        {
            int sourceY = display->game_visible_area.min_y + y - offsetY;
            source = (const UWORD *)gameBitmap->line[sourceY] +
                     display->game_visible_area.min_x;

            if(!needsEncode &&
               memcmp(source, oldSource, WIDTH * sizeof(UWORD)) != 0)
                needsEncode = 1;

            if(!needsEncode &&
               _palettePendingAny[bufferIndex] &&
               lineUsesPendingPalette(source, pending))
                needsEncode = 1;
        }
        else if(!needsEncode)
        {
            reusedLines++;
            continue;
        }

        if(!needsEncode)
        {
            reusedLines++;
            continue;
        }

        if(visible)
        {
            encodeAndPackIndexedLine(source, packed);
            CopyMem((APTR)source, (APTR)oldSource,
                    WIDTH * sizeof(UWORD));
        }
        else
        {
            memset(packed, 0, sizeof(packed));
            memset(oldSource, 0, WIDTH * sizeof(UWORD));
        }

        changedLines++;

        if(_interleaved[bufferIndex])
        {
            UWORD *lineShadow = rowShadow +
                y * PLANES * WORDS_PER_ROW;
            UBYTE *destination = (UBYTE *)bitmap->Planes[0] +
                y * _rowStride[bufferIndex];
            ULONG rowBytes = PLANES * WORDS_PER_ROW * sizeof(UWORD);

            if(_forceFull[bufferIndex] ||
               memcmp(packed, lineShadow, rowBytes) != 0)
            {
                copyAlignedBurst(destination, packed, rowBytes);
                copyAlignedBurst(lineShadow, packed, rowBytes);
                frameBytes += rowBytes;
                frameRuns++;
                _interleavedBurstLines++;
                _interleavedBurstBytes += rowBytes;
            }
        }
        else
        {
            for(plane = 0; plane < PLANES; plane++)
            {
                UWORD *shadow = planarShadow +
                    plane * PLANE_WORDS + y * WORDS_PER_ROW;
                UWORD *destination = (UWORD *)(
                    (UBYTE *)bitmap->Planes[plane] +
                    y * _rowStride[bufferIndex]);

                frameBytes += copyChangedPlaneRow(
                    destination, shadow, packed[plane], &frameRuns);
            }
        }
    }

    _forceFull[bufferIndex] = 0;
    memset(pending, 0, PALETTE_DIRTY_WORDS * sizeof(ULONG));
    _palettePendingAny[bufferIndex] = 0;

    _lastFrameBytes = frameBytes;
    _chipBytes += frameBytes;
    _dirtyRuns += frameRuns;
    _convertedLines += changedLines;
    _reusedLines += reusedLines;
    _indexedFastFrames++;
    _indexedChangedLines += changedLines;
    _indexedReusedLines += reusedLines;

    if(_interleaved[bufferIndex])
        _interleavedBurstFrames++;

    if(frameBytes == 0)
        _zeroWriteFrames++;
    if(frameBytes >= 60000UL)
        _fullishFrames++;

    return true;
}

/*
 * FRF_FIX83_WIDE_INDEXED_FAST_PATH
 *
 * Wide unrotated palette-indexed games previously fell through FIX77's
 * generic RGB path. Keep data indexed until a line is known to need HAM.
 */
bool Ham6EuaeOutput::renderWideIndexedFrame(
    _mame_display *display,
    BitMap *bitmap,
    int bufferIndex,
    int sourceWidth,
    int sourceHeight,
    int offsetY)
{
    mame_bitmap *gameBitmap = display->game_bitmap;
    const rectangle &visible = display->game_visible_area;
    UWORD *rawShadow = _rawIndexShadow[bufferIndex];
    UWORD *planarShadow = _planarShadow[bufferIndex];
    UWORD *rowShadow = _interleavedRowShadow[bufferIndex];
    ULONG *pending = _palettePending[bufferIndex];
    UWORD *rawLine = _rgbLine;
    ULONG frameBytes = 0;
    ULONG frameRuns = 0;
    ULONG changedLines = 0;
    ULONG reusedLines = 0;
    UWORD packed[PLANES][WORDS_PER_ROW] __attribute__((aligned(4)));
    UWORD sourceXMap[WIDTH];
    int depth = gameBitmap->depth;
    int x;
    int y;
    int plane;
    int frf87MkDense = 0;

    if(depth != 8 && depth != 16)
        return false;
    if(sourceWidth <= WIDTH)
        return false;

    if(!updatePalette(display))
        return false;

    if(frf88_opt_enabled && frf86b_game_mode() == 1 &&
       _interleaved[bufferIndex] && frf87_mk_dense_budget > 0)
    {
        frf87MkDense = 1;
        frf87_mk_dense_budget--;
    }
    else if(!frf88_opt_enabled || frf86b_game_mode() != 1)
    {
        frf87_mk_dense_budget = 0;
    }

    /* 320 divides per frame, instead of 320*sourceHeight divides. */
    for(x = 0; x < WIDTH; x++)
    {
        sourceXMap[x] = (UWORD)(
            visible.min_x +
            (int)(((ULONG)x * (ULONG)sourceWidth) / (ULONG)WIDTH));
    }

    if((display->changed_flags & GAME_VISIBLE_AREA_CHANGED) != 0)
    {
        _forceFull[0] = 1;
        _forceFull[1] = 1;
    }

    if(!_wideIndexedReported)
    {
        printf(
            "FRF FIX83 HAM6 WIDE INDEXED: raw=%dx%d -> %dx%d "
            "depth=%d xmap=320/frame raw-pen-compare=1 dense-row=adaptive\n",
            sourceWidth, sourceHeight, WIDTH, sourceHeight, depth);
        _wideIndexedReported = 1;
    }

    if(_interleaved[bufferIndex] && !_interleavedBurstReported)
    {
        printf(
            "FRF FIX72 HAM6 BURST: layout interleaved, "
            "240-byte changed-line CopyMemQuick path active\n");
        _interleavedBurstReported = 1;
    }

    for(y = 0; y < HEIGHT; y++)
    {
        UWORD *oldSource = rawShadow + y * WIDTH;
        int visibleLine = y >= offsetY && y < offsetY + sourceHeight;
        int needsEncode = _forceFull[bufferIndex] != 0 ||
            (frf87MkDense && visibleLine);

        if(visibleLine)
        {
            int sourceY = visible.min_y + y - offsetY;

            if(depth == 16)
            {
                const UINT16 *source =
                    (const UINT16 *)gameBitmap->line[sourceY];
                for(x = 0; x < WIDTH; x++)
                    rawLine[x] = source[sourceXMap[x]];
            }
            else
            {
                const UINT8 *source =
                    (const UINT8 *)gameBitmap->line[sourceY];
                for(x = 0; x < WIDTH; x++)
                    rawLine[x] = (UWORD)source[sourceXMap[x]];
            }

            if(!needsEncode &&
               memcmp(rawLine, oldSource, WIDTH * sizeof(UWORD)) != 0)
                needsEncode = 1;

            if(!needsEncode &&
               _palettePendingAny[bufferIndex] &&
               lineUsesPendingPalette(rawLine, pending))
                needsEncode = 1;
        }
        else if(!needsEncode)
        {
            reusedLines++;
            continue;
        }

        if(!needsEncode)
        {
            reusedLines++;
            continue;
        }

        if(visibleLine)
        {
            encodeAndPackIndexedLine(rawLine, packed);
            CopyMem((APTR)rawLine, (APTR)oldSource,
                    WIDTH * sizeof(UWORD));
        }
        else
        {
            memset(packed, 0, sizeof(packed));
            memset(oldSource, 0, WIDTH * sizeof(UWORD));
        }

        changedLines++;

        if(_interleaved[bufferIndex])
        {
            UWORD *lineShadow = rowShadow + y * PLANES * WORDS_PER_ROW;
            UBYTE *destination = (UBYTE *)bitmap->Planes[0] +
                y * _rowStride[bufferIndex];
            ULONG rowBytes = PLANES * WORDS_PER_ROW * sizeof(UWORD);

            if(_forceFull[bufferIndex] || frf87MkDense ||
               memcmp(packed, lineShadow, rowBytes) != 0)
            {
                copyAlignedBurst(destination, packed, rowBytes);
                copyAlignedBurst(lineShadow, packed, rowBytes);
                frameBytes += rowBytes;
                frameRuns++;
                _interleavedBurstLines++;
                _interleavedBurstBytes += rowBytes;
            }
        }
        else
        {
            for(plane = 0; plane < PLANES; plane++)
            {
                UWORD *shadow = planarShadow + plane * PLANE_WORDS +
                    y * WORDS_PER_ROW;
                UWORD *destination = (UWORD *)(
                    (UBYTE *)bitmap->Planes[plane] +
                    y * _rowStride[bufferIndex]);

                frameBytes += copyChangedPlaneRow(
                    destination, shadow, packed[plane], &frameRuns);
            }
        }
    }

    if(frf88_opt_enabled && frf86b_game_mode() == 1 &&
       _interleaved[bufferIndex] && !frf87MkDense &&
       changedLines >= 220UL)
    {
        /* Two cheap dense followers, then one normal probe frame. */
        frf87_mk_dense_budget = 2;
    }

    _forceFull[bufferIndex] = 0;
    memset(pending, 0, PALETTE_DIRTY_WORDS * sizeof(ULONG));
    _palettePendingAny[bufferIndex] = 0;

    _lastFrameBytes = frameBytes;
    _chipBytes += frameBytes;
    _dirtyRuns += frameRuns;
    _convertedLines += changedLines;
    _reusedLines += reusedLines;
    _indexedFastFrames++;
    _indexedChangedLines += changedLines;
    _indexedReusedLines += reusedLines;

    if(_interleaved[bufferIndex])
        _interleavedBurstFrames++;
    if(frameBytes == 0)
        _zeroWriteFrames++;
    if(frameBytes >= 60000UL)
        _fullishFrames++;

    return true;
}



/* FRF_FIX80_ROTATED_INDEXED_FAST_PATH
 * Compare oriented raw palette indices before RGB conversion or HAM encoding.
 */

/*
 * FIX81: narrow rotated indexed games (Terra Cresta is 224 pixels wide)
 * encode and copy only their word-aligned visible span. Rotated source reads
 * use bitmap base + rowbytes stride arithmetic instead of line[] per pixel.
 */
bool Ham6EuaeOutput::renderRotatedIndexedSpanFrame(
    _mame_display *display, BitMap *bitmap, int bufferIndex,
    int sourceWidth, int sourceHeight, int offsetX, int offsetY,
    int orientation)
{
    mame_bitmap *gameBitmap = display->game_bitmap;
    const rectangle &visibleArea = display->game_visible_area;
    UWORD *rawShadow = _rawIndexShadow[bufferIndex];
    UWORD *planarShadow = _planarShadow[bufferIndex];
    ULONG *pending = _palettePending[bufferIndex];
    UWORD *rawLine = _rgbLine;
    ULONG frameBytes=0, frameRuns=0, changedLines=0, reusedLines=0;
    UWORD packed[PLANES][WORDS_PER_ROW] __attribute__((aligned(4)));
    int orientedWidth=(orientation & ORIENTATION_SWAP_XY)?sourceHeight:sourceWidth;
    int orientedHeight=(orientation & ORIENTATION_SWAP_XY)?sourceWidth:sourceHeight;
    int firstWord=offsetX>>4;
    int spanWords=orientedWidth>>4;
    int depth=gameBitmap->depth;
    int bytesPerPixel=(depth==16)?2:1;
    int y,plane;

    if((offsetX & 15) || (orientedWidth & 15)) return false;
    if(depth!=8 && depth!=16) return false;
    if(_paletteEntries >= PALETTE_TABLE_SIZE) return false;

    if((display->changed_flags & GAME_VISIBLE_AREA_CHANGED) != 0)
    {
        _forceFull[0]=1; _forceFull[1]=1;
    }

    if(!_rotatedIndexedReported)
    {
        printf("FRF FIX81 HAM6 SPAN: rotated indexed active-span raw=%dx%d "
               "output=%dx%d spanWords=%d/20 savedPixels=%d strideGather=1 "
               "flags=%d offset=%d,%d\n",
               sourceWidth,sourceHeight,orientedWidth,orientedHeight,
               spanWords,WIDTH-orientedWidth,orientation,offsetX,offsetY);
        _rotatedIndexedReported=1;
    }

    for(y=0; y<HEIGHT; y++)
    {
        UWORD *oldSource=rawShadow+y*WIDTH;
        UWORD *oldSpan=oldSource+offsetX;
        int lineVisible=(y>=offsetY && y<offsetY+orientedHeight);
        int needsEncode=_forceFull[bufferIndex]!=0;

        if(!lineVisible)
        {
            if(_forceFull[bufferIndex])
            {
                memset(packed,0,sizeof(packed));
                for(plane=0; plane<PLANES; plane++)
                {
                    UWORD *shadow=planarShadow+plane*PLANE_WORDS+y*WORDS_PER_ROW;
                    UWORD *destination=(UWORD *)((UBYTE *)bitmap->Planes[plane]+y*_rowStride[bufferIndex]);
                    frameBytes += copyChangedPlaneRow(destination,shadow,packed[plane],&frameRuns);
                }
                memset(oldSource,0,WIDTH*sizeof(UWORD));
                changedLines++;
            }
            else reusedLines++;
            continue;
        }

        {
            int relativeY=y-offsetY;
            int outputX;
            const UBYTE *base=(const UBYTE *)gameBitmap->base;
            LONG step;
            const UBYTE *sourcePtr;

            if(orientation & ORIENTATION_SWAP_XY)
            {
                int sourceX=(orientation & ORIENTATION_FLIP_Y)?orientedHeight-1-relativeY:relativeY;
                int sourceY=(orientation & ORIENTATION_FLIP_X)?orientedWidth-1:0;
                sourceX += visibleArea.min_x;
                sourceY += visibleArea.min_y;
                sourcePtr=base+sourceY*gameBitmap->rowbytes+sourceX*bytesPerPixel;
                step=(orientation & ORIENTATION_FLIP_X)?-gameBitmap->rowbytes:gameBitmap->rowbytes;
            }
            else
            {
                int sourceY=(orientation & ORIENTATION_FLIP_Y)?orientedHeight-1-relativeY:relativeY;
                int sourceX=(orientation & ORIENTATION_FLIP_X)?orientedWidth-1:0;
                sourceY += visibleArea.min_y;
                sourceX += visibleArea.min_x;
                sourcePtr=base+sourceY*gameBitmap->rowbytes+sourceX*bytesPerPixel;
                step=(orientation & ORIENTATION_FLIP_X)?-bytesPerPixel:bytesPerPixel;
            }

            if(depth==16)
            {
                for(outputX=0; outputX<orientedWidth; outputX++)
                {
                    rawLine[outputX]=*(const UINT16 *)sourcePtr;
                    sourcePtr += step;
                }
            }
            else
            {
                for(outputX=0; outputX<orientedWidth; outputX++)
                {
                    rawLine[outputX]=(UWORD)*sourcePtr;
                    sourcePtr += step;
                }
            }
        }

        if(!needsEncode && memcmp(rawLine,oldSpan,orientedWidth*sizeof(UWORD))!=0) needsEncode=1;
        if(!needsEncode && _palettePendingAny[bufferIndex] &&
           spanUsesPendingPalette(rawLine,orientedWidth,pending)) needsEncode=1;
        if(!needsEncode) { reusedLines++; continue; }

        if(_forceFull[bufferIndex]) memset(packed,0,sizeof(packed));
        encodeAndPackIndexedSpan(rawLine,orientedWidth,firstWord,packed);
        CopyMem((APTR)rawLine,(APTR)oldSpan,orientedWidth*sizeof(UWORD));
        changedLines++;

        for(plane=0; plane<PLANES; plane++)
        {
            UWORD *shadow=planarShadow+plane*PLANE_WORDS+y*WORDS_PER_ROW;
            UWORD *destination=(UWORD *)((UBYTE *)bitmap->Planes[plane]+y*_rowStride[bufferIndex]);
            if(_forceFull[bufferIndex])
                frameBytes += copyChangedPlaneRow(destination,shadow,packed[plane],&frameRuns);
            else
                frameBytes += copyChangedPlaneSpan(destination,shadow,packed[plane],firstWord,spanWords,&frameRuns);
        }
    }

    _forceFull[bufferIndex]=0;
    memset(pending,0,PALETTE_DIRTY_WORDS*sizeof(ULONG));
    _palettePendingAny[bufferIndex]=0;
    _lastFrameBytes=frameBytes;
    _chipBytes += frameBytes;
    _dirtyRuns += frameRuns;
    _convertedLines += changedLines;
    _reusedLines += reusedLines;
    _indexedFastFrames++;
    _indexedChangedLines += changedLines;
    _indexedReusedLines += reusedLines;
    if(frameBytes==0) _zeroWriteFrames++;
    if(frameBytes>=60000UL) _fullishFrames++;
    return true;
}

bool Ham6EuaeOutput::renderRotatedIndexedFrame(
    _mame_display *display,
    BitMap *bitmap,
    int bufferIndex,
    int sourceWidth,
    int sourceHeight,
    int offsetX,
    int offsetY,
    int orientation)
{
    mame_bitmap *gameBitmap = display->game_bitmap;
    const rectangle &visibleArea = display->game_visible_area;
    UWORD *rawShadow = _rawIndexShadow[bufferIndex];
    UWORD *planarShadow = _planarShadow[bufferIndex];
    UWORD *rowShadow = _interleavedRowShadow[bufferIndex];
    ULONG *pending = _palettePending[bufferIndex];
    UWORD *rawLine = _rgbLine;
    ULONG frameBytes = 0;
    ULONG frameRuns = 0;
    ULONG changedLines = 0;
    ULONG reusedLines = 0;
    UWORD packed[PLANES][WORDS_PER_ROW] __attribute__((aligned(4)));
    int orientedWidth = (orientation & ORIENTATION_SWAP_XY)
        ? sourceHeight : sourceWidth;
    int orientedHeight = (orientation & ORIENTATION_SWAP_XY)
        ? sourceWidth : sourceHeight;
    int depth = gameBitmap->depth;
    int y;
    int plane;

    if(_paletteEntries >= PALETTE_TABLE_SIZE)
        return false;

    if((display->changed_flags & GAME_VISIBLE_AREA_CHANGED) != 0)
    {
        _forceFull[0] = 1;
        _forceFull[1] = 1;
    }

    if(!_rotatedIndexedReported)
    {
        printf(
            "FRF FIX80 HAM6 FAST: rotated indexed direct-pen "
            "raw=%dx%d output=%dx%d depth=%d flags=%d offset=%d,%d\n",
            sourceWidth, sourceHeight, orientedWidth, orientedHeight,
            depth, orientation, offsetX, offsetY);
        _rotatedIndexedReported = 1;
    }

    if(_interleaved[bufferIndex] && !_interleavedBurstReported)
    {
        printf(
            "FRF FIX72 HAM6 BURST: layout interleaved, "
            "240-byte changed-line CopyMemQuick path active\n");
        _interleavedBurstReported = 1;
    }

    for(y = 0; y < HEIGHT; y++)
    {
        UWORD *oldSource = rawShadow + y * WIDTH;
        int lineVisible = y >= offsetY && y < offsetY + orientedHeight;
        int needsEncode = _forceFull[bufferIndex] != 0;

        /* Unused palette entry 0xffff remains direct black. */
        memset(rawLine, 0xff, WIDTH * sizeof(UWORD));

        if(lineVisible)
        {
            int relativeY = y - offsetY;
            int outputX;

            if(orientation & ORIENTATION_SWAP_XY)
            {
                int sourceX = (orientation & ORIENTATION_FLIP_Y)
                    ? orientedHeight - 1 - relativeY : relativeY;
                int sourceY = (orientation & ORIENTATION_FLIP_X)
                    ? orientedWidth - 1 : 0;
                int sourceStep = (orientation & ORIENTATION_FLIP_X) ? -1 : 1;
                sourceX += visibleArea.min_x;
                sourceY += visibleArea.min_y;

                if(depth == 16)
                {
                    for(outputX = 0; outputX < orientedWidth;
                        outputX++, sourceY += sourceStep)
                    {
                        const UINT16 *source =
                            (const UINT16 *)gameBitmap->line[sourceY];
                        rawLine[offsetX + outputX] = source[sourceX];
                    }
                }
                else
                {
                    for(outputX = 0; outputX < orientedWidth;
                        outputX++, sourceY += sourceStep)
                    {
                        const UINT8 *source =
                            (const UINT8 *)gameBitmap->line[sourceY];
                        rawLine[offsetX + outputX] = (UWORD)source[sourceX];
                    }
                }
            }
            else
            {
                int sourceY = (orientation & ORIENTATION_FLIP_Y)
                    ? orientedHeight - 1 - relativeY : relativeY;
                int sourceX = (orientation & ORIENTATION_FLIP_X)
                    ? orientedWidth - 1 : 0;
                int sourceStep = (orientation & ORIENTATION_FLIP_X) ? -1 : 1;
                sourceY += visibleArea.min_y;
                sourceX += visibleArea.min_x;

                if(depth == 16 && sourceStep == 1)
                {
                    const UINT16 *source =
                        (const UINT16 *)gameBitmap->line[sourceY] + sourceX;
                    CopyMem((APTR)source, (APTR)(rawLine + offsetX),
                            orientedWidth * sizeof(UWORD));
                }
                else if(depth == 16)
                {
                    const UINT16 *source =
                        (const UINT16 *)gameBitmap->line[sourceY];
                    for(outputX = 0; outputX < orientedWidth;
                        outputX++, sourceX += sourceStep)
                        rawLine[offsetX + outputX] = source[sourceX];
                }
                else
                {
                    const UINT8 *source =
                        (const UINT8 *)gameBitmap->line[sourceY];
                    for(outputX = 0; outputX < orientedWidth;
                        outputX++, sourceX += sourceStep)
                        rawLine[offsetX + outputX] = (UWORD)source[sourceX];
                }
            }

            if(!needsEncode &&
               memcmp(rawLine, oldSource, WIDTH * sizeof(UWORD)) != 0)
                needsEncode = 1;
            if(!needsEncode && _palettePendingAny[bufferIndex] &&
               lineUsesPendingPalette(rawLine, pending))
                needsEncode = 1;
        }
        else if(!needsEncode &&
                memcmp(rawLine, oldSource, WIDTH * sizeof(UWORD)) != 0)
        {
            needsEncode = 1;
        }

        if(!needsEncode)
        {
            reusedLines++;
            continue;
        }

        encodeAndPackIndexedLine(rawLine, packed);
        CopyMem((APTR)rawLine, (APTR)oldSource, WIDTH * sizeof(UWORD));
        changedLines++;

        if(_interleaved[bufferIndex])
        {
            UWORD *lineShadow = rowShadow + y * PLANES * WORDS_PER_ROW;
            UBYTE *destination = (UBYTE *)bitmap->Planes[0] +
                y * _rowStride[bufferIndex];
            ULONG rowBytes = PLANES * WORDS_PER_ROW * sizeof(UWORD);
            if(_forceFull[bufferIndex] ||
               memcmp(packed, lineShadow, rowBytes) != 0)
            {
                copyAlignedBurst(destination, packed, rowBytes);
                copyAlignedBurst(lineShadow, packed, rowBytes);
                frameBytes += rowBytes;
                frameRuns++;
                _interleavedBurstLines++;
                _interleavedBurstBytes += rowBytes;
            }
        }
        else
        {
            for(plane = 0; plane < PLANES; plane++)
            {
                UWORD *shadow = planarShadow + plane * PLANE_WORDS +
                    y * WORDS_PER_ROW;
                UWORD *destination = (UWORD *)(
                    (UBYTE *)bitmap->Planes[plane] +
                    y * _rowStride[bufferIndex]);
                frameBytes += copyChangedPlaneRow(
                    destination, shadow, packed[plane], &frameRuns);
            }
        }
    }

    _forceFull[bufferIndex] = 0;
    memset(pending, 0, PALETTE_DIRTY_WORDS * sizeof(ULONG));
    _palettePendingAny[bufferIndex] = 0;
    _lastFrameBytes = frameBytes;
    _chipBytes += frameBytes;
    _dirtyRuns += frameRuns;
    _convertedLines += changedLines;
    _reusedLines += reusedLines;
    _indexedFastFrames++;
    _indexedChangedLines += changedLines;
    _indexedReusedLines += reusedLines;
    if(_interleaved[bufferIndex]) _interleavedBurstFrames++;
    if(frameBytes == 0) _zeroWriteFrames++;
    if(frameBytes >= 60000UL) _fullishFrames++;
    return true;
}

bool Ham6EuaeOutput::renderFrame(
    _mame_display *display,
    BitMap *bitmap,
    int bufferIndex)
{
    FrfFix74HamRenderTimer frfFix74HamRenderTimer;
    ULONG frameBytes = 0;
    ULONG frameRuns = 0;
    ULONG convertedLines = 0;
    ULONG reusedLines = 0;
    int sourceWidth;
    int sourceHeight;
    int orientation;
    int orientedWidth;
    int orientedHeight;
    int targetWidth;
    int offsetX;
    int offsetY;
    int y;
    int plane;
    int wordIndex;
    UWORD packed[PLANES][WORDS_PER_ROW];
    UWORD *sourceShadow;
    UWORD *planarShadow;

    if(!display || !display->game_bitmap || !bitmap ||
       bufferIndex < 0 || bufferIndex > 1)
        return false;

    sourceWidth = display->game_visible_area.max_x -
        display->game_visible_area.min_x + 1;
    sourceHeight = display->game_visible_area.max_y -
        display->game_visible_area.min_y + 1;
    orientation = _drawable.flags() & ORIENTATION_MASK;
    orientedWidth = (orientation & ORIENTATION_SWAP_XY)
        ? sourceHeight : sourceWidth;
    orientedHeight = (orientation & ORIENTATION_SWAP_XY)
        ? sourceWidth : sourceHeight;
    targetWidth = orientedWidth > WIDTH ? WIDTH : orientedWidth;

    if(sourceWidth <= 0 || sourceHeight <= 0 || orientedWidth <= 0 ||
       orientedHeight <= 0 || targetWidth <= 0 || orientedHeight > HEIGHT)
    {
        if(!_reportedIncompatibleFrame)
        {
            printf(
                "FRF FIX77 HAM6: unsupported geometry "
                "raw=%dx%d oriented=%dx%d flags=%d; height must be <=256\n",
                sourceWidth, sourceHeight, orientedWidth, orientedHeight,
                orientation);
            _reportedIncompatibleFrame = 1;
        }
        return false;
    }

    offsetX = (WIDTH - targetWidth) >> 1;
    offsetY = (HEIGHT - orientedHeight) >> 1;

    if(orientedWidth > WIDTH && !_reportedIncompatibleFrame)
    {
        printf(
            "FRF FIX77 HAM6 WIDE: horizontal handoff active "
            "raw=%dx%d oriented=%dx%d target=%dx%d flags=%d offset=%d,%d\n",
            sourceWidth, sourceHeight, orientedWidth, orientedHeight,
            targetWidth, orientedHeight, orientation, offsetX, offsetY);
        _reportedIncompatibleFrame = 1;
    }

    if(display->game_bitmap->depth == 16 &&
       (_videoAttributes & VIDEO_RGB_DIRECT) == 0 &&
       orientation == 0 && sourceWidth == WIDTH && offsetX == 0)
    {
        if(!_indexedFastReported)
        {
            printf(
                "FRF FIX71 HAM6 FAST: indexed16 direct-pen encoder "
                "%dx%d offsetY=%d active\n",
                sourceWidth, sourceHeight, offsetY);
            _indexedFastReported = 1;
        }
        return renderIndexed16Frame(
            display, bitmap, bufferIndex, sourceHeight, offsetY);
    }

    /* FIX83: wide unrotated indexed games use raw-pen fast path. */
    if((display->game_bitmap->depth == 8 ||
        display->game_bitmap->depth == 16) &&
       (_videoAttributes & VIDEO_RGB_DIRECT) == 0 &&
       orientation == 0 &&
       sourceWidth > WIDTH &&
       sourceHeight <= HEIGHT)
    {
        return renderWideIndexedFrame(
            display, bitmap, bufferIndex,
            sourceWidth, sourceHeight, offsetY);
    }

    if((display->game_bitmap->depth == 8 ||
        display->game_bitmap->depth == 16) &&
       (_videoAttributes & VIDEO_RGB_DIRECT) == 0 &&
       orientation != 0 && orientedWidth <= WIDTH)
    {
        if(!updatePalette(display)) return false;
        if(_paletteEntries < PALETTE_TABLE_SIZE)
        {
            if((offsetX & 15) == 0 &&
               (orientedWidth & 15) == 0 &&
               orientedWidth < WIDTH)
            {
                return renderRotatedIndexedSpanFrame(
                    display, bitmap, bufferIndex,
                    sourceWidth, sourceHeight,
                    offsetX, offsetY, orientation);
            }
            return renderRotatedIndexedFrame(
                display, bitmap, bufferIndex, sourceWidth, sourceHeight,
                offsetX, offsetY, orientation);
        }
    }

    if(!updatePalette(display)) return false;
    sourceShadow = _sourceShadow[bufferIndex];
    planarShadow = _planarShadow[bufferIndex];

    for(y = 0; y < HEIGHT; y++)
    {
        UWORD *oldSource = sourceShadow + y * WIDTH;
        if(!prepareRgbLine(display, y, sourceWidth, sourceHeight,
                           offsetX, offsetY))
            return false;
        if(!_forceFull[bufferIndex] &&
           memcmp(_rgbLine, oldSource, WIDTH * sizeof(UWORD)) == 0)
        {
            reusedLines++;
            continue;
        }

        encodeAndPackLine(_rgbLine, packed);
        convertedLines++;
        for(plane = 0; plane < PLANES; plane++)
        {
            UWORD *shadow = planarShadow + plane * PLANE_WORDS +
                y * WORDS_PER_ROW;
            UWORD *destination = (UWORD *)(
                (UBYTE *)bitmap->Planes[plane] +
                y * _rowStride[bufferIndex]);
            int runStart = -1;
            for(wordIndex = 0; wordIndex < WORDS_PER_ROW; wordIndex++)
            {
                if(packed[plane][wordIndex] != shadow[wordIndex])
                {
                    if(runStart < 0) runStart = wordIndex;
                    shadow[wordIndex] = packed[plane][wordIndex];
                }
                else if(runStart >= 0)
                {
                    int words = wordIndex - runStart;
                    copyWordRun(destination + runStart,
                                shadow + runStart, words);
                    frameBytes += words * sizeof(UWORD);
                    frameRuns++;
                    runStart = -1;
                }
            }
            if(runStart >= 0)
            {
                int words = WORDS_PER_ROW - runStart;
                copyWordRun(destination + runStart, shadow + runStart, words);
                frameBytes += words * sizeof(UWORD);
                frameRuns++;
            }
        }
        CopyMem((APTR)_rgbLine, (APTR)oldSource, WIDTH * sizeof(UWORD));
    }

    _forceFull[bufferIndex] = 0;
    _lastFrameBytes = frameBytes;
    _chipBytes += frameBytes;
    _dirtyRuns += frameRuns;
    _convertedLines += convertedLines;
    _reusedLines += reusedLines;
    if(frameBytes == 0) _zeroWriteFrames++;
    if(frameBytes >= 60000UL) _fullishFrames++;
    return true;
}

bool Ham6EuaeOutput::draw(_mame_display *display)
{
    FrfFix74HamDrawTimer frfFix74HamDrawTimer(
        _renderedFrames,
        _chipBytes,
        _convertedLines,
        _reusedLines);

    BitMap *displayBitmap;
    cycles_t renderStart;
    cycles_t renderTicks;
    cycles_t cyclesPerSecond;
    ULONG runsBefore;
    ULONG convertedBefore;
    ULONG reusedBefore;
    ULONG paletteBefore;
    ULONG indexedBefore;
    ULONG changedThisFrame;
    int frf86bMode;
    int frf86bLog;
    bool heavyFrame;

    if(!_active || !_directFront || !_screen)
        return false;

    _drawCalls++;

    frf86bMode = frf86b_game_mode();
    frf86bLog = frf86b_log_enabled();

    if(_smoothSkipPending) /* FRF88B_OUTRUN_BOUNDED_RECOVERY: OutRun must honor recovery */
    {
        if(frf86bLog && _probeSamples)
        {
            FrfFix85ProbeSample *samples =
                (FrfFix85ProbeSample *)_probeSamples;
            FrfFix85ProbeSample *sample = &samples[_probeWrite];

            sample->tick = osd_cycles();
            sample->renderTicks = 0;
            sample->drawCall = _drawCalls;
            sample->chipBytes = 0;
            sample->dirtyRuns = 0;
            sample->changedLines = 0;
            sample->reusedLines = 0;
            sample->paletteChanges = 0;
            sample->flags = FRF85_FLAG_REPEAT;

            _probeWrite = (_probeWrite + 1) % FRF85_PROBE_CAPACITY;
            if(_probeCount < FRF85_PROBE_CAPACITY)
                _probeCount++;
            _probeTotal++;
        }

        _smoothSkipPending = 0;
        _smoothSkippedFrames++;
        _smoothCooldown = 3;
        return true;
    }
    /* FRF88B_OUTRUN_BOUNDED_RECOVERY: keep smoother pressure/cooldown state across OutRun frames. */

    displayBitmap = _screen->RastPort.BitMap;
    runsBefore = _dirtyRuns;
    convertedBefore = _convertedLines;
    reusedBefore = _reusedLines;
    paletteBefore = _paletteActualChanges;
    indexedBefore = _indexedFastFrames;
    if(frf86bMode == 0 || frf86bLog || !frf88_opt_enabled || frf86bMode == 2)
        renderStart = osd_cycles();
    else
        renderStart = 0;

    if(!displayBitmap || !renderFrame(display, displayBitmap, 0))
    {
        _renderFailures++;
        return false;
    }
    changedThisFrame = _convertedLines - convertedBefore;

    if(!frf88_opt_enabled)
    {
        /* Exact pre-per-game FIX82 heavy detection for A/B testing. */
        renderTicks = osd_cycles() - renderStart;
        cyclesPerSecond = osd_cycles_per_second();
        heavyFrame = cyclesPerSecond > 0 &&
            renderTicks > (cyclesPerSecond / 50); /* FIX82: 20 ms */
    }
    else if(frf86bMode == 1)
    {
        heavyFrame = changedThisFrame >= 220UL; /* Mortal Kombat */
    }
    else if(frf86bMode == 2)
    {
        renderTicks = osd_cycles() - renderStart;
        cyclesPerSecond = osd_cycles_per_second();
        heavyFrame = cyclesPerSecond > 0 &&
            renderTicks > (cyclesPerSecond / 55); /* V88B OutRun bounded recovery: ~18.2 ms */

        if(cyclesPerSecond > 0 &&
           renderTicks > (cyclesPerSecond / 36)) /* ~27.8 ms emergency */
        {
            _smoothSkipPending = 1;
            _smoothHeavyStreak = 0;
        }
    }
    else
    {
        renderTicks = osd_cycles() - renderStart;
        cyclesPerSecond = osd_cycles_per_second();
        heavyFrame = cyclesPerSecond > 0 &&
            renderTicks > (cyclesPerSecond / 50); /* FIX82: 20 ms */
    }

    if(frf86bMode != 0)
    {
        if(frf86bLog)
        {
            renderTicks = osd_cycles() - renderStart;
            cyclesPerSecond = osd_cycles_per_second();
        }
        else
        {
            renderTicks = 0;
            cyclesPerSecond = 0;
        }
    }

    _renderedFrames++;
    _directFrontFrames++;

    if(frf86bLog && _probeSamples)
    {
        FrfFix85ProbeSample *samples =
            (FrfFix85ProbeSample *)_probeSamples;
        FrfFix85ProbeSample *sample = &samples[_probeWrite];
        ULONG flags = heavyFrame ? FRF85_FLAG_HEAVY : 0;

        if(_indexedFastFrames != indexedBefore)
            flags |= FRF85_FLAG_INDEXED;

        sample->tick = renderStart;
        sample->renderTicks = renderTicks;
        sample->drawCall = _drawCalls;
        sample->chipBytes = _lastFrameBytes;
        sample->dirtyRuns = _dirtyRuns - runsBefore;
        sample->changedLines = _convertedLines - convertedBefore;
        sample->reusedLines = _reusedLines - reusedBefore;
        sample->paletteChanges = _paletteActualChanges - paletteBefore;
        sample->flags = flags;

        _probeWrite = (_probeWrite + 1) % FRF85_PROBE_CAPACITY;
        if(_probeCount < FRF85_PROBE_CAPACITY)
            _probeCount++;
        _probeTotal++;
    }

    if(_smoothCooldown > 0)
        _smoothCooldown--;

    if(heavyFrame)
    {
        _smoothHeavyFrames++;
        _smoothHeavyStreak++;
        if(_smoothHeavyStreak >= 2 && _smoothCooldown == 0)
        {
            _smoothSkipPending = 1;
            _smoothHeavyStreak = 0;
        }
    }
    else
    {
        _smoothHeavyStreak = 0;
    }

    return true;
}
