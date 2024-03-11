#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct StateHandler;

	class Input
	{
	public:

		void input_init(int pidx);
		void input_free();
		void input_reset();
		void input_handleState(StateHandler* sh);
		void input_latch(bool value);
		uint8_t input_read();

	public:
		struct
		{
			uint8_t pidx;
		} config;
		uint8_t type;
		// latchline
		bool latchLine;
		// for controller
		uint16_t currentState; // actual state
		uint16_t latchedState;
	};



}
