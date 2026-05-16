#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../include/jit.h"
#include "../include/codegen.h"
#include "../include/token.h"
#include "../include/emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef _WIN32
#include <imagehlp.h>
typedef uint64_t uint64_t;
typedef uint32_t uint32_t;
typedef uint16_t uint16_t;
typedef uint8_t uint8_t;
#endif

/* Determine which DLL a function belongs to */
static const char* func_dll(const char *name) {
    if (strcmp(name, "ExitProcess") == 0)
        return "kernel32.dll";
    return "msvcrt.dll";
}

int jit_run(Codegen *cg, PEInfo *info) {
    if (info->entry_offset < 0) {
        fprintf(stderr, "[JIT] Entry point not found\n");
        return 1;
    }
    if (info->code_size == 0) {
        fprintf(stderr, "[JIT] No code generated\n");
        return 1;
    }

#ifdef _WIN32
    /* ---- Windows JIT ---- */

    /* Build unique import list with DLL mapping */
    typedef struct {
        char name[64];
        void *addr;
    } ImportThunk;

    int thunk_count = 0;
    int thunk_capacity = info->import_call_count;
    ImportThunk *thunks = (ImportThunk*)calloc(thunk_capacity, sizeof(ImportThunk));

    for (int k = 0; k < info->import_call_count; k++) {
        const char *fname = info->import_names[k];
        int found = 0;
        for (int t = 0; t < thunk_count; t++) {
            if (strcmp(thunks[t].name, fname) == 0) { found = 1; break; }
        }
        if (!found) {
            strcpy_safe(thunks[thunk_count].name, fname);
            /* Resolve address via LoadLibrary/GetProcAddress */
            const char *dll = func_dll(fname);
            HMODULE hMod = LoadLibraryA(dll);
            if (!hMod) {
                fprintf(stderr, "[JIT] Failed to load %s (error %lu)\n", dll, GetLastError());
                free(thunks);
                return 1;
            }
            void *addr = (void*)GetProcAddress(hMod, fname);
            if (!addr) {
                fprintf(stderr, "[JIT] Failed to resolve %s in %s\n", fname, dll);
                free(thunks);
                return 1;
            }
            thunks[thunk_count].addr = addr;
            thunk_count++;
        }
    }

    /* Allocate executable memory for thunk table + code */
    size_t thunk_size = thunk_count * 8;
    size_t total_size = thunk_size + info->code_size;
    /* Round up to page size */
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    size_t page_size = sysInfo.dwPageSize;
    size_t alloc_size = ((total_size + page_size - 1) / page_size) * page_size;

    void *exec_mem = VirtualAlloc(NULL, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!exec_mem) {
        fprintf(stderr, "[JIT] VirtualAlloc failed (error %lu)\n", GetLastError());
        free(thunks);
        return 1;
    }

    /* Fill thunk table at the start */
    uint64_t *thunk_table = (uint64_t*)exec_mem;
    for (int t = 0; t < thunk_count; t++) {
        thunk_table[t] = (uint64_t)(uintptr_t)thunks[t].addr;
    }

    /* Copy code after thunk table */
    uint8_t *code_base = (uint8_t*)exec_mem + thunk_size;
    memcpy(code_base, info->code, info->code_size);

    /* Patch FF 15 call sites to point to thunk table */
    for (int k = 0; k < info->import_call_count; k++) {
        const char *fname = info->import_names[k];
        int cs = info->import_call_sites[k];

        /* Find thunk index */
        int t_idx = -1;
        for (int t = 0; t < thunk_count; t++) {
            if (strcmp(thunks[t].name, fname) == 0) { t_idx = t; break; }
        }
        if (t_idx < 0) continue;

        uint64_t call_site_addr = (uint64_t)(uintptr_t)(code_base + cs);
        uint64_t thunk_entry_addr = (uint64_t)(uintptr_t)&thunk_table[t_idx];
        /* FF 15 is 6 bytes: FF 15 + 4-byte rel32 */
        int32_t disp32 = (int32_t)(thunk_entry_addr - (call_site_addr + 6));
        code_base[cs + 2] = (uint8_t)(disp32 & 0xFF);
        code_base[cs + 3] = (uint8_t)((disp32 >> 8) & 0xFF);
        code_base[cs + 4] = (uint8_t)((disp32 >> 16) & 0xFF);
        code_base[cs + 5] = (uint8_t)((disp32 >> 24) & 0xFF);
    }

    /* Prevent the code region from being freed on future allocations */
    DWORD oldProtect;
    VirtualProtect(exec_mem, alloc_size, PAGE_EXECUTE_READ, &oldProtect);

    free(thunks);

    /* Prepare argc and argv for main() */
    int jit_argc = 1;
    char jit_program_name[] = "program";
    char* jit_argv[2] = { jit_program_name, NULL };
    char** jit_argv_ptr = jit_argv;

    /* Initialize global variables argc and argv */
    int global_count = codegen_get_global_count(cg);
    printf("[JIT] Global count: %d\n", global_count);
    printf("[JIT] Code size: %d, total alloc: %zu\n", info->code_size, alloc_size);
    for (int i = 0; i < global_count; i++) {
        const char* name = codegen_get_global_name(cg, i);
        int offset = codegen_get_global_offset(cg, i);
        printf("[JIT] Global %d: name=%s, offset=%d\n", i, name, offset);
        if (offset < 0) continue;
        uint8_t* global_addr = code_base + info->code_size + offset;
        printf("[JIT]   Writing to address: %p\n", (void*)global_addr);
        if (strcmp(name, "argc") == 0) {
            *(int*)global_addr = jit_argc;
            printf("[JIT]   Set argc = %d\n", jit_argc);
        } else if (strcmp(name, "argv") == 0) {
            *(char***)global_addr = jit_argv_ptr;
            printf("[JIT]   Set argv = %p\n", (void*)jit_argv_ptr);
        }
    }

    /* Call entry point (main function) with argc/argv */
    printf("[JIT] Executing...\n");
    int (*entry)(int, char**) = (int (*)(int, char**))(code_base + info->entry_offset);
    int result = entry(jit_argc, jit_argv);

    printf("[JIT] Process exited with code %d\n", result);
    VirtualFree(exec_mem, 0, MEM_RELEASE);
    return result;

#else
    /* ---- Linux Emulator (embedded ELF/EXE emulator) ---- */
    (void)cg;

    if (info->entry_offset < 0) {
        fprintf(stderr, "[EMU] Entry point not found\n");
        return 1;
    }
    if (info->code_size == 0) {
        fprintf(stderr, "[EMU] No code generated\n");
        return 1;
    }

    /* Prepare argc and argv for main() */
    int jit_argc = 1;
    char jit_program_name[] = "program";
    char* jit_argv[2] = { jit_program_name, NULL };

    EmuContext emu;
    memset(&emu, 0, sizeof(emu));

    uint64_t entry_point = 0x400000 + info->entry_offset;
    if (emu_init(&emu, info->code, info->code_size, entry_point, jit_argc, jit_argv) != 0) {
        fprintf(stderr, "[EMU] Failed to initialize emulator\n");
        return 1;
    }

    printf("[EMU] Executing (entry: 0x%llx)...\n", (unsigned long long)entry_point);
    int result = emu_run(&emu);
    emu_free(&emu);

    printf("[EMU] Process exited with code %d\n", result);
    return result;
#endif
}
