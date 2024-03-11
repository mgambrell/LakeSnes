#include "cpu.h"
#include "statehandler.h"
#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace LakeSnes
{
	static uint8_t cpu_read(uint32_t adr);
	static void cpu_write(uint32_t adr, uint8_t val);
	static void cpu_idle();
	static void cpu_idleWait();
	static void cpu_checkInt();
	static uint8_t cpu_readOpcode();
	static uint16_t cpu_readOpcodeWord(bool intCheck);
	static uint8_t cpu_getFlags();
	static void cpu_setFlags(uint8_t value);
	static void cpu_setZN(uint16_t value, bool byte);
	static void cpu_doBranch(bool check);
	static uint8_t cpu_pullByte();
	static void cpu_pushByte(uint8_t value);
	static uint16_t cpu_pullWord(bool intCheck);
	static void cpu_pushWord(uint16_t value, bool intCheck);
	static uint16_t cpu_readWord(uint32_t adrl, uint32_t adrh, bool intCheck);
	static void cpu_writeWord(uint32_t adrl, uint32_t adrh, uint16_t value, bool reversed, bool intCheck);
	static void cpu_doInterrupt();
	static void cpu_doOpcode(uint8_t opcode);

	// addressing modes and opcode functions not declared, only used after defintions

	Cpu* cpu_init(CpuReadHandler read, CpuWriteHandler write, CpuIdleHandler idle) {
		cpu->read = read;
		cpu->write = write;
		cpu->idle = idle;
		return cpu;
	}

	void cpu_reset(bool hard) {
		if(hard) {
			cpu->a = 0;
			cpu->x = 0;
			cpu->y = 0;
			cpu->sp = 0;
			cpu->pc = 0;
			cpu->dp = 0;
			cpu->k = 0;
			cpu->db = 0;
			cpu->c = false;
			cpu->z = false;
			cpu->v = false;
			cpu->n = false;
			cpu->i = false;
			cpu->d = false;
			cpu->xf = false;
			cpu->mf = false;
			cpu->e = false;
			cpu->irqWanted = false;
		}
		cpu->waiting = false;
		cpu->stopped = false;
		cpu->nmiWanted = false;
		cpu->intWanted = false;
		cpu->intDelay = false;
		cpu->resetWanted = true;
	}

	void cpu_handleState(StateHandler* sh) {
		sh_handleBools(sh,
			&cpu->c, &cpu->z, &cpu->v, &cpu->n, &cpu->i, &cpu->d, &cpu->xf, &cpu->mf, &cpu->e, &cpu->waiting, &cpu->stopped,
			&cpu->irqWanted, &cpu->nmiWanted, &cpu->intWanted, &cpu->intDelay, &cpu->resetWanted, NULL
		);
		sh_handleBytes(sh, &cpu->k, &cpu->db, NULL);
		sh_handleWords(sh, &cpu->a, &cpu->x, &cpu->y, &cpu->sp, &cpu->pc, &cpu->dp, NULL);
	}

	void cpu_runOpcode() {
		if(cpu->resetWanted) {
			cpu->resetWanted = false;
			// reset: brk/interrupt without writes
			cpu_read((cpu->k << 16) | cpu->pc);
			cpu_idle();
			cpu_read(0x100 | (cpu->sp-- & 0xff));
			cpu_read(0x100 | (cpu->sp-- & 0xff));
			cpu_read(0x100 | (cpu->sp-- & 0xff));
			cpu->sp = (cpu->sp & 0xff) | 0x100;
			cpu->e = true;
			cpu->i = true;
			cpu->d = false;
			cpu_setFlags(cpu_getFlags()); // updates x and m flags, clears upper half of x and y if needed
			cpu->k = 0;
			cpu->pc = cpu_readWord(0xfffc, 0xfffd, false);
			return;
		}
		if(cpu->stopped) {
			cpu_idleWait();
			return;
		}
		if(cpu->waiting) {
			if(cpu->irqWanted || cpu->nmiWanted) {
				cpu->waiting = false;
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
		if(cpu->intWanted) {
			cpu_read((cpu->k << 16) | cpu->pc);
			cpu_doInterrupt();
		} else {
			uint8_t opcode = cpu_readOpcode();
			cpu_doOpcode(opcode);
		}
	}

	void cpu_nmi() {
		cpu->nmiWanted = true;
	}

	void cpu_setIrq(bool state) {
		cpu->irqWanted = state;
	}

	static uint8_t cpu_read(uint32_t adr) {
		cpu->intDelay = false;
		return cpu->read(adr);
	}

	static void cpu_write(uint32_t adr, uint8_t val) {
		cpu->intDelay = false;
		cpu->write(adr, val);
	}

	static void cpu_idle() {
		cpu->intDelay = false;
		cpu->idle(false);
	}

	static void cpu_idleWait() {
		cpu->intDelay = false;
		cpu->idle(true);
	}

	static void cpu_checkInt() {
		cpu->intWanted = (cpu->nmiWanted || (cpu->irqWanted && !cpu->i)) && !cpu->intDelay;
		cpu->intDelay = false;
	}

	static uint8_t cpu_readOpcode() {
		return cpu_read((cpu->k << 16) | cpu->pc++);
	}

	static uint16_t cpu_readOpcodeWord(bool intCheck) {
		uint8_t low = cpu_readOpcode();
		if(intCheck) cpu_checkInt();
		return low | (cpu_readOpcode() << 8);
	}

	static uint8_t cpu_getFlags() {
		uint8_t val = cpu->n << 7;
		val |= cpu->v << 6;
		val |= cpu->mf << 5;
		val |= cpu->xf << 4;
		val |= cpu->d << 3;
		val |= cpu->i << 2;
		val |= cpu->z << 1;
		val |= cpu->c;
		return val;
	}

	static void cpu_setFlags(uint8_t val) {
		cpu->n = val & 0x80;
		cpu->v = val & 0x40;
		cpu->mf = val & 0x20;
		cpu->xf = val & 0x10;
		cpu->d = val & 8;
		cpu->i = val & 4;
		cpu->z = val & 2;
		cpu->c = val & 1;
		if(cpu->e) {
			cpu->mf = true;
			cpu->xf = true;
			cpu->sp = (cpu->sp & 0xff) | 0x100;
		}
		if(cpu->xf) {
			cpu->x &= 0xff;
			cpu->y &= 0xff;
		}
	}

	static void cpu_setZN(uint16_t value, bool byte) {
		if(byte) {
			cpu->z = (value & 0xff) == 0;
			cpu->n = value & 0x80;
		} else {
			cpu->z = value == 0;
			cpu->n = value & 0x8000;
		}
	}

	static void cpu_doBranch(bool check) {
		if(!check) cpu_checkInt();
		uint8_t value = cpu_readOpcode();
		if(check) {
			cpu_checkInt();
			cpu_idle(); // taken branch: 1 extra cycle
			cpu->pc += (int8_t) value;
		}
	}

	static uint8_t cpu_pullByte() {
		cpu->sp++;
		if(cpu->e) cpu->sp = (cpu->sp & 0xff) | 0x100;
		return cpu_read(cpu->sp);
	}

	static void cpu_pushByte(uint8_t value) {
		cpu_write(cpu->sp, value);
		cpu->sp--;
		if(cpu->e) cpu->sp = (cpu->sp & 0xff) | 0x100;
	}

	static uint16_t cpu_pullWord(bool intCheck) {
		uint8_t value = cpu_pullByte();
		if(intCheck) cpu_checkInt();
		return value | (cpu_pullByte() << 8);
	}

	static void cpu_pushWord(uint16_t value, bool intCheck) {
		cpu_pushByte(value >> 8);
		if(intCheck) cpu_checkInt();
		cpu_pushByte(value & 0xff);
	}

	static uint16_t cpu_readWord(uint32_t adrl, uint32_t adrh, bool intCheck) {
		uint8_t value = cpu_read(adrl);
		if(intCheck) cpu_checkInt();
		return value | (cpu_read(adrh) << 8);
	}

	static void cpu_writeWord(uint32_t adrl, uint32_t adrh, uint16_t value, bool reversed, bool intCheck) {
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

	static void cpu_doInterrupt() {
		cpu_idle();
		cpu_pushByte(cpu->k);
		cpu_pushWord(cpu->pc, false);
		cpu_pushByte(cpu_getFlags());
		cpu->i = true;
		cpu->d = false;
		cpu->k = 0;
		cpu->intWanted = false;
		if(cpu->nmiWanted) {
			cpu->nmiWanted = false;
			cpu->pc = cpu_readWord(0xffea, 0xffeb, false);
		} else { // irq
			cpu->pc = cpu_readWord(0xffee, 0xffef, false);
		}
	}

	// addressing modes

	static void cpu_adrImp() {
		// only for 2-cycle implied opcodes
		cpu_checkInt();
		if(cpu->intWanted) {
			// if interrupt detected in 2-cycle implied/accumulator opcode,
			// idle cycle turns into read from pc
			cpu_read((cpu->k << 16) | cpu->pc);
		} else {
			cpu_idle();
		}
	}

	static uint32_t cpu_adrImm(uint32_t* low, bool xFlag) {
		if((xFlag && cpu->xf) || (!xFlag && cpu->mf)) {
			*low = (cpu->k << 16) | cpu->pc++;
			return 0;
		} else {
			*low = (cpu->k << 16) | cpu->pc++;
			return (cpu->k << 16) | cpu->pc++;
		}
	}

	static uint32_t cpu_adrDp(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		*low = (cpu->dp + adr) & 0xffff;
		return (cpu->dp + adr + 1) & 0xffff;
	}

	static uint32_t cpu_adrDpx(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		*low = (cpu->dp + adr + cpu->x) & 0xffff;
		return (cpu->dp + adr + cpu->x + 1) & 0xffff;
	}

	static uint32_t cpu_adrDpy(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		*low = (cpu->dp + adr + cpu->y) & 0xffff;
		return (cpu->dp + adr + cpu->y + 1) & 0xffff;
	}

	static uint32_t cpu_adrIdp(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint16_t pointer = cpu_readWord((cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
		*low = (cpu->db << 16) + pointer;
		return ((cpu->db << 16) + pointer + 1) & 0xffffff;
	}

	static uint32_t cpu_adrIdx(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		uint16_t pointer = cpu_readWord((cpu->dp + adr + cpu->x) & 0xffff, (cpu->dp + adr + cpu->x + 1) & 0xffff, false);
		*low = (cpu->db << 16) + pointer;
		return ((cpu->db << 16) + pointer + 1) & 0xffffff;
	}

	static uint32_t cpu_adrIdy(uint32_t* low, bool write) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint16_t pointer = cpu_readWord((cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !cpu->xf || ((pointer >> 8) != ((pointer + cpu->y) >> 8))) cpu_idle();
		*low = ((cpu->db << 16) + pointer + cpu->y) & 0xffffff;
		return ((cpu->db << 16) + pointer + cpu->y + 1) & 0xffffff;
	}

	static uint32_t cpu_adrIdl(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint32_t pointer = cpu_readWord((cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
		pointer |= cpu_read((cpu->dp + adr + 2) & 0xffff) << 16;
		*low = pointer;
		return (pointer + 1) & 0xffffff;
	}

	static uint32_t cpu_adrIly(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		if(cpu->dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint32_t pointer = cpu_readWord((cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
		pointer |= cpu_read((cpu->dp + adr + 2) & 0xffff) << 16;
		*low = (pointer + cpu->y) & 0xffffff;
		return (pointer + cpu->y + 1) & 0xffffff;
	}

	static uint32_t cpu_adrSr(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		cpu_idle();
		*low = (cpu->sp + adr) & 0xffff;
		return (cpu->sp + adr + 1) & 0xffff;
	}

	static uint32_t cpu_adrIsy(uint32_t* low) {
		uint8_t adr = cpu_readOpcode();
		cpu_idle();
		uint16_t pointer = cpu_readWord((cpu->sp + adr) & 0xffff, (cpu->sp + adr + 1) & 0xffff, false);
		cpu_idle();
		*low = ((cpu->db << 16) + pointer + cpu->y) & 0xffffff;
		return ((cpu->db << 16) + pointer + cpu->y + 1) & 0xffffff;
	}

	static uint32_t cpu_adrAbs(uint32_t* low) {
		uint16_t adr = cpu_readOpcodeWord(false);
		*low = (cpu->db << 16) + adr;
		return ((cpu->db << 16) + adr + 1) & 0xffffff;
	}

	static uint32_t cpu_adrAbx(uint32_t* low, bool write) {
		uint16_t adr = cpu_readOpcodeWord(false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !cpu->xf || ((adr >> 8) != ((adr + cpu->x) >> 8))) cpu_idle();
		*low = ((cpu->db << 16) + adr + cpu->x) & 0xffffff;
		return ((cpu->db << 16) + adr + cpu->x + 1) & 0xffffff;
	}

	static uint32_t cpu_adrAby(uint32_t* low, bool write) {
		uint16_t adr = cpu_readOpcodeWord(false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !cpu->xf || ((adr >> 8) != ((adr + cpu->y) >> 8))) cpu_idle();
		*low = ((cpu->db << 16) + adr + cpu->y) & 0xffffff;
		return ((cpu->db << 16) + adr + cpu->y + 1) & 0xffffff;
	}

	static uint32_t cpu_adrAbl(uint32_t* low) {
		uint32_t adr = cpu_readOpcodeWord(false);
		adr |= cpu_readOpcode() << 16;
		*low = adr;
		return (adr + 1) & 0xffffff;
	}

	static uint32_t cpu_adrAlx(uint32_t* low) {
		uint32_t adr = cpu_readOpcodeWord(false);
		adr |= cpu_readOpcode() << 16;
		*low = (adr + cpu->x) & 0xffffff;
		return (adr + cpu->x + 1) & 0xffffff;
	}

	// opcode functions

	static void cpu_and(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			cpu->a = (cpu->a & 0xff00) | ((cpu->a & value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			cpu->a &= value;
		}
		cpu_setZN(cpu->a, cpu->mf);
	}

	static void cpu_ora(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			cpu->a = (cpu->a & 0xff00) | ((cpu->a | value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			cpu->a |= value;
		}
		cpu_setZN(cpu->a, cpu->mf);
	}

	static void cpu_eor(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			cpu->a = (cpu->a & 0xff00) | ((cpu->a ^ value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			cpu->a ^= value;
		}
		cpu_setZN(cpu->a, cpu->mf);
	}

	static void cpu_adc(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			int result = 0;
			if(cpu->d) {
				result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
				if(result > 0x9) result = ((result + 0x6) & 0xf) + 0x10;
				result = (cpu->a & 0xf0) + (value & 0xf0) + result;
			} else {
				result = (cpu->a & 0xff) + value + cpu->c;
			}
			cpu->v = (cpu->a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
			if(cpu->d && result > 0x9f) result += 0x60;
			cpu->c = result > 0xff;
			cpu->a = (cpu->a & 0xff00) | (result & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			int result = 0;
			if(cpu->d) {
				result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
				if(result > 0x9) result = ((result + 0x6) & 0xf) + 0x10;
				result = (cpu->a & 0xf0) + (value & 0xf0) + result;
				if(result > 0x9f) result = ((result + 0x60) & 0xff) + 0x100;
				result = (cpu->a & 0xf00) + (value & 0xf00) + result;
				if(result > 0x9ff) result = ((result + 0x600) & 0xfff) + 0x1000;
				result = (cpu->a & 0xf000) + (value & 0xf000) + result;
			} else {
				result = cpu->a + value + cpu->c;
			}
			cpu->v = (cpu->a & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
			if(cpu->d && result > 0x9fff) result += 0x6000;
			cpu->c = result > 0xffff;
			cpu->a = result;
		}
		cpu_setZN(cpu->a, cpu->mf);
	}

	static void cpu_sbc(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			int result = 0;
			if(cpu->d) {
				result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
				if(result < 0x10) result = (result - 0x6) & ((result - 0x6 < 0) ? 0xf : 0x1f);
				result = (cpu->a & 0xf0) + (value & 0xf0) + result;
			} else {
				result = (cpu->a & 0xff) + value + cpu->c;
			}
			cpu->v = (cpu->a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
			if(cpu->d && result < 0x100) result -= 0x60;
			cpu->c = result > 0xff;
			cpu->a = (cpu->a & 0xff00) | (result & 0xff);
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			int result = 0;
			if(cpu->d) {
				result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
				if(result < 0x10) result = (result - 0x6) & ((result - 0x6 < 0) ? 0xf : 0x1f);
				result = (cpu->a & 0xf0) + (value & 0xf0) + result;
				if(result < 0x100) result = (result - 0x60) & ((result - 0x60 < 0) ? 0xff : 0x1ff);
				result = (cpu->a & 0xf00) + (value & 0xf00) + result;
				if(result < 0x1000) result = (result - 0x600) & ((result - 0x600 < 0) ? 0xfff : 0x1fff);
				result = (cpu->a & 0xf000) + (value & 0xf000) + result;
			} else {
				result = cpu->a + value + cpu->c;
			}
			cpu->v = (cpu->a & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
			if(cpu->d && result < 0x10000) result -= 0x6000;
			cpu->c = result > 0xffff;
			cpu->a = result;
		}
		cpu_setZN(cpu->a, cpu->mf);
	}

	static void cpu_cmp(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			result = (cpu->a & 0xff) + value + 1;
			cpu->c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			result = cpu->a + value + 1;
			cpu->c = result > 0xffff;
		}
		cpu_setZN(result, cpu->mf);
	}

	static void cpu_cpx(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->xf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			result = (cpu->x & 0xff) + value + 1;
			cpu->c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			result = cpu->x + value + 1;
			cpu->c = result > 0xffff;
		}
		cpu_setZN(result, cpu->xf);
	}

	static void cpu_cpy(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->xf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low) ^ 0xff;
			result = (cpu->y & 0xff) + value + 1;
			cpu->c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(low, high, true) ^ 0xffff;
			result = cpu->y + value + 1;
			cpu->c = result > 0xffff;
		}
		cpu_setZN(result, cpu->xf);
	}

	static void cpu_bit(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(low);
			uint8_t result = (cpu->a & 0xff) & value;
			cpu->z = result == 0;
			cpu->n = value & 0x80;
			cpu->v = value & 0x40;
		} else {
			uint16_t value = cpu_readWord(low, high, true);
			uint16_t result = cpu->a & value;
			cpu->z = result == 0;
			cpu->n = value & 0x8000;
			cpu->v = value & 0x4000;
		}
	}

	static void cpu_lda(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			cpu->a = (cpu->a & 0xff00) | cpu_read(low);
		} else {
			cpu->a = cpu_readWord(low, high, true);
		}
		cpu_setZN(cpu->a, cpu->mf);
	}

	static void cpu_ldx(uint32_t low, uint32_t high) {
		if(cpu->xf) {
			cpu_checkInt();
			cpu->x = cpu_read(low);
		} else {
			cpu->x = cpu_readWord(low, high, true);
		}
		cpu_setZN(cpu->x, cpu->xf);
	}

	static void cpu_ldy(uint32_t low, uint32_t high) {
		if(cpu->xf) {
			cpu_checkInt();
			cpu->y = cpu_read(low);
		} else {
			cpu->y = cpu_readWord(low, high, true);
		}
		cpu_setZN(cpu->y, cpu->xf);
	}

	static void cpu_sta(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			cpu_write(low, cpu->a);
		} else {
			cpu_writeWord(low, high, cpu->a, false, true);
		}
	}

	static void cpu_stx(uint32_t low, uint32_t high) {
		if(cpu->xf) {
			cpu_checkInt();
			cpu_write(low, cpu->x);
		} else {
			cpu_writeWord(low, high, cpu->x, false, true);
		}
	}

	static void cpu_sty(uint32_t low, uint32_t high) {
		if(cpu->xf) {
			cpu_checkInt();
			cpu_write(low, cpu->y);
		} else {
			cpu_writeWord(low, high, cpu->y, false, true);
		}
	}

	static void cpu_stz(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			cpu_checkInt();
			cpu_write(low, 0);
		} else {
			cpu_writeWord(low, high, 0, false, true);
		}
	}

	static void cpu_ror(uint32_t low, uint32_t high) {
		bool carry = false;
		int result = 0;
		if(cpu->mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			carry = value & 1;
			result = (value >> 1) | (cpu->c << 7);
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			carry = value & 1;
			result = (value >> 1) | (cpu->c << 15);
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, cpu->mf);
		cpu->c = carry;
	}

	static void cpu_rol(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->mf) {
			result = (cpu_read(low) << 1) | cpu->c;
			cpu_idle();
			cpu->c = result & 0x100;
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = (cpu_readWord(low, high, false) << 1) | cpu->c;
			cpu_idle();
			cpu->c = result & 0x10000;
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, cpu->mf);
	}

	static void cpu_lsr(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			cpu->c = value & 1;
			result = value >> 1;
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			cpu->c = value & 1;
			result = value >> 1;
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, cpu->mf);
	}

	static void cpu_asl(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->mf) {
			result = cpu_read(low) << 1;
			cpu_idle();
			cpu->c = result & 0x100;
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = cpu_readWord(low, high, false) << 1;
			cpu_idle();
			cpu->c = result & 0x10000;
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, cpu->mf);
	}

	static void cpu_inc(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->mf) {
			result = cpu_read(low) + 1;
			cpu_idle();
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = cpu_readWord(low, high, false) + 1;
			cpu_idle();
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, cpu->mf);
	}

	static void cpu_dec(uint32_t low, uint32_t high) {
		int result = 0;
		if(cpu->mf) {
			result = cpu_read(low) - 1;
			cpu_idle();
			cpu_checkInt();
			cpu_write(low, result);
		} else {
			result = cpu_readWord(low, high, false) - 1;
			cpu_idle();
			cpu_writeWord(low, high, result, true, true);
		}
		cpu_setZN(result, cpu->mf);
	}

	static void cpu_tsb(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			cpu->z = ((cpu->a & 0xff) & value) == 0;
			cpu_checkInt();
			cpu_write(low, value | (cpu->a & 0xff));
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			cpu->z = (cpu->a & value) == 0;
			cpu_writeWord(low, high, value | cpu->a, true, true);
		}
	}

	static void cpu_trb(uint32_t low, uint32_t high) {
		if(cpu->mf) {
			uint8_t value = cpu_read(low);
			cpu_idle();
			cpu->z = ((cpu->a & 0xff) & value) == 0;
			cpu_checkInt();
			cpu_write(low, value & ~(cpu->a & 0xff));
		} else {
			uint16_t value = cpu_readWord(low, high, false);
			cpu_idle();
			cpu->z = (cpu->a & value) == 0;
			cpu_writeWord(low, high, value & ~cpu->a, true, true);
		}
	}

	static void cpu_doOpcode(uint8_t opcode) {
		switch(opcode) {
			case 0x00: { // brk imm(s)
				uint32_t vector = (cpu->e) ? 0xfffe : 0xffe6;
				cpu_readOpcode();
				if (!cpu->e) cpu_pushByte(cpu->k);
				cpu_pushWord(cpu->pc, false);
				cpu_pushByte(cpu_getFlags());
				cpu->i = true;
				cpu->d = false;
				cpu->k = 0;
				cpu->pc = cpu_readWord(vector, vector + 1, true);
				break;
			}
			case 0x01: { // ora idx
				uint32_t low = 0;
				uint32_t high = cpu_adrIdx(&low);
				cpu_ora(low, high);
				break;
			}
			case 0x02: { // cop imm(s)
				uint32_t vector = (cpu->e) ? 0xfff4 : 0xffe4;
				cpu_readOpcode();
				if (!cpu->e) cpu_pushByte(cpu->k);
				cpu_pushWord(cpu->pc, false);
				cpu_pushByte(cpu_getFlags());
				cpu->i = true;
				cpu->d = false;
				cpu->k = 0;
				cpu->pc = cpu_readWord(vector, vector + 1, true);
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
				if(cpu->mf) {
					cpu->c = cpu->a & 0x80;
					cpu->a = (cpu->a & 0xff00) | ((cpu->a << 1) & 0xff);
				} else {
					cpu->c = cpu->a & 0x8000;
					cpu->a <<= 1;
				}
				cpu_setZN(cpu->a, cpu->mf);
				break;
			}
			case 0x0b: { // phd imp
				cpu_idle();
				cpu_pushWord(cpu->dp, true);
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
				cpu_doBranch(!cpu->n);
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
				cpu->c = false;
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
				if(cpu->mf) {
					cpu->a = (cpu->a & 0xff00) | ((cpu->a + 1) & 0xff);
				} else {
					cpu->a++;
				}
				cpu_setZN(cpu->a, cpu->mf);
				break;
			}
			case 0x1b: { // tcs imp
				cpu_adrImp();
				cpu->sp = (cpu->e) ? (cpu->a & 0xff) | 0x100 : cpu->a;
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
				cpu_pushWord(cpu->pc - 1, true);
				cpu->pc = value;
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
				cpu_pushByte(cpu->k);
				cpu_idle();
				uint8_t newK = cpu_readOpcode();
				cpu_pushWord(cpu->pc - 1, true);
				cpu->pc = value;
				cpu->k = newK;
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
				int result = (cpu->a << 1) | cpu->c;
				if(cpu->mf) {
					cpu->c = result & 0x100;
					cpu->a = (cpu->a & 0xff00) | (result & 0xff);
				} else {
					cpu->c = result & 0x10000;
					cpu->a = result;
				}
				cpu_setZN(cpu->a, cpu->mf);
				break;
			}
			case 0x2b: { // pld imp
				cpu_idle();
				cpu_idle();
				cpu->dp = cpu_pullWord(true);
				cpu_setZN(cpu->dp, false);
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
				cpu_doBranch(cpu->n);
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
				cpu->c = true;
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
				if(cpu->mf) {
					cpu->a = (cpu->a & 0xff00) | ((cpu->a - 1) & 0xff);
				} else {
					cpu->a--;
				}
				cpu_setZN(cpu->a, cpu->mf);
				break;
			}
			case 0x3b: { // tsc imp
				cpu_adrImp();
				cpu->a = cpu->sp;
				cpu_setZN(cpu->a, false);
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
				cpu->pc = cpu_pullWord(false);
				cpu_checkInt();
				cpu->k = cpu_pullByte();
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
				cpu->db = dest;
				cpu_write((dest << 16) | cpu->y, cpu_read((src << 16) | cpu->x));
				cpu->a--;
				cpu->x--;
				cpu->y--;
				if(cpu->a != 0xffff) {
					cpu->pc -= 3;
				}
				if(cpu->xf) {
					cpu->x &= 0xff;
					cpu->y &= 0xff;
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
				if(cpu->mf) {
					cpu_checkInt();
					cpu_pushByte(cpu->a);
				} else {
					cpu_pushWord(cpu->a, true);
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
				cpu->c = cpu->a & 1;
				if(cpu->mf) {
					cpu->a = (cpu->a & 0xff00) | ((cpu->a >> 1) & 0x7f);
				} else {
					cpu->a >>= 1;
				}
				cpu_setZN(cpu->a, cpu->mf);
				break;
			}
			case 0x4b: { // phk imp
				cpu_idle();
				cpu_checkInt();
				cpu_pushByte(cpu->k);
				break;
			}
			case 0x4c: { // jmp abs
				cpu->pc = cpu_readOpcodeWord(true);
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
				cpu_doBranch(!cpu->v);
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
				cpu->db = dest;
				cpu_write((dest << 16) | cpu->y, cpu_read((src << 16) | cpu->x));
				cpu->a--;
				cpu->x++;
				cpu->y++;
				if(cpu->a != 0xffff) {
					cpu->pc -= 3;
				}
				if(cpu->xf) {
					cpu->x &= 0xff;
					cpu->y &= 0xff;
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
				cpu->i = false;
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
				if(cpu->xf) {
					cpu_checkInt();
					cpu_pushByte(cpu->y);
				} else {
					cpu_pushWord(cpu->y, true);
				}
				break;
			}
			case 0x5b: { // tcd imp
				cpu_adrImp();
				cpu->dp = cpu->a;
				cpu_setZN(cpu->dp, false);
				break;
			}
			case 0x5c: { // jml abl
				uint16_t value = cpu_readOpcodeWord(false);
				cpu_checkInt();
				cpu->k = cpu_readOpcode();
				cpu->pc = value;
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
				cpu->pc = cpu_pullWord(false) + 1;
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
				cpu_pushWord(cpu->pc + (int16_t) value, true);
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
				if(cpu->mf) {
					cpu_checkInt();
					cpu->a = (cpu->a & 0xff00) | cpu_pullByte();
				} else {
					cpu->a = cpu_pullWord(true);
				}
				cpu_setZN(cpu->a, cpu->mf);
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
				bool carry = cpu->a & 1;
				if(cpu->mf) {
					cpu->a = (cpu->a & 0xff00) | ((cpu->a >> 1) & 0x7f) | (cpu->c << 7);
				} else {
					cpu->a = (cpu->a >> 1) | (cpu->c << 15);
				}
				cpu->c = carry;
				cpu_setZN(cpu->a, cpu->mf);
				break;
			}
			case 0x6b: { // rtl imp
				cpu_idle();
				cpu_idle();
				cpu->pc = cpu_pullWord(false) + 1;
				cpu_checkInt();
				cpu->k = cpu_pullByte();
				break;
			}
			case 0x6c: { // jmp ind
				uint16_t adr = cpu_readOpcodeWord(false);
				cpu->pc = cpu_readWord(adr, (adr + 1) & 0xffff, true);
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
				cpu_doBranch(cpu->v);
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
				cpu->i = true;
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
				if(cpu->xf) {
					cpu_checkInt();
					cpu->y = cpu_pullByte();
				} else {
					cpu->y = cpu_pullWord(true);
				}
				cpu_setZN(cpu->y, cpu->xf);
				break;
			}
			case 0x7b: { // tdc imp
				cpu_adrImp();
				cpu->a = cpu->dp;
				cpu_setZN(cpu->a, false);
				break;
			}
			case 0x7c: { // jmp iax
				uint16_t adr = cpu_readOpcodeWord(false);
				cpu_idle();
				cpu->pc = cpu_readWord((cpu->k << 16) | ((adr + cpu->x) & 0xffff), (cpu->k << 16) | ((adr + cpu->x + 1) & 0xffff), true);
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
				cpu->pc += (int16_t) cpu_readOpcodeWord(false);
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
				if(cpu->xf) {
					cpu->y = (cpu->y - 1) & 0xff;
				} else {
					cpu->y--;
				}
				cpu_setZN(cpu->y, cpu->xf);
				break;
			}
			case 0x89: { // biti imm(m)
				if(cpu->mf) {
					cpu_checkInt();
					uint8_t result = (cpu->a & 0xff) & cpu_readOpcode();
					cpu->z = result == 0;
				} else {
					uint16_t result = cpu->a & cpu_readOpcodeWord(true);
					cpu->z = result == 0;
				}
				break;
			}
			case 0x8a: { // txa imp
				cpu_adrImp();
				if(cpu->mf) {
					cpu->a = (cpu->a & 0xff00) | (cpu->x & 0xff);
				} else {
					cpu->a = cpu->x;
				}
				cpu_setZN(cpu->a, cpu->mf);
				break;
			}
			case 0x8b: { // phb imp
				cpu_idle();
				cpu_checkInt();
				cpu_pushByte(cpu->db);
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
				cpu_doBranch(!cpu->c);
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
				if(cpu->mf) {
					cpu->a = (cpu->a & 0xff00) | (cpu->y & 0xff);
				} else {
					cpu->a = cpu->y;
				}
				cpu_setZN(cpu->a, cpu->mf);
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
				cpu->sp = (cpu->e) ? (cpu->x & 0xff) | 0x100 : cpu->x;
				break;
			}
			case 0x9b: { // txy imp
				cpu_adrImp();
				if(cpu->xf) {
					cpu->y = cpu->x & 0xff;
				} else {
					cpu->y = cpu->x;
				}
				cpu_setZN(cpu->y, cpu->xf);
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
				if(cpu->xf) {
					cpu->y = cpu->a & 0xff;
				} else {
					cpu->y = cpu->a;
				}
				cpu_setZN(cpu->y, cpu->xf);
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
				if(cpu->xf) {
					cpu->x = cpu->a & 0xff;
				} else {
					cpu->x = cpu->a;
				}
				cpu_setZN(cpu->x, cpu->xf);
				break;
			}
			case 0xab: { // plb imp
				cpu_idle();
				cpu_idle();
				cpu_checkInt();
				cpu->db = cpu_pullByte();
				cpu_setZN(cpu->db, true);
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
				cpu_doBranch(cpu->c);
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
				cpu->v = false;
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
				if(cpu->xf) {
					cpu->x = cpu->sp & 0xff;
				} else {
					cpu->x = cpu->sp;
				}
				cpu_setZN(cpu->x, cpu->xf);
				break;
			}
			case 0xbb: { // tyx imp
				cpu_adrImp();
				if(cpu->xf) {
					cpu->x = cpu->y & 0xff;
				} else {
					cpu->x = cpu->y;
				}
				cpu_setZN(cpu->x, cpu->xf);
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
				if(cpu->xf) {
					cpu->y = (cpu->y + 1) & 0xff;
				} else {
					cpu->y++;
				}
				cpu_setZN(cpu->y, cpu->xf);
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
				if(cpu->xf) {
					cpu->x = (cpu->x - 1) & 0xff;
				} else {
					cpu->x--;
				}
				cpu_setZN(cpu->x, cpu->xf);
				break;
			}
			case 0xcb: { // wai imp
				cpu->waiting = true;
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
				cpu_doBranch(!cpu->z);
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
				cpu->d = false;
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
				if(cpu->xf) {
					cpu_checkInt();
					cpu_pushByte(cpu->x);
				} else {
					cpu_pushWord(cpu->x, true);
				}
				break;
			}
			case 0xdb: { // stp imp
				cpu->stopped = true;
				cpu_idle();
				cpu_idle();
				break;
			}
			case 0xdc: { // jml ial
				uint16_t adr = cpu_readOpcodeWord(false);
				cpu->pc = cpu_readWord(adr, (adr + 1) & 0xffff, false);
				cpu_checkInt();
				cpu->k = cpu_read((adr + 2) & 0xffff);
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
				if(cpu->xf) {
					cpu->x = (cpu->x + 1) & 0xff;
				} else {
					cpu->x++;
				}
				cpu_setZN(cpu->x, cpu->xf);
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
				uint8_t low = cpu->a & 0xff;
				uint8_t high = cpu->a >> 8;
				cpu->a = (low << 8) | high;
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
				cpu_doBranch(cpu->z);
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
				cpu->d = true;
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
				if(cpu->xf) {
					cpu_checkInt();
					cpu->x = cpu_pullByte();
				} else {
					cpu->x = cpu_pullWord(true);
				}
				cpu_setZN(cpu->x, cpu->xf);
				break;
			}
			case 0xfb: { // xce imp
				cpu_adrImp();
				bool temp = cpu->c;
				cpu->c = cpu->e;
				cpu->e = temp;
				cpu_setFlags(cpu_getFlags()); // updates x and m flags, clears upper half of x and y if needed
				break;
			}
			case 0xfc: { // jsr iax
				uint8_t adrl = cpu_readOpcode();
				cpu_pushWord(cpu->pc, false);
				uint16_t adr = adrl | (cpu_readOpcode() << 8);
				cpu_idle();
				uint16_t value = cpu_readWord((cpu->k << 16) | ((adr + cpu->x) & 0xffff), (cpu->k << 16) | ((adr + cpu->x + 1) & 0xffff), true);
				cpu->pc = value;
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