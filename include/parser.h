#ifndef PARSER_H
#define PARSER_H

#include "../include/lexer.h"
#include "../include/ast.h"

typedef struct {
    char name[MAX_IDENT_LEN];
    TypeDesc *type;
    int is_param;
    int offset;
} Symbol;

typedef struct Scope {
    Symbol *syms;
    int count;
    int capacity;
    struct Scope *parent;
    char class_name[MAX_IDENT_LEN];
    int is_class;
} Scope;

typedef struct {
    char name[MAX_IDENT_LEN];
    int offset;
} ClassMember;

typedef struct {
    char name[MAX_IDENT_LEN];
    int size;
    struct ASTNode *vtable;
    int vtable_count;
    ClassMember *members;
    int member_count;
    int member_capacity;
} ClassInfo;

typedef struct {
    char value[MAX_IDENT_LEN];
    int label_id;
} StringEntry;

typedef struct {
    Lexer *lexer;
    TokenRegistry *registry;
    int error_count;

    Scope *current_scope;
    Scope global_scope;

    /* Class tracking */
    ClassInfo *class_info;
    int class_count;
    int class_capacity;

    /* String literals */
    StringEntry *strings;
    int string_count;
    int string_capacity;

    /* Import tracking */
    char **imported_files;
    int imported_count;
    int imported_capacity;

    /* Source directory for import resolution (set to source file's dir) */
    char source_dir[256];
    /* Root directory for Python-style imports (set via -root flag) */
    char root_dir[256];
} Parser;

void parser_init(Parser *p, Lexer *lex, TokenRegistry *reg);
ASTNode* parser_parse(Parser *p);
void parser_error(Parser *p, const char *msg);

int parser_get_string_count(Parser *p);
const char* parser_get_string(Parser *p, int i);
int parser_get_string_label(Parser *p, int i);
void parser_free(Parser *p);

#endif