# Neo: minimal C agent. Depends on libcurl only.
# Build: make
# Run:   ./neo "your question"

CC     = cc
CFLAGS = -O2 -Wall -Wextra -I src
LDFLAGS = -lcurl

SRC = src/main.c src/config.c src/llm.c src/daemon.c src/skills.c src/agent_tools.c
OBJ = $(SRC:.c=.o)

neo: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f neo $(OBJ) tests/test_parse_commands

TEST_PARSE_CMD = tests/test_parse_commands
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/config.c src/config.h
	$(CC) $(CFLAGS) -o $@ tests/test_parse_commands.c src/config.c

test: $(TEST_PARSE_CMD)
	./$(TEST_PARSE_CMD)

.PHONY: clean test
