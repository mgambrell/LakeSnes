#pragma once

#include <stdint.h>
#include <array>

#include "cpu.h"
#include "dma.h"
#include "apu.h"
#include "ppu.h"
#include "cart.h"
#include "input.h"
#include "Add24.h"

namespace LakeSnes
{
	struct StateHandler;

	class Snes
	{
	public:
		Snes* snes_init();
		void snes_free();
		void snes_reset(bool hard);
		void snes_handleState(StateHandler* sh);
		void snes_runFrame();
		// used by dma, cpu
		void snes_runCycles(int cycles);
		void snes_syncCycles(bool start, int syncCycles);
		uint8_t snes_readBBus(uint8_t adr);
		void snes_writeBBus(uint8_t adr, uint8_t val);
		uint8_t snes_read(uint32_t adr);
		void snes_write(uint32_t adr, uint8_t val);
		void snes_cpuIdle(bool waiting);
		//void snes_cpuWrite(uint32_t adr, uint8_t val);

		uint8_t snes_cpuRead(Addr24 addr);

		// debugging
		void snes_runCpuCycle();
		void snes_runSpcCycle();

		uint8_t snes_cpuReadWith(uint32_t adr);

		// snes_other.c functions:

		bool snes_loadRom(const uint8_t* data, int length);

		// playerNumber shall be 1 or 2 (playerNumber 0 is not valid)
		void snes_setButtonState(int playerNumber, int button, bool pressed);

		void snes_setPixels(uint8_t* pixelData);
		void snes_setSamples(int16_t* sampleData, int samplesPerFrame);
		int snes_saveBattery(uint8_t* data);
		bool snes_loadBattery(uint8_t* data, int size);
		int snes_saveState(uint8_t* data);
		bool snes_loadState(uint8_t* data, int size);

		uint8_t snes_rread(uint32_t adr);

	private:
		void snes_runCycle();
		void snes_catchupApu();
		void snes_doAutoJoypad();
		uint8_t snes_readReg(uint16_t adr);
		void snes_writeReg(uint16_t adr, uint8_t val);

	private:
		int snes_getAccessTime(uint32_t adr);
		void build_accesstime(bool init);
		void free_accesstime();

	public:
		Cpu mycpu;
		Apu myapu;
		Ppu myppu;
		Dma mydma;
		Cart mycart;
		bool palTiming;
		// input
		std::array<Input,2> myinput;
		// ram
		uint8_t ram[0x20000];
		uint32_t ramAdr;
		// frame timing
		uint16_t hPos;
		uint16_t vPos;
		uint32_t frames;
		uint64_t cycles;
		uint64_t syncCycle;
		uint32_t nextHoriEvent;
		// cpu handling
		// nmi / irq
		bool hIrqEnabled;
		bool vIrqEnabled;
		bool nmiEnabled;
		uint16_t hTimer;
		uint16_t vTimer;
		bool inNmi;
		bool irqCondition;
		bool inIrq;
		bool inVblank;
		// joypad handling
		uint16_t portAutoRead[4]; // as read by auto-joypad read
		bool autoJoyRead;
		uint16_t autoJoyTimer; // times how long until reading is done
		bool ppuLatch;
		// multiplication/division
		uint8_t multiplyA;
		uint16_t multiplyResult;
		uint16_t divideA;
		uint16_t divideResult;
		// misc
		bool fastMem;
		uint8_t openBus;

	};

}
