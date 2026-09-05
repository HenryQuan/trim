CC = gcc
CFLAGS = -O2 -s -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wwrite-strings -Wstrict-prototypes
SRCS = src/main.c src/util.c src/exec.c src/compact.c src/read.c \
       src/string.c src/context.c src/ref/ref.c src/ref/profile.c \
       src/ref/parse.c src/ref/names.c src/ref/index.c src/ref/engine.c \
       src/ref/rx_compile.c src/ref/rx_match.c

trim: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

trim.exe: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

trm: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

trm.exe: $(SRCS) src/trim.h src/ref/rf.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

.PHONY: clean format
clean:
	rm -f trim trim.exe trm trm.exe

# format C sources/headers with clang-format (uses .clang-format)
format:
	clang-format -i --style=file $(SRCS) src/trim.h
