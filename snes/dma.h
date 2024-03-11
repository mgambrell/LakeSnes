#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct Snes;
	struct StateHandler;

	struct DmaChannel {
		uint8_t bAdr;
		uint16_t aAdr;
		uint8_t aBank;
		uint16_t size; // also indirect hdma adr
		uint8_t indBank; // hdma
		uint16_t tableAdr; // hdma
		uint8_t repCount; // hdma
		uint8_t unusedByte;
		bool dmaActive;
		bool hdmaActive;
		uint8_t mode;
		bool fixed;
		bool decrement;
		bool indirect; // hdma
		bool fromB;
		bool unusedBit;
		bool doTransfer; // hdma
		bool terminated; // hdma
	};

	struct Dma {
		DmaChannel channel[8];
		uint8_t dmaState;
		bool hdmaInitRequested;
		bool hdmaRunRequested;
	};

	void dma_init(Snes* snes);
	void dma_reset();
	void dma_handleState(StateHandler* sh);
	uint8_t dma_read(uint16_t adr); // 43x0-43xf
	void dma_write(uint16_t adr, uint8_t val); // 43x0-43xf
	void dma_handleDma(int cpuCycles);
	void dma_startDma(uint8_t val, bool hdma);

}

