#include "apu.h"
#include "snes.h"
#include "spc.h"
#include "dsp.h"
#include "statehandler.h"
#include "global.h"

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

	static void apu_cycle() {
		if((apu->cycles & 0x1f) == 0) {
			// every 32 cycles
			dsp_cycle(apu->dsp);
		}

		// handle timers
		for(int i = 0; i < 3; i++) {
			if(apu->timer[i].cycles == 0) {
				apu->timer[i].cycles = i == 2 ? 16 : 128;
				if(apu->timer[i].enabled) {
					apu->timer[i].divider++;
					if(apu->timer[i].divider == apu->timer[i].target) {
						apu->timer[i].divider = 0;
						apu->timer[i].counter++;
						apu->timer[i].counter &= 0xf;
					}
				}
			}
			apu->timer[i].cycles--;
		}

		apu->cycles++;
	}

	void apu_init() {
		apu->spc = spc_init(apu, apu_spcRead, apu_spcWrite, apu_spcIdle);
		apu->dsp = dsp_init(apu);
	}

	void apu_free() {
		spc_free(apu->spc);
		dsp_free(apu->dsp);
	}

	void apu_reset() {
		// TODO: hard reset for apu
		spc_reset(apu->spc, true);
		dsp_reset(apu->dsp);
		memset(apu->ram, 0, sizeof(apu->ram));
		apu->dspAdr = 0;
		apu->romReadable = true;
		apu->cycles = 0;
		memset(apu->inPorts, 0, sizeof(apu->inPorts));
		memset(apu->outPorts, 0, sizeof(apu->outPorts));
		for(int i = 0; i < 3; i++) {
			apu->timer[i].cycles = 0;
			apu->timer[i].divider = 0;
			apu->timer[i].target = 0;
			apu->timer[i].counter = 0;
			apu->timer[i].enabled = false;
		}
	}

	void apu_handleState( StateHandler* sh) {
		sh_handleBools(sh, &apu->romReadable, NULL);
		sh_handleBytes(sh,
			&apu->dspAdr, &apu->inPorts[0], &apu->inPorts[1], &apu->inPorts[2], &apu->inPorts[3], &apu->inPorts[4],
			&apu->inPorts[5], &apu->outPorts[0], &apu->outPorts[1], &apu->outPorts[2], &apu->outPorts[3], NULL
		);
		sh_handleLongLongs(sh, &apu->cycles, NULL);
		for(int i = 0; i < 3; i++) {
			sh_handleBools(sh, &apu->timer[i].enabled, NULL);
			sh_handleBytes(sh, &apu->timer[i].cycles, &apu->timer[i].divider, &apu->timer[i].target, &apu->timer[i].counter, NULL);
		}
		sh_handleByteArray(sh, apu->ram, 0x10000);
		// components
		spc_handleState(apu->spc, sh);
		dsp_handleState(apu->dsp, sh);
	}

	void apu_runCycles() {
		uint64_t sync_to = (uint64_t)snes->cycles * (snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster);

		while (apu->cycles < sync_to) {
			spc_runOpcode(apu->spc);
		}
	}

	uint8_t apu_read( uint16_t adr) {
		switch(adr) {
			case 0xf0:
			case 0xf1:
			case 0xfa:
			case 0xfb:
			case 0xfc: {
				return 0;
			}
			case 0xf2: {
				return apu->dspAdr;
			}
			case 0xf3: {
				return dsp_read(apu->dsp, apu->dspAdr & 0x7f);
			}
			case 0xf4:
			case 0xf5:
			case 0xf6:
			case 0xf7:
			case 0xf8:
			case 0xf9: {
				return apu->inPorts[adr - 0xf4];
			}
			case 0xfd:
			case 0xfe:
			case 0xff: {
				uint8_t ret = apu->timer[adr - 0xfd].counter;
				apu->timer[adr - 0xfd].counter = 0;
				return ret;
			}
		}
		if(apu->romReadable && adr >= 0xffc0) {
			return bootRom[adr - 0xffc0];
		}
		return apu->ram[adr];
	}

	void apu_write( uint16_t adr, uint8_t val) {
		switch(adr) {
			case 0xf0: {
				break; // test register
			}
			case 0xf1: {
				for(int i = 0; i < 3; i++) {
					if(!apu->timer[i].enabled && (val & (1 << i))) {
						apu->timer[i].divider = 0;
						apu->timer[i].counter = 0;
					}
					apu->timer[i].enabled = val & (1 << i);
				}
				if(val & 0x10) {
					apu->inPorts[0] = 0;
					apu->inPorts[1] = 0;
				}
				if(val & 0x20) {
					apu->inPorts[2] = 0;
					apu->inPorts[3] = 0;
				}
				apu->romReadable = val & 0x80;
				break;
			}
			case 0xf2: {
				apu->dspAdr = val;
				break;
			}
			case 0xf3: {
				if(apu->dspAdr < 0x80) dsp_write(apu->dsp, apu->dspAdr, val);
				break;
			}
			case 0xf4:
			case 0xf5:
			case 0xf6:
			case 0xf7: {
				apu->outPorts[adr - 0xf4] = val;
				break;
			}
			case 0xf8:
			case 0xf9: {
				apu->inPorts[adr - 0xf4] = val;
				break;
			}
			case 0xfa:
			case 0xfb:
			case 0xfc: {
				apu->timer[adr - 0xfa].target = val;
				break;
			}
		}
		apu->ram[adr] = val;
	}

	uint8_t apu_spcRead(void* mem, uint16_t adr) {
		apu_cycle();
		return apu_read(adr);
	}

	void apu_spcWrite(void* mem, uint16_t adr, uint8_t val) {
		apu_cycle();
		apu_write(adr, val);
	}

	void apu_spcIdle(void* mem, bool waiting) {
		apu_cycle();
	}

}
