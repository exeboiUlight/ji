#!/bin/bash
# JI 1.0 Build Script

set -e

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--Wall -Wextra -O2}"
SRCDIR="src"
BINDIR="bin"
TARGET="${BINDIR}/ji"

SRCS="
    ${SRCDIR}/main.c
    ${SRCDIR}/token.c
    ${SRCDIR}/lexer.c
    ${SRCDIR}/ast.c
    ${SRCDIR}/parser.c
    ${SRCDIR}/codegen.c
    ${SRCDIR}/emit.c
    ${SRCDIR}/pe.c
    ${SRCDIR}/elf.c
    ${SRCDIR}/safety.c
"

echo "=== Building JI 1.0 Compiler ==="
echo "Compiler: ${CC}"
echo "Flags:    ${CFLAGS}"
echo "Target:   ${TARGET}"

mkdir -p "${BINDIR}"

${CC} ${CFLAGS} -o "${TARGET}" ${SRCS}

echo "=== Build successful! ==="
echo "Output: ${TARGET}"
echo ""
echo "Usage: ${TARGET} examples/hello.ji"