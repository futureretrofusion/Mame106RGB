/******************************************************************************
 * amiga_video_ham6.h
 *
 * FIX70: complete E-UAE-derived native HAM6 output engine for MamAmiga.
 *
 * Copyright 1996-1998 Samuel Devulder
 * Copyright 2003-2007 Richard Drummond
 * Adaptation Copyright 2026 Future Retro Fusion
 *
 * GPL v2 or later.
 *****************************************************************************/

#ifndef AMIGA_VIDEO_HAM6_H
#define AMIGA_VIDEO_HAM6_H

#include "amiga_video.h"

extern "C" {
    #include <exec/types.h>
}

class IntuitionDrawable;
struct Screen;
struct ScreenBuffer;
struct MsgPort;
struct BitMap;
struct _mame_display;

class Ham6EuaeOutput
{
public:
    Ham6EuaeOutput(IntuitionDrawable &drawable, int videoAttributes);
    ~Ham6EuaeOutput();

    static bool isHam6ModeId(ULONG modeId);

    bool open();
    void close();
    bool draw(_mame_display *display);
    bool active() const { return _active != 0; }

private:
    enum {
        WIDTH = 320,
        HEIGHT = 256,
        PLANES = 6,
        WORDS_PER_ROW = 20,
        PLANE_WORDS = WORDS_PER_ROW * HEIGHT,
        SOURCE_WORDS = WIDTH * HEIGHT,
        PLANAR_WORDS = PLANES * PLANE_WORDS,
        DIRECT_TABLE_SIZE = 16384,
        HOLD_TABLE_SIZE = 32768,
        PALETTE_TABLE_SIZE = 65536,
        PALETTE_DIRTY_WORDS = PALETTE_TABLE_SIZE / 32
    };

    IntuitionDrawable &_drawable;
    int _videoAttributes;
    Screen *_screen;
    ScreenBuffer *_buffers[2];
    MsgPort *_safePort;

    int _front;
    int _back;
    int _safe[2];
    int _forceFull[2];
    int _active;
    int _directFront;
    int _paletteValid;
    int _reportedIncompatibleFrame;

    ULONG _rowStride[2];
    int _interleaved[2];

    UWORD *_sourceShadow[2];
    UWORD *_rawIndexShadow[2];
    UWORD *_planarShadow[2];
    UWORD *_interleavedRowShadow[2];
    UWORD *_rgbLine;
    UWORD *_palette15;
    ULONG *_palettePending[2];
    int _palettePendingAny[2];
    int _paletteEntries;

    UBYTE *_directError;
    UBYTE *_directCommand;
    UBYTE *_holdChoice;
    UBYTE *_paletteError;
    UBYTE *_paletteCommand;

    ULONG _swaps;
    ULONG _swapMisses;
    ULONG _busyFrames;
    ULONG _renderedFrames;
    ULONG _renderFailures;
    ULONG _chipBytes;
    ULONG _dirtyRuns;
    ULONG _convertedLines;
    ULONG _reusedLines;
    ULONG _zeroWriteFrames;
    ULONG _fullishFrames;
    ULONG _lastFrameBytes;
    ULONG _drawCalls;
    ULONG _directFrontFrames;
    ULONG _indexedFastFrames;
    ULONG _indexedChangedLines;
    ULONG _indexedReusedLines;
    ULONG _paletteUpdateEvents;
    ULONG _paletteActualChanges;
    ULONG _interleavedBurstFrames;
    ULONG _interleavedBurstLines;
    ULONG _interleavedBurstBytes;
    int _indexedFastReported;
    int _interleavedBurstReported;
    int _rotatedIndexedReported;
    int _wideIndexedReported;

    /* FRF_FIX82_ADAPTIVE_OVERLOAD_SMOOTHER */
    int _smoothHeavyStreak;
    int _smoothSkipPending;
    int _smoothCooldown;
    ULONG _smoothHeavyFrames;
    ULONG _smoothSkippedFrames;

    /* FIX85 probe ring. Void pointer keeps the private sample type in .cpp. */
    void *_probeSamples;
    ULONG _probeWrite;
    ULONG _probeCount;
    ULONG _probeTotal;

    /* FIX85B: captured at open so close() never depends on Machine lifetime. */
    char _probeGameName[32];
    char _probeGameDescription[128];

    void *allocFast(ULONG bytes, const char *name);
    void freeBuffers();
    void resetState();

    static int distance4(LONG rgb1, LONG rgb2);
    bool initialiseEncoderTables();
    bool installBasePalette();

    void drainSafeMessages();
    bool bitmapLayout(BitMap *bitmap, ULONG *rowStride, int *interleaved);
    void clearScreenBuffers();

    bool updatePalette(_mame_display *display);
    UWORD quantiseArgb32(ULONG colour) const;
    UWORD quantiseRgb15(UWORD pixel) const;
    UWORD quantiseRgb16(UWORD pixel) const;

    UBYTE encodePixel(UWORD pixel, ULONG *rgbState) const;
    UBYTE encodeIndexedPixel(UWORD pen, ULONG *rgbState) const;
    void encodeAndPackLine(
        const UWORD *source,
        UWORD packed[PLANES][WORDS_PER_ROW]) const;
    void encodeAndPackIndexedLine(
        const UWORD *source,
        UWORD packed[PLANES][WORDS_PER_ROW]) const;
    void encodeAndPackIndexedSpan(
        const UWORD *source,
        int pixelCount,
        int firstWord,
        UWORD packed[PLANES][WORDS_PER_ROW]) const;

    static void copyWordRun(UWORD *destination, const UWORD *source, int words);
    static ULONG copyChangedPlaneRow(
        UWORD *destination,
        UWORD *shadow,
        const UWORD *packed,
        ULONG *runCounter);
    static ULONG copyChangedPlaneSpan(
        UWORD *destination,
        UWORD *shadow,
        const UWORD *packed,
        int firstWord,
        int wordCount,
        ULONG *runCounter);
    static void copyAlignedBurst(
        void *destination,
        const void *source,
        ULONG bytes);
    bool lineUsesPendingPalette(
        const UWORD *source,
        const ULONG *pending) const;
    bool spanUsesPendingPalette(
        const UWORD *source,
        int pixelCount,
        const ULONG *pending) const;
    bool prepareRgbLine(
        _mame_display *display,
        int outputY,
        int sourceWidth,
        int sourceHeight,
        int targetWidth,  /* FRF92_UNIVERSAL_RUNTIME_VIEWPORT_SCALER_HAM_DECL */
        int targetHeight,
        int offsetX,
        int offsetY);

    bool renderIndexed16Frame(
        _mame_display *display,
        BitMap *bitmap,
        int bufferIndex,
        int sourceHeight,
        int offsetY);
    bool renderWideIndexedFrame(
        _mame_display *display,
        BitMap *bitmap,
        int bufferIndex,
        int sourceWidth,
        int sourceHeight,
        int offsetY);
    bool renderRotatedIndexedFrame(
        _mame_display *display,
        BitMap *bitmap,
        int bufferIndex,
        int sourceWidth,
        int sourceHeight,
        int offsetX,
        int offsetY,
        int orientation);
    bool renderRotatedIndexedSpanFrame(
        _mame_display *display,
        BitMap *bitmap,
        int bufferIndex,
        int sourceWidth,
        int sourceHeight,
        int offsetX,
        int offsetY,
        int orientation);
    bool renderFrame(_mame_display *display, BitMap *bitmap, int bufferIndex);
};

#endif
