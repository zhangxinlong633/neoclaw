# Neo: minimal C agent. Depends on libcurl only.
# Build: make
# Run:   ./neo "your question"

CC     = cc
CFLAGS = -O2 -Wall -Wextra -I src
LDFLAGS = -lcurl

SRC = src/main.c src/config.c src/llm.c src/daemon.c src/skills.c
OBJ = $(SRC:.c=.o)

neo: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f neo $(OBJ)

.PHONY: clean
