#ifndef PE_H
#define PE_H

#include <stdint.h>
#include <stdio.h>

/* Структура импорта */
typedef struct {
    const char *dll_name;    /* например "msvcrt.dll" */
    const char **func_names;
    int func_count;
    int func_capacity;
} ImportDescriptor;

/* Параметры для создания PE */
typedef struct {
    uint8_t *code;
    int code_size;
    int code_aligned;

    uint8_t *data;
    int data_size;
    int data_aligned;

    /* Точка входа (смещение в code) */
    int entry_offset;

    /* Импорты */
    ImportDescriptor *imports;
    int import_count;
    int import_capacity;

    /* Import call sites (FF 15 call [rip+...] positions in code to patch) */
    int *import_call_sites;
    char (*import_names)[64];
    int import_call_count;
    int import_call_capacity;

    /* Метаданные PE (Version Information) */
    char company[128];
    char copyright[256];
    char product_version[32];
    char file_version[32];
    char product_name[128];
    char file_description[256];

    /* UAC trust info */
    char requested_execution_level[32];
    int ui_access;
} PEInfo;

/* Записать PE-файл */
int pe_write(const char *path, PEInfo *info);

#endif
