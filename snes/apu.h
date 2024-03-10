#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct Apu;
	struct Snes;
	struct Spc;
	struct Dsp;
	struct StateHandler;

	struct Timer {
		uint8_t cycles;
		uint8_t divider;
		uint8_t target;
		uint8_t counter;
		bool enabled;
	};

	struct Apu {
		Snes* snes;
		Spc* spc;
		Dsp* dsp;
		uint8_t ram[0x10000];
		bool romReadable;
		uint8_t dspAdr;
		uint64_t cycles;
		uint8_t inPorts[6]; // includes 2 bytes of ram
		uint8_t outPorts[4];
		Timer timer[3];
	};


	Apu* apu_init(Snes* snes);
	void apu_free(Apu* apu);
	void apu_reset(Apu* apu);
	void apu_handleState(Apu* apu, StateHandler* sh);
	void apu_runCycles(Apu* apu);
	uint8_t apu_spcRead(void* mem, uint16_t adr);
	void apu_spcWrite(void* mem, uint16_t adr, uint8_t val);
	void apu_spcIdle(void* mem, bool waiting);

}