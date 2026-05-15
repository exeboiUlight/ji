#ifndef CODEGEN_H
#define CODEGEN_H

#include "../include/ast.h"
#include "../include/token.h"
#include "../include/emit.h"

/* Target OS */
enum TargetOS { TARGET_WINDOWS, TARGET_LINUX };

typedef struct {
    char value[256];
    int label_id;
} CodegenStringEntry;

typedef struct {
    char name[128];
    int size;
} CodegenClassSize;

typedef struct Codegen {
    TokenRegistry *registry;
    Emitter *emitter;
    int label_counter;
    enum TargetOS target_os;

    int *import_call_sites;
    char (*import_names)[64];
    int import_call_count;
    int import_call_capacity;

    CodegenStringEntry *strings;
    int string_count;
    int string_capacity;

    CodegenClassSize *class_sizes;
    int class_count;
    int class_capacity;
} Codegen;

void codegen_init(Codegen *cg, TokenRegistry *reg, Emitter *em);
void codegen_generate(Codegen *cg, ASTNode *program);
int codegen_get_entry(Codegen *cg);

int codegen_get_import_call_count(Codegen *cg);
int codegen_get_import_call_pos(Codegen *cg, int i);
const char* codegen_get_import_call_name(Codegen *cg, int i);

#endif