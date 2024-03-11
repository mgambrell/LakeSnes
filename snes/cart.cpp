#include "cart.h"
#include "snes.h"
#include "statehandler.h"
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

	bool Cart::cart_handleTypeState(StateHandler* sh) {
		// when loading, return if values match
		if(sh->saving) {
			sh_handleBytes(sh, &config.type, NULL);
			sh_handleInts(sh, &config.romSize, &config.ramSize, NULL);
			return true;
		} else {
			uint8_t type = 0;
			uint32_t romSize = 0;
			uint32_t ramSize = 0;
			sh_handleBytes(sh, &type, NULL);
			sh_handleInts(sh, &romSize, &ramSize, NULL);
			return !(type != type || romSize != romSize || ramSize != ramSize);
		}
	}

	void Cart::cart_handleState(StateHandler* sh) {
		if(ram != NULL) sh_handleByteArray(sh, ram, config.ramSize);

		switch(config.type) {
			case 4: cx4_handleState(sh); break;
		}
	}

	void Cart::cart_load(int type, uint8_t* rom, int romSize, int ramSize) {
		config.type = type;
		if(config.rom != NULL) free(config.rom);
		if(ram != NULL) free(ram);

		config.romSize = romSize;
		config.ramSize = ramSize;

		config.rom = (uint8_t*)malloc(romSize);
		if(ramSize > 0) {
			ram = (uint8_t*)malloc(ramSize);
			memset(ram, 0, ramSize);
		} else {
			ram = NULL;
		}
		memcpy(config.rom, rom, romSize);
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
			case 0: return config.snes->openBus;
			case 1: return cart_readLorom(bank, adr);
			case 2: return cart_readHirom(bank, adr);
			case 3: return cart_readExHirom(bank, adr);
			case 4: return cart_readCX4(bank, adr);
		}
		return config.snes->openBus;
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
		return config.snes->openBus;
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
		return config.snes->openBus;
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
		return config.snes->openBus;
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
		return config.snes->openBus;
	}

	void Cart::cart_writeHirom(uint8_t bank, uint16_t adr, uint8_t val) {
		bank &= 0x7f;
		if(bank < 0x40 && adr >= 0x6000 && adr < 0x8000 && config.ramSize > 0) {
			// banks 00-3f and 80-bf, adr 6000-7fff
			ram[(((bank & 0x3f) << 13) | (adr & 0x1fff)) & (config.ramSize - 1)] = val;
		}
	}

}