#pragma once

#include <stdint.h>

namespace LakeSnes
{
	class Snes;
	struct StateHandler;

	struct BgLayer {
		uint16_t hScroll;
		uint16_t vScroll;
		bool tilemapWider;
		bool tilemapHigher;
		uint16_t tilemapAdr;
		uint16_t tileAdr;
		bool bigTiles;
		bool mosaicEnabled;
	};

	struct Layer {
		bool mainScreenEnabled;
		bool subScreenEnabled;
		bool mainScreenWindowed;
		bool subScreenWindowed;
	};

	struct WindowLayer {
		bool window1enabled;
		bool window2enabled;
		bool window1inversed;
		bool window2inversed;
		uint8_t maskLogic;
	};

	class Ppu
	{
	public:


		void ppu_init(Snes* snes);
		void ppu_free();
		void ppu_reset();
		void ppu_handleState(StateHandler* sh);
		bool ppu_checkOverscan();
		void ppu_handleVblank();
		void ppu_handleFrameStart();
		void ppu_runLine(int line);
		uint8_t ppu_read(uint8_t adr);
		void ppu_write(uint8_t adr, uint8_t val);
		void ppu_latchHV();
		void ppu_putPixels(uint8_t* pixels);

	private:
		void ppu_handlePixel(int x, int y);
		int ppu_getPixel(int x, int y, bool sub, int* r, int* g, int* b);
		void ppu_handleOPT(int layer, int* lx, int* ly);
		uint16_t ppu_getOffsetValue(int col, int row);
		void ppu_getPixelForBgLayer(int x, int y, int layer);
		void ppu_calculateMode7Starts(int y);
		int ppu_getPixelForMode7(int x, int layer, bool priority);
		bool ppu_getWindowState(int layer, int x);
		void ppu_evaluateSprites(int line);
		uint16_t ppu_getVramRemap();

		public:
			struct {
				Snes* snes;
			} config;

		// vram access
		uint16_t vramPointer;
		bool vramIncrementOnHigh;
		uint16_t vramIncrement;
		uint8_t vramRemapMode;
		uint16_t vramReadBuffer;
		// cgram access
		uint16_t cgram[0x100];
		uint8_t cgramPointer;
		bool cgramSecondWrite;
		uint8_t cgramBuffer;
		// oam access
		uint16_t oam[0x100];
		uint8_t highOam[0x20];
		uint8_t oamAdr;
		uint8_t oamAdrWritten;
		bool oamInHigh;
		bool oamInHighWritten;
		bool oamSecondWrite;
		uint8_t oamBuffer;
		// object/sprites
		bool objPriority;
		uint16_t objTileAdr1;
		uint16_t objTileAdr2;
		uint8_t objSize;
		uint8_t objPixelBuffer[256]; // line buffers
		uint8_t objPriorityBuffer[256];
		bool timeOver;
		bool rangeOver;
		bool objInterlace;
		// background layers
		BgLayer bgLayer[4];
		uint8_t scrollPrev;
		uint8_t scrollPrev2;
		uint8_t mosaicSize;
		uint8_t mosaicStartLine;
		// layers
		Layer layer[5];
		// mode 7
		int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
		uint8_t m7prev;
		bool m7largeField;
		bool m7charFill;
		bool m7xFlip;
		bool m7yFlip;
		bool m7extBg;
		// mode 7 internal
		int32_t m7startX;
		int32_t m7startY;
		// windows
		WindowLayer windowLayer[6];
		uint8_t window1left;
		uint8_t window1right;
		uint8_t window2left;
		uint8_t window2right;
		// color math
		uint8_t clipMode;
		uint8_t preventMathMode;
		bool addSubscreen;
		bool subtractColor;
		bool halfColor;
		bool mathEnabled[6];
		uint8_t fixedColorR;
		uint8_t fixedColorG;
		uint8_t fixedColorB;
		// settings
		bool forcedBlank;
		uint8_t brightness;
		uint8_t mode;
		bool bg3priority;
		bool evenFrame;
		bool pseudoHires;
		bool overscan;
		bool frameOverscan; // if we are overscanning this frame (determined at 0,225)
		bool interlace;
		bool frameInterlace; // if we are interlacing this frame (determined at start vblank)
		bool directColor;
		// latching
		uint16_t hCount;
		uint16_t vCount;
		bool hCountSecond;
		bool vCountSecond;
		bool countersLatched;
		uint8_t ppu1openBus;
		uint8_t ppu2openBus;

		//vram
		uint16_t vram[0x8000];

		// pixel buffer (xbgr)
		// times 2 for even and odd frame
		uint8_t pixelBuffer[512 * 4 * 239 * 2];
	};


}