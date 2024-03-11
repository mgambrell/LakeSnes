#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct StateHandler;

	typedef uint8_t (*CpuReadHandler)(uint32_t adr);
	typedef void (*CpuWriteHandler)(uint32_t adr, uint8_t val);
	typedef void (*CpuIdleHandler)(bool waiting);

	struct Cpu
	{
		// reference to memory handler, pointers to read/write/idle handlers
		CpuReadHandler read;
		CpuWriteHandler write;
		CpuIdleHandler idle;
		// registers
		uint16_t a;
		uint16_t x;
		uint16_t y;
		uint16_t sp;
		uint16_t pc;
		uint16_t dp; // direct page (D)
		uint8_t k; // program bank (PB)
		uint8_t db; // data bank (B)
		// flags
		bool c;
		bool z;
		bool v;
		bool n;
		bool i;
		bool d;
		bool xf;
		bool mf;
		bool e;
		// power state (WAI/STP)
		bool waiting;
		bool stopped;
		// interrupts
		bool irqWanted;
		bool nmiWanted;
		bool intWanted;
		bool intDelay;
		bool resetWanted;
	};

	Cpu* cpu_init(CpuReadHandler read, CpuWriteHandler write, CpuIdleHandler idle);
	void cpu_reset(bool hard);
	void cpu_handleState(StateHandler* sh);
	void cpu_runOpcode();
	void cpu_nmi();
	void cpu_setIrq(bool state);

}
