#pragma once

#include <stdint.h>

#include "Add24.h"

namespace LakeSnes
{
	class Snes;
	struct StateHandler;

	class Cart
	{
	public:

		// TODO: how to handle reset & load?

		void cart_init(Snes* snes);
		void cart_free();
		void cart_reset(); // will reset special chips etc, general reading is set up in load
		bool cart_handleTypeState(StateHandler* sh);
		void cart_handleState(StateHandler* sh);
		void cart_load(int type, uint8_t* rom, int romSize, int ramSize); // loads rom, sets up ram buffer
		bool cart_handleBattery(bool save, uint8_t* data, int* size); // saves/loads ram
		uint8_t cart_read(uint8_t bank, uint16_t adr);
		void cart_write(uint8_t bank, uint16_t adr, uint8_t val);

		uint8_t cart_readLoromByteNew(bool RIGHT, Addr24 addr);
		void cart_writeLoromByteNew(bool RIGHT, Addr24 addr, uint8_t value);

	private:

		uint8_t cart_readLorom(uint8_t bank, uint16_t adr);
		void cart_writeLorom(uint8_t bank, uint16_t adr, uint8_t val);
		uint8_t cart_readHirom(uint8_t bank, uint16_t adr);
		uint8_t cart_readExHirom(uint8_t bank, uint16_t adr);
		void cart_writeHirom(uint8_t bank, uint16_t adr, uint8_t val);
		uint8_t cart_readCX4(uint8_t bank, uint16_t adr);
		void cart_writeCX4(uint8_t bank, uint16_t adr, uint8_t val);


	public:
		struct {
			Snes* snes;
			uint8_t* rom;
			uint32_t romSize;
			uint32_t ramSize;
			uint8_t type;
		} config;

		uint8_t* ram;

	};


}