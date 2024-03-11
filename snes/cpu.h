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

	private:

		void cpu_write(uint32_t adr, uint8_t val);
		void cpu_idle();
		void cpu_idleWait();
		void cpu_checkInt();
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


		// addressing modes
		void cpu_adrImp();
		uint32_t cpu_adrImm(uint32_t* low, bool xFlag);
		uint32_t cpu_adrDp(uint32_t* low);
		uint32_t cpu_adrDpx(uint32_t* low);
		uint32_t cpu_adrDpy(uint32_t* low);
		uint32_t cpu_adrIdp(uint32_t* low);
		uint32_t cpu_adrIdx(uint32_t* low);
		uint32_t cpu_adrIdy(uint32_t* low, bool write);
		uint32_t cpu_adrIdl(uint32_t* low);
		uint32_t cpu_adrIly(uint32_t* low);
		uint32_t cpu_adrSr(uint32_t* low);
		uint32_t cpu_adrIsy(uint32_t* low);
		uint32_t cpu_adrAbs(uint32_t* low);
		uint32_t cpu_adrAbx(uint32_t* low, bool write);
		uint32_t cpu_adrAby(uint32_t* low, bool write);
		uint32_t cpu_adrAbl(uint32_t* low);
		uint32_t cpu_adrAlx(uint32_t* low);

		// opcode functions
		void cpu_and(uint32_t low, uint32_t high);
		void cpu_ora(uint32_t low, uint32_t high);
		void cpu_eor(uint32_t low, uint32_t high);
		void cpu_adc(uint32_t low, uint32_t high);
		void cpu_sbc(uint32_t low, uint32_t high);
		void cpu_cmp(uint32_t low, uint32_t high);
		void cpu_cpx(uint32_t low, uint32_t high);
		void cpu_cpy(uint32_t low, uint32_t high);
		void cpu_bit(uint32_t low, uint32_t high);
		void cpu_lda(uint32_t low, uint32_t high);
		void cpu_ldx(uint32_t low, uint32_t high);
		void cpu_ldy(uint32_t low, uint32_t high);
		void cpu_sta(uint32_t low, uint32_t high);
		void cpu_stx(uint32_t low, uint32_t high);
		void cpu_sty(uint32_t low, uint32_t high);
		void cpu_stz(uint32_t low, uint32_t high);
		void cpu_ror(uint32_t low, uint32_t high);
		void cpu_rol(uint32_t low, uint32_t high);
		void cpu_lsr(uint32_t low, uint32_t high);
		void cpu_asl(uint32_t low, uint32_t high);
		void cpu_inc(uint32_t low, uint32_t high);
		void cpu_dec(uint32_t low, uint32_t high);
		void cpu_tsb(uint32_t low, uint32_t high);
		void cpu_trb(uint32_t low, uint32_t high);

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
