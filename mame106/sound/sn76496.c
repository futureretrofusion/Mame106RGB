/***************************************************************************

  sn76496.c

  FRF_MAME136U4_SN76489A_BACKPORT_V9

  MAME 0.106-compatible backport of the post-0.135 TI PSG state machine.

  The original 0.106 core used the old STEP/sample-energy loop. Later MAME
  removed that scheme, added explicit chip variants, corrected zero-period
  behaviour and modelled the SN76489A noise LFSR.

  This file preserves the MAME 0.106 sndintrf and stream callback ABI.

***************************************************************************/

#include "sndintrf.h"
#include "streams.h"
#include "sn76496.h"

#define MAX_OUTPUT 0x7fff
#define V9_CORE_VERSION "1.1-FRF-MAME0136U4-SN76489A-V9"

struct SN76496
{
    sound_stream *Channel;

    INT32 VolTable[16];
    INT32 Register[8];
    INT32 LastRegister;
    INT32 Volume[4];

    UINT32 RNG;
    INT32 FeedbackMask;
    INT32 WhitenoiseTap1;
    INT32 WhitenoiseTap2;
    INT32 Negate;

    INT32 Period[4];
    INT32 Count[4];
    INT32 Output[4];

    INT32 Variant;
};

static int SN76496_noise_mode(const struct SN76496 *R)
{
    return (R->Register[6] & 4) ? 1 : 0;
}

static void SN76496Write(int chip, int data)
{
    struct SN76496 *R = sndti_token(SOUND_SN76496, chip);
    int r;
    int c;
    int n;

    stream_update(R->Channel, 0);

    if (data & 0x80)
    {
        r = (data & 0x70) >> 4;
        R->LastRegister = r;
        R->Register[r] =
            (R->Register[r] & 0x3f0) |
            (data & 0x0f);
    }
    else
    {
        r = R->LastRegister;
    }

    c = r / 2;

    switch (r)
    {
        case 0:
        case 2:
        case 4:
            if ((data & 0x80) == 0)
            {
                R->Register[r] =
                    (R->Register[r] & 0x0f) |
                    ((data & 0x3f) << 4);
            }

            /* MAME 0.136u4 zero-period hardware behaviour. */
            if (R->Register[r] != 0)
                R->Period[c] = R->Register[r];
            else
                R->Period[c] = 0x400;

            if (r == 4 &&
                (R->Register[6] & 0x03) == 0x03)
            {
                R->Period[3] = 2 * R->Period[2];
            }
            break;

        case 1:
        case 3:
        case 5:
        case 7:
            R->Volume[c] = R->VolTable[data & 0x0f];

            if ((data & 0x80) == 0)
            {
                R->Register[r] =
                    (R->Register[r] & 0x3f0) |
                    (data & 0x0f);
            }
            break;

        case 6:
            if ((data & 0x80) == 0)
            {
                R->Register[r] =
                    (R->Register[r] & 0x3f0) |
                    (data & 0x0f);
            }

            n = R->Register[6];

            if ((n & 3) == 3)
                R->Period[3] = 2 * R->Period[2];
            else
                R->Period[3] = 1 << (5 + (n & 3));

            R->RNG = R->FeedbackMask;
            break;
    }
}

WRITE8_HANDLER( SN76496_0_w )
{
    SN76496Write(0, data);
}

WRITE8_HANDLER( SN76496_1_w )
{
    SN76496Write(1, data);
}

WRITE8_HANDLER( SN76496_2_w )
{
    SN76496Write(2, data);
}

WRITE8_HANDLER( SN76496_3_w )
{
    SN76496Write(3, data);
}

WRITE8_HANDLER( SN76496_4_w )
{
    SN76496Write(4, data);
}

static void SN76496Update(
    void *param,
    stream_sample_t **inputs,
    stream_sample_t **outputs,
    int samples)
{
    struct SN76496 *R = param;
    stream_sample_t *buffer = outputs[0];

    (void)inputs;

    while (samples-- > 0)
    {
        INT32 out;
        int i;

        /*
         * One callback sample is one divided PSG clock. This matches the
         * observable clock/16 cadence without generating eight duplicate
         * high-rate samples on the Amiga host.
         */
        for (i = 0; i < 3; i++)
        {
            R->Count[i]--;

            if (R->Count[i] <= 0)
            {
                R->Output[i] ^= 1;
                R->Count[i] = R->Period[i];
            }
        }

        R->Count[3]--;

        if (R->Count[3] <= 0)
        {
            int feedback;

            feedback =
                ((R->RNG & R->WhitenoiseTap1) ? 1 : 0) ^
                (
                    ((R->RNG & R->WhitenoiseTap2) ? 1 : 0) *
                    SN76496_noise_mode(R)
                );

            R->RNG >>= 1;

            if (feedback)
                R->RNG |= R->FeedbackMask;

            R->Output[3] = R->RNG & 1;
            R->Count[3] = R->Period[3];
        }

        out =
            (R->Output[0] ? R->Volume[0] : 0) +
            (R->Output[1] ? R->Volume[1] : 0) +
            (R->Output[2] ? R->Volume[2] : 0) +
            (R->Output[3] ? R->Volume[3] : 0);

        if (R->Negate)
            out = -out;

        *buffer++ = out;
    }
}

static void SN76496_set_gain(struct SN76496 *R, int gain)
{
    int i;
    double out;

    gain &= 0xff;
    out = MAX_OUTPUT / 4;

    while (gain-- > 0)
        out *= 1.023292992;

    for (i = 0; i < 15; i++)
    {
        if (out > MAX_OUTPUT / 4)
            R->VolTable[i] = MAX_OUTPUT / 4;
        else
            R->VolTable[i] = (INT32)out;

        out /= 1.258925412;
    }

    R->VolTable[15] = 0;
}

static int SN76496_init(
    struct SN76496 *R,
    int sndindex,
    int clock,
    int sample_rate,
    const struct SN76496_config *config)
{
    int i;
    int variant = SN76496_VARIANT_SN76496;

    (void)sndindex;
    (void)sample_rate;

    if (config != NULL &&
        config->magic == SN76496_CONFIG_MAGIC)
    {
        variant = config->variant;
    }

    sample_rate = clock / 16;

    if (sample_rate <= 0)
        return 1;

    R->Channel =
        stream_create(0, 1, sample_rate, R, SN76496Update);

    if (R->Channel == NULL)
        return 1;

    R->Variant = variant;

    for (i = 0; i < 4; i++)
        R->Volume[i] = 0;

    R->LastRegister = 0;

    for (i = 0; i < 8; i += 2)
    {
        R->Register[i] = 0;
        R->Register[i + 1] = 0x0f;
    }

    for (i = 0; i < 4; i++)
    {
        R->Output[i] = 0;
        R->Period[i] = 0;
        R->Count[i] = 0;
    }

    /* Verified SN76489A/SN76496 constants from MAME 0.136u4. */
    R->FeedbackMask = 0x10000;
    R->WhitenoiseTap1 = 0x04;
    R->WhitenoiseTap2 = 0x08;
    R->Negate = 0;

    R->RNG = R->FeedbackMask;
    R->Output[3] = R->RNG & 1;

    return 0;
}

static void *sn76496_start(
    int sndindex,
    int clock,
    const void *config)
{
    struct SN76496 *chip;

    chip = auto_malloc(sizeof(*chip));
    memset(chip, 0, sizeof(*chip));

    if (SN76496_init(
            chip,
            sndindex,
            clock,
            Machine->sample_rate,
            (const struct SN76496_config *)config) != 0)
    {
        return NULL;
    }

    SN76496_set_gain(chip, 0);

    state_save_register_item_array(
        "sn76496", sndindex, chip->Register);
    state_save_register_item(
        "sn76496", sndindex, chip->LastRegister);
    state_save_register_item_array(
        "sn76496", sndindex, chip->Volume);
    state_save_register_item(
        "sn76496", sndindex, chip->RNG);
    state_save_register_item(
        "sn76496", sndindex, chip->FeedbackMask);
    state_save_register_item(
        "sn76496", sndindex, chip->WhitenoiseTap1);
    state_save_register_item(
        "sn76496", sndindex, chip->WhitenoiseTap2);
    state_save_register_item_array(
        "sn76496", sndindex, chip->Period);
    state_save_register_item_array(
        "sn76496", sndindex, chip->Count);
    state_save_register_item_array(
        "sn76496", sndindex, chip->Output);
    state_save_register_item(
        "sn76496", sndindex, chip->Variant);

    return chip;
}

static void sn76496_set_info(
    void *token,
    UINT32 state,
    sndinfo *info)
{
    (void)token;
    (void)state;
    (void)info;
}

void sn76496_get_info(
    void *token,
    UINT32 state,
    sndinfo *info)
{
    (void)token;

    switch (state)
    {
        case SNDINFO_PTR_SET_INFO:
            info->set_info = sn76496_set_info;
            break;

        case SNDINFO_PTR_START:
            info->start = sn76496_start;
            break;

        case SNDINFO_PTR_STOP:
        case SNDINFO_PTR_RESET:
            break;

        case SNDINFO_STR_NAME:
            info->s = "SN76489A/SN76496";
            break;

        case SNDINFO_STR_CORE_FAMILY:
            info->s = "TI PSG";
            break;

        case SNDINFO_STR_CORE_VERSION:
            info->s = V9_CORE_VERSION;
            break;

        case SNDINFO_STR_CORE_FILE:
            info->s = __FILE__;
            break;

        case SNDINFO_STR_CORE_CREDITS:
            info->s =
                "MAME 0.136u4 TI PSG backport for MAME 0.106";
            break;
    }
}

