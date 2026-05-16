#include "../include/codegen.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================ */
/* Symbol table for codegen (locals + params)                    */
/* ============================================================ */

typedef struct { char name[128]; int offset; } CGSym;

static CGSym *cg_locals;
static int cg_local_count, cg_local_size, cg_local_capacity;
static CGSym *cg_params;
static int cg_param_count, cg_param_capacity;
static int cg_exit_label;

static int cg_find_local(const char *name) {
    for (int i = 0; i < cg_local_count; i++)
        if (strcmp(cg_locals[i].name, name) == 0) return cg_locals[i].offset;
    return 0;
}
static int cg_find_param(const char *name) {
    for (int i = 0; i < cg_param_count; i++)
        if (strcmp(cg_params[i].name, name) == 0) return cg_params[i].offset;
    return 0;
}
static void cg_add_local(const char *name) {
    if (cg_local_count >= cg_local_capacity) {
        int newcap = cg_local_capacity ? cg_local_capacity * 2 : 64;
        cg_locals = (CGSym*)realloc(cg_locals, newcap * sizeof(CGSym));
        cg_local_capacity = newcap;
    }
    int off = -(cg_local_size + 4);
    strcpy_safe(cg_locals[cg_local_count].name, name);
    cg_locals[cg_local_count].offset = off;
    cg_local_count++; cg_local_size += 4;
}

static void cg_add_global(Codegen *cg, const char *name, int size, int is_pointer) {
    if (cg->global_count >= cg->global_capacity) {
        int newcap = cg->global_capacity ? cg->global_capacity * 2 : 16;
        cg->globals = (CodegenGlobalVar*)realloc(cg->globals, newcap * sizeof(CodegenGlobalVar));
        cg->global_capacity = newcap;
    }
    strcpy_safe(cg->globals[cg->global_count].name, name);
    cg->globals[cg->global_count].data_offset = cg->global_data_size;
    cg->globals[cg->global_count].is_pointer = is_pointer;
    cg->global_data_size += size;
    cg->global_count++;
}

static int cg_find_global(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->global_count; i++) {
        if (strcmp(cg->globals[i].name, name) == 0) return i;
    }
    return -1;
}

/* String pool helper */
static int cg_add_string(Codegen *cg, const char *value) {
    for (int i = 0; i < cg->string_count; i++)
        if (strcmp(cg->strings[i].value, value) == 0) return i;
    if (cg->string_count >= cg->string_capacity) {
        int newcap = cg->string_capacity ? cg->string_capacity * 2 : 64;
        cg->strings = (CodegenStringEntry*)realloc(cg->strings, newcap * sizeof(CodegenStringEntry));
        cg->string_capacity = newcap;
    }
    int id = cg->string_count++;
    strcpy_safe(cg->strings[id].value, value);
    cg->strings[id].label_id = id;
    return id;
}

/* ============================================================ */
/* Forward declarations                                          */
/* ============================================================ */
static void cg_expr(Codegen *cg, ASTNode *n);
static void cg_stmt(Codegen *cg, ASTNode *n, int break_lab, int cont_lab);

/* ============================================================ */
/* Label helpers                                                  */
/* ============================================================ */
static int cg_label(Codegen *cg) { return cg->label_counter++; }

/* ============================================================ */
/* Expression codegen                                            */
/* ============================================================ */
static void cg_expr(Codegen *cg, ASTNode *n) {
    Emitter *e = cg->emitter;
    if (!n) return;

    switch (n->type) {
        case AST_INT_LIT:
            emit_mov_eax_imm(e, (uint32_t)n->d.int_lit.value);
            break;

        case AST_CHAR_LIT:
            emit_mov_eax_imm(e, (unsigned char)n->d.char_lit.value);
            break;

        case AST_STR_LIT: {
            int sid = cg_add_string(cg, n->d.str_lit.value);
            char label[32];
            snprintf(label, sizeof(label), "_str_%d", sid);
            emit_lea_eax_rip_label(e, label);
            break;
        }

        case AST_IDENT: {
            int off = cg_find_local(n->d.ident.name);
            if (off) { emit_mov_eax_rbp_disp(e, off); break; }
            off = cg_find_param(n->d.ident.name);
            if (off) { emit_mov_eax_rbp_disp(e, off); break; }
            int gidx = cg_find_global(cg, n->d.ident.name);
            if (gidx >= 0) {
                char label[32];
                int goff = cg->globals[gidx].data_offset;
                snprintf(label, sizeof(label), "_global_%d", goff);
                if (cg->globals[gidx].is_pointer) {
                    emit_lea_eax_rip_label(e, label);
                } else {
                    emit_mov_eax_rip_label(e, label);
                }
                break;
            }
            emit_xor_eax_eax(e);
            break;
        }

        case AST_THIS:
            emit_mov_eax_rbp_disp(e, 16);
            break;

        case AST_NULL:
            emit_xor_eax_eax(e);
            break;

        case AST_BINARY: {
            ASTOp op = n->d.binary.op;
            if (op == BIN_AND || op == BIN_OR) {
                int lf = cg_label(cg), le = cg_label(cg);
                char f[32], en[32];
                snprintf(f, sizeof(f), "L%d", lf);
                snprintf(en, sizeof(en), "L%d", le);

                cg_expr(cg, n->d.binary.left);
                emit_test_eax_eax(e);
                if (op == BIN_AND) emit_jz_label(e, f); else emit_jnz_label(e, f);

                cg_expr(cg, n->d.binary.right);
                emit_test_eax_eax(e);
                if (op == BIN_AND) emit_jz_label(e, f); else emit_jnz_label(e, f);

                emit_mov_eax_imm(e, 1);
                emit_jmp_label(e, en);
                emit_label_def(e, f);
                emit_xor_eax_eax(e);
                emit_label_def(e, en);
                break;
            }

            cg_expr(cg, n->d.binary.left);
            emit_push_rax(e);
            cg_expr(cg, n->d.binary.right);
            emit_mov_ebx_eax(e);
            emit_pop_rax(e);

            switch (op) {
                case BIN_ADD: emit_add_eax_ebx(e); break;
                case BIN_SUB: emit_sub_eax_ebx(e); break;
                case BIN_MUL: emit_imul_eax_ebx(e); break;
                case BIN_DIV: emit_cdq(e); emit_idiv_ebx(e); break;
                case BIN_MOD: emit_cdq(e); emit_idiv_ebx(e); emit_mov_eax_edx(e); break;
                case BIN_EQ: emit_cmp_eax_ebx(e); emit_setz_al(e); emit_movzx_eax_al(e); break;
                case BIN_NE: emit_cmp_eax_ebx(e); emit_setnz_al(e); emit_movzx_eax_al(e); break;
                case BIN_LT: emit_cmp_eax_ebx(e); emit_setl_al(e); emit_movzx_eax_al(e); break;
                case BIN_GT: emit_cmp_eax_ebx(e); emit_setg_al(e); emit_movzx_eax_al(e); break;
                case BIN_LE: emit_cmp_eax_ebx(e); emit_setle_al(e); emit_movzx_eax_al(e); break;
                case BIN_GE: emit_cmp_eax_ebx(e); emit_setge_al(e); emit_movzx_eax_al(e); break;
                default: emit_xor_eax_eax(e); break;
            }
            break;
        }

        case AST_UNARY: {
            ASTOp op = n->d.unary.op;
            ASTNode *operand = n->d.unary.operand;

            if (op == UN_NEG || op == UN_PLUS || op == UN_NOT || op == UN_BIT_NOT) {
                cg_expr(cg, operand);
                if (op == UN_NEG) emit_neg_eax(e);
                else if (op == UN_PLUS) { /* no-op */ }
                else if (op == UN_NOT) { emit_test_eax_eax(e); emit_mov_eax_imm(e, 0); emit_setz_al(e); emit_movzx_eax_al(e); }
                else if (op == UN_BIT_NOT) { emit_mov_ebx_imm(e, 0xFFFFFFFF); emit_xor_eax_ebx(e); }
                break;
            }

            if (op == UN_ADDR) {
                if (operand->type == AST_IDENT) {
                    int off = cg_find_local(operand->d.ident.name);
                    if (off) { emit_lea_eax_rbp_disp(e, off); break; }
                    off = cg_find_param(operand->d.ident.name);
                    if (off) { emit_lea_eax_rbp_disp(e, off); break; }
                }
                cg_expr(cg, operand);
                break;
            }

            if (op == UN_DEREF) {
                cg_expr(cg, operand);
                emit_mov_eax_eax_mem(e);
                break;
            }

            if (op == UN_PRE_INC || op == UN_PRE_DEC) {
                if (operand->type == AST_IDENT) {
                    int off = cg_find_local(operand->d.ident.name);
                    if (off) {
                        emit_mov_eax_rbp_disp(e, off);
                        if (op == UN_PRE_INC) emit_add_eax_imm(e, 1);
                        else emit_sub_eax_imm(e, 1);
                        emit_mov_rbp_disp_eax(e, off);
                        break;
                    }
                    off = cg_find_param(operand->d.ident.name);
                    if (off) {
                        emit_mov_eax_rbp_disp(e, off);
                        if (op == UN_PRE_INC) emit_add_eax_imm(e, 1);
                        else emit_sub_eax_imm(e, 1);
                        emit_mov_rbp_disp_eax(e, off);
                        break;
                    }
                }
                cg_expr(cg, operand);
                if (op == UN_PRE_INC) emit_add_eax_imm(e, 1);
                else emit_sub_eax_imm(e, 1);
                break;
            }

            if (op == UN_POST_INC || op == UN_POST_DEC) {
                if (operand->type == AST_IDENT) {
                    int off = cg_find_local(operand->d.ident.name);
                    if (off) {
                        emit_mov_eax_rbp_disp(e, off);
                        emit_push_rax(e);
                        if (op == UN_POST_INC) emit_add_eax_imm(e, 1);
                        else emit_sub_eax_imm(e, 1);
                        emit_mov_rbp_disp_eax(e, off);
                        emit_pop_rax(e);
                        break;
                    }
                    off = cg_find_param(operand->d.ident.name);
                    if (off) {
                        emit_mov_eax_rbp_disp(e, off);
                        emit_push_rax(e);
                        if (op == UN_POST_INC) emit_add_eax_imm(e, 1);
                        else emit_sub_eax_imm(e, 1);
                        emit_mov_rbp_disp_eax(e, off);
                        emit_pop_rax(e);
                        break;
                    }
                }
                cg_expr(cg, operand);
                if (op == UN_POST_INC) emit_add_eax_imm(e, 1);
                else emit_sub_eax_imm(e, 1);
                break;
            }

            break;
        }

        case AST_ASSIGN: {
            ASTOp aop = n->d.assign.op;
            if (aop == BIN_ASSIGN) {
                cg_expr(cg, n->d.assign.value);
                if (n->d.assign.target->type == AST_IDENT) {
                    int off = cg_find_local(n->d.assign.target->d.ident.name);
                    if (off) emit_mov_rbp_disp_eax(e, off);
                    else {
                        off = cg_find_param(n->d.assign.target->d.ident.name);
                        if (off) emit_mov_rbp_disp_eax(e, off);
                    }
                } else if (n->d.assign.target->type == AST_MEMBER) {
                    ASTNode *member = n->d.assign.target;
                    emit_mov_ebx_eax(e);
                    if (member->d.member.obj->type == AST_IDENT) {
                        int off = cg_find_local(member->d.member.obj->d.ident.name);
                        if (!off) off = cg_find_param(member->d.member.obj->d.ident.name);
                        if (off) {
                            if (member->d.member.arrow) {
                                emit_mov_eax_rbp_disp(e, off);
                            } else {
                                emit_lea_eax_rbp_disp(e, off);
                            }
                            if (member->d.member.member_offset >= 0)
                                emit_add_eax_imm(e, member->d.member.member_offset);
                            emit_mov_eax_mem_ebx(e);
                            break;
                        }
                    }
                    cg_expr(cg, member->d.member.obj);
                    if (member->d.member.arrow) {
                        emit_mov_eax_eax_mem(e);
                    }
                    if (member->d.member.member_offset >= 0)
                        emit_add_eax_imm(e, member->d.member.member_offset);
                    emit_mov_eax_mem_ebx(e);
                }
            } else {
                if (n->d.assign.target->type == AST_IDENT) {
                    int off = cg_find_local(n->d.assign.target->d.ident.name);
                    if (off) {
                        emit_mov_eax_rbp_disp(e, off);
                        emit_push_rax(e);
                        cg_expr(cg, n->d.assign.value);
                        emit_mov_ebx_eax(e);
                        emit_pop_rax(e);
                        switch (aop) {
                            case BIN_ADD: emit_add_eax_ebx(e); break;
                            case BIN_SUB: emit_sub_eax_ebx(e); break;
                            case BIN_MUL: emit_imul_eax_ebx(e); break;
                            case BIN_DIV: emit_cdq(e); emit_idiv_ebx(e); break;
                            default: break;
                        }
                        emit_mov_rbp_disp_eax(e, off);
                        break;
                    }
                    off = cg_find_param(n->d.assign.target->d.ident.name);
                    if (off) {
                        emit_mov_eax_rbp_disp(e, off);
                        emit_push_rax(e);
                        cg_expr(cg, n->d.assign.value);
                        emit_mov_ebx_eax(e);
                        emit_pop_rax(e);
                        switch (aop) {
                            case BIN_ADD: emit_add_eax_ebx(e); break;
                            case BIN_SUB: emit_sub_eax_ebx(e); break;
                            case BIN_MUL: emit_imul_eax_ebx(e); break;
                            case BIN_DIV: emit_cdq(e); emit_idiv_ebx(e); break;
                            default: break;
                        }
                        emit_mov_rbp_disp_eax(e, off);
                        break;
                    }
                }
                cg_expr(cg, n->d.assign.value);
            }
            break;
        }

        case AST_CALL: {
            /* Determine function name and import status first */
            char label[256];
            const char *fname = NULL;
            if (n->d.call.func->type == AST_IDENT) {
                fname = n->d.call.func->d.ident.name;
                snprintf(label, sizeof(label), "_%s", fname);
            } else if (n->d.call.func->type == AST_MEMBER) {
                fname = n->d.call.func->d.member.member;
                snprintf(label, sizeof(label), "_%s", fname);
            }

            int is_import = 0;
            if (fname) {
                const char *imports[] = {"printf","scanf","malloc","free","exit","puts","gets","fopen","fclose", NULL};
                for (int k = 0; imports[k]; k++)
                    if (strcmp(fname, imports[k]) == 0) { is_import = 1; break; }
            }

            int ac = 0; for (ASTNode *a = n->d.call.args; a; a = a->next) ac++;
            ASTNode *args[32];
            int idx = 0;
            for (ASTNode *a = n->d.call.args; a; a = a->next) args[idx++] = a;

            int stack_cleanup = 0;

            if (is_import && cg->target_os == TARGET_WINDOWS) {
                /* Windows x64 calling convention: first 4 args in RCX, RDX, R8, R9 */
                int stack_args = ac > 4 ? ac - 4 : 0;
                for (int i = idx - 1; i >= 0; i--) {
                    cg_expr(cg, args[i]);
                    if (i == 0)      emit_mov_rcx_rax(e);
                    else if (i == 1) emit_mov_rdx_rax(e);
                    else if (i == 2) emit_mov_r8_rax(e);
                    else if (i == 3) emit_mov_r9_rax(e);
                    else             emit_push_rax(e);
                }
                /* Callee expects 32 bytes shadow space + any stack args */
                emit_sub_rsp_imm(e, 32 + stack_args * 8);
                stack_cleanup = 32 + stack_args * 8;
            } else {
                /* Push all args on stack (internal calls and Linux) */
                for (int i = idx - 1; i >= 0; i--) {
                    cg_expr(cg, args[i]);
                    emit_push_rax(e);
                }
                if (n->d.call.func->type == AST_MEMBER) {
                    cg_expr(cg, n->d.call.func->d.member.obj);
                    emit_push_rax(e);
                    ac++;
                }
                stack_cleanup = ac * 8;
            }

            if (fname) {
                if (is_import) {
                    if (cg->import_call_count >= cg->import_call_capacity) {
                        int newcap = cg->import_call_capacity ? cg->import_call_capacity * 2 : 64;
                        cg->import_call_sites = (int*)realloc(cg->import_call_sites, newcap * sizeof(int));
                        cg->import_names = (char(*)[64])realloc(cg->import_names, newcap * 64);
                        cg->import_call_capacity = newcap;
                    }
                    if (cg->target_os == TARGET_WINDOWS) {
                        int save = emit_get_pos(e);
                        emit_byte(e, 0xFF);
                        emit_byte(e, 0x15);
                        emit_dword(e, 0);
                        cg->import_call_sites[cg->import_call_count] = save;
                        strcpy_safe(cg->import_names[cg->import_call_count], fname);
                        cg->import_call_count++;
                    } else {
                        int save = emit_get_pos(e);
                        emit_byte(e, 0xFF);
                        emit_byte(e, 0x15);
                        emit_reloc_gotpcrel(e, label);
                        cg->import_call_sites[cg->import_call_count] = save;
                        strcpy_safe(cg->import_names[cg->import_call_count], fname);
                        cg->import_call_count++;
                    }
                } else {
                    emit_call_label(e, label);
                }
            }

            if (stack_cleanup > 0) emit_add_rsp_imm(e, stack_cleanup);
            break;
        }

        case AST_MEMBER: {
            if (n->d.member.member_offset >= 0 && n->d.member.obj->type == AST_IDENT) {
                int off = cg_find_local(n->d.member.obj->d.ident.name);
                if (!off) off = cg_find_param(n->d.member.obj->d.ident.name);
                if (off) {
                    if (n->d.member.arrow) {
                        emit_mov_eax_rbp_disp(e, off);
                        emit_add_eax_imm(e, n->d.member.member_offset);
                        emit_mov_eax_eax_mem(e);
                    } else {
                        emit_mov_eax_rbp_disp(e, off + n->d.member.member_offset);
                    }
                    break;
                }
            }
            cg_expr(cg, n->d.member.obj);
            if (n->d.member.arrow) {
                emit_mov_eax_eax_mem(e);
            }
            break;
        }

        case AST_NEW: {
            const char *cname = n->d.new_expr.class_name;
            int size = 4;
            for (int ci = 0; ci < cg->class_count; ci++)
                if (strcmp(cg->class_sizes[ci].name, cname) == 0)
                    { size = cg->class_sizes[ci].size; break; }
            emit_mov_eax_imm(e, size > 0 ? size : 4);
            emit_push_rax(e);
            if (cg->import_call_count >= cg->import_call_capacity) {
                int newcap = cg->import_call_capacity ? cg->import_call_capacity * 2 : 64;
                cg->import_call_sites = (int*)realloc(cg->import_call_sites, newcap * sizeof(int));
                cg->import_names = (char(*)[64])realloc(cg->import_names, newcap * 64);
                cg->import_call_capacity = newcap;
            }
            int save = emit_get_pos(e);
            emit_byte(e, 0xFF);
            emit_byte(e, 0x15);
            emit_dword(e, 0);
            cg->import_call_sites[cg->import_call_count] = save;
            strcpy_safe(cg->import_names[cg->import_call_count], "malloc");
            cg->import_call_count++;
            emit_add_rsp_imm(e, 8);
            break;
        }

        case AST_DELETE: {
            cg_expr(cg, n->d.delete_expr.expr);
            emit_push_rax(e);
            if (cg->import_call_count >= cg->import_call_capacity) {
                int newcap = cg->import_call_capacity ? cg->import_call_capacity * 2 : 64;
                cg->import_call_sites = (int*)realloc(cg->import_call_sites, newcap * sizeof(int));
                cg->import_names = (char(*)[64])realloc(cg->import_names, newcap * 64);
                cg->import_call_capacity = newcap;
            }
            int save = emit_get_pos(e);
            emit_byte(e, 0xFF);
            emit_byte(e, 0x15);
            emit_dword(e, 0);
            cg->import_call_sites[cg->import_call_count] = save;
            strcpy_safe(cg->import_names[cg->import_call_count], "free");
            cg->import_call_count++;
            emit_add_rsp_imm(e, 8);
            emit_xor_eax_eax(e);
            break;
        }

        case AST_CAST: {
            cg_expr(cg, n->d.cast.expr);
            break;
        }

        case AST_SIZEOF: {
            emit_mov_eax_imm(e, 4);
            break;
        }

        default:
            emit_xor_eax_eax(e);
            break;
    }
}

/* ============================================================ */
/* Statement codegen                                             */
/* ============================================================ */
static void cg_stmt(Codegen *cg, ASTNode *n, int break_lab, int cont_lab) {
    Emitter *e = cg->emitter;
    if (!n) return;

    switch (n->type) {
        case AST_BLOCK:
            for (ASTNode *s = n->d.block.stmts; s; s = s->next)
                cg_stmt(cg, s, break_lab, cont_lab);
            break;

        case AST_VAR_DECL:
            cg_add_local(n->d.var_decl.name);
            if (n->d.var_decl.init) {
                cg_expr(cg, n->d.var_decl.init);
                int off = cg_find_local(n->d.var_decl.name);
                if (off) emit_mov_rbp_disp_eax(e, off);
            }
            break;

        case AST_IF: {
            int le = cg_label(cg), lx = cg_label(cg);
            char be[32], bx[32];
            snprintf(be, sizeof(be), "L%d", le);
            snprintf(bx, sizeof(bx), "L%d", lx);

            cg_expr(cg, n->d.if_stmt.cond);
            emit_test_eax_eax(e);
            emit_jz_label(e, be);
            cg_stmt(cg, n->d.if_stmt.then, break_lab, cont_lab);
            emit_jmp_label(e, bx);
            emit_label_def(e, be);
            if (n->d.if_stmt.els)
                cg_stmt(cg, n->d.if_stmt.els, break_lab, cont_lab);
            emit_label_def(e, bx);
            break;
        }

        case AST_WHILE: {
            int ls = cg_label(cg), le = cg_label(cg);
            char bs[32], es[32];
            snprintf(bs, sizeof(bs), "L%d", ls);
            snprintf(es, sizeof(es), "L%d", le);

            emit_label_def(e, bs);
            cg_expr(cg, n->d.loop.cond);
            emit_test_eax_eax(e);
            emit_jz_label(e, es);
            cg_stmt(cg, n->d.loop.body, le, ls);
            emit_jmp_label(e, bs);
            emit_label_def(e, es);
            break;
        }

        case AST_FOR: {
            int ls = cg_label(cg), le = cg_label(cg), li = cg_label(cg);
            char bs[32], es[32], istr[32];
            snprintf(bs, sizeof(bs), "L%d", ls);
            snprintf(es, sizeof(es), "L%d", le);
            snprintf(istr, sizeof(istr), "L%d", li);

            if (n->d.for_stmt.init) cg_expr(cg, n->d.for_stmt.init);
            emit_label_def(e, bs);
            if (n->d.for_stmt.cond) {
                cg_expr(cg, n->d.for_stmt.cond);
                emit_test_eax_eax(e);
                emit_jz_label(e, es);
            }
            cg_stmt(cg, n->d.for_stmt.body, le, li);
            emit_label_def(e, istr);
            if (n->d.for_stmt.incr) cg_expr(cg, n->d.for_stmt.incr);
            emit_jmp_label(e, bs);
            emit_label_def(e, es);
            break;
        }

        case AST_RETURN:
            if (n->d.ret.expr) cg_expr(cg, n->d.ret.expr);
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "_exit_%d", cg_exit_label);
                emit_jmp_label(e, buf);
            }
            break;

        case AST_EXPR_STMT:
            cg_expr(cg, n->d.binary.left);
            break;

        case AST_BREAK: {
            char buf[32];
            snprintf(buf, sizeof(buf), "L%d", break_lab);
            emit_jmp_label(e, buf);
            break;
        }

        case AST_CONTINUE: {
            char buf[32];
            snprintf(buf, sizeof(buf), "L%d", cont_lab);
            emit_jmp_label(e, buf);
            break;
        }

        default:
            break;
    }
}

/* ============================================================ */
/* Public API                                                    */
/* ============================================================ */
void codegen_init(Codegen *cg, TokenRegistry *reg, Emitter *em) {
    memset(cg, 0, sizeof(*cg));
    cg->registry = reg;
    cg->emitter = em;
    cg->target_os = TARGET_WINDOWS; /* default */
    cg->import_call_sites = NULL;
    cg->import_names = NULL;
    cg->import_call_count = 0;
    cg->import_call_capacity = 0;
    cg->strings = NULL;
    cg->string_count = 0;
    cg->string_capacity = 0;
    cg->class_sizes = NULL;
    cg->class_count = 0;
    cg->class_capacity = 0;
    cg->globals = NULL;
    cg->global_count = 0;
    cg->global_capacity = 0;
    cg->global_data_size = 0;

    cg_add_global(cg, "argc", 4, 0);
    cg_add_global(cg, "argv", 8, 1);
}

void codegen_generate(Codegen *cg, ASTNode *program) {
    Emitter *e = cg->emitter;
    if (!program || program->type != AST_PROGRAM) return;

    /* Reset per-compilation-unit state */
    cg_local_count = 0; cg_local_size = 0; cg_local_capacity = 0;
    cg_param_count = 0; cg_param_capacity = 0;
    cg_locals = NULL; cg_params = NULL;

    /* Collect function names and class sizes */
    int func_capacity = 64;
    char (*func_names)[128] = (char(*)[128])malloc(func_capacity * 128);
    int func_count = 0;
    for (int i = 0; i < program->d.program.count; i++) {
        ASTNode *n = program->d.program.nodes[i];
        if (n->type == AST_FUNC_DEF) {
            if (func_count >= func_capacity) {
                func_capacity *= 2;
                func_names = (char(*)[128])realloc(func_names, func_capacity * 128);
            }
            snprintf(func_names[func_count++], 128, "%s", n->d.func_def.name);
        }
        if (n->type == AST_CLASS_DEF || n->type == AST_STRUCT_DEF) {
            if (cg->class_count >= cg->class_capacity) {
                int newcap = cg->class_capacity ? cg->class_capacity * 2 : 16;
                cg->class_sizes = (CodegenClassSize*)realloc(cg->class_sizes, newcap * sizeof(CodegenClassSize));
                cg->class_capacity = newcap;
            }
            int ci = cg->class_count;
            snprintf(cg->class_sizes[ci].name, sizeof(cg->class_sizes[ci].name), "%s", n->d.struct_def.name);
            cg->class_sizes[ci].size = n->d.struct_def.size;
            cg->class_count++;
        }
    }

    for (int i = 0; i < program->d.program.count; i++) {
        ASTNode *func = program->d.program.nodes[i];
        if (func->type != AST_FUNC_DEF) continue;

        cg_local_count = 0; cg_local_size = 0;
        cg_param_count = 0; cg_param_capacity = 0;
        free(cg_params); cg_params = NULL;

        {
            int poff = 16;
            if (func->d.func_def.is_method) {
                if (cg_param_count >= cg_param_capacity) {
                    cg_param_capacity = cg_param_capacity ? cg_param_capacity * 2 : 16;
                    cg_params = (CGSym*)realloc(cg_params, cg_param_capacity * sizeof(CGSym));
                }
                strcpy_safe(cg_params[0].name, "this");
                cg_params[0].offset = poff;
                cg_param_count = 1;
                poff += 8;
            }
            for (ASTNode *p = func->d.func_def.params; p; p = p->next) {
                if (cg_param_count >= cg_param_capacity) {
                    cg_param_capacity = cg_param_capacity ? cg_param_capacity * 2 : 16;
                    cg_params = (CGSym*)realloc(cg_params, cg_param_capacity * sizeof(CGSym));
                }
                strcpy_safe(cg_params[cg_param_count].name, p->d.var_decl.name);
                cg_params[cg_param_count].offset = poff;
                cg_param_count++;
                poff += 8;
            }
        }

        cg_exit_label = cg_label(cg);

        char flabel[256];
        snprintf(flabel, 256, "_%s", func->d.func_def.name);
        emit_label_def(e, flabel);

        emit_push_rbp(e);
        emit_mov_rbp_rsp(e);
        emit_sub_rsp_imm(e, 256);

        cg_stmt(cg, func->d.func_def.body, 0, 0);

        char elabel[32];
        snprintf(elabel, 32, "_exit_%d", cg_exit_label);
        emit_label_def(e, elabel);
        emit_mov_rsp_rbp(e);
        emit_pop_rbp(e);
        emit_ret(e);
    }

    for (int i = 0; i < cg->string_count; i++) {
        char label[32];
        snprintf(label, sizeof(label), "_str_%d", i);
        emit_label_def(e, label);
        int len = strlen(cg->strings[i].value);
        emit_bytes(e, (uint8_t*)cg->strings[i].value, len + 1);
    }

    for (int i = 0; i < cg->global_count; i++) {
        char label[32];
        snprintf(label, sizeof(label), "_global_%d", cg->globals[i].data_offset);
        emit_label_def(e, label);
        if (cg->globals[i].is_pointer) {
            emit_qword(e, 0);
        } else {
            emit_dword(e, 0);
        }
    }

    emit_resolve(e);
    free(func_names);
    free(cg_locals);
    free(cg_params);
    cg_locals = NULL;
    cg_params = NULL;
}

int codegen_get_entry(Codegen *cg) {
    return emit_find_label(cg->emitter, "_main");
}

int codegen_get_import_call_count(Codegen *cg) { return cg->import_call_count; }
int codegen_get_import_call_pos(Codegen *cg, int i) { return cg->import_call_sites[i]; }
const char* codegen_get_import_call_name(Codegen *cg, int i) { return cg->import_names[i]; }

int codegen_get_global_count(Codegen *cg) { return cg->global_count; }
int codegen_get_global_offset(Codegen *cg, int idx) {
    if (idx >= 0 && idx < cg->global_count) return cg->globals[idx].data_offset;
    return -1;
}
const char* codegen_get_global_name(Codegen *cg, int idx) {
    if (idx >= 0 && idx < cg->global_count) return cg->globals[idx].name;
    return NULL;
}