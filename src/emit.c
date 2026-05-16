#include "../include/emit.h"
#include "../include/token.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void emit_init(Emitter *e) {
    e->code = NULL;
    e->capacity = 0;
    e->pos = 0;
    e->label_count = 0;
    e->labels_capacity = 0;
    e->labels = NULL;
    e->reloc_count = 0;
    e->relocs_capacity = 0;
    e->relocs = NULL;
    e->debug_count = 0;
    e->debug_capacity = 0;
    e->debug_map = NULL;
}

void emit_free(Emitter *e) {
    free(e->code);
    free(e->labels);
    free(e->relocs);
    free(e->debug_map);
}

static void ensure_capacity(Emitter *e, int needed) {
    while (e->pos + needed > e->capacity) {
        e->capacity = e->capacity ? e->capacity * 2 : 4096;
        e->code = (uint8_t*)realloc(e->code, e->capacity);
    }
}

void emit_byte(Emitter *e, uint8_t b) {
    ensure_capacity(e, 1);
    e->code[e->pos++] = b;
}

void emit_word(Emitter *e, uint16_t w) {
    ensure_capacity(e, 2);
    e->code[e->pos++] = w & 0xFF;
    e->code[e->pos++] = (w >> 8) & 0xFF;
}

void emit_dword(Emitter *e, uint32_t dw) {
    ensure_capacity(e, 4);
    e->code[e->pos++] = dw & 0xFF;
    e->code[e->pos++] = (dw >> 8) & 0xFF;
    e->code[e->pos++] = (dw >> 16) & 0xFF;
    e->code[e->pos++] = (dw >> 24) & 0xFF;
}

void emit_qword(Emitter *e, uint64_t qw) {
    ensure_capacity(e, 8);
    for (int i = 0; i < 8; i++) {
        e->code[e->pos++] = (qw >> (i * 8)) & 0xFF;
    }
}

void emit_bytes(Emitter *e, const uint8_t *data, int len) {
    ensure_capacity(e, len);
    memcpy(e->code + e->pos, data, len);
    e->pos += len;
}

static int find_or_add_label(Emitter *e, const char *name, int is_def) {
    for (int i = 0; i < e->label_count; i++) {
        if (strcmp(e->labels[i].name, name) == 0) {
            if (is_def) e->labels[i].defined = 1;
            return i;
        }
    }
    if (e->label_count >= e->labels_capacity) {
        int newcap = e->labels_capacity ? e->labels_capacity * 2 : 64;
        e->labels = (Label*)realloc(e->labels, newcap * sizeof(Label));
        e->labels_capacity = newcap;
    }
    int idx = e->label_count++;
    strcpy_safe(e->labels[idx].name, name);
    e->labels[idx].offset = is_def ? e->pos : 0;
    e->labels[idx].defined = is_def;
    return idx;
}

int emit_label_def(Emitter *e, const char *name) {
    int idx = find_or_add_label(e, name, 1);
    if (idx >= 0) {
        e->labels[idx].offset = e->pos;
        if (e->debug_count >= e->debug_capacity) {
            int newcap = e->debug_capacity ? e->debug_capacity * 2 : 64;
            e->debug_map = (DebugEntry*)realloc(e->debug_map, newcap * sizeof(DebugEntry));
            e->debug_capacity = newcap;
        }
        e->debug_map[e->debug_count].label_idx = idx;
        e->debug_map[e->debug_count].offset = e->pos;
        e->debug_count++;
    }
    return idx;
}

int emit_label_ref(Emitter *e, const char *name) {
    return find_or_add_label(e, name, 0);
}

/* === ModRM helpers === */
static uint8_t modrm(int mod, int reg, int rm) {
    return (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

static void rex_w(Emitter *e) { emit_byte(e, 0x48); }

/* === Basic instructions === */
void emit_mov_eax_imm(Emitter *e, uint32_t imm) { emit_byte(e, 0xB8); emit_dword(e, imm); }
void emit_mov_ebx_imm(Emitter *e, uint32_t imm) { emit_byte(e, 0xBB); emit_dword(e, imm); }
void emit_mov_ebx_eax(Emitter *e) { emit_byte(e, 0x89); emit_byte(e, modrm(3, 0, 3)); }
void emit_mov_rcx_rax(Emitter *e) { emit_byte(e, 0x48); emit_byte(e, 0x89); emit_byte(e, 0xC1); }
void emit_mov_rdx_rax(Emitter *e) { emit_byte(e, 0x48); emit_byte(e, 0x89); emit_byte(e, 0xC2); }
void emit_mov_r8_rax(Emitter *e)  { emit_byte(e, 0x49); emit_byte(e, 0x89); emit_byte(e, 0xC0); }
void emit_mov_r9_rax(Emitter *e)  { emit_byte(e, 0x49); emit_byte(e, 0x89); emit_byte(e, 0xC1); }
void emit_mov_eax_rbp_disp(Emitter *e, int disp) {
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0x8B);
        emit_byte(e, modrm(1, 0, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_mov_eax_rbp_disp32(e, disp);
    }
}
void emit_mov_rbp_disp_eax(Emitter *e, int disp) {
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0x89);
        emit_byte(e, modrm(1, 0, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_mov_rbp_disp32_eax(e, disp);
    }
}
void emit_mov_eax_rbp_disp32(Emitter *e, int disp) {
    emit_byte(e, 0x8B);
    emit_byte(e, modrm(2, 0, 5));
    emit_dword(e, (uint32_t)disp);
}
void emit_mov_rbp_disp32_eax(Emitter *e, int disp) {
    emit_byte(e, 0x89);
    emit_byte(e, modrm(2, 0, 5));
    emit_dword(e, (uint32_t)disp);
}
void emit_xor_eax_eax(Emitter *e) { emit_byte(e, 0x31); emit_byte(e, modrm(3, 0, 0)); }
void emit_add_eax_ebx(Emitter *e) { emit_byte(e, 0x01); emit_byte(e, modrm(3, 3, 0)); }
void emit_sub_eax_ebx(Emitter *e) { emit_byte(e, 0x29); emit_byte(e, modrm(3, 3, 0)); }
void emit_imul_eax_ebx(Emitter *e) { emit_byte(e, 0x0F); emit_byte(e, 0xAF); emit_byte(e, modrm(3, 0, 3)); }
void emit_cdq(Emitter *e) { emit_byte(e, 0x99); }
void emit_idiv_ebx(Emitter *e) { emit_byte(e, 0xF7); emit_byte(e, modrm(3, 7, 3)); }
void emit_neg_eax(Emitter *e) { emit_byte(e, 0xF7); emit_byte(e, modrm(3, 3, 0)); }
void emit_cmp_eax_ebx(Emitter *e) { emit_byte(e, 0x39); emit_byte(e, modrm(3, 3, 0)); }
void emit_test_eax_eax(Emitter *e) { emit_byte(e, 0x85); emit_byte(e, modrm(3, 0, 0)); }

static void emit_setcc_al(Emitter *e, uint8_t cc) {
    emit_byte(e, 0x0F);
    emit_byte(e, cc);
    emit_byte(e, modrm(3, 0, 0));
}
void emit_setz_al(Emitter *e)   { emit_setcc_al(e, 0x94); }
void emit_setnz_al(Emitter *e)  { emit_setcc_al(e, 0x95); }
void emit_setl_al(Emitter *e)   { emit_setcc_al(e, 0x9C); }
void emit_setg_al(Emitter *e)   { emit_setcc_al(e, 0x9F); }
void emit_setle_al(Emitter *e)  { emit_setcc_al(e, 0x9E); }
void emit_setge_al(Emitter *e)  { emit_setcc_al(e, 0x9D); }

void emit_movzx_eax_al(Emitter *e) { emit_byte(e, 0x0F); emit_byte(e, 0xB6); emit_byte(e, modrm(3, 0, 0)); }
void emit_mov_eax_edx(Emitter *e) { emit_byte(e, 0x89); emit_byte(e, modrm(3, 2, 0)); }
void emit_push_rax(Emitter *e) { emit_byte(e, 0x50); }
void emit_pop_rax(Emitter *e)  { emit_byte(e, 0x58); }
void emit_push_rbp(Emitter *e) { emit_byte(e, 0x55); }
void emit_pop_rbp(Emitter *e)  { emit_byte(e, 0x5D); }
void emit_mov_rbp_rsp(Emitter *e) { rex_w(e); emit_byte(e, 0x89); emit_byte(e, modrm(3, 4, 5)); }
void emit_mov_rsp_rbp(Emitter *e) { rex_w(e); emit_byte(e, 0x89); emit_byte(e, modrm(3, 5, 4)); }
void emit_sub_rsp_imm(Emitter *e, uint32_t imm) { rex_w(e); emit_byte(e, 0x81); emit_byte(e, modrm(3, 5, 4)); emit_dword(e, imm); }
void emit_add_rsp_imm(Emitter *e, uint32_t imm) { rex_w(e); emit_byte(e, 0x81); emit_byte(e, modrm(3, 0, 4)); emit_dword(e, imm); }
void emit_and_rsp_imm8(Emitter *e, uint8_t imm) { rex_w(e); emit_byte(e, 0x83); emit_byte(e, modrm(3, 4, 4)); emit_byte(e, imm); }
void emit_ret(Emitter *e) { emit_byte(e, 0xC3); }
void emit_add_eax_imm(Emitter *e, uint32_t imm) { emit_byte(e, 0x05); emit_dword(e, imm); }
void emit_sub_eax_imm(Emitter *e, uint32_t imm) { emit_byte(e, 0x2D); emit_dword(e, imm); }
void emit_not_eax(Emitter *e) { emit_byte(e, 0xF7); emit_byte(e, modrm(3, 2, 0)); }
void emit_xor_eax_ebx(Emitter *e) { emit_byte(e, 0x31); emit_byte(e, modrm(3, 3, 0)); }
static void ensure_reloc_capacity(Emitter *e) {
    if (e->reloc_count >= e->relocs_capacity) {
        int newcap = e->relocs_capacity ? e->relocs_capacity * 2 : 128;
        e->relocs = (Reloc*)realloc(e->relocs, newcap * sizeof(Reloc));
        e->relocs_capacity = newcap;
    }
}

void emit_lea_eax_rip_label(Emitter *e, const char *label) {
    emit_byte(e, 0x48); /* REX.W for 64-bit address */
    emit_byte(e, 0x8D);
    emit_byte(e, 0x05);
    int idx = emit_label_ref(e, label);
    if (idx >= 0) {
        ensure_reloc_capacity(e);
        e->relocs[e->reloc_count].offset = e->pos;
        e->relocs[e->reloc_count].label_idx = idx;
        e->relocs[e->reloc_count].type = RELOC_JMP;
        e->reloc_count++;
        emit_dword(e, 0);
    } else {
        emit_dword(e, 0);
    }
}

void emit_mov_eax_rip_label(Emitter *e, const char *label) {
    emit_byte(e, 0x48); /* REX.W for 64-bit address */
    emit_byte(e, 0x8B);
    emit_byte(e, 0x05);
    int idx = emit_label_ref(e, label);
    if (idx >= 0) {
        ensure_reloc_capacity(e);
        e->relocs[e->reloc_count].offset = e->pos;
        e->relocs[e->reloc_count].label_idx = idx;
        e->relocs[e->reloc_count].type = RELOC_JMP;
        e->reloc_count++;
        emit_dword(e, 0);
    } else {
        emit_dword(e, 0);
    }
}

void emit_lea_eax_rbp_disp(Emitter *e, int disp) {
    emit_byte(e, 0x48); /* REX.W for 64-bit address */
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0x8D);
        emit_byte(e, modrm(1, 0, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_byte(e, 0x8D);
        emit_byte(e, modrm(2, 0, 5));
        emit_dword(e, (uint32_t)disp);
    }
}
void emit_mov_eax_eax_mem(Emitter *e) { emit_byte(e, 0x8B); emit_byte(e, modrm(0, 0, 0)); }
void emit_mov_eax_mem_eax(Emitter *e) { emit_byte(e, 0x89); emit_byte(e, modrm(0, 0, 0)); }
void emit_mov_eax_mem_ebx(Emitter *e) { emit_byte(e, 0x89); emit_byte(e, modrm(0, 3, 0)); }
void emit_inc_rbp_disp(Emitter *e, int disp) {
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0xFF);
        emit_byte(e, modrm(1, 0, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_byte(e, 0xFF);
        emit_byte(e, modrm(2, 0, 5));
        emit_dword(e, (uint32_t)disp);
    }
}
void emit_dec_rbp_disp(Emitter *e, int disp) {
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0xFF);
        emit_byte(e, modrm(1, 1, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_byte(e, 0xFF);
        emit_byte(e, modrm(2, 1, 5));
        emit_dword(e, (uint32_t)disp);
    }
}
void emit_add_rbp_disp_eax(Emitter *e, int disp) {
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0x01);
        emit_byte(e, modrm(1, 0, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_byte(e, 0x01);
        emit_byte(e, modrm(2, 0, 5));
        emit_dword(e, (uint32_t)disp);
    }
}
void emit_sub_rbp_disp_eax(Emitter *e, int disp) {
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0x29);
        emit_byte(e, modrm(1, 0, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_byte(e, 0x29);
        emit_byte(e, modrm(2, 0, 5));
        emit_dword(e, (uint32_t)disp);
    }
}
void emit_imul_rbp_disp_eax(Emitter *e, int disp) {
    if (disp >= -128 && disp <= 127) {
        emit_byte(e, 0x0F);
        emit_byte(e, 0xAF);
        emit_byte(e, modrm(1, 0, 5));
        emit_byte(e, (uint8_t)(disp & 0xFF));
    } else {
        emit_byte(e, 0x0F);
        emit_byte(e, 0xAF);
        emit_byte(e, modrm(2, 0, 5));
        emit_dword(e, (uint32_t)disp);
    }
}

/* Relocation helpers */
static void emit_reloc(Emitter *e, const char *label, RelocType type) {
    int idx = emit_label_ref(e, label);
    if (idx < 0) return;
    ensure_reloc_capacity(e);
    e->relocs[e->reloc_count].offset = e->pos;
    e->relocs[e->reloc_count].label_idx = idx;
    e->relocs[e->reloc_count].type = type;
    e->reloc_count++;
    emit_dword(e, 0);
}
void emit_call_label(Emitter *e, const char *label) { emit_byte(e, 0xE8); emit_reloc(e, label, RELOC_CALL); }
void emit_jmp_label(Emitter *e, const char *label)  { emit_byte(e, 0xE9); emit_reloc(e, label, RELOC_JMP); }
void emit_jz_label(Emitter *e, const char *label)   { emit_byte(e, 0x0F); emit_byte(e, 0x84); emit_reloc(e, label, RELOC_JCC); }
void emit_jnz_label(Emitter *e, const char *label)  { emit_byte(e, 0x0F); emit_byte(e, 0x85); emit_reloc(e, label, RELOC_JCC); }
void emit_call_rip(Emitter *e, int32_t disp32) {
    emit_byte(e, 0xFF);
    emit_byte(e, 0x15);
    emit_dword(e, (uint32_t)disp32);
}
void emit_reloc_gotpcrel(Emitter *e, const char *label) {
    int idx = emit_label_ref(e, label);
    if (idx < 0) return;
    ensure_reloc_capacity(e);
    e->relocs[e->reloc_count].offset = e->pos;
    e->relocs[e->reloc_count].label_idx = idx;
    e->relocs[e->reloc_count].type = RELOC_GOTPCREL;
    e->reloc_count++;
    emit_dword(e, 0);
}
int emit_get_pos(Emitter *e) { return e->pos; }
uint8_t* emit_get_code(Emitter *e) { return e->code; }

int emit_resolve(Emitter *e) {
    for (int i = 0; i < e->reloc_count; i++) {
        Reloc *r = &e->relocs[i];
        /* GOTPCREL relocs are handled by the ELF writer */
        if (r->type == RELOC_GOTPCREL) continue;
        Label *l = &e->labels[r->label_idx];
        if (!l->defined) {
            fprintf(stderr, "Error: label '%s' not defined\n", l->name);
            return -1;
        }
        int target = l->offset;
        int from = r->offset;
        int32_t rel32 = target - (from + 4);
        e->code[from]     = (uint8_t)(rel32 & 0xFF);
        e->code[from + 1] = (uint8_t)((rel32 >> 8) & 0xFF);
        e->code[from + 2] = (uint8_t)((rel32 >> 16) & 0xFF);
        e->code[from + 3] = (uint8_t)((rel32 >> 24) & 0xFF);
    }
    return 0;
}

int emit_find_label(Emitter *e, const char *name) {
    for (int i = 0; i < e->label_count; i++) {
        if (strcmp(e->labels[i].name, name) == 0 && e->labels[i].defined)
            return e->labels[i].offset;
    }
    return -1;
}