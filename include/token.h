#ifndef TOKEN_H
#define TOKEN_H

#define MAX_TOKEN_NAME 64
#define MAX_TOKEN_REGISTRY 256
#define MAX_IDENT_LEN 128

#include <string.h>

/* Safe string copy — always null-terminates, uses sizeof for array dest */
#define strcpy_safe(dst, src) do { \
    size_t _len = strlen(src); \
    if (_len >= sizeof(dst)) _len = sizeof(dst) - 1; \
    memcpy((dst), (src), _len); \
    (dst)[_len] = '\0'; \
} while(0)

/* Типы токенов (базовые) */
typedef enum {
    TOKEN_EOF = 0,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_CHAR_LITERAL,
    TOKEN_STRING_LITERAL,

    /* Операторы */
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS,
    TOKEN_EQ, TOKEN_EQ_EQ,
    TOKEN_NOT, TOKEN_NOT_EQ,
    TOKEN_LT, TOKEN_GT, TOKEN_LT_EQ, TOKEN_GT_EQ,
    TOKEN_AND, TOKEN_AND_AND,
    TOKEN_OR, TOKEN_OR_OR,
    TOKEN_XOR,
    TOKEN_TILDE,
    TOKEN_LSHIFT, TOKEN_RSHIFT,
    TOKEN_QUESTION, TOKEN_COLON,
    TOKEN_PLUS_EQ, TOKEN_MINUS_EQ, TOKEN_STAR_EQ, TOKEN_SLASH_EQ,

    /* Разделители */
    TOKEN_SEMICOLON, TOKEN_COMMA, TOKEN_DOT,
    TOKEN_LPAREN, TOKEN_RPAREN,
    TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_LBRACKET, TOKEN_RBRACKET,
    TOKEN_ARROW,

    /* Ключевые слова (будут динамически регистрироваться) */
    TOKEN_KEYWORD_FIRST = 100,

    /* Пользовательские токены начинаются отсюда */
    TOKEN_CUSTOM_FIRST = 200,
} TokenKind;

/* Структура токена */
typedef struct {
    TokenKind type;
    char name[MAX_TOKEN_NAME];
    char lexeme[MAX_IDENT_LEN];
    int int_value;
    char char_value;
    int line;
    int column;
} Token;

/* Тип кастомного токена */
typedef struct {
    TokenKind type;
    char name[MAX_TOKEN_NAME];
    char pattern[MAX_TOKEN_NAME];  /* ключевое слово или паттерн */
    int is_keyword;                /* 1 = ключевое слово, 0 = оператор/разделитель */
} CustomTokenDef;

/* Реестр токенов */
typedef struct {
    CustomTokenDef tokens[MAX_TOKEN_REGISTRY];
    int count;
    TokenKind next_type;
} TokenRegistry;

/* Инициализация реестра */
void token_registry_init(TokenRegistry *reg);

/* Добавить кастомный токен */
TokenKind token_registry_add(TokenRegistry *reg, const char *name, const char *pattern, int is_keyword);

/* Найти кастомный токен по паттерну */
const CustomTokenDef* token_registry_find(TokenRegistry *reg, const char *pattern, int is_keyword);

/* Найти кастомный токен по типу */
const CustomTokenDef* token_registry_find_by_type(TokenRegistry *reg, TokenKind type);

/* Получить имя токена */
const char* token_type_name(TokenRegistry *reg, TokenKind type);

/* Печать токена */
void token_print(TokenRegistry *reg, Token *t);

#endif
