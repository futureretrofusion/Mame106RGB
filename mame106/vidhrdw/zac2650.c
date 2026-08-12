/*************************************************************/
/*                                                           */
/* Zaccaria/Zelco S2650 based games video                    */
/*                                                           */
/*************************************************************/

#include "driver.h"

UINT8 *s2636ram;
static mame_bitmap *spritebitmap;

static UINT8 dirtychar[256>>3];
static int CollisionBackground;
static int CollisionSprite;

static tilemap *bg_tilemap;


/**************************************************************/
/* The S2636 is a standard sprite chip used by several boards */
/* Emulation of this chip may be moved into a seperate unit   */
/* once it's workings are fully understood.                   */
/**************************************************************/

WRITE8_HANDLER( tinvader_videoram_w )
{
	if (videoram[offset] != data)
	{
		videoram[offset] = data;
		tilemap_mark_tile_dirty(bg_tilemap, offset);
	}
}

WRITE8_HANDLER( zac_s2636_w )
{
	if (s2636ram[offset] != data)
    {
		s2636ram[offset] = data;
        dirtychar[offset>>3] = 1;
    }
}

READ8_HANDLER( zac_s2636_r )
{
	if(offset!=0xCB) return s2636ram[offset];
    else return CollisionSprite;
}

READ8_HANDLER( tinvader_port_0_r )
{
	return input_port_0_r(0) - CollisionBackground;
}

/*****************************************/
/* Check for Collision between 2 sprites */
/*****************************************/

/* FRF FIX57: restored sprite-collision graphics and colours */
int SpriteCollision(int first,int second)
{
	int Checksum = 0;
	int x,y;
	struct drawgfxParams first_draw = {
		spritebitmap, Machine->gfx[1], 0, 0, 0, 0, 0, 0, NULL,
		TRANSPARENCY_NONE, 0, 0, 0, NULL, 0
	};
	struct drawgfxParams second_draw = {
		spritebitmap, Machine->gfx[1], 0, 1, 0, 0, 0, 0, NULL,
		TRANSPARENCY_PEN, 0, 0, 0, NULL, 0
	};

	if ((s2636ram[first * 0x10 + 10] < 0xf0) &&
	    (s2636ram[second * 0x10 + 10] < 0xf0))
	{
		int fx = (s2636ram[first * 0x10 + 10] * 4) - 22;
		int fy = (s2636ram[first * 0x10 + 12] * 3) + 3;
		int expand = (first == 1) ? 2 : 1;

		first_draw.gfx = Machine->gfx[expand];
		first_draw.code = first * 2;
		first_draw.color = 0;
		first_draw.sx = fx;
		first_draw.sy = fy;
		drawgfx(&first_draw);

		for (x = fx; x < fx + Machine->gfx[expand]->width; x++)
			for (y = fy; y < fy + Machine->gfx[expand]->height; y++)
			{
				if ((x < Machine->visible_area.min_x) || (x > Machine->visible_area.max_x) ||
				    (y < Machine->visible_area.min_y) || (y > Machine->visible_area.max_y))
					continue;
				Checksum += read_pixel(spritebitmap, x, y);
			}

		second_draw.code = second * 2;
		second_draw.color = 1;
		second_draw.sx = (s2636ram[second * 0x10 + 10] * 4) - 22;
		second_draw.sy = (s2636ram[second * 0x10 + 12] * 3) + 3;
		drawgfx(&second_draw);

		for (x = fx; x < fx + Machine->gfx[expand]->width; x++)
			for (y = fy; y < fy + Machine->gfx[expand]->height; y++)
			{
				if ((x < Machine->visible_area.min_x) || (x > Machine->visible_area.max_x) ||
				    (y < Machine->visible_area.min_y) || (y > Machine->visible_area.max_y))
					continue;
				Checksum -= read_pixel(spritebitmap, x, y);
			}

		first_draw.color = 1;
		drawgfx(&first_draw);
	}
	return Checksum;
}

static void get_bg_tile_info(int tile_index)
{
	int code = videoram[tile_index];

	SET_TILE_INFO(0, code, 0, 0)
}

VIDEO_START( tinvader )
{
	bg_tilemap = tilemap_create(get_bg_tile_info, tilemap_scan_rows,
		TILEMAP_OPAQUE, 24, 24, 32, 32);

	if ( !bg_tilemap )
		return 1;

	if ((spritebitmap = auto_bitmap_alloc(Machine->drv->screen_width,Machine->drv->screen_height)) == 0)
		return 1;

	if ((tmpbitmap = auto_bitmap_alloc(Machine->drv->screen_width,Machine->drv->screen_height)) == 0)
		return 1;

	return 0;
}

/* FRF FIX57: restored expanded-sprite graphics and collision passes */
static void tinvader_draw_sprites(mame_bitmap *bitmap)
{
	int offs;
	struct drawgfxParams sprite_draw = {
		bitmap, Machine->gfx[1], 0, 0, 0, 0, 0, 0, NULL,
		TRANSPARENCY_PEN, 0, 0, 0, NULL, 0
	};

	CollisionBackground = 0;
	copybitmap(tmpbitmap,bitmap,0,0,0,0,&Machine->visible_area,TRANSPARENCY_NONE,0);

	for (offs = 0; offs < 0x50; offs += 0x10)
	{
		if ((s2636ram[offs + 10] < 0xf0) && (offs != 0x30))
		{
			int spriteno = offs / 8;
			int expand = ((s2636ram[0xc0] & (spriteno * 2)) != 0) ? 2 : 1;
			int bx = (s2636ram[offs + 10] * 4) - 22;
			int by = (s2636ram[offs + 12] * 3) + 3;
			int x,y;

			if (dirtychar[spriteno])
			{
				decodechar(Machine->gfx[1],spriteno,s2636ram,Machine->drv->gfxdecodeinfo[1].gfxlayout);
				decodechar(Machine->gfx[2],spriteno,s2636ram,Machine->drv->gfxdecodeinfo[2].gfxlayout);
				dirtychar[spriteno] = 0;
			}

			sprite_draw.gfx = Machine->gfx[expand];
			sprite_draw.code = spriteno;
			sprite_draw.color = 1;
			sprite_draw.sx = bx;
			sprite_draw.sy = by;
			drawgfx(&sprite_draw);

			for (x = bx; x < bx + Machine->gfx[expand]->width; x++)
				for (y = by; y < by + Machine->gfx[expand]->height; y++)
				{
					if ((x < Machine->visible_area.min_x) || (x > Machine->visible_area.max_x) ||
					    (y < Machine->visible_area.min_y) || (y > Machine->visible_area.max_y))
						continue;
					if (read_pixel(bitmap, x, y) != read_pixel(tmpbitmap, x, y))
					{
						CollisionBackground = 0x80;
						break;
					}
				}

			sprite_draw.color = 0;
			drawgfx(&sprite_draw);
		}
	}

	CollisionSprite = 0;
	if (SpriteCollision(0,2)) CollisionSprite |= 0x10;
	if (SpriteCollision(0,4)) CollisionSprite |= 0x08;
	if (SpriteCollision(1,2)) CollisionSprite |= 0x04;
	if (SpriteCollision(1,4)) CollisionSprite |= 0x02;
}

VIDEO_UPDATE( tinvader )
{
	tilemap_draw(bitmap, &Machine->visible_area, bg_tilemap, 0, 0);
	tinvader_draw_sprites(bitmap);
}
