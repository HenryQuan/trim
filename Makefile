CC = gcc
CFLAGS = -O2 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wwrite-strings -Wstrict-prototypes
# Apple's linker no longer supports -s; keep stripping for MinGW/Linux.
ifeq (,$(findstring darwin,$(shell $(CC) -dumpmachine)))
LDFLAGS ?= -s
endif
SRCS = src/main.c src/util.c src/exec.c src/compact.c src/read.c \
       src/string.c src/keyword.c src/context.c src/ref/ref.c src/ref/profile.c \
       src/ref/parse.c src/ref/names.c src/ref/index.c src/ref/engine.c \
       src/ref/rx_compile.c src/ref/rx_match.c

trim: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

trim.exe: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

trm: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

trm.exe: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

.PHONY: clean format
clean:
	rm -f trim trim.exe trm trm.exe

# format C sources/headers with clang-format (uses .clang-format)
format:
	clang-format -i --style=file $(SRCS) src/trim.h
