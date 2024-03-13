#include "conf.h"
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{
	Snes* Snes::snes_init() {
		mycpu.cpu_init(this);
		mydma.dma_init(this);
		myapu.apu_init(this);
		myppu.ppu_init(this);
		mycart.cart_init(this);
		myinput[0].input_init(0);
		myinput[1].input_init(1);
		palTiming = false;
		return this;
	}

	void Snes::snes_free() {
		myppu.ppu_free();
		mycart.cart_free();
		myinput[0].input_free();
		myinput[1].input_free();
	}

	void Snes::snes_reset(bool hard) {
		mycpu.cpu_reset(hard);
		myapu.apu_reset();
		mydma.dma_reset();
		myppu.ppu_reset();
		myinput[0].input_reset();
		myinput[1].input_reset();
		mycart.cart_reset();
		if(hard) memset(ram, 0, sizeof(ram));
		ramAdr = 0;
		hPos = 0;
		vPos = 0;
		frames = 0;
		cycles = 0;
		syncCycle = 0;
		hIrqEnabled = false;
		vIrqEnabled = false;
		nmiEnabled = false;
		hTimer = 0x1ff;
		vTimer = 0x1ff;
		inNmi = false;
		irqCondition = false;
		inIrq = false;
		inVblank = false;
		memset(portAutoRead, 0, sizeof(portAutoRead));
		autoJoyRead = false;
		autoJoyTimer = 0;
		ppuLatch = true;
		multiplyA = 0xff;
		multiplyResult = 0xfe01;
		divideA = 0xffff;
		divideResult = 0x101;
		nextHoriEvent = 16;
	}

	void Snes::snes_handleState(StateHandler* sh) {
		sh_handleBools(sh,
			&palTiming, &hIrqEnabled, &vIrqEnabled, &nmiEnabled, &inNmi, &irqCondition,
			&inIrq, &inVblank, &autoJoyRead, &ppuLatch, NULL
		);
		sh_handleBytes(sh, &multiplyA, NULL);
		sh_handleWords(sh,
			&hPos, &vPos, &hTimer, &vTimer,
			&portAutoRead[0], &portAutoRead[1], &portAutoRead[2], &portAutoRead[3],
			&autoJoyTimer, &multiplyResult, &divideA, &divideResult, NULL
		);
		sh_handleInts(sh, &ramAdr, &frames, &nextHoriEvent, NULL);
		sh_handleLongLongs(sh, &cycles, &syncCycle, NULL);
		sh_handleByteArray(sh, ram, 0x20000);
		// components
		mycpu.cpu_handleState(sh);
		mydma.dma_handleState(sh);
		myppu.ppu_handleState(sh);
		myapu.apu_handleState(sh);
		myinput[0].input_handleState(sh);
		myinput[1].input_handleState(sh);
		mycart.cart_handleState(sh);
	}

	void Snes::snes_runFrame() {
		while(inVblank) {
			mycpu.cpu_runOpcode();
		}
		uint32_t frame = frames;
		while(!inVblank && frame == frames) {
			mycpu.cpu_runOpcode();
		}
	}

	void Snes::snes_runCycles(int nCycles) {
		if(hPos + nCycles >= 536 && hPos < 536) {
			// if we go past 536, add 40 cycles for dram refersh
			nCycles += 40;
		}
		for(int i = 0; i < nCycles; i += 2) {
			snes_runCycle();
		}
	}

	void Snes::snes_syncCycles(bool start, int syncCycles) {
		if(start) {
			syncCycle = cycles;
			int count = syncCycles - (cycles % syncCycles);
			snes_runCycles(count);
		} else {
			int count = syncCycles - ((cycles - syncCycle) % syncCycles);
			snes_runCycles(count);
		}
	}

	void Snes::snes_runCycle() {
		cycles += 2;
		// increment position
		hPos += 2;
		// check for h/v timer irq's
		bool condition = (
			(vIrqEnabled || hIrqEnabled) &&
			(vPos == vTimer || !vIrqEnabled) &&
			(hPos == hTimer * 4 || !hIrqEnabled)
		);
		if(!irqCondition && condition) {
			inIrq = true;
			mycpu.cpu_setIrq(true);
		}
		irqCondition = condition;
		// handle positional stuff
		if (hPos == nextHoriEvent) {
			switch (hPos) {
				case 16: {
					nextHoriEvent = 512;
					if(vPos == 0) mydma.hdmaInitRequested = true;
				} break;
				case 512: {
					nextHoriEvent = 1104;
					// render the line halfway of the screen for better compatibility
					if(!inVblank && vPos > 0) myppu.ppu_runLine(vPos);
				} break;
				case 1104: {
					if(!inVblank) mydma.hdmaRunRequested = true;
					if(!palTiming) {
						// line 240 of odd frame with no interlace is 4 cycles shorter
						// if((hPos == 1360 && vPos == 240 && !ppu_evenFrame() && !ppu_frameInterlace()) || hPos == 1364) {
						nextHoriEvent = (vPos == 240 && !myppu.evenFrame && !myppu.frameInterlace) ? 1360 : 1364;
					} else {
						// line 311 of odd frame with interlace is 4 cycles longer
						// if((hPos == 1364 && (vPos != 311 || ppu_evenFrame() || !ppu_frameInterlace())) || hPos == 1368)
						nextHoriEvent = (vPos != 311 || myppu.evenFrame || !myppu.frameInterlace) ? 1364 : 1368;
					}
				} break;
				case 1360:
				case 1364:
				case 1368: { // this is the end (of the h-line)
					nextHoriEvent = 16;

					hPos = 0;
					vPos++;
					if(!palTiming) {
						// even interlace frame is 263 lines
						if((vPos == 262 && (!myppu.frameInterlace || !myppu.evenFrame)) || vPos == 263) {
							if (mycart.config.type == 4) cx4_run();
							vPos = 0;
							frames++;
						}
				} else {
						// even interlace frame is 313 lines
						if((vPos == 312 && (!myppu.frameInterlace || !myppu.evenFrame)) || vPos == 313) {
							if (mycart.config.type == 4) cx4_run();
							vPos = 0;
							frames++;
						}
					}

					// end of hblank, do most vPos-tests
					bool startingVblank = false;
					if(vPos == 0) {
						// end of vblank
						inVblank = false;
						inNmi = false;
						myppu.ppu_handleFrameStart();
					} else if(vPos == 225) {
						// ask the ppu if we start vblank now or at vPos 240 (overscan)
						startingVblank = !myppu.ppu_checkOverscan();
					} else if(vPos == 240){
						// if we are not yet in vblank, we had an overscan frame, set startingVblank
						if(!inVblank) startingVblank = true;
					}
					if(startingVblank) {
						// catch up the apu at end of emulated frame (we end frame @ start of vblank)
						snes_catchupApu();
						// notify dsp of frame-end, because sometimes dma will extend much further past vblank (or even into the next frame)
						// Megaman X2 (titlescreen animation), Tales of Phantasia (game demo), Actraiser 2 (fade-in @ bootup)
						myapu.mydsp.dsp_newFrame();
						// we are starting vblank
						myppu.ppu_handleVblank();
						inVblank = true;
						inNmi = true;
						if(autoJoyRead) {
							// TODO: this starts a little after start of vblank
							autoJoyTimer = 4224;
							snes_doAutoJoypad();
						}
						if(nmiEnabled) {
							mycpu.cpu_nmi();
						}
					}
				} break;
			}
		}
		// handle autoJoyRead-timer
		if(autoJoyTimer > 0) autoJoyTimer -= 2;
	}

	void Snes::snes_catchupApu() {
		myapu.apu_runCycles();
	}

	void Snes::snes_doAutoJoypad() {
		memset(portAutoRead, 0, sizeof(portAutoRead));
		// latch controllers
		myinput[0].input_latch(true);
		myinput[1].input_latch(true);
		myinput[0].input_latch(false);
		myinput[1].input_latch(false);
		for(int i = 0; i < 16; i++) {
			uint8_t val = myinput[0].input_read();
			portAutoRead[0] |= ((val & 1) << (15 - i));
			portAutoRead[2] |= (((val >> 1) & 1) << (15 - i));
			val = myinput[1].input_read();
			portAutoRead[1] |= ((val & 1) << (15 - i));
			portAutoRead[3] |= (((val >> 1) & 1) << (15 - i));
		}
	}

	uint8_t Snes::snes_readBBus(uint8_t adr) {
		if(adr < 0x40) {
			return myppu.ppu_read(adr);
		}
		if(adr < 0x80) {
			snes_catchupApu(); // catch up the apu before reading
			return myapu.outPorts[adr & 0x3];
		}
		if(adr == 0x80) {
			uint8_t ret = ram[ramAdr++];
			ramAdr &= 0x1ffff;
			return ret;
		}
		return OpenBusRef();
	}

	void Snes::snes_writeBBus(uint8_t adr, uint8_t val) {
		if(adr < 0x40) {
			myppu.ppu_write(adr, val);
			return;
		}
		if(adr < 0x80) {
			snes_catchupApu(); // catch up the apu before writing
			myapu.inPorts[adr & 0x3] = val;
			return;
		}
		switch(adr) {
			case 0x80: {
				ram[ramAdr++] = val;
				ramAdr &= 0x1ffff;
				break;
			}
			case 0x81: {
				ramAdr = (ramAdr & 0x1ff00) | val;
				break;
			}
			case 0x82: {
				ramAdr = (ramAdr & 0x100ff) | (val << 8);
				break;
			}
			case 0x83: {
				ramAdr = (ramAdr & 0x0ffff) | ((val & 1) << 16);
				break;
			}
		}
	}

	uint8_t Snes::snes_readReg(uint16_t adr) {
		switch(adr) {
			case 0x4210: {
				uint8_t val = 0x2; // CPU version (4 bit)
				val |= inNmi << 7;
				inNmi = false;
				return val | (OpenBusRef() & 0x70);
			}
			case 0x4211: {
				uint8_t val = inIrq << 7;
				inIrq = false;
				mycpu.cpu_setIrq(false);
				return val | (OpenBusRef() & 0x7f);
			}
			case 0x4212: {
				uint8_t val = (autoJoyTimer > 0);
				val |= (hPos < 4 || hPos >= 1096) << 6;
				val |= inVblank << 7;
				return val | (OpenBusRef() & 0x3e);
			}
			case 0x4213: {
				return ppuLatch << 7; // IO-port
			}
			case 0x4214: {
				return divideResult & 0xff;
			}
			case 0x4215: {
				return divideResult >> 8;
			}
			case 0x4216: {
				return multiplyResult & 0xff;
			}
			case 0x4217: {
				return multiplyResult >> 8;
			}
			case 0x4218:
			case 0x421a:
			case 0x421c:
			case 0x421e: {
				return portAutoRead[(adr - 0x4218) / 2] & 0xff;
			}
			case 0x4219:
			case 0x421b:
			case 0x421d:
			case 0x421f: {
				return portAutoRead[(adr - 0x4219) / 2] >> 8;
			}
			default: {
				return OpenBusRef();
			}
		}
	}

	void Snes::snes_writeReg(uint16_t adr, uint8_t val) {
		switch(adr) {
			case 0x4200: {
				autoJoyRead = val & 0x1;
				if(!autoJoyRead) autoJoyTimer = 0;
				hIrqEnabled = val & 0x10;
				vIrqEnabled = val & 0x20;
				if(!hIrqEnabled && !vIrqEnabled) {
					inIrq = false;
					mycpu.cpu_setIrq(false);
				}
				// if nmi is enabled while inNmi is still set, immediately generate nmi
				if(!nmiEnabled && (val & 0x80) && inNmi) {
					mycpu.cpu_nmi();
				}
				nmiEnabled = val & 0x80;
			mycpu.intDelay = true; // nmi/irq is delayed by 1 opcode (TODINK: check if this conflicts with above nmi..)
			break;
			}
			case 0x4201: {
				if(!(val & 0x80) && ppuLatch) {
					// latch the ppu h/v registers
					myppu.ppu_latchHV();
				}
				ppuLatch = val & 0x80;
				break;
			}
			case 0x4202: {
				multiplyA = val;
				break;
			}
			case 0x4203: {
				multiplyResult = multiplyA * val;
				break;
			}
			case 0x4204: {
				divideA = (divideA & 0xff00) | val;
				break;
			}
			case 0x4205: {
				divideA = (divideA & 0x00ff) | (val << 8);
				break;
			}
			case 0x4206: {
				if(val == 0) {
					divideResult = 0xffff;
					multiplyResult = divideA;
				} else {
					divideResult = divideA / val;
					multiplyResult = divideA % val;
				}
				break;
			}
			case 0x4207: {
				hTimer = (hTimer & 0x100) | val;
				break;
			}
			case 0x4208: {
				hTimer = (hTimer & 0x0ff) | ((val & 1) << 8);
				break;
			}
			case 0x4209: {
				vTimer = (vTimer & 0x100) | val;
				break;
			}
			case 0x420a: {
				vTimer = (vTimer & 0x0ff) | ((val & 1) << 8);
				break;
			}
			case 0x420b: {
					mydma.dma_startDma(val, false);
				break;
			}
			case 0x420c: {
					mydma.dma_startDma(val, true);
				break;
			}
			case 0x420d: {
				mycpu._currAddr24.SetFastMemory(!!(val & 0x1));
				break;
			}
			default: {
				break;
			}
		}
	}

	uint8_t Snes::snes_readIO(uint16_t adr) {
		if(adr >= 0x2100 && adr < 0x2200) {
			return snes_readBBus(adr & 0xff); // B-bus
		}
		if(adr == 0x4016) {
			return myinput[0].input_read() | (OpenBusRef() & 0xfc);
		}
		if(adr == 0x4017) {
			return myinput[1].input_read() | (OpenBusRef() & 0xe0) | 0x1c;
		}
		if(adr >= 0x4200 && adr < 0x4220) {
			return snes_readReg(adr); // internal registers
		}
		if(adr >= 0x4300 && adr < 0x4380) {
			return mydma.dma_read(adr); // dma registers
		}
		LAKESNES_UNREACHABLE;
		return 0;
	}

	void Snes::snes_writeIO(uint16_t adr, uint8_t val)
	{
		//matt's reminder to deal with open bus
		//openBus = val;
		if(adr >= 0x2100 && adr < 0x2200) {
			snes_writeBBus(adr & 0xff, val); // B-bus
		}
		if(adr == 0x4016) {
			myinput[0].input_latch(val & 1); // input latch
			myinput[1].input_latch(val & 1);
		}
		if(adr >= 0x4200 && adr < 0x4220) {
			snes_writeReg(adr, val); // internal registers
		}
		if(adr >= 0x4300 && adr < 0x4380) {
			mydma.dma_write(adr, val); // dma registers
		}
	}

	// debugging

	void Snes::snes_runCpuCycle() {
		mycpu.cpu_runOpcode();
	}

	void Snes::snes_runSpcCycle() {
		// TODO: apu catchup is not aware of this, SPC runs extra cycle(s)
		myapu.myspc.spc_runOpcode();
	}

 }