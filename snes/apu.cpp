#include "apu.h"
#include "snes.h"
#include "spc.h"
#include "dsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{

	static const uint8_t bootRom[0x40] = {
		0xcd, 0xef, 0xbd, 0xe8, 0x00, 0xc6, 0x1d, 0xd0, 0xfc, 0x8f, 0xaa, 0xf4, 0x8f, 0xbb, 0xf5, 0x78,
		0xcc, 0xf4, 0xd0, 0xfb, 0x2f, 0x19, 0xeb, 0xf4, 0xd0, 0xfc, 0x7e, 0xf4, 0xd0, 0x0b, 0xe4, 0xf5,
		0xcb, 0xf4, 0xd7, 0x00, 0xfc, 0xd0, 0xf3, 0xab, 0x01, 0x10, 0xef, 0x7e, 0xf4, 0x10, 0xeb, 0xba,
		0xf6, 0xda, 0x00, 0xba, 0xf4, 0xc4, 0xf4, 0xdd, 0x5d, 0xd0, 0xdb, 0x1f, 0x00, 0x00, 0xc0, 0xff
	};

	static const double apuCyclesPerMaster = (32040 * 32) / (1364 * 262 * 60.0);
	static const double apuCyclesPerMasterPal = (32040 * 32) / (1364 * 312 * 50.0);

	void Apu::apu_cycle() {
		if((cycles & 0x1f) == 0) {
			// every 32 cycles
			mydsp.dsp_cycle();
		}

		// handle timers
		for(int i = 0; i < 3; i++) {
			if(timer[i].cycles == 0) {
				timer[i].cycles = i == 2 ? 16 : 128;
				if(timer[i].enabled) {
					timer[i].divider++;
					if(timer[i].divider == timer[i].target) {
						timer[i].divider = 0;
						timer[i].counter++;
						timer[i].counter &= 0xf;
					}
				}
			}
			timer[i].cycles--;
		}

		cycles++;
	}

	void Apu::apu_init(Snes* snes) {
		config.snes = snes;
		myspc.spc_init(this);
		mydsp.dsp_init(this);
	}

	void Apu::apu_free() {
		myspc.spc_free();
		mydsp.dsp_free();
	}

	void Apu::apu_reset() {
		// TODO: hard reset for apu
		myspc.spc_reset(true);
		mydsp.dsp_reset();
		memset(ram, 0, sizeof(ram));
		dspAdr = 0;
		romReadable = true;
		cycles = 0;
		memset(inPorts, 0, sizeof(inPorts));
		memset(outPorts, 0, sizeof(outPorts));
		for(int i = 0; i < 3; i++) {
			timer[i].cycles = 0;
			timer[i].divider = 0;
			timer[i].target = 0;
			timer[i].counter = 0;
			timer[i].enabled = false;
		}
	}

	void Apu::apu_runCycles() {
		uint64_t sync_to = (uint64_t)config.snes->cycles * (config.snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster);

		while (cycles < sync_to) {
			myspc.spc_runOpcode();
		}
	}

	uint8_t Apu::apu_read(uint16_t adr) {
		switch(adr) {
			case 0xf0:
			case 0xf1:
			case 0xfa:
			case 0xfb:
			case 0xfc: {
				return 0;
			}
			case 0xf2: {
				return dspAdr;
			}
			case 0xf3: {
				return mydsp.dsp_read(dspAdr & 0x7f);
			}
			case 0xf4:
			case 0xf5:
			case 0xf6:
			case 0xf7:
			case 0xf8:
			case 0xf9: {
				return inPorts[adr - 0xf4];
			}
			case 0xfd:
			case 0xfe:
			case 0xff: {
				uint8_t ret = timer[adr - 0xfd].counter;
				timer[adr - 0xfd].counter = 0;
				return ret;
			}
		}
		if(romReadable && adr >= 0xffc0) {
			return bootRom[adr - 0xffc0];
		}
		return ram[adr];
	}

	void Apu::apu_write(uint16_t adr, uint8_t val) {
		switch(adr) {
			case 0xf0: {
				break; // test register
			}
			case 0xf1: {
				for(int i = 0; i < 3; i++) {
					if(!timer[i].enabled && (val & (1 << i))) {
						timer[i].divider = 0;
						timer[i].counter = 0;
					}
					timer[i].enabled = val & (1 << i);
				}
				if(val & 0x10) {
					inPorts[0] = 0;
					inPorts[1] = 0;
				}
				if(val & 0x20) {
					inPorts[2] = 0;
					inPorts[3] = 0;
				}
				romReadable = val & 0x80;
				break;
			}
			case 0xf2: {
				dspAdr = val;
				break;
			}
			case 0xf3: {
				if(dspAdr < 0x80) mydsp.dsp_write(dspAdr, val);
				break;
			}
			case 0xf4:
			case 0xf5:
			case 0xf6:
			case 0xf7: {
				outPorts[adr - 0xf4] = val;
				break;
			}
			case 0xf8:
			case 0xf9: {
				inPorts[adr - 0xf4] = val;
				break;
			}
			case 0xfa:
			case 0xfb:
			case 0xfc: {
				timer[adr - 0xfa].target = val;
				break;
			}
		}
		ram[adr] = val;
	}

	uint8_t Apu::apu_spcRead(uint16_t adr) {
		apu_cycle();
		return apu_read(adr);
	}

	void Apu::apu_spcWrite(uint16_t adr, uint8_t val) {
		apu_cycle();
		apu_write(adr, val);
	}

	void Apu::apu_spcIdle(bool waiting) {
		(void)waiting;
		apu_cycle();
	}

}
