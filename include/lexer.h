#ifndef LEXER_H
#define LEXER_H

#include "../include/token.h"

typedef struct {
    TokenRegistry *registry;
    const char *source;
    int pos;
    int line;
    int column;
    int source_len;
    Token current;      /* текущий токен (peek) */
    int has_current;
} Lexer;

/* Инициализация лексера */
void lexer_init(Lexer *lex, TokenRegistry *reg, const char *source);

/* Получить следующий токен */
Token lexer_next(Lexer *lex);

/* Вернуть текущий токен (peek) */
Token lexer_peek(Lexer *lex);

/* Пропустить текущий токен (consume) */
void lexer_advance(Lexer *lex);

/* Проверить и пропустить токен */
int lexer_match(Lexer *lex, TokenKind type);

/* Проверить тип текущего токена */
int lexer_check(Lexer *lex, TokenKind type);

/* Освободить ресурсы */
void lexer_free(Lexer *lex);

#endif
