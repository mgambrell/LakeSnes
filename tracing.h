#pragma once


namespace LakeSnes
{
	class Snes;
}

void getProcessorStateCpu(LakeSnes::Snes* snes, char* line);
void getProcessorStateSpc(LakeSnes::Snes* snes, char* line);

