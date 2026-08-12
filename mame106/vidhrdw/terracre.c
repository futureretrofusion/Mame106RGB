/***************************************************************************

  vidhrdw.c

  Functions to emulate the video hardware of the machine.

***************************************************************************/

#include "driver.h"

/* FRF_TERRACRE_SCROLL_FIX_V1
 *
 * Preserve the raw 68000 register value, detect whether the changing
 * 9-bit Y-scroll value is direct or byte-swapped on the Amiga path,
 * and retain the logical value used by the tilemap.
 */
static UINT16 xscroll;
static UINT16 yscroll;
static UINT16 frf_tc_raw_yscroll;
static UINT16 frf_tc_previous_raw_yscroll;
static int frf_tc_have_previous_yscroll;
static int frf_tc_yscroll_swap_mode = -1;
static int frf_tc_direct_score;
static int frf_tc_swapped_score;
static int frf_tc_scroll_changes;
static int frf_tc_scroll_trace_left = 96;

static int frf_tc_scroll_delta_is_small(UINT16 current, UINT16 previous)
{
	int delta = (current - previous) & 0x01ff;

	if (delta > 0x0100)
		delta -= 0x0200;

	return delta >= -8 && delta <= 8 && delta != 0;
}

static UINT16 frf_tc_normalize_yscroll(UINT16 raw)
{
#ifdef __AMIGA__
	UINT16 swapped = FLIPENDIAN_INT16(raw);

	if (frf_tc_yscroll_swap_mode < 0 &&
		frf_tc_have_previous_yscroll &&
		raw != frf_tc_previous_raw_yscroll)
	{
		UINT16 previous_swapped =
			FLIPENDIAN_INT16(frf_tc_previous_raw_yscroll);

		if (frf_tc_scroll_delta_is_small(raw & 0x01ff,
			frf_tc_previous_raw_yscroll & 0x01ff))
			frf_tc_direct_score++;

		if (frf_tc_scroll_delta_is_small(swapped & 0x01ff,
			previous_swapped & 0x01ff))
			frf_tc_swapped_score++;

		frf_tc_scroll_changes++;

		if (frf_tc_scroll_changes >= 8)
		{
			if (frf_tc_swapped_score >= frf_tc_direct_score + 3)
				frf_tc_yscroll_swap_mode = 1;
			else if (frf_tc_direct_score >= frf_tc_swapped_score + 3)
				frf_tc_yscroll_swap_mode = 0;
			else if (frf_tc_scroll_changes >= 32)
				frf_tc_yscroll_swap_mode = 0;

			if (frf_tc_yscroll_swap_mode >= 0)
				logerror(
					"FRF Terra Cresta Y scroll mode: %s "
					"(direct=%d swapped=%d changes=%d)\n",
					frf_tc_yscroll_swap_mode ? "BYTE-SWAPPED" : "DIRECT",
					frf_tc_direct_score,
					frf_tc_swapped_score,
					frf_tc_scroll_changes);
		}
	}

	frf_tc_previous_raw_yscroll = raw;
	frf_tc_have_previous_yscroll = 1;

	if (frf_tc_yscroll_swap_mode == 1)
		return swapped;

	if (frf_tc_yscroll_swap_mode < 0 &&
		(raw & 0x01ff) == 0 &&
		(swapped & 0x01ff) != 0)
		return swapped;
#endif

	return raw;
}
static tilemap *background, *foreground;
static const unsigned char *spritepalettebank;

UINT16 *amazon_videoram;

static void
get_bg_tile_info(int tile_index)
{
	/* xxxx.----.----.----
     * ----.xx--.----.----
     * ----.--xx.xxxx.xxxx */
	unsigned data = amazon_videoram[tile_index];
	unsigned color = data>>11;
	SET_TILE_INFO( 1,data&0x3ff,color,0 );
}

static void
get_fg_tile_info(int tile_index)
{
	int data = videoram16[tile_index];
	SET_TILE_INFO( 0,data&0xff,0,0 );
}

static void
draw_sprites( mame_bitmap *bitmap, const rectangle *cliprect )
{
	const gfx_element *pGfx = Machine->gfx[2];
	const UINT16 *pSource = spriteram16;
	int i;
	int transparent_pen;

	if( pGfx->total_elements > 0x200 )
	{ /* HORE HORE Kid */
		transparent_pen = 0xf;
	}
	else
	{
		transparent_pen = 0x0;
	}
	
	{ 
	struct drawgfxParams dgp0={
		bitmap, 	// dest
		pGfx, 	// gfx
		0, 	// code
		0, 	// color
		0, 	// flipx
		0, 	// flipy
		0, 	// sx
		0, 	// sy
		cliprect, 	// clip
		TRANSPARENCY_PEN, 	// transparency
		transparent_pen, 	// transparent_color
		0, 	// scalex
		0, 	// scaley
		NULL, 	// pri_buffer
		0 	// priority_mask
	  };
	for( i=0; i<0x200; i+=8 )
	{
		int tile = pSource[1]&0xff;
		int attrs = pSource[2];
		int flipx = attrs&0x04;
		int flipy = attrs&0x08;
		int color = (attrs&0xf0)>>4;
		int sx = (pSource[3] & 0xff) - 0x80 + 256 * (attrs & 1);
		int sy = 240 - (pSource[0] & 0xff);

		if( transparent_pen )
		{
			int bank;

			if( attrs&0x02 ) tile |= 0x200;
			if( attrs&0x10 ) tile |= 0x100;

			bank = (tile&0xfc)>>1;
			if( tile&0x200 ) bank |= 0x80;
			if( tile&0x100 ) bank |= 0x01;

			color &= 0xe;
			color += 16*(spritepalettebank[bank]&0xf);
		}
		else
		{
			if( attrs&0x02 ) tile|= 0x100;
			color += 16 * (spritepalettebank[(tile>>1)&0xff] & 0x0f);
		}

		if (flip_screen)
		{
				sx=240-sx;
				sy=240-sy;
				flipx = !flipx;
				flipy = !flipy;
		}

		
		dgp0.code = tile;
		dgp0.color = color;
		dgp0.flipx = flipx;
		dgp0.flipy = flipy;
		dgp0.sx = sx;
		dgp0.sy = sy;
		drawgfx(&dgp0);

		pSource += 4;
	}
	} // end of patch paragraph

}

PALETTE_INIT( amazon )
{
	#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
	#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])
	int i;
	for( i = 0; i<Machine->drv->total_colors; i++)
	{
		int bit0,bit1,bit2,bit3,r,g,b;

		bit0 = (color_prom[0] >> 0) & 0x01;
		bit1 = (color_prom[0] >> 1) & 0x01;
		bit2 = (color_prom[0] >> 2) & 0x01;
		bit3 = (color_prom[0] >> 3) & 0x01;
		r = 0x0e * bit0 + 0x1f * bit1 + 0x43 * bit2 + 0x8f * bit3;
		bit0 = (color_prom[Machine->drv->total_colors] >> 0) & 0x01;
		bit1 = (color_prom[Machine->drv->total_colors] >> 1) & 0x01;
		bit2 = (color_prom[Machine->drv->total_colors] >> 2) & 0x01;
		bit3 = (color_prom[Machine->drv->total_colors] >> 3) & 0x01;
		g = 0x0e * bit0 + 0x1f * bit1 + 0x43 * bit2 + 0x8f * bit3;
		bit0 = (color_prom[2*Machine->drv->total_colors] >> 0) & 0x01;
		bit1 = (color_prom[2*Machine->drv->total_colors] >> 1) & 0x01;
		bit2 = (color_prom[2*Machine->drv->total_colors] >> 2) & 0x01;
		bit3 = (color_prom[2*Machine->drv->total_colors] >> 3) & 0x01;
		b = 0x0e * bit0 + 0x1f * bit1 + 0x43 * bit2 + 0x8f * bit3;

		palette_set_color(i,r,g,b);
		color_prom++;
	}

	color_prom += 2*Machine->drv->total_colors;
	/* color_prom now points to the beginning of the lookup table */


	/* characters use colors 0-15 */
	for (i = 0;i < TOTAL_COLORS(0);i++)
		COLOR(0,i) = i;

	/* background tiles use colors 192-255 in four banks */
	/* the bottom two bits of the color code select the palette bank for */
	/* pens 0-7; the top two bits for pens 8-15. */
	for (i = 0;i < TOTAL_COLORS(1);i++)
	{
		if (i & 8) COLOR(1,i) = 192 + (i & 0x0f) + ((i & 0xc0) >> 2);
		else COLOR(1,i) = 192 + (i & 0x0f) + ((i & 0x30) >> 0);
	}

	/* sprites use colors 128-191 in four banks */
	/* The lookup table tells which colors to pick from the selected bank */
	/* the bank is selected by another PROM and depends on the top 8 bits of */
	/* the sprite code. The PROM selects the bank *separately* for pens 0-7 and */
	/* 8-15 (like for tiles). */
	for (i = 0;i < TOTAL_COLORS(2)/16;i++)
	{
		int j;

		for (j = 0;j < 16;j++)
		{
			if (i & 8)
				COLOR(2,i + j * (TOTAL_COLORS(2)/16)) = 128 + ((j & 0x0c) << 2) + (*color_prom & 0x0f);
			else
				COLOR(2,i + j * (TOTAL_COLORS(2)/16)) = 128 + ((j & 0x03) << 4) + (*color_prom & 0x0f);
		}

		color_prom++;
	}

	/* color_prom now points to the beginning of the sprite palette bank table */
	spritepalettebank = color_prom;	/* we'll need it at run time */
}

WRITE16_HANDLER( amazon_background_w )
{
	COMBINE_DATA( &amazon_videoram[offset] );
	tilemap_mark_tile_dirty( background, offset );
}

WRITE16_HANDLER( amazon_foreground_w )
{
	COMBINE_DATA( &videoram16[offset] );
	tilemap_mark_tile_dirty( foreground, offset );
}

WRITE16_HANDLER( amazon_flipscreen_w )
{
	if( ACCESSING_LSB )
	{
		coin_counter_w( 0, data&0x01 );
		coin_counter_w( 1, (data&0x02)>>1 );
		flip_screen_set(data&0x04);
	}
}

WRITE16_HANDLER( amazon_scrolly_w )
{
	frf_tc_raw_yscroll =
		(frf_tc_raw_yscroll & mem_mask) | (data & ~mem_mask);
	yscroll = frf_tc_normalize_yscroll(frf_tc_raw_yscroll) & 0x01ff;

	tilemap_set_scrolly(background,0,yscroll);

	if (frf_tc_scroll_trace_left > 0)
	{
		logerror(
			"FRF TC SCROLLY data=%04x mask=%04x raw=%04x logical=%04x mode=%d\n",
			data,
			mem_mask,
			frf_tc_raw_yscroll,
			yscroll,
			frf_tc_yscroll_swap_mode);
		frf_tc_scroll_trace_left--;
	}
}

WRITE16_HANDLER( amazon_scrollx_w )
{
	COMBINE_DATA(&xscroll);
	tilemap_set_scrollx(background,0,xscroll);

	if (frf_tc_scroll_trace_left > 0)
	{
		logerror(
			"FRF TC SCROLLX data=%04x mask=%04x logical=%04x\n",
			data,
			mem_mask,
			xscroll);
		frf_tc_scroll_trace_left--;
	}
}

/* FRF_TERRACRE_COMBINED_SCROLL_V1
 *
 * The Amiga fast 68000 memory path performs one handler lookup for a
 * longword write. Terra Cresta writes X and Y scroll as adjacent words,
 * so both addresses must belong to one handler.
 */
WRITE16_HANDLER( amazon_scroll_w )
{
	switch (offset)
	{
		case 0:
			COMBINE_DATA(&xscroll);
			tilemap_set_scrollx(background,0,xscroll);
			break;

		case 1:
			COMBINE_DATA(&yscroll);
			tilemap_set_scrolly(background,0,yscroll);
			break;
	}
}


VIDEO_START( amazon )
{
	background = tilemap_create(get_bg_tile_info,tilemap_scan_cols,TILEMAP_OPAQUE,16,16,64,32);
	foreground = tilemap_create(get_fg_tile_info,tilemap_scan_cols,TILEMAP_TRANSPARENT,8,8,64,32);
	if( background && foreground )
	{
		tilemap_set_transparent_pen(foreground,0xf);
		return 0;
	}
	return 1;
}

VIDEO_UPDATE( amazon )
{
	/*
	 * Reapply global scroll immediately before drawing in case the Amiga
	 * tilemap path failed to retain the single-row/single-column state.
	 */
	tilemap_set_scrollx(background,0,xscroll);
	tilemap_set_scrolly(background,0,yscroll);

	if( xscroll&0x2000 )
	{
		fillbitmap( bitmap,get_black_pen(),cliprect );
	}
	else
	{
		tilemap_draw( bitmap,cliprect, background, 0, 0 );
	}
	draw_sprites( bitmap,cliprect );
	tilemap_draw( bitmap,cliprect, foreground, 0, 0 );
}
