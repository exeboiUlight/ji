#ifndef PROJECT_H
#define PROJECT_H

#define PROJ_NAME_MAX 128
#define PROJ_VER_MAX 32
#define PROJ_AUTHOR_MAX 128
#define PROJ_DESC_MAX 256
#define PROJ_ICON_MAX 256
#define PROJ_COMPANY_MAX 128
#define PROJ_COPYRIGHT_MAX 256
#define PROJ_EXEC_LEVEL_MAX 32
#define PROJ_CATEGORIES_MAX 256
#define PROJ_COMMENT_MAX 256

typedef struct {
    char name[PROJ_NAME_MAX];
    char version[PROJ_VER_MAX];
    char author[PROJ_AUTHOR_MAX];
    char description[PROJ_DESC_MAX];
    char icon[PROJ_ICON_MAX];

    struct {
        char company[PROJ_COMPANY_MAX];
        char copyright[PROJ_COPYRIGHT_MAX];
        char product_version[PROJ_VER_MAX];
        char file_version[PROJ_VER_MAX];
        struct {
            char requested_execution_level[PROJ_EXEC_LEVEL_MAX];
            int ui_access;
        } trust_info;
    } windows;

    struct {
        struct {
            char categories[PROJ_CATEGORIES_MAX];
            char comment[PROJ_COMMENT_MAX];
        } desktop_entry;
    } linux;
} ProjectConfig;

int project_create(const char *name);
int project_load(const char *dir, ProjectConfig *cfg);
void project_print(const ProjectConfig *cfg);

#endif
