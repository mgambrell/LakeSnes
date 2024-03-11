#pragma once

#include "snes.h"

namespace LakeSnes
{
	extern Snes g_snes;
}

#define snes (&LakeSnes::g_snes)
#define cpu (&snes->mycpu)
#define dma (&snes->mydma)
#define ppu (&snes->myppu)
#define cart (&snes->mycart)
#define input (snes->myinput)
#define apu (&snes->myapu)
#define spc (&apu->myspc)
#define dsp (&apu->mydsp)
