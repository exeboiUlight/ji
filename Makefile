CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
SRCDIR = src
INCDIR = include
BINDIR = bin
TARGET = $(BINDIR)/ji

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/token.c \
       $(SRCDIR)/lexer.c \
       $(SRCDIR)/ast.c \
       $(SRCDIR)/parser.c \
       $(SRCDIR)/codegen.c \
       $(SRCDIR)/emit.c \
       $(SRCDIR)/pe.c \
       $(SRCDIR)/elf.c \
       $(SRCDIR)/safety.c \
       $(SRCDIR)/jit.c \
       $(SRCDIR)/project.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	$(TARGET) examples/main.ji

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)
	rm -rf $(BINDIR)
