#include "../include/token.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* base_token_names[] = {
    [TOKEN_EOF]         = "EOF",
    [TOKEN_IDENTIFIER]  = "IDENTIFIER",
    [TOKEN_NUMBER]      = "NUMBER",
    [TOKEN_CHAR_LITERAL]   = "CHAR_LITERAL",
    [TOKEN_STRING_LITERAL] = "STRING_LITERAL",
    [TOKEN_PLUS]        = "+",
    [TOKEN_MINUS]       = "-",
    [TOKEN_STAR]        = "*",
    [TOKEN_SLASH]       = "/",
    [TOKEN_PERCENT]     = "%",
    [TOKEN_PLUS_PLUS]   = "++",
    [TOKEN_MINUS_MINUS] = "--",
    [TOKEN_EQ]          = "=",
    [TOKEN_EQ_EQ]       = "==",
    [TOKEN_NOT]         = "!",
    [TOKEN_NOT_EQ]      = "!=",
    [TOKEN_LT]          = "<",
    [TOKEN_GT]          = ">",
    [TOKEN_LT_EQ]       = "<=",
    [TOKEN_GT_EQ]       = ">=",
    [TOKEN_AND]         = "&",
    [TOKEN_AND_AND]     = "&&",
    [TOKEN_OR]          = "|",
    [TOKEN_OR_OR]       = "||",
    [TOKEN_XOR]         = "^",
    [TOKEN_TILDE]       = "~",
    [TOKEN_LSHIFT]      = "<<",
    [TOKEN_RSHIFT]      = ">>",
    [TOKEN_QUESTION]    = "?",
    [TOKEN_COLON]       = ":",
    [TOKEN_PLUS_EQ]     = "+=",
    [TOKEN_MINUS_EQ]    = "-=",
    [TOKEN_STAR_EQ]     = "*=",
    [TOKEN_SLASH_EQ]    = "/=",
    [TOKEN_SEMICOLON]   = ";",
    [TOKEN_COMMA]       = ",",
    [TOKEN_DOT]         = ".",
    [TOKEN_LPAREN]      = "(",
    [TOKEN_RPAREN]      = ")",
    [TOKEN_LBRACE]      = "{",
    [TOKEN_RBRACE]      = "}",
    [TOKEN_LBRACKET]    = "[",
    [TOKEN_RBRACKET]    = "]",
    [TOKEN_ARROW]       = "->",
};

void token_registry_init(TokenRegistry *reg) {
    reg->count = 0;
    reg->next_type = TOKEN_CUSTOM_FIRST;
}

TokenKind token_registry_add(TokenRegistry *reg, const char *name, const char *pattern, int is_keyword) {
    if (reg->count >= MAX_TOKEN_REGISTRY) {
        fprintf(stderr, "Token registry full!\n");
        return TOKEN_EOF;
    }
    CustomTokenDef *def = &reg->tokens[reg->count];
    def->type = reg->next_type++;
    strcpy_safe(def->name, name);
    strcpy_safe(def->pattern, pattern);
    def->is_keyword = is_keyword;
    reg->count++;

    printf("[TokenRegistry] Registered token: %s (type=%d, pattern='%s', keyword=%d)\n",
           name, def->type, pattern, is_keyword);
    return def->type;
}

const CustomTokenDef* token_registry_find(TokenRegistry *reg, const char *pattern, int is_keyword) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->tokens[i].pattern, pattern) == 0 &&
            reg->tokens[i].is_keyword == is_keyword) {
            return &reg->tokens[i];
        }
    }
    return NULL;
}

const CustomTokenDef* token_registry_find_by_type(TokenRegistry *reg, TokenKind type) {
    for (int i = 0; i < reg->count; i++) {
        if (reg->tokens[i].type == type)
            return &reg->tokens[i];
    }
    return NULL;
}

const char* token_type_name(TokenRegistry *reg, TokenKind type) {
    if (type < sizeof(base_token_names)/sizeof(base_token_names[0]) && base_token_names[type]) {
        return base_token_names[type];
    }
    if (type >= TOKEN_KEYWORD_FIRST && type < TOKEN_CUSTOM_FIRST) {
        const CustomTokenDef *def = token_registry_find_by_type(reg, type);
        if (def) return def->name;
        return "KEYWORD";
    }
    const CustomTokenDef *def = token_registry_find_by_type(reg, type);
    if (def) return def->name;
    return "UNKNOWN";
}

void token_print(TokenRegistry *reg, Token *t) {
    printf("[%d:%d] %s", t->line, t->column, token_type_name(reg, t->type));
    if (t->type == TOKEN_IDENTIFIER) {
        printf(" = '%s'", t->lexeme);
    } else if (t->type == TOKEN_NUMBER) {
        printf(" = %d", t->int_value);
    } else if (t->type == TOKEN_CHAR_LITERAL) {
        printf(" = '%c'", t->char_value);
    } else if (t->type == TOKEN_STRING_LITERAL) {
        printf(" = \"%s\"", t->lexeme);
    } else if (t->type >= TOKEN_KEYWORD_FIRST) {
        printf(" = '%s'", t->lexeme);
    }
    printf("\n");
}