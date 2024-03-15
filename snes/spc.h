#pragma once

#include <stdint.h>

namespace LakeSnes
{
	class Apu;
	class Snes;

	class Spc
	{
	public:
		struct {
			Apu* apu;
			Snes* snes;
		} config;

		// registers
		uint8_t a;
		uint8_t x;
		uint8_t y;
		uint8_t sp;
		uint16_t pc;
		// flags
		bool c;
		bool z;
		bool v;
		bool n;
		bool i;
		bool h;
		bool p;
		bool b;
		// stopping
		bool stopped;
		// reset
		bool resetWanted;
		// single-cycle
		uint8_t opcode;
		uint32_t step;
		uint32_t bstep;
		uint16_t adr;
		uint16_t adr1;
		uint8_t dat;
		uint16_t dat16;
		uint8_t param;

		void spc_init(Apu* apu);
		void spc_free();
		void spc_reset(bool hard);
		void spc_runOpcode();

	private:
		uint8_t spc_read(uint16_t adr);
		void spc_write(uint16_t adr, uint8_t val);
		void spc_idle();
		void spc_idleWait();
		uint8_t spc_readOpcode();
		uint16_t spc_readOpcodeWord();
		uint8_t spc_getFlags();
		void spc_setFlags(uint8_t value);
		void spc_setZN(uint8_t value);
		void spc_doBranch(uint8_t value, bool check);
		uint8_t spc_pullByte();
		void spc_pushByte(uint8_t value);
		uint16_t spc_pullWord();
		void spc_pushWord(uint16_t value);
		uint16_t spc_readWord(uint16_t adrl, uint16_t adrh);
		void spc_writeWord(uint16_t adrl, uint16_t adrh, uint16_t value);
		void spc_doOpcode(uint8_t opcode);

		// adressing modes
		uint16_t spc_adrDp();
		uint16_t spc_adrAbs();
		void spc_adrAbs_stepped();
		uint16_t spc_adrInd();
		uint16_t spc_adrIdx();
		void spc_adrIdx_stepped();
		uint16_t spc_adrImm();
		uint16_t spc_adrDpx();
		void spc_adrDpx_stepped();
		void spc_adrDpy_stepped();
		uint16_t spc_adrAbx();
		void spc_adrAbx_stepped();
		uint16_t spc_adrAby();
		void spc_adrAby_stepped();
		uint16_t spc_adrIdy();
		void spc_adrIdy_stepped();
		uint16_t spc_adrDpDp(uint8_t* srcVal);
		void spc_adrDpDp_stepped();
		uint16_t spc_adrDpImm(uint8_t* srcVal);
		uint16_t spc_adrIndInd(uint8_t* srcVal);
		uint8_t spc_adrAbsBit(uint16_t* adr);
		void spc_adrAbsBit_stepped();
		uint16_t spc_adrDpWord(uint16_t* low);
		uint16_t spc_adrIndP();

		// opcode functions
		void spc_and(uint16_t adr);
		void spc_andm(uint16_t dst, uint8_t value);
		void spc_or(uint16_t adr);
		void spc_orm(uint16_t dst, uint8_t value);
		void spc_eor(uint16_t adr);
		void spc_eorm(uint16_t dst, uint8_t value);
		void spc_adc(uint16_t adr);
		void spc_adcm(uint16_t dst, uint8_t value);
		void spc_sbc(uint16_t adr);
		void spc_sbcm(uint16_t dst, uint8_t value);
		void spc_cmp(uint16_t adr);
		void spc_cmpx(uint16_t adr);
		void spc_cmpy(uint16_t adr);
		void spc_cmpm(uint16_t dst, uint8_t value);
		void spc_mov(uint16_t adr);
		void spc_movx(uint16_t adr);
		void spc_movy(uint16_t adr);
		void spc_movs(uint16_t adr);
		void spc_movsx(uint16_t adr);
		void spc_movsy(uint16_t adr);
		void spc_asl(uint16_t adr);
		void spc_lsr(uint16_t adr);
		void spc_rol(uint16_t adr);
		void spc_ror(uint16_t adr);
		void spc_inc(uint16_t adr);
		void spc_dec(uint16_t adr);
	};



}