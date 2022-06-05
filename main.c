#include "../ass.h"
IBios* interface;

#define MAP1_CLEAR_TILE	0
#define MAP2_CLEAR_TILE	128
#define MAP_TILE_LIMIT	1024
#define OBJ_CLEAR_TILE	48
#define LOGO_START_TILE	288

#define SCREEN_TILE_W	40
#define SCREEN_TILE_H	30

#define ACTIVE_TILE_W	32
#define ACTIVE_TILE_H	28

#define BORDER_TILE_W	((SCREEN_TILE_W - ACTIVE_TILE_W) / 2)
#define BORDER_TILE_H	((SCREEN_TILE_H - ACTIVE_TILE_H) / 2)

#define MAP_TILE_W		64
#define MAP_TILE_H		64

#define PLAYFIELD_TILE_W	32
#define PLAYFIELD_TILE_H	32

#define RAM_ANALOG_CALIBRATION	0x1684

extern const uint8_t PlayfieldTiles[];
extern const uint8_t ObjectTiles[];

extern uint16_t AnalogRead;
extern uint16_t AnalogSelect[4];
extern uint16_t DigitalRead;
extern uint32_t Objects[64];
extern uint16_t Palette[256];
extern uint16_t ProgramRam[];

extern void FFEntry();
extern void VBlankHandler();
extern void HBlankHandler();

static uint16_t PaletteLut[256];
static uint16_t ButtonsDown;
static uint16_t ButtonsToggled;

// Convert arcade color to A3X format
static uint16_t ColorToB5G5R5(uint8_t bits)
{
	uint8_t r = bits & 7;
	r = (r << 2) | (r >> 1);
	uint8_t g = (bits >> 3) & 7;
	g = (g << 2) | (g >> 1);
	uint8_t b = (bits >> 6) & 3;
	b = (b << 3) | (b << 2) | (b >> 1);
	return (b << 10) | (g << 5) | r;
}

// Convert entire arcade palette for use with tile map
void UpdatePalette(void)
{
	for (int i = 0; i < 256; i++)
	{
		PALETTE[i] = PaletteLut[Palette[i] & 0xff];
	}
}

// Convert arcade object list to A3X objects. There are too many 8x8 tiles
// (1024) and too many palettes (32 selectable) for static assignment, so
// instead we dynamically copy just the tiles and the palettes we need for each
// frame. There is an upper bound of 48 double size objects, or 192 8x8 tiles.
// The game also seems to limit itself to 15 palettes at once, which works out.
void UpdateObjects(void)
{
	// Map 32 selectable 4-color palettes to the (up to) 15 currently in use.
	uint8_t pcount = 0;
	uint8_t pindices[32] = { 0 };

	// The first 16 object indices are unused.
	for (int i = 16; i < 64; i++)
	{
		uint32_t o = Objects[i];
		uint8_t hflip = (o >> 31) & 1;
		uint8_t vflip = (o >> 30) & 1;
		uint8_t pal = (o >> 24) & 0x1f;
		uint8_t tile = (o >> 16) & 0xff;
		uint8_t xpos = ((o >> 8) & 0xff) + 8;
		uint8_t ypos = ~(o & 0xff) - 16;

		// There is no explicit enable bit, so check a couple obvious things.
		if (tile != OBJ_CLEAR_TILE && ypos < ACTIVE_TILE_H * 8)
		{
			uint16_t sx = xpos + BORDER_TILE_W * 8 - 8;
			uint16_t sy = ypos + BORDER_TILE_H * 8;

			// Copy the 4 8x8 tiles needed by this object. We may copy some twice for different objects.
			MISC->DmaCopy(TILESET + i * 4 * 32, ObjectTiles + tile * 4 * 32, 4 * 32 / 4, DMA_INT);

			if (pindices[pal])
			{
				// This palette is already in use.
				pal = pindices[pal];
			}
			else if (pcount < 15)
			{
				// Append the converted palette to the global object palette.
				pcount++;
				for (int j = 0; j < 4; j++)
				{
					uint16_t c = PaletteLut[Palette[pal * 4 + j] & 0xff];
					PALETTE[256 + pcount * 16 + j] = c;
				}
				pindices[pal] = pcount;
				pal = pcount;
			}
			else
			{
				// There's really nothing good to be done if we exceed our limit.
			}

			OBJECTS_A[i] = (pal << 12) | (1 << 11) | (i * 4);
			OBJECTS_B[i] = (1 << 28) | (vflip << 27) | (hflip << 26) | (sy << 12) | sx;
		}
		else
		{
			OBJECTS_A[i] = 0;
		}
	}
}

void UpdateInput(void)
{
	uint16_t buttons = INP_JOYPAD1;
	uint16_t pressed = buttons & ~ButtonsDown;
	ButtonsDown = buttons;
	ButtonsToggled ^= pressed;

	uint16_t digital = 0xFF;
	if (buttons & 0x10) // a (kb z)
	{
		digital &= ~(1 << 5); // throw
	}
	if (buttons & 0x20) // b (kb x)
	{
		digital &= ~(1 << 4); // aux coin
	}
	if (buttons & 0x40) // x (kb a)
	{
		digital &= ~(1 << 0); // left coin
	}
	if (buttons & 0x80) // y (kb s)
	{
		digital &= ~(1 << 1); // right coin
	}
	if (buttons & 0x100) // back (kb d)
	{
		digital &= ~(1 << 3); // start 2
	}
	if (buttons & 0x200) // start (kb f)
	{
		digital &= ~(1 << 2); // start 1
	}
	if (ButtonsToggled & 0x400) // l (kb c)
	{
		digital &= ~(1 << 7); // service
	}

	DigitalRead = digital;

	uint8_t analogs[4] = { 0x7F, 0x7F, 0x80, 0x80 };
	if (buttons & 0x8) // left
	{
		analogs[3] = 0xFF;
	}
	else if (buttons & 0x2) // right
	{
		analogs[3] = 0x00;
	}
	else
	{
		int value = INP_JOYSTK1H;
		value = -value + 0x7f;
		analogs[3] = value;
	}
	if (buttons & 0x1) // up
	{
		analogs[1] = 0xFF;
	}
	else if (buttons & 0x4) // down
	{
		analogs[1] = 0x00;
	}
	else
	{
		int value = INP_JOYSTK1V;
		value = -value + 0x7F;
		analogs[1] = value;
	}

	AnalogRead = 0xFF7F;
	for (int i = 0; i < 4; i++)
	{
		if (AnalogSelect[i])
		{
			AnalogSelect[i] = 0;
			AnalogRead = analogs[i] | 0xFF00;
		}
	}

	// Override analog calibration settings to the maximum range
	ProgramRam[(RAM_ANALOG_CALIBRATION >> 1) + 0] = 0x0000;
	ProgramRam[(RAM_ANALOG_CALIBRATION >> 1) + 1] = 0x00FF;
	ProgramRam[(RAM_ANALOG_CALIBRATION >> 1) + 2] = 0x0000;
	ProgramRam[(RAM_ANALOG_CALIBRATION >> 1) + 3] = 0x00FF;
}

// Convert a playfield write to two tile map writes. The arcade hardware
// supports 64 4-color palettes, which maps poorly to A3X's 16 16-color
// palettes. To compensate, we keep 4 copies of each tile in memory, which each
// copy "shifted" to allow selection of 4 subsets of colors out of every 16
// color palette. Doing this for all tiles would take up the entirety of tile
// memory so we treat the title logo tiles as an exception, as there are a lot
// of them and they are always assigned the same palette. We also need to use
// two maps because we need to address more than 1024 tiles.
void OnPlayfieldWrite(uint16_t address, uint16_t value)
{
	// Convert address to an index
	address &= 0x7ff;
	address >>= 1;

	uint16_t tile = ((value & 0x8000) >> 7) | (value & 0xff);
	uint16_t pal = (value >> 8) & 0x3f;

	// X and Y are transposed
	uint16_t y = address % PLAYFIELD_TILE_W;
	uint16_t x = address / PLAYFIELD_TILE_W;
	// Rotate by one
	x = (x + 1) % PLAYFIELD_TILE_H;
	uint16_t index = (y + BORDER_TILE_H) * MAP_TILE_W + (x + BORDER_TILE_W);

	if (tile < LOGO_START_TILE)
	{
		// Use low 2 bits of palette to select 1 of 4 copies of each tile.
		tile = tile * 4 + (pal & 3);
	}
	else
	{
		// Logo tiles have just one copy each.
		tile = LOGO_START_TILE * 4 + (tile - LOGO_START_TILE);
	}

	// The top 4 bits select the 16-color palette.
	pal = pal >> 2;
	uint16_t mvalue = (pal << 12) | (tile & 0x3ff);

	// Write the map value to the map that can actually address the correct tile
	// and write a clear tile to the opposite map.
	if (tile < MAP_TILE_LIMIT)
	{
		MAP1[index] = mvalue;
		MAP2[index] = MAP2_CLEAR_TILE;
	}
	else
	{
		MAP1[index] = MAP1_CLEAR_TILE;
		MAP2[index] = mvalue;
	}
}

void OnVBlank(void)
{
	UpdatePalette();
	UpdateObjects();
	UpdateInput();
}

int main(void)
{
	// Precalculate a lookup table for speed
	for (int i = 0; i < 256; i++)
	{
		PaletteLut[i] = ColorToB5G5R5(i);
	}

	// Enable tile mode (320x240 pixel resolution)
	REG_SCREENMODE = SMODE_TILE;

	// Initialize video memory
	MISC->DmaClear(TILESET, 0, 0x4000, DMA_INT);
	MISC->DmaClear(OBJECTS_A, 0, 0x1000, DMA_INT);
	MISC->DmaClear(MAP1, MAP1_CLEAR_TILE, 0x4000 / 2, DMA_SHORT);
	MISC->DmaClear(MAP2, MAP2_CLEAR_TILE, 0x4000 / 2, DMA_SHORT);
	MISC->DmaCopy(TILESET + 0x4000, PlayfieldTiles, 0xAC00 / 4, DMA_INT);

	// Enable layers 1 & 2
	REG_MAPSET = 0x30;
	// Shift layer 1 by 1 (512 tiles) and layer 2 by 3 (1536 tiles)
	REG_MAPSHIFT = (3 << 2) | 1;
	// Enable window mask for objects so they don't cross the edge of the active area
	REG_WINMASK = 0x20;
	REG_WINLEFT = BORDER_TILE_W * 8;
	REG_WINRIGHT = (SCREEN_TILE_W - BORDER_TILE_W) * 8;

	// Register our interrupt handlers
	interface->VBlank = VBlankHandler;
	interface->HBlank = HBlankHandler;
	inton();

	// Start the game!
	FFEntry();
}
