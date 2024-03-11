#include "cpu.h"
#include "statehandler.h"
#include "snes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _MSC_VER
#define LAKESNES_UNREACHABLE __assume(false)
#define LAKENES_NOINLINE __declspec(noinline)
#else
#define LAKESNES_UNREACHABLE __builtin_unreachable()
#define LAKENES_NOINLINE __attribute__((noinline)) 
#endif
#define LAKESNES_UNREACHABLE_CASE default: LAKESNES_UNREACHABLE; break;

namespace
{
	enum class MemOp
	{
		Fetch, Read, Write
	};

	//for verification
	constexpr int test_getAccessTime(bool fastMem, int bank, int adr) {
		if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && adr < 0x8000) {
			// 00-3f,80-bf:0-7fff
			if(adr < 0x2000 || adr >= 0x6000) return 8; // 0-1fff, 6000-7fff
			if(adr < 0x4000 || adr >= 0x4200) return 6; // 2000-3fff, 4200-5fff
			return 12; // 4000-41ff
		}
		// 40-7f,co-ff:0000-ffff, 00-3f,80-bf:8000-ffff
		return (fastMem && bank >= 0x80) ? 6 : 8; // depends on setting in banks 80+
	}

	enum class RESOURCE
	{
		OPENBUS,
		ROM,
		WRAM,
		SRAM,
		IOBLOCK,
		IOBLOCK_SLOWJOYPAD,
	};

	//NEXT UP: the entire memory map implementation is here
	struct In {
		int bank, addr;
	} in;

	struct Out {
		int offset;
		RESOURCE res;
	} out;


	template<int BANK> LAKENES_NOINLINE int cpu_access_new_work(LakeSnes::Snes* snes, int bank, int addr)
	{
		#define CASE10(X) \
			case 0x00+X: case 0x01+X: case 0x02+X: case 0x03+X: case 0x04+X: case 0x05+X: case 0x06+X: case 0x07+X: \
			case 0x08+X: case 0x09+X: case 0x0A+X: case 0x0B+X: case 0x0C+X: case 0x0D+X: case 0x0E + (X): case 0x0F+X:

		Out out;
		out.res = RESOURCE::OPENBUS;
		bool HIGHHALF = true;
		int addrBlock=addr>>13;
		switch(BANK)
		{
			LAKESNES_UNREACHABLE_CASE
			CASE10(0x00) CASE10(0x10) CASE10(0x20) CASE10(0x30)
				HIGHHALF = false;
				//fallthrough
			CASE10(0x80) CASE10(0x90) CASE10(0xA0) CASE10(0xB0)
				switch(addrBlock)
				{
					LAKESNES_UNREACHABLE_CASE
					case 0x00>>1:
						out.res = RESOURCE::WRAM;
						out.offset = addr&0x1FFF;
						break;
					case 0x02>>1:
						out.res = RESOURCE::IOBLOCK;
						out.offset = addr;
						break;
					case 0x04>>1:
						out.offset = addr;
						if(addr < 0x4000 || addr >= 0x4200)
							out.res = RESOURCE::IOBLOCK_SLOWJOYPAD;
						else 
							out.res = RESOURCE::IOBLOCK;
						break;
					case 0x06>>1:
						out.res = RESOURCE::ROM;
						out.offset = (addr&0x7FFF)+((bank&0x7F)*32768);
						break;
					case 0x08>>1:
					case 0x0A>>1:
					case 0x0C>>1:
					case 0x0E>>1:
						out.res = RESOURCE::ROM;
						out.offset = (addr&0x7FFF)+((bank&0x7F)*32768);
						break;
				}
			break;

			CASE10(0x40)
			CASE10(0x50)
			CASE10(0x60)
				HIGHHALF = false;
				switch(addrBlock)
				{
					LAKESNES_UNREACHABLE_CASE
					case 0x00>>1: case 0x02>>1: case 0x04>>1: case 0x06>>1:
						out.res = RESOURCE::OPENBUS;
						break;
					case 0x08>>1: case 0x0A>>1: case 0x0C>>1: case 0x0E>>1:
						out.res = RESOURCE::ROM;
						break;
				}
				break;

			case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
			case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: // WRAM takes over here 
				HIGHHALF = false;
				switch(addrBlock)
				{
					LAKESNES_UNREACHABLE_CASE
					case 0x00>>1: case 0x02>>1: case 0x04>>1: case 0x06>>1:
						out.res = RESOURCE::SRAM;
						out.offset = addr; //masking to size is done elsewhere
						break;
					case 0x08>>1: case 0x0A>>1: case 0x0C>>1: case 0x0E>>1:
						out.res = RESOURCE::ROM;
						break;
				}
				break;

			case 0x7E:
				HIGHHALF = false;
				out.res = RESOURCE::WRAM;
				out.offset = addr;
				break;
			case 0x7F:
				HIGHHALF = false;
				out.res = RESOURCE::WRAM;
				out.offset = addr+0x10000;
				break;

			CASE10(0xC0) CASE10(0xD0) CASE10(0xE0) CASE10(0xF0)
			switch(addrBlock)
			{
				LAKESNES_UNREACHABLE_CASE
				case 0x00>>1: case 0x02>>1: case 0x04>>1: case 0x06>>1:
				out.res = RESOURCE::OPENBUS;
				break;
			case 0x08>>1: case 0x0A>>1: case 0x0C>>1: case 0x0E>>1:
				out.res = RESOURCE::ROM;
				break;
			}
			break;
		}

		//brief summary: everything's 8, but:
		//* 0x80+ rom with fastrom is 6
		//* open bus follows rom
		//* normal registers are 6
		//* slow joypad registers are double access cycles = 12
		//in other words: everything's 6, except slow joypad is 6, and a slow rom is 8
		//hopefully the compiler can optimize this by knowing what resource we just chose

		int kCycles;
		switch(out.res)
		{
			LAKESNES_UNREACHABLE_CASE
			case RESOURCE::OPENBUS:
			case RESOURCE::ROM:
				kCycles = snes->fastMem ? 6 : 8;
				break;
			case RESOURCE::SRAM:
				kCycles = 8;
				break;
			case RESOURCE::WRAM:
				kCycles = 8;
				break;
			case RESOURCE::IOBLOCK:
				kCycles = 6;
				break;
			case RESOURCE::IOBLOCK_SLOWJOYPAD:
				kCycles = 12;
				break;
		}

		kCycles -= 4;
		snes->mydma.dma_handleDma(kCycles);
		snes->snes_runCycles(kCycles);

		uint8_t rv;
		switch(out.res)
		{
			LAKESNES_UNREACHABLE_CASE
			case RESOURCE::OPENBUS:
				rv = snes->openBus;
				break;
			case RESOURCE::ROM:
				rv = snes->mycart.config.rom[out.offset & (snes->mycart.config.romSize - 1)];
				break;
			case RESOURCE::SRAM:
				rv = snes->mycart.ram[out.offset & (snes->mycart.config.ramSize-1)];
				break;
			case RESOURCE::WRAM:
				rv = snes->ram[out.offset];
				break;
			case RESOURCE::IOBLOCK:
			case RESOURCE::IOBLOCK_SLOWJOYPAD:
				rv = snes->snes_read(out.offset);
				break;
		}
		
		
		snes->mydma.dma_handleDma(4);
		snes->snes_runCycles(4);
		return (uint8_t)rv;
	}

	template<int BYTES, MemOp OP> int cpu_access_new(LakeSnes::Snes* snes, int bank, int addr, int value)
	{
		//FOR NOW: assuming lorom+SRAM
		//TODO: can we factor fastMem check out of this somehow?
		//answer: YES: by keeping a fastrom bit set in a virtual bit 8 of the bank registers (which will never be changed), we can catch it here and map it differently

		int barge;
		switch(bank)
		{
			LAKESNES_UNREACHABLE_CASE

			#define DOIT(X) case X: barge = cpu_access_new_work<X>(snes,bank,addr); break;
			DOIT(0)DOIT(1)DOIT(2)DOIT(3)DOIT(4)DOIT(5)DOIT(6)DOIT(7)DOIT(8)DOIT(9)DOIT(10)DOIT(11)DOIT(12)DOIT(13)DOIT(14)DOIT(15)
			DOIT(16)DOIT(17)DOIT(18)DOIT(19)DOIT(20)DOIT(21)DOIT(22)DOIT(23)DOIT(24)DOIT(25)DOIT(26)DOIT(27)DOIT(28)DOIT(29)DOIT(30)DOIT(31)
			DOIT(32)DOIT(33)DOIT(34)DOIT(35)DOIT(36)DOIT(37)DOIT(38)DOIT(39)DOIT(40)DOIT(41)DOIT(42)DOIT(43)DOIT(44)DOIT(45)DOIT(46)DOIT(47)
			DOIT(48)DOIT(49)DOIT(50)DOIT(51)DOIT(52)DOIT(53)DOIT(54)DOIT(55)DOIT(56)DOIT(57)DOIT(58)DOIT(59)DOIT(60)DOIT(61)DOIT(62)DOIT(63)
			DOIT(64)DOIT(65)DOIT(66)DOIT(67)DOIT(68)DOIT(69)DOIT(70)DOIT(71)DOIT(72)DOIT(73)DOIT(74)DOIT(75)DOIT(76)DOIT(77)DOIT(78)DOIT(79)
			DOIT(80)DOIT(81)DOIT(82)DOIT(83)DOIT(84)DOIT(85)DOIT(86)DOIT(87)DOIT(88)DOIT(89)DOIT(90)DOIT(91)DOIT(92)DOIT(93)DOIT(94)DOIT(95)
			DOIT(96)DOIT(97)DOIT(98)DOIT(99)DOIT(100)DOIT(101)DOIT(102)DOIT(103)DOIT(104)DOIT(105)DOIT(106)DOIT(107)DOIT(108)DOIT(109)DOIT(110)DOIT(111)
			DOIT(112)DOIT(113)DOIT(114)DOIT(115)DOIT(116)DOIT(117)DOIT(118)DOIT(119)DOIT(120)DOIT(121)DOIT(122)DOIT(123)DOIT(124)DOIT(125)DOIT(126)DOIT(127)
			DOIT(128)DOIT(129)DOIT(130)DOIT(131)DOIT(132)DOIT(133)DOIT(134)DOIT(135)DOIT(136)DOIT(137)DOIT(138)DOIT(139)DOIT(140)DOIT(141)DOIT(142)DOIT(143)
			DOIT(144)DOIT(145)DOIT(146)DOIT(147)DOIT(148)DOIT(149)DOIT(150)DOIT(151)DOIT(152)DOIT(153)DOIT(154)DOIT(155)DOIT(156)DOIT(157)DOIT(158)DOIT(159)
			DOIT(160)DOIT(161)DOIT(162)DOIT(163)DOIT(164)DOIT(165)DOIT(166)DOIT(167)DOIT(168)DOIT(169)DOIT(170)DOIT(171)DOIT(172)DOIT(173)DOIT(174)DOIT(175)
			DOIT(176)DOIT(177)DOIT(178)DOIT(179)DOIT(180)DOIT(181)DOIT(182)DOIT(183)DOIT(184)DOIT(185)DOIT(186)DOIT(187)DOIT(188)DOIT(189)DOIT(190)DOIT(191)
			DOIT(192)DOIT(193)DOIT(194)DOIT(195)DOIT(196)DOIT(197)DOIT(198)DOIT(199)DOIT(200)DOIT(201)DOIT(202)DOIT(203)DOIT(204)DOIT(205)DOIT(206)DOIT(207)
			DOIT(208)DOIT(209)DOIT(210)DOIT(211)DOIT(212)DOIT(213)DOIT(214)DOIT(215)DOIT(216)DOIT(217)DOIT(218)DOIT(219)DOIT(220)DOIT(221)DOIT(222)DOIT(223)
			DOIT(224)DOIT(225)DOIT(226)DOIT(227)DOIT(228)DOIT(229)DOIT(230)DOIT(231)DOIT(232)DOIT(233)DOIT(234)DOIT(235)DOIT(236)DOIT(237)DOIT(238)DOIT(239)
			DOIT(240)DOIT(241)DOIT(242)DOIT(243)DOIT(244)DOIT(245)DOIT(246)DOIT(247)DOIT(248)DOIT(249)DOIT(250)DOIT(251)DOIT(252)DOIT(253)DOIT(254)DOIT(255)
		}

		return (uint8_t)barge;
	}
}

namespace LakeSnes
{

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

	uint8_t Cpu::cpu_read(Addr24 addr)
	{
		auto addrl = (addr.bank<<16)+addr.addr;
		return cpu_read(addrl);
	}

	uint16_t Cpu::cpu_readWord(Addr24 addr, bool intCheck)
	{
		auto addrl = (addr.bank<<16)+addr.addr;
		auto addrh = ((addr.bank<<16)+addr.addr+1)&0xFFFFFF;
		return cpu_readWord(addrl,addrh,intCheck);
	}

	void Cpu::cpu_write(Addr24 addr, uint8_t val)
	{
		auto addrl = (addr.bank<<16)+addr.addr;
		return cpu_write(addrl,val);
	}

	void Cpu::cpu_writeWord(Addr24 addr, uint16_t value, bool reversed, bool intCheck)
	{
		auto addrl = (addr.bank<<16)+addr.addr;
		auto addrh = ((addr.bank<<16)+addr.addr+1)&0xFFFFFF;
		cpu_writeWord(addrl,addrh,value,reversed,intCheck);
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

	uint8_t Cpu::cpu_readOpcode()
	{
		intDelay = false;
		return cpu_access_new<1,MemOp::Fetch>(config.snes,k,pc++,0);
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

	Cpu::Addr24 Cpu::cpu_adrImm(bool xFlag) {
		if((xFlag && xf) || (!xFlag && mf)) {
			return MakeAddr24(k,pc++);
		} else {
			auto adr = pc;
			pc+=2;
			return MakeAddr24(k,adr);
		}
	}

	Cpu::Addr24 Cpu::cpu_adrDp() {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		return MakeAddr24(dp,adr);
	}

	Cpu::Addr24 Cpu::cpu_adrDpx() {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		return MakeAddr24(dp,adr+x);
	}

	Cpu::Addr24 Cpu::cpu_adrDpy() {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		return MakeAddr24(dp,adr+y);
	}

	Cpu::Addr24 Cpu::cpu_adrIdp() {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint16_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		return MakeAddr24(db,pointer);
	}

	Cpu::Addr24 Cpu::cpu_adrIdx() {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		cpu_idle();
		uint16_t pointer = cpu_readWord((dp + adr + x) & 0xffff, (dp + adr + x + 1) & 0xffff, false);
		return MakeAddr24(db,pointer);
	}

	Cpu::Addr24 Cpu::cpu_adrIdy(bool write) {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint16_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !xf || ((pointer >> 8) != ((pointer + y) >> 8))) cpu_idle();
		return MakeAddr24(db,pointer+y);
	}

	Cpu::Addr24 Cpu::cpu_adrIdl() {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint32_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		auto bank = cpu_read((dp + adr + 2) & 0xffff);
		return MakeAddr24(bank, pointer);
	}

	Cpu::Addr24 Cpu::cpu_adrIly() {
		uint8_t adr = cpu_readOpcode();
		if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
		uint32_t pointer = cpu_readWord((dp + adr) & 0xffff, (dp + adr + 1) & 0xffff, false);
		auto bank = cpu_read((dp + adr + 2) & 0xffff);
		return MakeAddr24(bank, pointer + y);
	}

	Cpu::Addr24 Cpu::cpu_adrSr() {
		uint8_t adr = cpu_readOpcode();
		cpu_idle();
		return MakeAddr24(0,sp + adr); //note: DB is 0 for stack
	}

	Cpu::Addr24 Cpu::cpu_adrIsy() {
		uint8_t adr = cpu_readOpcode();
		cpu_idle();
		uint16_t pointer = cpu_readWord((sp + adr) & 0xffff, (sp + adr + 1) & 0xffff, false);
		cpu_idle();
		pointer += y;
		return MakeAddr24(db,pointer);
	}

	Cpu::Addr24 Cpu::cpu_adrAbs() {
		uint16_t adr = cpu_readOpcodeWord(false);
		return MakeAddr24(db,adr);
	}

	Cpu::Addr24 Cpu::cpu_adrAbx(bool write) {
		uint16_t adr = cpu_readOpcodeWord(false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !xf || ((adr >> 8) != ((adr + x) >> 8))) cpu_idle();
		adr += x;
		return MakeAddr24(db,adr);
	}

	Cpu::Addr24 Cpu::cpu_adrAby(bool write) {
		uint16_t adr = cpu_readOpcodeWord(false);
		// writing opcode or x = 0 or page crossed: 1 extra cycle
		if(write || !xf || ((adr >> 8) != ((adr + y) >> 8))) cpu_idle();
		adr += y;
		return MakeAddr24(db,adr);
	}

	Cpu::Addr24 Cpu::cpu_adrAbl() {
		uint32_t adr = cpu_readOpcodeWord(false);
		auto bank = cpu_readOpcode();
		return MakeAddr24(bank,adr);
	}

	Cpu::Addr24 Cpu::cpu_adrAlx() {
		uint32_t adr = cpu_readOpcodeWord(false);
		auto bank = cpu_readOpcode();
		adr += x;
		return MakeAddr24(bank,adr);
	}

	// opcode functions

	void Cpu::cpu_and(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr);
			a = (a & 0xff00) | ((a & value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(addr, true);
			a &= value;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_ora(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr);
			a = (a & 0xff00) | ((a | value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(addr, true);
			a |= value;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_eor(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr);
			a = (a & 0xff00) | ((a ^ value) & 0xff);
		} else {
			uint16_t value = cpu_readWord(addr, true);
			a ^= value;
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_adc(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr);
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
			uint16_t value = cpu_readWord(addr, true);
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

	void Cpu::cpu_sbc(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr) ^ 0xff;
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
			uint16_t value = cpu_readWord(addr, true) ^ 0xffff;
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

	void Cpu::cpu_cmp(Addr24 addr) {
		int result = 0;
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr) ^ 0xff;
			result = (a & 0xff) + value + 1;
			c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(addr, true) ^ 0xffff;
			result = a + value + 1;
			c = result > 0xffff;
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_cpx(Addr24 addr) {
		int result = 0;
		if(xf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr) ^ 0xff;
			result = (x & 0xff) + value + 1;
			c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(addr, true) ^ 0xffff;
			result = x + value + 1;
			c = result > 0xffff;
		}
		cpu_setZN(result, xf);
	}

	void Cpu::cpu_cpy(Addr24 addr) {
		int result = 0;
		if(xf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr) ^ 0xff;
			result = (y & 0xff) + value + 1;
			c = result > 0xff;
		} else {
			uint16_t value = cpu_readWord(addr, true) ^ 0xffff;
			result = y + value + 1;
			c = result > 0xffff;
		}
		cpu_setZN(result, xf);
	}

	void Cpu::cpu_bit(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			uint8_t value = cpu_read(addr);
			uint8_t result = (a & 0xff) & value;
			z = result == 0;
			n = value & 0x80;
			v = value & 0x40;
		} else {
			uint16_t value = cpu_readWord(addr, true);
			uint16_t result = a & value;
			z = result == 0;
			n = value & 0x8000;
			v = value & 0x4000;
		}
	}

	void Cpu::cpu_lda(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			a = (a & 0xff00) | cpu_read(addr);
		} else {
			a = cpu_readWord(addr, true);
		}
		cpu_setZN(a, mf);
	}

	void Cpu::cpu_ldx(Addr24 addr) {
		if(xf) {
			cpu_checkInt();
			x = cpu_read(addr);
		} else {
			x = cpu_readWord(addr, true);
		}
		cpu_setZN(x, xf);
	}

	void Cpu::cpu_ldy(Addr24 addr) {
		if(xf) {
			cpu_checkInt();
			y = cpu_read(addr);
		} else {
			y = cpu_readWord(addr, true);
		}
		cpu_setZN(y, xf);
	}

	void Cpu::cpu_sta(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			cpu_write(addr, a);
		} else {
			cpu_writeWord(addr, a, false, true);
		}
	}

	void Cpu::cpu_stx(Addr24 addr) {
		if(xf) {
			cpu_checkInt();
			cpu_write(addr, x);
		} else {
			cpu_writeWord(addr, x, false, true);
		}
	}

	void Cpu::cpu_sty(Addr24 addr) {
		if(xf) {
			cpu_checkInt();
			cpu_write(addr, y);
		} else {
			cpu_writeWord(addr, y, false, true);
		}
	}

	void Cpu::cpu_stz(Addr24 addr) {
		if(mf) {
			cpu_checkInt();
			cpu_write(addr, 0);
		} else {
			cpu_writeWord(addr, 0, false, true);
		}
	}

	void Cpu::cpu_ror(Addr24 addr) {
		bool carry = false;
		int result = 0;
		if(mf) {
			uint8_t value = cpu_read(addr);
			cpu_idle();
			carry = value & 1;
			result = (value >> 1) | (c << 7);
			cpu_checkInt();
			cpu_write(addr, result);
		} else {
			uint16_t value = cpu_readWord(addr, false);
			cpu_idle();
			carry = value & 1;
			result = (value >> 1) | (c << 15);
			cpu_writeWord(addr, result, true, true);
		}
		cpu_setZN(result, mf);
		c = carry;
	}

	void Cpu::cpu_rol(Addr24 addr) {
		int result = 0;
		if(mf) {
			result = (cpu_read(addr) << 1) | c;
			cpu_idle();
			c = result & 0x100;
			cpu_checkInt();
			cpu_write(addr, result);
		} else {
			result = (cpu_readWord(addr, false) << 1) | c;
			cpu_idle();
			c = result & 0x10000;
			cpu_writeWord(addr, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_lsr(Addr24 addr) {
		int result = 0;
		if(mf) {
			uint8_t value = cpu_read(addr);
			cpu_idle();
			c = value & 1;
			result = value >> 1;
			cpu_checkInt();
			cpu_write(addr, result);
		} else {
			uint16_t value = cpu_readWord(addr, false);
			cpu_idle();
			c = value & 1;
			result = value >> 1;
			cpu_writeWord(addr, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_asl(Addr24 addr) {
		int result = 0;
		if(mf) {
			result = cpu_read(addr) << 1;
			cpu_idle();
			c = result & 0x100;
			cpu_checkInt();
			cpu_write(addr, result);
		} else {
			result = cpu_readWord(addr, false) << 1;
			cpu_idle();
			c = result & 0x10000;
			cpu_writeWord(addr, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_inc(Addr24 addr) {
		int result = 0;
		if(mf) {
			result = cpu_read(addr) + 1;
			cpu_idle();
			cpu_checkInt();
			cpu_write(addr, result);
		} else {
			result = cpu_readWord(addr, false) + 1;
			cpu_idle();
			cpu_writeWord(addr, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_dec(Addr24 addr) {
		int result = 0;
		if(mf) {
			result = cpu_read(addr) - 1;
			cpu_idle();
			cpu_checkInt();
			cpu_write(addr, result);
		} else {
			result = cpu_readWord(addr, false) - 1;
			cpu_idle();
			cpu_writeWord(addr, result, true, true);
		}
		cpu_setZN(result, mf);
	}

	void Cpu::cpu_tsb(Addr24 addr) {
		if(mf) {
			uint8_t value = cpu_read(addr);
			cpu_idle();
			z = ((a & 0xff) & value) == 0;
			cpu_checkInt();
			cpu_write(addr, value | (a & 0xff));
		} else {
			uint16_t value = cpu_readWord(addr, false);
			cpu_idle();
			z = (a & value) == 0;
			cpu_writeWord(addr, value | a, true, true);
		}
	}

	void Cpu::cpu_trb(Addr24 addr) {
		if(mf) {
			uint8_t value = cpu_read(addr);
			cpu_idle();
			z = ((a & 0xff) & value) == 0;
			cpu_checkInt();
			cpu_write(addr, value & ~(a & 0xff));
		} else {
			uint16_t value = cpu_readWord(addr, false);
			cpu_idle();
			z = (a & value) == 0;
			cpu_writeWord(addr, value & ~a, true, true);
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
				auto addr = cpu_adrIdx();
				cpu_ora(addr);
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
				auto addr = cpu_adrSr();
				cpu_ora(addr);
				break;
			}
			case 0x04: { // tsb dp
				auto addr = cpu_adrDp();
				cpu_tsb(addr);
				break;
			}
			case 0x05: { // ora dp
				auto addr = cpu_adrDp();
				cpu_ora(addr);
				break;
			}
			case 0x06: { // asl dp
				auto addr = cpu_adrDp();
				cpu_asl(addr);
				break;
			}
			case 0x07: { // ora idl
				auto addr = cpu_adrIdl();
				cpu_ora(addr);
				break;
			}
			case 0x08: { // php imp
				cpu_idle();
				cpu_checkInt();
				cpu_pushByte(cpu_getFlags());
				break;
			}
			case 0x09: { // ora imm(m)
				auto addr = cpu_adrImm(false);
				cpu_ora(addr);
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
				auto addr = cpu_adrAbs();
				cpu_tsb(addr);
				break;
			}
			case 0x0d: { // ora abs
				auto addr = cpu_adrAbs();
				cpu_ora(addr);
				break;
			}
			case 0x0e: { // asl abs
				auto addr = cpu_adrAbs();
				cpu_asl(addr);
				break;
			}
			case 0x0f: { // ora abl
				auto addr = cpu_adrAbl();
				cpu_ora(addr);
				break;
			}
			case 0x10: { // bpl rel
				cpu_doBranch(!n);
				break;
			}
			case 0x11: { // ora idy(r)
				auto addr = cpu_adrIdy(false);
				cpu_ora(addr);
				break;
			}
			case 0x12: { // ora idp
				auto addr = cpu_adrIdp();
				cpu_ora(addr);
				break;
			}
			case 0x13: { // ora isy
				auto addr = cpu_adrIsy();
				cpu_ora(addr);
				break;
			}
			case 0x14: { // trb dp
				auto addr = cpu_adrDp();
				cpu_trb(addr);
				break;
			}
			case 0x15: { // ora dpx
				auto addr = cpu_adrDpx();
				cpu_ora(addr);
				break;
			}
			case 0x16: { // asl dpx
				auto addr = cpu_adrDpx();
				cpu_asl(addr);
				break;
			}
			case 0x17: { // ora ily
				auto addr = cpu_adrIly();
				cpu_ora(addr);
				break;
			}
			case 0x18: { // clc imp
				cpu_adrImp();
				c = false;
				break;
			}
			case 0x19: { // ora aby(r)
				auto addr = cpu_adrAby(false);
				cpu_ora(addr);
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
				auto addr = cpu_adrAbs();
				cpu_trb(addr);
				break;
			}
			case 0x1d: { // ora abx(r)
				auto addr = cpu_adrAbx(false);
				cpu_ora(addr);
				break;
			}
			case 0x1e: { // asl abx
				auto addr = cpu_adrAbx(true);
				cpu_asl(addr);
				break;
			}
			case 0x1f: { // ora alx
				auto addr = cpu_adrAlx();
				cpu_ora(addr);
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
				auto addr = cpu_adrIdx();
				cpu_and(addr);
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
				auto addr = cpu_adrSr();
				cpu_and(addr);
				break;
			}
			case 0x24: { // bit dp
				auto addr = cpu_adrDp();
				cpu_bit(addr);
				break;
			}
			case 0x25: { // and dp
				auto addr = cpu_adrDp();
				cpu_and(addr);
				break;
			}
			case 0x26: { // rol dp
				auto addr = cpu_adrDp();
				cpu_rol(addr);
				break;
			}
			case 0x27: { // and idl
				auto addr = cpu_adrIdl();
				cpu_and(addr);
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
				auto addr = cpu_adrImm(false);
				cpu_and(addr);
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
				auto addr = cpu_adrAbs();
				cpu_bit(addr);
				break;
			}
			case 0x2d: { // and abs
				auto addr = cpu_adrAbs();
				cpu_and(addr);
				break;
			}
			case 0x2e: { // rol abs
				auto addr = cpu_adrAbs();
				cpu_rol(addr);
				break;
			}
			case 0x2f: { // and abl
				auto addr = cpu_adrAbl();
				cpu_and(addr);
				break;
			}
			case 0x30: { // bmi rel
				cpu_doBranch(n);
				break;
			}
			case 0x31: { // and idy(r)
				auto addr = cpu_adrIdy(false);
				cpu_and(addr);
				break;
			}
			case 0x32: { // and idp
				auto addr = cpu_adrIdp();
				cpu_and(addr);
				break;
			}
			case 0x33: { // and isy
				auto addr = cpu_adrIsy();
				cpu_and(addr);
				break;
			}
			case 0x34: { // bit dpx
				auto addr = cpu_adrDpx();
				cpu_bit(addr);
				break;
			}
			case 0x35: { // and dpx
				auto addr = cpu_adrDpx();
				cpu_and(addr);
				break;
			}
			case 0x36: { // rol dpx
				auto addr = cpu_adrDpx();
				cpu_rol(addr);
				break;
			}
			case 0x37: { // and ily
				auto addr = cpu_adrIly();
				cpu_and(addr);
				break;
			}
			case 0x38: { // sec imp
				cpu_adrImp();
				c = true;
				break;
			}
			case 0x39: { // and aby(r)
				auto addr = cpu_adrAby(false);
				cpu_and(addr);
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
				auto addr = cpu_adrAbx(false);
				cpu_bit(addr);
				break;
			}
			case 0x3d: { // and abx(r)
				auto addr = cpu_adrAbx(false);
				cpu_and(addr);
				break;
			}
			case 0x3e: { // rol abx
				auto addr = cpu_adrAbx(true);
				cpu_rol(addr);
				break;
			}
			case 0x3f: { // and alx
				auto addr = cpu_adrAlx();
				cpu_and(addr);
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
				auto addr = cpu_adrIdx();
				cpu_eor(addr);
				break;
			}
			case 0x42: { // wdm imm(s)
				cpu_checkInt();
				cpu_readOpcode();
				break;
			}
			case 0x43: { // eor sr
				auto addr = cpu_adrSr();
				cpu_eor(addr);
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
				auto addr = cpu_adrDp();
				cpu_eor(addr);
				break;
			}
			case 0x46: { // lsr dp
				auto addr = cpu_adrDp();
				cpu_lsr(addr);
				break;
			}
			case 0x47: { // eor idl
				auto addr = cpu_adrIdl();
				cpu_eor(addr);
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
				auto addr = cpu_adrImm(false);
				cpu_eor(addr);
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
				auto addr = cpu_adrAbs();
				cpu_eor(addr);
				break;
			}
			case 0x4e: { // lsr abs
				auto addr = cpu_adrAbs();
				cpu_lsr(addr);
				break;
			}
			case 0x4f: { // eor abl
				auto addr = cpu_adrAbl();
				cpu_eor(addr);
				break;
			}
			case 0x50: { // bvc rel
				cpu_doBranch(!v);
				break;
			}
			case 0x51: { // eor idy(r)
				auto addr = cpu_adrIdy(false);
				cpu_eor(addr);
				break;
			}
			case 0x52: { // eor idp
				auto addr = cpu_adrIdp();
				cpu_eor(addr);
				break;
			}
			case 0x53: { // eor isy
				auto addr = cpu_adrIsy();
				cpu_eor(addr);
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
				auto addr = cpu_adrDpx();
				cpu_eor(addr);
				break;
			}
			case 0x56: { // lsr dpx
				auto addr = cpu_adrDpx();
				cpu_lsr(addr);
				break;
			}
			case 0x57: { // eor ily
				auto addr = cpu_adrIly();
				cpu_eor(addr);
				break;
			}
			case 0x58: { // cli imp
				cpu_adrImp();
				i = false;
				break;
			}
			case 0x59: { // eor aby(r)
				auto addr = cpu_adrAby(false);
				cpu_eor(addr);
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
				auto addr = cpu_adrAbx(false);
				cpu_eor(addr);
				break;
			}
			case 0x5e: { // lsr abx
					auto addr = cpu_adrAbx(true);
				cpu_lsr(addr);
				break;
			}
			case 0x5f: { // eor alx
				auto addr = cpu_adrAlx();
				cpu_eor(addr);
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
				auto addr = cpu_adrIdx();
				cpu_adc(addr);
				break;
			}
			case 0x62: { // per rll
				uint16_t value = cpu_readOpcodeWord(false);
				cpu_idle();
				cpu_pushWord(pc + (int16_t) value, true);
				break;
			}
			case 0x63: { // adc sr
				auto addr = cpu_adrSr();
				cpu_adc(addr);
				break;
			}
			case 0x64: { // stz dp
				auto addr = cpu_adrDp();
				cpu_stz(addr);
				break;
			}
			case 0x65: { // adc dp
				auto addr = cpu_adrDp();
				cpu_adc(addr);
				break;
			}
			case 0x66: { // ror dp
				auto addr = cpu_adrDp();
				cpu_ror(addr);
				break;
			}
			case 0x67: { // adc idl
				auto addr = cpu_adrIdl();
				cpu_adc(addr);
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
				auto addr = cpu_adrImm(false);
				cpu_adc(addr);
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
				auto addr = cpu_adrAbs();
				cpu_adc(addr);
				break;
			}
			case 0x6e: { // ror abs
				auto addr = cpu_adrAbs();
				cpu_ror(addr);
				break;
			}
			case 0x6f: { // adc abl
				auto addr = cpu_adrAbl();
				cpu_adc(addr);
				break;
			}
			case 0x70: { // bvs rel
				cpu_doBranch(v);
				break;
			}
			case 0x71: { // adc idy(r)
				auto addr = cpu_adrIdy(false);
				cpu_adc(addr);
				break;
			}
			case 0x72: { // adc idp
				auto addr = cpu_adrIdp();
				cpu_adc(addr);
				break;
			}
			case 0x73: { // adc isy
				auto addr = cpu_adrIsy();
				cpu_adc(addr);
				break;
			}
			case 0x74: { // stz dpx
					auto addr = cpu_adrDpx();
				cpu_stz(addr);
				break;
			}
			case 0x75: { // adc dpx
				auto addr = cpu_adrDpx();
				cpu_adc(addr);
				break;
			}
			case 0x76: { // ror dpx
				auto addr = cpu_adrDpx();
				cpu_ror(addr);
				break;
			}
			case 0x77: { // adc ily
				auto addr = cpu_adrIly();
				cpu_adc(addr);
				break;
			}
			case 0x78: { // sei imp
				cpu_adrImp();
				i = true;
				break;
			}
			case 0x79: { // adc aby(r)
				auto addr = cpu_adrAby(false);
				cpu_adc(addr);
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
				auto addr = cpu_adrAbx(false);
				cpu_adc(addr);
				break;
			}
			case 0x7e: { // ror abx
				auto addr = cpu_adrAbx(true);
				cpu_ror(addr);
				break;
			}
			case 0x7f: { // adc alx
				auto addr = cpu_adrAlx();
				cpu_adc(addr);
				break;
			}
			case 0x80: { // bra rel
				cpu_doBranch(true);
				break;
			}
			case 0x81: { // sta idx
				auto addr = cpu_adrIdx();
				cpu_sta(addr);
				break;
			}
			case 0x82: { // brl rll
				pc += (int16_t) cpu_readOpcodeWord(false);
				cpu_checkInt();
				cpu_idle();
				break;
			}
			case 0x83: { // sta sr
				auto addr = cpu_adrSr();
				cpu_sta(addr);
				break;
			}
			case 0x84: { // sty dp
				auto addr = cpu_adrDp();
				cpu_sty(addr);
				break;
			}
			case 0x85: { // sta dp
				auto addr = cpu_adrDp();
				cpu_sta(addr);
				break;
			}
			case 0x86: { // stx dp
				auto addr = cpu_adrDp();
				cpu_stx(addr);
				break;
			}
			case 0x87: { // sta idl
				auto addr = cpu_adrIdl();
				cpu_sta(addr);
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
				auto addr = cpu_adrAbs();
				cpu_sty(addr);
				break;
			}
			case 0x8d: { // sta abs
				auto addr = cpu_adrAbs();
				cpu_sta(addr);
				break;
			}
			case 0x8e: { // stx abs
				auto addr = cpu_adrAbs();
				cpu_stx(addr);
				break;
			}
			case 0x8f: { // sta abl
				auto addr = cpu_adrAbl();
				cpu_sta(addr);
				break;
			}
			case 0x90: { // bcc rel
				cpu_doBranch(!c);
				break;
			}
			case 0x91: { // sta idy
				auto addr = cpu_adrIdy(true);
				cpu_sta(addr);
				break;
			}
			case 0x92: { // sta idp
				auto addr = cpu_adrIdp();
				cpu_sta(addr);
				break;
			}
			case 0x93: { // sta isy
				auto addr = cpu_adrIsy();
				cpu_sta(addr);
				break;
			}
			case 0x94: { // sty dpx
				auto addr = cpu_adrDpx();
				cpu_sty(addr);
				break;
			}
			case 0x95: { // sta dpx
				auto addr = cpu_adrDpx();
				cpu_sta(addr);
				break;
			}
			case 0x96: { // stx dpy
				auto addr = cpu_adrDpy();
				cpu_stx(addr);
				break;
			}
			case 0x97: { // sta ily
				auto addr = cpu_adrIly();
				cpu_sta(addr);
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
				auto addr = cpu_adrAby(true);
				cpu_sta(addr);
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
				auto addr = cpu_adrAbs();
				cpu_stz(addr);
				break;
			}
			case 0x9d: { // sta abx
				auto addr = cpu_adrAbx(true);
				cpu_sta(addr);
				break;
			}
			case 0x9e: { // stz abx
				auto addr = cpu_adrAbx(true);
				cpu_stz(addr);
				break;
			}
			case 0x9f: { // sta alx
				auto addr = cpu_adrAlx();
				cpu_sta(addr);
				break;
			}
			case 0xa0: { // ldy imm(x)
				auto addr = cpu_adrImm(true);
				cpu_ldy(addr);
				break;
			}
			case 0xa1: { // lda idx
				auto addr = cpu_adrIdx();
				cpu_lda(addr);
				break;
			}
			case 0xa2: { // ldx imm(x)
				auto addr = cpu_adrImm(true);
				cpu_ldx(addr);
				break;
			}
			case 0xa3: { // lda sr
				auto addr = cpu_adrSr();
				cpu_lda(addr);
				break;
			}
			case 0xa4: { // ldy dp
				auto addr =  cpu_adrDp();
				cpu_ldy(addr);
				break;
			}
			case 0xa5: { // lda dp
				auto addr = cpu_adrDp();
				cpu_lda(addr);
				break;
			}
			case 0xa6: { // ldx dp
				auto addr = cpu_adrDp();
				cpu_ldx(addr);
				break;
			}
			case 0xa7: { // lda idl
				auto addr = cpu_adrIdl();
				cpu_lda(addr);
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
				auto addr = cpu_adrImm(false);
				cpu_lda(addr);
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
				auto addr = cpu_adrAbs();
				cpu_ldy(addr);
				break;
			}
			case 0xad: { // lda abs
				auto addr = cpu_adrAbs();
				cpu_lda(addr);
				break;
			}
			case 0xae: { // ldx abs
				auto addr = cpu_adrAbs();
				cpu_ldx(addr);
				break;
			}
			case 0xaf: { // lda abl
				auto addr = cpu_adrAbl();
				cpu_lda(addr);
				break;
			}
			case 0xb0: { // bcs rel
				cpu_doBranch(c);
				break;
			}
			case 0xb1: { // lda idy(r)
				auto addr = cpu_adrIdy(false);
				cpu_lda(addr);
				break;
			}
			case 0xb2: { // lda idp
				auto addr = cpu_adrIdp();
				cpu_lda(addr);
				break;
			}
			case 0xb3: { // lda isy
				auto addr = cpu_adrIsy();
				cpu_lda(addr);
				break;
			}
			case 0xb4: { // ldy dpx
				auto addr = cpu_adrDpx();
				cpu_ldy(addr);
				break;
			}
			case 0xb5: { // lda dpx
				auto addr = cpu_adrDpx();
				cpu_lda(addr);
				break;
			}
			case 0xb6: { // ldx dpy
				auto addr = cpu_adrDpy();
				cpu_ldx(addr);
				break;
			}
			case 0xb7: { // lda ily
				auto addr = cpu_adrIly();
				cpu_lda(addr);
				break;
			}
			case 0xb8: { // clv imp
				cpu_adrImp();
				v = false;
				break;
			}
			case 0xb9: { // lda aby(r)
				auto addr = cpu_adrAby(false);
				cpu_lda(addr);
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
				auto addr = cpu_adrAbx(false);
				cpu_ldy(addr);
				break;
			}
			case 0xbd: { // lda abx(r)
					auto addr = cpu_adrAbx(false);
				cpu_lda(addr);
				break;
			}
			case 0xbe: { // ldx aby(r)
				auto addr = cpu_adrAby(false);
				cpu_ldx(addr);
				break;
			}
			case 0xbf: { // lda alx
				auto addr = cpu_adrAlx();
				cpu_lda(addr);
				break;
			}
			case 0xc0: { // cpy imm(x)
				auto addr = cpu_adrImm(true);
				cpu_cpy(addr);
				break;
			}
			case 0xc1: { // cmp idx
				auto addr = cpu_adrIdx();
				cpu_cmp(addr);
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
					auto addr = cpu_adrSr();
				cpu_cmp(addr);
				break;
			}
			case 0xc4: { // cpy dp
				auto addr = cpu_adrDp();
				cpu_cpy(addr);
				break;
			}
			case 0xc5: { // cmp dp
				auto addr = cpu_adrDp();
				cpu_cmp(addr);
				break;
			}
			case 0xc6: { // dec dp
				auto addr = cpu_adrDp();
				cpu_dec(addr);
				break;
			}
			case 0xc7: { // cmp idl
				auto addr = cpu_adrIdl();
				cpu_cmp(addr);
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
				auto addr = cpu_adrImm(false);
				cpu_cmp(addr);
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
				auto addr = cpu_adrAbs();
				cpu_cpy(addr);
				break;
			}
			case 0xcd: { // cmp abs
					auto addr = cpu_adrAbs();
				cpu_cmp(addr);
				break;
			}
			case 0xce: { // dec abs
					auto addr = cpu_adrAbs();
				cpu_dec(addr);
				break;
			}
			case 0xcf: { // cmp abl
				auto addr = cpu_adrAbl();
				cpu_cmp(addr);
				break;
			}
			case 0xd0: { // bne rel
				cpu_doBranch(!z);
				break;
			}
			case 0xd1: { // cmp idy(r)
				auto addr = cpu_adrIdy(false);
				cpu_cmp(addr);
				break;
			}
			case 0xd2: { // cmp idp
				auto addr = cpu_adrIdp();
				cpu_cmp(addr);
				break;
			}
			case 0xd3: { // cmp isy
				auto addr = cpu_adrIsy();
				cpu_cmp(addr);
				break;
			}
			case 0xd4: { // pei dp
				auto addr = cpu_adrDp();
				cpu_pushWord(cpu_readWord(addr, false), true);
				break;
			}
			case 0xd5: { // cmp dpx
				auto addr = cpu_adrDpx();
				cpu_cmp(addr);
				break;
			}
			case 0xd6: { // dec dpx
				auto addr = cpu_adrDpx();
				cpu_dec(addr);
				break;
			}
			case 0xd7: { // cmp ily
				auto addr = cpu_adrIly();
				cpu_cmp(addr);
				break;
			}
			case 0xd8: { // cld imp
				cpu_adrImp();
				d = false;
				break;
			}
			case 0xd9: { // cmp aby(r)
				auto addr = cpu_adrAby(false);
				cpu_cmp(addr);
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
				auto addr = cpu_adrAbx(false);
				cpu_cmp(addr);
				break;
			}
			case 0xde: { // dec abx
				auto addr = cpu_adrAbx(true);
				cpu_dec(addr);
				break;
			}
			case 0xdf: { // cmp alx
				auto addr = cpu_adrAlx();
				cpu_cmp(addr);
				break;
			}
			case 0xe0: { // cpx imm(x)
				auto addr = cpu_adrImm(true);
				cpu_cpx(addr);
				break;
			}
			case 0xe1: { // sbc idx
				auto addr = cpu_adrIdx();
				cpu_sbc(addr);
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
				auto addr = cpu_adrSr();
				cpu_sbc(addr);
				break;
			}
			case 0xe4: { // cpx dp
				auto addr = cpu_adrDp();
				cpu_cpx(addr);
				break;
			}
			case 0xe5: { // sbc dp
				auto addr = cpu_adrDp();
				cpu_sbc(addr);
				break;
			}
			case 0xe6: { // inc dp
				auto addr = cpu_adrDp();
				cpu_inc(addr);
				break;
			}
			case 0xe7: { // sbc idl
				auto addr = cpu_adrIdl();
				cpu_sbc(addr);
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
				auto addr = cpu_adrImm(false);
				cpu_sbc(addr);
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
				auto addr = cpu_adrAbs();
				cpu_cpx(addr);
				break;
			}
			case 0xed: { // sbc abs
				auto addr = cpu_adrAbs();
				cpu_sbc(addr);
				break;
			}
			case 0xee: { // inc abs
				auto addr = cpu_adrAbs();
				cpu_inc(addr);
				break;
			}
			case 0xef: { // sbc abl
				auto addr = cpu_adrAbl();
				cpu_sbc(addr);
				break;
			}
			case 0xf0: { // beq rel
				cpu_doBranch(z);
				break;
			}
			case 0xf1: { // sbc idy(r)
				auto addr = cpu_adrIdy(false);
				cpu_sbc(addr);
				break;
			}
			case 0xf2: { // sbc idp
				auto addr = cpu_adrIdp();
				cpu_sbc(addr);
				break;
			}
			case 0xf3: { // sbc isy
				auto addr = cpu_adrIsy();
				cpu_sbc(addr);
				break;
			}
			case 0xf4: { // pea imm(l)
				cpu_pushWord(cpu_readOpcodeWord(false), true);
				break;
			}
			case 0xf5: { // sbc dpx
				auto addr = cpu_adrDpx();
				cpu_sbc(addr);
				break;
			}
			case 0xf6: { // inc dpx
				auto addr = cpu_adrDpx();
				cpu_inc(addr);
				break;
			}
			case 0xf7: { // sbc ily
				auto addr = cpu_adrIly();
				cpu_sbc(addr);
				break;
			}
			case 0xf8: { // sed imp
				cpu_adrImp();
				d = true;
				break;
			}
			case 0xf9: { // sbc aby(r)
				auto addr = cpu_adrAby(false);
				cpu_sbc(addr);
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
				auto addr = cpu_adrAbx(false);
				cpu_sbc(addr);
				break;
			}
			case 0xfe: { // inc abx
				auto addr = cpu_adrAbx(true);
				cpu_inc(addr);
				break;
			}
			case 0xff: { // sbc alx
				auto addr = cpu_adrAlx();
				cpu_sbc(addr);
				break;
			}
		}
	}
}