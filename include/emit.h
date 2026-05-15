#ifndef EMIT_H
#define EMIT_H

#include <stdint.h>

typedef enum {
    RELOC_JMP,      /* E9 rel32 */
    RELOC_CALL,     /* E8 rel32 */
    RELOC_JCC,      /* 0F 8X rel32 */
    RELOC_GOTPCREL  /* R_X86_64_GOTPCREL (for Linux) */
} RelocType;

typedef struct {
    char name[64];
    int offset;
    int defined;
} Label;

typedef struct {
    int offset;       /* позиция в буфере */
    int label_idx;    /* индекс метки */
    RelocType type;   /* тип reloc */
} Reloc;

typedef struct {
    int label_idx;
    int offset;
} DebugEntry;

typedef struct {
    uint8_t *code;
    int capacity;
    int pos;
    Label *labels;
    int label_count;
    int labels_capacity;
    Reloc *relocs;
    int reloc_count;
    int relocs_capacity;

    /* Отладочная карта (label -> offset) */
    DebugEntry *debug_map;
    int debug_count;
    int debug_capacity;
} Emitter;

/* Инициализация */
void emit_init(Emitter *e);
void emit_free(Emitter *e);

/* Управление метками */
int emit_label_def(Emitter *e, const char *name);
int emit_label_ref(Emitter *e, const char *name);

/* Базовые эмиты */
void emit_byte(Emitter *e, uint8_t b);
void emit_word(Emitter *e, uint16_t w);
void emit_dword(Emitter *e, uint32_t dw);
void emit_qword(Emitter *e, uint64_t qw);
void emit_bytes(Emitter *e, const uint8_t *data, int len);

/* === x86-64 инструкции === */

void emit_mov_eax_imm(Emitter *e, uint32_t imm);
void emit_mov_ebx_imm(Emitter *e, uint32_t imm);
void emit_mov_ebx_eax(Emitter *e);
void emit_mov_rcx_rax(Emitter *e);
void emit_mov_rdx_rax(Emitter *e);
void emit_mov_r8_rax(Emitter *e);
void emit_mov_r9_rax(Emitter *e);
void emit_mov_eax_rbp_disp(Emitter *e, int disp);
void emit_mov_rbp_disp_eax(Emitter *e, int disp);
void emit_mov_eax_rbp_disp32(Emitter *e, int disp);
void emit_mov_rbp_disp32_eax(Emitter *e, int disp);
void emit_xor_eax_eax(Emitter *e);
void emit_add_eax_ebx(Emitter *e);
void emit_sub_eax_ebx(Emitter *e);
void emit_imul_eax_ebx(Emitter *e);
void emit_cdq(Emitter *e);
void emit_idiv_ebx(Emitter *e);
void emit_neg_eax(Emitter *e);
void emit_cmp_eax_ebx(Emitter *e);
void emit_test_eax_eax(Emitter *e);
void emit_setz_al(Emitter *e);
void emit_setnz_al(Emitter *e);
void emit_setl_al(Emitter *e);
void emit_setg_al(Emitter *e);
void emit_setle_al(Emitter *e);
void emit_setge_al(Emitter *e);
void emit_movzx_eax_al(Emitter *e);
void emit_mov_eax_edx(Emitter *e);
void emit_push_rax(Emitter *e);
void emit_pop_rax(Emitter *e);
void emit_push_rbp(Emitter *e);
void emit_pop_rbp(Emitter *e);
void emit_mov_rbp_rsp(Emitter *e);
void emit_mov_rsp_rbp(Emitter *e);
void emit_sub_rsp_imm(Emitter *e, uint32_t imm);
void emit_add_rsp_imm(Emitter *e, uint32_t imm);
void emit_and_rsp_imm8(Emitter *e, uint8_t imm);
void emit_ret(Emitter *e);
void emit_call_label(Emitter *e, const char *label);
void emit_call_rip(Emitter *e, int32_t disp32);
void emit_jmp_label(Emitter *e, const char *label);
void emit_jz_label(Emitter *e, const char *label);
void emit_jnz_label(Emitter *e, const char *label);
int emit_resolve(Emitter *e);
int emit_get_pos(Emitter *e);
uint8_t* emit_get_code(Emitter *e);
int emit_find_label(Emitter *e, const char *name);
void emit_add_eax_imm(Emitter *e, uint32_t imm);
void emit_sub_eax_imm(Emitter *e, uint32_t imm);
void emit_not_eax(Emitter *e);
void emit_xor_eax_ebx(Emitter *e);
void emit_lea_eax_rip_label(Emitter *e, const char *label);
void emit_lea_eax_rbp_disp(Emitter *e, int disp);
void emit_mov_eax_eax_mem(Emitter *e);
void emit_mov_eax_mem_eax(Emitter *e);
void emit_mov_eax_mem_ebx(Emitter *e);
void emit_inc_rbp_disp(Emitter *e, int disp);
void emit_dec_rbp_disp(Emitter *e, int disp);
void emit_add_rbp_disp_eax(Emitter *e, int disp);
void emit_sub_rbp_disp_eax(Emitter *e, int disp);
void emit_imul_rbp_disp_eax(Emitter *e, int disp);

/* GOTPCREL relocation for Linux */
void emit_reloc_gotpcrel(Emitter *e, const char *label);

#endif