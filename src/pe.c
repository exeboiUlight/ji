#include "../include/pe.h"
#include <stdlib.h>
#include <string.h>

/* IMAGE_DOS_HEADER */
typedef struct {
    uint16_t e_magic;       /* MZ */
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;     /* offset to PE header */
} DOSHeader;

/* PE signature */
static const uint32_t PE_SIGNATURE = 0x00004550; /* "PE\0\0" */

/* IMAGE_FILE_HEADER */
typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} COFFHeader;

/* IMAGE_OPTIONAL_HEADER64 (PE32+) */
typedef struct {
    uint16_t Magic;            /* 0x020B */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;  /* RVA */
    uint32_t BaseOfCode;          /* RVA */
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;         /* 3 = CONSOLE */
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    /* DATA_DIRECTORY[16] */
    uint32_t ExportRVA, ExportSize;
    uint32_t ImportRVA, ImportSize;
    uint32_t ResourceRVA, ResourceSize;
    uint32_t ExceptionRVA, ExceptionSize;
    uint32_t SecurityRVA, SecuritySize;
    uint32_t RelocRVA, RelocSize;
    uint32_t DebugRVA, DebugSize;
    uint32_t ArchRVA, ArchSize;
    uint32_t GlobalPtrRVA, GlobalPtrSize;
    uint32_t TLSRVA, TLSSize;
    uint32_t LoadConfigRVA, LoadConfigSize;
    uint32_t BoundImportRVA, BoundImportSize;
    uint32_t IATRVA, IATSize;
    uint32_t DelayImportRVA, DelayImportSize;
    uint32_t COMPlusRVA, COMPlusSize;
    uint32_t ReservedRVA, ReservedSize;
} OptHeader64;

/* IMAGE_SECTION_HEADER */
typedef struct {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} SectionHeader;

static inline uint32_t align_up(uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
}

static inline uint32_t rva_to_offset(uint32_t rva, uint32_t section_rva, uint32_t section_offset) {
    return rva - section_rva + section_offset;
}

/* Запись PE32+ для x64 */
int pe_write(const char *path, PEInfo *info) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    const uint32_t sect_align = 0x1000;
    const uint32_t file_align = 0x200;

    /* Размеры заголовков */
    uint32_t dos_size = sizeof(DOSHeader);
    uint32_t stub_size = 64; /* DOS stub */
    uint32_t pe_sig_size = 4;
    uint32_t coff_size = sizeof(COFFHeader);
    uint32_t opt_size = sizeof(OptHeader64);

    /* Вычисляем размер rdata (импорты) */
    uint32_t import_dir_size = 0;
    uint32_t thunk_data_size = 0;
    uint32_t hint_name_size = 0;
    uint32_t dll_name_size = 0;
    uint32_t func_name_size = 0;

    for (int i = 0; i < info->import_count; i++) {
        import_dir_size += sizeof(uint32_t) * 5; /* 5 DWORDs per IMAGE_IMPORT_DESCRIPTOR */
        thunk_data_size += (info->imports[i].func_count + 1) * sizeof(uint64_t); /* OFT */
        thunk_data_size += (info->imports[i].func_count + 1) * sizeof(uint64_t); /* FT */
        dll_name_size += strlen(info->imports[i].dll_name) + 1;
        for (int j = 0; j < info->imports[i].func_count; j++) {
            hint_name_size += 2 + strlen(info->imports[i].func_names[j]) + 1; /* hint + name + null */
            hint_name_size = (hint_name_size + 1) & ~1; /* align to word */
        }
    }

    /* Null terminator IMAGE_IMPORT_DESCRIPTOR */
    import_dir_size += sizeof(uint32_t) * 5; /* 5 DWORDs = 20 bytes */

    uint32_t raw_rdata_size = import_dir_size + thunk_data_size + hint_name_size + dll_name_size + func_name_size;

    /* 3 секции: .text, .rdata, maybe .data */
    int section_count = 3;
    if (info->data_size == 0) section_count = 2;

    /* Вычисляем RVAs */
    uint32_t text_rva = sect_align; /* после заголовков */
    uint32_t text_raw_size = align_up(info->code_size, file_align);
    uint32_t text_virt_size = align_up(info->code_size, sect_align);

    uint32_t rdata_rva = text_rva + align_up(text_virt_size, sect_align);
    uint32_t rdata_raw_size = align_up(raw_rdata_size, file_align);
    uint32_t rdata_virt_size = align_up(raw_rdata_size, sect_align);

    uint32_t data_rva = 0, data_raw_size = 0, data_virt_size = 0;
    if (section_count > 2) {
        data_rva = rdata_rva + align_up(rdata_virt_size, sect_align);
        data_raw_size = align_up(info->data_size, file_align);
        data_virt_size = align_up(info->data_size, sect_align);
    }

    /* Размер образа */
    uint32_t image_size;
    if (section_count > 2)
        image_size = data_rva + data_virt_size;
    else
        image_size = rdata_rva + rdata_virt_size;
    image_size = align_up(image_size, sect_align);

    /* Размер заголовков */
    uint32_t headers_size = align_up(dos_size + stub_size + pe_sig_size + coff_size + opt_size
        + section_count * sizeof(SectionHeader), file_align);

    /* === DOS HEADER === */
    DOSHeader dos;
    memset(&dos, 0, sizeof(dos));
    dos.e_magic = 0x5A4D; /* MZ */
    dos.e_cblp = 0x90;
    dos.e_cp = 3;
    dos.e_crlc = 0;
    dos.e_cparhdr = 4;
    dos.e_minalloc = 0;
    dos.e_maxalloc = 0xFFFF;
    dos.e_ss = 0;
    dos.e_sp = 0xB8;
    dos.e_csum = 0;
    dos.e_ip = 0;
    dos.e_cs = 0;
    dos.e_lfarlc = 0x40;
    dos.e_ovno = 0;
    dos.e_res[0] = 0; dos.e_res[1] = 0; dos.e_res[2] = 0; dos.e_res[3] = 0;
    dos.e_oemid = 0;
    dos.e_oeminfo = 0;
    memset(dos.e_res2, 0, sizeof(dos.e_res2));
    dos.e_lfanew = dos_size + stub_size;
    fwrite(&dos, 1, sizeof(dos), f);

    /* DOS stub */
    {
        const uint8_t stub[] = {
            0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21,
            0x54, 0x68, 0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63,
            0x61, 0x6E, 0x6E, 0x6F, 0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69,
            0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20, 0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A,
            0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        fwrite(stub, 1, sizeof(stub), f);
    }

    /* PE signature */
    fwrite(&PE_SIGNATURE, 1, 4, f);

    /* COFF HEADER */
    {
        COFFHeader coff;
        memset(&coff, 0, sizeof(coff));
        coff.Machine = 0x8664; /* AMD64 */
        coff.NumberOfSections = section_count;
        coff.SizeOfOptionalHeader = opt_size;
        coff.Characteristics = 0x0022; /* EXE + large address aware + 32-bit (for 64-bit too) */
        /* 0x002 = EXE, 0x0020 = large address aware */
        fwrite(&coff, 1, sizeof(coff), f);
    }

    /* OPTIONAL HEADER PE32+ */
    {
        OptHeader64 opt;
        memset(&opt, 0, sizeof(opt));
        opt.Magic = 0x020B;
        opt.MajorLinkerVersion = 1;
        opt.MinorLinkerVersion = 0;
        opt.SizeOfCode = text_raw_size;
        opt.SizeOfInitializedData = rdata_raw_size + data_raw_size;
        opt.SizeOfUninitializedData = 0;
        opt.AddressOfEntryPoint = text_rva + info->entry_offset;
        opt.BaseOfCode = text_rva;
        opt.ImageBase = 0x0000000140000000ULL;
        opt.SectionAlignment = sect_align;
        opt.FileAlignment = file_align;
        opt.MajorOperatingSystemVersion = 5;
        opt.MinorOperatingSystemVersion = 1;
        opt.MajorImageVersion = 0;
        opt.MinorImageVersion = 0;
        opt.MajorSubsystemVersion = 5;
        opt.MinorSubsystemVersion = 1;
        opt.SizeOfImage = image_size;
        opt.SizeOfHeaders = headers_size;
        opt.CheckSum = 0;
        opt.Subsystem = 3; /* CONSOLE */
        opt.DllCharacteristics = 0x0100; /* NX compatible */
        opt.SizeOfStackReserve = 0x100000ULL;
        opt.SizeOfStackCommit = 0x1000ULL;
        opt.SizeOfHeapReserve = 0x100000ULL;
        opt.SizeOfHeapCommit = 0x1000ULL;
        opt.NumberOfRvaAndSizes = 16;

        /* Если есть импорты, заполняем Import Directory */
        if (info->import_count > 0) {
            opt.ImportRVA = rdata_rva;
            opt.ImportSize = import_dir_size;
        }

        fwrite(&opt, 1, sizeof(opt), f);
    }

    /* SECTION HEADERS */
    off_t text_raw_off = headers_size;
    off_t rdata_raw_off = text_raw_off + text_raw_size;
    off_t data_raw_off = rdata_raw_off + rdata_raw_size;

    /* .text section */
    {
        SectionHeader sh;
        memset(&sh, 0, sizeof(sh));
        memcpy(sh.Name, ".text", 5);
        sh.VirtualSize = text_virt_size;
        sh.VirtualAddress = text_rva;
        sh.SizeOfRawData = text_raw_size;
        sh.PointerToRawData = text_raw_off;
        sh.Characteristics = 0x60000020; /* CODE | EXECUTE | READ */
        fwrite(&sh, 1, sizeof(sh), f);
    }

    /* .rdata section */
    {
        SectionHeader sh;
        memset(&sh, 0, sizeof(sh));
        memcpy(sh.Name, ".rdata", 6);
        sh.VirtualSize = rdata_virt_size;
        sh.VirtualAddress = rdata_rva;
        sh.SizeOfRawData = rdata_raw_size;
        sh.PointerToRawData = rdata_raw_off;
        sh.Characteristics = 0x40000040; /* INITIALIZED_DATA | READ */
        fwrite(&sh, 1, sizeof(sh), f);
    }

    /* .data section */
    if (section_count > 2) {
        SectionHeader sh;
        memset(&sh, 0, sizeof(sh));
        memcpy(sh.Name, ".data", 5);
        sh.VirtualSize = data_virt_size;
        sh.VirtualAddress = data_rva;
        sh.SizeOfRawData = data_raw_size;
        sh.PointerToRawData = data_raw_off;
        sh.Characteristics = 0xC0000040; /* INITIALIZED_DATA | READ | WRITE */
        fwrite(&sh, 1, sizeof(sh), f);
    }

    /* Выравнивание до .text */
    {
        long cur = ftell(f);
        while (cur < (long)text_raw_off) {
            fputc(0, f);
            cur++;
        }
    }

    /* Build import data and patch call sites BEFORE writing .text */
    uint8_t *rdata_buf = NULL;
    int *oft_offsets = NULL, *ft_offsets = NULL, *dllname_offsets = NULL;
    uint32_t *hn_rvas = NULL;
    int ft_off = 0, oft_start = 0;

    if (info->import_count > 0) {
        rdata_buf = (uint8_t*)calloc(1, raw_rdata_size);
        int rp = 0;

        int import_dir_start = rp;
        oft_offsets = (int*)calloc(info->import_count, sizeof(int));
        ft_offsets = (int*)calloc(info->import_count, sizeof(int));
        dllname_offsets = (int*)calloc(info->import_count, sizeof(int));

        for (int i = 0; i < info->import_count; i++) {
            rp += 5 * 4;
        }
        rp += 5 * 4;

        /* OFT */
        oft_start = rp;
        int cum_oft = 0;
        for (int i = 0; i < info->import_count; i++) {
            oft_offsets[i] = cum_oft;
            int entry_size = (info->imports[i].func_count + 1) * 8;
            rp += entry_size;
            cum_oft += entry_size;
        }

        /* FT */
        ft_off = rp;
        int cum_ft = 0;
        for (int i = 0; i < info->import_count; i++) {
            ft_offsets[i] = cum_ft;
            int entry_size = (info->imports[i].func_count + 1) * 8;
            rp += entry_size;
            cum_ft += entry_size;
        }

        /* Hint/Name + DLL names */
        hn_rvas = (uint32_t*)calloc(info->import_count * 64, sizeof(uint32_t));
        int hn_idx = 0;

        for (int i = 0; i < info->import_count; i++) {
            dllname_offsets[i] = rp - import_dir_start;
            const char *dll = info->imports[i].dll_name;
            int len = strlen(dll) + 1;
            memcpy(rdata_buf + rp, dll, len);
            rp += len;

            for (int j = 0; j < info->imports[i].func_count; j++) {
                uint32_t hn_off = rp - import_dir_start;
                *(uint16_t*)(rdata_buf + rp) = 0;
                rp += 2;
                const char *fn = info->imports[i].func_names[j];
                int flen = strlen(fn) + 1;
                memcpy(rdata_buf + rp, fn, flen);
                rp += flen;
                if (rp % 2) rp++;
                hn_rvas[hn_idx++] = rdata_rva + hn_off;
            }
        }

        /* Заполняем Import Directory (5 DWORDs per entry = 20 bytes) */
        rp = import_dir_start;
        int hn_index = 0;
        for (int i = 0; i < info->import_count; i++) {
            *(uint32_t*)(rdata_buf + rp) = rdata_rva + oft_start + oft_offsets[i];
            rp += 4;
            *(uint32_t*)(rdata_buf + rp) = 0; rp += 4;
            *(uint32_t*)(rdata_buf + rp) = 0; rp += 4;
            *(uint32_t*)(rdata_buf + rp) = rdata_rva + dllname_offsets[i];
            rp += 4;
            *(uint32_t*)(rdata_buf + rp) = rdata_rva + ft_off + ft_offsets[i];
            rp += 4;
        }
        for (int j = 0; j < 5; j++) {
            *(uint32_t*)(rdata_buf + rp) = 0;
            rp += 4;
        }

        /* OFT entries */
        hn_index = 0;
        for (int i = 0; i < info->import_count; i++) {
            rp = oft_start + oft_offsets[i];
            for (int j = 0; j < info->imports[i].func_count; j++) {
                *(uint64_t*)(rdata_buf + rp) = hn_rvas[hn_index];
                rp += 8;
                hn_index++;
            }
            *(uint64_t*)(rdata_buf + rp) = 0;
        }

        /* FT entries (same as OFT initially) */
        hn_index = 0;
        for (int i = 0; i < info->import_count; i++) {
            rp = ft_off + ft_offsets[i];
            for (int j = 0; j < info->imports[i].func_count; j++) {
                *(uint64_t*)(rdata_buf + rp) = hn_rvas[hn_index];
                rp += 8;
                hn_index++;
            }
            *(uint64_t*)(rdata_buf + rp) = 0;
        }

        /* Patch FF 15 call sites to point to IAT entries */
        for (int k = 0; k < info->import_call_count; k++) {
            const char *fname = info->import_names[k];
            int cs = info->import_call_sites[k];
            int found = 0;
            for (int i = 0; i < info->import_count && !found; i++) {
                for (int j = 0; j < info->imports[i].func_count && !found; j++) {
                    if (strcmp(info->imports[i].func_names[j], fname) == 0) {
                        uint64_t iat_rva = rdata_rva + ft_off + ft_offsets[i] + j * 8;
                        int32_t disp32 = (int32_t)(iat_rva - (text_rva + (uint32_t)cs + 6));
                        info->code[cs + 2] = (uint8_t)(disp32 & 0xFF);
                        info->code[cs + 3] = (uint8_t)((disp32 >> 8) & 0xFF);
                        info->code[cs + 4] = (uint8_t)((disp32 >> 16) & 0xFF);
                        info->code[cs + 5] = (uint8_t)((disp32 >> 24) & 0xFF);
                        found = 1;
                    }
                }
            }
        }
    }

    /* .text: пишем код (уже с пропатченными импортами) */
    fwrite(info->code, 1, info->code_size, f);
    {
        long cur = ftell(f);
        long target = text_raw_off + text_raw_size;
        while (cur < target) { fputc(0, f); cur++; }
    }

    /* .rdata: импорты */
    if (info->import_count > 0 && rdata_buf) {
        fwrite(rdata_buf, 1, raw_rdata_size, f);
        free(rdata_buf); rdata_buf = NULL;
        free(oft_offsets);
        free(ft_offsets);
        free(dllname_offsets);
        free(hn_rvas);

        long cur = ftell(f);
        long target = rdata_raw_off + rdata_raw_size;
        while (cur < target) { fputc(0, f); cur++; }
    }

    /* .data: пишем данные */
    if (section_count > 2 && info->data_size > 0) {
        fwrite(info->data, 1, info->data_size, f);
        long cur = ftell(f);
        long target = data_raw_off + data_raw_size;
        while (cur < target) { fputc(0, f); cur++; }
    }

    fclose(f);
    return 0;
}

int pe_info_add_import(PEInfo *info, const char *dll_name, const char *func_name) {
    if (!info || !dll_name || !func_name) return -1;

    for (int i = 0; i < info->import_count; i++) {
        if (strcmp(info->imports[i].dll_name, dll_name) == 0) {
            for (int j = 0; j < info->imports[i].func_count; j++) {
                if (strcmp(info->imports[i].func_names[j], func_name) == 0)
                    return 0;
            }
            if (info->imports[i].func_count < info->imports[i].func_capacity) {
                info->imports[i].func_names[info->imports[i].func_count] = func_name;
                info->imports[i].func_count++;
                return 0;
            }
        }
    }

    if (info->import_count >= info->import_capacity) {
        int newcap = info->import_capacity ? info->import_capacity * 2 : 4;
        info->imports = (ImportDescriptor*)realloc(info->imports, newcap * sizeof(ImportDescriptor));
        info->import_capacity = newcap;
    }

    int idx = info->import_count++;
    memset(&info->imports[idx], 0, sizeof(ImportDescriptor));
    info->imports[idx].dll_name = dll_name;
    info->imports[idx].func_capacity = 16;
    info->imports[idx].func_names = (const char**)malloc(16 * sizeof(const char*));
    info->imports[idx].func_names[0] = func_name;
    info->imports[idx].func_count = 1;

    return 0;
}

static uint8_t *static_lib_data = NULL;
static int static_lib_size = 0;
static int static_lib_capacity = 0;

int pe_info_add_static_lib(PEInfo *info, const char *obj_path, const uint8_t *obj_data, int obj_size) {
    (void)info;
    (void)obj_path;

    if (!obj_data || obj_size <= 0) return -1;

    if (static_lib_capacity == 0) {
        static_lib_capacity = 4096;
        static_lib_data = (uint8_t*)malloc(static_lib_capacity);
    }

    if (static_lib_size + obj_size > static_lib_capacity) {
        static_lib_capacity *= 2;
        static_lib_data = (uint8_t*)realloc(static_lib_data, static_lib_capacity);
    }

    memcpy(static_lib_data + static_lib_size, obj_data, obj_size);
    static_lib_size += obj_size;

    printf("[PE] Added static lib: %s (%d bytes)\n", obj_path, obj_size);
    return 0;
}

uint8_t* get_static_lib_data(void) { return static_lib_data; }
int get_static_lib_size(void) { return static_lib_size; }
void free_static_lib_data(void) {
    if (static_lib_data) { free(static_lib_data); static_lib_data = NULL; }
    static_lib_size = 0;
    static_lib_capacity = 0;
}
