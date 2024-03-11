#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct Snes;
	struct StateHandler;

	struct Cart {
		Snes* snes;
		uint8_t type;

		uint8_t* rom;
		uint32_t romSize;
		uint8_t* ram;
		uint32_t ramSize;
	};

	// TODO: how to handle reset & load?

	void cart_init();
	void cart_free();
	void cart_reset(); // will reset special chips etc, general reading is set up in load
	bool cart_handleTypeState(StateHandler* sh);
	void cart_handleState(StateHandler* sh);
	void cart_load(int type, uint8_t* rom, int romSize, int ramSize); // loads rom, sets up ram buffer
	bool cart_handleBattery(bool save, uint8_t* data, int* size); // saves/loads ram
	uint8_t cart_read(uint8_t bank, uint16_t adr);
	void cart_write(uint8_t bank, uint16_t adr, uint8_t val);

}