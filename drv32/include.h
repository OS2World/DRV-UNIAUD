/* INCLUDE.H - Generic include file for Watcom-based device drivers
 *  include this file after all other header files
 */

#ifndef INCLUDE_INCLUDED
#define INCLUDE_INCLUDED

#ifndef NULL
#define NULL 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define NUM_ELEMENTS(array) ( sizeof(array) / sizeof((array)[0]) )

// debugging

void nop(void);
#pragma aux nop = \
   "nop" \
   parm nomemory \
   modify nomemory exact [];

void int3(void);
#pragma aux int3 = \
   "int 3" \
   parm nomemory \
   modify nomemory exact [];

void mark(unsigned short);
#pragma aux mark = \
   parm [ax] \
   modify [ax bx cx dx si di es];

typedef struct {        // two 32-bit unsigned values
   unsigned long lo;
   unsigned long hi;
} RDTSC_COUNT;

// Note: the Watcom inline assembler does understand the
// rdtsc instruction, but only if the -5 command-line parameter is
// used.

void rdtsc(RDTSC_COUNT __far *);
#pragma aux rdtsc = \
   "db   15, 49" \
   "mov  es:[bx],eax" \
   "mov  es:[bx+4],edx" \
   parm nomemory [es bx] \
   modify exact [ax dx];

// flag set/query

void cli(void);
#pragma aux cli = \
   "cli" \
   parm nomemory \
   modify nomemory exact [];

void sti(void);
#pragma aux sti = \
   "sti" \
   parm nomemory \
   modify nomemory exact [];

void stc(void);
#pragma aux stc = \
   "stc" \
   parm nomemory \
   modify nomemory exact [];

void clc(void);
#pragma aux clc = \
   "clc" \
   parm nomemory \
   modify nomemory exact [];

int _AF(void);       // auxiliary carry flag
#pragma aux _AF = \
   "pushf" \
   "pop  ax" \
   "and  ax,0x10" \
   parm nomemory \
   modify nomemory exact [ax];

int _CF(void);       // carry flag
#pragma aux _CF = \
   "pushf" \
   "pop  ax" \
   "and  ax,1" \
   parm nomemory \
   modify nomemory exact [ax];

int _IF(void);       // interrupt flag
#pragma aux _IF = \
   "pushf" \
   "pop  ax" \
   "and  ax,0x200" \
   parm nomemory \
   modify nomemory exact [ax];

int _OF(void);       // overflow flag
#pragma aux _OF = \
   "pushf" \
   "pop  ax" \
   "and  ax,0x800" \
   parm nomemory \
   modify nomemory exact [ax];

int _PF(void);       // parity flag
#pragma aux _PF = \
   "pushf" \
   "pop  ax" \
   "and  ax,4" \
   parm nomemory \
   modify nomemory exact [ax];

int _SF(void);       // sign flag
#pragma aux _SF = \
   "pushf" \
   "pop  ax" \
   "and  ax,0x80" \
   parm nomemory \
   modify nomemory exact [ax];

int _ZF(void);       // zero flag
#pragma aux _ZF = \
   "pushf" \
   "pop  ax" \
   "and  ax,0x40" \
   parm nomemory \
   modify nomemory exact [ax];

void setCF(unsigned char);
#pragma aux setCF = \
   "sub  ah,ah" \
   "sub  ah,al" \
   parm nomemory [al] \
   modify nomemory exact [ah];

void pushf(void);
#pragma aux pushf = \
   "pushf" \
   parm nomemory \
   modify nomemory exact [];

void popf(void);
#pragma aux popf = \
   "popf" \
   parm nomemory \
   modify nomemory exact [];

// instruction execut ion control

void ret(void);
#pragma aux ret = \
   "ret" \
   parm nomemory \
   modify nomemory exact [];

void retf(void);
#pragma aux retf = \
   "retf" \
   parm nomemory \
   modify nomemory exact [];

// input/output

unsigned char inp(unsigned short);
#pragma aux inp = \
   "in   al,dx" \
   value [al] \
   parm nomemory [dx] \
   modify nomemory exact [al];

void outp(unsigned short, unsigned char);
#pragma aux outp = \
   "out  dx,al" \
   parm nomemory [dx] [al] \
   modify nomemory exact [];

// shifts and rotates

unsigned char rotl8(unsigned char value, unsigned char count);
#pragma aux rotl8 = \
   "rol  al,cl" \
   parm nomemory [al] [cl] \
   modify nomemory exact [al];

unsigned int rotl16(unsigned int value, unsigned char count);
#pragma aux rotl16 = \
   "rol  ax,cl" \
   parm nomemory [ax] [cl] \
   modify nomemory exact [ax];

unsigned char rotr8(unsigned char value, unsigned char count);
#pragma aux rotr8 = \
   "ror  al,cl" \
   parm nomemory [al] [cl] \
   modify nomemory exact [al];

unsigned int rotr16(unsigned int value, unsigned char count);
#pragma aux rotr16 = \
   "ror  ax,cl" \
   parm nomemory [ax] [cl] \
   modify nomemory exact [ax];

// register query/set

unsigned _DS(void);
#pragma aux _DS = \
   "mov  ax,ds" \
   parm nomemory \
   modify nomemory exact [ax];

unsigned _ES(void);
#pragma aux _ES = \
   "mov  ax,es" \
   parm nomemory \
   modify nomemory exact [ax];

unsigned _SS(void);
#pragma aux _SS = \
   "mov  ax,ss" \
   parm nomemory \
   modify nomemory exact [ax];

unsigned _CS(void);
#pragma aux _CS = \
   "mov  ax,cs" \
   parm nomemory \
   modify nomemory exact [ax];

unsigned _AX(void);
#pragma aux _AX = \
   value [ax] \
   parm nomemory \
   modify nomemory;

unsigned _BX(void);
#pragma aux _BX = \
   value [bx] \
   parm nomemory \
   modify nomemory;

unsigned _CX(void);
#pragma aux _CX = \
   value [cx] \
   parm nomemory \
   modify nomemory;

unsigned _DX(void);
#pragma aux _DX = \
   value [dx] \
   parm nomemory \
   modify nomemory;

unsigned _SI(void);
#pragma aux _SI = \
   value [si] \
   parm nomemory \
   modify nomemory;

unsigned _DI(void);
#pragma aux _DI = \
   value [di] \
   parm nomemory \
   modify nomemory;

unsigned _BP(void);
#pragma aux _BP = \
   value [bp] \
   parm nomemory \
   modify nomemory;

unsigned _SP(void);
#pragma aux _SP = \
   "mov  ax,sp" \
   parm nomemory \
   modify nomemory exact [ax];

unsigned long _EAX(void);
#pragma aux _EAX = \
   "mov  edx,eax" \
   "shr  edx,16" \
   value [dx ax] \
   parm nomemory \
   modify nomemory exact [ax dx];

unsigned long _EBX(void);
#pragma aux _EBX = \
   "mov  edx,ebx" \
   "shr  edx,16" \
   value [dx bx] \
   parm nomemory \
   modify nomemory exact [bx dx];

unsigned long _ECX(void);
#pragma aux _ECX = \
   "mov  edx,ecx" \
   "shr  edx,16" \
   value [dx cx] \
   parm nomemory \
   modify nomemory exact [cx dx];

unsigned long _EDX(void);
#pragma aux _EDX = \
   "mov  ax,dx" \
   "shr  edx,16" \
   value [dx ax] \
   parm nomemory \
   modify nomemory exact [ax dx];

unsigned long _ESP(void);
#pragma aux _ESP = \
   "mov  edx,esp" \
   "mov  ax,sp" \
   "shr  edx,16" \
   parm nomemory [dx ax] \
   modify nomemory exact [ax dx];

void setEAX(unsigned long);         // make sure you call zeroESP() first
#pragma aux setEAX = \
   "mov  eax,[esp]" \
   parm caller nomemory [] \
   modify nomemory exact [ax];

void setEBX(unsigned long);         // make sure you call zeroESP() first
#pragma aux setEBX = \
   "mov  ebx,[esp]" \
   parm caller nomemory [] \
   modify nomemory exact [bx];

void setECX(unsigned long);         // make sure you call zeroESP() first
#pragma aux setECX = \
   "mov  ecx,[esp]" \
   parm caller nomemory [] \
   modify nomemory exact [cx];

void setEDX(unsigned long);         // make sure you call zeroESP() first
#pragma aux setEDX = \
   "mov  edx,[esp]" \
   parm caller nomemory [] \
   modify nomemory exact [dx];

void setDS(unsigned short us);
#pragma aux setDS = \
   "mov  ds,ax" \
   parm nomemory [ax] \
   modify nomemory exact [];

void setES(unsigned short us);
#pragma aux setES = \
   "mov  es,ax" \
   parm nomemory [ax] \
   modify nomemory exact [];

void setSS(unsigned short us);
#pragma aux setSS = \
   "mov  ss,ax" \
   parm nomemory [ax] \
   modify nomemory exact [];

void setFS(unsigned short us);
#pragma aux setFS = \
   "mov  fs,ax" \
   parm nomemory [ax] \
   modify nomemory exact [];

void setGS(unsigned short us);
#pragma aux setGS = \
   "mov  gs,ax" \
   parm nomemory [ax] \
   modify nomemory exact [];

void setAX(unsigned short us);
#pragma aux setAX = \
   parm nomemory [ax] \
   modify nomemory exact [];

void setBX(unsigned short us);
#pragma aux setBX = \
   parm nomemory [bx] \
   modify nomemory exact [];

void setCX(unsigned short us);
#pragma aux setCX = \
   parm nomemory [cx] \
   modify nomemory exact [];

void setDX(unsigned short us);
#pragma aux setDX = \
   parm nomemory [dx] \
   modify nomemory exact [];

void setSI(unsigned short us);
#pragma aux setSI = \
   parm nomemory [si] \
   modify nomemory exact [];

void setDI(unsigned short us);
#pragma aux setDI = \
   parm nomemory [di] \
   modify nomemory exact [];

void setBP(unsigned short us);
#pragma aux setBP = \
   parm nomemory [bp] \
   modify nomemory exact [];

void setSP(unsigned short us);
#pragma aux setSP = \
   parm nomemory [sp] \
   modify nomemory exact [];

// specialized register setting

void zeroESP(void);           // zeroes the upper half of ESP
#pragma aux zeroESP = \
   "movzx   esp,sp" \
   parm nomemory \
   modify nomemory;

// push/pop

void pusha(void);
#pragma aux pusha = \
   "pusha" \
   parm nomemory \
   modify nomemory exact [];

void pushDS(void);
#pragma aux pushDS = \
   "push ds" \
   parm nomemory \
   modify nomemory exact [];

void pushES(void);
#pragma aux pushES = \
   "push es" \
   parm nomemory \
   modify nomemory exact [];

void pushSS(void);
#pragma aux pushSS = \
   "push ss" \
   parm nomemory \
   modify nomemory exact [];

void pushFS(void);
#pragma aux pushFS = \
   "push fs" \
   parm nomemory \
   modify nomemory exact [];

void pushGS(void);
#pragma aux pushGS = \
   "push gs" \
   parm nomemory \
   modify nomemory exact [];

void popa(void);
#pragma aux popa = \
   "popa" \
   parm nomemory \
   modify nomemory exact [];

void popDS(void);
#pragma aux popDS = \
   "pop  ds" \
   parm nomemory \
   modify nomemory exact [];

void popES(void);
#pragma aux popES = \
   "pop  es" \
   parm nomemory \
   modify nomemory exact [];

void popSS(void);
#pragma aux popSS = \
   "pop  ss" \
   parm nomemory \
   modify nomemory exact [];

void popFS(void);
#pragma aux popFS = \
   "pop  fs" \
   parm nomemory \
   modify nomemory exact [];

void popGS(void);
#pragma aux popGS = \
   "pop  gs" \
   parm nomemory \
   modify nomemory exact [];


#ifndef min
#define min(a,b) ( (a) < (b) ? (a) : (b) )
#endif

#ifndef max
#define max(a,b) ( (a) > (b) ? (a) : (b) )
#endif

/* Implement the new bool type.  These lines will need to be
   #ifdef'd when Watcom supports the bool type internally.
*/

#define bool int
#define false 0
#define true 1

#endif
