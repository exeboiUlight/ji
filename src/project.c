#include "../include/project.h"
#include "../include/token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_p(path) _mkdir(path)
#else
#include <sys/stat.h>
#define mkdir_p(path) mkdir(path, 0755)
#endif

/* Minimal JSON string value extraction */
static int json_str_value(const char *json, const char *key, char *out, int out_size) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) { out[0] = '\0'; return 0; }
    p = strchr(p, ':');
    if (!p) { out[0] = '\0'; return 0; }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') { out[0] = '\0'; return 0; }
    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p+1)) { p++; }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static int json_bool_value(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return (strncmp(p, "true", 4) == 0);
}

int project_create(const char *name) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", name);

#ifdef _WIN32
    _mkdir(name);
#else
    mkdir(name, 0755);
#endif

    char src_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src", name);
    mkdir_p(src_dir);

    /* Write project.json */
    char proj_path[512];
    snprintf(proj_path, sizeof(proj_path), "%s/project.json", name);
    FILE *f = fopen(proj_path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot create %s\n", proj_path);
        return 1;
    }
    fprintf(f, "{\n");
    fprintf(f, "    \"name\": \"%s\",\n", name);
    fprintf(f, "    \"version\": \"0.1.0\",\n");
    fprintf(f, "    \"author\": \"\",\n");
    fprintf(f, "    \"description\": \"\",\n");
    fprintf(f, "    \"icon\": \"\",\n");
    fprintf(f, "\n");
    fprintf(f, "    \"windows\": {\n");
    fprintf(f, "        \"company\": \"\",\n");
    fprintf(f, "        \"copyright\": \"\",\n");
    fprintf(f, "        \"product_version\": \"0.1.0\",\n");
    fprintf(f, "        \"file_version\": \"0.1.0\",\n");
    fprintf(f, "        \"trust_info\": {\n");
    fprintf(f, "            \"requested_execution_level\": \"asInvoker\",\n");
    fprintf(f, "            \"ui_access\": false\n");
    fprintf(f, "        }\n");
    fprintf(f, "    },\n");
    fprintf(f, "\n");
    fprintf(f, "    \"linux\": {\n");
    fprintf(f, "        \"desktop_entry\": {\n");
    fprintf(f, "            \"categories\": \"\",\n");
    fprintf(f, "            \"comment\": \"\"\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "}\n");
    fclose(f);

    /* Write src/main.ji */
    char main_path[512];
    snprintf(main_path, sizeof(main_path), "%s/src/main.ji", name);
    f = fopen(main_path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot create %s\n", main_path);
        return 1;
    }
    fprintf(f, "import \"io.ji\";\n\n");
    fprintf(f, "int main() {\n");
    fprintf(f, "    printf(\"Hello from '%s'!\\n\");\n", name);
    fprintf(f, "\n");
    fprintf(f, "    return 0;\n");
    fprintf(f, "}\n");
    fclose(f);

    /* Write src/io.ji */
    char io_path[512];
    snprintf(io_path, sizeof(io_path), "%s/src/io.ji", name);
    f = fopen(io_path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot create %s\n", io_path);
        return 1;
    }
    fprintf(f, "// io.ji - System, IO, and memory functions for JI\n");
    fprintf(f, "// All functions are automatically linked to msvcrt.dll (Windows) or libc.so.6 (Linux)\n\n");
    fprintf(f, "int printf(char* fmt);\n");
    fprintf(f, "int scanf(char* fmt);\n");
    fprintf(f, "int puts(char* str);\n");
    fprintf(f, "char* gets(char* buf);\n");
    fprintf(f, "void* fopen(char* filename, char* mode);\n");
    fprintf(f, "int fclose(void* file);\n");
    fprintf(f, "void* malloc(int size);\n");
    fprintf(f, "void free(void* ptr);\n");
    fprintf(f, "void exit(int code);\n");
    fclose(f);

    printf("[Project] Created project '%s'\n", name);
    printf("  %s\n", proj_path);
    printf("  %s\n", main_path);
    printf("  %s\n", io_path);
    return 0;
}

int project_load(const char *dir, ProjectConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    char path[512];
    snprintf(path, sizeof(path), "%s/project.json", dir);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = (char*)malloc(sz + 1);
    if (!json) { fclose(f); return 1; }
    fread(json, 1, sz, f);
    json[sz] = '\0';
    fclose(f);

    /* Parse top-level fields */
    json_str_value(json, "name", cfg->name, sizeof(cfg->name));
    json_str_value(json, "version", cfg->version, sizeof(cfg->version));
    json_str_value(json, "author", cfg->author, sizeof(cfg->author));
    json_str_value(json, "description", cfg->description, sizeof(cfg->description));
    json_str_value(json, "icon", cfg->icon, sizeof(cfg->icon));

    /* Parse windows section */
    const char *ws = strstr(json, "\"windows\"");
    if (ws) {
        json_str_value(ws, "company", cfg->windows.company, sizeof(cfg->windows.company));
        json_str_value(ws, "copyright", cfg->windows.copyright, sizeof(cfg->windows.copyright));
        json_str_value(ws, "product_version", cfg->windows.product_version, sizeof(cfg->windows.product_version));
        json_str_value(ws, "file_version", cfg->windows.file_version, sizeof(cfg->windows.file_version));
        const char *ti = strstr(ws, "trust_info");
        if (ti) {
            json_str_value(ti, "requested_execution_level", cfg->windows.trust_info.requested_execution_level,
                          sizeof(cfg->windows.trust_info.requested_execution_level));
            cfg->windows.trust_info.ui_access = json_bool_value(ti, "ui_access");
        }
    }

    /* Parse linux section */
    const char *ls = strstr(json, "\"linux\"");
    if (ls) {
        const char *de = strstr(ls, "desktop_entry");
        if (de) {
            json_str_value(de, "categories", cfg->linux.desktop_entry.categories,
                          sizeof(cfg->linux.desktop_entry.categories));
            json_str_value(de, "comment", cfg->linux.desktop_entry.comment,
                          sizeof(cfg->linux.desktop_entry.comment));
        }
    }

    free(json);
    return 0;
}

void project_print(const ProjectConfig *cfg) {
    printf("Project: %s\n", cfg->name);
    printf("  Version: %s\n", cfg->version);
    printf("  Author: %s\n", cfg->author);
    printf("  Description: %s\n", cfg->description);
    printf("  Icon: %s\n", cfg->icon);
    printf("  Windows:\n");
    printf("    Company: %s\n", cfg->windows.company);
    printf("    Copyright: %s\n", cfg->windows.copyright);
    printf("    Product Version: %s\n", cfg->windows.product_version);
    printf("    File Version: %s\n", cfg->windows.file_version);
    printf("    Execution Level: %s\n", cfg->windows.trust_info.requested_execution_level);
    printf("    UI Access: %s\n", cfg->windows.trust_info.ui_access ? "true" : "false");
    printf("  Linux:\n");
    printf("    Categories: %s\n", cfg->linux.desktop_entry.categories);
    printf("    Comment: %s\n", cfg->linux.desktop_entry.comment);
}
