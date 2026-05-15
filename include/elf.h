#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include "pe.h"
#include "emit.h"

int elf_write(const char *path, PEInfo *info, Emitter *emit);

#endif
