#include "spc.h"
#include "apu.h"
#include "statehandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{
	void Spc::spc_init(Apu* apu) {
		config.apu = apu;
		config.snes = apu->config.snes;
	}

	void Spc::spc_free() {
	}

	void Spc::spc_reset(bool hard) {
		if(hard) {
			a = 0;
			x = 0;
			y = 0;
			sp = 0;
			pc = 0;
			c = false;
			z = false;
			v = false;
			n = false;
			i = false;
			h = false;
			p = false;
			b = false;
		}
		stopped = false;
		resetWanted = true;
		step = 0;
	}

	void Spc::spc_handleState(StateHandler* sh) {
		sh_handleBools(sh,
			&c, &z, &v, &n, &i, &h, &p, &b, &stopped,
			&resetWanted, NULL
		);
		sh_handleBytes(sh, &a, &x, &y, &sp, &opcode, &dat, &param, NULL);
		sh_handleWords(sh, &pc, &adr, &adr1, &dat16, NULL);
		sh_handleInts(sh, &step, &bstep, NULL);
	}

	// Actraiser 2, Rendering Ranger B2, and a handful of other games have
	// very tight timing constraints while processing uploads from the main cpu.
	// Certain select opcodes as well as all 2-cycle (1 fetch, 1 exec_insn) run
	// in single-cycle mode.   -dink sept 15, 2023

	void Spc::spc_runOpcode() {
		if(resetWanted) {
			// based on 6502, brk without writes
			resetWanted = false;
			spc_read(pc);
			spc_read(pc);
			spc_read(0x100 | sp--);
			spc_read(0x100 | sp--);
			spc_read(0x100 | sp--);
			spc_idle();
			i = false;
			pc = spc_readWord(0xfffe, 0xffff);
			return;
		}
		if(stopped) {
			spc_idleWait();
			return;
		}
		if (step == 0) {
			bstep = 0;
			opcode = spc_readOpcode();
			step = 1;
			return;
		}
		spc_doOpcode(opcode);
		if (step == 1) step = 0; // reset step for non cycle-stepped opcodes.
	}

	uint8_t Spc::spc_read(uint16_t adr) {
		return config.apu->apu_spcRead(adr);
	}

	void Spc::spc_write(uint16_t adr, uint8_t val) {
		config.apu->apu_spcWrite(adr, val);
	}

	void Spc::spc_idle() {
		config.apu->apu_spcIdle(false);
	}

	void Spc::spc_idleWait() {
		config.apu->apu_spcIdle(true);
	}

	uint8_t Spc::spc_readOpcode() {
		return spc_read(pc++);
	}

	uint16_t Spc::spc_readOpcodeWord() {
		uint8_t low = spc_readOpcode();
		return low | (spc_readOpcode() << 8);
	}

	uint8_t Spc::spc_getFlags() {
		uint8_t val = n << 7;
		val |= v << 6;
		val |= p << 5;
		val |= b << 4;
		val |= h << 3;
		val |= i << 2;
		val |= z << 1;
		val |= c;
		return val;
	}

	void Spc::spc_setFlags(uint8_t val) {
		n = val & 0x80;
		v = val & 0x40;
		p = val & 0x20;
		b = val & 0x10;
		h = val & 8;
		i = val & 4;
		z = val & 2;
		c = val & 1;
	}

	void Spc::spc_setZN(uint8_t value) {
		z = value == 0;
		n = value & 0x80;
	}

	void Spc::spc_doBranch(uint8_t value, bool check) {
		if(check) {
			// taken branch: 2 extra cycles
			spc_idle();
			spc_idle();
			pc += (int8_t) value;
		}
	}

	uint8_t Spc::spc_pullByte() {
		sp++;
		return spc_read(0x100 | sp);
	}

	void Spc::spc_pushByte(uint8_t value) {
		spc_write(0x100 | sp, value);
		sp--;
	}

	uint16_t Spc::spc_pullWord() {
		uint8_t value = spc_pullByte();
		return value | (spc_pullByte() << 8);
	}

	void Spc::spc_pushWord(uint16_t value) {
		spc_pushByte(value >> 8);
		spc_pushByte(value & 0xff);
	}

	uint16_t Spc::spc_readWord(uint16_t adrl, uint16_t adrh) {
		uint8_t value = spc_read(adrl);
		return value | (spc_read(adrh) << 8);
	}

	void Spc::spc_writeWord(uint16_t adrl, uint16_t adrh, uint16_t value) {
		spc_write(adrl, value & 0xff);
		spc_write(adrh, value >> 8);
	}

	// adressing modes

	uint16_t Spc::spc_adrDp() {
		return spc_readOpcode() | (p << 8);
	}

	uint16_t Spc::spc_adrAbs() {
		return spc_readOpcodeWord();
	}

	void Spc::spc_adrAbs_stepped() {
		switch (bstep++) {
			case 0: adr =   spc_readOpcode(); break;
			case 1: adr |= (spc_readOpcode() << 8); bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrInd() {
		spc_read(pc);
		return x | (p << 8);
	}

	uint16_t Spc::spc_adrIdx() {
		uint8_t pointer = spc_readOpcode();
		spc_idle();
		return spc_readWord(((pointer + x) & 0xff) | (p << 8), ((pointer + x + 1) & 0xff) | (p << 8));
	}

	void Spc::spc_adrIdx_stepped() {
		switch (bstep++) {
			case 0: param = spc_readOpcode(); break;
			case 1: spc_idle(); break;
			case 2: break;
			case 3: adr = spc_readWord(((param + x) & 0xff) | (p << 8), ((param + x + 1) & 0xff) | (p << 8)); bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrImm() {
		return pc++;
	}

	uint16_t Spc::spc_adrDpx() {
		uint16_t res = ((spc_readOpcode() + x) & 0xff) | (p << 8);
		spc_idle();
		return res;
	}

	void Spc::spc_adrDpx_stepped() {
		switch (bstep++) {
			case 0: adr = spc_readOpcode(); break;
			case 1: spc_idle(); adr = ((adr + x) & 0xff) | (p << 8); bstep = 0; break;
		}
	}

	#if 0
	// deprecated/unused!
	static uint16_t spc_adrDpy() {
		uint16_t res = ((spc_readOpcode() + y) & 0xff) | (p << 8);
		spc_idle();
		return res;
	}
	#endif

	void Spc::spc_adrDpy_stepped() {
		switch (bstep++) {
			case 0: adr = spc_readOpcode(); break;
			case 1: spc_idle(); adr = ((adr + y) & 0xff) | (p << 8); bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrAbx() {
		uint16_t res = (spc_readOpcodeWord() + x) & 0xffff;
		spc_idle();
		return res;
	}

	void Spc::spc_adrAbx_stepped() {
		switch (bstep++) {
			case 0: adr = spc_readOpcode(); break;
			case 1: adr |= (spc_readOpcode() << 8); break;
			case 2: spc_idle(); adr = (adr + x) & 0xffff; bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrAby() {
		uint16_t res = (spc_readOpcodeWord() + y) & 0xffff;
		spc_idle();
		return res;
	}

	void Spc::spc_adrAby_stepped() {
		switch (bstep++) {
			case 0: adr = spc_readOpcode(); break;
			case 1: adr |= (spc_readOpcode() << 8); break;
			case 2: spc_idle(); adr = (adr + y) & 0xffff; bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrIdy() {
		uint8_t pointer = spc_readOpcode();
		uint16_t adr = spc_readWord(pointer | (p << 8), ((pointer + 1) & 0xff) | (p << 8));
		spc_idle();
		return (adr + y) & 0xffff;
	}

	void Spc::spc_adrIdy_stepped() {
		switch (bstep++) {
			case 0: dat = spc_readOpcode(); break;
			case 1: adr = spc_read(dat | (p << 8)); break;
			case 2: adr |= (spc_read(((dat + 1) & 0xff) | (p << 8)) << 8); break;
			case 3: spc_idle(); adr = (adr + y) & 0xffff; bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrDpDp(uint8_t* srcVal) {
		*srcVal = spc_read(spc_readOpcode() | (p << 8));
		return spc_readOpcode() | (p << 8);
	}

	void Spc::spc_adrDpDp_stepped() {
		switch (bstep++) {
			case 0: dat = spc_readOpcode(); break;
			case 1: dat = spc_read(dat | (p << 8)); break;
			case 2: adr = spc_readOpcode() | (p << 8); bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrDpImm(uint8_t* srcVal) {
		*srcVal = spc_readOpcode();
		return spc_readOpcode() | (p << 8);
	}

	uint16_t Spc::spc_adrIndInd(uint8_t* srcVal) {
		spc_read(pc);
		*srcVal = spc_read(y | (p << 8));
		return x | (p << 8);
	}

	uint8_t Spc::spc_adrAbsBit(uint16_t* adr) {
		uint16_t adrBit = spc_readOpcodeWord();
		*adr = adrBit & 0x1fff;
		return adrBit >> 13;
	}

	void Spc::spc_adrAbsBit_stepped() {
		switch (bstep++) {
			case 0: adr = spc_readOpcode(); break;
			case 1: adr |= (spc_readOpcode() << 8); dat = adr >> 13; adr &= 0x1fff; bstep = 0; break;
		}
	}

	uint16_t Spc::spc_adrDpWord(uint16_t* low) {
		uint8_t adr = spc_readOpcode();
		*low = adr | (p << 8);
		return ((adr + 1) & 0xff) | (p << 8);
	}

	uint16_t Spc::spc_adrIndP() {
		spc_read(pc);
		return x++ | (p << 8);
	}

	// opcode functions

	void Spc::spc_and(uint16_t adr) {
		a &= spc_read(adr);
		spc_setZN(a);
	}

	void Spc::spc_andm(uint16_t dst, uint8_t value) {
		uint8_t result = spc_read(dst) & value;
		spc_write(dst, result);
		spc_setZN(result);
	}

	void Spc::spc_or(uint16_t adr) {
		a |= spc_read(adr);
		spc_setZN(a);
	}

	void Spc::spc_orm(uint16_t dst, uint8_t value) {
		uint8_t result = spc_read(dst) | value;
		spc_write(dst, result);
		spc_setZN(result);
	}

	void Spc::spc_eor(uint16_t adr) {
		a ^= spc_read(adr);
		spc_setZN(a);
	}

	void Spc::spc_eorm(uint16_t dst, uint8_t value) {
		uint8_t result = spc_read(dst) ^ value;
		spc_write(dst, result);
		spc_setZN(result);
	}

	void Spc::spc_adc(uint16_t adr) {
		uint8_t value = spc_read(adr);
		int result = a + value + c;
		v = (a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
		h = ((a & 0xf) + (value & 0xf) + c) > 0xf;
		c = result > 0xff;
		a = result;
		spc_setZN(a);
	}

	void Spc::spc_adcm(uint16_t dst, uint8_t value) {
		uint8_t applyOn = spc_read(dst);
		int result = applyOn + value + c;
		v = (applyOn & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
		h = ((applyOn & 0xf) + (value & 0xf) + c) > 0xf;
		c = result > 0xff;
		spc_write(dst, result);
		spc_setZN(result);
	}

	void Spc::spc_sbc(uint16_t adr) {
		uint8_t value = spc_read(adr) ^ 0xff;
		int result = a + value + c;
		v = (a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
		h = ((a & 0xf) + (value & 0xf) + c) > 0xf;
		c = result > 0xff;
		a = result;
		spc_setZN(a);
	}

	void Spc::spc_sbcm(uint16_t dst, uint8_t value) {
		value ^= 0xff;
		uint8_t applyOn = spc_read(dst);
		int result = applyOn + value + c;
		v = (applyOn & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
		h = ((applyOn & 0xf) + (value & 0xf) + c) > 0xf;
		c = result > 0xff;
		spc_write(dst, result);
		spc_setZN(result);
	}

	void Spc::spc_cmp(uint16_t adr) {
		uint8_t value = spc_read(adr) ^ 0xff;
		int result = a + value + 1;
		c = result > 0xff;
		spc_setZN(result);
	}

	void Spc::spc_cmpx(uint16_t adr) {
		uint8_t value = spc_read(adr) ^ 0xff;
		int result = x + value + 1;
		c = result > 0xff;
		spc_setZN(result);
	}

	void Spc::spc_cmpy(uint16_t adr) {
		uint8_t value = spc_read(adr) ^ 0xff;
		int result = y + value + 1;
		c = result > 0xff;
		spc_setZN(result);
	}

	void Spc::spc_cmpm(uint16_t dst, uint8_t value) {
		value ^= 0xff;
		int result = spc_read(dst) + value + 1;
		c = result > 0xff;
		spc_idle();
		spc_setZN(result);
	}

	void Spc::spc_mov(uint16_t adr) {
		a = spc_read(adr);
		spc_setZN(a);
	}

	void Spc::spc_movx(uint16_t adr) {
		x = spc_read(adr);
		spc_setZN(x);
	}

	void Spc::spc_movy(uint16_t adr) {
		y = spc_read(adr);
		spc_setZN(y);
	}

	void Spc::spc_movs(uint16_t adr) {
		switch (bstep++) {
			case 0: spc_read(adr); break;
			case 1: spc_write(adr, a); bstep = 0; break;
		}
	}

	void Spc::spc_movsx(uint16_t adr) {
		switch (bstep++) {
			case 0: spc_read(adr); break;
			case 1: spc_write(adr, x); bstep = 0; break;
		}
	}

	void Spc::spc_movsy(uint16_t adr) {
		switch (bstep++) {
			case 0: spc_read(adr); break;
			case 1: spc_write(adr, y); bstep = 0; break;
		}
	}

	void Spc::spc_asl(uint16_t adr) {
		uint8_t val = spc_read(adr);
		c = val & 0x80;
		val <<= 1;
		spc_write(adr, val);
		spc_setZN(val);
	}

	void Spc::spc_lsr(uint16_t adr) {
		uint8_t val = spc_read(adr);
		c = val & 1;
		val >>= 1;
		spc_write(adr, val);
		spc_setZN(val);
	}

	void Spc::spc_rol(uint16_t adr) {
		uint8_t val = spc_read(adr);
		bool newC = val & 0x80;
		val = (val << 1) | c;
		c = newC;
		spc_write(adr, val);
		spc_setZN(val);
	}

	void Spc::spc_ror(uint16_t adr) {
		uint8_t val = spc_read(adr);
		bool newC = val & 1;
		val = (val >> 1) | (c << 7);
		c = newC;
		spc_write(adr, val);
		spc_setZN(val);
	}

	void Spc::spc_inc(uint16_t adr) {
		uint8_t val = spc_read(adr) + 1;
		spc_write(adr, val);
		spc_setZN(val);
	}

	void Spc::spc_dec(uint16_t adr) {
		uint8_t val = spc_read(adr) - 1;
		spc_write(adr, val);
		spc_setZN(val);
	}

	void Spc::spc_doOpcode(uint8_t opcode) {
		switch(opcode) {
			case 0x00: { // nop imp
				spc_read(pc);
				// no operation
				break;
			}
			case 0x01:
			case 0x11:
			case 0x21:
			case 0x31:
			case 0x41:
			case 0x51:
			case 0x61:
			case 0x71:
			case 0x81:
			case 0x91:
			case 0xa1:
			case 0xb1:
			case 0xc1:
			case 0xd1:
			case 0xe1:
			case 0xf1: { // tcall imp
				spc_read(pc);
				spc_idle();
				spc_pushWord(pc);
				spc_idle();
				uint16_t adr = 0xffde - (2 * (opcode >> 4));
				pc = spc_readWord(adr, adr + 1);
				break;
			}
			case 0x02:
			case 0x22:
			case 0x42:
			case 0x62:
			case 0x82:
			case 0xa2:
			case 0xc2:
			case 0xe2: { // set1 dp
				uint16_t adr = spc_adrDp();
				spc_write(adr, spc_read(adr) | (1 << (opcode >> 5)));
				break;
			}
			case 0x12:
			case 0x32:
			case 0x52:
			case 0x72:
			case 0x92:
			case 0xb2:
			case 0xd2:
			case 0xf2: { // clr1 dp
				uint16_t adr = spc_adrDp();
				spc_write(adr, spc_read(adr) & ~(1 << (opcode >> 5)));
				break;
			}
			case 0x03:
			case 0x23:
			case 0x43:
			case 0x63:
			case 0x83:
			case 0xa3:
			case 0xc3:
			case 0xe3: { // bbs dp, rel
				uint8_t val = spc_read(spc_adrDp());
				spc_idle();
				spc_doBranch(spc_readOpcode(), val & (1 << (opcode >> 5)));
				break;
			}
			case 0x13:
			case 0x33:
			case 0x53:
			case 0x73:
			case 0x93:
			case 0xb3:
			case 0xd3:
			case 0xf3: { // bbc dp, rel
				uint8_t val = spc_read(spc_adrDp());
				spc_idle();
				spc_doBranch(spc_readOpcode(), (val & (1 << (opcode >> 5))) == 0);
				break;
			}
			case 0x04: { // or  dp
				spc_or(spc_adrDp());
				break;
			}
			case 0x05: { // or  abs
				spc_or(spc_adrAbs());
				break;
			}
			case 0x06: { // or  ind
				spc_or(spc_adrInd());
				break;
			}
			case 0x07: { // or  idx
				spc_or(spc_adrIdx());
				break;
			}
			case 0x08: { // or  imm
				spc_or(spc_adrImm());
				break;
			}
			case 0x09: { // orm dp, dp
				uint8_t src = 0;
				uint16_t dst = spc_adrDpDp(&src);
				spc_orm(dst, src);
				break;
			}
			case 0x0a: { // or1 abs.bit
				uint16_t adr = 0;
				uint8_t bit = spc_adrAbsBit(&adr);
				c = c | ((spc_read(adr) >> bit) & 1);
				spc_idle();
				break;
			}
			case 0x0b: { // asl dp
				spc_asl(spc_adrDp());
				break;
			}
			case 0x0c: { // asl abs
				spc_asl(spc_adrAbs());
				break;
			}
			case 0x0d: { // pushp imp
				spc_read(pc);
				spc_pushByte(spc_getFlags());
				spc_idle();
				break;
			}
			case 0x0e: { // tset1 abs
				uint16_t adr = spc_adrAbs();
				uint8_t val = spc_read(adr);
				spc_read(adr);
				uint8_t result = a + (val ^ 0xff) + 1;
				spc_setZN(result);
				spc_write(adr, val | a);
				break;
			}
			case 0x0f: { // brk imp
				spc_read(pc);
				spc_pushWord(pc);
				spc_pushByte(spc_getFlags());
				spc_idle();
				i = false;
				b = true;
				pc = spc_readWord(0xffde, 0xffdf);
				break;
			}
			case 0x10: { // bpl rel (!n)
				switch (step++) {
					case 1: dat = spc_readOpcode(); if (n) step = 0; break;
					case 2: spc_idle(); break;
					case 3: spc_idle(); pc += (int8_t) dat; step = 0; break;
				}
				break;
			}
			case 0x14: { // or  dpx
				spc_or(spc_adrDpx());
				break;
			}
			case 0x15: { // or  abx
				spc_or(spc_adrAbx());
				break;
			}
			case 0x16: { // or  aby
				spc_or(spc_adrAby());
				break;
			}
			case 0x17: { // or  idy
				spc_or(spc_adrIdy());
				break;
			}
			case 0x18: { // orm dp, imm
				uint8_t src = 0;
				uint16_t dst = spc_adrDpImm(&src);
				spc_orm(dst, src);
				break;
			}
			case 0x19: { // orm ind, ind
				uint8_t src = 0;
				uint16_t dst = spc_adrIndInd(&src);
				spc_orm(dst, src);
				break;
			}
			case 0x1a: { // decw dp
				uint16_t low = 0;
				uint16_t high = spc_adrDpWord(&low);
				uint16_t value = spc_read(low) - 1;
				spc_write(low, value & 0xff);
				value += spc_read(high) << 8;
				spc_write(high, value >> 8);
				z = value == 0;
				n = value & 0x8000;
				break;
			}
			case 0x1b: { // asl dpx
				spc_asl(spc_adrDpx());
				break;
			}
			case 0x1c: { // asla imp
				spc_read(pc);
				c = a & 0x80;
				a <<= 1;
				spc_setZN(a);
				break;
			}
			case 0x1d: { // decx imp
				spc_read(pc);
				x--;
				spc_setZN(x);
				break;
			}
			case 0x1e: { // cmpx abs
				spc_cmpx(spc_adrAbs());
				break;
			}
			case 0x1f: { // jmp iax
				uint16_t pointer = spc_readOpcodeWord();
				spc_idle();
				pc = spc_readWord((pointer + x) & 0xffff, (pointer + x + 1) & 0xffff);
				break;
			}
			case 0x20: { // clrp imp
				spc_read(pc);
				p = false;
				break;
			}
			case 0x24: { // and dp
				spc_and(spc_adrDp());
				break;
			}
			case 0x25: { // and abs
				spc_and(spc_adrAbs());
				break;
			}
			case 0x26: { // and ind
				spc_and(spc_adrInd());
				break;
			}
			case 0x27: { // and idx
				spc_and(spc_adrIdx());
				break;
			}
			case 0x28: { // and imm
				spc_and(spc_adrImm());
				break;
			}
			case 0x29: { // andm dp, dp
				uint8_t src = 0;
				uint16_t dst = spc_adrDpDp(&src);
				spc_andm(dst, src);
				break;
			}
			case 0x2a: { // or1n abs.bit
				uint16_t adr = 0;
				uint8_t bit = spc_adrAbsBit(&adr);
				c = c | (~(spc_read(adr) >> bit) & 1);
				spc_idle();
				break;
			}
			case 0x2b: { // rol dp
				spc_rol(spc_adrDp());
				break;
			}
			case 0x2c: { // rol abs
				spc_rol(spc_adrAbs());
				break;
			}
			case 0x2d: { // pusha imp
				spc_read(pc);
				spc_pushByte(a);
				spc_idle();
				break;
			}
			case 0x2e: { // cbne dp, rel
				uint8_t val = spc_read(spc_adrDp()) ^ 0xff;
				spc_idle();
				uint8_t result = a + val + 1;
				spc_doBranch(spc_readOpcode(), result != 0);
				break;
			}
			case 0x2f: { // bra rel
				spc_doBranch(spc_readOpcode(), true);
				break;
			}
			case 0x30: { // bmi rel
				spc_doBranch(spc_readOpcode(), n);
				break;
			}
			case 0x34: { // and dpx
				spc_and(spc_adrDpx());
				break;
			}
			case 0x35: { // and abx
				spc_and(spc_adrAbx());
				break;
			}
			case 0x36: { // and aby
				spc_and(spc_adrAby());
				break;
			}
			case 0x37: { // and idy
				spc_and(spc_adrIdy());
				break;
			}
			case 0x38: { // andm dp, imm
				uint8_t src = 0;
				uint16_t dst = spc_adrDpImm(&src);
				spc_andm(dst, src);
				break;
			}
			case 0x39: { // andm ind, ind
				uint8_t src = 0;
				uint16_t dst = spc_adrIndInd(&src);
				spc_andm(dst, src);
				break;
			}
			case 0x3a: { // incw dp
				switch (step++) {
					case 1: adr = spc_adrDpWord(&adr1); break;
					case 2: dat16 = spc_read(adr1) + 1; break;
					case 3: spc_write(adr1, dat16 & 0xff); break;
					case 4: dat16 += spc_read(adr) << 8; break;
					case 5:
						spc_write(adr, dat16 >> 8);
						z = dat16 == 0;
						n = dat16 & 0x8000;
						step = 0;
						break;
				}
				break;
			}
			case 0x3b: { // rol dpx
				spc_rol(spc_adrDpx());
				break;
			}
			case 0x3c: { // rola imp
				spc_read(pc);
				bool newC = a & 0x80;
				a = (a << 1) | c;
				c = newC;
				spc_setZN(a);
				break;
			}
			case 0x3d: { // incx imp
				spc_read(pc);
				x++;
				spc_setZN(x);
				break;
			}
			case 0x3e: { // cmpx dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_cmpx(adr); step = 0; break;
				}
				break;
			}
			case 0x3f: { // call abs
				uint16_t dst = spc_readOpcodeWord();
				spc_idle();
				spc_pushWord(pc);
				spc_idle();
				spc_idle();
				pc = dst;
				break;
			}
			case 0x40: { // setp imp
				spc_read(pc);
				p = true;
				break;
			}
			case 0x44: { // eor dp
				spc_eor(spc_adrDp());
				break;
			}
			case 0x45: { // eor abs
				spc_eor(spc_adrAbs());
				break;
			}
			case 0x46: { // eor ind
				spc_eor(spc_adrInd());
				break;
			}
			case 0x47: { // eor idx
				spc_eor(spc_adrIdx());
				break;
			}
			case 0x48: { // eor imm
				spc_eor(spc_adrImm());
				break;
			}
			case 0x49: { // eorm dp, dp
				uint8_t src = 0;
				uint16_t dst = spc_adrDpDp(&src);
				spc_eorm(dst, src);
				break;
			}
			case 0x4a: { // and1 abs.bit
				uint16_t adr = 0;
				uint8_t bit = spc_adrAbsBit(&adr);
				c = c & ((spc_read(adr) >> bit) & 1);
				break;
			}
			case 0x4b: { // lsr dp
				spc_lsr(spc_adrDp());
				break;
			}
			case 0x4c: { // lsr abs
				spc_lsr(spc_adrAbs());
				break;
			}
			case 0x4d: { // pushx imp
				spc_read(pc);
				spc_pushByte(x);
				spc_idle();
				break;
			}
			case 0x4e: { // tclr1 abs
				uint16_t adr = spc_adrAbs();
				uint8_t val = spc_read(adr);
				spc_read(adr);
				uint8_t result = a + (val ^ 0xff) + 1;
				spc_setZN(result);
				spc_write(adr, val & ~a);
				break;
			}
			case 0x4f: { // pcall dp
				uint8_t dst = spc_readOpcode();
				spc_idle();
				spc_pushWord(pc);
				spc_idle();
				pc = 0xff00 | dst;
				break;
			}
			case 0x50: { // bvc rel
				spc_doBranch(spc_readOpcode(), !v);
				break;
			}
			case 0x54: { // eor dpx
				spc_eor(spc_adrDpx());
				break;
			}
			case 0x55: { // eor abx
				spc_eor(spc_adrAbx());
				break;
			}
			case 0x56: { // eor aby
				spc_eor(spc_adrAby());
				break;
			}
			case 0x57: { // eor idy
				spc_eor(spc_adrIdy());
				break;
			}
			case 0x58: { // eorm dp, imm
				uint8_t src = 0;
				uint16_t dst = spc_adrDpImm(&src);
				spc_eorm(dst, src);
				break;
			}
			case 0x59: { // eorm ind, ind
				uint8_t src = 0;
				uint16_t dst = spc_adrIndInd(&src);
				spc_eorm(dst, src);
				break;
			}
			case 0x5a: { // cmpw dp
				uint16_t low = 0;
				uint16_t high = spc_adrDpWord(&low);
				uint16_t value = spc_readWord(low, high) ^ 0xffff;
				uint16_t ya = a | (y << 8);
				int result = ya + value + 1;
				c = result > 0xffff;
				z = (result & 0xffff) == 0;
				n = result & 0x8000;
				break;
			}
			case 0x5b: { // lsr dpx
				spc_lsr(spc_adrDpx());
				break;
			}
			case 0x5c: { // lsra imp
				spc_read(pc);
				c = a & 1;
				a >>= 1;
				spc_setZN(a);
				break;
			}
			case 0x5d: { // movxa imp
				spc_read(pc);
				x = a;
				spc_setZN(x);
				break;
			}
			case 0x5e: { // cmpy abs
				spc_cmpy(spc_adrAbs());
				break;
			}
			case 0x5f: { // jmp abs
				pc = spc_readOpcodeWord();
				break;
			}
			case 0x60: { // clrc imp
				spc_read(pc);
				c = false;
				break;
			}
			case 0x64: { // cmp dp
				spc_cmp(spc_adrDp());
				break;
			}
			case 0x65: { // cmp abs
				spc_cmp(spc_adrAbs());
				break;
			}
			case 0x66: { // cmp ind
				spc_cmp(spc_adrInd());
				break;
			}
			case 0x67: { // cmp idx
				spc_cmp(spc_adrIdx());
				break;
			}
			case 0x68: { // cmp imm
				spc_cmp(spc_adrImm());
				break;
			}
			case 0x69: { // cmpm dp, dp
				switch (step++) {
					case 1: spc_adrDpDp_stepped(); break;
					case 2: spc_adrDpDp_stepped(); break;
					case 3: spc_adrDpDp_stepped(); break;
					case 4: dat ^= 0xff; dat16 = spc_read(adr) + dat + 1; break;
					case 5: c = dat16 > 0xff; spc_idle(); spc_setZN(dat16); step = 0; break;
				}
				break;
			}
			case 0x6a: { // and1n abs.bit
				uint16_t adr = 0;
				uint8_t bit = spc_adrAbsBit(&adr);
				c = c & (~(spc_read(adr) >> bit) & 1);
				break;
			}
			case 0x6b: { // ror dp
				spc_ror(spc_adrDp());
				break;
			}
			case 0x6c: { // ror abs
				spc_ror(spc_adrAbs());
				break;
			}
			case 0x6d: { // pushy imp
				spc_read(pc);
				spc_pushByte(y);
				spc_idle();
				break;
			}
			case 0x6e: { // dbnz dp, rel
				uint16_t adr = spc_adrDp();
				uint8_t result = spc_read(adr) - 1;
				spc_write(adr, result);
				spc_doBranch(spc_readOpcode(), result != 0);
				break;
			}
			case 0x6f: { // ret imp
				spc_read(pc);
				spc_idle();
				pc = spc_pullWord();
				break;
			}
			case 0x70: { // bvs rel
				spc_doBranch(spc_readOpcode(), v);
				break;
			}
			case 0x74: { // cmp dpx
				spc_cmp(spc_adrDpx());
				break;
			}
			case 0x75: { // cmp abx
				spc_cmp(spc_adrAbx());
				break;
			}
			case 0x76: { // cmp aby
				spc_cmp(spc_adrAby());
				break;
			}
			case 0x77: { // cmp idy
				spc_cmp(spc_adrIdy());
				break;
			}
			case 0x78: { // cmpm dp, imm
				uint8_t src = 0;
				uint16_t dst = spc_adrDpImm(&src);
				spc_cmpm(dst, src);
				break;
			}
			case 0x79: { // cmpm ind, ind
				uint8_t src = 0;
				uint16_t dst = spc_adrIndInd(&src);
				spc_cmpm(dst, src);
				break;
			}
			case 0x7a: { // addw dp
				uint16_t low = 0;
				uint16_t high = spc_adrDpWord(&low);
				uint8_t vall = spc_read(low);
				spc_idle();
				uint16_t value = vall | (spc_read(high) << 8);
				uint16_t ya = a | (y << 8);
				int result = ya + value;
				v = (ya & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
				h = ((ya & 0xfff) + (value & 0xfff)) > 0xfff;
				c = result > 0xffff;
				z = (result & 0xffff) == 0;
				n = result & 0x8000;
				a = result & 0xff;
				y = result >> 8;
				break;
			}
			case 0x7b: { // ror dpx
				spc_ror(spc_adrDpx());
				break;
			}
			case 0x7c: { // rora imp
				spc_read(pc);
				bool newC = a & 1;
				a = (a >> 1) | (c << 7);
				c = newC;
				spc_setZN(a);
				break;
			}
			case 0x7d: { // movax imp
				spc_read(pc);
				a = x;
				spc_setZN(a);
				break;
			}
			case 0x7e: { // cmpy dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_cmpy(adr); step = 0; break;
				}
				break;
			}
			case 0x7f: { // reti imp
				spc_read(pc);
				spc_idle();
				spc_setFlags(spc_pullByte());
				pc = spc_pullWord();
				break;
			}
			case 0x80: { // setc imp
				spc_read(pc);
				c = true;
				break;
			}
			case 0x84: { // adc dp
				spc_adc(spc_adrDp());
				break;
			}
			case 0x85: { // adc abs
				spc_adc(spc_adrAbs());
				break;
			}
			case 0x86: { // adc ind
				spc_adc(spc_adrInd());
				break;
			}
			case 0x87: { // adc idx
				spc_adc(spc_adrIdx());
				break;
			}
			case 0x88: { // adc imm
				spc_adc(spc_adrImm());
				break;
			}
			case 0x89: { // adcm dp, dp
				uint8_t src = 0;
				uint16_t dst = spc_adrDpDp(&src);
				spc_adcm(dst, src);
				break;
			}
			case 0x8a: { // eor1 abs.bit
				uint16_t adr = 0;
				uint8_t bit = spc_adrAbsBit(&adr);
				c = c ^ ((spc_read(adr) >> bit) & 1);
				spc_idle();
				break;
			}
			case 0x8b: { // dec dp
				spc_dec(spc_adrDp());
				break;
			}
			case 0x8c: { // dec abs
				spc_dec(spc_adrAbs());
				break;
			}
			case 0x8d: { // movy imm
				switch (step++) {
					case 1: adr = spc_adrImm(); break;
					case 2: spc_movy(adr); step = 0; break;
				}
				break;
			}
			case 0x8e: { // popp imp
				spc_read(pc);
				spc_idle();
				spc_setFlags(spc_pullByte());
				break;
			}
			case 0x8f: { // movm dp, imm
				switch (step++) {
					case 1: dat = spc_readOpcode(); break;
					case 2: adr = spc_readOpcode() | (p << 8); break;
					case 3: spc_read(adr); break;
					case 4: spc_write(adr, dat); step = 0; break;
				}
				break;
			}
			case 0x90: { // bcc rel
				spc_doBranch(spc_readOpcode(), !c);
				break;
			}
			case 0x94: { // adc dpx
				spc_adc(spc_adrDpx());
				break;
			}
			case 0x95: { // adc abx
				spc_adc(spc_adrAbx());
				break;
			}
			case 0x96: { // adc aby
				spc_adc(spc_adrAby());
				break;
			}
			case 0x97: { // adc idy
				spc_adc(spc_adrIdy());
				break;
			}
			case 0x98: { // adcm dp, imm
				uint8_t src = 0;
				uint16_t dst = spc_adrDpImm(&src);
				spc_adcm(dst, src);
				break;
			}
			case 0x99: { // adcm ind, ind
				uint8_t src = 0;
				uint16_t dst = spc_adrIndInd(&src);
				spc_adcm(dst, src);
				break;
			}
			case 0x9a: { // subw dp
				uint16_t low = 0;
				uint16_t high = spc_adrDpWord(&low);
				uint8_t vall = spc_read(low);
				spc_idle();
				uint16_t value = (vall | (spc_read(high) << 8)) ^ 0xffff;
				uint16_t ya = a | (y << 8);
				int result = ya + value + 1;
				v = (ya & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
				h = ((ya & 0xfff) + (value & 0xfff) + 1) > 0xfff;
				c = result > 0xffff;
				z = (result & 0xffff) == 0;
				n = result & 0x8000;
				a = result & 0xff;
				y = result >> 8;
				break;
			}
			case 0x9b: { // dec dpx
				spc_dec(spc_adrDpx());
				break;
			}
			case 0x9c: { // deca imp
				spc_read(pc);
				a--;
				spc_setZN(a);
				break;
			}
			case 0x9d: { // movxp imp
				spc_read(pc);
				x = sp;
				spc_setZN(x);
				break;
			}
			case 0x9e: { // div imp
				spc_read(pc);
				for(int i = 0; i < 10; i++) spc_idle();
				h = (x & 0xf) <= (y & 0xf);
				int yva = (y << 8) | a;
				int x = this->x << 9;
				for(int i = 0; i < 9; i++) {
					yva <<= 1;
					yva |= (yva & 0x20000) ? 1 : 0;
					yva &= 0x1ffff;
					if(yva >= x) yva ^= 1;
					if(yva & 1) yva -= x;
					yva &= 0x1ffff;
				}
				y = yva >> 9;
				v = yva & 0x100;
				a = yva & 0xff;
				spc_setZN(a);
				break;
			}
			case 0x9f: { // xcn imp
				spc_read(pc);
				spc_idle();
				spc_idle();
				spc_idle();
				a = (a >> 4) | (a << 4);
				spc_setZN(a);
				break;
			}
			case 0xa0: { // ei  imp
				spc_read(pc);
				spc_idle();
				i = true;
				break;
			}
			case 0xa4: { // sbc dp
				spc_sbc(spc_adrDp());
				break;
			}
			case 0xa5: { // sbc abs
				spc_sbc(spc_adrAbs());
				break;
			}
			case 0xa6: { // sbc ind
				spc_sbc(spc_adrInd());
				break;
			}
			case 0xa7: { // sbc idx
				spc_sbc(spc_adrIdx());
				break;
			}
			case 0xa8: { // sbc imm
				spc_sbc(spc_adrImm());
				break;
			}
			case 0xa9: { // sbcm dp, dp
				uint8_t src = 0;
				uint16_t dst = spc_adrDpDp(&src);
				spc_sbcm(dst, src);
				break;
			}
			case 0xaa: { // mov1 abs.bit
				switch (step++) {
					case 1: spc_adrAbsBit_stepped(); break;
					case 2: spc_adrAbsBit_stepped(); break;
					case 3: c = (spc_read(adr) >> dat) & 1; step = 0; break;
				}
				break;
			}
			case 0xab: { // inc dp
				spc_inc(spc_adrDp());
				break;
			}
			case 0xac: { // inc abs
				spc_inc(spc_adrAbs());
				break;
			}
			case 0xad: { // cmpy imm
				spc_cmpy(spc_adrImm());
				break;
			}
			case 0xae: { // popa imp
				spc_read(pc);
				spc_idle();
				a = spc_pullByte();
				break;
			}
			case 0xaf: { // movs ind+
				switch (step++) {
					case 1: adr = spc_adrIndP(); break;
					case 2: spc_idle(); break;
					case 3: spc_write(adr, a); step = 0; break;
				}
				break;
			}
			case 0xb0: { // bcs rel (c)
				switch (step++) {
					case 1: dat = spc_readOpcode(); if (!c) step = 0; break;
					case 2: spc_idle(); break;
					case 3: spc_idle(); pc += (int8_t) dat; step = 0; break;
				}
				break;
			}
			case 0xb4: { // sbc dpx
				spc_sbc(spc_adrDpx());
				break;
			}
			case 0xb5: { // sbc abx
				spc_sbc(spc_adrAbx());
				break;
			}
			case 0xb6: { // sbc aby
				spc_sbc(spc_adrAby());
				break;
			}
			case 0xb7: { // sbc idy
				spc_sbc(spc_adrIdy());
				break;
			}
			case 0xb8: { // sbcm dp, imm
				uint8_t src = 0;
				uint16_t dst = spc_adrDpImm(&src);
				spc_sbcm(dst, src);
				break;
			}
			case 0xb9: { // sbcm ind, ind
				uint8_t src = 0;
				uint16_t dst = spc_adrIndInd(&src);
				spc_sbcm(dst, src);
				break;
			}
			case 0xba: { // movw dp
				switch (step++) {
					case 1: adr = spc_adrDpWord(&adr1); break;
					case 2: dat = spc_read(adr1); break;
					case 3: spc_idle(); break;
					case 4:
						dat16 = dat | (spc_read(adr) << 8);
						a = dat16 & 0xff;
						y = dat16 >> 8;
						z = dat16 == 0;
						n = dat16 & 0x8000;
						step = 0;
						break;
				}
				break;
			}
			case 0xbb: { // inc dpx
				spc_inc(spc_adrDpx());
				break;
			}
			case 0xbc: { // inca imp
				spc_read(pc);
				a++;
				spc_setZN(a);
				break;
			}
			case 0xbd: { // movpx imp
				spc_read(pc);
				sp = x;
				break;
			}
			case 0xbe: { // das imp
				spc_read(pc);
				spc_idle();
				if(a > 0x99 || !c) {
					a -= 0x60;
					c = false;
				}
				if((a & 0xf) > 9 || !h) {
					a -= 6;
				}
				spc_setZN(a);
				break;
			}
			case 0xbf: { // mov ind+
				switch (step++) {
					case 1: adr = spc_adrIndP(); break;
					case 2: a = spc_read(adr); break;
					case 3: spc_idle(); spc_setZN(a); step = 0; break;
				}
				break;
			}
			case 0xc0: { // di  imp
				spc_read(pc);
				spc_idle();
				i = false;
				break;
			}
			case 0xc4: { // movs dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_movs(adr); break;
					case 3: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xc5: { // movs abs
				switch (step++) {
					case 1: spc_adrAbs_stepped(); break;
					case 2: spc_adrAbs_stepped(); break;
					case 3: spc_movs(adr); break;
					case 4: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xc6: { // movs ind
				switch (step++) {
					case 1: adr = spc_adrInd(); break;
					case 2: spc_movs(adr); break;
					case 3: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xc7: { // movs idx
				switch (step++) {
					case 1: spc_adrIdx_stepped(); break;
					case 2: spc_adrIdx_stepped(); break;
					case 3: spc_adrIdx_stepped(); break;
					case 4: spc_adrIdx_stepped(); break;
					case 5: spc_movs(adr); break;
					case 6: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xc8: { // cmpx imm
				spc_cmpx(spc_adrImm());
				break;
			}
			case 0xc9: { // movsx abs
				switch (step++) {
					case 1: spc_adrAbs_stepped(); break;
					case 2: spc_adrAbs_stepped(); break;
					case 3: spc_movsx(adr); break;
					case 4: spc_movsx(adr); step = 0; break;
				}
				break;
			}
			case 0xca: { // mov1s abs.bit
				switch (step++) {
					case 1: spc_adrAbsBit_stepped(); break;
					case 2: spc_adrAbsBit_stepped(); break;
					case 3: dat = (spc_read(adr) & (~(1 << dat))) | (c << dat); break;
					case 4: spc_idle(); break;
					case 5: spc_write(adr, dat); step = 0; break;
				}
				break;
			}
			case 0xcb: { // movsy dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_movsy(adr); break;
					case 3: spc_movsy(adr); step = 0; break;
				}
				break;
			}
			case 0xcc: { // movsy abs
				switch (step++) {
					case 1: spc_adrAbs_stepped(); break;
					case 2: spc_adrAbs_stepped(); break;
					case 3: spc_movsy(adr); break;
					case 4: spc_movsy(adr); step = 0; break;
				}
				break;
			}
			case 0xcd: { // movx imm
				switch (step++) {
					case 1: adr = spc_adrImm(); break;
					case 2: spc_movx(adr); step = 0; break;
				}
				break;
			}
			case 0xce: { // popx imp
				spc_read(pc);
				spc_idle();
				x = spc_pullByte();
				break;
			}
			case 0xcf: { // mul imp
				spc_read(pc);
				for(int i = 0; i < 7; i++) spc_idle();
				uint16_t result = a * y;
				a = result & 0xff;
				y = result >> 8;
				spc_setZN(y);
				break;
			}
			case 0xd0: { // bne rel (!z)
				switch (step++) {
					case 1: dat = spc_readOpcode(); if (z) step = 0; break;
					case 2: spc_idle(); break;
					case 3: spc_idle(); pc += (int8_t) dat; step = 0; break;
				}
				break;
			}
			case 0xd4: { // movs dpx
				switch (step++) {
					case 1: spc_adrDpx_stepped(); break;
					case 2: spc_adrDpx_stepped(); break;
					case 3: spc_movs(adr); break;
					case 4: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xd5: { // movs abx
				switch (step++) {
					case 1: spc_adrAbx_stepped(); break;
					case 2: spc_adrAbx_stepped(); break;
					case 3: spc_adrAbx_stepped(); break;
					case 4: spc_movs(adr); break;
					case 5: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xd6: { // movs aby
				switch (step++) {
					case 1: spc_adrAby_stepped(); break;
					case 2: spc_adrAby_stepped(); break;
					case 3: spc_adrAby_stepped(); break;
					case 4: spc_movs(adr); break;
					case 5: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xd7: { // movs idy
				switch (step++) {
					case 1: spc_adrIdy_stepped(); break;
					case 2: spc_adrIdy_stepped(); break;
					case 3: spc_adrIdy_stepped(); break;
					case 4: spc_adrIdy_stepped(); break;
					case 5: spc_movs(adr); break;
					case 6: spc_movs(adr); step = 0; break;
				}
				break;
			}
			case 0xd8: { // movsx dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_movsx(adr); break;
					case 3: spc_movsx(adr); step = 0; break;
				}
				break;
			}
			case 0xd9: { // movsx dpy
				switch (step++) {
					case 1: spc_adrDpy_stepped(); break;
					case 2: spc_adrDpy_stepped(); break;
					case 3: spc_movsx(adr); break;
					case 4: spc_movsx(adr); step = 0; break;
				}
				break;
			}
			case 0xda: { // movws dp
				switch (step++) {
					case 1: adr = spc_adrDpWord(&adr1); break;
					case 2: spc_read(adr1); break;
					case 3: spc_write(adr1, a); break;
					case 4: spc_write(adr, y); step = 0; break;
				}
				break;
			}
			case 0xdb: { // movsy dpx
				switch (step++) {
					case 1: spc_adrDpx_stepped(); break;
					case 2: spc_adrDpx_stepped(); break;
					case 3: spc_movsy(adr); break;
					case 4: spc_movsy(adr); step = 0; break;
				}
				break;
			}
			case 0xdc: { // decy imp
				spc_read(pc);
				y--;
				spc_setZN(y);
				break;
			}
			case 0xdd: { // movay imp
				spc_read(pc);
				a = y;
				spc_setZN(a);
				break;
			}
			case 0xde: { // cbne dpx, rel
				uint8_t val = spc_read(spc_adrDpx()) ^ 0xff;
				spc_idle();
				uint8_t result = a + val + 1;
				spc_doBranch(spc_readOpcode(), result != 0);
				break;
			}
			case 0xdf: { // daa imp
				spc_read(pc);
				spc_idle();
				if(a > 0x99 || c) {
					a += 0x60;
					c = true;
				}
				if((a & 0xf) > 9 || h) {
					a += 6;
				}
				spc_setZN(a);
				break;
			}
			case 0xe0: { // clrv imp
				spc_read(pc);
				v = false;
				h = false;
				break;
			}
			case 0xe4: { // mov dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xe5: { // mov abs
				switch (step++) {
					case 1: spc_adrAbs_stepped(); break;
					case 2: spc_adrAbs_stepped(); break;
					case 3: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xe6: { // mov ind
				switch (step++) {
					case 1: adr = spc_adrInd(); break;
					case 2: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xe7: { // mov idx
				switch (step++) {
					case 1: spc_adrIdx_stepped(); break;
					case 2: spc_adrIdx_stepped(); break;
					case 3: spc_adrIdx_stepped(); break;
					case 4: spc_adrIdx_stepped(); break;
					case 5: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xe8: { // mov imm
				switch (step++) {
					case 1: adr = spc_adrImm(); break;
					case 2: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xe9: { // movx abs
				switch (step++) {
					case 1: spc_adrAbs_stepped(); break;
					case 2: spc_adrAbs_stepped(); break;
					case 3: spc_movx(adr); step = 0; break;
				}
				break;
			}
			case 0xea: { // not1 abs.bit
				uint16_t adr = 0;
				uint8_t bit = spc_adrAbsBit(&adr);
				uint8_t result = spc_read(adr) ^ (1 << bit);
				spc_write(adr, result);
				break;
			}
			case 0xeb: { // movy dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_movy(adr); step = 0; break;
				}
				break;
			}
			case 0xec: { // movy abs
				switch (step++) {
					case 1: spc_adrAbs_stepped(); break;
					case 2: spc_adrAbs_stepped(); break;
					case 3: spc_movy(adr); step = 0; break;
				}
				break;
		 }
			case 0xed: { // notc imp
				spc_read(pc);
				spc_idle();
				c = !c;
				break;
			}
			case 0xee: { // popy imp
				spc_read(pc);
				spc_idle();
				y = spc_pullByte();
				break;
			}
			case 0xef: { // sleep imp
				spc_read(pc);
				spc_idle();
				stopped = true; // no interrupts, so sleeping stops as well
				break;
			}
			case 0xf0: { // beq rel (z)
				switch (step++) {
					case 1: dat = spc_readOpcode(); if (!z) step = 0; break;
					case 2: spc_idle(); break;
					case 3: spc_idle(); pc += (int8_t) dat; step = 0; break;
				}
				break;
			}
			case 0xf4: { // mov dpx
				switch (step++) {
					case 1: spc_adrDpx_stepped(); break;
					case 2: spc_adrDpx_stepped(); break;
					case 3: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xf5: { // mov abx
				switch (step++) {
					case 1: spc_adrAbx_stepped(); break;
					case 2: spc_adrAbx_stepped(); break;
					case 3: spc_adrAbx_stepped(); break;
					case 4: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xf6: { // mov aby
				switch (step++) {
					case 1: spc_adrAby_stepped(); break;
					case 2: spc_adrAby_stepped(); break;
					case 3: spc_adrAby_stepped(); break;
					case 4: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xf7: { // mov idy
				switch (step++) {
					case 1: spc_adrIdy_stepped(); break;
					case 2: spc_adrIdy_stepped(); break;
					case 3: spc_adrIdy_stepped(); break;
					case 4: spc_adrIdy_stepped(); break;
					case 5: spc_mov(adr); step = 0; break;
				}
				break;
			}
			case 0xf8: { // movx dp
				switch (step++) {
					case 1: adr = spc_adrDp(); break;
					case 2: spc_movx(adr); step = 0; break;
				}
				break;
			}
			case 0xf9: { // movx dpy
				switch (step++) {
					case 1: spc_adrDpy_stepped(); break;
					case 2: spc_adrDpy_stepped(); break;
					case 3: spc_movx(adr); step = 0; break;
				}
				break;
			}
			case 0xfa: { // movm dp, dp
				switch (step++) {
					case 1: spc_adrDpDp_stepped(); break;
					case 2: spc_adrDpDp_stepped(); break;
					case 3: spc_adrDpDp_stepped(); break;
					case 4: spc_write(adr, dat); step = 0; break;
				}
				break;
			}
			case 0xfb: { // movy dpx
				switch (step++) {
					case 1: spc_adrDpx_stepped(); break;
					case 2: spc_adrDpx_stepped(); break;
					case 3: spc_movy(adr); step = 0; break;
				}
				break;
			}
			case 0xfc: { // incy imp
				spc_read(pc);
				y++;
				spc_setZN(y);
				break;
			}
			case 0xfd: { // movya imp
				spc_read(pc);
				y = a;
				spc_setZN(y);
				break;
			}
			case 0xfe: { // dbnzy rel
				switch (step++) {
					case 1: spc_read(pc); break;
					case 2: spc_idle(); y--; break;
					case 3: dat = spc_readOpcode(); if (!(y != 0)) step = 0; break;
					case 4: spc_idle(); break;
					case 5: spc_idle(); pc += (int8_t) dat; step = 0; break;
				}
				break;
			}
			case 0xff: { // stop imp
				spc_read(pc);
				spc_idle();
				stopped = true;
				break;
			}
		}
	}
}
