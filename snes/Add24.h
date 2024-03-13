#pragma once

#include <stdint.h>

namespace LakeSnes
{
	//This collects all the information needed for a quick memory map probe, in a 64bit register
	struct Addr24
	{
	public:

		//The bank that's being represented
		uint8_t bank() const { return _bank; }

		//The 16-bit address that's being represented
		uint16_t addr() const { return _addr; }

		//For diagnostic purposes, you can evaluate the address that's being basically represented
		uint32_t evalLong() const { return (_bank<<16)+_addr; }

		//The current open bus value.
		//This is stored here because we often need to return it as the result of a probe
		uint8_t openBus() const { return _fast; }

		//Cycles used for fast accesses
		int FastCycles() const { return _fast; }

		//Sets the fast memory flag
		void SetFastMemory(bool en) { _fast = en ? 6 : 8; }

		uint16_t _addr = 0;
		uint8_t _bank = 0;
		uint8_t _openBus = 0;
		uint8_t _fast = 0;
		uint8_t _dummy[3];
	};


}