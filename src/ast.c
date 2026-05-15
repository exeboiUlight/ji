#include "../include/ast.h"
#include <stdlib.h>
#include <string.h>

ASTNode* ast_alloc(ASTNodeType type) {
    ASTNode *n = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!n) return NULL;
    n->type = type;
    return n;
}

TypeDesc* type_alloc(TokenKind base) {
    TypeDesc *t = (TypeDesc*)calloc(1, sizeof(TypeDesc));
    if (!t) return NULL;
    t->base_type = base;
    return t;
}

void type_free(TypeDesc *t) {
    if (!t) return;
    type_free(t->next);
    free(t);
}

static void free_list(ASTNode *n) {
    while (n) { ASTNode *next = n->next; ast_free(n); n = next; }
}

void ast_free(ASTNode *n) {
    if (!n) return;
    switch (n->type) {
        case AST_PROGRAM:
            for (int i = 0; i < n->d.program.count; i++) ast_free(n->d.program.nodes[i]);
            free(n->d.program.nodes);
            break;
        case AST_FUNC_DEF:
            type_free(n->d.func_def.ret_type);
            free_list(n->d.func_def.params);
            ast_free(n->d.func_def.body);
            break;
        case AST_FUNC_DECL:
            type_free(n->d.func_decl.ret_type);
            free_list(n->d.func_decl.params);
            break;
        case AST_BLOCK:          free_list(n->d.block.stmts); break;
        case AST_VAR_DECL:       type_free(n->d.var_decl.type); ast_free(n->d.var_decl.init); break;
        case AST_IF:             ast_free(n->d.if_stmt.cond); ast_free(n->d.if_stmt.then); ast_free(n->d.if_stmt.els); break;
        case AST_WHILE: case AST_DO_WHILE:
                                 ast_free(n->d.loop.cond); ast_free(n->d.loop.body); break;
        case AST_FOR:            ast_free(n->d.for_stmt.init); ast_free(n->d.for_stmt.cond);
                                 ast_free(n->d.for_stmt.incr); ast_free(n->d.for_stmt.body); break;
        case AST_SWITCH:         ast_free(n->d.switch_stmt.cond); free_list(n->d.switch_stmt.cases); break;
        case AST_CASE:           free_list(n->d.case_stmt.stmts); break;
        case AST_DEFAULT:        free_list(n->d.case_stmt.stmts); break;
        case AST_RETURN:         ast_free(n->d.ret.expr); break;
        case AST_EXPR_STMT:      ast_free(n->d.binary.left); break;
        case AST_STRUCT_DEF: case AST_CLASS_DEF:
                                 free_list(n->d.struct_def.members); break;
        case AST_BINARY:         ast_free(n->d.binary.left); ast_free(n->d.binary.right); break;
        case AST_UNARY:          ast_free(n->d.unary.operand); break;
        case AST_ASSIGN:         ast_free(n->d.assign.target); ast_free(n->d.assign.value); break;
        case AST_CALL:           ast_free(n->d.call.func); free_list(n->d.call.args); break;
        case AST_TERNARY:        ast_free(n->d.ternary.cond); ast_free(n->d.ternary.then); ast_free(n->d.ternary.els); break;
        case AST_MEMBER:         ast_free(n->d.member.obj); break;
        case AST_ARRAY_SUB:      ast_free(n->d.array_sub.arr); ast_free(n->d.array_sub.index); break;
        case AST_CAST:           type_free(n->d.cast.type); ast_free(n->d.cast.expr); break;
        case AST_SIZEOF:         ast_free(n->d.sizeof_expr.expr); break;
        case AST_NEW:            free_list(n->d.new_expr.args); break;
        case AST_DELETE:         ast_free(n->d.delete_expr.expr); break;
        case AST_IMPORT:
            for (int i = 0; i < n->d.import.count; i++)
                ast_free(n->d.import.imported_nodes[i]);
            free(n->d.import.imported_nodes);
            break;
        default: break;
    }
    free(n);
}
