#ifndef EMU_H
#define EMU_H

#include <stdint.h>

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint32_t eflags;
    uint8_t *memory;
    uint64_t mem_size;
    uint64_t stack_top;
    int running;
    int exit_code;
} EmuContext;

int emu_init(EmuContext *ctx, uint8_t *code, uint64_t code_size, uint64_t entry_point, int argc, char** argv);
void emu_free(EmuContext *ctx);
int emu_run(EmuContext *ctx);

#endif