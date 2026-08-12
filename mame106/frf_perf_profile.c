#include "driver.h"
#include "osdepend.h"
#include "frf_perf_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct frf_perf_counters
{
    cycles_t core_us;
    cycles_t video_between_us;
    cycles_t update_us;
    cycles_t sound_us;
    cycles_t video_us;
    cycles_t osd_us;
    cycles_t palette_us;
    cycles_t remap_us;
    cycles_t c2p_us;
    cycles_t wait_us;
    unsigned long frames;
    unsigned long partial_updates;
} frf_perf_counters;

static const void *frf_cached_driver = 0;
static int frf_cached_is_ddragon = 0;
static int frf_cached_log_enabled = 0;
static int frf_announced = 0;

static frf_perf_counters frf_perf;
static cycles_t frf_frame_begin_tick = 0;
static cycles_t frf_last_frame_end_tick = 0;
static cycles_t frf_video_lifetime_us = 0;
static cycles_t frf_video_at_last_frame_end = 0;

static cycles_t frf_sound_tick = 0;
static cycles_t frf_video_tick = 0;
static cycles_t frf_osd_tick = 0;
static cycles_t frf_palette_tick = 0;
static cycles_t frf_remap_tick = 0;
static cycles_t frf_c2p_tick = 0;
static cycles_t frf_wait_tick = 0;

static int frf_equal_ci(const char *a, const char *b)
{
    unsigned char ca, cb;
    if(!a || !b) return 0;
    while(*a && *b)
    {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if(ca >= 'a' && ca <= 'z') ca = (unsigned char)(ca - ('a' - 'A'));
        if(cb >= 'a' && cb <= 'z') cb = (unsigned char)(cb - ('a' - 'A'));
        if(ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static int frf_env_false(const char *value)
{
    return value && (frf_equal_ci(value,"0") || frf_equal_ci(value,"OFF") ||
                     frf_equal_ci(value,"NO") || frf_equal_ci(value,"FALSE"));
}

static int frf_env_true(const char *value)
{
    return value && (frf_equal_ci(value,"1") || frf_equal_ci(value,"ON") ||
                     frf_equal_ci(value,"YES") || frf_equal_ci(value,"TRUE") ||
                     frf_equal_ci(value,"ALL"));
}

static const char *frf_tune_name(void)
{
#ifdef FRF_DDRAGON_HOTPATH_ACTIVE
    return "HOTPATH";
#else
    return "BASELINE";
#endif
}

static void frf_reset_measurements(void)
{
    memset(&frf_perf,0,sizeof(frf_perf));
    frf_frame_begin_tick = 0;
    frf_last_frame_end_tick = 0;
    frf_video_lifetime_us = 0;
    frf_video_at_last_frame_end = 0;
    frf_sound_tick = 0;
    frf_video_tick = 0;
    frf_osd_tick = 0;
    frf_palette_tick = 0;
    frf_remap_tick = 0;
    frf_c2p_tick = 0;
    frf_wait_tick = 0;
}

static void frf_refresh_settings(void)
{
    const void *driver_now = 0;
    const char *game = 0;
    const char *log_env;

    if(Machine && Machine->gamedrv)
    {
        driver_now = (const void *)Machine->gamedrv;
        game = Machine->gamedrv->name;
    }

    if(driver_now == frf_cached_driver) return;

    frf_cached_driver = driver_now;
    frf_cached_is_ddragon = game && strncmp(game,"ddragon",7) == 0;
    frf_announced = 0;

    log_env = getenv("FRF_PERF_LOG");
    if(frf_env_false(log_env))
        frf_cached_log_enabled = 0;
    else if(frf_env_true(log_env))
        frf_cached_log_enabled = 1;
    else
        frf_cached_log_enabled = frf_cached_is_ddragon;

    frf_reset_measurements();

    if(frf_cached_is_ddragon && !frf_announced)
    {
        frf_announced = 1;
        printf("FRF Double Dragon V2A tune=%s scheduler=existing-KRB-div16/interleave1\n",
               frf_tune_name());
    }
}

static int frf_perf_enabled(void)
{
    frf_refresh_settings();
    return frf_cached_log_enabled;
}

static cycles_t frf_elapsed(cycles_t start)
{
    cycles_t now;
    if(!start) return 0;
    now = osd_cycles();
    return now >= start ? now - start : 0;
}

static unsigned long frf_avg(cycles_t total, unsigned long count)
{
    if(!count) return 0;
    return (unsigned long)(total / (cycles_t)count);
}

static void frf_perf_report(void)
{
    unsigned long count = frf_perf.frames ? frf_perf.frames : 1;
    unsigned long partial_whole = frf_perf.partial_updates / count;
    unsigned long partial_frac = ((frf_perf.partial_updates % count) * 100UL) / count;
    const char *game = (Machine && Machine->gamedrv && Machine->gamedrv->name) ?
                       Machine->gamedrv->name : "unknown";

    printf("FRF PERF2 game=%s tune=%s frames=%lu total=%luus core=%luus update=%luus sound=%luus render=%luus osd=%luus palette=%luus remap=%luus c2p=%luus wait=%luus partial=%lu.%02lu\n",
           game, frf_tune_name(), count,
           frf_avg(frf_perf.core_us + frf_perf.video_between_us + frf_perf.update_us,count),
           frf_avg(frf_perf.core_us,count),
           frf_avg(frf_perf.update_us,count),
           frf_avg(frf_perf.sound_us,count),
           frf_avg(frf_perf.video_us,count),
           frf_avg(frf_perf.osd_us,count),
           frf_avg(frf_perf.palette_us,count),
           frf_avg(frf_perf.remap_us,count),
           frf_avg(frf_perf.c2p_us,count),
           frf_avg(frf_perf.wait_us,count),
           partial_whole,partial_frac);

    memset(&frf_perf,0,sizeof(frf_perf));
}

void frf_perf_frame_begin(void)
{
    cycles_t now;
    cycles_t between;
    cycles_t video_between;

    if(!frf_perf_enabled()) return;
    now = osd_cycles();

    if(frf_last_frame_end_tick)
    {
        between = now >= frf_last_frame_end_tick ? now - frf_last_frame_end_tick : 0;
        video_between = frf_video_lifetime_us >= frf_video_at_last_frame_end ?
                        frf_video_lifetime_us - frf_video_at_last_frame_end : 0;
        frf_perf.video_between_us += video_between;
        frf_perf.core_us += between > video_between ? between - video_between : 0;
    }

    frf_frame_begin_tick = now;
}

void frf_perf_frame_end(void)
{
    cycles_t now;
    if(!frf_perf_enabled() || !frf_frame_begin_tick) return;

    now = osd_cycles();
    if(now >= frf_frame_begin_tick)
        frf_perf.update_us += now - frf_frame_begin_tick;

    frf_last_frame_end_tick = now;
    frf_video_at_last_frame_end = frf_video_lifetime_us;
    frf_frame_begin_tick = 0;
    frf_perf.frames++;

    if(frf_perf.frames >= 120)
        frf_perf_report();
}

#define FRF_BEGIN(name) do { if(frf_perf_enabled()) name = osd_cycles(); } while(0)
#define FRF_END(name,field) do { if(frf_perf_enabled() && name) { cycles_t d = frf_elapsed(name); frf_perf.field += d; name = 0; } } while(0)

void frf_perf_sound_begin(void) { FRF_BEGIN(frf_sound_tick); }
void frf_perf_sound_end(void) { FRF_END(frf_sound_tick,sound_us); }
void frf_perf_video_begin(void) { FRF_BEGIN(frf_video_tick); }
void frf_perf_video_end(void)
{
    if(frf_perf_enabled() && frf_video_tick)
    {
        cycles_t d = frf_elapsed(frf_video_tick);
        frf_perf.video_us += d;
        frf_video_lifetime_us += d;
        frf_video_tick = 0;
    }
}
void frf_perf_note_partial_update(void) { if(frf_perf_enabled()) frf_perf.partial_updates++; }
void frf_perf_osd_begin(void) { FRF_BEGIN(frf_osd_tick); }
void frf_perf_osd_end(void) { FRF_END(frf_osd_tick,osd_us); }
void frf_perf_palette_begin(void) { FRF_BEGIN(frf_palette_tick); }
void frf_perf_palette_end(void) { FRF_END(frf_palette_tick,palette_us); }
void frf_perf_remap_begin(void) { FRF_BEGIN(frf_remap_tick); }
void frf_perf_remap_end(void) { FRF_END(frf_remap_tick,remap_us); }
void frf_perf_c2p_begin(void) { FRF_BEGIN(frf_c2p_tick); }
void frf_perf_c2p_end(void) { FRF_END(frf_c2p_tick,c2p_us); }
void frf_perf_wait_begin(void) { FRF_BEGIN(frf_wait_tick); }
void frf_perf_wait_end(void) { FRF_END(frf_wait_tick,wait_us); }
