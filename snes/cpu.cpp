#include "cpu.h"
#include "statehandler.h"
#include "snes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{

	// addressing modes and opcode functions not declared, only used after defintions

	void Cpu::cpu_init(Snes* snes) {
		config.snes = snes;
	}

	void Cpu::cpu_reset(bool hard) {
		if(hard) {
			a = 0;
			x = 0;
			y = 0;
			sp = 0;
			pc = 0;
			dp = 0;
			k = 0;
			db = 0;
			c = false;
			z = false;
			v = false;
			n = false;
			i = false;
			d = false;
			xf = false;
			mf = false;
			e = false;
			irqWanted = false;
		}
		waiting = false;
		stopped = false;
		nmiWanted = false;
		intWanted = false;
		intDelay = false;
		resetWanted = true;
	}

	void Cpu::cpu_handleState(StateHandler* sh) {
		sh_handleBools(sh,
			&c, &z, &v, &n, &i, &d, &xf, &mf, &e, &waiting, &stopped,
			&irqWanted, &nmiWanted, &intWanted, &intDelay, &resetWanted, NULL
		);
		sh_handleBytes(sh, &k, &db, NULL);
		sh_handleWords(sh, &a, &x, &y, &sp, &pc, &dp, NULL);
	}

	void Cpu::cpu_runOpcode() {
		if(resetWanted) {
			resetWanted = false;
			// reset: brk/interrupt without writes
			cpu_read((k << 16) | pc);
			cpu_idle();
			cpu_read(0x100 | (sp-- & 0xff));
			cpu_read(0x100 | (sp-- & 0xff));
			cpu_read(0x100 | (sp-- & 0xff));
			sp = (sp & 0xff) | 0x100;
			e = true;
			i = true;
			d = false;
			cpu_setFlags(cpu_getFlags()); // updates x and m flags, clears upper half of x and y if needed
			k = 0;
			pc = cpu_readWord(0xfffc, 0xfffd, false);
			return;
		}
		if(stopped) {
			cpu_idleWait();
			return;
		}
		if(waiting) {
			if(irqWanted || nmiWanted) {
				waiting = false;
				cpu_idle();
				cpu_checkInt();
				cpu_idle();
				return;
			} else {
				cpu_idleWait();
				return;
			}
		}
		// not stopped or waiting, execute a opcode or go to interrupt
		if(intWanted) {
			cpu_read((k << 16) | pc);
			cpu_doInterrupt();
		} else {
			uint8_t opcode = cpu_readOpcode();
			cpu_doOpcode(opcode);
		}
	}

	void Cpu::cpu_nmi() {
		nmiWanted = true;
	}

	void Cpu::cpu_setIrq(bool state) {
		irqWanted = state;
	}

	uint8_t Cpu::cpu_read(uint32_t adr) {
		intDelay = false;
		return config.snes->snes_cpuRead(adr);
	}

	void Cpu::cpu_write(uint32_t adr, uint8_t val) {
		intDelay = false;
		config.snes->snes_cpuWrite(adr, val);
	}

	void Cpu::cpu_idle() {
		intDelay = false;
		config.snes->snes_cpuIdle(false);
	}

	void Cpu::cpu_idleWait() {
		intDelay = false;
		config.snes->snes_cpuIdle(true);
	}

	void Cpu::cpu_checkInt() {
		intWanted = (nmiWanted || (irqWanted && !i)) && !intDelay;
		intDelay = false;
	}

	uint8_t Cpu::cpu_readOpcode() {
		return cpu_read((k << 16) | pc++);
	}

	uint16_t Cpu::cpu_readOpcodeWord(bool intCheck) {
		uint8_t low = cpu_readOpcode();
		if(intCheck) cpu_checkInt();
		return low | (cpu_readOpcode() << 8);
	}

	uint8_t Cpu::cpu_getFlags() {
		uint8_t val = n << 7;
		val |= v << 6;
		val |= mf << 5;
		val |= xf << 4;
		val |= d << 3;
		val |= i << 2;
		val |= z << 1;
		val |= c;
		return val;
	}

	void Cpu::cpu_setFlags(uint8_t val) {
		n = val & 0x80;
		v = val & 0x40;
		mf = val & 0x20;
		xf = val & 0x10;
		d = val & 8;
		i = val & 4;
		z = val & 2;
		c = val & 1;
		if(e) {
			mf = true;
			xf = true;
			sp = (sp & 0xff) | 0x100;
		}
		if(xf) {
			x &= 0xff;
			y &= 0xff;
		}
	}

	void Cpu::cpu_setZN(uint16_t value, bool byte) {
		if(byte) {
			z = (value & 0xff) == 0;
			n = value & 0x80;
		} else {
			z = value == 0;
			n = value & 0x8000;
		}
	}

	void Cpu::cpu_doBranch(bool check) {
		if(!check) cpu_checkInt();
		uint8_t value = cpu_readOpcode();
		if(check) {
			cpu_checkInt();
			cpu_idle(); // taken branch: 1 extra cycle
			pc += (int8_t) value;
		}
	}

	uint8_t Cpu::cpu_pullByte() {
		sp++;
		if(e) sp = (sp & 0xff) | 0x100;
		return cpu_read(sp);
	}

	void Cpu::cpu_pushByte(uint8_t value) {
		cpu_write(sp, value);
		sp--;
		if(e) sp = (sp & 0xff) | 0x100;
	}

	uint16_t Cpu::cpu_pullWord(bool intCheck) {
		uint8_t value = cpu_pullByte();
		if(intCheck) cpu_checkInt();
		return value | (cpu_pullByte() << 8);
	}

	void Cpu::cpu_pushWord(uint16_t value, bool intCheck) {
		cpu_pushByte(value >> 8);
		if(intCheck) cpu_checkInt();
		cpu_pushByte(value & 0xff);
	}

	uint16_t Cpu::cpu_readWord(uint32_t adrl, uint32_t adrh, bool intCheck) {
		uint8_t value = cpu_read(adrl);
		if(intCheck) cpu_checkInt();
		return value | (cpu_read(adrh) << 8);
	}

	void Cpu::cpu_writeWord(uint32_t adrl, uint32_t adrh, uint16_t value, bool reversed, bool intCheck) {
		if(reversed) {
			cpu_write(adrh, value >> 8);
			if(intCheck) cpu_checkInt();
			cpu_write(adrl, value & 0xff);
		} else {
			cpu_write(adrl, value & 0xff);
			if(intCheck) cpu_checkInt();
			cpu_write(adrh, value >> 8);
		}
	}

	void Cpu::cpu_doInterrupt() {
		cpu_idle();
		cpu_pushByte(k);
		cpu_pushWord(pc, false);
		cpu_pushByte(cpu_getFlags());
		i = true;
		d = false;
		k = 0;
		intWanted = false;
		if(nmiWanted) {
			nmiWanted = false;
			pc = cpu_readWord(0xffea, 0xffeb, false);
		} else { // irq
			pc = cpu_readWord(0xffee, 0xffef, false);
		}
	}

	// addressing modes

	void Cpu::cpu_adrImp() {
		// only for 2-cycle implied opcodes
		cpu_checkInt();
		if(intWanted) {
			// if interrupt detected in 2-cycle implied/accumulator opcode,
			// idle cycle turns into read from pc
			cpu_read((k << 16) | pc);
		} else {
			cpu_idle();
		}
	}

	uint32_t Cpu::cpu_adrImm(uint32_t* low, bool xFlag) {
		if((xFlag && xf) || (!xFlag && mf)) {
			*low = (k << 16) | pc++;
			return 0;
		} else {
			*low = (k << 16) | pc++;
			return (k << 16) | pc++;
		}
	}

	uint32_t Cpu::cpu_adrDp(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		*low = (dp + adr) & 0xffff;
		return (dp + adr + 1) & 0xffff;
	}

	uint32_t Cpu::cpu_adrDpx(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		*low = (dp + adr + x) & 0xffff;
		return (dp + adr + x + 1) & 0xffff;
	}

	uint32_t Cpu::cpu_adrDpy(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		*low = (dp + adr + y) & 0xffff;
		return (dp + adr + y + 1) & 0xffff;
	}

	uint32_t Cpu::cpu_adrIdp(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint16_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		*low = (db << 16) + pointer;
		return ((db << 16) + pointer + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrIdx(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		uint16_t pointer = cpu_readWord((dp + adr + x) & 0xffff, (dp + adr + x + 1) & 0xffff, false);
		*low = (db << 16) + pointer;
		return ((db << 16) + pointer + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrIdy(uint32_t* low, bool write) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint16_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !xf || ((pointer >> 8) != ((pointer + y) >> 8))) cpu_idle();
		*low = ((db << 16) + pointer + y) & 0xffffff;
		return ((db << 16) + pointer + y + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrIdl(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint32_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		pointer |= cpu_read((dp + adr + 2) & 0xffff) << 16;
		*low = pointer;
		return (pointer + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrIly(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint32_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		pointer |= cpu_read((dp + adr + 2) & 0xffff) << 16;
		*low = (pointer + y) & 0xffffff;
		return (pointer + y + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrSr(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		cpu_idle();
		*low = (sp + adr) & 0xffff;
		return (sp + adr + 1) & 0xffff;
	}

	uint32_t Cpu::cpu_adrIsy(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		cpu_idle();
		uint16_t pointer = cpu_readWord((sp + adr) & 0xffff, (sp + adr + 1) & 0xffff, false);
		cpu_idle();
		*low = ((db << 16) + pointer + y) & 0xffffff;
		return ((db << 16) + pointer + y + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrAbs(uint32_t* low) {
		uint16_t adr = cpu_readOpcodeWord(false);
		*low = (db << 16) + adr;
		return ((db << 16) + adr + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrAbx(uint32_t* low, bool write) {
		uint16_t adr = cpu_readOpcodeWord(false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !xf || ((adr >> 8) != ((adr + x) >> 8))) cpu_idle();
		*low = ((db << 16) + adr + x) & 0xffffff;
		return ((db << 16) + adr + x + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrAby(uint32_t* low, bool write) {
		uint16_t adr = cpu_readOpcodeWord(false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !xf || ((adr >> 8) != ((adr + y) >> 8))) cpu_idle();
		*low = ((db << 16) + adr + y) & 0xffffff;
		return ((db << 16) + adr + y + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrAbl(uint32_t* low) {
		uint32_t adr = cpu_readOpcodeWord(false);
		adr |= cpu_readOpcode() << 16;
		*low = adr;
		return (adr + 1) & 0xffffff;
	}

	uint32_t Cpu::cpu_adrAlx(uint32_t* low) {
		uint32_t adr = cpu_readOpcodeWord(false);
		adr |= cpu_readOpcode() << 16;
		*low = (adr + x) & 0xffffff;
		return (adr + x + 1) & 0xffffff;
	}

	// opcode functions

	void Cpu::cpu_and(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			a = (a & 0xff00) | ((a & value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			a &= value;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_ora(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			a = (a & 0xff00) | ((a | value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			a |= value;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_eor(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			a = (a & 0xff00) | ((a ^ value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			a ^= value;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_adc(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			int result = 0;
			if(d) {
				result = (a & 0xf) + (value & 0xf) + c;
				if(result > 0x9) result = ((result + 0x6) & 0xf) + 0x10;
				result = (a & 0xf0) + (value & 0xf0) + result;
			} else {
				result = (a & 0xff) + value + c;
			}
			v = (a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
			if(d && result > 0x9f) result += 0x60;
			c = result > 0xff;
			a = (a & 0xff00) | (result & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			int result = 0;
			if(d) {
				result = (a & 0xf) + (value & 0xf) + c;
				if(result > 0x9) result = ((result + 0x6) & 0xf) + 0x10;
				result = (a & 0xf0) + (value & 0xf0) + result;
				if(result > 0x9f) result = ((result + 0x60) & 0xff) + 0x100;
				result = (a & 0xf00) + (value & 0xf00) + result;
				if(result > 0x9ff) result = ((result + 0x600) & 0xfff) + 0x1000;
				result = (a & 0xf000) + (value & 0xf000) + result;
			} else {
				result = a + value + c;
			}
			v = (a & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
			if(d && result > 0x9fff) result += 0x6000;
			c = result > 0xffff;
			a = result;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_sbc(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			int result = 0;
			if(d) {
				result = (a & 0xf) + (value & 0xf) + c;
				if(result < 0x10) result = (result - 0x6) & ((result - 0x6 < 0) ? 0xf : 0x1f);
				result = (a & 0xf0) + (value & 0xf0) + result;
			} else {
				result = (a & 0xff) + value + c;
			}
			v = (a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
			if(d && result < 0x100) result -= 0x60;
			c = result > 0xff;
			a = (a & 0xff00) | (result & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			int result = 0;
			if(d) {
				result = (a & 0xf) + (value & 0xf) + c;
				if(result < 0x10) result = (result - 0x6) & ((result - 0x6 < 0) ? 0xf : 0x1f);
				result = (a & 0xf0) + (value & 0xf0) + result;
				if(result < 0x100) result = (result - 0x60) & ((result - 0x60 < 0) ? 0xff : 0x1ff);
				result = (a & 0xf00) + (value & 0xf00) + result;
				if(result < 0x1000) result = (result - 0x600) & ((result - 0x600 < 0) ? 0xfff : 0x1fff);
				result = (a & 0xf000) + (value & 0xf000) + result;
			} else {
				result = a + value + c;
			}
			v = (a & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
			if(d && result < 0x10000) result -= 0x6000;
			c = result > 0xffff;
			a = result;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_cmp(uint32_t low, uint32_t high) {
		int result = 0;
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			result = (a & 0xff) + value + 1;
			c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			result = a + value + 1;
			c = result > 0xffff;
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_cpx(uint32_t low, uint32_t high) {
		int result = 0;
		if(xf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			result = (x & 0xff) + value + 1;
			c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			result = x + value + 1;
			c = result > 0xffff;
		}
		cpu_setZN(result, xf);
	}

	void Cpu::cpu_cpy(uint32_t low, uint32_t high) {
		int result = 0;
		if(xf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			result = (y & 0xff) + value + 1;
			c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			result = y + value + 1;
			c = result > 0xffff;
		}
		cpu_setZN(result, xf);
	}

	void Cpu::cpu_bit(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			uint8_t result = (a & 0xff) & value;
			z = result == 0;
			n = value & 0x80;
			v = value & 0x40;
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			uint16_t result = a & value;
			z = result == 0;
			n = value & 0x8000;
			v = value & 0x4000;
		}
	}

	void Cpu::cpu_lda(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			a = (a & 0xff00) | cpu_read(low);
		} else {
			a = cpu_readWord(low, high, true);
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_ldx(uint32_t low, uint32_t high) {
		if(xf) {
			cpu_checkInt();
			x = cpu_read(low);
		} else {
			x = cpu_readWord(low, high, true);
		}
		cpu_setZN(x, xf);
	}

	void Cpu::cpu_ldy(uint32_t low, uint32_t high) {
		if(xf) {
			cpu_checkInt();
			y = cpu_read(low);
		} else {
			y = cpu_readWord(low, high, true);
		}
		cpu_setZN(y, xf);
	}

	void Cpu::cpu_sta(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			cpu_write(low, a);
		} else {
			cpu_writeWord(low, high, a, false, true);
		}
	}

	void Cpu::cpu_stx(uint32_t low, uint32_t high) {
		if(xf) {
			cpu_checkInt();
			cpu_write(low, x);
		} else {
			cpu_writeWord(low, high, x, false, true);
		}
	}

	void Cpu::cpu_sty(uint32_t low, uint32_t high) {
		if(xf) {
			cpu_checkInt();
			cpu_write(low, y);
		} else {
			cpu_writeWord(low, high, y, false, true);
		}
	}

	void Cpu::cpu_stz(uint32_t low, uint32_t high) {
		if(mf) {
			cpu_checkInt();
			cpu_write(low, 0);
		} else {
			cpu_writeWord(low, high, 0, false, true);
		}
	}

	void Cpu::cpu_ror(uint32_t low, uint32_t high) {
		bool carry = false;
		int result = 0;
		if(mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			carry = value & 1;
			result = (value >> 1) | (c << 7);
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			carry = value & 1;
			result = (value >> 1) | (c << 15);
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, mf);
		c = carry;
	}

	void Cpu::cpu_rol(uint32_t low, uint32_t high) {
		int result = 0;
		if(mf) {
			result = (cpu_read(low) << 1) | c;
			cpu_idle();
			c = result & 0x100;
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = (cpu_readWord(low, high, false) << 1) | c;
			cpu_idle();
			c = result & 0x10000;
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_lsr(uint32_t low, uint32_t high) {
		int result = 0;
		if(mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			c = value & 1;
			result = value >> 1;
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			c = value & 1;
			result = value >> 1;
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_asl(uint32_t low, uint32_t high) {
		int result = 0;
		if(mf) {
			result = cpu_read(low) << 1;
			cpu_idle();
			c = result & 0x100;
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = cpu_readWord(low, high, false) << 1;
			cpu_idle();
			c = result & 0x10000;
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_inc(uint32_t low, uint32_t high) {
		int result = 0;
		if(mf) {
			result = cpu_read(low) + 1;
			cpu_idle();
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = cpu_readWord(low, high, false) + 1;
			cpu_idle();
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_dec(uint32_t low, uint32_t high) {
		int result = 0;
		if(mf) {
			result = cpu_read(low) - 1;
			cpu_idle();
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = cpu_readWord(low, high, false) - 1;
			cpu_idle();
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_tsb(uint32_t low, uint32_t high) {
		if(mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			z = ((a & 0xff) & value) == 0;
			cpu_checkInt();
			cpu_write(low, value | (a & 0xff));
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			z = (a & value) == 0;
			cpu_writeWord(low, high, value | a, true, true);
		}
	}

	void Cpu::cpu_trb(uint32_t low, uint32_t high) {
		if(mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			z = ((a & 0xff) & value) == 0;
			cpu_checkInt();
			cpu_write(low, value & ~(a & 0xff));
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			z = (a & value) == 0;
			cpu_writeWord(low, high, value & ~a, true, true);
		}
	}

	void Cpu::cpu_doOpcode(uint8_t opcode) {
		switch(opcode) {
			case 0x00: { // brk imm(s)
				uint32_t vector = (e) ? 0xfffe : 0xffe6;
				cpu_readOpcode();
				if (!e) cpu_pushByte(k);
				cpu_pushWord(pc, false);
				cpu_pushByte(cpu_getFlags());
				i = true;
				d = false;
				k = 0;
				pc = cpu_readWord(vector, vector + 1, true);
				break;
			}
			case 0x01: { // ora idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x02: { // cop imm(s)
				uint32_t vector = (e) ? 0xfff4 : 0xffe4;
				cpu_readOpcode();
				if (!e) cpu_pushByte(k);
				cpu_pushWord(pc, false);
				cpu_pushByte(cpu_getFlags());
				i = true;
				d = false;
				k = 0;
				pc = cpu_readWord(vector, vector + 1, true);
				break;
			}
			case 0x03: { // ora sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x04: { // tsb dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_tsb(low, high);
				break;
			}
			case 0x05: { // ora dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x06: { // asl dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_asl(low, high);
				break;
			}
			case 0x07: { // ora idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x08: { // php imp
				cpu_idle();
				cpu_checkInt();
				cpu_pushByte(cpu_getFlags());
				break;
			}
			case 0x09: { // ora imm(m)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, false);
				cpu_ora(low, high);
				break;
			}
			case 0x0a: { // asla imp
				cpu_adrImp();
				if(mf) {
					c = a & 0x80;
					a = (a & 0xff00) | ((a << 1) & 0xff);
				} else {
					c = a & 0x8000;
					a <<= 1;
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x0b: { // phd imp
				cpu_idle();
				cpu_pushWord(dp, true);
				break;
			}
			case 0x0c: { // tsb abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_tsb(low, high);
				break;
			}
			case 0x0d: { // ora abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x0e: { // asl abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_asl(low, high);
				break;
			}
			case 0x0f: { // ora abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x10: { // bpl rel
				cpu_doBranch(!n);
				break;
			}
			case 0x11: { // ora idy(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, false);
				cpu_ora(low, high);
				break;
			}
			case 0x12: { // ora idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x13: { // ora isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x14: { // trb dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_trb(low, high);
				break;
			}
			case 0x15: { // ora dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x16: { // asl dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_asl(low, high);
				break;
			}
			case 0x17: { // ora ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x18: { // clc imp
				cpu_adrImp();
				c = false;
				break;
			}
			case 0x19: { // ora aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_ora(low, high);
				break;
			}
			case 0x1a: { // inca imp
				cpu_adrImp();
				if(mf) {
					a = (a & 0xff00) | ((a + 1) & 0xff);
				} else {
					a++;
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x1b: { // tcs imp
				cpu_adrImp();
				sp = (e) ? (a & 0xff) | 0x100 : a;
				break;
			}
			case 0x1c: { // trb abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_trb(low, high);
				break;
			}
			case 0x1d: { // ora abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_ora(low, high);
				break;
			}
			case 0x1e: { // asl abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_asl(low, high);
				break;
			}
			case 0x1f: { // ora alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x20: { // jsr abs
				uint16_t value = cpu_readOpcodeWord(false);
				cpu_idle();
				cpu_pushWord(pc - 1, true);
				pc = value;
				break;
			}
			case 0x21: { // and idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_and(low, high);
				break;
			}
			case 0x22: { // jsl abl
				uint16_t value = cpu_readOpcodeWord(false);
				cpu_pushByte(k);
				cpu_idle();
				uint8_t newK = cpu_readOpcode();
				cpu_pushWord(pc - 1, true);
				pc = value;
				k = newK;
				break;
			}
			case 0x23: { // and sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_and(low, high);
				break;
			}
			case 0x24: { // bit dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_bit(low, high);
				break;
			}
			case 0x25: { // and dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_and(low, high);
				break;
			}
			case 0x26: { // rol dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_rol(low, high);
				break;
			}
			case 0x27: { // and idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_and(low, high);
				break;
			}
			case 0x28: { // plp imp
				cpu_idle();
				cpu_idle();
				cpu_checkInt();
				cpu_setFlags(cpu_pullByte());
				break;
			}
			case 0x29: { // and imm(m)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, false);
				cpu_and(low, high);
				break;
			}
			case 0x2a: { // rola imp
				cpu_adrImp();
				int result = (a << 1) | c;
				if(mf) {
					c = result & 0x100;
					a = (a & 0xff00) | (result & 0xff);
				} else {
					c = result & 0x10000;
					a = result;
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x2b: { // pld imp
				cpu_idle();
				cpu_idle();
				dp = cpu_pullWord(true);
				cpu_setZN(dp, false);
				break;
			}
			case 0x2c: { // bit abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_bit(low, high);
				break;
			}
			case 0x2d: { // and abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_and(low, high);
				break;
			}
			case 0x2e: { // rol abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_rol(low, high);
				break;
			}
			case 0x2f: { // and abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_and(low, high);
				break;
			}
			case 0x30: { // bmi rel
				cpu_doBranch(n);
				break;
			}
			case 0x31: { // and idy(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, false);
				cpu_and(low, high);
				break;
			}
			case 0x32: { // and idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_and(low, high);
				break;
			}
			case 0x33: { // and isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_and(low, high);
				break;
			}
			case 0x34: { // bit dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_bit(low, high);
				break;
			}
			case 0x35: { // and dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_and(low, high);
				break;
			}
			case 0x36: { // rol dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_rol(low, high);
				break;
			}
			case 0x37: { // and ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_and(low, high);
				break;
			}
			case 0x38: { // sec imp
				cpu_adrImp();
				c = true;
				break;
			}
			case 0x39: { // and aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_and(low, high);
				break;
			}
			case 0x3a: { // deca imp
				cpu_adrImp();
				if(mf) {
					a = (a & 0xff00) | ((a - 1) & 0xff);
				} else {
					a--;
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x3b: { // tsc imp
				cpu_adrImp();
				a = sp;
				cpu_setZN(a, false);
				break;
			}
			case 0x3c: { // bit abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_bit(low, high);
				break;
			}
			case 0x3d: { // and abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_and(low, high);
				break;
			}
			case 0x3e: { // rol abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_rol(low, high);
				break;
			}
			case 0x3f: { // and alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_and(low, high);
				break;
			}
			case 0x40: { // rti imp
				cpu_idle();
				cpu_idle();
				cpu_setFlags(cpu_pullByte());
				pc = cpu_pullWord(false);
				cpu_checkInt();
				k = cpu_pullByte();
				break;
			}
			case 0x41: { // eor idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x42: { // wdm imm(s)
				cpu_checkInt();
				cpu_readOpcode();
				break;
			}
			case 0x43: { // eor sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x44: { // mvp bm
				uint8_t dest = cpu_readOpcode();
				uint8_t src = cpu_readOpcode();
				db = dest;
				cpu_write((dest << 16) | y, cpu_read((src << 16) | x));
				a--;
				x--;
				y--;
				if(a != 0xffff) {
					pc -= 3;
				}
				if(xf) {
					x &= 0xff;
					y &= 0xff;
				}
				cpu_idle();
				cpu_checkInt();
				cpu_idle();
				break;
			}
			case 0x45: { // eor dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x46: { // lsr dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_lsr(low, high);
				break;
			}
			case 0x47: { // eor idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x48: { // pha imp
				cpu_idle();
				if(mf) {
					cpu_checkInt();
					cpu_pushByte(a);
				} else {
					cpu_pushWord(a, true);
				}
				break;
			}
			case 0x49: { // eor imm(m)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, false);
				cpu_eor(low, high);
				break;
			}
			case 0x4a: { // lsra imp
				cpu_adrImp();
				c = a & 1;
				if(mf) {
					a = (a & 0xff00) | ((a >> 1) & 0x7f);
				} else {
					a >>= 1;
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x4b: { // phk imp
				cpu_idle();
				cpu_checkInt();
				cpu_pushByte(k);
				break;
			}
			case 0x4c: { // jmp abs
				pc = cpu_readOpcodeWord(true);
				break;
			}
			case 0x4d: { // eor abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x4e: { // lsr abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_lsr(low, high);
				break;
			}
			case 0x4f: { // eor abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x50: { // bvc rel
				cpu_doBranch(!v);
				break;
			}
			case 0x51: { // eor idy(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, false);
				cpu_eor(low, high);
				break;
			}
			case 0x52: { // eor idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x53: { // eor isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x54: { // mvn bm
				uint8_t dest = cpu_readOpcode();
				uint8_t src = cpu_readOpcode();
				db = dest;
				cpu_write((dest << 16) | y, cpu_read((src << 16) | x));
				a--;
				x++;
				y++;
				if(a != 0xffff) {
					pc -= 3;
				}
				if(xf) {
					x &= 0xff;
					y &= 0xff;
				}
				cpu_idle();
				cpu_checkInt();
				cpu_idle();
				break;
			}
			case 0x55: { // eor dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x56: { // lsr dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_lsr(low, high);
				break;
			}
			case 0x57: { // eor ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x58: { // cli imp
				cpu_adrImp();
				i = false;
				break;
			}
			case 0x59: { // eor aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_eor(low, high);
				break;
			}
			case 0x5a: { // phy imp
				cpu_idle();
				if(xf) {
					cpu_checkInt();
					cpu_pushByte(y);
				} else {
					cpu_pushWord(y, true);
				}
				break;
			}
			case 0x5b: { // tcd imp
				cpu_adrImp();
				dp = a;
				cpu_setZN(dp, false);
				break;
			}
			case 0x5c: { // jml abl
				uint16_t value = cpu_readOpcodeWord(false);
				cpu_checkInt();
				k = cpu_readOpcode();
				pc = value;
				break;
			}
			case 0x5d: { // eor abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_eor(low, high);
				break;
			}
			case 0x5e: { // lsr abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_lsr(low, high);
				break;
			}
			case 0x5f: { // eor alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_eor(low, high);
				break;
			}
			case 0x60: { // rts imp
				cpu_idle();
				cpu_idle();
				pc = cpu_pullWord(false) + 1;
				cpu_checkInt();
				cpu_idle();
				break;
			}
			case 0x61: { // adc idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x62: { // per rll
				uint16_t value = cpu_readOpcodeWord(false);
				cpu_idle();
				cpu_pushWord(pc + (int16_t) value, true);
				break;
			}
			case 0x63: { // adc sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x64: { // stz dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_stz(low, high);
				break;
			}
			case 0x65: { // adc dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x66: { // ror dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_ror(low, high);
				break;
			}
			case 0x67: { // adc idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x68: { // pla imp
				cpu_idle();
				cpu_idle();
				if(mf) {
					cpu_checkInt();
					a = (a & 0xff00) | cpu_pullByte();
				} else {
					a = cpu_pullWord(true);
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x69: { // adc imm(m)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, false);
				cpu_adc(low, high);
				break;
			}
			case 0x6a: { // rora imp
				cpu_adrImp();
				bool carry = a & 1;
				if(mf) {
					a = (a & 0xff00) | ((a >> 1) & 0x7f) | (c << 7);
				} else {
					a = (a >> 1) | (c << 15);
				}
				c = carry;
				cpu_setZN(a, mf);
				break;
			}
			case 0x6b: { // rtl imp
				cpu_idle();
				cpu_idle();
				pc = cpu_pullWord(false) + 1;
				cpu_checkInt();
				k = cpu_pullByte();
				break;
			}
			case 0x6c: { // jmp ind
				uint16_t adr = cpu_readOpcodeWord(false);
				pc = cpu_readWord(adr, (adr + 1) & 0xffff, true);
				break;
			}
			case 0x6d: { // adc abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x6e: { // ror abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_ror(low, high);
				break;
			}
			case 0x6f: { // adc abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x70: { // bvs rel
				cpu_doBranch(v);
				break;
			}
			case 0x71: { // adc idy(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, false);
				cpu_adc(low, high);
				break;
			}
			case 0x72: { // adc idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x73: { // adc isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x74: { // stz dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_stz(low, high);
				break;
			}
			case 0x75: { // adc dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x76: { // ror dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_ror(low, high);
				break;
			}
			case 0x77: { // adc ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x78: { // sei imp
				cpu_adrImp();
				i = true;
				break;
			}
			case 0x79: { // adc aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_adc(low, high);
				break;
			}
			case 0x7a: { // ply imp
				cpu_idle();
				cpu_idle();
				if(xf) {
					cpu_checkInt();
					y = cpu_pullByte();
				} else {
					y = cpu_pullWord(true);
				}
				cpu_setZN(y, xf);
				break;
			}
			case 0x7b: { // tdc imp
				cpu_adrImp();
				a = dp;
				cpu_setZN(a, false);
				break;
			}
			case 0x7c: { // jmp iax
				uint16_t adr = cpu_readOpcodeWord(false);
				cpu_idle();
				pc = cpu_readWord((k << 16) | ((adr + x) & 0xffff), (k << 16) | ((adr + x + 1) & 0xffff), true);
				break;
			}
			case 0x7d: { // adc abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_adc(low, high);
				break;
			}
			case 0x7e: { // ror abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_ror(low, high);
				break;
			}
			case 0x7f: { // adc alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_adc(low, high);
				break;
			}
			case 0x80: { // bra rel
				cpu_doBranch(true);
				break;
			}
			case 0x81: { // sta idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x82: { // brl rll
				pc += (int16_t) cpu_readOpcodeWord(false);
				cpu_checkInt();
				cpu_idle();
				break;
			}
			case 0x83: { // sta sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x84: { // sty dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_sty(low, high);
				break;
			}
			case 0x85: { // sta dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x86: { // stx dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_stx(low, high);
				break;
			}
			case 0x87: { // sta idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x88: { // dey imp
				cpu_adrImp();
				if(xf) {
					y = (y - 1) & 0xff;
				} else {
					y--;
				}
				cpu_setZN(y, xf);
				break;
			}
			case 0x89: { // biti imm(m)
				if(mf) {
					cpu_checkInt();
					uint8_t result = (a & 0xff) & cpu_readOpcode();
					z = result == 0;
				} else {
					uint16_t result = a & cpu_readOpcodeWord(true);
					z = result == 0;
				}
				break;
			}
			case 0x8a: { // txa imp
				cpu_adrImp();
				if(mf) {
					a = (a & 0xff00) | (x & 0xff);
				} else {
					a = x;
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x8b: { // phb imp
				cpu_idle();
				cpu_checkInt();
				cpu_pushByte(db);
				break;
			}
			case 0x8c: { // sty abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_sty(low, high);
				break;
			}
			case 0x8d: { // sta abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x8e: { // stx abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_stx(low, high);
				break;
			}
			case 0x8f: { // sta abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x90: { // bcc rel
				cpu_doBranch(!c);
				break;
			}
			case 0x91: { // sta idy
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, true);
				cpu_sta(low, high);
				break;
			}
			case 0x92: { // sta idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x93: { // sta isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x94: { // sty dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_sty(low, high);
				break;
			}
			case 0x95: { // sta dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x96: { // stx dpy
				uint32_t low = 0;
				uint32_t high = cpu_adrDpy(&low);
				cpu_stx(low, high);
				break;
			}
			case 0x97: { // sta ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_sta(low, high);
				break;
			}
			case 0x98: { // tya imp
				cpu_adrImp();
				if(mf) {
					a = (a & 0xff00) | (y & 0xff);
				} else {
					a = y;
				}
				cpu_setZN(a, mf);
				break;
			}
			case 0x99: { // sta aby
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, true);
				cpu_sta(low, high);
				break;
			}
			case 0x9a: { // txs imp
				cpu_adrImp();
				sp = (e) ? (x & 0xff) | 0x100 : x;
				break;
			}
			case 0x9b: { // txy imp
				cpu_adrImp();
				if(xf) {
					y = x & 0xff;
				} else {
					y = x;
				}
				cpu_setZN(y, xf);
				break;
			}
			case 0x9c: { // stz abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_stz(low, high);
				break;
			}
			case 0x9d: { // sta abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_sta(low, high);
				break;
			}
			case 0x9e: { // stz abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_stz(low, high);
				break;
			}
			case 0x9f: { // sta alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_sta(low, high);
				break;
			}
			case 0xa0: { // ldy imm(x)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, true);
				cpu_ldy(low, high);
				break;
			}
			case 0xa1: { // lda idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xa2: { // ldx imm(x)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, true);
				cpu_ldx(low, high);
				break;
			}
			case 0xa3: { // lda sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xa4: { // ldy dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_ldy(low, high);
				break;
			}
			case 0xa5: { // lda dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xa6: { // ldx dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_ldx(low, high);
				break;
			}
			case 0xa7: { // lda idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xa8: { // tay imp
				cpu_adrImp();
				if(xf) {
					y = a & 0xff;
				} else {
					y = a;
				}
				cpu_setZN(y, xf);
				break;
			}
			case 0xa9: { // lda imm(m)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, false);
				cpu_lda(low, high);
				break;
			}
			case 0xaa: { // tax imp
				cpu_adrImp();
				if(xf) {
					x = a & 0xff;
				} else {
					x = a;
				}
				cpu_setZN(x, xf);
				break;
			}
			case 0xab: { // plb imp
				cpu_idle();
				cpu_idle();
				cpu_checkInt();
				db = cpu_pullByte();
				cpu_setZN(db, true);
				break;
			}
			case 0xac: { // ldy abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_ldy(low, high);
				break;
			}
			case 0xad: { // lda abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xae: { // ldx abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_ldx(low, high);
				break;
			}
			case 0xaf: { // lda abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xb0: { // bcs rel
				cpu_doBranch(c);
				break;
			}
			case 0xb1: { // lda idy(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, false);
				cpu_lda(low, high);
				break;
			}
			case 0xb2: { // lda idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xb3: { // lda isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xb4: { // ldy dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_ldy(low, high);
				break;
			}
			case 0xb5: { // lda dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xb6: { // ldx dpy
				uint32_t low = 0;
				uint32_t high = cpu_adrDpy(&low);
				cpu_ldx(low, high);
				break;
			}
			case 0xb7: { // lda ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xb8: { // clv imp
				cpu_adrImp();
				v = false;
				break;
			}
			case 0xb9: { // lda aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_lda(low, high);
				break;
			}
			case 0xba: { // tsx imp
				cpu_adrImp();
				if(xf) {
					x = sp & 0xff;
				} else {
					x = sp;
				}
				cpu_setZN(x, xf);
				break;
			}
			case 0xbb: { // tyx imp
				cpu_adrImp();
				if(xf) {
					x = y & 0xff;
				} else {
					x = y;
				}
				cpu_setZN(x, xf);
				break;
			}
			case 0xbc: { // ldy abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_ldy(low, high);
				break;
			}
			case 0xbd: { // lda abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_lda(low, high);
				break;
			}
			case 0xbe: { // ldx aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_ldx(low, high);
				break;
			}
			case 0xbf: { // lda alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_lda(low, high);
				break;
			}
			case 0xc0: { // cpy imm(x)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, true);
				cpu_cpy(low, high);
				break;
			}
			case 0xc1: { // cmp idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xc2: { // rep imm(s)
				uint8_t val = cpu_readOpcode();
				cpu_checkInt();
				cpu_setFlags(cpu_getFlags() & ~val);
				cpu_idle();
				break;
			}
			case 0xc3: { // cmp sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xc4: { // cpy dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_cpy(low, high);
				break;
			}
			case 0xc5: { // cmp dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xc6: { // dec dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_dec(low, high);
				break;
			}
			case 0xc7: { // cmp idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xc8: { // iny imp
				cpu_adrImp();
				if(xf) {
					y = (y + 1) & 0xff;
				} else {
					y++;
				}
				cpu_setZN(y, xf);
				break;
			}
			case 0xc9: { // cmp imm(m)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, false);
				cpu_cmp(low, high);
				break;
			}
			case 0xca: { // dex imp
				cpu_adrImp();
				if(xf) {
					x = (x - 1) & 0xff;
				} else {
					x--;
				}
				cpu_setZN(x, xf);
				break;
			}
			case 0xcb: { // wai imp
				waiting = true;
				cpu_idle();
				cpu_idle();
				break;
			}
			case 0xcc: { // cpy abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_cpy(low, high);
				break;
			}
			case 0xcd: { // cmp abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xce: { // dec abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_dec(low, high);
				break;
			}
			case 0xcf: { // cmp abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xd0: { // bne rel
				cpu_doBranch(!z);
				break;
			}
			case 0xd1: { // cmp idy(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, false);
				cpu_cmp(low, high);
				break;
			}
			case 0xd2: { // cmp idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xd3: { // cmp isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xd4: { // pei dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_pushWord(cpu_readWord(low, high, false), true);
				break;
			}
			case 0xd5: { // cmp dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xd6: { // dec dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_dec(low, high);
				break;
			}
			case 0xd7: { // cmp ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xd8: { // cld imp
				cpu_adrImp();
				d = false;
				break;
			}
			case 0xd9: { // cmp aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_cmp(low, high);
				break;
			}
			case 0xda: { // phx imp
				cpu_idle();
				if(xf) {
					cpu_checkInt();
					cpu_pushByte(x);
				} else {
					cpu_pushWord(x, true);
				}
				break;
			}
			case 0xdb: { // stp imp
				stopped = true;
				cpu_idle();
				cpu_idle();
				break;
			}
			case 0xdc: { // jml ial
				uint16_t adr = cpu_readOpcodeWord(false);
				pc = cpu_readWord(adr, (adr + 1) & 0xffff, false);
				cpu_checkInt();
				k = cpu_read((adr + 2) & 0xffff);
				break;
			}
			case 0xdd: { // cmp abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_cmp(low, high);
				break;
			}
			case 0xde: { // dec abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_dec(low, high);
				break;
			}
			case 0xdf: { // cmp alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_cmp(low, high);
				break;
			}
			case 0xe0: { // cpx imm(x)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, true);
				cpu_cpx(low, high);
				break;
			}
			case 0xe1: { // sbc idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xe2: { // sep imm(s)
				uint8_t val = cpu_readOpcode();
				cpu_checkInt();
				cpu_setFlags(cpu_getFlags() | val);
				cpu_idle();
				break;
			}
			case 0xe3: { // sbc sr
				uint32_t low = 0;
				uint32_t high = cpu_adrSr(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xe4: { // cpx dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_cpx(low, high);
				break;
			}
			case 0xe5: { // sbc dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xe6: { // inc dp
				uint32_t low = 0;
				uint32_t high = cpu_adrDp(&low);
				cpu_inc(low, high);
				break;
			}
			case 0xe7: { // sbc idl
				uint32_t low = 0;
				uint32_t high = cpu_adrIdl(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xe8: { // inx imp
				cpu_adrImp();
				if(xf) {
					x = (x + 1) & 0xff;
				} else {
					x++;
				}
				cpu_setZN(x, xf);
				break;
			}
			case 0xe9: { // sbc imm(m)
				uint32_t low = 0;
				uint32_t high = cpu_adrImm(&low, false);
				cpu_sbc(low, high);
				break;
			}
			case 0xea: { // nop imp
				cpu_adrImp();
				// no operation
				break;
			}
			case 0xeb: { // xba imp
				uint8_t low = a & 0xff;
				uint8_t high = a >> 8;
				a = (low << 8) | high;
				cpu_setZN(high, true);
				cpu_idle();
				cpu_checkInt();
				cpu_idle();
				break;
			}
			case 0xec: { // cpx abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_cpx(low, high);
				break;
			}
			case 0xed: { // sbc abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xee: { // inc abs
				uint32_t low = 0;
				uint32_t high = cpu_adrAbs(&low);
				cpu_inc(low, high);
				break;
			}
			case 0xef: { // sbc abl
				uint32_t low = 0;
				uint32_t high = cpu_adrAbl(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xf0: { // beq rel
				cpu_doBranch(z);
				break;
			}
			case 0xf1: { // sbc idy(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrIdy(&low, false);
				cpu_sbc(low, high);
				break;
			}
			case 0xf2: { // sbc idp
				uint32_t low = 0;
				uint32_t high = cpu_adrIdp(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xf3: { // sbc isy
				uint32_t low = 0;
				uint32_t high = cpu_adrIsy(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xf4: { // pea imm(l)
				cpu_pushWord(cpu_readOpcodeWord(false), true);
				break;
			}
			case 0xf5: { // sbc dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xf6: { // inc dpx
				uint32_t low = 0;
				uint32_t high = cpu_adrDpx(&low);
				cpu_inc(low, high);
				break;
			}
			case 0xf7: { // sbc ily
				uint32_t low = 0;
				uint32_t high = cpu_adrIly(&low);
				cpu_sbc(low, high);
				break;
			}
			case 0xf8: { // sed imp
				cpu_adrImp();
				d = true;
				break;
			}
			case 0xf9: { // sbc aby(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAby(&low, false);
				cpu_sbc(low, high);
				break;
			}
			case 0xfa: { // plx imp
				cpu_idle();
				cpu_idle();
				if(xf) {
					cpu_checkInt();
					x = cpu_pullByte();
				} else {
					x = cpu_pullWord(true);
				}
				cpu_setZN(x, xf);
				break;
			}
			case 0xfb: { // xce imp
				cpu_adrImp();
				bool temp = c;
				c = e;
				e = temp;
				cpu_setFlags(cpu_getFlags()); // updates x and m flags, clears upper half of x and y if needed
				break;
			}
			case 0xfc: { // jsr iax
				uint8_t adrl = cpu_readOpcode();
				cpu_pushWord(pc, false);
				uint16_t adr = adrl | (cpu_readOpcode() << 8);
				cpu_idle();
				uint16_t value = cpu_readWord((k << 16) | ((adr + x) & 0xffff), (k << 16) | ((adr + x + 1) & 0xffff), true);
				pc = value;
				break;
			}
			case 0xfd: { // sbc abx(r)
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, false);
				cpu_sbc(low, high);
				break;
			}
			case 0xfe: { // inc abx
				uint32_t low = 0;
				uint32_t high = cpu_adrAbx(&low, true);
				cpu_inc(low, high);
				break;
			}
			case 0xff: { // sbc alx
				uint32_t low = 0;
				uint32_t high = cpu_adrAlx(&low);
				cpu_sbc(low, high);
				break;
			}
		}
	}
}