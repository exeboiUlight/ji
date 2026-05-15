# JI 1.0 — Language Documentation

## Overview

JI 1.0 is a C-like compiled language with OOP support, inline NASM assembly, and Rust-like static safety analysis. It compiles to x86-64 native executables (PE for Windows, ELF for Linux), and supports **Just-In-Time (JIT) execution** and a **project system** inspired by .NET, Rust, and Zig.

**Author:** Bloodbath OS Project

---

## Compilation & Usage

### Single File Compilation

```
ji input.ji [-target windows|linux] [-root dir] [-o output]
```

| Flag | Description |
|------|-------------|
| `input.ji` | Source file |
| `-target windows\|linux` | Target OS (default: windows) |
| `-root dir` | Root directory for Python-style imports |
| `-o output` | Output executable path |

**Examples:**

```
ji hello.ji
ji hello.ji -target linux -o hello.elf
ji src/main.ji -root src -o bin/program.exe
```

### Project System

```
ji new <project-name>          Create new project
ji build [project-dir]         Build project to executable
ji run [project-dir]           Run project via JIT
```

| Command | Description |
|---------|-------------|
| `ji new <name>` | Creates a new project with `project.json`, `src/main.ji`, `src/io.ji` |
| `ji build [dir]` | Compiles the project into an executable (uses `project.json` for settings) |
| `ji run [dir]` | Compiles and executes the project in-memory via JIT (no file output) |

`[project-dir]` defaults to the current directory (`.`).

### JIT Execution

The JIT mode compiles source code directly to memory and executes it without creating an executable file on disk. This is useful for rapid development and testing.

**How it works:**
1. Source is parsed and compiled to x86-64 machine code (same as normal compilation)
2. Imported functions (`printf`, `malloc`, etc.) are resolved via `LoadLibrary`/`GetProcAddress`
3. Machine code is copied to executable memory (`VirtualAlloc` with `PAGE_EXECUTE_READWRITE`)
4. Import call sites are patched to point to a thunk table with resolved function addresses
5. The `main()` function is called directly
6. After execution, all allocated memory is freed

**Supported on:** Windows (x86-64). Linux JIT support is planned.

**Example:**
```
ji run                    # JIT-run current directory as a project
ji run myproject          # JIT-run myproject/
```

---

## Language Syntax

### Types

| Type | Size | Description |
|------|------|-------------|
| `int` | 4 bytes | Signed 32-bit integer |
| `char` | 1 byte | Single character |
| `float` | 4 bytes | 32-bit float |
| `double` | 8 bytes | 64-bit float |
| `void` | — | No return value |
| `struct` | varies | User-defined struct |
| `class` | varies | User-defined class |

Pointers: `int*`, `char*`, `void*`, etc.

### Variables

```
int x;
int y = 42;
char* name = "Hello";
float pi = 3.14f;
int* ptr = null;
```

### Functions

```
int add(int a, int b) {
    return a + b;
}

void greet(char* name) {
    printf("Hello, %s!\n", name);
}
```

### Control Flow

**if / else:**
```
if (x > 0) {
    printf("positive");
} else if (x == 0) {
    printf("zero");
} else {
    printf("negative");
}
```

**while:**
```
while (i < 10) {
    printf("%d\n", i);
    i = i + 1;
}
```

**do / while:**
```
do {
    printf("%d\n", i);
    i = i + 1;
} while (i < 10);
```

**for:**
```
for (i = 0; i < 10; i = i + 1) {
    printf("%d\n", i);
}
```

**switch:**
```
switch (x) {
    case 1:
        printf("one");
        break;
    case 2:
        printf("two");
        break;
    default:
        printf("other");
}
```

### Operators

| Category | Operators |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `<` `>` `<=` `>=` |
| Logical | `&&` `\|\|` `!` |
| Bitwise | `&` `\|` `^` `~` `<<` `>>` |
| Assignment | `=` `+=` `-=` `*=` `/=` |
| Unary | `++` `--` `&` `*` `-` |
| Ternary | `? :` |
| Member | `.` `->` |
| Subscript | `[]` |
| Cast | `(type)expr` |
| Sizeof | `sizeof(expr)` |

### Structs & Classes

**Struct:**
```
struct Point {
    int x;
    int y;
};
```

**Class (with methods):**
```
class Counter {
    int value;

    void increment() {
        this.value = this.value + 1;
    }

    int getValue() {
        return this.value;
    }
};

int main() {
    Counter c;
    c.value = 0;
    c.increment();
    printf("%d\n", c.getValue());
    return 0;
}
```

**Virtual methods:**
```
class Animal {
    virtual void speak() { printf("...\n"); }
};

class Dog : class Animal {
    virtual void speak() { printf("Woof!\n"); }
};

```
Note: Virtual methods use a vtable at offset 0 of the class instance. Dynamic dispatch is not yet fully automatic — virtual calls must be done manually via the vtable.

---

## Inline Assembly (`asm { }`)

JI supports inline NASM-style assembly blocks. Inside `asm { }` you can write raw x86-64 assembly instructions. All standard NASM instruction syntax is supported.

**Syntax:**
```
asm {
    ; NASM-style assembly
    mov eax, 42
    ret
}
```

**Example — return a value:**
```
int get_forty_two() {
    asm {
        mov eax, 42
        ret
    }
}
```

**Example — syscall (Linux):**
```
void syscall_exit(int code) {
    asm {
        mov eax, 60      ; syscall number for exit
        mov edi, [rbp + 16]  ; first argument
        syscall
    }
}
```

**Example — calling convention helpers:**
```
int multiply(int a, int b) {
    asm {
        mov eax, [rbp + 16]  ; first param
        mov ebx, [rbp + 24]  ; second param
        imul eax, ebx
        ret
    }
}
```

**Supported instructions:**
- Data mov: `mov`, `xchg`, `lea`
- Arithmetic: `add`, `sub`, `imul`, `idiv`, `inc`, `dec`, `neg`, `not`
- Logic: `and`, `or`, `xor`, `shl`, `shr`, `test`
- Stack: `push`, `pop`
- Control: `ret`, `call`, `jmp`
- Conditional jumps: `je`, `jne`, `jz`, `jnz`, `jl`, `jg`, `jle`, `jge`, `ja`, `jae`, `jb`, `jbe`
- Other: `int`, `syscall`, `nop`, `cdq`, `cdqe`, `cqo`
- Comparison: `cmp`

**Registers:**
- 64-bit: `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rsp`, `rbp`, `r8`–`r15`
- 32-bit: `eax`, `ebx`, `ecx`, `edx`, `esi`, `edi`, `esp`, `ebp`, `r8d`–`r15d`
- 16-bit: `ax`, `bx`, `cx`, `dx`, `si`, `di`, `sp`, `bp`, `r8w`–`r15w`
- 8-bit: `al`, `bl`, `cl`, `dl`, `ah`, `bh`, `ch`, `dh`, `sil`, `dil`, `bpl`, `spl`, `r8b`–`r15b`

**Memory addressing:**
```
[rax]
[rax + 8]
[rax + rbx]
[rax + rbx * 8]
[rax + rbx * 4 + 16]
[rip + offset]
[0x1000]
```

**Labels in asm blocks:**
```
asm {
    mov eax, 0
.loop:
    add eax, 1
    cmp eax, 10
    jl .loop
    ret
}
```

**Comments:**
```
asm {
    ; This is a comment
    nop  ; inline comment
}
```

**Important notes:**
- Asm blocks are emitted directly into the function body. You are responsible for correct prologue/epilogue (the compiler emits `push rbp; mov rbp, rsp; sub rsp, 256` before the block).
- Use `[rbp + 16]` for the first parameter, `[rbp + 24]` for the second, etc.
- Use `[rbp - 4]`, `[rbp - 8]` for local variables.
- Use `[rbp + 8]` for the return address.
- The `ret` instruction should be used with care — it returns from the function immediately.

---

## Import System

JI supports file imports using Python-style module resolution:

```
import "io.ji"
import "math.ji"
import "utils/array.ji"
```

The compiler searches for imported files in:
1. The source file's directory
2. The root directory (specified via `-root` flag)

In project mode, `src/main.ji` imports `"io.ji"` which is resolved from the `src/` directory. The `-root` flag is not needed for project builds — the project system automatically configures the root to `src/`.

> **Note:** Only the declarations are needed — the actual implementations (printf, malloc, etc.) are linked from `msvcrt.dll` (Windows) or `libc.so.6` (Linux) at compile time, or resolved via `LoadLibrary`/`GetProcAddress` during JIT execution.

---

## Memory Safety (Static Analysis)

JI includes a Rust-like static safety checker that analyzes your code at compile time:

**Warnings (non-fatal):**
- Null pointer dereference
- Uninitialized variable usage
- Division by zero detection
- Array bounds checking
- Use-after-free detection
- Invalid casts
- Dead code
- Unreachable code
- Invalid pointer operations
- Memory leaks
- Negative array sizes

Example:
```
int main() {
    int* p = null;
    *p = 42;  // Safety: null dereference warning
    return 0;
}
```

---

## Calling Convention

Functions use the **Microsoft x64 calling convention** on Windows and **System V AMD64 ABI** on Linux internally, but the compiler abstracts this:
- First 4 arguments are passed in registers (RCX/RDX/R8/R9 on Windows, RDI/RSI/RDX/RCX on Linux)
- On Linux: 5th+ arguments go on the stack
- Return value is in `eax`/`rax`

When writing inline assembly, parameters are read from the stack at `[rbp + 16]`, `[rbp + 24]`, etc. regardless of platform (the compiler's own generated code uses register passing, but asm blocks work with a simplified stack convention).

---

## Standard Library (Implicit)

The following C runtime functions are automatically importable and available without explicit declarations. You can also use `import "io.ji";` to include explicit declarations in your source.

### Console Input/Output

| Declaration | Description |
|-------------|-------------|
| `int printf(char* fmt);` | Print formatted string to stdout |
| `int scanf(char* fmt);` | Read formatted input from stdin |
| `int puts(char* str);` | Print string to stdout with newline |
| `char* gets(char* buf);` | Read string from stdin |

### File Input/Output

| Declaration | Description |
|-------------|-------------|
| `void* fopen(char* filename, char* mode);` | Open file, returns handle or null |
| `int fclose(void* file);` | Close file handle |

### Memory Management

| Declaration | Description |
|-------------|-------------|
| `void* malloc(int size);` | Allocate memory, returns pointer or null |
| `void free(void* ptr);` | Free allocated memory |

### Process Control

| Declaration | Description |
|-------------|-------------|
| `void exit(int code);` | Exit process with status code |

---

## Complete Examples

### Hello World

```
int main() {
    printf("Hello, World!\n");
    return 0;
}
```

Compile: `ji hello.ji`

### Fibonacci

```
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    int i = 0;
    while (i < 10) {
        printf("fib(%d) = %d\n", i, fib(i));
        i = i + 1;
    }
    return 0;
}
```

### Inline Assembly — String Print (Linux)

```
void print_hello() {
    asm {
        mov eax, 1       ; sys_write
        mov edi, 1       ; stdout
        lea rsi, [rel msg]
        mov edx, 13      ; length
        syscall
        ret
    }
msg:
    db "Hello, World!", 10
}
```

### Custom Token System

The compiler features a custom token registration system, allowing you to extend the language with new keywords at runtime:

```
token_registry_add(&reg, "my_keyword", "my_keyword", 1);
```

This is used internally to register all language keywords and can be leveraged for domain-specific extensions.

---

## Project System

### Directory Structure

```
myproject/
├── project.json          # Project configuration
├── src/
│   ├── main.ji           # Entry point (must contain main())
│   └── io.ji             # Standard I/O declarations (auto-generated)
```

### project.json Reference

The `project.json` file stores project metadata and platform-specific settings:

```json
{
    "name": "myproject",
    "version": "0.1.0",
    "author": "",
    "description": "",
    "icon": "",

    "windows": {
        "company": "",
        "copyright": "",
        "product_version": "0.1.0",
        "file_version": "0.1.0",
        "trust_info": {
            "requested_execution_level": "asInvoker",
            "ui_access": false
        }
    },

    "linux": {
        "desktop_entry": {
            "categories": "",
            "comment": ""
        }
    }
}
```

#### Top-Level Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Project name (used as executable name) |
| `version` | string | Project version (e.g. `"0.1.0"`) |
| `author` | string | Author name |
| `description` | string | Short project description |
| `icon` | string | Path to icon file (optional) |

#### Windows Section

##### `company`
- **Type:** `string`
- **Max length:** 128 символов
- **Описание:** Название компании-разработчика. Записывается в PE-секцию `.rsrc` как `CompanyName` в Version Information. Отображается в свойствах файла (Правая кнопка → Свойства → Подробно → Название компании).

##### `copyright`
- **Type:** `string`
- **Max length:** 256 символов
- **Описание:** Уведомление об авторских правах. Записывается как `LegalCopyright` в Version Information. Пример: `"Copyright © 2025 Bloodbath OS. All rights reserved."`.

##### `product_version`
- **Type:** `string`
- **Max length:** 32 символа
- **Описание:** Версия продукта в произвольном формате (рекомендуется `X.Y.Z`). Записывается как `ProductVersion` в Version Information. Пример: `"1.0.0"`, `"2025.04.01"`.

##### `file_version`
- **Type:** `string`
- **Max length:** 32 символа
- **Описание:** Версия файла. Записывается как `FileVersion` в Version Information и также используется для `dwFileVersion` в VS_FIXEDFILEINFO (должна быть в формате `X.Y.Z.W` для корректного отображения в Windows). Пример: `"1.0.0.0"`.

##### `trust_info.requested_execution_level`
- **Type:** `string`
- **Max length:** 32 символа
- **Описание:** Уровень выполнения UAC (User Account Control). Определяет, с какими правами будет запущено приложение. Записывается в манифест приложения.
- **Допустимые значения:**
  - `"asInvoker"` — (по умолчанию) запуск с правами текущего пользователя. UAC-запрос не появляется.
  - `"highestAvailable"` — запрос максимально доступных прав текущего пользователя. Если пользователь администратор — появится UAC-запрос.
  - `"requireAdministrator"` — обязательный запуск от имени администратора. UAC-запрос появляется всегда.
- **Влияние на JIT:** При JIT-запуске (`ji run`) этот параметр не влияет, так как процесс уже запущен. Учитывается только при сборке исполняемого файла (`ji build`) и последующем запуске `.exe`.

##### `trust_info.ui_access`
- **Type:** `bool`
- **Default:** `false`
- **Описание:** Флаг доступности UI для специальных возможностей (accessibility). Если `true`, приложение может программно управлять другими окнами (например, для экранных дикторов, автоматизации тестирования, IME). Требует цифровой подписи при `true`.
- **Примечание:** Если `requested_execution_level` = `"requireAdministrator"`, этот флаг должен быть `false`, иначе Windows отклонит манифест.

#### Linux Section

| Field | Type | Description |
|-------|------|-------------|
| `desktop_entry.categories` | string | Desktop entry categories (e.g. `"Development;IDE;"`) |
| `desktop_entry.comment` | string | Desktop entry comment |

### Creating a Project

```
ji new myproject
```

This generates:
- `myproject/project.json` — configuration with defaults
- `myproject/src/main.ji` — entry point with "Hello from 'myproject'!"
- `myproject/src/io.ji` — standard library declarations (printf, scanf, puts, fopen, fclose, malloc, free, exit)

### Building a Project

```
ji build                  # Build current directory
ji build myproject        # Build myproject/
```

Compiles all `.ji` source files in `src/` using settings from `project.json`. Output: `myproject/myproject.exe` (Windows) or `myproject/myproject.elf` (Linux).

### Running a Project via JIT

```
ji run                    # JIT-run current directory
ji run myproject          # JIT-run myproject/
```

Compiles source in memory, resolves imports (printf, malloc, etc.) via `LoadLibrary`/`GetProcAddress`, allocates executable memory with `VirtualAlloc`, patches call sites, and executes `main()`. No executable file is written to disk.

---

## JIT Internals (Windows x86-64)

The JIT engine performs these steps:

1. **Compilation** — Source is compiled to x86-64 machine code into a `Codegen` buffer (same as static compilation)
2. **Import resolution** — Each imported function name (e.g. `printf`, `malloc`) is resolved via `LoadLibraryA("msvcrt.dll")` + `GetProcAddress()`. A flat thunk table is built — one 8-byte entry per unique import name.
3. **Code patching** — Each `FF 15` (call `[rel32]`) instruction in the compiled code is patched to point to the corresponding thunk entry containing the resolved function address.
4. **Memory allocation** — `VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE)` allocates executable memory.
5. **Execution** — Machine code is copied to the allocated buffer, and the `main()` function is called via a function pointer.
6. **Cleanup** — After `main()` returns, all allocated buffers are freed with `VirtualFree`.

The JIT engine is designed so that the `Codegen` argument is unused on Windows but reserved for future Linux JIT support (where `mmap` with `PROT_EXEC` would be used instead).

---

## Project Examples

### Hello World Project

```
ji new hello
ji build hello
hello/hello.exe
```

Or using JIT:

```
ji run hello
```

Output:
```
Hello from 'hello'!
```

### Fibonacci Project

**src/main.ji:**
```
import "io.ji";

int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    int i = 0;
    while (i < 10) {
        printf("fib(%d) = %d\n", i, fib(i));
        i = i + 1;
    }
    return 0;
}
```

**src/io.ji** (identical across all projects — declare only the functions you use):
```
int printf(char* fmt);
```

```
ji build
```

---

## Command Reference

| Action | Command |
|--------|---------|
| Compile single file | `ji program.ji` |
| Compile for Linux | `ji program.ji -target linux` |
| Custom output | `ji program.ji -o myprog.exe` |
| Set root directory | `ji program.ji -root src` |
| Create new project | `ji new <project-name>` |
| Build project | `ji build [project-dir]` |
| JIT-run project | `ji run [project-dir]` |

---

## Limitations

- The PE writer has known alignment issues with certain section sizes
- Virtual method dispatch via vtable is low-level (no automatic `call [rax]` yet)
- Only x86-64 target is supported
- No optimizer — straightforward instruction selection
- Pointer safety checker is advisory (non-fatal warnings)
- String literals are limited to 255 characters
- Maximum 256 labels and 1024 relocations per compilation unit
- JIT execution is Windows x86-64 only (Linux JIT support is planned)
- JIT import resolution only searches `msvcrt.dll` — custom DLL imports are not yet supported
- Project system does not support multiple source files beyond `src/main.ji` and `src/io.ji` (single-compilation-unit model)
- `strcpy_safe` macro uses `sizeof(dst)` — safe only with array arguments, not pointers
