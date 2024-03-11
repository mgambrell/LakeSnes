#pragma once

#include <stdint.h>

namespace LakeSnes
{
	class Snes;
	struct StateHandler;

	void cx4_init(Snes *snes);
	uint8_t cx4_read(uint32_t addr);
	void cx4_write(uint32_t addr, uint8_t value);
	void cx4_run();
	void cx4_reset();
	void cx4_handleState(StateHandler* sh);

}
