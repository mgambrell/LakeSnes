#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct StateHandler;

	typedef uint8_t (*SpcReadHandler)(void* mem, uint16_t adr);
	typedef void (*SpcWriteHandler)(void* mem, uint16_t adr, uint8_t val);
	typedef void (*SpcIdleHandler)(void* mem, bool waiting);

	struct Spc {
		// reference to memory handler, pointers to read/write/idle handlers
		void* mem;
		SpcReadHandler read;
		SpcWriteHandler write;
		SpcIdleHandler idle;
		// registers
		uint8_t a;
		uint8_t x;
		uint8_t y;
		uint8_t sp;
		uint16_t pc;
		// flags
		bool c;
		bool z;
		bool v;
		bool n;
		bool i;
		bool h;
		bool p;
		bool b;
		// stopping
		bool stopped;
		// reset
		bool resetWanted;
		// single-cycle
		uint8_t opcode;
		uint32_t step;
		uint32_t bstep;
		uint16_t adr;
		uint16_t adr1;
		uint8_t dat;
		uint16_t dat16;
		uint8_t param;
	};

	void spc_init(SpcReadHandler read, SpcWriteHandler write, SpcIdleHandler idle);
	void spc_free();
	void spc_reset(bool hard);
	void spc_handleState(StateHandler* sh);
	void spc_runOpcode();


}