//what the heck? this slows things down.
//#define LAKESNES_CONFIG_CPU_ONE_SYNC_PER_BUS_ACCESSES 1

#define LAKESNES_CONFIG_CPU_ONE_SYNC_PER_INSTRUCTION

#include "conf.h"

#include "cpu.h"
#include "statehandler.h"
#include "snes.h"
#include "Add24.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace
{
	enum class MemOp
	{
		Idle,
		Fetch, Read,
		Write,
		DmaRead, DmaWrite,
	};

	constexpr bool MemOp_IsReadType(MemOp OP)
	{
		return (OP == MemOp::Read || OP == MemOp::Fetch || OP == MemOp::DmaRead);
	}

	constexpr bool MemOp_IsDmaType(MemOp OP)
	{
		return (OP == MemOp::DmaRead || OP == MemOp::DmaWrite);
	}

	void _actually_run_cyles(LakeSnes::Snes* snes, int CYC)
	{
		snes->mydma.dma_handleDma(CYC);
		snes->snes_runCycles(CYC);
	}

	void _catchup_cycles(LakeSnes::Snes* snes)
	{
		#ifdef LAKESNES_CONFIG_CPU_ONE_SYNC_PER_INSTRUCTION
		_actually_run_cyles(snes, snes->pendingCycles);
		snes->pendingCycles = 0;
		#endif
	}

	void _inner_cpu_access_new_run_cyles(LakeSnes::Snes* snes, int CYC)
	{
		#ifdef LAKESNES_CONFIG_CPU_ONE_SYNC_PER_INSTRUCTION
		//just bank them for later
		snes->pendingCycles += CYC;
		#else
		_actually_run_cyles(snes,CYC);
		#endif
	}

	template<MemOp OP> void cpu_access_new_run_cyles_before(LakeSnes::Snes* snes, int CYC)
	{
		if(MemOp_IsDmaType(OP)) return;

		int defer = 0;

		//only reads defer any cycles for later
		if(MemOp_IsReadType(OP))
			defer = 4;

		//Don't defer anything if we want one sync only (it will be fully handled here, then, since none are deferred)
		#ifdef LAKESNES_CONFIG_CPU_ONE_SYNC_PER_BUS_ACCESSES
		defer = 0;
		#endif

		CYC -= defer;

		_inner_cpu_access_new_run_cyles(snes, CYC);
	}

	template<MemOp OP> void cpu_access_new_run_cyles_after(LakeSnes::Snes* snes)
	{
		if(MemOp_IsDmaType(OP)) return;

		//in this case, we took the full charge already
		#ifdef LAKESNES_CONFIG_CPU_ONE_SYNC_PER_BUS_ACCESSES
		return;
		#endif

		//for reads, 4 cycles have been deferred until now.
		if(MemOp_IsReadType(OP))
		{
			_inner_cpu_access_new_run_cyles(snes, 4);
		}
	}

	template<int BYTES, MemOp OP> int cpu_access_new(LakeSnes::Snes* snes, const LakeSnes::Addr24 addr, int value = 0, bool reversed = false, bool intCheck = false)
	{
		//TODO: move to snes module
		//TODO: handle open bus saving
		
		constexpr bool WORDSIZED = (BYTES==2);
		constexpr bool READTYPE = MemOp_IsReadType(OP);
		constexpr bool DMATYPE = MemOp_IsDmaType(OP);

		if(OP == MemOp::Idle)
		{
			cpu_access_new_run_cyles_before<OP>(snes, 6);
			return 0;
		}

		int rv = 0;
		int at = 0;

		//Too complicated to optimize WORDSIZED for now.
		//Just reduce it to two operations
		if(WORDSIZED)
		{
			//intCheck causes a check for interrupt between bytes of a 2-byte access
			//I'm not sure if it's an optimization to skip it sometimes, or what.
			//Note: sometimes it looks like this CPU core was allowing accesses straddling bank boundaries to move to the next bank.
			//I don't think that's right.. 
			//It seems bsnes does it but ares does not?
			if(reversed)
			{
				//todo addrh with >> 8 first!
				auto hiaddr = addr;
				hiaddr._addr++;
				rv = cpu_access_new<1,OP>(snes,hiaddr,value>>8,false,false);
				if(intCheck) snes->mycpu.cpu_checkInt();
				rv |= cpu_access_new<1,OP>(snes,addr,value,false,false)<<8;
			}
			else
			{
				rv = cpu_access_new<1,OP>(snes,addr,value,false,false);
				if(intCheck) snes->mycpu.cpu_checkInt();
				auto hiaddr = addr;
				hiaddr._addr += 1;
				rv |= cpu_access_new<1,OP>(snes,hiaddr,value>>8,false,false)<<8;
			}
			return rv;
		}

		auto a = addr.addr();
		auto b = addr.bank();

		//some apparently finetuned logic taken from ares (ISC license) and judged to be more or less identical to this codebase's MIT license
		//then again, I've revised this so it's a bit changed

		//TODO RETHINK LATER
		//The /ROMSEL signal on the card edge is literally just:
		//low if ((A22 high or A15 high) and (A23-A17 not equal to 0x7E))
		//The 5A22 translates the addresses and responds to offsets $4000..$43FF (if bank bit 6 is zero)

		//Bank 7E/7F is always patched on top by the system
		if((b>>1)==0x3F)
		{
			cpu_access_new_run_cyles_before<OP>(snes, 8);
			goto CASE_WRAM;
		}

		//Catch (almost) everything sent to the cart by the system.
		//00-3f,80-bf:8000-ffff; 40-7f,c0-ff:0000-ffff
		//TODO: like ares, pre-evaluate fastmem timings into a convenient place
		if(a & 0x408000) 
		{
			if(a & 0x800000)
			{
				cpu_access_new_run_cyles_before<OP>(snes, addr.FastCycles());
				goto CASE_CARTSPECIAL_RIGHTHALF;
			}
			else
			{
				cpu_access_new_run_cyles_before<OP>(snes, 8);
				goto CASE_CARTSPECIAL_LEFTHALF;
			}
		}

		//A bit more for the cart to deal with.. but also, detects the low ram
		//00-3f,80-bf:0000-1fff,6000-7fff
		if((a + 0x6000) & 0x4000)
		{
			//isolate low ram (0000-1fff). everything else goes to cart special logic
			if(a&0xE000)
			{
				if(a & 0x800000)
				{
					cpu_access_new_run_cyles_before<OP>(snes, addr.FastCycles());
					goto CASE_CARTSPECIAL_RIGHTHALF;
				}
				else
				{
					cpu_access_new_run_cyles_before<OP>(snes, 8);
					goto CASE_CARTSPECIAL_LEFTHALF;
				}
			}
			else
			{
				cpu_access_new_run_cyles_before<OP>(snes, 8);
				goto CASE_LOWRAM;
			}
		}

		//Detects the entire IO block range (except.. for.. a little bit..)
		//00-3f,80-bf:2000-3fff,4200-5fff
		if((a - 0x4000) & 0x7e00) 
		{
			cpu_access_new_run_cyles_before<OP>(snes, 8);
			goto CASE_IOBLOCK;
		}

		//..and the remainder is this
		//00-3f,80-bf:4000-41ff
		cpu_access_new_run_cyles_before<OP>(snes, 12);
		goto CASE_IOBLOCK;

		//.............................

	CASE_WRAM:
		at = ((addr.bank() & 1) << 16) | addr.addr();
		if(READTYPE)
			rv = snes->ram[at];
		else
			snes->ram[at] = (uint8_t)value;
		goto CASE_END;

	CASE_LOWRAM:
		at = addr.addr() & 0x1FFF;
		if(READTYPE)
			rv = snes->ram[at];
		else
			snes->ram[at] = (uint8_t)value;
		goto CASE_END;

	CASE_CARTSPECIAL_LEFTHALF:
		if(READTYPE)
			rv = snes->mycart.cart_readLoromByteNew(false,addr);
		else 
			snes->mycart.cart_writeLoromByteNew(false,addr,(uint8_t)value);
		goto CASE_END;

	CASE_CARTSPECIAL_RIGHTHALF:
		if(READTYPE)
			rv = snes->mycart.cart_readLoromByteNew(true,addr);
		else
			snes->mycart.cart_writeLoromByteNew(true,addr,(uint8_t)value);
		goto CASE_END;

	CASE_IOBLOCK:
		if(READTYPE)
			rv = snes->snes_readIO(addr.addr());
		else
			snes->snes_writeIO(addr.addr(),(uint8_t)value);
		goto CASE_END;

	CASE_END:

		cpu_access_new_run_cyles_after<OP>(snes);

		return rv;
	}

} //anonymous namespace

namespace LakeSnes
{

	//I know, it's not logical to have these on the cpu. hang on.
	uint8_t Cpu::dma_read(uint8_t bank, uint16_t adr)
	{
		return cpu_access_new<1,MemOp::DmaRead>(config.snes,MakeAddr24(bank,adr));
	}

	void Cpu::dma_write(uint8_t bank, uint16_t adr, uint8_t val)
	{
		cpu_access_new<1,MemOp::DmaWrite>(config.snes,MakeAddr24(bank,adr),val);
	}

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
			_xf = false;
			_mf = false;
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
			&c, &z, &v, &n, &i, &d, &_xf, &_mf, &e, &waiting, &stopped,
			&irqWanted, &nmiWanted, &intWanted, &intDelay, &resetWanted, NULL
		);
		sh_handleBytes(sh, &k, &db, NULL);
		sh_handleWords(sh, &a, &x, &y, &sp, &pc, &dp, NULL);
	}

	void Cpu::cpu_runOpcode() {
		if(resetWanted) {
			resetWanted = false;
			// reset: brk/interrupt without writes
			cpu_read(MakeAddr24(k,pc));
			cpu_idle();
			cpu_read(MakeAddr24(0, 0x100 | (sp-- & 0xff)));
			cpu_read(MakeAddr24(0, 0x100 | (sp-- & 0xff)));
			cpu_read(MakeAddr24(0, 0x100 | (sp-- & 0xff)));
			sp = (sp & 0xff) | 0x100;
			e = true;
			i = true;
			d = false;
			cpu_setFlags(cpu_getFlags()); // updates x and m flags, clears upper half of x and y if needed
			k = 0;
			pc = cpu_readWord(MakeAddr24(0,0xfffc),false);
			goto END;
		}
		if(stopped) {
			cpu_idleWait();
			goto END;
		}
		if(waiting) {
			if(irqWanted || nmiWanted) {
				waiting = false;
				cpu_idle();
				cpu_checkInt();
				cpu_idle();
				goto END;
			} else {
				cpu_idleWait();
				goto END;
			}
		}
		// not stopped or waiting, execute a opcode or go to interrupt
		if(intWanted) {
			cpu_read(MakeAddr24(k,pc));
			cpu_doInterrupt();
		} else {
			uint8_t opcode = cpu_readOpcode();
			cpu_doOpcode(opcode);
		}
	END:
		_catchup_cycles(config.snes);
	}

	void Cpu::cpu_nmi() {
		nmiWanted = true;
	}

	void Cpu::cpu_setIrq(bool state) {
		irqWanted = state;
	}

	uint8_t Cpu::cpu_read(Addr24 addr)
	{
		return cpu_access_new<1,MemOp::Read>(config.snes,addr,0,false,false);
	}

	uint16_t Cpu::cpu_readWord(Addr24 addr, bool intCheck)
	{
		return cpu_access_new<2,MemOp::Read>(config.snes,addr,0,false,intCheck);
	}

	void Cpu::cpu_write(Addr24 addr, uint8_t val)
	{
		cpu_access_new<1,MemOp::Write>(config.snes,addr,val,false,false);
	}

	void Cpu::cpu_writeWord(Addr24 addr, uint16_t value, bool reversed, bool intCheck)
	{
		cpu_access_new<2,MemOp::Write>(config.snes,addr,value,reversed,intCheck);
	}

	void Cpu::cpu_idle() {
		intDelay = false;
		cpu_access_new<1,MemOp::Idle>(config.snes,{0});
	}

	void Cpu::cpu_idleWait() {
		intDelay = false;
		cpu_access_new<1,MemOp::Idle>(config.snes,{0});
	}

	void Cpu::cpu_checkInt() {
		intWanted = (nmiWanted || (irqWanted && !i)) && !intDelay;
		intDelay = false;
	}

	uint8_t Cpu::cpu_readOpcode()
	{
		intDelay = false;
		return cpu_access_new<1,MemOp::Fetch>(config.snes,MakeAddr24(k,pc++),0,false,false);
	}

	uint16_t Cpu::cpu_readOpcodeWord(bool intCheck)
	{
		auto rv = (uint16_t)cpu_access_new<2,MemOp::Fetch>(config.snes,MakeAddr24(k,pc),0,false,intCheck);
		pc+=2;
		return rv;
	}

	uint8_t Cpu::cpu_getFlags() {
		uint8_t val = n << 7;
		val |= v << 6;
		val |= _mf << 5;
		val |= _xf << 4;
		val |= d << 3;
		val |= i << 2;
		val |= z << 1;
		val |= c;
		return val;
	}

	void Cpu::cpu_setFlags(uint8_t val) {
		n = val & 0x80;
		v = val & 0x40;
		_mf = val & 0x20;
		_xf = val & 0x10;
		d = val & 8;
		i = val & 4;
		z = val & 2;
		c = val & 1;
		if(e) {
			_mf = true;
			_xf = true;
			sp = (sp & 0xff) | 0x100;
		}
		if(_xf) {
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
		uint8_t rv = (uint8_t)cpu_access_new<1,MemOp::Read>(config.snes,MakeAddr24(0,sp));
		if(e) sp = (sp & 0xff) | 0x100;
		return rv;
	}

	void Cpu::cpu_pushByte(uint8_t value) {
		cpu_access_new<1,MemOp::Write>(config.snes,MakeAddr24(0,sp),value,false,false);
		sp--;
		if(e) sp = (sp & 0xff) | 0x100;
	}

	uint16_t Cpu::cpu_pullWord(bool intCheck)
	{
		//this can't be done internally with word-sized ops because of the special SP wrap-around incrementing behaviour
		uint8_t value = cpu_pullByte();
		if(intCheck) cpu_checkInt();
		return value | (cpu_pullByte() << 8);
	}

	void Cpu::cpu_pushWord(uint16_t value, bool intCheck)
	{
		//this can't be done internally with word-sized ops because of the special SP wrap-around incrementing behaviour
		cpu_pushByte(value >> 8);
		if(intCheck) cpu_checkInt();
		cpu_pushByte(value & 0xff);
	}

	//uint16_t Cpu::cpu_readWord(uint32_t adrl, uint32_t adrh, bool intCheck) {
	//	uint8_t value = cpu_read(adrl);
	//	if(intCheck) cpu_checkInt();
	//	return value | (cpu_read(adrh) << 8);
	//}

	//void Cpu::cpu_writeWord(uint32_t adrl, uint32_t adrh, uint16_t value, bool reversed, bool intCheck) {
	//	if(reversed) {
	//		cpu_write(adrh, value >> 8);
	//		if(intCheck) cpu_checkInt();
	//		cpu_write(adrl, value & 0xff);
	//	} else {
	//		cpu_write(adrl, value & 0xff);
	//		if(intCheck) cpu_checkInt();
	//		cpu_write(adrh, value >> 8);
	//	}
	//}

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
			pc = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,0xFFEA));
		} else { // irq
			pc = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,0xFFEE));
		}
	}

	// addressing modes

	//adds instruction implementations
	template<bool XF, bool MF> class TCpu : public Cpu
	{
	public:

		void cpu_adrImp() {
			// only for 2-cycle implied opcodes
			cpu_checkInt();
			if(intWanted) {
				// if interrupt detected in 2-cycle implied/accumulator opcode,
				// idle cycle turns into read from pc
				cpu_access_new<1,MemOp::Read>(config.snes,MakeAddr24(k,pc));
			} else {
				cpu_idle();
			}
		}

		Addr24 cpu_adrImm(bool xFlag) {
			if((xFlag && XF) || (!xFlag && MF)) {
				return MakeAddr24(k,pc++);
			} else {
				auto adr = pc;
				pc+=2;
				return MakeAddr24(k,adr);
			}
		}

		Addr24 cpu_adrDp() {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			return MakeAddr24(0,dp+adr);
		}

		Addr24 cpu_adrDpx() {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			cpu_idle();
			return MakeAddr24(0,dp+adr+x);
		}

		Addr24 cpu_adrDpy() {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			cpu_idle();
			return MakeAddr24(0,dp+adr+y);
		}

		Addr24 cpu_adrIdp() {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			uint16_t pointer = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,dp));
			return MakeAddr24(db,pointer);
		}

		Addr24 cpu_adrIdx() {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			cpu_idle();
			uint16_t pointer = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,dp + x));
			return MakeAddr24(db,pointer);
		}

		Addr24 cpu_adrIdy(bool write) {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			uint16_t pointer = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,dp + adr));
			// writing opcode or x = 0 or page crossed: 1 extra cycle
			if(write || !XF || ((pointer >> 8) != ((pointer + y) >> 8))) cpu_idle();
			return MakeAddr24(db,pointer+y);
		}

		Addr24 cpu_adrIdl() {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			uint16_t pointer = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,dp + adr));
			auto bank = cpu_read(MakeAddr24(0,dp + adr + 2));
			return MakeAddr24(bank, pointer);
		}

		Addr24 cpu_adrIly() {
			uint8_t adr = cpu_readOpcode();
			if(dp & 0xff) cpu_idle(); // dpr not 0: 1 extra cycle
			uint16_t pointer = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,dp + adr));
			auto bank = cpu_read(MakeAddr24(0,dp + adr + 2));
			return MakeAddr24(bank, pointer + y);
		}

		Addr24 cpu_adrSr() {
			uint8_t adr = cpu_readOpcode();
			cpu_idle();
			return MakeAddr24(0,sp + adr); //note: DB is 0 for stack
		}

		Addr24 cpu_adrIsy() {
			uint8_t adr = cpu_readOpcode();
			cpu_idle();
			uint16_t pointer = (uint16_t)cpu_access_new<2,MemOp::Read>(config.snes,MakeAddr24(0,sp + adr));
			cpu_idle();
			pointer += y;
			return MakeAddr24(db,pointer);
		}

		Addr24 cpu_adrAbs() {
			uint16_t adr = cpu_readOpcodeWord(false);
			return MakeAddr24(db,adr);
		}

		Addr24 cpu_adrAbx(bool write) {
			uint16_t adr = cpu_readOpcodeWord(false);
			// writing opcode or x = 0 or page crossed: 1 extra cycle
			if(write || !XF || ((adr >> 8) != ((adr + x) >> 8))) cpu_idle();
			adr += x;
			return MakeAddr24(db,adr);
		}

		Addr24 cpu_adrAby(bool write) {
			uint16_t adr = cpu_readOpcodeWord(false);
			// writing opcode or x = 0 or page crossed: 1 extra cycle
			if(write || !XF || ((adr >> 8) != ((adr + y) >> 8))) cpu_idle();
			adr += y;
			return MakeAddr24(db,adr);
		}

		Addr24 cpu_adrAbl() {
			uint32_t adr = cpu_readOpcodeWord(false);
			auto bank = cpu_readOpcode();
			return MakeAddr24(bank,adr);
		}

		Addr24 cpu_adrAlx() {
			uint32_t adr = cpu_readOpcodeWord(false);
			auto bank = cpu_readOpcode();
			adr += x;
			return MakeAddr24(bank,adr);
		}

		// opcode functions

		void cpu_and(Addr24 addr) {
			if(MF) {
				cpu_checkInt();
				uint8_t value = cpu_read(addr);
				a = (a & 0xff00) | ((a & value) & 0xff);
			} else {
				uint16_t value = cpu_readWord(addr, true);
				a &= value;
			}
			cpu_setZN(a, MF);
		}

		void cpu_ora(Addr24 addr) {
			if(MF) {
				cpu_checkInt();
				uint8_t value = cpu_read(addr);
				a = (a & 0xff00) | ((a | value) & 0xff);
			} else {
				uint16_t value = cpu_readWord(addr, true);
				a |= value;
			}
			cpu_setZN(a, MF);
		}

		void cpu_eor(Addr24 addr) {
			if(MF) {
				cpu_checkInt();
				uint8_t value = cpu_read(addr);
				a = (a & 0xff00) | ((a ^ value) & 0xff);
			} else {
				uint16_t value = cpu_readWord(addr, true);
				a ^= value;
			}
			cpu_setZN(a, MF);
		}

		void cpu_adc(Addr24 addr) {
			if(MF) {
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
			cpu_setZN(a, MF);
		}

		void cpu_sbc(Addr24 addr) {
			if(MF) {
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
			cpu_setZN(a, MF);
		}

		void cpu_cmp(Addr24 addr) {
			int result = 0;
			if(MF) {
				cpu_checkInt();
				uint8_t value = cpu_read(addr) ^ 0xff;
				result = (a & 0xff) + value + 1;
				c = result > 0xff;
			} else {
				uint16_t value = cpu_readWord(addr, true) ^ 0xffff;
				result = a + value + 1;
				c = result > 0xffff;
			}
			cpu_setZN(result, MF);
		}

		void cpu_cpx(Addr24 addr) {
			int result = 0;
			if(XF) {
				cpu_checkInt();
				uint8_t value = cpu_read(addr) ^ 0xff;
				result = (x & 0xff) + value + 1;
				c = result > 0xff;
			} else {
				uint16_t value = cpu_readWord(addr, true) ^ 0xffff;
				result = x + value + 1;
				c = result > 0xffff;
			}
			cpu_setZN(result, XF);
		}

		void cpu_cpy(Addr24 addr) {
			int result = 0;
			if(XF) {
				cpu_checkInt();
				uint8_t value = cpu_read(addr) ^ 0xff;
				result = (y & 0xff) + value + 1;
				c = result > 0xff;
			} else {
				uint16_t value = cpu_readWord(addr, true) ^ 0xffff;
				result = y + value + 1;
				c = result > 0xffff;
			}
			cpu_setZN(result, XF);
		}

		void cpu_bit(Addr24 addr) {
			if(MF) {
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

		void cpu_lda(Addr24 addr) {
			if(MF) {
				cpu_checkInt();
				a = (a & 0xff00) | cpu_read(addr);
			} else {
				a = cpu_readWord(addr, true);
			}
			cpu_setZN(a, MF);
		}

		void cpu_ldx(Addr24 addr) {
			if(XF) {
				cpu_checkInt();
				x = cpu_read(addr);
			} else {
				x = cpu_readWord(addr, true);
			}
			cpu_setZN(x, XF);
		}

		void cpu_ldy(Addr24 addr) {
			if(XF) {
				cpu_checkInt();
				y = cpu_read(addr);
			} else {
				y = cpu_readWord(addr, true);
			}
			cpu_setZN(y, XF);
		}

		void cpu_sta(Addr24 addr) {
			if(MF) {
				cpu_checkInt();
				cpu_write(addr, a);
			} else {
				cpu_writeWord(addr, a, false, true);
			}
		}

		void cpu_stx(Addr24 addr) {
			if(XF) {
				cpu_checkInt();
				cpu_write(addr, x);
			} else {
				cpu_writeWord(addr, x, false, true);
			}
		}

		void cpu_sty(Addr24 addr) {
			if(XF) {
				cpu_checkInt();
				cpu_write(addr, y);
			} else {
				cpu_writeWord(addr, y, false, true);
			}
		}

		void cpu_stz(Addr24 addr) {
			if(MF) {
				cpu_checkInt();
				cpu_write(addr, 0);
			} else {
				cpu_writeWord(addr, 0, false, true);
			}
		}

		void cpu_ror(Addr24 addr) {
			bool carry = false;
			int result = 0;
			if(MF) {
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
			cpu_setZN(result, MF);
			c = carry;
		}

		void cpu_rol(Addr24 addr) {
			int result = 0;
			if(MF) {
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
			cpu_setZN(result, MF);
		}

		void cpu_lsr(Addr24 addr) {
			int result = 0;
			if(MF) {
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
			cpu_setZN(result, MF);
		}

		void cpu_asl(Addr24 addr) {
			int result = 0;
			if(MF) {
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
			cpu_setZN(result, MF);
		}

		void cpu_inc(Addr24 addr) {
			int result = 0;
			if(MF) {
				result = cpu_read(addr) + 1;
				cpu_idle();
				cpu_checkInt();
				cpu_write(addr, result);
			} else {
				result = cpu_readWord(addr, false) + 1;
				cpu_idle();
				cpu_writeWord(addr, result, true, true);
			}
			cpu_setZN(result, MF);
		}

		void cpu_dec(Addr24 addr) {
			int result = 0;
			if(MF) {
				result = cpu_read(addr) - 1;
				cpu_idle();
				cpu_checkInt();
				cpu_write(addr, result);
			} else {
				result = cpu_readWord(addr, false) - 1;
				cpu_idle();
				cpu_writeWord(addr, result, true, true);
			}
			cpu_setZN(result, MF);
		}

		void cpu_tsb(Addr24 addr) {
			if(MF) {
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

		void cpu_trb(Addr24 addr) {
			if(MF) {
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


		void _cpu_doOpcode(uint8_t opcode) {
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
					pc = cpu_readWord(MakeAddr24(0,vector),true);
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
					pc = cpu_readWord(MakeAddr24(0,vector),true);
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
					if(MF) {
						c = a & 0x80;
						a = (a & 0xff00) | ((a << 1) & 0xff);
					} else {
						c = a & 0x8000;
						a <<= 1;
					}
					cpu_setZN(a, MF);
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
					if(MF) {
						a = (a & 0xff00) | ((a + 1) & 0xff);
					} else {
						a++;
					}
					cpu_setZN(a, MF);
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
					if(MF) {
						c = result & 0x100;
						a = (a & 0xff00) | (result & 0xff);
					} else {
						c = result & 0x10000;
						a = result;
					}
					cpu_setZN(a, MF);
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
					if(MF) {
						a = (a & 0xff00) | ((a - 1) & 0xff);
					} else {
						a--;
					}
					cpu_setZN(a, MF);
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
					cpu_write(MakeAddr24(dest, y), cpu_read(MakeAddr24(src, x)));
					a--;
					x--;
					y--;
					if(a != 0xffff) {
						pc -= 3;
					}
					if(XF) {
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
					if(MF) {
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
					if(MF) {
						a = (a & 0xff00) | ((a >> 1) & 0x7f);
					} else {
						a >>= 1;
					}
					cpu_setZN(a, MF);
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
					cpu_write(MakeAddr24(dest, y), cpu_read(MakeAddr24(src, x)));
					a--;
					x++;
					y++;
					if(a != 0xffff) {
						pc -= 3;
					}
					if(XF) {
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
					if(XF) {
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
					if(MF) {
						cpu_checkInt();
						a = (a & 0xff00) | cpu_pullByte();
					} else {
						a = cpu_pullWord(true);
					}
					cpu_setZN(a, MF);
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
					if(MF) {
						a = (a & 0xff00) | ((a >> 1) & 0x7f) | (c << 7);
					} else {
						a = (a >> 1) | (c << 15);
					}
					c = carry;
					cpu_setZN(a, MF);
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
					pc = cpu_readWord(MakeAddr24(0,adr),true);
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
					if(XF) {
						cpu_checkInt();
						y = cpu_pullByte();
					} else {
						y = cpu_pullWord(true);
					}
					cpu_setZN(y, XF);
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
					pc = cpu_readWord(MakeAddr24(k,adr + x),true);
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
					if(XF) {
						y = (y - 1) & 0xff;
					} else {
						y--;
					}
					cpu_setZN(y, XF);
					break;
				}
				case 0x89: { // biti imm(m)
					if(MF) {
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
					if(MF) {
						a = (a & 0xff00) | (x & 0xff);
					} else {
						a = x;
					}
					cpu_setZN(a, MF);
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
					if(MF) {
						a = (a & 0xff00) | (y & 0xff);
					} else {
						a = y;
					}
					cpu_setZN(a, MF);
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
					if(XF) {
						y = x & 0xff;
					} else {
						y = x;
					}
					cpu_setZN(y, XF);
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
					if(XF) {
						y = a & 0xff;
					} else {
						y = a;
					}
					cpu_setZN(y, XF);
					break;
				}
				case 0xa9: { // lda imm(m)
					auto addr = cpu_adrImm(false);
					cpu_lda(addr);
					break;
				}
				case 0xaa: { // tax imp
					cpu_adrImp();
					if(XF) {
						x = a & 0xff;
					} else {
						x = a;
					}
					cpu_setZN(x, XF);
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
					if(XF) {
						x = sp & 0xff;
					} else {
						x = sp;
					}
					cpu_setZN(x, XF);
					break;
				}
				case 0xbb: { // tyx imp
					cpu_adrImp();
					if(XF) {
						x = y & 0xff;
					} else {
						x = y;
					}
					cpu_setZN(x, XF);
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
					if(XF) {
						y = (y + 1) & 0xff;
					} else {
						y++;
					}
					cpu_setZN(y, XF);
					break;
				}
				case 0xc9: { // cmp imm(m)
					auto addr = cpu_adrImm(false);
					cpu_cmp(addr);
					break;
				}
				case 0xca: { // dex imp
					cpu_adrImp();
					if(XF) {
						x = (x - 1) & 0xff;
					} else {
						x--;
					}
					cpu_setZN(x, XF);
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
					if(XF) {
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
					pc = cpu_readWord(MakeAddr24(0,adr),false);
					cpu_checkInt();
					k = cpu_read(MakeAddr24(0, adr + 2));
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
					if(XF) {
						x = (x + 1) & 0xff;
					} else {
						x++;
					}
					cpu_setZN(x, XF);
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
					if(XF) {
						cpu_checkInt();
						x = cpu_pullByte();
					} else {
						x = cpu_pullWord(true);
					}
					cpu_setZN(x, XF);
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
					pc = cpu_readWord(MakeAddr24(k,adr + x),true);
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
		} //_cpu_doOpcode

	}; //class TCpu


	void Cpu::cpu_doOpcode(uint8_t opcode)
	{
		//looks awkward but runs okay on arm, just a couple of 'cbz' instructions
		if(_xf)
			if(_mf)
				((TCpu<true,true>*)this)->_cpu_doOpcode(opcode);
			else 
				((TCpu<true,false>*)this)->_cpu_doOpcode(opcode);
		else 
			if(_mf)
				((TCpu<false,true>*)this)->_cpu_doOpcode(opcode);
			else 
				((TCpu<false,false>*)this)->_cpu_doOpcode(opcode);
	}

}