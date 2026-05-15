#include "../include/parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void parser_init(Parser *p, Lexer *lex, TokenRegistry *reg) {
    p->lexer = lex; p->registry = reg;
    p->error_count = 0;
    p->global_scope.count = 0; p->global_scope.capacity = 0;
    p->global_scope.syms = NULL; p->global_scope.parent = NULL;
    p->global_scope.is_class = 0;
    p->current_scope = &p->global_scope;
    p->class_count = 0; p->class_capacity = 0; p->class_info = NULL;
    p->string_count = 0; p->string_capacity = 0; p->strings = NULL;
    p->imported_count = 0; p->imported_capacity = 0; p->imported_files = NULL;
    p->source_dir[0] = '\0';
    p->root_dir[0] = '\0';
}

void parser_error(Parser *p, const char *msg) {
    Token t = lexer_peek(p->lexer);
    fprintf(stderr, "Error [%d:%d]: %s\n", t.line, t.column, msg);
    p->error_count++;
}

/* === FORWARD DECLARATIONS (исправляют "implicit declaration") === */
static ASTNode* parse_expression(Parser *p);
static ASTNode* parse_assignment(Parser *p);
static ASTNode* parse_unary(Parser *p);
static ASTNode* parse_statement(Parser *p);
static ASTNode* parse_struct_or_class(Parser *p, int is_class);
static TypeDesc* parse_type(Parser *p, int allow_void);
static ASTNode* parse_block(Parser *p);
static ASTNode* parse_primary(Parser *p);
static ASTNode* parse_postfix(Parser *p);
static Token lexer_peek_n(Lexer *lex, int n);

/* Scope management */
static void push_scope(Parser *p) {
    Scope *s = (Scope*)calloc(1, sizeof(Scope));
    s->parent = p->current_scope;
    s->syms = NULL; s->count = 0; s->capacity = 0;
    p->current_scope = s;
}
static void pop_scope(Parser *p) {
    Scope *s = p->current_scope;
    p->current_scope = s->parent;
    free(s->syms);
    free(s);
}

static Symbol* find_symbol(Parser *p, const char *name) {
    for (Scope *s = p->current_scope; s; s = s->parent)
        for (int i = 0; i < s->count; i++)
            if (strcmp(s->syms[i].name, name) == 0) return &s->syms[i];
    return NULL;
}

static Symbol* add_symbol(Parser *p, const char *name, TypeDesc *type, int is_param) {
    Scope *s = p->current_scope;
    if (s->count >= s->capacity) {
        int newcap = s->capacity ? s->capacity * 2 : 64;
        s->syms = (Symbol*)realloc(s->syms, newcap * sizeof(Symbol));
        s->capacity = newcap;
    }
    Symbol *sym = &s->syms[s->count++];
    strcpy_safe(sym->name, name);
    sym->type = type;
    sym->is_param = is_param;
    return sym;
}

/* String pool */
int parser_get_string_count(Parser *p) { return p->string_count; }
const char* parser_get_string(Parser *p, int i) { return p->strings[i].value; }
int parser_get_string_label(Parser *p, int i) { return p->strings[i].label_id; }

static int add_string(Parser *p, const char *val) {
    for (int i = 0; i < p->string_count; i++)
        if (strcmp(p->strings[i].value, val) == 0) return i;
    if (p->string_count >= p->string_capacity) {
        int newcap = p->string_capacity ? p->string_capacity * 2 : 64;
        p->strings = (StringEntry*)realloc(p->strings, newcap * sizeof(StringEntry));
        p->string_capacity = newcap;
    }
    int id = p->string_count++;
    strcpy_safe(p->strings[id].value, val);
    p->strings[id].label_id = id;
    return id;
}

static int token_is(Parser *p, const char *name) {
    Token t = lexer_peek(p->lexer);
    if (t.type >= TOKEN_KEYWORD_FIRST) {
        const char *tn = token_type_name(p->registry, t.type);
        return strcmp(tn, name) == 0;
    }
    return 0;
}

/* === IMPORT RESOLUTION === */
static int is_already_imported(Parser *p, const char *filename) {
    for (int i = 0; i < p->imported_count; i++)
        if (strcmp(p->imported_files[i], filename) == 0) return 1;
    return 0;
}

static void resolve_import_path(Parser *p, const char *rel, char *out, int size) {
    /* Try root_dir first (Python-style: import is always from root) */
    if (p->root_dir[0] != '\0') {
        snprintf(out, size, "%s/%s", p->root_dir, rel);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); return; }
    }
    /* Fall back to source_dir (relative to the source file) */
    snprintf(out, size, "%s/%s", p->source_dir, rel);
    FILE *f = fopen(out, "rb");
    if (f) { fclose(f); return; }
    /* Try raw path as last resort */
    snprintf(out, size, "%s", rel);
    f = fopen(out, "rb");
    if (f) { fclose(f); return; }
    parser_error(p, "Import file not found");
    out[0] = '\0';
}

static ASTNode* parse_import_statement(Parser *p) {
    lexer_advance(p->lexer); /* skip 'import' */
    Token t = lexer_peek(p->lexer);

    char rel_path[512];
    if (t.type == TOKEN_STRING_LITERAL) {
        lexer_advance(p->lexer);
        strcpy_safe(rel_path, t.lexeme);
    } else if (t.type == TOKEN_IDENTIFIER) {
        char dotted[512] = {0};
        while (1) {
            strncat(dotted, t.lexeme, sizeof(dotted)-strlen(dotted)-1);
            lexer_advance(p->lexer);
            if (lexer_match(p->lexer, TOKEN_DOT)) {
                strncat(dotted, "/", sizeof(dotted)-strlen(dotted)-1);
                t = lexer_peek(p->lexer);
                if (t.type != TOKEN_IDENTIFIER) {
                    parser_error(p, "Expected identifier after dot");
                    return NULL;
                }
            } else break;
        }
        snprintf(rel_path, sizeof(rel_path), "%s.ji", dotted);
    } else {
        parser_error(p, "Expected string literal or identifier after import");
        return NULL;
    }

    if (!lexer_match(p->lexer, TOKEN_SEMICOLON))
        parser_error(p, "Expected ';' after import");

    char full_path[1024];
    resolve_import_path(p, rel_path, full_path, sizeof(full_path));
    if (full_path[0] == '\0') return NULL;

    if (is_already_imported(p, full_path)) {
        ASTNode *dummy = ast_alloc(AST_IMPORT);
        dummy->d.import.filename[0] = '\0';
        dummy->d.import.imported_nodes = NULL;
        dummy->d.import.count = 0;
        return dummy;
    }

    FILE *f = fopen(full_path, "rb");
    if (!f) {
        parser_error(p, "Cannot open import file");
        char err[1100]; snprintf(err, sizeof(err), "Could not open '%s'", full_path);
        parser_error(p, err);
        return NULL;
    }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); parser_error(p, "Out of memory loading import"); return NULL; }
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);

    if (p->imported_count >= p->imported_capacity) {
        int newcap = p->imported_capacity ? p->imported_capacity * 2 : 16;
        p->imported_files = (char**)realloc(p->imported_files, newcap * sizeof(char*));
        p->imported_capacity = newcap;
    }
    p->imported_files[p->imported_count] = (char*)malloc(strlen(full_path) + 1);
    strcpy(p->imported_files[p->imported_count], full_path);
    p->imported_count++;

    Lexer sub_lex;
    lexer_init(&sub_lex, p->registry, buf);

    Parser sub_parser;
    parser_init(&sub_parser, &sub_lex, p->registry);
    sub_parser.class_count = p->class_count;
    sub_parser.class_capacity = p->class_capacity;
    sub_parser.class_info = (ClassInfo*)realloc(NULL, sub_parser.class_capacity * sizeof(ClassInfo));
    for (int i = 0; i < p->class_count; i++)
        sub_parser.class_info[i] = p->class_info[i];
    strcpy(sub_parser.source_dir, p->source_dir);
    strcpy(sub_parser.root_dir, p->root_dir);

    ASTNode *imported_prog = parser_parse(&sub_parser);
    if (imported_prog && imported_prog->type == AST_PROGRAM) {
        ASTNode *node = ast_alloc(AST_IMPORT);
        strcpy_safe(node->d.import.filename, full_path);
        node->d.import.count = imported_prog->d.program.count;
        node->d.import.imported_nodes = imported_prog->d.program.nodes;
        imported_prog->d.program.nodes = NULL;
        imported_prog->d.program.count = 0;
        ast_free(imported_prog);

        p->class_count = sub_parser.class_count;
        p->class_capacity = sub_parser.class_capacity;
        free(p->class_info);
        p->class_info = sub_parser.class_info;
        sub_parser.class_info = NULL;
        sub_parser.class_count = 0;
        sub_parser.class_capacity = 0;

        free(buf);
        parser_free(&sub_parser);
        return node;
    }

    ast_free(imported_prog);
    p->class_count = sub_parser.class_count;
    p->class_capacity = sub_parser.class_capacity;
    free(p->class_info);
    p->class_info = sub_parser.class_info;
    sub_parser.class_info = NULL;
    sub_parser.class_count = 0;
    sub_parser.class_capacity = 0;
    free(buf);
    parser_free(&sub_parser);
    return NULL;
}

/* ========= TYPE PARSING ========= */
static TypeDesc* parse_type(Parser *p, int allow_void) {
    Token t = lexer_peek(p->lexer);

    /* Skip type qualifiers */
    if (token_is(p, "const") || token_is(p, "static")) {
        lexer_advance(p->lexer);
        return parse_type(p, allow_void);
    }

    if (t.type == TOKEN_IDENTIFIER) {
        for (int i = 0; i < p->class_count; i++)
            if (strcmp(p->class_info[i].name, t.lexeme) == 0) {
                lexer_advance(p->lexer);
                TypeDesc *td = type_alloc(TOKEN_IDENTIFIER);
                strcpy_safe(td->type_name, t.lexeme);
                while (lexer_match(p->lexer, TOKEN_STAR)) td->is_pointer++;
                return td;
            }
        parser_error(p, "Unknown type"); return NULL;
    }

    const char *tn = token_type_name(p->registry, t.type);
    if (strcmp(tn, "int") == 0 || strcmp(tn, "char") == 0 ||
        strcmp(tn, "float") == 0 || strcmp(tn, "double") == 0 ||
        strcmp(tn, "void") == 0) {
        if (strcmp(tn, "void") == 0 && !allow_void) {
            Token next = lexer_peek_n(p->lexer, 1);
            if (next.type != TOKEN_STAR) {
                parser_error(p, "void not allowed here");
                return NULL;
            }
        }
        lexer_advance(p->lexer);
        TypeDesc *td = type_alloc(t.type);
        while (lexer_match(p->lexer, TOKEN_STAR)) td->is_pointer++;
        return td;
    }
    parser_error(p, "Expected type"); return NULL;
}

/* ========= EXPRESSION PARSING ========= */
static ASTNode* parse_primary(Parser *p) {
    Token t = lexer_peek(p->lexer);

    if (t.type == TOKEN_NUMBER) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_INT_LIT);
        n->d.int_lit.value = t.int_value;
        return n;
    }
    if (t.type == TOKEN_STRING_LITERAL) {
        lexer_advance(p->lexer);
        add_string(p, t.lexeme);
        ASTNode *n = ast_alloc(AST_STR_LIT);
        strcpy_safe(n->d.str_lit.value, t.lexeme);
        return n;
    }
    if (t.type == TOKEN_CHAR_LITERAL) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_CHAR_LIT);
        n->d.char_lit.value = t.char_value;
        return n;
    }
    if (t.type == TOKEN_LPAREN) {
        int save_pos = p->lexer->pos, save_line = p->lexer->line, save_col = p->lexer->column;
        int save_has = p->lexer->has_current;
        Token save_cur = p->lexer->current;
        int save_errors = p->error_count;

        lexer_advance(p->lexer);
        Token next = lexer_peek(p->lexer);
        int could_be_cast = 0;
        const char *nname = token_type_name(p->registry, next.type);
        if (strcmp(nname, "int")==0||strcmp(nname,"char")==0||strcmp(nname,"float")==0||
            strcmp(nname,"double")==0||strcmp(nname,"void")==0||
            strcmp(nname,"const")==0||strcmp(nname,"static")==0) {
            could_be_cast = 1;
        }
        if (next.type == TOKEN_IDENTIFIER) {
            for (int i = 0; i < p->class_count; i++)
                if (strcmp(p->class_info[i].name, next.lexeme) == 0) { could_be_cast = 1; break; }
        }

        if (could_be_cast) {
            TypeDesc *cast_type = parse_type(p, 1);
            if (cast_type && lexer_match(p->lexer, TOKEN_RPAREN)) {
                ASTNode *cn = ast_alloc(AST_CAST);
                cn->d.cast.type = cast_type;
                cn->d.cast.expr = parse_unary(p);
                return cn;
            }
            if (cast_type) type_free(cast_type);
        }

        p->lexer->pos = save_pos; p->lexer->line = save_line; p->lexer->column = save_col;
        p->lexer->has_current = save_has; p->lexer->current = save_cur;
        p->error_count = save_errors;

        lexer_advance(p->lexer);
        ASTNode *n = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
        return n;
    }
    if (t.type == TOKEN_IDENTIFIER) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_IDENT);
        strcpy_safe(n->d.ident.name, t.lexeme);
        return n;
    }
    if (token_is(p, "this")) {
        lexer_advance(p->lexer);
        return ast_alloc(AST_THIS);
    }
    if (token_is(p, "null") || token_is(p, "NULL")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_INT_LIT);
        n->d.int_lit.value = 0;
        return n;
    }
    if (token_is(p, "new")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_NEW);
        Token cn = lexer_peek(p->lexer);
        if (cn.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected class name after new"); return n; }
        strcpy_safe(n->d.new_expr.class_name, cn.lexeme);
        lexer_advance(p->lexer);
        if (lexer_match(p->lexer, TOKEN_LPAREN)) {
            ASTNode *args = NULL; ASTNode **tail = &args;
            if (lexer_peek(p->lexer).type != TOKEN_RPAREN) {
                do {
                    *tail = parse_expression(p); tail = &(*tail)->next;
                } while (lexer_match(p->lexer, TOKEN_COMMA));
            }
            n->d.new_expr.args = args;
            if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
        }
        return n;
    }
    if (token_is(p, "delete")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_DELETE);
        n->d.delete_expr.expr = parse_expression(p);
        return n;
    }
    if (token_is(p, "sizeof")) {
        lexer_advance(p->lexer);
        if (lexer_match(p->lexer, TOKEN_LPAREN)) {
            ASTNode *n = ast_alloc(AST_SIZEOF);
            n->d.sizeof_expr.expr = parse_expression(p);
            if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
            return n;
        }
    }

    parser_error(p, "Expected expression");
    return NULL;
}

static ASTNode* parse_postfix(Parser *p) {
    ASTNode *n = parse_primary(p);
    if (!n) return NULL;

    while (1) {
        Token t = lexer_peek(p->lexer);
        if (t.type == TOKEN_LPAREN) {
            lexer_advance(p->lexer);
            ASTNode *call = ast_alloc(AST_CALL);
            call->d.call.func = n;
            call->d.call.args = NULL;
            call->d.call.arg_count = 0;
            if (lexer_peek(p->lexer).type != TOKEN_RPAREN) {
                ASTNode **tail = &call->d.call.args;
                call->d.call.arg_count = 0;
                do {
                    *tail = parse_assignment(p); tail = &(*tail)->next;
                    call->d.call.arg_count++;
                } while (lexer_match(p->lexer, TOKEN_COMMA));
            }
            if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
            n = call;
        } else if (t.type == TOKEN_LBRACKET) {
            lexer_advance(p->lexer);
            ASTNode *sub = ast_alloc(AST_ARRAY_SUB);
            sub->d.array_sub.arr = n;
            sub->d.array_sub.index = parse_expression(p);
            if (!lexer_match(p->lexer, TOKEN_RBRACKET)) parser_error(p, "Expected ']'");
            n = sub;
        } else if (t.type == TOKEN_DOT || t.type == TOKEN_ARROW) {
            lexer_advance(p->lexer);
            Token m = lexer_peek(p->lexer);
            if (m.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected member name"); break; }
            lexer_advance(p->lexer);

            if (lexer_peek(p->lexer).type == TOKEN_LPAREN) {
                char class_name[MAX_IDENT_LEN] = "";
                if (n && n->type == AST_IDENT) {
                    Symbol *sym = find_symbol(p, n->d.ident.name);
                    if (sym && sym->type && sym->type->base_type >= TOKEN_IDENTIFIER) {
                        if (sym->type->base_type == TOKEN_IDENTIFIER || strcmp(token_type_name(p->registry, sym->type->base_type), "class") == 0)
                            strcpy_safe(class_name, sym->type->type_name);
                    }
                }

                if (class_name[0]) {
                    lexer_advance(p->lexer);
                    ASTNode *call = ast_alloc(AST_CALL);
                    ASTNode *func = ast_alloc(AST_IDENT);
                    char _tmp[256]; snprintf(_tmp, sizeof(_tmp), "%s__%s", class_name, m.lexeme); strcpy_safe(func->d.ident.name, _tmp);
                    call->d.call.func = func;
                    call->d.call.args = NULL;
                    call->d.call.arg_count = 0;

                    ASTNode *this_arg = ast_alloc(AST_THIS);
                    call->d.call.args = this_arg;
                    call->d.call.arg_count = 1;

                    ASTNode **tail = &this_arg->next;
                    if (lexer_peek(p->lexer).type != TOKEN_RPAREN) {
                        do {
                            *tail = parse_assignment(p);
                            tail = &(*tail)->next;
                            call->d.call.arg_count++;
                        } while (lexer_match(p->lexer, TOKEN_COMMA));
                    }
                    if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
                    n = call;
                } else {
                    ASTNode *mem = ast_alloc(AST_MEMBER);
                    mem->d.member.obj = n;
                    strcpy_safe(mem->d.member.member, m.lexeme);
                    mem->d.member.arrow = (t.type == TOKEN_ARROW);
                    mem->d.member.member_offset = -1;
                    n = mem;
                }
            } else {
                int member_offset = -1;
                char class_name[MAX_IDENT_LEN] = "";
                if (n && n->type == AST_IDENT) {
                    Symbol *sym = find_symbol(p, n->d.ident.name);
                    if (sym && sym->type && sym->type->base_type == TOKEN_IDENTIFIER) {
                        strcpy_safe(class_name, sym->type->type_name);
                    }
                }
                if (class_name[0]) {
                    for (int ci = 0; ci < p->class_count; ci++) {
                        if (strcmp(p->class_info[ci].name, class_name) == 0) {
                            for (int mi = 0; mi < p->class_info[ci].member_count; mi++) {
                                if (strcmp(p->class_info[ci].members[mi].name, m.lexeme) == 0) {
                                    member_offset = p->class_info[ci].members[mi].offset;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
                ASTNode *mem = ast_alloc(AST_MEMBER);
                mem->d.member.obj = n;
                strcpy_safe(mem->d.member.member, m.lexeme);
                mem->d.member.arrow = (t.type == TOKEN_ARROW);
                mem->d.member.member_offset = member_offset;
                n = mem;
            }
        } else if (t.type == TOKEN_PLUS_PLUS) {
            lexer_advance(p->lexer);
            ASTNode *un = ast_alloc(AST_UNARY);
            un->d.unary.op = UN_POST_INC;
            un->d.unary.operand = n;
            n = un;
        } else if (t.type == TOKEN_MINUS_MINUS) {
            lexer_advance(p->lexer);
            ASTNode *un = ast_alloc(AST_UNARY);
            un->d.unary.op = UN_POST_DEC;
            un->d.unary.operand = n;
            n = un;
        } else break;
    }
    return n;
}

static ASTNode* parse_unary(Parser *p) {
    Token t = lexer_peek(p->lexer);
    ASTOp op;
    int is_unary = 1;

    if (t.type == TOKEN_PLUS) { op = UN_PLUS; }
    else if (t.type == TOKEN_MINUS) { op = UN_NEG; }
    else if (t.type == TOKEN_NOT) { op = UN_NOT; }
    else if (t.type == TOKEN_TILDE) { op = UN_BIT_NOT; }
    else if (t.type == TOKEN_AND) { op = UN_ADDR; }
    else if (t.type == TOKEN_STAR) { op = UN_DEREF; }
    else if (t.type == TOKEN_PLUS_PLUS) { op = UN_PRE_INC; }
    else if (t.type == TOKEN_MINUS_MINUS) { op = UN_PRE_DEC; }
    else { is_unary = 0; }

    if (is_unary) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_UNARY);
        n->d.unary.op = op;
        n->d.unary.operand = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

static ASTNode* parse_binary(Parser *p, ASTNode* (*sub)(Parser*), const TokenKind *ops, const ASTOp *ast_ops, int count) {
    ASTNode *n = sub(p); if (!n) return NULL;
    while (1) {
        TokenKind tt = lexer_peek(p->lexer).type;
        int found = -1;
        for (int i = 0; i < count; i++) if (tt == ops[i]) { found = i; break; }
        if (found < 0) break;
        lexer_advance(p->lexer);
        ASTNode *bin = ast_alloc(AST_BINARY);
        bin->d.binary.op = ast_ops[found];
        bin->d.binary.left = n;
        bin->d.binary.right = sub(p);
        n = bin;
    }
    return n;
}

static ASTNode* parse_mul(Parser *p) {
    static TokenKind ops[] = { TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT };
    static ASTOp aops[] = { BIN_MUL, BIN_DIV, BIN_MOD };
    return parse_binary(p, parse_unary, ops, aops, 3);
}
static ASTNode* parse_add(Parser *p) {
    static TokenKind ops[] = { TOKEN_PLUS, TOKEN_MINUS };
    static ASTOp aops[] = { BIN_ADD, BIN_SUB };
    return parse_binary(p, parse_mul, ops, aops, 2);
}
static ASTNode* parse_shift(Parser *p) {
    static TokenKind ops[] = { TOKEN_LSHIFT, TOKEN_RSHIFT };
    static ASTOp aops[] = { BIN_LSHIFT, BIN_RSHIFT };
    return parse_binary(p, parse_add, ops, aops, 2);
}
static ASTNode* parse_rel(Parser *p) {
    static TokenKind ops[] = { TOKEN_LT, TOKEN_GT, TOKEN_LT_EQ, TOKEN_GT_EQ };
    static ASTOp aops[] = { BIN_LT, BIN_GT, BIN_LE, BIN_GE };
    return parse_binary(p, parse_shift, ops, aops, 4);
}
static ASTNode* parse_eq(Parser *p) {
    static TokenKind ops[] = { TOKEN_EQ_EQ, TOKEN_NOT_EQ };
    static ASTOp aops[] = { BIN_EQ, BIN_NE };
    return parse_binary(p, parse_rel, ops, aops, 2);
}
static ASTNode* parse_bit_and(Parser *p) {
    static TokenKind ops[] = { TOKEN_AND };
    static ASTOp aops[] = { BIN_BIT_AND };
    return parse_binary(p, parse_eq, ops, aops, 1);
}
static ASTNode* parse_xor(Parser *p) {
    static TokenKind ops[] = { TOKEN_XOR };
    static ASTOp aops[] = { BIN_XOR };
    return parse_binary(p, parse_bit_and, ops, aops, 1);
}
static ASTNode* parse_bit_or(Parser *p) {
    static TokenKind ops[] = { TOKEN_OR };
    static ASTOp aops[] = { BIN_BIT_OR };
    return parse_binary(p, parse_xor, ops, aops, 1);
}
static ASTNode* parse_and(Parser *p) {
    static TokenKind ops[] = { TOKEN_AND_AND };
    static ASTOp aops[] = { BIN_AND };
    return parse_binary(p, parse_bit_or, ops, aops, 1);
}
static ASTNode* parse_or(Parser *p) {
    static TokenKind ops[] = { TOKEN_OR_OR };
    static ASTOp aops[] = { BIN_OR };
    return parse_binary(p, parse_and, ops, aops, 1);
}

static ASTNode* parse_ternary(Parser *p) {
    ASTNode *n = parse_or(p); if (!n) return NULL;
    if (lexer_match(p->lexer, TOKEN_QUESTION)) {
        ASTNode *tern = ast_alloc(AST_TERNARY);
        tern->d.ternary.cond = n;
        tern->d.ternary.then = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_COLON)) parser_error(p, "Expected ':'");
        tern->d.ternary.els = parse_ternary(p);
        return tern;
    }
    return n;
}

static ASTNode* parse_assignment(Parser *p) {
    ASTNode *n = parse_ternary(p); if (!n) return NULL;
    TokenKind t = lexer_peek(p->lexer).type;
    ASTOp op;

    if (t == TOKEN_EQ) op = BIN_ASSIGN;
    else if (t == TOKEN_PLUS_EQ) op = BIN_ADD;
    else if (t == TOKEN_MINUS_EQ) op = BIN_SUB;
    else if (t == TOKEN_STAR_EQ) op = BIN_MUL;
    else if (t == TOKEN_SLASH_EQ) op = BIN_DIV;
    else return n;

    lexer_advance(p->lexer);

    ASTNode *assign = ast_alloc(AST_ASSIGN);
    assign->d.assign.op = op;
    assign->d.assign.target = n;
    assign->d.assign.value = parse_assignment(p);
    return assign;
}

static ASTNode* parse_expression(Parser *p) {
    ASTNode *n = parse_assignment(p); if (!n) return NULL;
    while (lexer_match(p->lexer, TOKEN_COMMA)) {
        ASTNode *c = ast_alloc(AST_COMMA);
        c->d.binary.left = n;
        c->d.binary.right = parse_assignment(p);
        n = c;
    }
    return n;
}

/* ========= STATEMENT PARSING ========= */
static ASTNode* parse_block(Parser *p) {
    ASTNode *block = ast_alloc(AST_BLOCK);
    block->d.block.stmts = NULL;
    if (!lexer_match(p->lexer, TOKEN_LBRACE)) { parser_error(p, "Expected '{'"); return block; }
    push_scope(p);
    ASTNode **tail = &block->d.block.stmts;
    while (lexer_peek(p->lexer).type != TOKEN_RBRACE && lexer_peek(p->lexer).type != TOKEN_EOF) {
        *tail = parse_statement(p);
        if (*tail) tail = &(*tail)->next;
        else lexer_advance(p->lexer);
    }
    pop_scope(p);
    if (!lexer_match(p->lexer, TOKEN_RBRACE)) parser_error(p, "Expected '}'");
    return block;
}

static ASTNode* parse_statement(Parser *p) {
    Token t = lexer_peek(p->lexer);

    if (token_is(p, "struct") || token_is(p, "class")) {
        return parse_struct_or_class(p, token_is(p, "class"));
    }

    {
        int could_be_type = 0;
        const char *tn = token_type_name(p->registry, t.type);
        if (strcmp(tn, "int")==0||strcmp(tn, "char")==0||strcmp(tn, "float")==0||strcmp(tn, "double")==0||strcmp(tn, "void")==0) {
            could_be_type = 1;
        }
        if (t.type == TOKEN_IDENTIFIER) {
            for (int i = 0; i < p->class_count; i++)
                if (strcmp(p->class_info[i].name, t.lexeme) == 0) { could_be_type = 1; break; }
        }
        if (could_be_type) {
            int save_pos = p->lexer->pos;
            int save_line = p->lexer->line;
            int save_col = p->lexer->column;
            int save_has = p->lexer->has_current;
            Token save_cur = p->lexer->current;

            TypeDesc *type = parse_type(p, 0);
            if (type) {
                t = lexer_peek(p->lexer);
                if (t.type == TOKEN_IDENTIFIER || t.type == TOKEN_STAR) {
                    Token name_tok;
                    if (t.type == TOKEN_IDENTIFIER) {
                        name_tok = t; lexer_advance(p->lexer);
                    } else {
                        lexer_advance(p->lexer);
                        name_tok = lexer_peek(p->lexer);
                        if (name_tok.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected var name"); return NULL; }
                        lexer_advance(p->lexer);
                        type->is_pointer++;
                    }

                    ASTNode *decl = ast_alloc(AST_VAR_DECL);
                    decl->d.var_decl.type = type;
                    strcpy_safe(decl->d.var_decl.name, name_tok.lexeme);
                    if (lexer_match(p->lexer, TOKEN_EQ))
                        decl->d.var_decl.init = parse_expression(p);
                    if (type->base_type == TOKEN_IDENTIFIER) {
                        decl->d.var_decl.is_class_instance = 1;
                        strcpy_safe(decl->d.var_decl.class_name, type->type_name);
                    }
                    add_symbol(p, decl->d.var_decl.name, type, 0);
                    if (!lexer_match(p->lexer, TOKEN_SEMICOLON))
                        parser_error(p, "Expected ';' after var decl");
                    return decl;
                }
            }
            p->lexer->pos = save_pos;
            p->lexer->line = save_line;
            p->lexer->column = save_col;
            p->lexer->has_current = save_has;
            p->lexer->current = save_cur;
            type_free(type);
        }
    }

    if (token_is(p, "if")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_IF);
        if (!lexer_match(p->lexer, TOKEN_LPAREN)) parser_error(p, "Expected '(' after if");
        n->d.if_stmt.cond = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
        n->d.if_stmt.then = parse_statement(p);
        if (token_is(p, "else")) { lexer_advance(p->lexer); n->d.if_stmt.els = parse_statement(p); }
        return n;
    }
    if (token_is(p, "while")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_WHILE);
        if (!lexer_match(p->lexer, TOKEN_LPAREN)) parser_error(p, "Expected '('");
        n->d.loop.cond = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
        n->d.loop.body = parse_statement(p);
        return n;
    }
    if (token_is(p, "do")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_DO_WHILE);
        n->d.loop.body = parse_statement(p);
        if (!token_is(p, "while")) parser_error(p, "Expected 'while'");
        lexer_advance(p->lexer);
        if (!lexer_match(p->lexer, TOKEN_LPAREN)) parser_error(p, "Expected '('");
        n->d.loop.cond = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");
        return n;
    }
    if (token_is(p, "for")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_FOR);
        if (!lexer_match(p->lexer, TOKEN_LPAREN)) parser_error(p, "Expected '('");
        push_scope(p);
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON))
            n->d.for_stmt.init = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");
        if (lexer_peek(p->lexer).type != TOKEN_SEMICOLON)
            n->d.for_stmt.cond = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");
        if (lexer_peek(p->lexer).type != TOKEN_RPAREN)
            n->d.for_stmt.incr = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
        n->d.for_stmt.body = parse_statement(p);
        pop_scope(p);
        return n;
    }
    if (token_is(p, "switch")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_SWITCH);
        if (!lexer_match(p->lexer, TOKEN_LPAREN)) parser_error(p, "Expected '('");
        n->d.switch_stmt.cond = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");
        if (!lexer_match(p->lexer, TOKEN_LBRACE)) parser_error(p, "Expected '{'");
        ASTNode **tail = &n->d.switch_stmt.cases;
        while (lexer_peek(p->lexer).type != TOKEN_RBRACE && lexer_peek(p->lexer).type != TOKEN_EOF) {
            if (token_is(p, "case")) {
                lexer_advance(p->lexer);
                ASTNode *c = ast_alloc(AST_CASE);
                Token val = lexer_peek(p->lexer);
                if (val.type != TOKEN_NUMBER) { parser_error(p, "Expected number"); break; }
                c->d.case_stmt.value = val.int_value;
                lexer_advance(p->lexer);
                if (!lexer_match(p->lexer, TOKEN_COLON)) parser_error(p, "Expected ':'");
                ASTNode **st = &c->d.case_stmt.stmts;
                while (lexer_peek(p->lexer).type != TOKEN_RBRACE && !token_is(p, "case") && !token_is(p, "default"))
                    { *st = parse_statement(p); if (*st) st = &(*st)->next; else lexer_advance(p->lexer); }
                *tail = c; tail = &(*tail)->next;
            } else if (token_is(p, "default")) {
                lexer_advance(p->lexer);
                ASTNode *c = ast_alloc(AST_DEFAULT);
                if (!lexer_match(p->lexer, TOKEN_COLON)) parser_error(p, "Expected ':'");
                ASTNode **st = &c->d.case_stmt.stmts;
                while (lexer_peek(p->lexer).type != TOKEN_RBRACE && !token_is(p, "case") && !token_is(p, "default"))
                    { *st = parse_statement(p); if (*st) st = &(*st)->next; else lexer_advance(p->lexer); }
                *tail = c; tail = &(*tail)->next;
            } else break;
        }
        if (!lexer_match(p->lexer, TOKEN_RBRACE)) parser_error(p, "Expected '}'");
        return n;
    }
    if (token_is(p, "break")) { lexer_advance(p->lexer); ASTNode *n = ast_alloc(AST_BREAK); if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'"); return n; }
    if (token_is(p, "continue")) { lexer_advance(p->lexer); ASTNode *n = ast_alloc(AST_CONTINUE); if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'"); return n; }
    if (token_is(p, "return")) {
        lexer_advance(p->lexer);
        ASTNode *n = ast_alloc(AST_RETURN);
        if (lexer_peek(p->lexer).type != TOKEN_SEMICOLON)
            n->d.ret.expr = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");
        return n;
    }

    if (t.type == TOKEN_LBRACE) return parse_block(p);

    ASTNode *expr = parse_expression(p);
    if (expr) {
        ASTNode *stmt = ast_alloc(AST_EXPR_STMT);
        stmt->d.binary.left = expr;
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");
        return stmt;
    }
    return NULL;
}

/* ========= STRUCT / CLASS DEFINITION ========= */
static ASTNode* parse_struct_or_class(Parser *p, int is_class) {
    lexer_advance(p->lexer);
    Token name = lexer_peek(p->lexer);
    if (name.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected name"); return NULL; }
    lexer_advance(p->lexer);

    ASTNode *def = ast_alloc(is_class ? AST_CLASS_DEF : AST_STRUCT_DEF);
    strcpy_safe(def->d.struct_def.name, name.lexeme);
    def->d.struct_def.members = NULL;
    def->d.struct_def.member_count = 0;
    def->d.struct_def.size = 0;

    if (p->class_count >= p->class_capacity) {
        int newcap = p->class_capacity ? p->class_capacity * 2 : 16;
        p->class_info = (ClassInfo*)realloc(p->class_info, newcap * sizeof(ClassInfo));
        p->class_capacity = newcap;
    }
    int ci = p->class_count;
    memset(&p->class_info[ci], 0, sizeof(ClassInfo));
    strcpy_safe(p->class_info[ci].name, name.lexeme);
    p->class_info[ci].size = 0;
    p->class_info[ci].vtable = NULL;
    p->class_info[ci].vtable_count = 0;
    p->class_info[ci].members = NULL;
    p->class_info[ci].member_count = 0;
    p->class_info[ci].member_capacity = 0;
    p->class_count++;

    if (!lexer_match(p->lexer, TOKEN_LBRACE)) { parser_error(p, "Expected '{'"); return def; }

    ASTNode **tail = &def->d.struct_def.members;
    while (lexer_peek(p->lexer).type != TOKEN_RBRACE && lexer_peek(p->lexer).type != TOKEN_EOF) {
        Token t2 = lexer_peek(p->lexer);

        if (token_is(p, "virtual")) {
            lexer_advance(p->lexer);
            TypeDesc *ret_type = parse_type(p, 1);
            if (!ret_type) break;
            Token mn = lexer_peek(p->lexer);
            if (mn.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected method name"); break; }
            lexer_advance(p->lexer);
            if (!lexer_match(p->lexer, TOKEN_LPAREN)) { parser_error(p, "Expected '('"); break; }

            ASTNode *method = ast_alloc(AST_FUNC_DEF);
            method->d.func_def.ret_type = ret_type;
            int _nlen = snprintf(NULL, 0, "%s__%s", p->class_info[ci].name, mn.lexeme);
            char *_tmp = (char*)malloc(_nlen + 1);
            snprintf(_tmp, _nlen + 1, "%s__%s", p->class_info[ci].name, mn.lexeme);
            strcpy_safe(method->d.func_def.name, _tmp); free(_tmp);
            method->d.func_def.is_method = 1;
            strcpy_safe(method->d.func_def.class_name, p->class_info[ci].name);
            method->d.func_def.is_virtual = 1;
            method->d.func_def.vtable_index = p->class_info[ci].vtable_count++;

            ASTNode *params_tail = NULL;
            method->d.func_def.params = NULL;
            method->d.func_def.param_count = 0;
            if (lexer_peek(p->lexer).type != TOKEN_RPAREN) {
                do {
                    TypeDesc *ptype = parse_type(p, 0);
                    if (!ptype) break;
                    Token pn = lexer_peek(p->lexer);
                    if (pn.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected param name"); break; }
                    lexer_advance(p->lexer);
                    ASTNode *pdecl = ast_alloc(AST_VAR_DECL);
                    pdecl->d.var_decl.type = ptype;
                    strcpy_safe(pdecl->d.var_decl.name, pn.lexeme);
                    if (!params_tail) method->d.func_def.params = pdecl;
                    else params_tail->next = pdecl;
                    params_tail = pdecl;
                    method->d.func_def.param_count++;
                } while (lexer_match(p->lexer, TOKEN_COMMA));
            }
            if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");

            method->d.func_def.body = parse_block(p);
            *tail = method; tail = &(*tail)->next;
            def->d.struct_def.member_count++;
            continue;
        }

        if (t2.type == TOKEN_IDENTIFIER || t2.type >= TOKEN_KEYWORD_FIRST) {
            int save_pos = p->lexer->pos;
            int save_line = p->lexer->line;
            int save_col = p->lexer->column;
            int save_has = p->lexer->has_current;
            Token save_cur = p->lexer->current;

            TypeDesc *mt = parse_type(p, 1);
            if (!mt) break;
            Token mn = lexer_peek(p->lexer);
            if (mn.type == TOKEN_IDENTIFIER) {
                lexer_advance(p->lexer);
                if (lexer_peek(p->lexer).type == TOKEN_LPAREN) {
                    lexer_advance(p->lexer);
                    ASTNode *method = ast_alloc(AST_FUNC_DEF);
                    method->d.func_def.ret_type = mt;
                    int _nlen2 = snprintf(NULL, 0, "%s__%s", p->class_info[ci].name, mn.lexeme);
                    char *_tmp2 = (char*)malloc(_nlen2 + 1);
                    snprintf(_tmp2, _nlen2 + 1, "%s__%s", p->class_info[ci].name, mn.lexeme);
                    strcpy_safe(method->d.func_def.name, _tmp2); free(_tmp2);
                    method->d.func_def.is_method = 1;
                    strcpy_safe(method->d.func_def.class_name, p->class_info[ci].name);

                    ASTNode *ptail = NULL;
                    method->d.func_def.params = NULL;
                    method->d.func_def.param_count = 0;
                    if (lexer_peek(p->lexer).type != TOKEN_RPAREN) {
                        do {
                            TypeDesc *pt = parse_type(p, 0);
                            if (!pt) break;
                            Token pnt = lexer_peek(p->lexer);
                            if (pnt.type != TOKEN_IDENTIFIER) break;
                            lexer_advance(p->lexer);
                            ASTNode *pd = ast_alloc(AST_VAR_DECL);
                            pd->d.var_decl.type = pt;
                            strcpy_safe(pd->d.var_decl.name, pnt.lexeme);
                            if (!ptail) method->d.func_def.params = pd; else ptail->next = pd;
                            ptail = pd;
                            method->d.func_def.param_count++;
                        } while (lexer_match(p->lexer, TOKEN_COMMA));
                    }
                    if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");

                    if (lexer_peek(p->lexer).type == TOKEN_LBRACE) {
                        method->d.func_def.body = parse_block(p);
                        *tail = method; tail = &(*tail)->next;
                        def->d.struct_def.member_count++;
                        continue;
                    } else {
                        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");
                        ast_free(method);
                        continue;
                    }
                }
            }

            p->lexer->pos = save_pos;
            p->lexer->line = save_line;
            p->lexer->column = save_col;
            p->lexer->has_current = save_has;
            p->lexer->current = save_cur;
            type_free(mt);
        }

        TypeDesc *ftype = parse_type(p, 0);
        if (!ftype) break;
        Token fn = lexer_peek(p->lexer);
        if (fn.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected field name"); break; }
        lexer_advance(p->lexer);

        ASTNode *field = ast_alloc(AST_VAR_DECL);
        field->d.var_decl.type = ftype;
        strcpy_safe(field->d.var_decl.name, fn.lexeme);
        if (lexer_match(p->lexer, TOKEN_EQ))
            field->d.var_decl.init = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");

        *tail = field; tail = &(*tail)->next;
        def->d.struct_def.member_count++;
        int mi = p->class_info[ci].member_count;
        if (mi >= p->class_info[ci].member_capacity) {
            int newcap = p->class_info[ci].member_capacity ? p->class_info[ci].member_capacity * 2 : 16;
            p->class_info[ci].members = (ClassMember*)realloc(p->class_info[ci].members, newcap * sizeof(ClassMember));
            p->class_info[ci].member_capacity = newcap;
        }
        strcpy_safe(p->class_info[ci].members[mi].name, fn.lexeme);
        p->class_info[ci].members[mi].offset = def->d.struct_def.size;
        p->class_info[ci].member_count++;
        def->d.struct_def.size += 4;
    }

    if (!lexer_match(p->lexer, TOKEN_RBRACE)) parser_error(p, "Expected '}'");
    if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';' after struct/class");

    p->class_info[ci].size = def->d.struct_def.size;
    return def;
}

static Token lexer_peek_n(Lexer *lex, int n) {
    int sp=lex->pos, sl=lex->line, sc=lex->column, sh=lex->has_current;
    Token sc2=lex->current;
    for (int i=0; i<n; i++) {
        if (lex->has_current) { lex->has_current=0; } else lexer_next(lex);
    }
    Token r = lexer_peek(lex);
    lex->pos=sp; lex->line=sl; lex->column=sc;
    lex->has_current=sh; lex->current=sc2;
    return r;
}

/* ========= FUNCTION PARSING ========= */
static ASTNode* parse_function(Parser *p, TypeDesc *ret_type) {
    Token name = lexer_peek(p->lexer);
    if (name.type != TOKEN_IDENTIFIER) { parser_error(p, "Expected function name"); return NULL; }
    lexer_advance(p->lexer);

    if (lexer_peek(p->lexer).type != TOKEN_LPAREN) {
        ASTNode *decl = ast_alloc(AST_VAR_DECL);
        decl->d.var_decl.type = ret_type;
        strcpy_safe(decl->d.var_decl.name, name.lexeme);
        if (lexer_match(p->lexer, TOKEN_EQ))
            decl->d.var_decl.init = parse_expression(p);
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';'");
        return decl;
    }

    lexer_advance(p->lexer);
    push_scope(p);

    ASTNode *func = ast_alloc(AST_FUNC_DEF);
    func->d.func_def.ret_type = ret_type;
    strcpy_safe(func->d.func_def.name, name.lexeme);
    func->d.func_def.is_method = 0;

    ASTNode *ptail = NULL;
    func->d.func_def.params = NULL;
    func->d.func_def.param_count = 0;
    if (lexer_peek(p->lexer).type != TOKEN_RPAREN) {
        if (token_is(p, "void") && lexer_peek_n(p->lexer, 1).type == TOKEN_RPAREN) {
            lexer_advance(p->lexer);
        } else do {
            TypeDesc *ptype = parse_type(p, 0);
            if (!ptype) break;
            Token pn = lexer_peek(p->lexer);
            if (pn.type == TOKEN_IDENTIFIER) {
                lexer_advance(p->lexer);
                ASTNode *pd = ast_alloc(AST_VAR_DECL);
                pd->d.var_decl.type = ptype;
                strcpy_safe(pd->d.var_decl.name, pn.lexeme);
                if (!ptail) func->d.func_def.params = pd; else ptail->next = pd;
                ptail = pd;
                func->d.func_def.param_count++;
                add_symbol(p, pn.lexeme, ptype, 1);
            } else {
                add_symbol(p, "", ptype, 1);
            }
        } while (lexer_match(p->lexer, TOKEN_COMMA));
    }
    if (!lexer_match(p->lexer, TOKEN_RPAREN)) parser_error(p, "Expected ')'");

    if (lexer_peek(p->lexer).type == TOKEN_LBRACE) {
        func->d.func_def.body = parse_block(p);
    } else {
        if (!lexer_match(p->lexer, TOKEN_SEMICOLON)) parser_error(p, "Expected ';' or '{'");
        pop_scope(p);
        ASTNode *decl = ast_alloc(AST_FUNC_DECL);
        decl->d.func_decl.ret_type = ret_type;
        strcpy_safe(decl->d.func_decl.name, name.lexeme);
        decl->d.func_decl.params = func->d.func_def.params;
        decl->d.func_decl.param_count = func->d.func_def.param_count;
        free(func);
        return decl;
    }

    pop_scope(p);
    return func;
}

/* ========= DECLARATION PARSING ========= */
static ASTNode* parse_declaration(Parser *p) {
    Token t = lexer_peek(p->lexer);

    if (token_is(p, "import")) {
        return parse_import_statement(p);
    }

    if (token_is(p, "struct") || token_is(p, "class")) {
        return parse_struct_or_class(p, token_is(p, "class"));
    }

    int save_pos = p->lexer->pos, save_line = p->lexer->line, save_col = p->lexer->column;
    int save_has = p->lexer->has_current;
    Token save_cur = p->lexer->current;

    TypeDesc *type = parse_type(p, 1);
    if (!type) return NULL;

    t = lexer_peek(p->lexer);
    if (t.type == TOKEN_IDENTIFIER || t.type == TOKEN_STAR) {
        ASTNode *func = parse_function(p, type);
        if (func) return func;
    }

    p->lexer->pos = save_pos; p->lexer->line = save_line; p->lexer->column = save_col;
    p->lexer->has_current = save_has; p->lexer->current = save_cur;
    type_free(type);

    lexer_advance(p->lexer);
    return NULL;
}

ASTNode* parser_parse(Parser *p) {
    ASTNode *prog = ast_alloc(AST_PROGRAM);
    prog->d.program.nodes = NULL;
    prog->d.program.count = 0;

    while (lexer_peek(p->lexer).type != TOKEN_EOF) {
        ASTNode *decl = parse_declaration(p);
        if (decl) {
            if (decl->type == AST_IMPORT && decl->d.import.count > 0) {
                for (int i = 0; i < decl->d.import.count; i++) {
                    prog->d.program.count++;
                    prog->d.program.nodes = (ASTNode**)realloc(prog->d.program.nodes,
                        prog->d.program.count * sizeof(ASTNode*));
                    prog->d.program.nodes[prog->d.program.count - 1] = decl->d.import.imported_nodes[i];
                }
                decl->d.import.count = 0;
                decl->d.import.imported_nodes = NULL;
                ast_free(decl);
            } else if (decl->type == AST_IMPORT) {
                ast_free(decl);
            } else {
                prog->d.program.count++;
                prog->d.program.nodes = (ASTNode**)realloc(prog->d.program.nodes,
                    prog->d.program.count * sizeof(ASTNode*));
                prog->d.program.nodes[prog->d.program.count - 1] = decl;
            }
        } else {
            lexer_advance(p->lexer);
        }
    }
    return prog;
}

void parser_free(Parser *p) {
    for (int i = 0; i < p->class_count; i++)
        free(p->class_info[i].members);
    free(p->class_info);
    free(p->strings);
    for (int i = 0; i < p->imported_count; i++)
        free(p->imported_files[i]);
    free(p->imported_files);
    free(p->global_scope.syms);
    /* free any nested scopes still allocated */
    while (p->current_scope != &p->global_scope) {
        Scope *s = p->current_scope;
        p->current_scope = s->parent;
        free(s->syms);
        free(s);
    }
}