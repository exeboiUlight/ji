#ifndef SAFETY_H
#define SAFETY_H

#include "../include/ast.h"
#include "../include/token.h"

typedef enum {
    SAFETY_OK = 0,
    SAFETY_ERR_NULL_DEREF,
    SAFETY_ERR_UNINIT_VAR,
    SAFETY_ERR_DIVISION_BY_ZERO,
    SAFETY_ERR_ARRAY_BOUNDS,
    SAFETY_ERR_TYPE_MISMATCH,
    SAFETY_ERR_USE_AFTER_FREE,
    SAFETY_ERR_INVALID_CAST,
    SAFETY_ERR_DEAD_CODE,
    SAFETY_ERR_UNREACHABLE,
    SAFETY_ERR_INVALID_POINTER,
    SAFETY_ERR_MEMORY_LEAK,
    SAFETY_ERR_NEGATIVE_ARRAY_SIZE,
    SAFETY_ERR_SIZEOF_TYPE,
} SafetyErrorType;

typedef struct {
    SafetyErrorType type;
    int line;
    int col;
    char message[256];
    char details[256];
} SafetyError;

typedef struct {
    SafetyError *errors;
    int error_count;
    int error_capacity;

    int *current_function;
    int function_depth;
    int function_capacity;

    int loop_depth;
    int in_switch;

    int null_checks_enabled;
    int bounds_checks_enabled;
    int uninit_checks_enabled;
} SafetyContext;

typedef struct {
    char name[MAX_IDENT_LEN];
    TypeDesc *type;
    int initialized;
    int may_be_null;
    int address_taken;
    int line_decl;
    int last_use_line;
    int is_freed;
    int is_param;
} VariableTracker;

typedef struct {
    VariableTracker *vars;
    int var_count;
    int var_capacity;
    int scope_depth;
    int in_loop;
    int break_label;
    int continue_label;
} LocalVarContext;

void safety_init(SafetyContext *ctx);
void safety_free(SafetyContext *ctx);

int safety_check_program(SafetyContext *ctx, ASTNode *program);
int safety_check_function(SafetyContext *ctx, ASTNode *func);
int safety_check_statement(SafetyContext *ctx, ASTNode *stmt, LocalVarContext *lvc);
int safety_check_expression(SafetyContext *ctx, ASTNode *expr, LocalVarContext *lvc);

void safety_report_error(SafetyContext *ctx, SafetyErrorType type, int line, int col,
                         const char *msg, const char *details);

int type_compatible(TypeDesc *a, TypeDesc *b);
int type_can_be_null(TypeDesc *t);
int is_pointer_type(TypeDesc *t);
int is_array_type(TypeDesc *t);

#endif