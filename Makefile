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

.PHONY: clean
clean:
	rm -f trim trim.exe trm trm.exe
