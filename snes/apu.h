#pragma once

#include <stdint.h>

#include "spc.h"
#include "dsp.h"

namespace LakeSnes
{
	class Snes;
	struct StateHandler;

	struct Timer {
		uint8_t cycles;
		uint8_t divider;
		uint8_t target;
		uint8_t counter;
		bool enabled;
	};

	class Apu
	{
	public:

		void apu_init(Snes* snes);
		void apu_free();
		void apu_reset();
		void apu_handleState(StateHandler* sh);
		void apu_runCycles();
		uint8_t apu_spcRead(uint16_t adr);
		void apu_spcWrite(uint16_t adr, uint8_t val);
		void apu_spcIdle(bool waiting);

	private:
		void apu_cycle();
		uint8_t apu_read(uint16_t adr);
		void apu_write( uint16_t adr, uint8_t val);

	public:
		struct {
			Snes* snes;
		} config;
		Spc myspc;
		Dsp mydsp;
		uint8_t ram[0x10000];
		bool romReadable;
		uint8_t dspAdr;
		uint64_t cycles;
		uint8_t inPorts[6]; // includes 2 bytes of ram
		uint8_t outPorts[4];
		Timer timer[3];
	};



}