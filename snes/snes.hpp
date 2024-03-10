#ifndef LAKESNES_SNES_HPP
#define LAKESNES_SNES_HPP

//include these first since we know snes.h needs it, to keep it out of the namespace
#include <stdint.h>
#include <stdbool.h>

namespace LakeSnes
{
	extern "C"
	{
		#include "snes.h"
	}
}

#endif