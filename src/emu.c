#include "../include/emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEM_SIZE (16 * 1024 * 1024)
#define STACK_SIZE (1024 * 1024)
#define CODE_OFFSET 0x400000

static int read_mem8(EmuContext *ctx, uint64_t addr, uint8_t *out) {
    if (addr >= MEM_SIZE) { fprintf(stderr, "[EMU] Read beyond memory: 0x%llx\n", (unsigned long long)addr); return -1; }
    *out = ctx->memory[addr];
    return 0;
}

static int read_mem32(EmuContext *ctx, uint64_t addr, uint32_t *out) {
    if (addr + 4 > MEM_SIZE) { fprintf(stderr, "[EMU] Read32 beyond memory: 0x%llx\n", (unsigned long long)addr); return -1; }
    memcpy(out, &ctx->memory[addr], 4);
    return 0;
}

static int read_mem64(EmuContext *ctx, uint64_t addr, uint64_t *out) {
    if (addr + 8 > MEM_SIZE) { fprintf(stderr, "[EMU] Read64 beyond memory: 0x%llx\n", (unsigned long long)addr); return -1; }
    memcpy(out, &ctx->memory[addr], 8);
    return 0;
}

static void write_mem64(EmuContext *ctx, uint64_t addr, uint64_t val) {
    if (addr + 8 <= MEM_SIZE) memcpy(&ctx->memory[addr], &val, 8);
}

static void write_mem32(EmuContext *ctx, uint64_t addr, uint32_t val) {
    if (addr + 4 <= MEM_SIZE) memcpy(&ctx->memory[addr], &val, 4);
}

static void push64(EmuContext *ctx, uint64_t val) {
    ctx->rsp -= 8;
    write_mem64(ctx, ctx->rsp, val);
}

static uint64_t pop64(EmuContext *ctx) {
    uint64_t val;
    read_mem64(ctx, ctx->rsp, &val);
    ctx->rsp += 8;
    return val;
}

static uint64_t get_reg64(EmuContext *ctx, int reg) {
    switch (reg) {
        case 0: return ctx->rax;
        case 1: return ctx->rcx;
        case 2: return ctx->rdx;
        case 3: return ctx->rbx;
        case 4: return ctx->rsp;
        case 5: return ctx->rbp;
        case 6: return ctx->rsi;
        case 7: return ctx->rdi;
        case 8: return ctx->r8;
        case 9: return ctx->r9;
        case 10: return ctx->r10;
        case 11: return ctx->r11;
        case 12: return ctx->r12;
        case 13: return ctx->r13;
        case 14: return ctx->r14;
        case 15: return ctx->r15;
        default: return 0;
    }
}

static void set_reg64(EmuContext *ctx, int reg, uint64_t val) {
    switch (reg) {
        case 0: ctx->rax = val; break;
        case 1: ctx->rcx = val; break;
        case 2: ctx->rdx = val; break;
        case 3: ctx->rbx = val; break;
        case 4: ctx->rsp = val; break;
        case 5: ctx->rbp = val; break;
        case 6: ctx->rsi = val; break;
        case 7: ctx->rdi = val; break;
        case 8: ctx->r8 = val; break;
        case 9: ctx->r9 = val; break;
        case 10: ctx->r10 = val; break;
        case 11: ctx->r11 = val; break;
        case 12: ctx->r12 = val; break;
        case 13: ctx->r13 = val; break;
        case 14: ctx->r14 = val; break;
        case 15: ctx->r15 = val; break;
    }
}

static uint64_t get_rm64(EmuContext *ctx, uint8_t modrm, int *len) {
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;
    uint64_t addr = 0;

    switch (rm) {
        case 0: addr = ctx->rax + ctx->rcx; break;
        case 1: addr = ctx->rax + ctx->rdx; break;
        case 2: addr = ctx->rbp + ctx->rsi; break;
        case 3: addr = ctx->rbp + ctx->rdi; break;
        case 4: addr = ctx->rsi; break;
        case 5:
            if (mod == 0) {
                int32_t disp;
                read_mem32(ctx, ctx->rip + 1, (uint32_t*)&disp);
                addr = (uint64_t)(int64_t)disp;
                *len = 5;
                return addr;
            }
            addr = ctx->rbp;
            break;
        case 6: addr = ctx->rdi; break;
        case 7: addr = ctx->rax; break;
    }

    if (mod == 1) {
        int8_t disp;
        read_mem8(ctx, ctx->rip + 1, (uint8_t*)&disp);
        addr += disp;
        *len = 2;
    } else if (mod == 2) {
        int32_t disp;
        read_mem32(ctx, ctx->rip + 1, (uint32_t*)&disp);
        addr += disp;
        *len = 5;
    } else {
        *len = 1;
    }

    return addr;
}

static int emu_exec(EmuContext *ctx) {
    if (ctx->rip >= MEM_SIZE) {
        fprintf(stderr, "[EMU] RIP beyond memory: 0x%llx\n", (unsigned long long)ctx->rip);
        return -1;
    }

    uint8_t op;
    if (read_mem8(ctx, ctx->rip, &op)) return -1;

    switch (op) {
        case 0x55:
            push64(ctx, ctx->rbp);
            ctx->rbp = ctx->rsp;
            ctx->rip++;
            break;

        case 0x5D:
            ctx->rbp = pop64(ctx);
            ctx->rip++;
            break;

        case 0xC3:
            ctx->rip = pop64(ctx);
            break;

        case 0xC9:
            ctx->rip++;
            break;

        case 0x50:
            push64(ctx, ctx->rax);
            ctx->rip++;
            break;

        case 0x58:
            ctx->rax = pop64(ctx);
            ctx->rip++;
            break;

        case 0xB8: {
            uint32_t imm;
            read_mem32(ctx, ctx->rip + 1, &imm);
            ctx->rax = imm;
            ctx->rip += 5;
            break;
        }

        case 0xB9: {
            uint32_t imm;
            read_mem32(ctx, ctx->rip + 1, &imm);
            ctx->rcx = imm;
            ctx->rip += 5;
            break;
        }

        case 0xBA: {
            uint32_t imm;
            read_mem32(ctx, ctx->rip + 1, &imm);
            ctx->rdx = imm;
            ctx->rip += 5;
            break;
        }

        case 0xBB: {
            uint32_t imm;
            read_mem32(ctx, ctx->rip + 1, &imm);
            ctx->rbx = imm;
            ctx->rip += 5;
            break;
        }

        case 0x05: {
            uint32_t imm;
            read_mem32(ctx, ctx->rip + 1, &imm);
            ctx->rax = ctx->rax + imm;
            ctx->rip += 5;
            break;
        }

        case 0x2D: {
            uint32_t imm;
            read_mem32(ctx, ctx->rip + 1, &imm);
            ctx->rax = ctx->rax - imm;
            ctx->rip += 5;
            break;
        }

        case 0xE8: {
            int32_t disp;
            read_mem32(ctx, ctx->rip + 1, (uint32_t*)&disp);
            push64(ctx, ctx->rip + 5);
            ctx->rip = ctx->rip + 5 + disp;
            break;
        }

        case 0xE9: {
            int32_t disp;
            read_mem32(ctx, ctx->rip + 1, (uint32_t*)&disp);
            ctx->rip = ctx->rip + 5 + disp;
            break;
        }

        case 0x90:
            ctx->rip++;
            break;

        case 0x48: {
            uint8_t op2;
            read_mem8(ctx, ctx->rip + 1, &op2);
            if (op2 == 0x89) {
                uint8_t modrm;
                read_mem8(ctx, ctx->rip + 2, &modrm);
                uint8_t mod = (modrm >> 6) & 3;
                uint8_t reg = (modrm >> 3) & 7;
                if (mod == 3) {
                    set_reg64(ctx, modrm & 7, get_reg64(ctx, reg));
                }
                ctx->rip += 3;
            } else if (op2 == 0x81) {
                uint8_t modrm;
                read_mem8(ctx, ctx->rip + 2, &modrm);
                uint8_t reg = (modrm >> 3) & 7;
                if (reg == 5) {
                    int32_t imm;
                    read_mem32(ctx, ctx->rip + 3, &imm);
                    ctx->rsp = ctx->rsp - imm;
                    ctx->rip += 7;
                } else if (reg == 0) {
                    int32_t imm;
                    read_mem32(ctx, ctx->rip + 3, &imm);
                    ctx->rsp = ctx->rsp + imm;
                    ctx->rip += 7;
                } else {
                    ctx->rip += 7;
                }
            } else if (op2 == 0x8D) {
                uint8_t modrm;
                read_mem8(ctx, ctx->rip + 2, &modrm);
                ctx->rip += 3;
            } else {
                ctx->rip += 2;
            }
            break;
        }

        case 0x89: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t reg = (modrm >> 3) & 7;
            if (mod == 3) {
                set_reg64(ctx, modrm & 7, get_reg64(ctx, reg));
            }
            ctx->rip += 2;
            break;
        }

        case 0x8B: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t reg = (modrm >> 3) & 7;
            if (mod == 3) {
                set_reg64(ctx, reg, get_reg64(ctx, modrm & 7));
            } else {
                int len;
                uint64_t addr = get_rm64(ctx, modrm, &len);
                uint64_t val;
                read_mem64(ctx, addr, &val);
                set_reg64(ctx, reg, val);
                ctx->rip += 1 + len;
            }
            break;
        }

        case 0x01: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t reg = (modrm >> 3) & 7;
            if (mod == 3) {
                uint64_t *dst = (uint64_t*)((uint64_t*)&ctx->rax)[reg];
                uint64_t *src = (uint64_t*)((uint64_t*)&ctx->rax)[modrm & 7];
                *dst = *dst + *src;
            }
            ctx->rip += 2;
            break;
        }

        case 0x29: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t reg = (modrm >> 3) & 7;
            if (mod == 3) {
                uint64_t *dst = (uint64_t*)((uint64_t*)&ctx->rax)[reg];
                uint64_t *src = (uint64_t*)((uint64_t*)&ctx->rax)[modrm & 7];
                *dst = *dst - *src;
            }
            ctx->rip += 2;
            break;
        }

        case 0x31: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t reg = (modrm >> 3) & 7;
            if (mod == 3) {
                uint64_t *dst = (uint64_t*)((uint64_t*)&ctx->rax)[reg];
                uint64_t *src = (uint64_t*)((uint64_t*)&ctx->rax)[modrm & 7];
                *dst = *dst ^ *src;
            }
            ctx->rip += 2;
            break;
        }

        case 0x85: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            ctx->rip += 2;
            break;
        }

        case 0x0F: {
            uint8_t op2;
            read_mem8(ctx, ctx->rip + 1, &op2);
            if (op2 == 0x84) {
                int32_t disp;
                read_mem32(ctx, ctx->rip + 2, &disp);
                if ((ctx->eflags & 0x40) != 0)
                    ctx->rip = ctx->rip + 6 + disp;
                else
                    ctx->rip += 6;
            } else if (op2 == 0x85) {
                int32_t disp;
                read_mem32(ctx, ctx->rip + 2, &disp);
                if ((ctx->eflags & 0x40) == 0)
                    ctx->rip = ctx->rip + 6 + disp;
                else
                    ctx->rip += 6;
            } else if (op2 == 0x94 || op2 == 0x95 || op2 == 0x9C || op2 == 0x9D || op2 == 0x9E || op2 == 0x9F) {
                ctx->rip += 3;
            } else if (op2 == 0xAF) {
                uint8_t modrm;
                read_mem8(ctx, ctx->rip + 2, &modrm);
                uint8_t mod = (modrm >> 6) & 3;
                uint8_t reg = (modrm >> 3) & 7;
                if (mod == 3) {
                    uint64_t *dst = (uint64_t*)((uint64_t*)&ctx->rax)[reg];
                    uint64_t *src = (uint64_t*)((uint64_t*)&ctx->rax)[modrm & 7];
                    *dst = *dst * *src;
                }
                ctx->rip += 3;
            } else if (op2 == 0xB6) {
                ctx->rip += 3;
            } else {
                ctx->rip += 2;
            }
            break;
        }

        case 0x81: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t reg = (modrm >> 3) & 7;
            if (reg == 5) {
                int32_t imm;
                read_mem32(ctx, ctx->rip + 2, &imm);
                ctx->rsp = ctx->rsp - imm;
                ctx->rip += 6;
            } else if (reg == 0) {
                int32_t imm;
                read_mem32(ctx, ctx->rip + 2, &imm);
                ctx->rsp = ctx->rsp + imm;
                ctx->rip += 6;
            } else {
                ctx->rip += 6;
            }
            break;
        }

        case 0x83: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t reg = (modrm >> 3) & 7;
            if (reg == 4) {
                int8_t imm;
                read_mem8(ctx, ctx->rip + 2, &imm);
                ctx->rsp = ctx->rsp + imm;
                ctx->rip += 3;
            } else {
                ctx->rip += 3;
            }
            break;
        }

        case 0xF7: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t reg = (modrm >> 3) & 7;
            if (reg == 7) {
                uint64_t divisor = ctx->rbx;
                if (divisor == 0) {
                    ctx->exit_code = -1;
                    ctx->running = 0;
                    return 0;
                }
                int64_t dividend = ((int64_t)ctx->rdx << 32) | (ctx->rax & 0xFFFFFFFF);
                int64_t result = dividend / (int64_t)divisor;
                int64_t remainder = dividend % (int64_t)divisor;
                ctx->rax = result & 0xFFFFFFFF;
                ctx->rdx = remainder & 0xFFFFFFFF;
            } else if (reg == 3) {
                ctx->rax = -(int64_t)ctx->rax;
            }
            ctx->rip += 2;
            break;
        }

        case 0xFF: {
            uint8_t modrm;
            read_mem8(ctx, ctx->rip + 1, &modrm);
            uint8_t reg = (modrm >> 3) & 7;
            if (reg == 6) {
                uint8_t mod = (modrm >> 6) & 3;
                if (mod == 0) {
                    uint64_t target;
                    read_mem64(ctx, ctx->rip + 2, &target);
                    push64(ctx, ctx->rip + 10);
                    ctx->rip = target;
                } else if (mod == 3) {
                    push64(ctx, ctx->rip + 2);
                    ctx->rip = get_reg64(ctx, modrm & 7);
                }
            } else if (reg == 2) {
                uint8_t mod = (modrm >> 6) & 3;
                if (mod == 3) {
                    push64(ctx, get_reg64(ctx, modrm & 7));
                } else {
                    int len;
                    uint64_t addr = get_rm64(ctx, modrm, &len);
                    uint64_t val;
                    read_mem64(ctx, addr, &val);
                    push64(ctx, val);
                    ctx->rip += 1 + len;
                }
            } else if (reg == 0) {
                uint8_t mod = (modrm >> 6) & 3;
                if (mod == 3) {
                    uint64_t *p = (uint64_t*)((uint64_t*)&ctx->rax)[modrm & 7];
                    (*p)++;
                }
                ctx->rip += 2;
            } else if (reg == 1) {
                uint8_t mod = (modrm >> 6) & 3;
                if (mod == 3) {
                    uint64_t *p = (uint64_t*)((uint64_t*)&ctx->rax)[modrm & 7];
                    (*p)--;
                }
                ctx->rip += 2;
            }
            break;
        }

        case 0x99:
            ctx->rdx = (int64_t)ctx->rax >> 32;
            ctx->rip++;
            break;

        default:
            fprintf(stderr, "[EMU] Unknown opcode: 0x%02X at RIP=0x%llx\n", op, (unsigned long long)ctx->rip);
            ctx->rip++;
            break;
    }

    return 0;
}

int emu_init(EmuContext *ctx, uint8_t *code, uint64_t code_size, uint64_t entry_point) {
    ctx->mem_size = MEM_SIZE;
    ctx->memory = (uint8_t*)calloc(1, ctx->mem_size);
    if (!ctx->memory) return -1;

    ctx->stack_top = MEM_SIZE - STACK_SIZE;
    ctx->rsp = ctx->stack_top;
    ctx->rbp = ctx->stack_top;
    ctx->rip = entry_point;

    if (code_size > 0 && code_size < MEM_SIZE - CODE_OFFSET)
        memcpy(ctx->memory + CODE_OFFSET, code, code_size);

    ctx->running = 1;
    ctx->exit_code = 0;

    return 0;
}

void emu_free(EmuContext *ctx) {
    free(ctx->memory);
}

int emu_run(EmuContext *ctx) {
    int max_instructions = 10000000;
    int count = 0;

    while (ctx->running && count < max_instructions) {
        if (emu_exec(ctx) != 0) break;
        count++;
    }

    if (count >= max_instructions) {
        fprintf(stderr, "[EMU] Too many instructions, possible infinite loop\n");
        return -1;
    }

    return ctx->exit_code;
}