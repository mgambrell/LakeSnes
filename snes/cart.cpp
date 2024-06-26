#include "cart.h"
#include "snes.h"
#include "cx4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{

	void Cart::cart_init(Snes* snes)
	{
		config.snes = snes;
		config.type = 0;
		config.romSize = 0;
		config.ramSize = 0;
		config.rom = NULL;
		ram = NULL;
	}

	void Cart::cart_free() {
		if(config.rom != NULL) free(config.rom);
		if(ram != NULL) free(ram);
	}

	void Cart::cart_reset() {
		// do not reset ram, assumed to be battery backed
		switch (config.type) {
			case 0x04:
				cx4_init(config.snes);
				cx4_reset();
				break;
		}
	}

	void Cart::cart_load(int type, uint8_t* rom, int romSize, int ramSize) {
		config.type = type;
		if(config.rom != NULL) free(config.rom);
		if(ram != NULL) free(ram);

		config.romSize = romSize;
		config.ramSize = ramSize;

		//allocate 8MB of data. this way we can pre-mirror the data, which therefore doesn't need to be done at runtime
		//we've applied the mirroring in a sloppy way, but it's simple at least
		//I'm not sure what's to be done for e.g. 96 megabit romhacks..
		config.rom = (uint8_t*)malloc(8*1024*1024);
		int dst = 0;
		while(dst < 8*1024*1024)
		{
			int sz = std::min(romSize,8*1024*1024-dst);
			memcpy(config.rom+dst,rom,sz);
			dst += sz;
		}

		if(ramSize > 0) {
			ram = (uint8_t*)malloc(ramSize);
			memset(ram, 0, ramSize);
		} else {
			ram = NULL;
		}
	}

	bool Cart::cart_handleBattery(bool save, uint8_t* data, int* size) {
		if(save) {
			*size = config.ramSize;
			if(data == NULL) return true;
			// assumes data is correct size
			if(ram != NULL) memcpy(data, ram, config.ramSize);
			return true;
		} else {
			if(*size != config.ramSize) return false;
			if(ram != NULL) memcpy(ram, data, config.ramSize);
			return true;
		}
	}

	uint8_t Cart::cart_read(uint8_t bank, uint16_t adr) {
		switch(config.type) {
			case 0: return config.snes->OpenBusRef();
			case 1: return cart_readLorom(bank, adr);
			case 2: return cart_readHirom(bank, adr);
			case 3: return cart_readExHirom(bank, adr);
			case 4: return cart_readCX4(bank, adr);
		}
		return config.snes->OpenBusRef();
	}

	void Cart::cart_write(uint8_t bank, uint16_t adr, uint8_t val) {
		switch(config.type) {
			case 0: break;
			case 1: cart_writeLorom(bank, adr, val); break;
			case 2: cart_writeHirom(bank, adr, val); break;
			case 3: cart_writeHirom(bank, adr, val); break;
			case 4: cart_writeCX4(bank, adr, val); break;
		}
	}

	void Cart::cart_writeLoromByteNew(bool RIGHT, Addr24 addr, uint8_t value)
	{
		auto a = addr.addr();
		auto b = addr.bank();

		if(!RIGHT)
		{
			if((b&0x70)==0x70)
				ram[(((b) << 15) | a) & (config.ramSize - 1)] = value;
		}
	}

	uint8_t Cart::cart_readLoromByteNew(bool RIGHT, Addr24 addr)
	{
		auto a = addr.addr();
		auto b = addr.bank();
		
		//TODO: sram check (assumed for now)
		//this fun logic can do the 0x70 check in a sneaky way..
		//if((~(b&0x70))&0x70)
		//but I think a comparison will be quicker

		if(RIGHT)
		{
			if(a&0x8000)
				return config.rom[(((b&0x7F) << 15) | (a & 0x7fff))];
			else
				return addr.openBus();
		}
		else
		{
			if(a&0x8000)
				return config.rom[((b << 15) | (a & 0x7fff))];
			if((b&0x70)==0x70)
				return ram[(((b) << 15) | a) & (config.ramSize - 1)];
			else
				return addr.openBus();
		}
	}

	uint8_t Cart::cart_readLorom(uint8_t bank, uint16_t adr) {
		if(((bank >= 0x70 && bank < 0x7e) || bank >= 0xf0) && adr < 0x8000 && config.ramSize > 0) {
			// banks 70-7e and f0-ff, adr 0000-7fff
			return ram[(((bank & 0xf) << 15) | adr) & (config.ramSize - 1)];
		}
		bank &= 0x7f;
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return config.rom[((bank << 15) | (adr & 0x7fff)) & (config.romSize - 1)];
		}
		return config.snes->OpenBusRef();
	}

	void Cart::cart_writeLorom(uint8_t bank, uint16_t adr, uint8_t val) {
		if(((bank >= 0x70 && bank < 0x7e) || bank > 0xf0) && adr < 0x8000 && config.ramSize > 0) {
			// banks 70-7e and f0-ff, adr 0000-7fff
			ram[(((bank & 0xf) << 15) | adr) & (config.ramSize - 1)] = val;
		}
	}

	uint8_t Cart::cart_readCX4(uint8_t bank, uint16_t adr) {
		// cx4 mapper
		if((bank & 0x7f) < 0x40 && adr >= 0x6000 && adr < 0x8000) {
			// banks 00-3f and 80-bf, adr 6000-7fff
		return cx4_read(adr);
		}
		// save ram
		if(((bank >= 0x70 && bank < 0x7e) || bank >= 0xf0) && adr < 0x8000 && config.ramSize > 0) {
			// banks 70-7d and f0-ff, adr 0000-7fff
			return ram[(((bank & 0xf) << 15) | adr) & (config.ramSize - 1)];
		}
		bank &= 0x7f;
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return config.rom[((bank << 15) | (adr & 0x7fff)) & (config.romSize - 1)];
		}
		return config.snes->OpenBusRef();
	}

	void Cart::cart_writeCX4(uint8_t bank, uint16_t adr, uint8_t val) {
		// cx4 mapper
		if((bank & 0x7f) < 0x40 && adr >= 0x6000 && adr < 0x8000) {
			// banks 00-3f and 80-bf, adr 6000-7fff
		cx4_write(adr, val);
		}
		// save ram
		if(((bank >= 0x70 && bank < 0x7e) || bank > 0xf0) && adr < 0x8000 && config.ramSize > 0) {
			// banks 70-7d and f0-ff, adr 0000-7fff
			ram[(((bank & 0xf) << 15) | adr) & (config.ramSize - 1)] = val;
		}
	}

	uint8_t Cart::cart_readHirom(uint8_t bank, uint16_t adr) {
		bank &= 0x7f;
		if(bank < 0x40 && adr >= 0x6000 && adr < 0x8000 && config.ramSize > 0) {
			// banks 00-3f and 80-bf, adr 6000-7fff
			return ram[(((bank & 0x3f) << 13) | (adr & 0x1fff)) & (config.ramSize - 1)];
		}
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return config.rom[(((bank & 0x3f) << 16) | adr) & (config.romSize - 1)];
		}
		return config.snes->OpenBusRef();
	}

	uint8_t Cart::cart_readExHirom(uint8_t bank, uint16_t adr) {
		if((bank & 0x7f) < 0x40 && adr >= 0x6000 && adr < 0x8000 && config.ramSize > 0) {
			// banks 00-3f and 80-bf, adr 6000-7fff
			return ram[(((bank & 0x3f) << 13) | (adr & 0x1fff)) & (config.ramSize - 1)];
		}
		bool secondHalf = bank < 0x80;
		bank &= 0x7f;
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return config.rom[(((bank & 0x3f) << 16) | (secondHalf ? 0x400000 : 0) | adr) & (config.romSize - 1)];
		}
		return config.snes->OpenBusRef();
	}

	void Cart::cart_writeHirom(uint8_t bank, uint16_t adr, uint8_t val) {
		bank &= 0x7f;
		if(bank < 0x40 && adr >= 0x6000 && adr < 0x8000 && config.ramSize > 0) {
			// banks 00-3f and 80-bf, adr 6000-7fff
			ram[(((bank & 0x3f) << 13) | (adr & 0x1fff)) & (config.ramSize - 1)] = val;
		}
	}

}