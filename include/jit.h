#ifndef JIT_H
#define JIT_H

#include "pe.h"
#include "codegen.h"

int jit_run(Codegen *cg, PEInfo *info);

#endif
