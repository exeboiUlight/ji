#include "../include/lexer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void lexer_init(Lexer *lex, TokenRegistry *reg, const char *source) {
    lex->registry = reg;
    lex->source = source;
    lex->pos = 0;
    lex->line = 1;
    lex->column = 1;
    lex->source_len = strlen(source);
    lex->has_current = 0;
}

/* Вспомогательные функции */
static char peek(Lexer *lex) {
    if (lex->pos >= lex->source_len) return '\0';
    return lex->source[lex->pos];
}

static char peek_next(Lexer *lex) {
    if (lex->pos + 1 >= lex->source_len) return '\0';
    return lex->source[lex->pos + 1];
}

static char advance(Lexer *lex) {
    char c = lex->source[lex->pos++];
    lex->column++;
    if (c == '\n') {
        lex->line++;
        lex->column = 1;
    }
    return c;
}

static void skip_whitespace(Lexer *lex) {
    while (lex->pos < lex->source_len) {
        char c = peek(lex);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(lex);
        } else if (c == '/') {
            if (peek_next(lex) == '/') {
                /* Однострочный комментарий */
                while (peek(lex) != '\n' && peek(lex) != '\0') advance(lex);
            } else if (peek_next(lex) == '*') {
                /* Многострочный комментарий */
                advance(lex); advance(lex); /* skip slash-star */
                while (lex->pos < lex->source_len) {
                    if (peek(lex) == '*' && peek_next(lex) == '/') {
                        advance(lex); advance(lex);
                        break;
                    }
                    advance(lex);
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

static Token make_token(Lexer *lex, TokenKind type) {
    Token t;
    t.type = type;
    t.lexeme[0] = '\0';
    t.int_value = 0;
    t.char_value = 0;
    t.line = lex->line;
    t.column = lex->column;
    strcpy_safe(t.name, token_type_name(lex->registry, type));
    return t;
}

static Token make_token_lexeme(Lexer *lex, TokenKind type, const char *lexeme) {
    Token t = make_token(lex, type);
    strcpy_safe(t.lexeme, lexeme);
    return t;
}

/* Многосимвольные операторы */
static Token read_operator(Lexer *lex) {
    char c = advance(lex);
    char d = peek(lex);

    /* Двухсимвольные операторы */
    switch (c) {
        case '+':
            if (d == '+') { advance(lex); return make_token_lexeme(lex, TOKEN_PLUS_PLUS, "++"); }
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_PLUS_EQ, "+="); }
            return make_token_lexeme(lex, TOKEN_PLUS, "+");
        case '-':
            if (d == '-') { advance(lex); return make_token_lexeme(lex, TOKEN_MINUS_MINUS, "--"); }
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_MINUS_EQ, "-="); }
            if (d == '>') { advance(lex); return make_token_lexeme(lex, TOKEN_ARROW, "->"); }
            return make_token_lexeme(lex, TOKEN_MINUS, "-");
        case '*':
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_STAR_EQ, "*="); }
            return make_token_lexeme(lex, TOKEN_STAR, "*");
        case '/':
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_SLASH_EQ, "/="); }
            /* Комментарии обрабатываются в skip_whitespace */
            return make_token_lexeme(lex, TOKEN_SLASH, "/");
        case '%':
            return make_token_lexeme(lex, TOKEN_PERCENT, "%");
        case '=':
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_EQ_EQ, "=="); }
            return make_token_lexeme(lex, TOKEN_EQ, "=");
        case '!':
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_NOT_EQ, "!="); }
            return make_token_lexeme(lex, TOKEN_NOT, "!");
        case '<':
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_LT_EQ, "<="); }
            if (d == '<') { advance(lex); return make_token_lexeme(lex, TOKEN_LSHIFT, "<<"); }
            return make_token_lexeme(lex, TOKEN_LT, "<");
        case '>':
            if (d == '=') { advance(lex); return make_token_lexeme(lex, TOKEN_GT_EQ, ">="); }
            if (d == '>') { advance(lex); return make_token_lexeme(lex, TOKEN_RSHIFT, ">>"); }
            return make_token_lexeme(lex, TOKEN_GT, ">");
        case '&':
            if (d == '&') { advance(lex); return make_token_lexeme(lex, TOKEN_AND_AND, "&&"); }
            return make_token_lexeme(lex, TOKEN_AND, "&");
        case '|':
            if (d == '|') { advance(lex); return make_token_lexeme(lex, TOKEN_OR_OR, "||"); }
            return make_token_lexeme(lex, TOKEN_OR, "|");
        case '^':
            return make_token_lexeme(lex, TOKEN_XOR, "^");
        case '~':
            return make_token_lexeme(lex, TOKEN_TILDE, "~");
        case '?':
            return make_token_lexeme(lex, TOKEN_QUESTION, "?");
        case ':':
            return make_token_lexeme(lex, TOKEN_COLON, ":");
        case ';':
            return make_token_lexeme(lex, TOKEN_SEMICOLON, ";");
        case ',':
            return make_token_lexeme(lex, TOKEN_COMMA, ",");
        case '.':
            return make_token_lexeme(lex, TOKEN_DOT, ".");
        case '(':
            return make_token_lexeme(lex, TOKEN_LPAREN, "(");
        case ')':
            return make_token_lexeme(lex, TOKEN_RPAREN, ")");
        case '{':
            return make_token_lexeme(lex, TOKEN_LBRACE, "{");
        case '}':
            return make_token_lexeme(lex, TOKEN_RBRACE, "}");
        case '[':
            return make_token_lexeme(lex, TOKEN_LBRACKET, "[");
        case ']':
            return make_token_lexeme(lex, TOKEN_RBRACKET, "]");
        default:
        {
            /* Неизвестный символ — возвращаем как идентификатор ошибки */
            char buf[2] = {c, '\0'};
            return make_token_lexeme(lex, TOKEN_EOF, buf);
        }
    }
}

static Token read_string(Lexer *lex) {
    Token t;
    t.line = lex->line;
    t.column = lex->column;
    advance(lex); /* skip opening " */
    int i = 0;
    while (peek(lex) != '"' && peek(lex) != '\0') {
        if (peek(lex) == '\\') {
            advance(lex);
            switch (advance(lex)) {
                case 'n': t.lexeme[i++] = '\n'; break;
                case 't': t.lexeme[i++] = '\t'; break;
                case '0': t.lexeme[i++] = '\0'; break;
                case '\\': t.lexeme[i++] = '\\'; break;
                case '"': t.lexeme[i++] = '"'; break;
                default: t.lexeme[i++] = '\\'; t.lexeme[i++] = lex->source[lex->pos-1]; break;
            }
        } else {
            t.lexeme[i++] = advance(lex);
        }
        if (i >= MAX_IDENT_LEN - 1) break;
    }
    t.lexeme[i] = '\0';
    if (peek(lex) == '"') advance(lex);
    t.type = TOKEN_STRING_LITERAL;
    t.int_value = 0;
    t.char_value = 0;
    strcpy_safe(t.name, "STRING_LITERAL");
    return t;
}

static Token read_char(Lexer *lex) {
    Token t;
    t.line = lex->line;
    t.column = lex->column;
    advance(lex); /* skip ' */
    if (peek(lex) == '\\') {
        advance(lex);
        switch (advance(lex)) {
            case 'n': t.char_value = '\n'; break;
            case 't': t.char_value = '\t'; break;
            case '0': t.char_value = '\0'; break;
            case '\\': t.char_value = '\\'; break;
            case '\'': t.char_value = '\''; break;
            default: t.char_value = lex->source[lex->pos-1]; break;
        }
    } else {
        t.char_value = advance(lex);
    }
    t.lexeme[0] = t.char_value;
    t.lexeme[1] = '\0';
    if (peek(lex) == '\'') advance(lex);
    t.type = TOKEN_CHAR_LITERAL;
    t.int_value = t.char_value;
    strcpy_safe(t.name, "CHAR_LITERAL");
    return t;
}

static Token read_number(Lexer *lex) {
    Token t;
    t.line = lex->line;
    t.column = lex->column;
    int i = 0;
    int value = 0;

    /* Проверка на шестнадцатеричное */
    if (peek(lex) == '0' && (peek_next(lex) == 'x' || peek_next(lex) == 'X')) {
        t.lexeme[i++] = advance(lex); /* 0 */
        t.lexeme[i++] = advance(lex); /* x */
        while (isxdigit(peek(lex))) {
            t.lexeme[i++] = peek(lex);
            value = value * 16 + (isdigit(peek(lex)) ? peek(lex) - '0' : tolower(peek(lex)) - 'a' + 10);
            advance(lex);
        }
        t.int_value = value;
    } else {
        while (isdigit(peek(lex))) {
            t.lexeme[i++] = peek(lex);
            value = value * 10 + (peek(lex) - '0');
            advance(lex);
        }
        t.int_value = value;
    }
    t.lexeme[i] = '\0';
    t.type = TOKEN_NUMBER;
    t.char_value = 0;
    strcpy_safe(t.name, "NUMBER");
    return t;
}

Token lexer_next(Lexer *lex) {
    skip_whitespace(lex);

    if (lex->pos >= lex->source_len) {
        return make_token(lex, TOKEN_EOF);
    }

    char c = peek(lex);

    /* Строковые литералы */
    if (c == '"') return read_string(lex);

    /* Символьные литералы */
    if (c == '\'') return read_char(lex);

    /* Числа */
    if (isdigit(c)) return read_number(lex);

    /* Идентификаторы и ключевые слова */
    if (isalpha(c) || c == '_') {
        int i = 0;
        char ident[MAX_IDENT_LEN];
        while (isalnum(peek(lex)) || peek(lex) == '_') {
            ident[i++] = advance(lex);
            if (i >= MAX_IDENT_LEN - 1) break;
        }
        ident[i] = '\0';

        /* Проверяем в реестре — сначала ключевые слова */
        const CustomTokenDef *def = token_registry_find(lex->registry, ident, 1);
        if (def) {
            return make_token_lexeme(lex, def->type, ident);
        }

        /* Обычный идентификатор */
        return make_token_lexeme(lex, TOKEN_IDENTIFIER, ident);
    }

    /* Операторы и разделители */
    return read_operator(lex);
}

Token lexer_peek(Lexer *lex) {
    if (!lex->has_current) {
        lex->current = lexer_next(lex);
        lex->has_current = 1;
    }
    return lex->current;
}

void lexer_advance(Lexer *lex) {
    if (lex->has_current) {
        lex->has_current = 0;
    } else {
        lexer_next(lex); /* пропускаем */
    }
}

int lexer_match(Lexer *lex, TokenKind type) {
    if (lexer_peek(lex).type == type) {
        lexer_advance(lex);
        return 1;
    }
    return 0;
}

int lexer_check(Lexer *lex, TokenKind type) {
    return lexer_peek(lex).type == type;
}

void lexer_free(Lexer *lex) {
    (void)lex;
}
