#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct StateHandler;

	struct Input
	{
		uint8_t pidx;
		uint8_t type;
		// latchline
		bool latchLine;
		// for controller
		uint16_t currentState; // actual state
		uint16_t latchedState;
	};

	void input_init(int pidx);
	void input_free(int pidx);
	void input_reset(int pidx);
	void input_handleState(int pidx, StateHandler* sh);
	void input_latch(int pidx, bool value);
	uint8_t input_read(int pidx);

}
