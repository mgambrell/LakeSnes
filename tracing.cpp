#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "tracing.h"
#include "snes.h"
#include "apu.h"

// name for each opcode, to be filled in with sprintf (length = 14 (13+\0))
static const char* opcodeNames[256] = {
  "brk #$%02x     ", "ora ($%02x,x)  ", "cop #$%02x     ", "ora $%02x,s    ", "tsb $%02x      ",   "ora $%02x      ", "asl $%02x      ", "ora [$%02x]    ", "php          ", "ora #$%s   ",   "asl          ", "phd          ", "tsb $%04x    ", "ora $%04x    ", "asl $%04x    ", "ora $%06x  ",
  "bpl $%04x    ",   "ora ($%02x),y  ", "ora ($%02x)    ", "ora ($%02x,s),y", "trb $%02x      ",   "ora $%02x,x    ", "asl $%02x,x    ", "ora [$%02x],y  ", "clc          ", "ora $%04x,y  ", "inc          ", "tcs          ", "trb $%04x    ", "ora $%04x,x  ", "asl $%04x,x  ", "ora $%06x,x",
  "jsr $%04x    ",   "and ($%02x,x)  ", "jsl $%06x  ",     "and $%02x,s    ", "bit $%02x      ",   "and $%02x      ", "rol $%02x      ", "and [$%02x]    ", "plp          ", "and #$%s   ",   "rol          ", "pld          ", "bit $%04x    ", "and $%04x    ", "rol $%04x    ", "and $%06x  ",
  "bmi $%04x    ",   "and ($%02x),y  ", "and ($%02x)    ", "and ($%02x,s),y", "bit $%02x,x    ",   "and $%02x,x    ", "rol $%02x,x    ", "and [$%02x],y  ", "sec          ", "and $%04x,y  ", "dec          ", "tsc          ", "bit $%04x,x  ", "and $%04x,x  ", "rol $%04x,x  ", "and $%06x,x",
  "rti          ",   "eor ($%02x,x)  ", "wdm #$%02x     ", "eor $%02x,s    ", "mvp $%02x, $%02x ", "eor $%02x      ", "lsr $%02x      ", "eor [$%02x]    ", "pha          ", "eor #$%s   ",   "lsr          ", "phk          ", "jmp $%04x    ", "eor $%04x    ", "lsr $%04x    ", "eor $%06x  ",
  "bvc $%04x    ",   "eor ($%02x),y  ", "eor ($%02x)    ", "eor ($%02x,s),y", "mvn $%02x, $%02x ", "eor $%02x,x    ", "lsr $%02x,x    ", "eor [$%02x],y  ", "cli          ", "eor $%04x,y  ", "phy          ", "tcd          ", "jml $%06x  ",   "eor $%04x,x  ", "lsr $%04x,x  ", "eor $%06x,x",
  "rts          ",   "adc ($%02x,x)  ", "per $%04x    ",   "adc $%02x,s    ", "stz $%02x      ",   "adc $%02x      ", "ror $%02x      ", "adc [$%02x]    ", "pla          ", "adc #$%s   ",   "ror          ", "rtl          ", "jmp ($%04x)  ", "adc $%04x    ", "ror $%04x    ", "adc $%06x  ",
  "bvs $%04x    ",   "adc ($%02x),y  ", "adc ($%02x)    ", "adc ($%02x,s),y", "stz $%02x,x    ",   "adc $%02x,x    ", "ror $%02x,x    ", "adc [$%02x],y  ", "sei          ", "adc $%04x,y  ", "ply          ", "tdc          ", "jmp ($%04x,x)", "adc $%04x,x  ", "ror $%04x,x  ", "adc $%06x,x",
  "bra $%04x    ",   "sta ($%02x,x)  ", "brl $%04x    ",   "sta $%02x,s    ", "sty $%02x      ",   "sta $%02x      ", "stx $%02x      ", "sta [$%02x]    ", "dey          ", "bit #$%s   ",   "txa          ", "phb          ", "sty $%04x    ", "sta $%04x    ", "stx $%04x    ", "sta $%06x  ",
  "bcc $%04x    ",   "sta ($%02x),y  ", "sta ($%02x)    ", "sta ($%02x,s),y", "sty $%02x,x    ",   "sta $%02x,x    ", "stx $%02x,y    ", "sta [$%02x],y  ", "tya          ", "sta $%04x,y  ", "txs          ", "txy          ", "stz $%04x    ", "sta $%04x,x  ", "stz $%04x,x  ", "sta $%06x,x",
  "ldy #$%s   ",     "lda ($%02x,x)  ", "ldx #$%s   ",     "lda $%02x,s    ", "ldy $%02x      ",   "lda $%02x      ", "ldx $%02x      ", "lda [$%02x]    ", "tay          ", "lda #$%s   ",   "tax          ", "plb          ", "ldy $%04x    ", "lda $%04x    ", "ldx $%04x    ", "lda $%06x  ",
  "bcs $%04x    ",   "lda ($%02x),y  ", "lda ($%02x)    ", "lda ($%02x,s),y", "ldy $%02x,x    ",   "lda $%02x,x    ", "ldx $%02x,y    ", "lda [$%02x],y  ", "clv          ", "lda $%04x,y  ", "tsx          ", "tyx          ", "ldy $%04x,x  ", "lda $%04x,x  ", "ldx $%04x,y  ", "lda $%06x,x",
  "cpy #$%s   ",     "cmp ($%02x,x)  ", "rep #$%02x     ", "cmp $%02x,s    ", "cpy $%02x      ",   "cmp $%02x      ", "dec $%02x      ", "cmp [$%02x]    ", "iny          ", "cmp #$%s   ",   "dex          ", "wai          ", "cpy $%04x    ", "cmp $%04x    ", "dec $%04x    ", "cmp $%06x  ",
  "bne $%04x    ",   "cmp ($%02x),y  ", "cmp ($%02x)    ", "cmp ($%02x,s),y", "pei $%02x      ",   "cmp $%02x,x    ", "dec $%02x,x    ", "cmp [$%02x],y  ", "cld          ", "cmp $%04x,y  ", "phx          ", "stp          ", "jml [$%04x]  ", "cmp $%04x,x  ", "dec $%04x,x  ", "cmp $%06x,x",
  "cpx #$%s   ",     "sbc ($%02x,x)  ", "sep #$%02x     ", "sbc $%02x,s    ", "cpx $%02x      ",   "sbc $%02x      ", "inc $%02x      ", "sbc [$%02x]    ", "inx          ", "sbc #$%s   ",   "nop          ", "xba          ", "cpx $%04x    ", "sbc $%04x    ", "inc $%04x    ", "sbc $%06x  ",
  "beq $%04x    ",   "sbc ($%02x),y  ", "sbc ($%02x)    ", "sbc ($%02x,s),y", "pea #$%04x   ",     "sbc $%02x,x    ", "inc $%02x,x    ", "sbc [$%02x],y  ", "sed          ", "sbc $%04x,y  ", "plx          ", "xce          ", "jsr ($%04x,x)", "sbc $%04x,x  ", "inc $%04x,x  ", "sbc $%06x,x"
};

// address types for each opcode
static const int opcodeType[256] = {
  1, 1, 1, 1, 1, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 1, 1, 1, 1, 0, 2, 0, 0, 2, 2, 2, 3,
  2, 1, 3, 1, 1, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 1, 1, 1, 1, 0, 2, 0, 0, 2, 2, 2, 3,
  0, 1, 1, 1, 8, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 8, 1, 1, 1, 0, 2, 0, 0, 3, 2, 2, 3,
  0, 1, 7, 1, 1, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 1, 1, 1, 1, 0, 2, 0, 0, 2, 2, 2, 3,
  6, 1, 7, 1, 1, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 1, 1, 1, 1, 0, 2, 0, 0, 2, 2, 2, 3,
  5, 1, 5, 1, 1, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 1, 1, 1, 1, 0, 2, 0, 0, 2, 2, 2, 3,
  5, 1, 1, 1, 1, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 1, 1, 1, 1, 0, 2, 0, 0, 2, 2, 2, 3,
  5, 1, 1, 1, 1, 1, 1, 1, 0, 4, 0, 0, 2, 2, 2, 3,
  6, 1, 1, 1, 2, 1, 1, 1, 0, 2, 0, 0, 2, 2, 2, 3
};

// name for each opcode, for spc (length = 18 (17+\0))
static const char* opcodeNamesSpc[256] = {
  "nop              ", "tcall 0          ", "set1 $%02x.0       ", "bbs $%02x.0, $%04x ", "or a, $%02x        ", "or a, $%04x      ", "or a, (x)        ", "or a, ($%02x+x)    ", "or a, #$%02x       ",   "or $%02x, $%02x      ", "or1 c, $%04x.%01x   ",  "asl $%02x          ", "asl $%04x        ", "push p           ",   "tset $%04x, a    ",   "brk              ",
  "bpl $%04x        ", "tcall 1          ", "clr1 $%02x.0       ", "bbc $%02x.0, $%04x ", "or a, $%02x+x      ", "or a, $%04x+x    ", "or a, $%04x+y    ", "or a, ($%02x)+y    ", "or $%02x, #$%02x     ", "or (x), (y)      ",     "decw $%02x         ",   "asl $%02x+x        ", "asl a            ", "dec x            ",   "cmp x, $%04x     ",   "jmp ($%04x+x)    ",
  "clrp             ", "tcall 2          ", "set1 $%02x.1       ", "bbs $%02x.1, $%04x ", "and a, $%02x       ", "and a, $%04x     ", "and a, (x)       ", "and a, ($%02x+x)   ", "and a, #$%02x      ",   "and $%02x, $%02x     ", "or1 c, /$%04x.%01x  ",  "rol $%02x          ", "rol $%04x        ", "push a           ",   "cbne $%02x, $%04x  ", "bra $%04x        ",
  "bmi $%04x        ", "tcall 3          ", "clr1 $%02x.1       ", "bbc $%02x.1, $%04x ", "and a, $%02x+x     ", "and a, $%04x+x   ", "and a, $%04x+y   ", "and a, ($%02x)+y   ", "and $%02x, #$%02x    ", "and (x), (y)     ",     "incw $%02x         ",   "rol $%02x+x        ", "rol a            ", "inc x            ",   "cmp x, $%02x       ", "call $%04x       ",
  "setp             ", "tcall 4          ", "set1 $%02x.2       ", "bbs $%02x.2, $%04x ", "eor a, $%02x       ", "eor a, $%04x     ", "eor a, (x)       ", "eor a, ($%02x+x)   ", "eor a, #$%02x      ",   "eor $%02x, $%02x     ", "and1 c, $%04x.%01x  ",  "lsr $%02x          ", "lsr $%04x        ", "push x           ",   "tclr $%04x, a    ",   "pcall $%02x        ",
  "bvc $%04x        ", "tcall 5          ", "clr1 $%02x.2       ", "bbc $%02x.2, $%04x ", "eor a, $%02x+x     ", "eor a, $%04x+x   ", "eor a, $%04x+y   ", "eor a, ($%02x)+y   ", "eor $%02x, #$%02x    ", "eor (x), (y)     ",     "cmpw ya, $%02x     ",   "lsr $%02x+x        ", "lsr a            ", "mov x, a         ",   "cmp y, $%04x     ",   "jmp $%04x        ",
  "clrc             ", "tcall 6          ", "set1 $%02x.3       ", "bbs $%02x.3, $%04x ", "cmp a, $%02x       ", "cmp a, $%04x     ", "cmp a, (x)       ", "cmp a, ($%02x+x)   ", "cmp a, #$%02x      ",   "cmp $%02x, $%02x     ", "and1 c, /$%04x.%01x ",  "ror $%02x          ", "ror $%04x        ", "push y           ",   "dbnz $%02x, $%04x  ", "ret              ",
  "bvs $%04x        ", "tcall 7          ", "clr1 $%02x.3       ", "bbc $%02x.3, $%04x ", "cmp a, $%02x+x     ", "cmp a, $%04x+x   ", "cmp a, $%04x+y   ", "cmp a, ($%02x)+y   ", "cmp $%02x, #$%02x    ", "cmp (x), (y)     ",     "addw ya, $%02x     ",   "ror $%02x+x        ", "ror a            ", "mov a, x         ",   "cmp y, $%02x       ", "reti             ",
  "setc             ", "tcall 8          ", "set1 $%02x.4       ", "bbs $%02x.4, $%04x ", "adc a, $%02x       ", "adc a, $%04x     ", "adc a, (x)       ", "adc a, ($%02x+x)   ", "adc a, #$%02x      ",   "adc $%02x, $%02x     ", "eor1 c, $%04x.%01x  ",  "dec $%02x          ", "dec $%04x        ", "mov y, #$%02x      ", "pop p            ",   "mov $%02x, #$%02x    ",
  "bcc $%04x        ", "tcall 9          ", "clr1 $%02x.4       ", "bbc $%02x.4, $%04x ", "adc a, $%02x+x     ", "adc a, $%04x+x   ", "adc a, $%04x+y   ", "adc a, ($%02x)+y   ", "adc $%02x, #$%02x    ", "adc (x), (y)     ",     "subw ya, $%02x     ",   "dec $%02x+x        ", "dec a            ", "mov x, sp        ",   "div ya, x        ",   "xcn a            ",
  "ei               ", "tcall 10         ", "set1 $%02x.5       ", "bbs $%02x.5, $%04x ", "sbc a, $%02x       ", "sbc a, $%04x     ", "sbc a, (x)       ", "sbc a, ($%02x+x)   ", "sbc a, #$%02x      ",   "sbc $%02x, $%02x     ", "mov1 c, $%04x.%01x  ",  "inc $%02x          ", "inc $%04x        ", "cmp y, #$%02x      ", "pop a            ",   "mov (x+), a      ",
  "bcs $%04x        ", "tcall 11         ", "clr1 $%02x.5       ", "bbc $%02x.5, $%04x ", "sbc a, $%02x+x     ", "sbc a, $%04x+x   ", "sbc a, $%04x+y   ", "sbc a, ($%02x)+y   ", "sbc $%02x, #$%02x    ", "sbc (x), (y)     ",     "movw ya, $%02x     ",   "inc $%02x+x        ", "inc a            ", "mov sp, x        ",   "das a            ",   "mov a, (x+)      ",
  "di               ", "tcall 12         ", "set1 $%02x.6       ", "bbs $%02x.6, $%04x ", "mov $%02x, a       ", "mov $%04x, a     ", "mov (x), a       ", "mov ($%02x+x), a   ", "cmp x, #$%02x      ",   "mov $%04x, x     ",     "mov1 $%04x.%01x, c  ",  "mov $%02x, y       ", "mov $%04x, y     ", "mov x, #$%02x      ", "pop x            ",   "mul ya           ",
  "bne $%04x        ", "tcall 13         ", "clr1 $%02x.6       ", "bbc $%02x.6, $%04x ", "mov $%02x+x, a     ", "mov $%04x+x, a   ", "mov $%04x+y, a   ", "mov ($%02x)+y, a   ", "mov $%02x, x       ",   "mov $%02x+y, x     ",   "movw $%02x, ya     ",   "mov $%02x+x, y     ", "dec y            ", "mov a, y         ",   "cbne $%02x+x, $%04x", "daa a            ",
  "clrv             ", "tcall 14         ", "set1 $%02x.7       ", "bbs $%02x.7, $%04x ", "mov a, $%02x       ", "mov a, $%04x     ", "mov a, (x)       ", "mov a, ($%02x+x)   ", "mov a, #$%02x      ",   "mov x, $%04x     ",     "not1 $%04x.%01x     ",  "mov y, $%02x       ", "mov y, $%04x     ", "notc             ",   "pop y            ",   "sleep            ",
  "beq $%04x        ", "tcall 15         ", "clr1 $%02x.7       ", "bbc $%02x.7, $%04x ", "mov a, $%02x+x     ", "mov a, $%04x+x   ", "mov a, $%04x+y   ", "mov a, ($%02x)+y   ", "mov x, $%02x       ",   "mov x, $%02x+y     ",   "mov $%02x, $%02x     ", "mov y, $%02x+x     ", "inc y            ", "mov y, a         ",   "dbnz y, $%04x    ",   "stop             "
};

// address types for each opcode, for spc
static const int opcodeTypeSpc[256] = {
  0, 0, 1, 5, 1, 2, 0, 1, 1, 4, 6, 1, 2, 0, 2, 0,
  3, 0, 1, 5, 1, 2, 2, 1, 4, 0, 1, 1, 0, 0, 2, 2,
  0, 0, 1, 5, 1, 2, 0, 1, 1, 4, 6, 1, 2, 0, 5, 3,
  3, 0, 1, 5, 1, 2, 2, 1, 4, 0, 1, 1, 0, 0, 1, 2,
  0, 0, 1, 5, 1, 2, 0, 1, 1, 4, 6, 1, 2, 0, 2, 1,
  3, 0, 1, 5, 1, 2, 2, 1, 4, 0, 1, 1, 0, 0, 2, 2,
  0, 0, 1, 5, 1, 2, 0, 1, 1, 4, 6, 1, 2, 0, 5, 0,
  3, 0, 1, 5, 1, 2, 2, 1, 4, 0, 1, 1, 0, 0, 1, 0,
  0, 0, 1, 5, 1, 2, 0, 1, 1, 4, 6, 1, 2, 1, 0, 4,
  3, 0, 1, 5, 1, 2, 2, 1, 4, 0, 1, 1, 0, 0, 0, 0,
  0, 0, 1, 5, 1, 2, 0, 1, 1, 4, 6, 1, 2, 1, 0, 0,
  3, 0, 1, 5, 1, 2, 2, 1, 4, 0, 1, 1, 0, 0, 0, 0,
  0, 0, 1, 5, 1, 2, 0, 1, 1, 2, 6, 1, 2, 1, 0, 0,
  3, 0, 1, 5, 1, 2, 2, 1, 1, 1, 1, 1, 0, 0, 5, 0,
  0, 0, 1, 5, 1, 2, 0, 1, 1, 2, 6, 1, 2, 0, 0, 0,
  3, 0, 1, 5, 1, 2, 2, 1, 1, 1, 4, 1, 0, 0, 3, 0
};

static void getDisassemblyCpu(LakeSnes::Snes* snes, char* line);
static void getDisassemblySpc(LakeSnes::Snes* snes, char* line);

void getProcessorStateCpu(LakeSnes::Snes* snes, char* line) {
  // 0        1         2         3         4         5         6         7         8
  // 12345678901234567890123456789012345678901234567890123456789012345678901234567890
  // CPU 12:3456 1234567890123 A:1234 X:1234 Y:1234 SP:1234 DP:1234 DP:12 e nvmxdizc
  char disLine[14] = "             ";
  getDisassemblyCpu(snes, disLine);
  sprintf(
    line, "CPU %02x:%04x %s A:%04x X:%04x Y:%04x SP:%04x DP:%04x DB:%02x %c %c%c%c%c%c%c%c%c",
    snes->mycpu.k, snes->mycpu.pc, disLine, snes->mycpu.a, snes->mycpu.x, snes->mycpu.y,
    snes->mycpu.sp, snes->mycpu.dp, snes->mycpu.db, snes->mycpu.e ? 'E' : 'e',
    snes->mycpu.n ? 'N' : 'n', snes->mycpu.v ? 'V' : 'v', snes->mycpu._mf ? 'M' : 'm', snes->mycpu._xf ? 'X' : 'x',
    snes->mycpu.d ? 'D' : 'd', snes->mycpu.i ? 'I' : 'i', snes->mycpu.z ? 'Z' : 'z', snes->mycpu.c ? 'C' : 'c'
  );
}

void getProcessorStateSpc(LakeSnes::Snes* snes, char* line) {
  // 0        1         2         3         4         5         6         7         8
  // 12345678901234567890123456789012345678901234567890123456789012345678901234567890
  // SPC 3456 12345678901234567 A:12 X:12 Y:12 SP:12 nvpbhizc
  char disLine[18] = "                 ";
  getDisassemblySpc(snes, disLine);
  sprintf(
    line, "SPC %04x %s A:%02x X:%02x Y:%02x SP:%02x %c%c%c%c%c%c%c%c",
    snes->myapu.myspc.pc, disLine, snes->myapu.myspc.a, snes->myapu.myspc.x, snes->myapu.myspc.y, snes->myapu.myspc.sp,
    snes->myapu.myspc.n ? 'N' : 'n', snes->myapu.myspc.v ? 'V' : 'v', snes->myapu.myspc.p ? 'P' : 'p', snes->myapu.myspc.b ? 'B' : 'b',
    snes->myapu.myspc.h ? 'H' : 'h', snes->myapu.myspc.i ? 'I' : 'i', snes->myapu.myspc.z ? 'Z' : 'z', snes->myapu.myspc.c ? 'C' : 'c'
  );
}

static void getDisassemblyCpu(LakeSnes::Snes* snes, char* line) {
  uint32_t adr = snes->mycpu.pc | (snes->mycpu.k << 16);
  
  //stopped and waiting have been removed due to relative uselessnes.
  //could be readded but should be done with several flags in one value
  #if 0
  if(snes->mycpu.stopped) {
    sprintf(line, "%s", "<stopped>    ");
    return;
  }
  if(snes->mycpu.waiting) {
    sprintf(line, "%s", "<waiting>    ");
    return;
  }
  #endif

  if(snes->mycpu.intWanted) {
    sprintf(line, "%s", "<interrupt>  ");
    return;
  }
  // read 4 bytes
  uint8_t opcode = snes->snes_peekByte(adr);
  uint8_t byte = snes->snes_peekByte((adr + 1) & 0xffffff);
  uint8_t byte2 = snes->snes_peekByte((adr + 2) & 0xffffff);
  uint16_t word = (byte2 << 8) | byte;
  uint32_t longv = (snes->snes_peekByte((adr + 3) & 0xffffff) << 16) | word;
  uint16_t rel = snes->mycpu.pc + 2 + (int8_t) byte;
  uint16_t rell = snes->mycpu.pc + 3 + (int16_t) word;
  // switch on type
  switch(opcodeType[opcode]) {
    case 0: sprintf(line, "%s", opcodeNames[opcode]); break;
    case 1: sprintf(line, opcodeNames[opcode], byte); break;
    case 2: sprintf(line, opcodeNames[opcode], word); break;
    case 3: sprintf(line, opcodeNames[opcode], longv); break;
    case 4: {
      char num[5] = "    ";
      if(snes->mycpu._mf) {
        sprintf(num, "%02x  ", byte);
      } else {
        sprintf(num, "%04x", word);
      }
      sprintf(line, opcodeNames[opcode], num);
      break;
    }
    case 5: {
      char num[5] = "    ";
      if(snes->mycpu._xf) {
        sprintf(num, "%02x  ", byte);
      } else {
        sprintf(num, "%04x", word);
      }
      sprintf(line, opcodeNames[opcode], num);
      break;
    }
    case 6: sprintf(line, opcodeNames[opcode], rel); break;
    case 7: sprintf(line, opcodeNames[opcode], rell); break;
    case 8: sprintf(line, opcodeNames[opcode], byte2, byte); break;
  }
}

void getDisassemblySpc(LakeSnes::Snes* snes, char* line) {
  uint16_t adr = snes->myapu.myspc.pc;
  if(snes->myapu.myspc.stopped) {
    sprintf(line, "%s", "<stopped>        ");
    return;
  }
  // read 3 bytes
  // TODO: this can have side effects, implement and use peaking
  uint8_t opcode = snes->myapu.apu_read(adr);
  uint8_t byte = snes->myapu.apu_read((adr + 1) & 0xffff);
  uint8_t byte2 = snes->myapu.apu_read((adr + 2) & 0xffff);
  uint16_t word = (byte2 << 8) | byte;
  uint16_t rel = snes->myapu.myspc.pc + 2 + (int8_t) byte;
  uint16_t rel2 = snes->myapu.myspc.pc + 2 + (int8_t) byte2;
  uint16_t wordb = word & 0x1fff;
  uint8_t bit = word >> 13;
  // switch on type
  switch(opcodeTypeSpc[opcode]) {
    case 0: sprintf(line, "%s", opcodeNamesSpc[opcode]); break;
    case 1: sprintf(line, opcodeNamesSpc[opcode], byte); break;
    case 2: sprintf(line, opcodeNamesSpc[opcode], word); break;
    case 3: sprintf(line, opcodeNamesSpc[opcode], rel); break;
    case 4: sprintf(line, opcodeNamesSpc[opcode], byte2, byte); break;
    case 5: sprintf(line, opcodeNamesSpc[opcode], byte, rel2); break;
    case 6: sprintf(line, opcodeNamesSpc[opcode], wordb, bit); break;
  }
}
