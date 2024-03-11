#pragma once

#include <stdint.h>

namespace LakeSnes
{
	class Snes;
	struct StateHandler;

	class Cpu
	{
	public:

		void cpu_init(Snes* snes);
		void cpu_reset(bool hard);
		void cpu_handleState(StateHandler* sh);
		void cpu_runOpcode();
		void cpu_nmi();
		void cpu_setIrq(bool state);

		uint8_t cpu_read(uint32_t adr);
		void cpu_checkInt();

	private:

		struct Addr24
		{
			uint8_t bank;
			uint8_t dummy;
			uint16_t addr;

			uint16_t low() const { return addr; }
			uint16_t high() const { return (uint16_t)(addr+1); }
		};

		Addr24 MakeAddr24(uint8_t bank, uint16_t addr)
		{
			return Addr24 { bank, 0, addr };
		}

		void cpu_write(uint32_t adr, uint8_t val);
		void cpu_idle();
		void cpu_idleWait();
		uint8_t cpu_readOpcode();
		uint16_t cpu_readOpcodeWord(bool intCheck);
		uint8_t cpu_getFlags();
		void cpu_setFlags(uint8_t value);
		void cpu_setZN(uint16_t value, bool byte);
		void cpu_doBranch(bool check);
		uint8_t cpu_pullByte();
		void cpu_pushByte(uint8_t value);
		uint16_t cpu_pullWord(bool intCheck);
		void cpu_pushWord(uint16_t value, bool intCheck);
		uint16_t cpu_readWord(uint32_t adrl, uint32_t adrh, bool intCheck);
		void cpu_writeWord(uint32_t adrl, uint32_t adrh, uint16_t value, bool reversed, bool intCheck);
		void cpu_doInterrupt();
		void cpu_doOpcode(uint8_t opcode);

		uint8_t cpu_read(Addr24 addr);
		uint16_t cpu_readWord(Addr24 addr, bool intCheck);
		void cpu_write(Addr24 addr, uint8_t val);
		void cpu_writeWord(Addr24 addr, uint16_t value, bool reversed, bool intCheck);

		// addressing modes
		void cpu_adrImp();
		[[nodiscard]] Addr24 cpu_adrImm(bool xFlag);
		[[nodiscard]] Addr24 cpu_adrDp();
		[[nodiscard]] Addr24 cpu_adrDpx();
		[[nodiscard]] Addr24 cpu_adrDpy();
		[[nodiscard]] Addr24 cpu_adrIdp();
		[[nodiscard]] Addr24 cpu_adrIdx();
		[[nodiscard]] Addr24 cpu_adrIdy(bool write);
		[[nodiscard]] Addr24 cpu_adrIdl();
		[[nodiscard]] Addr24 cpu_adrIly();
		[[nodiscard]] Addr24 cpu_adrSr();
		[[nodiscard]] Addr24 cpu_adrIsy();
		[[nodiscard]] Addr24 cpu_adrAbs();
		[[nodiscard]] Addr24 cpu_adrAbx(bool write);
		[[nodiscard]] Addr24 cpu_adrAby(bool write);
		[[nodiscard]] Addr24 cpu_adrAbl();
		[[nodiscard]] Addr24 cpu_adrAlx();

		// opcode functions
		void cpu_and(Addr24 addr);
		void cpu_ora(Addr24 addr);
		void cpu_eor(Addr24 addr);
		void cpu_adc(Addr24 addr);
		void cpu_sbc(Addr24 addr);
		void cpu_cmp(Addr24 addr);
		void cpu_cpx(Addr24 addr);
		void cpu_cpy(Addr24 addr);
		void cpu_bit(Addr24 addr);
		void cpu_lda(Addr24 addr);
		void cpu_ldx(Addr24 addr);
		void cpu_ldy(Addr24 addr);
		void cpu_sta(Addr24 addr);
		void cpu_stx(Addr24 addr);
		void cpu_sty(Addr24 addr);
		void cpu_stz(Addr24 addr);
		void cpu_ror(Addr24 addr);
		void cpu_rol(Addr24 addr);
		void cpu_lsr(Addr24 addr);
		void cpu_asl(Addr24 addr);
		void cpu_inc(Addr24 addr);
		void cpu_dec(Addr24 addr);
		void cpu_tsb(Addr24 addr);
		void cpu_trb(Addr24 addr);

	public:
		struct {
			Snes* snes;
		} config;
		// registers
		uint16_t a;
		uint16_t x;
		uint16_t y;
		uint16_t sp;
		uint16_t pc;
		uint16_t dp; // direct page (D)
		uint8_t k; // program bank (PB)
		uint8_t db; // data bank (B)
		// flags
		bool c;
		bool z;
		bool v;
		bool n;
		bool i;
		bool d;
		bool xf;
		bool mf;
		bool e;
		// power state (WAI/STP)
		bool waiting;
		bool stopped;
		// interrupts
		bool irqWanted;
		bool nmiWanted;
		bool intWanted;
		bool intDelay;
		bool resetWanted;
	};

}
