CC = gcc
CFLAGS = -O2 -s
SRCS = src/main.c src/util.c src/exec.c src/compact.c src/read.c

trim: $(SRCS) src/trim.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

trim.exe: $(SRCS) src/trim.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

trm: $(SRCS) src/trim.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

trm.exe: $(SRCS) src/trim.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

.PHONY: clean format
clean:
	rm -f trim trim.exe trm trm.exe

# format C sources/headers with clang-format (uses .clang-format)
format:
	clang-format -i --style=file $(SRCS) src/trim.h
