#include "../include/elf.h"
#include "../include/token.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ELF64 types */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word   p_type;
    Elf64_Word   p_flags;
    Elf64_Off    p_offset;
    Elf64_Addr   p_vaddr;
    Elf64_Addr   p_paddr;
    Elf64_Xword  p_filesz;
    Elf64_Xword  p_memsz;
    Elf64_Xword  p_align;
} Elf64_Phdr;

typedef struct {
    Elf64_Word   st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half   st_shndx;
    Elf64_Addr   st_value;
    Elf64_Xword  st_size;
} Elf64_Sym;

typedef struct {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

typedef struct {
    Elf64_Sxword d_tag;
    Elf64_Xword  d_val;
} Elf64_Dyn;

/* Constants */
#define ET_EXEC       2
#define EM_X86_64     62
#define EV_CURRENT    1

#define PT_NULL       0
#define PT_LOAD       1
#define PT_DYNAMIC    2
#define PT_INTERP     3
#define PT_PHDR       6
#define PT_GNU_STACK  0x6474e551

#define PF_X          1
#define PF_W          2
#define PF_R          4

#define STB_GLOBAL    1
#define STT_FUNC      2
#define STV_DEFAULT   0
#define SHN_UNDEF     0

#define R_X86_64_GLOB_DAT  6

#define DT_NULL       0
#define DT_NEEDED     1
#define DT_SYMTAB     6
#define DT_SYMENT     11
#define DT_STRTAB     5
#define DT_STRSZ      10
#define DT_RELA       7
#define DT_RELASZ     8
#define DT_RELAENT    9

#define ELF64_ST_INFO(b,t) (((b) << 4) + ((t) & 0xF))
#define ELF64_R_INFO(s,t)  (((Elf64_Xword)(s) << 32) + (t))

#define PAGE_SIZE 0x1000
#define PAGE_ALIGN(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

static const char *interp_path = "/lib64/ld-linux-x86-64.so.2";

/* Collect unique import function names from GOTPCREL relocs */
static int collect_imports(Emitter *emit, char (**pnames)[64]) {
    int cap = 64;
    *pnames = (char(*)[64])malloc(cap * 64);
    int count = 0;
    for (int i = 0; i < emit->reloc_count; i++) {
        if (emit->relocs[i].type != RELOC_GOTPCREL) continue;
        int li = emit->relocs[i].label_idx;
        if (li < 0 || li >= emit->label_count) continue;
        const char *lname = emit->labels[li].name;
        if (lname[0] == '_') lname++;
        int found = 0;
        for (int j = 0; j < count; j++)
            if (strcmp((*pnames)[j], lname) == 0) { found = 1; break; }
        if (!found) {
            if (count >= cap) {
                cap *= 2;
                *pnames = (char(*)[64])realloc(*pnames, cap * 64);
            }
            strcpy_safe((*pnames)[count], lname);
            count++;
        }
    }
    return count;
}

/* Determine the library for a function name (for DT_NEEDED) */
static const char* func_library(const char *name) {
    if (strcmp(name, "LoadLibraryA") == 0 ||
        strcmp(name, "GetProcAddress") == 0 ||
        strcmp(name, "FreeLibrary") == 0 ||
        strcmp(name, "ExitProcess") == 0)
        return "kernel32.dll";
    return "libc.so.6";
}

/* Collect unique libraries */
static int collect_libraries(char (**plibs)[64], char (*funcs)[64], int func_count) {
    int cap = 16;
    *plibs = (char(*)[64])malloc(cap * 64);
    int count = 0;
    for (int i = 0; i < func_count; i++) {
        const char *lib = func_library(funcs[i]);
        if (strcmp(lib, "kernel32.dll") == 0) continue; /* skip Windows-only */
        int found = 0;
        for (int j = 0; j < count; j++)
            if (strcmp((*plibs)[j], lib) == 0) { found = 1; break; }
        if (!found) {
            if (count >= cap) {
                cap *= 2;
                *plibs = (char(*)[64])realloc(*plibs, cap * 64);
            }
            strcpy_safe((*plibs)[count], lib);
            count++;
        }
    }
    return count;
}

int elf_write(const char *path, PEInfo *info, Emitter *emit) {
    /* Collect imports */
    char (*import_funcs)[64] = NULL;
    int import_count = collect_imports(emit, &import_funcs);

    char (*libraries)[64] = NULL;
    int lib_count = 0;

    /* --- Static ELF (no imports) --- */
    if (import_count == 0) {
        FILE *f = fopen(path, "wb");
        if (!f) return -1;

        uint64_t vaddr = 0x400000;
        uint64_t code_size = info->code_size;
        uint64_t code_pad = PAGE_ALIGN(code_size);

        /* Headers occupy one page */
        int ehdr_size = sizeof(Elf64_Ehdr);
        int phdr_count = 2; /* PT_LOAD + PT_GNU_STACK */
        int phdr_size = phdr_count * sizeof(Elf64_Phdr);
        int hdr_size = ehdr_size + phdr_size;
        int hdr_pad = PAGE_ALIGN(hdr_size);

        /* ELF header */
        Elf64_Ehdr ehdr;
        memset(&ehdr, 0, sizeof(ehdr));
        ehdr.e_ident[0] = 0x7F; ehdr.e_ident[1] = 'E'; ehdr.e_ident[2] = 'L'; ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = 2; ehdr.e_ident[5] = 1; ehdr.e_ident[6] = EV_CURRENT;
        ehdr.e_type = ET_EXEC; ehdr.e_machine = EM_X86_64; ehdr.e_version = EV_CURRENT;
        ehdr.e_entry = vaddr + hdr_pad + info->entry_offset;
        ehdr.e_phoff = sizeof(Elf64_Ehdr); ehdr.e_phnum = phdr_count;
        ehdr.e_ehsize = sizeof(Elf64_Ehdr); ehdr.e_phentsize = sizeof(Elf64_Phdr);
        fwrite(&ehdr, 1, sizeof(ehdr), f);

        /* PT_LOAD */
        {
            Elf64_Phdr phdr;
            memset(&phdr, 0, sizeof(phdr));
            phdr.p_type = PT_LOAD;
            phdr.p_flags = PF_R | PF_X;
            phdr.p_offset = hdr_pad;
            phdr.p_vaddr = vaddr + hdr_pad;
            phdr.p_paddr = vaddr + hdr_pad;
            phdr.p_filesz = code_size;
            phdr.p_memsz = code_pad;
            phdr.p_align = PAGE_SIZE;
            fwrite(&phdr, 1, sizeof(phdr), f);
        }

        /* PT_GNU_STACK */
        {
            Elf64_Phdr phdr;
            memset(&phdr, 0, sizeof(phdr));
            phdr.p_type = PT_GNU_STACK;
            phdr.p_flags = PF_R | PF_W;
            phdr.p_offset = 0; phdr.p_vaddr = 0; phdr.p_paddr = 0;
            phdr.p_filesz = 0; phdr.p_memsz = 0; phdr.p_align = 16;
            fwrite(&phdr, 1, sizeof(phdr), f);
        }

        /* Pad to text offset */
        {
            long cur = ftell(f);
            while (cur < (long)hdr_pad) { fputc(0, f); cur++; }
        }

        /* Write code */
        fwrite(info->code, 1, code_size, f);
        /* Pad */
        {
            long cur = ftell(f);
            while (cur < (long)(hdr_pad + code_pad)) { fputc(0, f); cur++; }
        }

        fclose(f);
        return 0;
    }

    /* --- Dynamic ELF (with imports) --- */
    lib_count = collect_libraries(&libraries, import_funcs, import_count);

    /* Compute dynamic strtab offsets for libraries */
    int *lib_str_offs = (int*)calloc(lib_count, sizeof(int));
    int dynstr_size = 0;
    for (int i = 0; i < lib_count; i++) {
        lib_str_offs[i] = dynstr_size;
        dynstr_size += strlen(libraries[i]) + 1;
    }

    /* Dynamic strtab offsets for function names */
    int *func_str_offs = (int*)calloc(import_count, sizeof(int));
    for (int i = 0; i < import_count; i++) {
        func_str_offs[i] = dynstr_size;
        dynstr_size += strlen(import_funcs[i]) + 1;
    }

    /* Sizes */
    int interp_len = strlen(interp_path) + 1;
    int interp_pad = PAGE_ALIGN(interp_len);

    int code_size = info->code_size;
    int code_pad  = PAGE_ALIGN(code_size);

    int got_entries = import_count + 1; /* +1 NULL entry */
    int got_size = got_entries * 8;

    int sym_count = import_count + 1;
    int dynsym_size = sym_count * sizeof(Elf64_Sym);

    int rela_count = import_count;
    int rela_size = rela_count * sizeof(Elf64_Rela);

    int dyn_entries = lib_count + 9; /* DT_NEEDEDs + 8 fixed + DT_NULL */
    int dyn_size = dyn_entries * sizeof(Elf64_Dyn);

    /* Layout */
    const uint64_t base_vaddr = 0x400000;

    int ehdr_size = sizeof(Elf64_Ehdr);
    int phdr_count = 6; /* PHDR + INTERP + LOAD(text) + LOAD(data) + DYNAMIC + STACK */
    int phdr_size = phdr_count * sizeof(Elf64_Phdr);

    int interp_file_off = ehdr_size + phdr_size;
    int hdr_end = interp_file_off + interp_pad;
    int hdr_pad = PAGE_ALIGN(hdr_end);

    uint64_t text_file_off = hdr_pad;
    uint64_t text_vaddr = base_vaddr + text_file_off;

    uint64_t data_file_off = text_file_off + code_pad;
    uint64_t data_vaddr = base_vaddr + data_file_off;

    uint64_t got_vaddr   = data_vaddr;
    uint64_t dynsym_vaddr = data_vaddr + got_size;
    uint64_t dynstr_vaddr = dynsym_vaddr + dynsym_size;
    uint64_t rela_vaddr   = dynstr_vaddr + dynstr_size;
    uint64_t dyn_vaddr    = rela_vaddr + rela_size;

    uint64_t data_end_file = got_size + dynsym_size + dynstr_size + rela_size + dyn_size;
    uint64_t data_pad = PAGE_ALIGN(data_end_file);

    FILE *f = fopen(path, "wb");
    if (!f) { free(lib_str_offs); free(func_str_offs); return -1; }

    /* ELF header */
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = 0x7F; ehdr.e_ident[1] = 'E'; ehdr.e_ident[2] = 'L'; ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; ehdr.e_ident[5] = 1; ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_type = ET_EXEC; ehdr.e_machine = EM_X86_64; ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = text_vaddr + info->entry_offset;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_phnum = phdr_count;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    fwrite(&ehdr, 1, sizeof(ehdr), f);

    /* PT_PHDR */
    {
        Elf64_Phdr phdr;
        memset(&phdr, 0, sizeof(phdr));
        phdr.p_type = PT_PHDR;
        phdr.p_flags = PF_R;
        phdr.p_offset = ehdr.e_phoff;
        phdr.p_vaddr = base_vaddr + ehdr.e_phoff;
        phdr.p_paddr = phdr.p_vaddr;
        phdr.p_filesz = phdr_size;
        phdr.p_memsz = phdr.p_filesz;
        phdr.p_align = 8;
        fwrite(&phdr, 1, sizeof(phdr), f);
    }

    /* PT_INTERP */
    {
        Elf64_Phdr phdr;
        memset(&phdr, 0, sizeof(phdr));
        phdr.p_type = PT_INTERP;
        phdr.p_flags = PF_R;
        phdr.p_offset = interp_file_off;
        phdr.p_vaddr = base_vaddr + interp_file_off;
        phdr.p_paddr = phdr.p_vaddr;
        phdr.p_filesz = interp_len;
        phdr.p_memsz = interp_len;
        phdr.p_align = 1;
        fwrite(&phdr, 1, sizeof(phdr), f);
    }

    /* PT_LOAD: text (RX) */
    {
        Elf64_Phdr phdr;
        memset(&phdr, 0, sizeof(phdr));
        phdr.p_type = PT_LOAD;
        phdr.p_flags = PF_R | PF_X;
        phdr.p_offset = text_file_off;
        phdr.p_vaddr = text_vaddr;
        phdr.p_paddr = text_vaddr;
        phdr.p_filesz = code_size;
        phdr.p_memsz = code_pad;
        phdr.p_align = PAGE_SIZE;
        fwrite(&phdr, 1, sizeof(phdr), f);
    }

    /* PT_LOAD: data (RW) */
    {
        Elf64_Phdr phdr;
        memset(&phdr, 0, sizeof(phdr));
        phdr.p_type = PT_LOAD;
        phdr.p_flags = PF_R | PF_W;
        phdr.p_offset = data_file_off;
        phdr.p_vaddr = data_vaddr;
        phdr.p_paddr = data_vaddr;
        phdr.p_filesz = data_end_file;
        phdr.p_memsz = data_pad;
        phdr.p_align = PAGE_SIZE;
        fwrite(&phdr, 1, sizeof(phdr), f);
    }

    /* PT_DYNAMIC */
    {
        Elf64_Phdr phdr;
        memset(&phdr, 0, sizeof(phdr));
        phdr.p_type = PT_DYNAMIC;
        phdr.p_flags = PF_R | PF_W;
        phdr.p_offset = data_file_off + (dyn_vaddr - data_vaddr);
        phdr.p_vaddr = dyn_vaddr;
        phdr.p_paddr = dyn_vaddr;
        phdr.p_filesz = dyn_size;
        phdr.p_memsz = dyn_size;
        phdr.p_align = 8;
        fwrite(&phdr, 1, sizeof(phdr), f);
    }

    /* PT_GNU_STACK */
    {
        Elf64_Phdr phdr;
        memset(&phdr, 0, sizeof(phdr));
        phdr.p_type = PT_GNU_STACK;
        phdr.p_flags = PF_R | PF_W;
        phdr.p_align = 16;
        fwrite(&phdr, 1, sizeof(phdr), f);
    }

    /* .interp */
    {
        long cur = ftell(f);
        while (cur < (long)interp_file_off) { fputc(0, f); cur++; }
    }
    fwrite(interp_path, 1, interp_len, f);
    /* Pad interp to page */
    {
        long cur = ftell(f);
        while (cur < (long)hdr_pad) { fputc(0, f); cur++; }
    }

    /* .text: code */
    fwrite(info->code, 1, code_size, f);
    /* Pad text to page */
    {
        long cur = ftell(f);
        while (cur < (long)(text_file_off + code_pad)) { fputc(0, f); cur++; }
    }

    /* .got: all zeros (filled by dynamic linker) */
    for (int i = 0; i < got_entries; i++) {
        uint64_t zero = 0;
        fwrite(&zero, 1, 8, f);
    }

    /* .dynsym: NULL entry + one per import */
    {
        Elf64_Sym null_sym;
        memset(&null_sym, 0, sizeof(null_sym));
        fwrite(&null_sym, 1, sizeof(null_sym), f);
        for (int i = 0; i < import_count; i++) {
            Elf64_Sym sym;
            memset(&sym, 0, sizeof(sym));
            sym.st_name = func_str_offs[i];
            sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
            sym.st_other = STV_DEFAULT;
            sym.st_shndx = SHN_UNDEF;
            fwrite(&sym, 1, sizeof(sym), f);
        }
    }

    /* .dynstr: library names + function names */
    for (int i = 0; i < lib_count; i++)
        fwrite(libraries[i], 1, strlen(libraries[i]) + 1, f);
    for (int i = 0; i < import_count; i++)
        fwrite(import_funcs[i], 1, strlen(import_funcs[i]) + 1, f);

    /* .rela.dyn: one entry per import */
    for (int i = 0; i < import_count; i++) {
        Elf64_Rela rela;
        memset(&rela, 0, sizeof(rela));
        rela.r_offset = got_vaddr + (i + 1) * 8;
        rela.r_info = ELF64_R_INFO(i + 1, R_X86_64_GLOB_DAT);
        rela.r_addend = 0;
        fwrite(&rela, 1, sizeof(rela), f);
    }

    /* .dynamic section */
    for (int i = 0; i < lib_count; i++) {
        Elf64_Dyn dyn;
        dyn.d_tag = DT_NEEDED;
        dyn.d_val = lib_str_offs[i];
        fwrite(&dyn, 1, sizeof(dyn), f);
    }
    {
        Elf64_Dyn dyn;
        dyn.d_tag = DT_SYMTAB; dyn.d_val = dynsym_vaddr; fwrite(&dyn, 1, sizeof(dyn), f);
        dyn.d_tag = DT_SYMENT; dyn.d_val = sizeof(Elf64_Sym); fwrite(&dyn, 1, sizeof(dyn), f);
        dyn.d_tag = DT_STRTAB; dyn.d_val = dynstr_vaddr; fwrite(&dyn, 1, sizeof(dyn), f);
        dyn.d_tag = DT_STRSZ; dyn.d_val = dynstr_size; fwrite(&dyn, 1, sizeof(dyn), f);
        dyn.d_tag = DT_RELA; dyn.d_val = rela_vaddr; fwrite(&dyn, 1, sizeof(dyn), f);
        dyn.d_tag = DT_RELASZ; dyn.d_val = rela_size; fwrite(&dyn, 1, sizeof(dyn), f);
        dyn.d_tag = DT_RELAENT; dyn.d_val = sizeof(Elf64_Rela); fwrite(&dyn, 1, sizeof(dyn), f);
        dyn.d_tag = DT_NULL; dyn.d_val = 0; fwrite(&dyn, 1, sizeof(dyn), f);
    }

    /* Pad data segment */
    {
        long cur = ftell(f);
        while (cur < (long)(data_file_off + data_pad)) { fputc(0, f); cur++; }
    }

    /* Patch GOTPCREL call sites */
    for (int i = 0; i < emit->reloc_count; i++) {
        if (emit->relocs[i].type != RELOC_GOTPCREL) continue;
        int li = emit->relocs[i].label_idx;
        if (li < 0 || li >= emit->label_count) continue;
        const char *lname = emit->labels[li].name;
        if (lname[0] == '_') lname++;

        int got_idx = -1;
        for (int j = 0; j < import_count; j++)
            if (strcmp(import_funcs[j], lname) == 0) { got_idx = j + 1; break; }
        if (got_idx < 0) continue;

        int reloc_off = emit->relocs[i].offset;
        uint64_t call_end_vaddr = text_vaddr + reloc_off;
        uint64_t got_entry_vaddr = got_vaddr + got_idx * 8;

        int32_t disp32 = (int32_t)(got_entry_vaddr - call_end_vaddr);
        info->code[reloc_off]     = (uint8_t)(disp32 & 0xFF);
        info->code[reloc_off + 1] = (uint8_t)((disp32 >> 8) & 0xFF);
        info->code[reloc_off + 2] = (uint8_t)((disp32 >> 16) & 0xFF);
        info->code[reloc_off + 3] = (uint8_t)((disp32 >> 24) & 0xFF);
    }

    free(lib_str_offs);
    free(func_str_offs);
    free(import_funcs);
    free(libraries);
    fclose(f);
    return 0;
}
