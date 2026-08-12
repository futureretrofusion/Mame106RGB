/*====================================================================*/
/*               TX-1/Buggy Boy  (Tatsumi) Hardware                   */
/*                      Philip J Bennett 2005                         */
/*                                                                    */
/*                         Video Emulation                            */
/*====================================================================*/

#include "driver.h"

extern UINT8 *buggyb1_vram;
extern UINT8 *buggyboy_vram;
extern UINT8 *bb_objram;
extern UINT8 *bb_sky;

extern tilemap *buggyb1_tilemap;
extern tilemap *buggyboy_tilemap;
extern size_t bb_objectram_size;

extern UINT8 *tx1_vram;
extern UINT8 *tx1_object_ram;

extern tilemap *tx1_tilemap;
extern size_t tx1_objectram_size;

/* FRF FIX49: restored native MAME 0.106 TX-1/Buggy Boy video source */

/********/
/* TX-1 */
/********/

WRITE8_HANDLER( tx1_vram_w )
{
	if (tx1_vram[offset] != data)
		tilemap_mark_tile_dirty(tx1_tilemap, offset / 2);

	tx1_vram[offset] = data;
}

static void get_tx1_tile_info(int tile_index)
{
	int bit15, upper, lower, tileno;

	tile_index <<= 1;
	bit15 = (tx1_vram[tile_index + 1] & 0x80) << 6;
	upper = (tx1_vram[tile_index + 1] & 0x03) << 11;
	lower = tx1_vram[tile_index] << 3;
	tileno = (bit15 | upper | lower) / 8;

	SET_TILE_INFO(0, tileno, 0, 0)
}

VIDEO_START( tx1 )
{
	tx1_tilemap = tilemap_create(
		get_tx1_tile_info,
		tilemap_scan_rows,
		TILEMAP_TRANSPARENT,
		8, 8, 128, 64);

	tilemap_set_transparent_pen(tx1_tilemap, 0xff);
	return 0;
}

VIDEO_UPDATE( tx1 )
{
	tilemap_draw(bitmap, cliprect, tx1_tilemap, 0, 0);
}

/*************/
/* Buggy Boy */
/*************/

PALETTE_INIT( buggyboy )
{
	int i;

	for (i = 0; i < 256; i++)
	{
		int bit0, bit1, bit2, bit3, bit4, r, g, b;

		bit0 = color_prom[i] & 1;
		bit1 = (color_prom[i] >> 1) & 1;
		bit2 = (color_prom[i] >> 2) & 1;
		bit3 = (color_prom[i] >> 3) & 1;
		bit4 = (color_prom[i + 0x300] >> 2) & 1;
		r = 0x06 * bit4 + 0x0d * bit0 + 0x1e * bit1
		  + 0x41 * bit2 + 0x8a * bit3;

		bit0 = color_prom[i + 0x100] & 1;
		bit1 = (color_prom[i + 0x100] >> 1) & 1;
		bit2 = (color_prom[i + 0x100] >> 2) & 1;
		bit3 = (color_prom[i + 0x100] >> 3) & 1;
		bit4 = (color_prom[i + 0x300] >> 1) & 1;
		g = 0x06 * bit4 + 0x0d * bit0 + 0x1e * bit1
		  + 0x41 * bit2 + 0x8a * bit3;

		bit0 = color_prom[i + 0x200] & 1;
		bit1 = (color_prom[i + 0x200] >> 1) & 1;
		bit2 = (color_prom[i + 0x200] >> 2) & 1;
		bit3 = (color_prom[i + 0x200] >> 3) & 1;
		bit4 = color_prom[i + 0x300] & 1;
		b = 0x06 * bit4 + 0x0d * bit0 + 0x1e * bit1
		  + 0x41 * bit2 + 0x8a * bit3;

		palette_set_color(i, r, g, b);
	}

	/* Objects use colours 0-63. */
	for (i = 0; i < 2048; i++)
		colortable[256 + i] =
			((0x0f - color_prom[i + 0x500]) & 0x0f) + 48;

	for (i = 0; i < 2048; i++)
		colortable[256 + 2048 + i] =
			((0x0f - color_prom[i + 0x500]) & 0x0f) + 32;

	for (i = 0; i < 2048; i++)
		colortable[256 + 2048 + 2048 + i] =
			((0x0f - color_prom[i + 0x500]) & 0x0f) + 16;

	for (i = 0; i < 2048; i++)
		colortable[256 + 2048 + 2048 + 2048 + i] =
			((0x0f - color_prom[i + 0x500]) & 0x0f);

	/* Road uses colours 64-127. */
	for (i = 0; i < 256; i++)
		colortable[256 + 8192 + i] =
			(color_prom[i + 0x1500] & 0x0f) + 64;

	for (i = 0; i < 256; i++)
		colortable[256 + 8192 + 256 + i] =
			(color_prom[i + 0x1500] & 0x0f) + 64 + 16;

	for (i = 0; i < 256; i++)
		colortable[256 + 8192 + 256 * 2 + i] =
			(color_prom[i + 0x1500] & 0x0f) + 64 + 32;

	for (i = 0; i < 256; i++)
		colortable[256 + 8192 + 256 * 3 + i] =
			(color_prom[i + 0x1500] & 0x0f) + 64 + 48;

	/* Characters use colours 192-255. */
	for (i = 0; i < 256; i++)
		colortable[i] = (color_prom[i + 0x400] & 0x0f) + 192;
}

WRITE8_HANDLER( buggyb1_vram_w )
{
	if (buggyb1_vram[offset] != data)
		tilemap_mark_tile_dirty(buggyb1_tilemap, offset / 2);

	buggyb1_vram[offset] = data;
}

WRITE8_HANDLER( buggyboy_vram_w )
{
	if (buggyboy_vram[offset] != data)
		tilemap_mark_tile_dirty(buggyboy_tilemap, offset / 2);

	buggyboy_vram[offset] = data;
}

static void get_buggyb1_tile_info(int tile_index)
{
	int color, bit15, upper, lower, tileno;

	tile_index <<= 1;
	color = (buggyb1_vram[tile_index + 1] >> 2) & 0x3f;
	bit15 = buggyb1_vram[tile_index + 1] & 0x80;
	upper = (buggyb1_vram[tile_index + 1] & 0x03) << 11;
	lower = buggyb1_vram[tile_index] << 3;
	tileno = ((bit15 << 6) | upper | lower) / 8;

	SET_TILE_INFO(0, tileno, color, 0);
}

static void get_buggyboy_tile_info(int tile_index)
{
	int color, bit15, upper, lower, tileno;

	tile_index <<= 1;
	color = (buggyboy_vram[tile_index + 1] >> 2) & 0x3f;
	bit15 = (buggyboy_vram[tile_index + 1] & 0x80) << 6;
	upper = (buggyboy_vram[tile_index + 1] & 0x03) << 11;
	lower = buggyboy_vram[tile_index] << 3;
	tileno = (bit15 | upper | lower) / 8;

	SET_TILE_INFO(0, tileno, color, 0)
}

/* Rewrite once the original scale parameters are fully understood. */
static void bb_draw_objects(mame_bitmap *bitmap, const rectangle *cliprect)
{
	int offs;
	UINT8 PROM_lookup;
	UINT16 ROM_lookup;

	UINT8 *rom_lut =
		(UINT8 *)memory_region(REGION_USER3);
	UINT8 *prom_lut =
		(UINT8 *)memory_region(REGION_PROMS) + 0x1600;
	UINT8 *ROM_LUTA =
		(UINT8 *)memory_region(REGION_USER2);
	UINT8 *ROM_LUTB =
		(UINT8 *)memory_region(REGION_USER2) + 0x8000;
	UINT8 *ROM_CLUT =
		(UINT8 *)memory_region(REGION_USER3) + 0x2000;

	struct drawgfxParams object_draw = {
		bitmap,
		Machine->gfx[0],
		0,
		0,
		0,
		0,
		0,
		0,
		cliprect,
		TRANSPARENCY_PEN,
		0,
		0x0000ffff,
		0x0000ffff,
		NULL,
		0
	};

	for (offs = 0; offs <= bb_objectram_size; offs += 16)
	{
		int inc, last;
		int bit_12, PSA0_12, PSA, object_flip_x;
		int index_y, index_x, index = 0;

		if (bb_objram[offs + 1] == 0xff)
			return;

		PROM_lookup = bb_objram[offs];
		ROM_lookup =
			(bb_objram[offs] << 4)
			| ((bb_objram[offs + 3] >> 3) & 0x0f);

		if (rom_lut[ROM_lookup] == 0xff)
			continue;

		bit_12 =
			(((bb_objram[offs] >> 7) & 1)
			| ((bb_objram[offs] >> 6) & 1)) << 12;

		PSA0_12 =
			(((prom_lut[PROM_lookup] & 0x0f) << 8)
			| rom_lut[ROM_lookup]
			| bit_12) & 0x1fff;

		PSA = PSA0_12 << 2;
		object_flip_x = (bb_objram[offs + 5] >> 7) & 1;

		for (index_y = 0; index_y < 16; index_y++)
		{
			if (object_flip_x)
			{
				index_x = 16;
				inc = -1;
				last = 0;
			}
			else
			{
				index_x = 0;
				inc = 1;
				last = 16;
			}

			while (index_x != last)
			{
				int chunk_number =
					(ROM_LUTB[PSA + index] << 8)
					| ROM_LUTA[PSA + index];

				int sx =
					bb_objram[offs + 8]
					+ (bb_objram[offs + 9] << 8)
					+ index_x * 8;

				int sy =
					bb_objram[offs + 1]
					+ index_y * 7;

				int bit13 =
					(bb_objram[offs + 5] & 0x10) << 9;

				int bit12 =
					(chunk_number & 0x2000) >> 1;

				int bits6_and_7 =
					(chunk_number & 0x1000
						? chunk_number
						: bb_objram[offs + 5] << 6)
					& 0x00c0;

				int CLUT_ROM_ADDR =
					(chunk_number & 0x0f3f)
					+ bits6_and_7
					+ bit12
					+ bit13;

				int bits10_and_11 =
					0x0c00
					- ((bb_objram[offs + 5] << 8) & 0x0c00);

				int OPCS =
					(bb_objram[offs + 5] & 0x60) << 3;

				int OPCD =
					(ROM_CLUT[CLUT_ROM_ADDR]
					+ OPCS
					+ bits10_and_11) & 0x0fff;

				int tmp = OPCD & 0x7f;
				int tmp2 = (OPCD & 0x300) >> 1;
				int tmp3 = bits10_and_11 >> 1;
				int color = tmp + tmp2 + tmp3;
				int trans;

				int bank =
					(((bb_objram[offs + 5] >> 3) & 0x02)
					| ((chunk_number >> 13) & 0x01)) + 1;

				int zoomx = 0xffff;
				int zoomy = 0xffff;
				int flipx =
					((chunk_number >> 15) & 1)
					^ object_flip_x;
				int flipy = 0;
				const gfx_element *gfx = Machine->gfx[bank];

				if (!(OPCD & 0x80))
					trans = TRANSPARENCY_PEN;
				else
					trans = TRANSPARENCY_NONE;

				index_x += inc;
				index++;

				object_draw.gfx = gfx;
				object_draw.code = chunk_number;
				object_draw.color = color;
				object_draw.flipx = flipx;
				object_draw.flipy = flipy;
				object_draw.sx = sx;
				object_draw.sy = sy;
				object_draw.transparency = trans;
				object_draw.transparent_color = 0;
				object_draw.scalex = zoomx;
				object_draw.scaley = zoomy;
				drawgfxzoom(&object_draw);
			}
		}
	}
}

VIDEO_START( buggyb1 )
{
	buggyb1_tilemap = tilemap_create(
		get_buggyb1_tile_info,
		tilemap_scan_rows,
		TILEMAP_TRANSPARENT,
		8, 8, 64, 64);

	tilemap_set_transparent_pen(buggyb1_tilemap, 0);
	return 0;
}

VIDEO_START( buggyboy )
{
	buggyboy_tilemap = tilemap_create(
		get_buggyboy_tile_info,
		tilemap_scan_rows,
		TILEMAP_TRANSPARENT,
		8, 8, 128, 64);

	tilemap_set_transparent_pen(buggyboy_tilemap, 0);
	return 0;
}

static void draw_sky(mame_bitmap *bitmap)
{
	int x, y, colour;

	for (y = 0; y < 256; y++)
	{
		for (x = 0; x <= Machine->visible_area.max_x; x++)
		{
			colour = (((*bb_sky & 0x7f) + y) >> 2) & 0x3f;
			plot_pixel(bitmap, x, y, Machine->pens[0x80 + colour]);
		}
	}
}

/*
 * The original MAME 0.106 layer mixing remains preliminary and does not
 * completely reproduce the PCB's character/HUD/object priority logic.
 */

VIDEO_UPDATE( buggyb1 )
{
	if (*bb_sky & 0x80)
	{
		draw_sky(bitmap);
		tilemap_draw(bitmap, cliprect, buggyb1_tilemap, 0, 0);
		bb_draw_objects(bitmap, cliprect);
	}
	else
	{
		tilemap_draw(
			bitmap,
			cliprect,
			buggyb1_tilemap,
			TILEMAP_IGNORE_TRANSPARENCY,
			0);

		bb_draw_objects(bitmap, cliprect);
	}
}

VIDEO_UPDATE( buggyboy )
{
	if (*bb_sky & 0x80)
	{
		draw_sky(bitmap);
		tilemap_draw(bitmap, cliprect, buggyboy_tilemap, 0, 0);
		bb_draw_objects(bitmap, cliprect);
	}
	else
	{
		tilemap_draw(
			bitmap,
			cliprect,
			buggyboy_tilemap,
			TILEMAP_IGNORE_TRANSPARENCY,
			0);

		bb_draw_objects(bitmap, cliprect);
	}
}
