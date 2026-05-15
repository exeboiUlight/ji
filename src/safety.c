#include "../include/safety.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void safety_init(SafetyContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->null_checks_enabled = 1;
    ctx->bounds_checks_enabled = 1;
    ctx->uninit_checks_enabled = 1;
    ctx->errors = NULL;
    ctx->error_count = 0;
    ctx->error_capacity = 0;
    ctx->current_function = NULL;
    ctx->function_depth = 0;
    ctx->function_capacity = 0;
}

void safety_free(SafetyContext *ctx) {
    free(ctx->errors);
    free(ctx->current_function);
}

void safety_report_error(SafetyContext *ctx, SafetyErrorType type, int line, int col,
                         const char *msg, const char *details) {
    if (ctx->error_count >= ctx->error_capacity) {
        int newcap = ctx->error_capacity ? ctx->error_capacity * 2 : 64;
        ctx->errors = (SafetyError*)realloc(ctx->errors, newcap * sizeof(SafetyError));
        ctx->error_capacity = newcap;
    }
    SafetyError *err = &ctx->errors[ctx->error_count++];
    err->type = type;
    err->line = line;
    err->col = col;
    strcpy_safe(err->message, msg);
    if (details) strcpy_safe(err->details, details);
    else err->details[0] = '\0';

    const char *prefix = "[SAFETY]";
    fprintf(stderr, "%s Warning [%d:%d]: %s", prefix, line, col, msg);
    if (details && details[0]) fprintf(stderr, " | %s", details);
    fprintf(stderr, "\n");
}

int is_pointer_type(TypeDesc *t) {
    return t && t->is_pointer > 0;
}

int is_array_type(TypeDesc *t) {
    return t && t->is_array;
}

int type_can_be_null(TypeDesc *t) {
    if (!t) return 1;
    if (t->is_pointer) return 1;
    if (t->base_type == TOKEN_IDENTIFIER) return 1;
    return 0;
}

int type_compatible(TypeDesc *a, TypeDesc *b) {
    if (!a || !b) return 0;
    if (a->base_type == b->base_type) return 1;
    if (a->is_pointer && b->is_pointer) return 1;
    if (a->is_pointer && b->base_type == TOKEN_IDENTIFIER) return 1;
    if (b->is_pointer && a->base_type == TOKEN_IDENTIFIER) return 1;
    if (is_array_type(a) && is_pointer_type(b)) return 1;
    if (is_pointer_type(a) && is_array_type(b)) return 1;
    return 0;
}

static VariableTracker* find_var(LocalVarContext *lvc, const char *name) {
    for (int i = 0; i < lvc->var_count; i++) {
        if (strcmp(lvc->vars[i].name, name) == 0) return &lvc->vars[i];
    }
    return NULL;
}

static void add_var(LocalVarContext *lvc, const char *name, TypeDesc *type, int line, int is_param) {
    if (lvc->var_count >= lvc->var_capacity) {
        int newcap = lvc->var_capacity ? lvc->var_capacity * 2 : 64;
        lvc->vars = (VariableTracker*)realloc(lvc->vars, newcap * sizeof(VariableTracker));
        lvc->var_capacity = newcap;
    }
    VariableTracker *v = &lvc->vars[lvc->var_count++];
    strcpy_safe(v->name, name);
    v->type = type;
    v->initialized = is_param;
    v->may_be_null = type_can_be_null(type);
    v->address_taken = 0;
    v->line_decl = line;
    v->last_use_line = line;
    v->is_freed = 0;
    v->is_param = is_param;
}

static void mark_initialized(VariableTracker *v) {
    if (v) v->initialized = 1;
}

static void mark_used(VariableTracker *v, int line) {
    if (v) v->last_use_line = line;
}

static void mark_freed(VariableTracker *v) {
    if (v) v->is_freed = 1;
}

int safety_check_expression(SafetyContext *ctx, ASTNode *expr, LocalVarContext *lvc) {
    if (!expr) return 0;

    switch (expr->type) {
        case AST_IDENT: {
            VariableTracker *v = find_var(lvc, expr->d.ident.name);
            if (!v) {
                safety_report_error(ctx, SAFETY_ERR_UNINIT_VAR, expr->line, expr->col,
                    "Use of undeclared variable", expr->d.ident.name);
                return 1;
            }
            if (ctx->uninit_checks_enabled && !v->initialized) {
                safety_report_error(ctx, SAFETY_ERR_UNINIT_VAR, expr->line, expr->col,
                    "Use of uninitialized variable", v->name);
                return 1;
            }
            mark_used(v, expr->line);
            break;
        }

        case AST_BINARY: {
            ASTOp op = expr->d.binary.op;
            if (op == BIN_DIV || op == BIN_MOD) {
                if (expr->d.binary.right->type == AST_INT_LIT && expr->d.binary.right->d.int_lit.value == 0) {
                    safety_report_error(ctx, SAFETY_ERR_DIVISION_BY_ZERO, expr->line, expr->col,
                        "Division by constant zero", "Division by zero detected");
                    return 1;
                }
                if (expr->d.binary.right->type == AST_IDENT) {
                    VariableTracker *v = find_var(lvc, expr->d.binary.right->d.ident.name);
                    if (v && ctx->uninit_checks_enabled && !v->initialized) {
                        safety_report_error(ctx, SAFETY_ERR_DIVISION_BY_ZERO, expr->line, expr->col,
                            "Division by uninitialized variable", v->name);
                        return 1;
                    }
                }
            }
            safety_check_expression(ctx, expr->d.binary.left, lvc);
            safety_check_expression(ctx, expr->d.binary.right, lvc);
            break;
        }

        case AST_UNARY: {
            ASTOp op = expr->d.unary.op;
            if (op == UN_DEREF) {
                safety_check_expression(ctx, expr->d.unary.operand, lvc);
                if (expr->d.unary.operand->type == AST_IDENT) {
                    VariableTracker *v = find_var(lvc, expr->d.unary.operand->d.ident.name);
                    if (v && ctx->null_checks_enabled && v->may_be_null) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Dereferencing pointer '%s'", v->name);
                        safety_report_error(ctx, SAFETY_ERR_NULL_DEREF, expr->line, expr->col,
                            "Potential nullptr dereference", buf);
                    }
                }
            }
            if (op == UN_ADDR) {
                if (expr->d.unary.operand->type == AST_IDENT) {
                    VariableTracker *v = find_var(lvc, expr->d.unary.operand->d.ident.name);
                    if (v) v->address_taken = 1;
                }
            }
            safety_check_expression(ctx, expr->d.unary.operand, lvc);
            break;
        }

        case AST_ASSIGN: {
            safety_check_expression(ctx, expr->d.assign.value, lvc);
            if (expr->d.assign.target->type == AST_IDENT) {
                VariableTracker *v = find_var(lvc, expr->d.assign.target->d.ident.name);
                if (v) {
                    mark_initialized(v);
                    if (expr->d.assign.value->type == AST_INT_LIT && expr->d.assign.value->d.int_lit.value == 0) {
                        v->may_be_null = 1;
                    }
                }
            }
            break;
        }

        case AST_ARRAY_SUB: {
            safety_check_expression(ctx, expr->d.array_sub.arr, lvc);
            safety_check_expression(ctx, expr->d.array_sub.index, lvc);
            if (expr->d.array_sub.index->type == AST_INT_LIT && expr->d.array_sub.index->d.int_lit.value < 0) {
                safety_report_error(ctx, SAFETY_ERR_ARRAY_BOUNDS, expr->line, expr->col,
                    "Negative array index", "Index cannot be negative");
                return 1;
            }
            break;
        }

        case AST_MEMBER: {
            safety_check_expression(ctx, expr->d.member.obj, lvc);
            if (expr->d.member.arrow) {
                if (expr->d.member.obj->type == AST_IDENT) {
                    VariableTracker *v = find_var(lvc, expr->d.member.obj->d.ident.name);
                    if (v && ctx->null_checks_enabled && v->may_be_null) {
                        safety_report_error(ctx, SAFETY_ERR_NULL_DEREF, expr->line, expr->col,
                            "Field access through nullptr", v->name);
                        return 1;
                    }
                }
            }
            break;
        }

        case AST_NEW: {
            break;
        }

        case AST_DELETE: {
            safety_check_expression(ctx, expr->d.delete_expr.expr, lvc);
            if (expr->d.delete_expr.expr->type == AST_IDENT) {
                VariableTracker *v = find_var(lvc, expr->d.delete_expr.expr->d.ident.name);
                if (v) {
                    if (v->is_freed) {
                        safety_report_error(ctx, SAFETY_ERR_USE_AFTER_FREE, expr->line, expr->col,
                            "Double free detected", v->name);
                        return 1;
                    }
                    mark_freed(v);
                }
            }
            break;
        }

        case AST_CALL: {
            for (ASTNode *a = expr->d.call.args; a; a = a->next) {
                safety_check_expression(ctx, a, lvc);
            }
            break;
        }

        default:
            break;
    }
    return 0;
}

int safety_check_statement(SafetyContext *ctx, ASTNode *stmt, LocalVarContext *lvc) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case AST_BLOCK: {
            int prev_scope = lvc->scope_depth;
            lvc->scope_depth++;
            for (ASTNode *s = stmt->d.block.stmts; s; s = s->next) {
                safety_check_statement(ctx, s, lvc);
            }
            for (int i = lvc->var_count - 1; i >= 0; i--) {
                VariableTracker *v = &lvc->vars[i];
                if (v->type && !v->initialized && !v->is_param && ctx->uninit_checks_enabled) {
                    if (v->last_use_line < stmt->line) {
                    }
                }
            }
            lvc->scope_depth = prev_scope;
            break;
        }

        case AST_VAR_DECL: {
            add_var(lvc, stmt->d.var_decl.name, stmt->d.var_decl.type, stmt->line, 0);
            if (stmt->d.var_decl.init) {
                safety_check_expression(ctx, stmt->d.var_decl.init, lvc);
                VariableTracker *v = find_var(lvc, stmt->d.var_decl.name);
                if (v) mark_initialized(v);
            }
            break;
        }

        case AST_IF: {
            safety_check_expression(ctx, stmt->d.if_stmt.cond, lvc);
            safety_check_statement(ctx, stmt->d.if_stmt.then, lvc);
            if (stmt->d.if_stmt.els) {
                safety_check_statement(ctx, stmt->d.if_stmt.els, lvc);
            }
            break;
        }

        case AST_WHILE:
        case AST_DO_WHILE: {
            int prev_loop = lvc->in_loop;
            lvc->in_loop = 1;
            safety_check_expression(ctx, stmt->d.loop.cond, lvc);
            safety_check_statement(ctx, stmt->d.loop.body, lvc);
            lvc->in_loop = prev_loop;
            break;
        }

        case AST_FOR: {
            int prev_loop = lvc->in_loop;
            lvc->in_loop = 1;
            if (stmt->d.for_stmt.init) safety_check_expression(ctx, stmt->d.for_stmt.init, lvc);
            if (stmt->d.for_stmt.cond) safety_check_expression(ctx, stmt->d.for_stmt.cond, lvc);
            safety_check_statement(ctx, stmt->d.for_stmt.body, lvc);
            if (stmt->d.for_stmt.incr) safety_check_expression(ctx, stmt->d.for_stmt.incr, lvc);
            lvc->in_loop = prev_loop;
            break;
        }

        case AST_RETURN: {
            if (stmt->d.ret.expr) {
                safety_check_expression(ctx, stmt->d.ret.expr, lvc);
            }
            break;
        }

        case AST_BREAK: {
            if (!lvc->in_loop && !ctx->in_switch) {
                safety_report_error(ctx, SAFETY_ERR_DEAD_CODE, stmt->line, stmt->col,
                    "break outside loop or switch", NULL);
                return 1;
            }
            break;
        }

        case AST_CONTINUE: {
            if (!lvc->in_loop) {
                safety_report_error(ctx, SAFETY_ERR_DEAD_CODE, stmt->line, stmt->col,
                    "continue outside loop", NULL);
                return 1;
            }
            break;
        }

        case AST_SWITCH: {
            safety_check_expression(ctx, stmt->d.switch_stmt.cond, lvc);
            int prev_switch = ctx->in_switch;
            ctx->in_switch = 1;
            for (ASTNode *c = stmt->d.switch_stmt.cases; c; c = c->next) {
                safety_check_statement(ctx, c, lvc);
            }
            ctx->in_switch = prev_switch;
            break;
        }

        case AST_EXPR_STMT: {
            safety_check_expression(ctx, stmt->d.binary.left, lvc);
            break;
        }

        default:
            break;
    }
    return 0;
}

int safety_check_function(SafetyContext *ctx, ASTNode *func) {
    if (!func || func->type != AST_FUNC_DEF) return 0;

    LocalVarContext lvc;
    memset(&lvc, 0, sizeof(lvc));
    lvc.scope_depth = 0;
    lvc.in_loop = 0;

    if (ctx->function_depth >= ctx->function_capacity) {
        int newcap = ctx->function_capacity ? ctx->function_capacity * 2 : 16;
        ctx->current_function = (int*)realloc(ctx->current_function, newcap * sizeof(int));
        ctx->function_capacity = newcap;
    }
    ctx->current_function[ctx->function_depth]++;

    for (ASTNode *p = func->d.func_def.params; p; p = p->next) {
        if (p->type == AST_VAR_DECL) {
            add_var(&lvc, p->d.var_decl.name, p->d.var_decl.type, p->line, 1);
        }
    }

    if (func->d.func_def.body) {
        safety_check_statement(ctx, func->d.func_def.body, &lvc);
    }

    ctx->function_depth--;
    return 0;
}

int safety_check_program(SafetyContext *ctx, ASTNode *program) {
    if (!program || program->type != AST_PROGRAM) return 0;

    for (int i = 0; i < program->d.program.count; i++) {
        ASTNode *node = program->d.program.nodes[i];
        if (node->type == AST_FUNC_DEF) {
            safety_check_function(ctx, node);
        } else if (node->type == AST_CLASS_DEF || node->type == AST_STRUCT_DEF) {
            for (ASTNode *m = node->d.struct_def.members; m; m = m->next) {
                if (m->type == AST_FUNC_DEF) {
                    safety_check_function(ctx, m);
                }
            }
        }
    }
    return ctx->error_count;
}