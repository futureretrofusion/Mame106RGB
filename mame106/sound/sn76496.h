#ifndef SN76496_H
#define SN76496_H

/*
 * FRF_MAME136U4_SN76489A_BACKPORT_V9
 *
 * MAME 0.106 exposes one generic SN76496 sound type. This configuration
 * lets a driver identify the physical variant while preserving the old ABI.
 */
#define SN76496_CONFIG_MAGIC             0x534e3634U
#define SN76496_VARIANT_SN76496          0
#define SN76496_VARIANT_SN76489A         1

struct SN76496_config
{
    unsigned int magic;
    int variant;
};

WRITE8_HANDLER( SN76496_0_w );
WRITE8_HANDLER( SN76496_1_w );
WRITE8_HANDLER( SN76496_2_w );
WRITE8_HANDLER( SN76496_3_w );
WRITE8_HANDLER( SN76496_4_w );

#endif

