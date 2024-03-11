#include "cart.h"
#include "snes.h"
#include "statehandler.h"
#include "cx4.h"
#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{
	static uint8_t cart_readLorom(uint8_t bank, uint16_t adr);
	static void cart_writeLorom(uint8_t bank, uint16_t adr, uint8_t val);
	static uint8_t cart_readHirom(uint8_t bank, uint16_t adr);
	static uint8_t cart_readExHirom(uint8_t bank, uint16_t adr);
	static void cart_writeHirom(uint8_t bank, uint16_t adr, uint8_t val);
	static uint8_t cart_readCX4(uint8_t bank, uint16_t adr);
	static void cart_writeCX4(uint8_t bank, uint16_t adr, uint8_t val);

	void cart_init() {
		cart->type = 0;
		cart->rom = NULL;
		cart->romSize = 0;
		cart->ram = NULL;
		cart->ramSize = 0;
	}

	void cart_free() {
		if(cart->rom != NULL) free(cart->rom);
		if(cart->ram != NULL) free(cart->ram);
	}

	void cart_reset() {
		// do not reset ram, assumed to be battery backed
		switch (cart->type) {
			case 0x04:
				cx4_init(snes);
				cx4_reset();
				break;
		}
	}

	bool cart_handleTypeState(StateHandler* sh) {
		// when loading, return if values match
		if(sh->saving) {
			sh_handleBytes(sh, &cart->type, NULL);
			sh_handleInts(sh, &cart->romSize, &cart->ramSize, NULL);
			return true;
		} else {
			uint8_t type = 0;
			uint32_t romSize = 0;
			uint32_t ramSize = 0;
			sh_handleBytes(sh, &type, NULL);
			sh_handleInts(sh, &romSize, &ramSize, NULL);
			return !(type != cart->type || romSize != cart->romSize || ramSize != cart->ramSize);
		}
	}

	void cart_handleState(StateHandler* sh) {
		if(cart->ram != NULL) sh_handleByteArray(sh, cart->ram, cart->ramSize);

		switch(cart->type) {
			case 4: cx4_handleState(sh); break;
		}
	}

	void cart_load(int type, uint8_t* rom, int romSize, int ramSize) {
		cart->type = type;
		if(cart->rom != NULL) free(cart->rom);
		if(cart->ram != NULL) free(cart->ram);
		cart->rom = (uint8_t*)malloc(romSize);
		cart->romSize = romSize;
		if(ramSize > 0) {
			cart->ram = (uint8_t*)malloc(ramSize);
			memset(cart->ram, 0, ramSize);
		} else {
			cart->ram = NULL;
		}
		cart->ramSize = ramSize;
		memcpy(cart->rom, rom, romSize);
	}

	bool cart_handleBattery(bool save, uint8_t* data, int* size) {
		if(save) {
			*size = cart->ramSize;
			if(data == NULL) return true;
			// assumes data is correct size
			if(cart->ram != NULL) memcpy(data, cart->ram, cart->ramSize);
			return true;
		} else {
			if(*size != cart->ramSize) return false;
			if(cart->ram != NULL) memcpy(cart->ram, data, cart->ramSize);
			return true;
		}
	}

	uint8_t cart_read(uint8_t bank, uint16_t adr) {
		switch(cart->type) {
			case 0: return snes->openBus;
			case 1: return cart_readLorom(bank, adr);
			case 2: return cart_readHirom(bank, adr);
			case 3: return cart_readExHirom(bank, adr);
			case 4: return cart_readCX4(bank, adr);
		}
		return snes->openBus;
	}

	void cart_write(uint8_t bank, uint16_t adr, uint8_t val) {
		switch(cart->type) {
			case 0: break;
			case 1: cart_writeLorom(bank, adr, val); break;
			case 2: cart_writeHirom(bank, adr, val); break;
			case 3: cart_writeHirom(bank, adr, val); break;
			case 4: cart_writeCX4(bank, adr, val); break;
		}
	}

	static uint8_t cart_readLorom(uint8_t bank, uint16_t adr) {
		if(((bank >= 0x70 && bank < 0x7e) || bank >= 0xf0) && adr < 0x8000 && cart->ramSize > 0) {
			// banks 70-7e and f0-ff, adr 0000-7fff
			return cart->ram[(((bank & 0xf) << 15) | adr) & (cart->ramSize - 1)];
		}
		bank &= 0x7f;
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return cart->rom[((bank << 15) | (adr & 0x7fff)) & (cart->romSize - 1)];
		}
		return snes->openBus;
	}

	static void cart_writeLorom(uint8_t bank, uint16_t adr, uint8_t val) {
		if(((bank >= 0x70 && bank < 0x7e) || bank > 0xf0) && adr < 0x8000 && cart->ramSize > 0) {
			// banks 70-7e and f0-ff, adr 0000-7fff
			cart->ram[(((bank & 0xf) << 15) | adr) & (cart->ramSize - 1)] = val;
		}
	}

	static uint8_t cart_readCX4(uint8_t bank, uint16_t adr) {
		// cx4 mapper
		if((bank & 0x7f) < 0x40 && adr >= 0x6000 && adr < 0x8000) {
			// banks 00-3f and 80-bf, adr 6000-7fff
		return cx4_read(adr);
		}
		// save ram
		if(((bank >= 0x70 && bank < 0x7e) || bank >= 0xf0) && adr < 0x8000 && cart->ramSize > 0) {
			// banks 70-7d and f0-ff, adr 0000-7fff
			return cart->ram[(((bank & 0xf) << 15) | adr) & (cart->ramSize - 1)];
		}
		bank &= 0x7f;
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return cart->rom[((bank << 15) | (adr & 0x7fff)) & (cart->romSize - 1)];
		}
		return snes->openBus;
	}

	static void cart_writeCX4(uint8_t bank, uint16_t adr, uint8_t val) {
		// cx4 mapper
		if((bank & 0x7f) < 0x40 && adr >= 0x6000 && adr < 0x8000) {
			// banks 00-3f and 80-bf, adr 6000-7fff
		cx4_write(adr, val);
		}
		// save ram
		if(((bank >= 0x70 && bank < 0x7e) || bank > 0xf0) && adr < 0x8000 && cart->ramSize > 0) {
			// banks 70-7d and f0-ff, adr 0000-7fff
			cart->ram[(((bank & 0xf) << 15) | adr) & (cart->ramSize - 1)] = val;
		}
	}

	static uint8_t cart_readHirom(uint8_t bank, uint16_t adr) {
		bank &= 0x7f;
		if(bank < 0x40 && adr >= 0x6000 && adr < 0x8000 && cart->ramSize > 0) {
			// banks 00-3f and 80-bf, adr 6000-7fff
			return cart->ram[(((bank & 0x3f) << 13) | (adr & 0x1fff)) & (cart->ramSize - 1)];
		}
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return cart->rom[(((bank & 0x3f) << 16) | adr) & (cart->romSize - 1)];
		}
		return snes->openBus;
	}

	static uint8_t cart_readExHirom(uint8_t bank, uint16_t adr) {
		if((bank & 0x7f) < 0x40 && adr >= 0x6000 && adr < 0x8000 && cart->ramSize > 0) {
			// banks 00-3f and 80-bf, adr 6000-7fff
			return cart->ram[(((bank & 0x3f) << 13) | (adr & 0x1fff)) & (cart->ramSize - 1)];
		}
		bool secondHalf = bank < 0x80;
		bank &= 0x7f;
		if(adr >= 0x8000 || bank >= 0x40) {
			// adr 8000-ffff in all banks or all addresses in banks 40-7f and c0-ff
			return cart->rom[(((bank & 0x3f) << 16) | (secondHalf ? 0x400000 : 0) | adr) & (cart->romSize - 1)];
		}
		return snes->openBus;
	}

	static void cart_writeHirom(uint8_t bank, uint16_t adr, uint8_t val) {
		bank &= 0x7f;
		if(bank < 0x40 && adr >= 0x6000 && adr < 0x8000 && cart->ramSize > 0) {
			// banks 00-3f and 80-bf, adr 6000-7fff
			cart->ram[(((bank & 0x3f) << 13) | (adr & 0x1fff)) & (cart->ramSize - 1)] = val;
		}
	}

}