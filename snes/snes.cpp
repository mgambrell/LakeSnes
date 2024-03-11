#include "snes.h"
#include "cpu.h"
#include "apu.h"
#include "spc.h"
#include "dsp.h"
#include "dma.h"
#include "ppu.h"
#include "cart.h"
#include "cx4.h"
#include "input.h"
#include "statehandler.h"
#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{
	Snes g_snes;

	static void snes_runCycle();
	static void snes_catchupApu();
	static void snes_doAutoJoypad();
	static uint8_t snes_readReg(uint16_t adr);
	static void snes_writeReg(uint16_t adr, uint8_t val);
	static uint8_t snes_rread(uint32_t adr); // wrapped by read, to set open bus
	static int snes_getAccessTime(uint32_t adr);
	static void build_accesstime(bool init);
	static void free_accesstime();

	static uint8_t *access_time;

	Snes* snes_init() {
		cpu_init();
		dma_init();
		apu_init();
		ppu_init();
		cart_init();
		input_init(0);
		input_init(1);
		snes->palTiming = false;
		build_accesstime(true);
		return snes;
	}

	void snes_free() {
		ppu_free();
		cart_free();
		input_free(0);
		input_free(1);
		free_accesstime();
	}

	void snes_reset(bool hard) {
		cpu_reset(hard);
		apu_reset();
		dma_reset();
		ppu_reset();
		input_reset(0);
		input_reset(1);
		cart_reset();
		if(hard) memset(snes->ram, 0, sizeof(snes->ram));
		snes->ramAdr = 0;
		snes->hPos = 0;
		snes->vPos = 0;
		snes->frames = 0;
		snes->cycles = 0;
		snes->syncCycle = 0;
		snes->hIrqEnabled = false;
		snes->vIrqEnabled = false;
		snes->nmiEnabled = false;
		snes->hTimer = 0x1ff;
		snes->vTimer = 0x1ff;
		snes->inNmi = false;
		snes->irqCondition = false;
		snes->inIrq = false;
		snes->inVblank = false;
		memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
		snes->autoJoyRead = false;
		snes->autoJoyTimer = 0;
		snes->ppuLatch = true;
		snes->multiplyA = 0xff;
		snes->multiplyResult = 0xfe01;
		snes->divideA = 0xffff;
		snes->divideResult = 0x101;
		snes->fastMem = false;
		snes->openBus = 0;
		snes->nextHoriEvent = 16;
	}

	void snes_handleState(StateHandler* sh) {
		sh_handleBools(sh,
			&snes->palTiming, &snes->hIrqEnabled, &snes->vIrqEnabled, &snes->nmiEnabled, &snes->inNmi, &snes->irqCondition,
			&snes->inIrq, &snes->inVblank, &snes->autoJoyRead, &snes->ppuLatch, &snes->fastMem, NULL
		);
		sh_handleBytes(sh, &snes->multiplyA, &snes->openBus, NULL);
		sh_handleWords(sh,
			&snes->hPos, &snes->vPos, &snes->hTimer, &snes->vTimer,
			&snes->portAutoRead[0], &snes->portAutoRead[1], &snes->portAutoRead[2], &snes->portAutoRead[3],
			&snes->autoJoyTimer, &snes->multiplyResult, &snes->divideA, &snes->divideResult, NULL
		);
		sh_handleInts(sh, &snes->ramAdr, &snes->frames, &snes->nextHoriEvent, NULL);
		sh_handleLongLongs(sh, &snes->cycles, &snes->syncCycle, NULL);
		sh_handleByteArray(sh, snes->ram, 0x20000);
		// components
		cpu_handleState(sh);
		dma_handleState(sh);
		ppu_handleState(sh);
		apu_handleState(sh);
		input_handleState(0, sh);
		input_handleState(1, sh);
		cart_handleState(sh);
	}

	void snes_runFrame() {
		while(snes->inVblank) {
			cpu_runOpcode();
		}
		uint32_t frame = snes->frames;
		while(!snes->inVblank && frame == snes->frames) {
			cpu_runOpcode();
		}
	}

	void snes_runCycles(int cycles) {
		if(snes->hPos + cycles >= 536 && snes->hPos < 536) {
			// if we go past 536, add 40 cycles for dram refersh
			cycles += 40;
		}
		for(int i = 0; i < cycles; i += 2) {
			snes_runCycle();
		}
	}

	void snes_syncCycles(bool start, int syncCycles) {
		if(start) {
			snes->syncCycle = snes->cycles;
			int count = syncCycles - (snes->cycles % syncCycles);
			snes_runCycles(count);
		} else {
			int count = syncCycles - ((snes->cycles - snes->syncCycle) % syncCycles);
			snes_runCycles(count);
		}
	}

	static void snes_runCycle() {
		snes->cycles += 2;
		// increment position
		snes->hPos += 2;
		// check for h/v timer irq's
		bool condition = (
			(snes->vIrqEnabled || snes->hIrqEnabled) &&
			(snes->vPos == snes->vTimer || !snes->vIrqEnabled) &&
			(snes->hPos == snes->hTimer * 4 || !snes->hIrqEnabled)
		);
		if(!snes->irqCondition && condition) {
			snes->inIrq = true;
			cpu_setIrq(true);
		}
		snes->irqCondition = condition;
		// handle positional stuff
		if (snes->hPos == snes->nextHoriEvent) {
			switch (snes->hPos) {
				case 16: {
					snes->nextHoriEvent = 512;
					if(snes->vPos == 0) dma->hdmaInitRequested = true;
				} break;
				case 512: {
					snes->nextHoriEvent = 1104;
					// render the line halfway of the screen for better compatibility
					if(!snes->inVblank && snes->vPos > 0) ppu_runLine(snes->vPos);
				} break;
				case 1104: {
					if(!snes->inVblank) dma->hdmaRunRequested = true;
					if(!snes->palTiming) {
						// line 240 of odd frame with no interlace is 4 cycles shorter
						// if((snes->hPos == 1360 && snes->vPos == 240 && !ppu_evenFrame() && !ppu_frameInterlace()) || snes->hPos == 1364) {
						snes->nextHoriEvent = (snes->vPos == 240 && !ppu->evenFrame && !ppu->frameInterlace) ? 1360 : 1364;
					} else {
						// line 311 of odd frame with interlace is 4 cycles longer
						// if((snes->hPos == 1364 && (snes->vPos != 311 || ppu_evenFrame() || !ppu_frameInterlace())) || snes->hPos == 1368)
						snes->nextHoriEvent = (snes->vPos != 311 || ppu->evenFrame || !ppu->frameInterlace) ? 1364 : 1368;
					}
				} break;
				case 1360:
				case 1364:
				case 1368: { // this is the end (of the h-line)
					snes->nextHoriEvent = 16;

					snes->hPos = 0;
					snes->vPos++;
					if(!snes->palTiming) {
						// even interlace frame is 263 lines
						if((snes->vPos == 262 && (!ppu->frameInterlace || !ppu->evenFrame)) || snes->vPos == 263) {
							if (cart->type == 4) cx4_run();
							snes->vPos = 0;
							snes->frames++;
						}
				} else {
						// even interlace frame is 313 lines
						if((snes->vPos == 312 && (!ppu->frameInterlace || !ppu->evenFrame)) || snes->vPos == 313) {
							if (cart->type == 4) cx4_run();
							snes->vPos = 0;
							snes->frames++;
						}
					}

					// end of hblank, do most vPos-tests
					bool startingVblank = false;
					if(snes->vPos == 0) {
						// end of vblank
						snes->inVblank = false;
						snes->inNmi = false;
						ppu_handleFrameStart();
					} else if(snes->vPos == 225) {
						// ask the ppu if we start vblank now or at vPos 240 (overscan)
						startingVblank = !ppu_checkOverscan();
					} else if(snes->vPos == 240){
						// if we are not yet in vblank, we had an overscan frame, set startingVblank
						if(!snes->inVblank) startingVblank = true;
					}
					if(startingVblank) {
						// catch up the apu at end of emulated frame (we end frame @ start of vblank)
						snes_catchupApu();
						// notify dsp of frame-end, because sometimes dma will extend much further past vblank (or even into the next frame)
						// Megaman X2 (titlescreen animation), Tales of Phantasia (game demo), Actraiser 2 (fade-in @ bootup)
				dsp_newFrame();
						// we are starting vblank
						ppu_handleVblank();
						snes->inVblank = true;
						snes->inNmi = true;
						if(snes->autoJoyRead) {
							// TODO: this starts a little after start of vblank
							snes->autoJoyTimer = 4224;
							snes_doAutoJoypad();
						}
						if(snes->nmiEnabled) {
							cpu_nmi();
						}
					}
				} break;
			}
		}
		// handle autoJoyRead-timer
		if(snes->autoJoyTimer > 0) snes->autoJoyTimer -= 2;
	}

	static void snes_catchupApu() {
		apu_runCycles();
	}

	static void snes_doAutoJoypad() {
		memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
		// latch controllers
		input_latch(0, true);
		input_latch(1, true);
		input_latch(0, false);
		input_latch(1, false);
		for(int i = 0; i < 16; i++) {
			uint8_t val = input_read(0);
			snes->portAutoRead[0] |= ((val & 1) << (15 - i));
			snes->portAutoRead[2] |= (((val >> 1) & 1) << (15 - i));
			val = input_read(1);
			snes->portAutoRead[1] |= ((val & 1) << (15 - i));
			snes->portAutoRead[3] |= (((val >> 1) & 1) << (15 - i));
		}
	}

	uint8_t snes_readBBus(uint8_t adr) {
		if(adr < 0x40) {
			return ppu_read(adr);
		}
		if(adr < 0x80) {
			snes_catchupApu(); // catch up the apu before reading
			return snes->myapu.outPorts[adr & 0x3];
		}
		if(adr == 0x80) {
			uint8_t ret = snes->ram[snes->ramAdr++];
			snes->ramAdr &= 0x1ffff;
			return ret;
		}
		return snes->openBus;
	}

	void snes_writeBBus(uint8_t adr, uint8_t val) {
		if(adr < 0x40) {
			ppu_write(adr, val);
			return;
		}
		if(adr < 0x80) {
			snes_catchupApu(); // catch up the apu before writing
			snes->myapu.inPorts[adr & 0x3] = val;
			return;
		}
		switch(adr) {
			case 0x80: {
				snes->ram[snes->ramAdr++] = val;
				snes->ramAdr &= 0x1ffff;
				break;
			}
			case 0x81: {
				snes->ramAdr = (snes->ramAdr & 0x1ff00) | val;
				break;
			}
			case 0x82: {
				snes->ramAdr = (snes->ramAdr & 0x100ff) | (val << 8);
				break;
			}
			case 0x83: {
				snes->ramAdr = (snes->ramAdr & 0x0ffff) | ((val & 1) << 16);
				break;
			}
		}
	}

	static uint8_t snes_readReg(uint16_t adr) {
		switch(adr) {
			case 0x4210: {
				uint8_t val = 0x2; // CPU version (4 bit)
				val |= snes->inNmi << 7;
				snes->inNmi = false;
				return val | (snes->openBus & 0x70);
			}
			case 0x4211: {
				uint8_t val = snes->inIrq << 7;
				snes->inIrq = false;
				cpu_setIrq(false);
				return val | (snes->openBus & 0x7f);
			}
			case 0x4212: {
				uint8_t val = (snes->autoJoyTimer > 0);
				val |= (snes->hPos < 4 || snes->hPos >= 1096) << 6;
				val |= snes->inVblank << 7;
				return val | (snes->openBus & 0x3e);
			}
			case 0x4213: {
				return snes->ppuLatch << 7; // IO-port
			}
			case 0x4214: {
				return snes->divideResult & 0xff;
			}
			case 0x4215: {
				return snes->divideResult >> 8;
			}
			case 0x4216: {
				return snes->multiplyResult & 0xff;
			}
			case 0x4217: {
				return snes->multiplyResult >> 8;
			}
			case 0x4218:
			case 0x421a:
			case 0x421c:
			case 0x421e: {
				return snes->portAutoRead[(adr - 0x4218) / 2] & 0xff;
			}
			case 0x4219:
			case 0x421b:
			case 0x421d:
			case 0x421f: {
				return snes->portAutoRead[(adr - 0x4219) / 2] >> 8;
			}
			default: {
				return snes->openBus;
			}
		}
	}

	static void snes_writeReg(uint16_t adr, uint8_t val) {
		switch(adr) {
			case 0x4200: {
				snes->autoJoyRead = val & 0x1;
				if(!snes->autoJoyRead) snes->autoJoyTimer = 0;
				snes->hIrqEnabled = val & 0x10;
				snes->vIrqEnabled = val & 0x20;
				if(!snes->hIrqEnabled && !snes->vIrqEnabled) {
					snes->inIrq = false;
					cpu_setIrq(false);
				}
				// if nmi is enabled while inNmi is still set, immediately generate nmi
				if(!snes->nmiEnabled && (val & 0x80) && snes->inNmi) {
					cpu_nmi();
				}
				snes->nmiEnabled = val & 0x80;
			snes->mycpu.intDelay = true; // nmi/irq is delayed by 1 opcode (TODINK: check if this conflicts with above nmi..)
			break;
			}
			case 0x4201: {
				if(!(val & 0x80) && snes->ppuLatch) {
					// latch the ppu h/v registers
					ppu_latchHV();
				}
				snes->ppuLatch = val & 0x80;
				break;
			}
			case 0x4202: {
				snes->multiplyA = val;
				break;
			}
			case 0x4203: {
				snes->multiplyResult = snes->multiplyA * val;
				break;
			}
			case 0x4204: {
				snes->divideA = (snes->divideA & 0xff00) | val;
				break;
			}
			case 0x4205: {
				snes->divideA = (snes->divideA & 0x00ff) | (val << 8);
				break;
			}
			case 0x4206: {
				if(val == 0) {
					snes->divideResult = 0xffff;
					snes->multiplyResult = snes->divideA;
				} else {
					snes->divideResult = snes->divideA / val;
					snes->multiplyResult = snes->divideA % val;
				}
				break;
			}
			case 0x4207: {
				snes->hTimer = (snes->hTimer & 0x100) | val;
				break;
			}
			case 0x4208: {
				snes->hTimer = (snes->hTimer & 0x0ff) | ((val & 1) << 8);
				break;
			}
			case 0x4209: {
				snes->vTimer = (snes->vTimer & 0x100) | val;
				break;
			}
			case 0x420a: {
				snes->vTimer = (snes->vTimer & 0x0ff) | ((val & 1) << 8);
				break;
			}
			case 0x420b: {
				dma_startDma(val, false);
				break;
			}
			case 0x420c: {
				dma_startDma(val, true);
				break;
			}
			case 0x420d: {
				if (snes->fastMem != (val & 0x1)) {
					build_accesstime(false);
				}
				snes->fastMem = val & 0x1;
				break;
			}
			default: {
				break;
			}
		}
	}

	static uint8_t snes_rread(uint32_t adr) {
		uint8_t bank = adr >> 16;
		adr &= 0xffff;
		if(bank == 0x7e || bank == 0x7f) {
			return snes->ram[((bank & 1) << 16) | adr]; // ram
		}
		if(bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) {
			if(adr < 0x2000) {
				return snes->ram[adr]; // ram mirror
			}
			if(adr >= 0x2100 && adr < 0x2200) {
				return snes_readBBus(adr & 0xff); // B-bus
			}
			if(adr == 0x4016) {
				return input_read(0) | (snes->openBus & 0xfc);
			}
			if(adr == 0x4017) {
				return input_read(1) | (snes->openBus & 0xe0) | 0x1c;
			}
			if(adr >= 0x4200 && adr < 0x4220) {
				return snes_readReg(adr); // internal registers
			}
			if(adr >= 0x4300 && adr < 0x4380) {
				return dma_read(adr); // dma registers
			}
		}
		// read from cart
		return cart_read(bank, adr);
	}

	void snes_write(uint32_t adr, uint8_t val) {
		snes->openBus = val;
		uint8_t bank = adr >> 16;
		adr &= 0xffff;
		if(bank == 0x7e || bank == 0x7f) {
			snes->ram[((bank & 1) << 16) | adr] = val; // ram
		}
		if(bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) {
			if(adr < 0x2000) {
				snes->ram[adr] = val; // ram mirror
			}
			if(adr >= 0x2100 && adr < 0x2200) {
				snes_writeBBus(adr & 0xff, val); // B-bus
			}
			if(adr == 0x4016) {
				input_latch(0, val & 1); // input latch
				input_latch(1, val & 1);
			}
			if(adr >= 0x4200 && adr < 0x4220) {
				snes_writeReg(adr, val); // internal registers
			}
			if(adr >= 0x4300 && adr < 0x4380) {
				dma_write(adr, val); // dma registers
			}
		}
		// write to cart
		cart_write(bank, adr, val);
	}

	static int snes_getAccessTime(uint32_t adr) {
		uint8_t bank = adr >> 16;
		adr &= 0xffff;
		if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && adr < 0x8000) {
			// 00-3f,80-bf:0-7fff
			if(adr < 0x2000 || adr >= 0x6000) return 8; // 0-1fff, 6000-7fff
			if(adr < 0x4000 || adr >= 0x4200) return 6; // 2000-3fff, 4200-5fff
			return 12; // 4000-41ff
		}
		// 40-7f,co-ff:0000-ffff, 00-3f,80-bf:8000-ffff
		return (snes->fastMem && bank >= 0x80) ? 6 : 8; // depends on setting in banks 80+
	}

	static void build_accesstime(bool init) {
		if (init) {
			access_time = (uint8_t *)malloc(0x1000000);
		}
		for (int i = 0; i < 0x1000000; i++) {
			access_time[i] = snes_getAccessTime(i);
		}
	}

	static void free_accesstime() {
		free(access_time);
	}

	uint8_t snes_read(uint32_t adr) {
		uint8_t val = snes_rread(adr);
		snes->openBus = val;
		return val;
	}

	void snes_cpuIdle(bool waiting) {
		dma_handleDma(6);
		snes_runCycles(6);
	}

	uint8_t snes_cpuRead(uint32_t adr) {
		const int cycles = access_time[adr] - 4;
		dma_handleDma(cycles);
		snes_runCycles(cycles);
		uint8_t rv = snes_read(adr);
		dma_handleDma(4);
		snes_runCycles(4);
		return rv;
	}

	void snes_cpuWrite(uint32_t adr, uint8_t val) {
		const int cycles = access_time[adr];
		dma_handleDma(cycles);
		snes_runCycles(cycles);
		snes_write(adr, val);
	}

	// debugging

	void snes_runCpuCycle() {
		cpu_runOpcode();
	}

	void snes_runSpcCycle() {
		// TODO: apu catchup is not aware of this, SPC runs extra cycle(s)
		spc_runOpcode();
	}

 }