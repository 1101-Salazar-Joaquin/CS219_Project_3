#ifndef CPU_H
#define CPU_H
#include <cstdint>

#define MAX_PROGRAM_LINES 500
#define MAX_OPERANDS 6
#define MAX_LABELS 500
#define LINE_LEN 256
#define TOK_LEN 48

typedef struct Instruction {
    char text[LINE_LEN];
    char base[8];
    char cond[6];
    char operands[MAX_OPERANDS][TOK_LEN];
    int operandCount;
    int setFlags;
    char labelHere[32];
    char labelTarget[32];
} Instruction;

struct CPU_State {
    
    uint32_t regs[12];
    static const uint32_t MEM_BASE;     
    static const int MEM_SLOTS = 5;    
    uint32_t mem[MEM_SLOTS];
    int N, Z, C, V;

    Instruction program[MAX_PROGRAM_LINES];
    int programSize;

    char labelNames[MAX_LABELS][32];
    int labelIndices[MAX_LABELS];
    int labelCount;
};

void CPU_init(CPU_State *cpu);
void CPU_loadProgram(CPU_State *cpu, const char lines[][LINE_LEN], int lineCount);
void CPU_run(CPU_State *cpu);

#endif