@echo off
chcp 65001 >nul
title JI Compiler Build

set CC=gcc
set CFLAGS=-Wall -Wextra -O2
set SRCDIR=src
set BINDIR=bin
set TARGET=%BINDIR%\ji.exe

if not exist %BINDIR% mkdir %BINDIR%

echo === Building JI 1.0 Compiler ===
echo Compiler: %CC%
echo Flags:    %CFLAGS%
echo.

set SRCS=%SRCDIR%\main.c ^
         %SRCDIR%\token.c ^
         %SRCDIR%\lexer.c ^
         %SRCDIR%\ast.c ^
         %SRCDIR%\parser.c ^
         %SRCDIR%\codegen.c ^
         %SRCDIR%\emit.c ^
         %SRCDIR%\pe.c ^
         %SRCDIR%\elf.c ^
         %SRCDIR%\safety.c ^
         %SRCDIR%\jit.c ^
         %SRCDIR%\project.c

%CC% %CFLAGS% -o %TARGET% %SRCS%
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    if exist %TARGET% del %TARGET%
    exit /b 1
)

echo.
echo === Build successful! ===
echo Output: %TARGET%
echo.
echo Usage: %TARGET% examples\hello.ji