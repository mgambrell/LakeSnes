#include "input.h"
#include "snes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{

	void Input::input_init(int pidx)
	{
		// TODO: handle (where?)
		pidx = pidx;
		type = 1;
		currentState = 0;
		// TODO: handle I/O line (and latching of PPU)
	}

	void Input::input_free() {
	}

	void Input::input_reset() {
		latchLine = false;
		latchedState = 0;
	}

	void Input::input_latch(bool value) {
		latchLine = value;
		if(latchLine) latchedState = currentState;
	}

	uint8_t Input::input_read() {
		if(latchLine) latchedState = currentState;
		uint8_t ret = latchedState & 1;
		latchedState >>= 1;
		latchedState |= 0x8000;
		return ret;
	}

}