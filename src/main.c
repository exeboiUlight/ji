#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/token.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/codegen.h"
#include "../include/pe.h"
#include "../include/elf.h"
#include "../include/safety.h"
#include "../include/jit.h"
#include "../include/project.h"

enum TargetOS { TARGET_WINDOWS, TARGET_LINUX };
static enum TargetOS target_os = TARGET_WINDOWS;

static char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Error: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);
    return buf;
}

static void change_ext(const char *path, const char *ext, char *out, int size) {
    snprintf(out, size, "%s", path);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    strncat(out, ext, size - strlen(out) - 1);
}

static void register_tokens(TokenRegistry *reg) {
    const char *types[] = {"int","char","void","float","double","if","else","while","for",
        "do","switch","case","default","break","continue","return","struct","class",
        "virtual","public","private","protected","new","delete","this","null","NULL",
        "sizeof","typedef","const","static","import","as",
        "asm",
        NULL};
    for (int i = 0; types[i]; i++)
        token_registry_add(reg, types[i], types[i], 1);
}

static int compile_source(const char *source_path, const char *root_path,
                          enum TargetOS tgt, Emitter *emit, Codegen *cg,
                          PEInfo *info, ASTNode **out_ast)
{
    char *source = read_file(source_path);
    if (!source) return 1;
    printf("[Main] File: %s (%zu bytes)\n", source_path, strlen(source));

    TokenRegistry reg;
    token_registry_init(&reg);
    register_tokens(&reg);

    Lexer lex;
    lexer_init(&lex, &reg, source);

    Parser parser;
    parser_init(&parser, &lex, &reg);

    if (root_path && root_path[0] != '\0')
        strcpy_safe(parser.root_dir, root_path);
    else
        strcpy(parser.root_dir, ".");

    {
        char *last_slash = NULL;
        for (char *p = (char*)source_path; *p; p++)
            if (*p == '\\' || *p == '/') last_slash = p;
        if (last_slash) {
            int len = (int)(last_slash - source_path);
            snprintf(parser.source_dir, sizeof(parser.source_dir), "%s", source_path);
            parser.source_dir[len < 255 ? len : 255] = '\0';
        } else {
            strcpy(parser.source_dir, ".");
        }
    }

    ASTNode *ast = parser_parse(&parser);
    if (parser.error_count > 0) {
        fprintf(stderr, "Errors: %d\n", parser.error_count);
        ast_free(ast); parser_free(&parser); lexer_free(&lex); free(source);
        return 1;
    }

    printf("[Main] === Safety check ===\n");
    SafetyContext safety;
    safety_init(&safety);
    int safety_errors = safety_check_program(&safety, ast);
    safety_free(&safety);
    if (safety_errors > 0)
        fprintf(stderr, "[SAFETY] Warnings: %d (non-fatal)\n", safety_errors);
    else
        printf("[Main] Safety check: OK\n");

    emit_init(emit);
    codegen_init(cg, &reg, emit);
    cg->target_os = tgt;
    codegen_generate(cg, ast);

    if (emit->pos == 0) {
        fprintf(stderr, "No code generated\n");
        emit_free(emit); ast_free(ast); parser_free(&parser); lexer_free(&lex); free(source);
        return 1;
    }

    printf("[Main] Code size: %d bytes\n", emit->pos);
    printf("[Main] Import calls: %d\n", cg->import_call_count);

    memset(info, 0, sizeof(*info));
    info->code = emit->code;
    info->code_size = emit->pos;
    info->entry_offset = codegen_get_entry(cg);
    if (info->entry_offset < 0) {
        fprintf(stderr, "Entry point (main) not found\n");
        emit_free(emit); ast_free(ast); parser_free(&parser); lexer_free(&lex); free(source);
        return 1;
    }

    /* Parser no longer needed */
    parser_free(&parser);

    info->import_call_count = cg->import_call_count;
    info->import_call_capacity = cg->import_call_count;
    info->import_call_sites = NULL;
    info->import_names = NULL;
    if (cg->import_call_count > 0) {
        info->import_call_sites = (int*)malloc(cg->import_call_count * sizeof(int));
        info->import_names = (char(*)[64])malloc(cg->import_call_count * 64);
        for (int i = 0; i < cg->import_call_count; i++) {
            info->import_call_sites[i] = cg->import_call_sites[i];
            strcpy_safe(info->import_names[i], cg->import_names[i]);
            info->import_names[i][63] = '\0';
        }
    }

    info->import_capacity = 2;
    info->imports = (ImportDescriptor*)calloc(2, sizeof(ImportDescriptor));

    info->imports[0].dll_name = (tgt == TARGET_WINDOWS) ? "kernel32.dll" : "";
    info->imports[0].func_capacity = 1;
    info->imports[0].func_names = (const char**)malloc(sizeof(const char*));
    info->imports[0].func_names[0] = "ExitProcess";
    info->imports[0].func_count = 1;
    info->import_count = 1;

    if (cg->import_call_count > 0) {
        info->imports[1].dll_name = (tgt == TARGET_WINDOWS) ? "msvcrt.dll" : "libc.so.6";
        info->imports[1].func_capacity = cg->import_call_count;
        info->imports[1].func_names = (const char**)malloc(cg->import_call_count * sizeof(const char*));
        info->imports[1].func_count = 0;
        for (int i = 0; i < cg->import_call_count; i++) {
            int dup = 0;
            for (int j = 0; j < info->imports[1].func_count; j++)
                if (strcmp(info->imports[1].func_names[j], cg->import_names[i]) == 0) { dup = 1; break; }
            if (!dup) {
                info->imports[1].func_names[info->imports[1].func_count] = cg->import_names[i];
                info->imports[1].func_count++;
            }
        }
        info->import_count = 2;
    }

    if (out_ast) *out_ast = ast;

    lexer_free(&lex);
    free(source);
    return 0;
}

static int do_build(Codegen *cg, PEInfo *info, const char *exe_path) {
    printf("[Main] Creating %s ...\n", exe_path);
    int ret;
    if (target_os == TARGET_WINDOWS)
        ret = pe_write(exe_path, info);
    else
        ret = elf_write(exe_path, info, cg->emitter);

    if (ret == 0)
        printf("[Main] Success: %s (%d bytes code)\n", exe_path, info->code_size);
    else
        fprintf(stderr, "Output file write error\n");
    return ret;
}

int main(int argc, char **argv) {
    printf("=== JI 1.0 Compiler (JIT + Project System) ===\n");
    printf("C-like language with OOP -> x86-64 .exe/.elf / JIT\n\n");

    if (argc < 2) {
        printf("Usage:\n");
        printf("  ji new <project-name>          Create new project\n");
        printf("  ji build [project-dir]         Build project to executable\n");
        printf("  ji run [project-dir]           Run project via JIT\n");
        printf("  ji <input.ji> [options]        Compile single file\n");
        printf("\nOptions for single file:\n");
        printf("  -target windows|linux          Target OS\n");
        printf("  -root dir                      Import root directory\n");
        printf("  -o output                      Output file path\n");
        return 1;
    }

    /* ---- ji new <name> ---- */
    if (strcmp(argv[1], "new") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: ji new <project-name>\n");
            return 1;
        }
        return project_create(argv[2]);
    }

    /* ---- ji build / ji run ---- */
    if (strcmp(argv[1], "build") == 0 || strcmp(argv[1], "run") == 0) {
        int is_run = (strcmp(argv[1], "run") == 0);
        const char *proj_dir = (argc > 2) ? argv[2] : ".";

        ProjectConfig cfg;
        if (project_load(proj_dir, &cfg) != 0) return 1;
        printf("\n=== Project ===\n");
        project_print(&cfg);
        printf("\n");

        project_fetch_libraries(&cfg, proj_dir);
        printf("\n");

        /* Determine target from project name or default */
        char main_ji[512];
        snprintf(main_ji, sizeof(main_ji), "%s/src/main.ji", proj_dir);

        /* Include src dir in root path for imports */
        char src_root[512];
        snprintf(src_root, sizeof(src_root), "%s/src", proj_dir);

        Emitter emit;
        Codegen cg;
        PEInfo info;
        ASTNode *ast = NULL;

        if (compile_source(main_ji, src_root, target_os, &emit, &cg, &info, &ast) != 0)
            return 1;

        for (int i = 0; i < cfg.libraries.count; i++) {
            LibraryConfig *lib = &cfg.libraries.libs[i];
            if (lib->type == LIB_DYNAMIC && lib->name[0] != '\0') {
                char dll_name[256];
                const char *base = lib->name;
                const char *dot = strrchr(base, '.');
                if (dot) {
                    int len = (int)(dot - base);
                    if (len < sizeof(dll_name)) {
                        memcpy(dll_name, base, len);
                        dll_name[len] = '\0';
                    } else {
                        dll_name[0] = '\0';
                    }
                } else {
                    snprintf(dll_name, sizeof(dll_name), "%s", base);
                }
                printf("[Main] Linking dynamic library: %s -> %s.dll\n", lib->name, dll_name);
                pe_info_add_import(&info, dll_name, ""); /* Add DLL import */
            } else if (lib->type == LIB_STATIC) {
                printf("[Main] Linking static library: %s\n", lib->lib_file);
                FILE *lf = fopen(lib->lib_file, "rb");
                if (lf) {
                    fseek(lf, 0, SEEK_END);
                    long sz = ftell(lf);
                    fseek(lf, 0, SEEK_SET);
                    uint8_t *buf = (uint8_t*)malloc(sz);
                    fread(buf, 1, sz, lf);
                    fclose(lf);
                    pe_info_add_static_lib(&info, lib->lib_file, buf, (int)sz);
                    free(buf);
                } else {
                    fprintf(stderr, "[Main] Warning: static library not found: %s\n", lib->lib_file);
                }
            }
        }

        if (is_run) {
            /* JIT execute */
            info.code = emit.code;
            int ret = jit_run(&cg, &info);
            /* Cleanup */
            free(info.import_call_sites);
            free(info.import_names);
            if (info.imports) {
                for (int i = 0; i < info.import_count; i++)
                    free((void*)info.imports[i].func_names);
                free(info.imports);
            }
            emit_free(&emit);
            if (ast) ast_free(ast);
            return ret;
        } else {
            /* Build to executable */
            char exe_path[512];
            snprintf(exe_path, sizeof(exe_path), "%s/%s", proj_dir, cfg.name);
            if (target_os == TARGET_WINDOWS)
                strcat(exe_path, ".exe");
            else
                strcat(exe_path, ".elf");

            int ret = do_build(&cg, &info, exe_path);

            free(info.import_call_sites);
            free(info.import_names);
            if (info.imports) {
                for (int i = 0; i < info.import_count; i++)
                    free((void*)info.imports[i].func_names);
                free(info.imports);
            }
            emit_free(&emit);
            if (ast) ast_free(ast);
            return ret;
        }
    }

    /* ---- ji <input.ji> [options] (single file compile) ---- */
    char exe_path[512];
    change_ext(argv[1], ".exe", exe_path, sizeof(exe_path));

    char root_path[256] = {0};
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc) {
            strcpy_safe(exe_path, argv[++i]);
        } else if (strcmp(argv[i], "-target") == 0 && i+1 < argc) {
            i++;
            if (strcmp(argv[i], "windows") == 0) target_os = TARGET_WINDOWS;
            else if (strcmp(argv[i], "linux") == 0) target_os = TARGET_LINUX;
            else fprintf(stderr, "Unknown target: %s\n", argv[i]);
        } else if (strcmp(argv[i], "-root") == 0 && i+1 < argc) {
            strcpy_safe(root_path, argv[++i]);
        }
    }

    if (target_os == TARGET_LINUX) {
        char *dot = strrchr(exe_path, '.');
        if (dot && strcmp(dot, ".exe") == 0) *dot = '\0';
        strcat(exe_path, ".elf");
    }

    Emitter emit;
    Codegen cg;
    PEInfo info;
    ASTNode *ast = NULL;

    if (compile_source(argv[1], root_path, target_os, &emit, &cg, &info, &ast) != 0)
        return 1;

    int ret = do_build(&cg, &info, exe_path);

    free(info.import_call_sites);
    free(info.import_names);
    if (info.imports) {
        for (int i = 0; i < info.import_count; i++)
            free((void*)info.imports[i].func_names);
        free(info.imports);
    }

    emit_free(&emit);
    if (ast) ast_free(ast);

    return ret;
}
