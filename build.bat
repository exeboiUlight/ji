@echo off
chcp 65001 >nul
title JI Compiler Build

set CC=gcc
set CFLAGS=-Wall -Wextra -O2
set SRCDIR=src
set BINDIR=bin
set TARGET=bin/ji.exe

echo === Building JI 1.0 Compiler ===
echo Compiler: %CC%
echo Flags:    %CFLAGS%
echo Target:   %TARGET%
echo.

set SRCS=%SRCDIR%\main.c ^
         %SRCDIR%\project.c ^
         %SRCDIR%\jit.c ^
         %SRCDIR%\emu.c ^
         %SRCDIR%\token.c ^
         %SRCDIR%\lexer.c ^
         %SRCDIR%\ast.c ^
         %SRCDIR%\parser.c ^
         %SRCDIR%\codegen.c ^
         %SRCDIR%\emit.c ^
         %SRCDIR%\pe.c ^
         %SRCDIR%\elf.c ^
         %SRCDIR%\safety.c

%CC% %CFLAGS% -o %TARGET% %SRCS%

echo.
echo === Build successful! ===
echo Output: %TARGET%
echo.
echo Usage: %TARGET% examples\hello.ji