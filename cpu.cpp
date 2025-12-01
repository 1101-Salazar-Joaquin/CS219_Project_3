#include "cpu.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cctype>

using namespace std;

const uint32_t CPU_State::MEM_BASE = 0x100u;
static void trim_inplace(char *s) {
    if (!s) return;
    int i = 0;
    while (s[i] && isspace((unsigned char)s[i])) ++i;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static void toUpper_inplace(char *s) {
    for (int i = 0; s[i]; ++i) s[i] = (char)toupper((unsigned char)s[i]);
}

static int startsWith_c(const char *s, const char *pref) {
    return strncmp(s, pref, strlen(pref)) == 0;
}

static int tokenize_keep_brackets(const char *line, char tokens[][TOK_LEN], int maxTokens) {
    int t = 0;
    int p = 0;
    char cur[TOK_LEN]; cur[0] = '\0';
    int ci = 0;
    int inBracket = 0;
    while (line[p] && t < maxTokens) {
        char ch = line[p++];
        if (ch == '[') { inBracket = 1; if (ci < TOK_LEN-1) cur[ci++] = ch; }
        else if (ch == ']') { inBracket = 0; if (ci < TOK_LEN-1) cur[ci++] = ch; }
        else if (!inBracket && (ch == ',' || isspace((unsigned char)ch))) {
            if (ci > 0) {
                cur[ci] = '\0';
                trim_inplace(cur);
                if (cur[0]) { strncpy(tokens[t++], cur, TOK_LEN-1); tokens[t-1][TOK_LEN-1]=0; }
                ci = 0;
            }
        } else {
            if (ci < TOK_LEN-1) cur[ci++] = ch;
        }
    }
    if (ci > 0 && t < maxTokens) {
        cur[ci] = '\0';
        trim_inplace(cur);
        if (cur[0]) { strncpy(tokens[t++], cur, TOK_LEN-1); tokens[t-1][TOK_LEN-1]=0; }
    }
    return t;
}

static int isRegisterIdx(const char *tok) {
    if (!tok || tok[0] == '\0') return -1;
    if (tok[0] != 'R' && tok[0] != 'r') return -1;
    char *end; long v = strtol(tok + 1, &end, 10);
    if (end == tok + 1) return -1;
    if (v < 0 || v > 11) return -1;
    return (int)v;
}

static int isImmediateTok(const char *tok) {
    return (tok && tok[0] == '#');
}

static uint32_t parseImmediateTok(const char *tok) {
    if (!tok || tok[0] != '#') return 0u;
    const char *n = tok + 1;
    if (n[0] == '0' && (n[1] == 'x' || n[1] == 'X')) {
        unsigned int v = 0; sscanf(n, "%x", &v); return (uint32_t)v;
    } else {
        unsigned long v = strtoul(n, NULL, 10); return (uint32_t)(v & 0xFFFFFFFFu);
    }
}

static int isMemoryOperandC(const char *tok) {
    int L = (int)strlen(tok);
    return (L >= 3 && tok[0] == '[' && tok[L-1] == ']');
}

static int addr_to_index(uint32_t addr) {
    if (addr < CPU_State::MEM_BASE) return -1;
    if ((addr - CPU_State::MEM_BASE) % 4u != 0u) return -1;
    uint32_t slot = (addr - CPU_State::MEM_BASE) / 4u;
    if (slot >= (uint32_t)CPU_State::MEM_SLOTS) return -1;
    return (int)slot;
}

static void print_state(const CPU_State *cpu, const char *instrText) {
    printf("%s\n", instrText ? instrText : "");
    printf("Register array:\n");
    printf("R0 =0x%X R1=0x%X R2=0x%X R3=0x%X R4=0x%X R5=0x%X\n",
           cpu->regs[0], cpu->regs[1], cpu->regs[2], cpu->regs[3], cpu->regs[4], cpu->regs[5]);
    printf("R6=0x%X R7=0x%X R8=0x%X R9=0x%X R10=0x%X R11=0x%X\n",
           cpu->regs[6], cpu->regs[7], cpu->regs[8], cpu->regs[9], cpu->regs[10], cpu->regs[11]);
    printf("NZCV: %d%d%d%d\n", cpu->N, cpu->Z, cpu->C, cpu->V);
    for (int i = 0; i < CPU_State::MEM_SLOTS; ++i) {
        if (cpu->mem[i] == 0u) printf("___");
        else printf("0x%X", cpu->mem[i]);
        if (i + 1 < CPU_State::MEM_SLOTS) printf(",");
    }
    printf("\n");
}

void CPU_init(CPU_State *cpu) {
    for (int i = 0; i < 12; ++i) cpu->regs[i] = 0u;
    for (int i = 0; i < CPU_State::MEM_SLOTS; ++i) cpu->mem[i] = 0u;
    cpu->N = cpu->Z = cpu->C = cpu->V = 0;
    cpu->programSize = 0;
    cpu->labelCount = 0;
}

void CPU_loadProgram(CPU_State *cpu, const char lines[][LINE_LEN], int lineCount) {
    cpu->programSize = 0;
    cpu->labelCount = 0;
    const char *bases[] = {"MOV","MVN","ADD","SUB","AND","ORR","EOR","LSL","LSR","LDR","STR","CMP","BEQ"};
    int basesCount = sizeof(bases)/sizeof(bases[0]);

    for (int li = 0; li < lineCount && li < MAX_PROGRAM_LINES; ++li) {
        char raw[LINE_LEN];
        strncpy(raw, lines[li], LINE_LEN-1); raw[LINE_LEN-1] = '\0';
        trim_inplace(raw);
        if (raw[0] == '\0') continue;
        if (raw[0] == ';' || raw[0] == '#') continue;

        char tokens[12][TOK_LEN];
        int tokCount = tokenize_keep_brackets(raw, tokens, 12);
        if (tokCount == 0) continue;

        Instruction ins;
        memset(&ins, 0, sizeof(Instruction));
        strncpy(ins.text, raw, LINE_LEN-1);

        int tokenIndex = 0;

        char firstUpper[TOK_LEN]; strncpy(firstUpper, tokens[0], TOK_LEN-1); firstUpper[TOK_LEN-1]=0;
        toUpper_inplace(firstUpper);
        int firstIsOpcode = 0;
        for (int b = 0; b < basesCount; ++b) {
            if (startsWith_c(firstUpper, bases[b])) { firstIsOpcode = 1; break; }
        }
        if (!firstIsOpcode) {
            char lbl[32]; strncpy(lbl, tokens[0], 31); lbl[31]=0;
            int L = (int)strlen(lbl);
            if (L > 0 && lbl[L-1] == ':') lbl[L-1] = '\0';
            strncpy(ins.labelHere, lbl, sizeof(ins.labelHere)-1);
            if (cpu->labelCount < MAX_LABELS) {
                strncpy(cpu->labelNames[cpu->labelCount], lbl, 31);
                cpu->labelNames[cpu->labelCount][31]=0;
                cpu->labelIndices[cpu->labelCount] = cpu->programSize;
                cpu->labelCount++;
            }
            tokenIndex = 1;
            if (tokenIndex >= tokCount) {
                cpu->program[cpu->programSize++] = ins;
                continue;
            }
        }
        char opcode[TOK_LEN]; strncpy(opcode, tokens[tokenIndex++], TOK_LEN-1); opcode[TOK_LEN-1]=0;
        char up[TOK_LEN]; strncpy(up, opcode, TOK_LEN-1); up[TOK_LEN-1]=0; toUpper_inplace(up);
        char matchedBase[8] = ""; char suffix[TOK_LEN] = ""; 
        for (int b = 0; b < basesCount; ++b) {
            int len = (int)strlen(bases[b]);
            if (strncmp(up, bases[b], len) == 0) {
                strncpy(matchedBase, bases[b], sizeof(matchedBase)-1);
                // suffix is rest of opcode token
                strncpy(suffix, up + len, TOK_LEN-1);
                break;
            }
        }

        if (matchedBase[0] == '\0') {
            // unknown base; set base to opcode
            strncpy(ins.base, up, sizeof(ins.base)-1);
            // remaining tokens as operands
            for (int k = tokenIndex; k < tokCount && ins.operandCount < MAX_OPERANDS; ++k)
                strncpy(ins.operands[ins.operandCount++], tokens[k], TOK_LEN-1);
            cpu->program[cpu->programSize++] = ins;
            continue;
        }

        strncpy(ins.base, matchedBase, sizeof(ins.base)-1);

        // handle S suffix
        if (suffix[0] == 'S') {
            ins.setFlags = 1;
            // shift suffix left by 1
            memmove(suffix, suffix + 1, strlen(suffix));
        }
        if (suffix[0] != '\0') strncpy(ins.cond, suffix, sizeof(ins.cond)-1);

        // operands
        for (int k = tokenIndex; k < tokCount && ins.operandCount < MAX_OPERANDS; ++k) {
            strncpy(ins.operands[ins.operandCount++], tokens[k], TOK_LEN-1);
        }

        if (strcmp(ins.base, "BEQ") == 0 && ins.operandCount >= 1) {
            strncpy(ins.labelTarget, ins.operands[0], sizeof(ins.labelTarget)-1);
        }

        cpu->program[cpu->programSize++] = ins;
    }
}

/* condition evaluation */
static int conditionTrue(const CPU_State *cpu, const char *cond) {
    if (!cond || cond[0] == '\0') return 1;
    char c[TOK_LEN]; strncpy(c, cond, TOK_LEN-1); c[TOK_LEN-1]=0; toUpper_inplace(c);
    if (strcmp(c, "EQ") == 0) return cpu->Z;
    if (strcmp(c, "NE") == 0) return !cpu->Z;
    if (strcmp(c, "GT") == 0) return (!cpu->Z && (cpu->N == cpu->V));
    if (strcmp(c, "GE") == 0) return (cpu->N == cpu->V);
    if (strcmp(c, "LT") == 0) return (cpu->N != cpu->V);
    if (strcmp(c, "LE") == 0) return (cpu->Z || (cpu->N != cpu->V));
    return 0;
}

/* NZ / NZCV updates */
static void updateNZ_for(uint32_t *N, uint32_t *Z, uint32_t res) {
    *Z = (res == 0) ? 1 : 0;
    *N = ((res >> 31) & 1u) ? 1 : 0;
}

static void updateAddNZCV(CPU_State *cpu, uint32_t a, uint32_t b, uint32_t r) {
    unsigned long long wide = (unsigned long long)a + (unsigned long long)b;
    cpu->C = ((wide >> 32) & 1ull) ? 1 : 0;
    int32_t sa = (int32_t)a, sb = (int32_t)b, sr = (int32_t)r;
    cpu->V = ((sa > 0 && sb > 0 && sr < 0) || (sa < 0 && sb < 0 && sr > 0)) ? 1 : 0;
    cpu->Z = (r == 0) ? 1 : 0;
    cpu->N = ((r >> 31) & 1u) ? 1 : 0;
}

static void updateSubNZCV(CPU_State *cpu, uint32_t a, uint32_t b, uint32_t r) {
    cpu->C = (a >= b) ? 1 : 0;
    int32_t sa = (int32_t)a, sb = (int32_t)b, sr = (int32_t)r;
    cpu->V = ((sa < 0 && sb > 0 && sr > 0) || (sa > 0 && sb < 0 && sr < 0)) ? 1 : 0;
    cpu->Z = (r == 0) ? 1 : 0;
    cpu->N = ((r >> 31) & 1u) ? 1 : 0;
}

static uint32_t getOperandValue(const CPU_State *cpu, const char *op) {
    if (!op) return 0u;
    char t[TOK_LEN]; strncpy(t, op, TOK_LEN-1); t[TOK_LEN-1]=0; trim_inplace(t);
    int r = isRegisterIdx(t);
    if (r >= 0) return cpu->regs[r];
    if (isImmediateTok(t)) return parseImmediateTok(t);
    if (isMemoryOperandC(t)) {
        int L = (int)strlen(t);
        if (L >= 3) {
            char inner[TOK_LEN]; strncpy(inner, t+1, (size_t)L-2); inner[L-2]=0; trim_inplace(inner);
            int idx = isRegisterIdx(inner);
            if (idx >= 0) {
                uint32_t addr = cpu->regs[idx];
                int midx = addr_to_index(addr);
                if (midx >= 0) return cpu->mem[midx];
            }
        }
        return 0;
    }

    if (t[0] == '0' && t[1] == 'x') { unsigned int v=0; sscanf(t, "%x", &v); return (uint32_t)v; }
    if (t[0]) { unsigned long v = strtoul(t, NULL, 10); return (uint32_t)(v & 0xFFFFFFFFu); }
    return 0;
}

void CPU_run(CPU_State *cpu) {
    int pc = 0;
    while (pc < cpu->programSize) {
        Instruction *ins = &cpu->program[pc];
        int shouldExec = conditionTrue(cpu, ins->cond);

        if (strcmp(ins->base, "BEQ") == 0) {
            print_state(cpu, ins->text);
            if (shouldExec && ins->labelTarget[0] != '\0') {
                int found = -1;
                for (int i = 0; i < cpu->labelCount; ++i) {
                    if (strcmp(cpu->labelNames[i], ins->labelTarget) == 0) { found = cpu->labelIndices[i]; break; }
                }
                if (found >= 0) { pc = found; continue; }
            }
            pc++;
            continue;
        }

        if (!shouldExec) {
            print_state(cpu, ins->text);
            pc++;
            continue;
        }

        if (strcmp(ins->base, "MOV") == 0 || strcmp(ins->base, "MVN") == 0) {
            int rd = isRegisterIdx(ins->operands[0]);
            uint32_t val = getOperandValue(cpu, ins->operands[1]);
            if (strcmp(ins->base, "MVN") == 0) val = ~val;
            if (rd >= 0) cpu->regs[rd] = val;
            if (ins->setFlags) updateNZ_for((uint32_t*)&cpu->N, (uint32_t*)&cpu->Z, val);
            print_state(cpu, ins->text);
            pc++;
            continue;
        }

        if (strcmp(ins->base, "CMP") == 0) {
            uint32_t a = getOperandValue(cpu, ins->operands[0]);
            uint32_t b = getOperandValue(cpu, ins->operands[1]);
            uint32_t r = (uint32_t)(a - b);
            updateSubNZCV(cpu, a, b, r);
            print_state(cpu, ins->text);
            pc++;
            continue;
        }

        if (strcmp(ins->base, "ADD")==0 || strcmp(ins->base,"SUB")==0 ||
            strcmp(ins->base,"AND")==0 || strcmp(ins->base,"ORR")==0 || strcmp(ins->base,"EOR")==0) {

            int rd = isRegisterIdx(ins->operands[0]);
            uint32_t rn = getOperandValue(cpu, ins->operands[1]);
            uint32_t op2 = getOperandValue(cpu, ins->operands[2]);
            uint32_t res = 0u;
            if (strcmp(ins->base,"ADD")==0) {
                res = (uint32_t)(rn + op2);
                if (ins->setFlags) updateAddNZCV(cpu, rn, op2, res);
            } else if (strcmp(ins->base,"SUB")==0) {
                res = (uint32_t)(rn - op2);
                if (ins->setFlags) updateSubNZCV(cpu, rn, op2, res);
            } else if (strcmp(ins->base,"AND")==0) {
                res = rn & op2; if (ins->setFlags) updateNZ_for((uint32_t*)&cpu->N, (uint32_t*)&cpu->Z, res);
            } else if (strcmp(ins->base,"ORR")==0) {
                res = rn | op2; if (ins->setFlags) updateNZ_for((uint32_t*)&cpu->N, (uint32_t*)&cpu->Z, res);
            } else if (strcmp(ins->base,"EOR")==0) {
                res = rn ^ op2; if (ins->setFlags) updateNZ_for((uint32_t*)&cpu->N, (uint32_t*)&cpu->Z, res);
            }

            if (rd >= 0) cpu->regs[rd] = res;
            print_state(cpu, ins->text);
            pc++;
            continue;
        }

        if (strcmp(ins->base,"LSL")==0 || strcmp(ins->base,"LSR")==0) {
            int rd = isRegisterIdx(ins->operands[0]);
            uint32_t rn = getOperandValue(cpu, ins->operands[1]);
            uint32_t imm = 0;
            if (isImmediateTok(ins->operands[2])) imm = parseImmediateTok(ins->operands[2]);
            else imm = (uint32_t)strtoul(ins->operands[2], NULL, 10);
            uint32_t res = 0;
            if (strcmp(ins->base,"LSL")==0) {
                if (imm >= 32) res = 0; else res = (uint32_t)(rn << imm);
            } else {
                if (imm >= 32) res = 0; else res = (uint32_t)(rn >> imm);
            }
            if (rd >= 0) cpu->regs[rd] = res;
            if (ins->setFlags) updateNZ_for((uint32_t*)&cpu->N, (uint32_t*)&cpu->Z, res);
            print_state(cpu, ins->text);
            pc++;
            continue;
        }

        if (strcmp(ins->base,"LDR")==0) {
            int rd = isRegisterIdx(ins->operands[0]);
            char memop[TOK_LEN]; strncpy(memop, ins->operands[1], TOK_LEN-1); memop[TOK_LEN-1]=0;
            if (isMemoryOperandC(memop)) {
                int L = (int)strlen(memop);
                if (L >= 3) {
                    char inner[TOK_LEN]; strncpy(inner, memop+1, (size_t)L-2); inner[L-2]=0; trim_inplace(inner);
                    int rIdx = isRegisterIdx(inner);
                    if (rIdx >= 0) {
                        uint32_t addr = cpu->regs[rIdx];
                        int midx = addr_to_index(addr);
                        if (midx >= 0) {
                            if (rd >= 0) cpu->regs[rd] = cpu->mem[midx];
                            print_state(cpu, ins->text);
                            pc++; continue;
                        } else {
                            print_state(cpu, ins->text);
                            pc++; continue;
                        }
                    }
                }
            }
            print_state(cpu, ins->text);
            pc++; continue;
        }

        // STR
        if (strcmp(ins->base,"STR")==0) {
            int rs = isRegisterIdx(ins->operands[0]);
            char memop[TOK_LEN]; strncpy(memop, ins->operands[1], TOK_LEN-1); memop[TOK_LEN-1]=0;
            if (isMemoryOperandC(memop)) {
                int L = (int)strlen(memop);
                if (L >= 3) {
                    char inner[TOK_LEN]; strncpy(inner, memop+1, (size_t)L-2); inner[L-2]=0; trim_inplace(inner);
                    int rIdx = isRegisterIdx(inner);
                    if (rIdx >= 0 && rs >= 0) {
                        uint32_t addr = cpu->regs[rIdx];
                        int midx = addr_to_index(addr);
                        if (midx >= 0) {
                            cpu->mem[midx] = cpu->regs[rs];
                            print_state(cpu, ins->text);
                            pc++; continue;
                        } else {
                            print_state(cpu, ins->text);
                            pc++; continue;
                        }
                    }
                }
            }
            print_state(cpu, ins->text);
            pc++; continue;
        }
        print_state(cpu, ins->text);
        pc++;
    }
}