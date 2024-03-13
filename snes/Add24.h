#pragma once

#include <stdint.h>

namespace LakeSnes
{

	struct Addr24
	{
		uint8_t _bank;
		uint8_t _dummy;
		uint16_t _addr;

		uint32_t eval() const { return (_bank<<16)+_addr; }
		uint8_t bank() const { return _bank; }
		uint16_t addr() const { return _addr; }
	};

	inline Addr24 MakeAddr24(uint8_t bank, uint16_t addr)
	{
		return Addr24 { bank, 0, addr };
	}

}