#ifndef AST_H
#define AST_H

#include "../include/token.h"

#ifndef MAX_IDENT_LEN
#define MAX_IDENT_LEN 64
#endif

/* === AST node types === */
typedef enum {
    /* Top-level */
    AST_PROGRAM,
    AST_FUNC_DEF,       /* function definition */
    AST_FUNC_DECL,      /* function declaration (prototype) */
    /* Statements */
    AST_BLOCK,
    AST_VAR_DECL,
    AST_IF,
    AST_WHILE,
    AST_DO_WHILE,
    AST_FOR,
    AST_SWITCH,
    AST_CASE,
    AST_DEFAULT,
    AST_BREAK,
    AST_CONTINUE,
    AST_RETURN,
    AST_EXPR_STMT,
    /* Declarations */
    AST_STRUCT_DEF,
    AST_CLASS_DEF,
    /* Expressions */
    AST_IDENT,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_STR_LIT,
    AST_CHAR_LIT,
    AST_BINARY,
    AST_UNARY,
    AST_ASSIGN,
    AST_CALL,
    AST_TERNARY,
    AST_COMMA,
    AST_MEMBER,        /* obj.member or obj->member */
    AST_ARRAY_SUB,     /* arr[index] */
    AST_CAST,
    AST_SIZEOF,
    AST_NEW,
    AST_DELETE,
    AST_THIS,
    AST_NULL,
    AST_IMPORT,
    AST_ASM_BLOCK,
} ASTNodeType;

/* Binary/unary operators */
typedef enum {
    /* binary */
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_EQ, BIN_NE, BIN_LT, BIN_GT, BIN_LE, BIN_GE,
    BIN_AND, BIN_OR, BIN_BIT_AND, BIN_BIT_OR, BIN_XOR,
    BIN_LSHIFT, BIN_RSHIFT,
    BIN_ASSIGN, /* plain assignment = */
    /* unary */
    UN_NEG, UN_NOT, UN_BIT_NOT,
    UN_ADDR, UN_DEREF,
    UN_PRE_INC, UN_PRE_DEC,
    UN_POST_INC, UN_POST_DEC,
    UN_PLUS,
} ASTOp;

/* Type descriptor for variable declarations */
typedef struct TypeDesc {
    TokenKind base_type;       /* TOKEN for int/char/void/class/struct */
    char type_name[MAX_IDENT_LEN]; /* for struct/class names */
    int is_pointer;            /* pointer depth */
    int is_array;              /* 1 if array */
    int array_size;            /* 0 = unspecified */
    struct TypeDesc *next;     /* for function params / chaining */
} TypeDesc;

/* AST node */
typedef struct ASTNode {
    ASTNodeType type;
    int line, col;             /* source location */
    struct ASTNode *next;      /* for linked lists (statements, args) */
    union {
        /* Program */
        struct { int count; struct ASTNode **nodes; } program;

        /* Function definition */
        struct {
            TypeDesc *ret_type;
            char name[MAX_IDENT_LEN];
            struct ASTNode *params;   /* linked list of VarDecl */
            int param_count;
            struct ASTNode *body;     /* Block */
            int is_method;            /* 1 if class method */
            int is_constructor;       /* 1 if constructor (like JS) */
            char class_name[MAX_IDENT_LEN];
            int is_virtual;
            int vtable_index;
        } func_def;

        /* Function declaration (prototype) */
        struct {
            TypeDesc *ret_type;
            char name[MAX_IDENT_LEN];
            struct ASTNode *params;
            int param_count;
        } func_decl;

        /* Block */
        struct { struct ASTNode *stmts; } block;

        /* Variable declaration */
        struct {
            TypeDesc *type;
            char name[MAX_IDENT_LEN];
            struct ASTNode *init;
            int is_class_instance;
            char class_name[MAX_IDENT_LEN];
        } var_decl;

        /* If/else */
        struct { struct ASTNode *cond, *then, *els; } if_stmt;

        /* While / Do-while */
        struct { struct ASTNode *cond, *body; } loop;

        /* For */
        struct { struct ASTNode *init, *cond, *incr, *body; } for_stmt;

        /* Switch */
        struct { struct ASTNode *cond, *cases; } switch_stmt;

        /* Case / Default */
        struct { int value; struct ASTNode *stmts; } case_stmt;

        /* Break / Continue / Return */
        struct { struct ASTNode *expr; } ret;

        /* Struct / Class definition */
        struct {
            char name[MAX_IDENT_LEN];
            struct ASTNode *members;  /* linked list of VarDecl + FuncDef */
            int member_count;
            int size;                 /* total size in bytes */
        } struct_def;

        /* Identifier */
        struct { char name[MAX_IDENT_LEN]; } ident;

        /* Literals */
        struct { long long value; } int_lit;
        struct { double value; } float_lit;
        struct { char value[MAX_IDENT_LEN]; } str_lit;
        struct { char value; } char_lit;

        /* Binary */
        struct { ASTOp op; struct ASTNode *left, *right; } binary;

        /* Unary */
        struct { ASTOp op; struct ASTNode *operand; } unary;

        /* Assignment */
        struct { ASTOp op; struct ASTNode *target, *value; } assign;

        /* Function call */
        struct { struct ASTNode *func; struct ASTNode *args; int arg_count; } call;

        /* Ternary */
        struct { struct ASTNode *cond, *then, *els; } ternary;

        /* Member access */
        struct { struct ASTNode *obj; char member[MAX_IDENT_LEN]; int arrow; int member_offset; } member;

        /* Array subscript */
        struct { struct ASTNode *arr, *index; } array_sub;

        /* Cast */
        struct { TypeDesc *type; struct ASTNode *expr; } cast;

        /* Sizeof */
        struct { struct ASTNode *expr; } sizeof_expr;

        /* New / Delete */
        struct { char class_name[MAX_IDENT_LEN]; struct ASTNode *args; } new_expr;
        struct { struct ASTNode *expr; } delete_expr;

        /* Import */
        struct { char filename[MAX_IDENT_LEN]; struct ASTNode **imported_nodes; int count; } import;
        /* Inline assembly block */
        struct { char *text; int text_len; } asm_block;
    } d;
} ASTNode;

ASTNode* ast_alloc(ASTNodeType type);
TypeDesc* type_alloc(TokenKind base);
void ast_free(ASTNode *node);
void type_free(TypeDesc *t);

#endif
