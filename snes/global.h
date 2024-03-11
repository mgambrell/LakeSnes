#pragma once

#include "snes.h"

namespace LakeSnes
{
	extern Snes g_snes;
}

#define snes (&LakeSnes::g_snes)
#define cpu (&snes->mycpu)
#define dma (&snes->mydma)
#define apu (&snes->myapu)