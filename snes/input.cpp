#include "input.h"
#include "snes.h"
#include "statehandler.h"
#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{

	void input_init(int pidx)
	{
		// TODO: handle (where?)
		input[pidx].pidx = pidx;
		input[pidx].type = 1;
		input[pidx].currentState = 0;
		// TODO: handle I/O line (and latching of PPU)
	}

	void input_free(int pidx) {
	}

	void input_reset(int pidx) {
		input[pidx].latchLine = false;
		input[pidx].latchedState = 0;
	}

	void input_handleState(int pidx, StateHandler* sh) {
		// TODO: handle types (switch type on state load?)
		sh_handleBytes(sh, &input[pidx].type, NULL);
		sh_handleBools(sh, &input[pidx].latchLine, NULL);
		sh_handleWords(sh, &input[pidx].currentState, &input[pidx].latchedState, NULL);
	}

	void input_latch(int pidx, bool value) {
		input[pidx].latchLine = value;
		if(input[pidx].latchLine) input[pidx].latchedState = input[pidx].currentState;
	}

	uint8_t input_read(int pidx) {
		if(input[pidx].latchLine) input[pidx].latchedState = input[pidx].currentState;
		uint8_t ret = input[pidx].latchedState & 1;
		input[pidx].latchedState >>= 1;
		input[pidx].latchedState |= 0x8000;
		return ret;
	}

}