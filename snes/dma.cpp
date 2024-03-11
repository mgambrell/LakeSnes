#include "dma.h"
#include "snes.h"
#include "cpu.h"
#include "statehandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{

	static const int bAdrOffsets[8][4] = {
		{0, 0, 0, 0},
		{0, 1, 0, 1},
		{0, 0, 0, 0},
		{0, 0, 1, 1},
		{0, 1, 2, 3},
		{0, 1, 0, 1},
		{0, 0, 0, 0},
		{0, 0, 1, 1}
	};

	static const int transferLength[8] = {
		1, 2, 2, 4, 4, 4, 2, 4
	};



	void Dma::dma_init(Snes* snes) {
		this->snes = snes;
	}

	void Dma::dma_free() {
	}

	void Dma::dma_reset() {
		for(int i = 0; i < 8; i++) {
			channel[i].bAdr = 0xff;
			channel[i].aAdr = 0xffff;
			channel[i].aBank = 0xff;
			channel[i].size = 0xffff;
			channel[i].indBank = 0xff;
			channel[i].tableAdr = 0xffff;
			channel[i].repCount = 0xff;
			channel[i].unusedByte = 0xff;
			channel[i].dmaActive = false;
			channel[i].hdmaActive = false;
			channel[i].mode = 7;
			channel[i].fixed = true;
			channel[i].decrement = true;
			channel[i].indirect = true;
			channel[i].fromB = true;
			channel[i].unusedBit = true;
			channel[i].doTransfer = false;
			channel[i].terminated = false;
		}
		dmaState = 0;
		hdmaInitRequested = false;
		hdmaRunRequested = false;
	}

	void Dma::dma_handleState(StateHandler* sh) {
		sh_handleBools(sh, &hdmaInitRequested, &hdmaRunRequested, NULL);
		sh_handleBytes(sh, &dmaState, NULL);
		for(int i = 0; i < 8; i++) {
			sh_handleBools(sh,
				&channel[i].dmaActive, &channel[i].hdmaActive, &channel[i].fixed, &channel[i].decrement,
				&channel[i].indirect, &channel[i].fromB, &channel[i].unusedBit, &channel[i].doTransfer,
				&channel[i].terminated, NULL
			);
			sh_handleBytes(sh,
				&channel[i].bAdr, &channel[i].aBank, &channel[i].indBank, &channel[i].repCount,
				&channel[i].unusedByte, &channel[i].mode, NULL
			);
			sh_handleWords(sh, &channel[i].aAdr, &channel[i].size, &channel[i].tableAdr, NULL);
		}
	}

	uint8_t Dma::dma_read(uint16_t adr) {
		uint8_t c = (adr & 0x70) >> 4;
		switch(adr & 0xf) {
			case 0x0: {
				uint8_t val = channel[c].mode;
				val |= channel[c].fixed << 3;
				val |= channel[c].decrement << 4;
				val |= channel[c].unusedBit << 5;
				val |= channel[c].indirect << 6;
				val |= channel[c].fromB << 7;
				return val;
			}
			case 0x1: {
				return channel[c].bAdr;
			}
			case 0x2: {
				return channel[c].aAdr & 0xff;
			}
			case 0x3: {
				return channel[c].aAdr >> 8;
			}
			case 0x4: {
				return channel[c].aBank;
			}
			case 0x5: {
				return channel[c].size & 0xff;
			}
			case 0x6: {
				return channel[c].size >> 8;
			}
			case 0x7: {
				return channel[c].indBank;
			}
			case 0x8: {
				return channel[c].tableAdr & 0xff;
			}
			case 0x9: {
				return channel[c].tableAdr >> 8;
			}
			case 0xa: {
				return channel[c].repCount;
			}
			case 0xb:
			case 0xf: {
				return channel[c].unusedByte;
			}
			default: {
				return snes->openBus;
			}
		}
	}

	void Dma::dma_write(uint16_t adr, uint8_t val) {
		uint8_t c = (adr & 0x70) >> 4;
		switch(adr & 0xf) {
			case 0x0: {
				channel[c].mode = val & 0x7;
				channel[c].fixed = val & 0x8;
				channel[c].decrement = val & 0x10;
				channel[c].unusedBit = val & 0x20;
				channel[c].indirect = val & 0x40;
				channel[c].fromB = val & 0x80;
				break;
			}
			case 0x1: {
				channel[c].bAdr = val;
				break;
			}
			case 0x2: {
				channel[c].aAdr = (channel[c].aAdr & 0xff00) | val;
				break;
			}
			case 0x3: {
				channel[c].aAdr = (channel[c].aAdr & 0xff) | (val << 8);
				break;
			}
			case 0x4: {
				channel[c].aBank = val;
				break;
			}
			case 0x5: {
				channel[c].size = (channel[c].size & 0xff00) | val;
				break;
			}
			case 0x6: {
				channel[c].size = (channel[c].size & 0xff) | (val << 8);
				break;
			}
			case 0x7: {
				channel[c].indBank = val;
				break;
			}
			case 0x8: {
				channel[c].tableAdr = (channel[c].tableAdr & 0xff00) | val;
				break;
			}
			case 0x9: {
				channel[c].tableAdr = (channel[c].tableAdr & 0xff) | (val << 8);
				break;
			}
			case 0xa: {
				channel[c].repCount = val;
				break;
			}
			case 0xb:
			case 0xf: {
				channel[c].unusedByte = val;
				break;
			}
			default: {
				break;
			}
		}
	}

	void Dma::dma_waitCycle() {
		// run hdma if requested, no sync (already sycned due to dma)
		if(hdmaInitRequested) dma_initHdma(false, 0);
		if(hdmaRunRequested) dma_doHdma(false, 0);
		snes->snes_runCycles(8);
	}

	void Dma::dma_doDma(int cpuCycles) {
		// nmi/irq is delayed by 1 opcode if requested during dma/hdma
		snes->mycpu.intDelay = true;
		// align to multiple of 8
		snes->snes_syncCycles(true, 8);
		// full transfer overhead
		dma_waitCycle();
		for(int i = 0; i < 8; i++) {
			if(!channel[i].dmaActive) continue;
			// do channel i
			dma_waitCycle(); // overhead per channel
			int offIndex = 0;
			while(channel[i].dmaActive) {
				dma_waitCycle();
				dma_transferByte(
					channel[i].aAdr, channel[i].aBank,
					channel[i].bAdr + bAdrOffsets[channel[i].mode][offIndex++], channel[i].fromB
				);
				offIndex &= 3;
				if(!channel[i].fixed) {
					channel[i].aAdr += channel[i].decrement ? -1 : 1;
				}
				channel[i].size--;
				if(channel[i].size == 0) {
					channel[i].dmaActive = false;
				}
			}
		}
		// re-align to cpu cycles
		snes->snes_syncCycles(false, cpuCycles);
	}

	void Dma::dma_initHdma(bool doSync, int cpuCycles) {
		hdmaInitRequested = false;
		bool hdmaEnabled = false;
		// check if a channel is enabled, and do reset
		for(int i = 0; i < 8; i++) {
			if(channel[i].hdmaActive) hdmaEnabled = true;
			channel[i].doTransfer = false;
			channel[i].terminated = false;
		}
		if(!hdmaEnabled) return;
		// nmi/irq is delayed by 1 opcode if requested during dma/hdma
		snes->mycpu.intDelay = true;
		if(doSync) snes->snes_syncCycles(true, 8);
		// full transfer overhead
		snes->snes_runCycles(8);
		for(int i = 0; i < 8; i++) {
			if(channel[i].hdmaActive) {
				// terminate any dma
				channel[i].dmaActive = false;
				// load address, repCount, and indirect address if needed
				snes->snes_runCycles(8);
				channel[i].tableAdr = channel[i].aAdr;
				channel[i].repCount = snes->snes_read((channel[i].aBank << 16) | channel[i].tableAdr++);
				if(channel[i].repCount == 0) channel[i].terminated = true;
				if(channel[i].indirect) {
					snes->snes_runCycles(8);
					channel[i].size = snes->snes_read((channel[i].aBank << 16) | channel[i].tableAdr++);
					snes->snes_runCycles(8);
					channel[i].size |= snes->snes_read((channel[i].aBank << 16) | channel[i].tableAdr++) << 8;
				}
				channel[i].doTransfer = true;
			}
		}
		if(doSync) snes->snes_syncCycles(false, cpuCycles);
	}

	void Dma::dma_doHdma(bool doSync, int cpuCycles) {
		hdmaRunRequested = false;
		bool hdmaActive = false;
		int lastActive = 0;
		for(int i = 0; i < 8; i++) {
			if(channel[i].hdmaActive) {
				hdmaActive = true;
				if(!channel[i].terminated) lastActive = i;
			}
		}
		if(!hdmaActive) return;
		// nmi/irq is delayed by 1 opcode if requested during dma/hdma
		snes->mycpu.intDelay = true;
		if(doSync) snes->snes_syncCycles(true, 8);
		// full transfer overhead
		snes->snes_runCycles(8);
		// do all copies
		for(int i = 0; i < 8; i++) {
			// terminate any dma
			if(channel[i].hdmaActive) channel[i].dmaActive = false;
			if(channel[i].hdmaActive && !channel[i].terminated) {
				// do the hdma
				if(channel[i].doTransfer) {
					for(int j = 0; j < transferLength[channel[i].mode]; j++) {
						snes->snes_runCycles(8);
						if(channel[i].indirect) {
							dma_transferByte(
								channel[i].size++, channel[i].indBank,
								channel[i].bAdr + bAdrOffsets[channel[i].mode][j], channel[i].fromB
							);
						} else {
							dma_transferByte(
								channel[i].tableAdr++, channel[i].aBank,
								channel[i].bAdr + bAdrOffsets[channel[i].mode][j], channel[i].fromB
							);
						}
					}
				}
			}
		}
		// do all updates
		for(int i = 0; i < 8; i++) {
			if(channel[i].hdmaActive && !channel[i].terminated) {
				channel[i].repCount--;
				channel[i].doTransfer = channel[i].repCount & 0x80;
				snes->snes_runCycles(8);
				uint8_t newRepCount = snes->snes_read((channel[i].aBank << 16) | channel[i].tableAdr);
				if((channel[i].repCount & 0x7f) == 0) {
					channel[i].repCount = newRepCount;
					channel[i].tableAdr++;
					if(channel[i].indirect) {
						if(channel[i].repCount == 0 && i == lastActive) {
							// if this is the last active channel, only fetch high, and use 0 for low
							channel[i].size = 0;
						} else {
							snes->snes_runCycles(8);
							channel[i].size = snes->snes_read((channel[i].aBank << 16) | channel[i].tableAdr++);
						}
						snes->snes_runCycles(8);
						channel[i].size |= snes->snes_read((channel[i].aBank << 16) | channel[i].tableAdr++) << 8;
					}
					if(channel[i].repCount == 0) channel[i].terminated = true;
					channel[i].doTransfer = true;
				}
			}
		}
		if(doSync) snes->snes_syncCycles(false, cpuCycles);
	}

	void Dma::dma_transferByte(uint16_t aAdr, uint8_t aBank, uint8_t bAdr, bool fromB) {
		// accessing 0x2180 via b-bus while a-bus accesses ram gives open bus
		bool validB = !(bAdr == 0x80 && (aBank == 0x7e || aBank == 0x7f || (
			(aBank < 0x40 || (aBank >= 0x80 && aBank < 0xc0)) && aAdr < 0x2000
		)));
		// accesing b-bus, or dma regs via a-bus gives open bus
		bool validA = !((aBank < 0x40 || (aBank >= 0x80 && aBank < 0xc0)) && (
			aAdr == 0x420b || aAdr == 0x420c || (aAdr >= 0x4300 && aAdr < 0x4380) || (aAdr >= 0x2100 && aAdr < 0x2200)
		));
		if(fromB) {
			uint8_t val = validB ? snes->snes_readBBus(bAdr) : snes->openBus;
			if(validA) snes->snes_write((aBank << 16) | aAdr, val);
		} else {
			uint8_t val = validA ? snes->snes_read((aBank << 16) | aAdr) : snes->openBus;
			if(validB) snes->snes_writeBBus(bAdr, val);
		}
	}

	void Dma::dma_handleDma(int cpuCycles) {
		// if hdma triggered, do it, except if dmastate indicates dma will be done now
		// (it will be done as part of the dma in that case)
		if(hdmaInitRequested && dmaState != 2) dma_initHdma(true, cpuCycles);
		if(hdmaRunRequested && dmaState != 2) dma_doHdma(true, cpuCycles);
		if(dmaState == 1) {
			dmaState = 2;
			return;
		}
		if(dmaState == 2) {
			// do dma
			dma_doDma(cpuCycles);
			dmaState = 0;
		}
	}

	void Dma::dma_startDma(uint8_t val, bool hdma) {
		for(int i = 0; i < 8; i++) {
			if(hdma) {
				channel[i].hdmaActive = val & (1 << i);
			} else {
				channel[i].dmaActive = val & (1 << i);
			}
		}
		if(!hdma) {
			dmaState = val != 0 ? 1 : 0;
		}
	}

}
