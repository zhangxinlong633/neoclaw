# Neo: minimal C agent. Depends on libcurl only.
# Build: make
# Run:   ./neo "your question"

CC     = cc
CFLAGS = -O2 -Wall -Wextra -I src
LDFLAGS = -lcurl

SRC = src/main.c src/config.c src/llm.c src/daemon.c src/skills.c src/agent_tools.c src/command_tools.c
OBJ = $(SRC:.c=.o)

neo: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f neo $(OBJ) tests/test_parse_commands tests/test_command_exec tests/test_parse_workflows

TEST_PARSE_CMD = tests/test_parse_commands
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/config.c src/config.h
	$(CC) $(CFLAGS) -o $@ tests/test_parse_commands.c src/config.c

TEST_CMD_EXEC = tests/test_command_exec
$(TEST_CMD_EXEC): tests/test_command_exec.c src/command_tools.c src/config.c src/command_tools.h src/config.h
	$(CC) $(CFLAGS) -o $@ tests/test_command_exec.c src/command_tools.c src/config.c

TEST_PARSE_WF = tests/test_parse_workflows
$(TEST_PARSE_WF): tests/test_parse_workflows.c src/config.c src/config.h
	$(CC) $(CFLAGS) -o $@ tests/test_parse_workflows.c src/config.c

test: $(TEST_PARSE_CMD) $(TEST_CMD_EXEC) $(TEST_PARSE_WF)
	./$(TEST_PARSE_CMD)
	./$(TEST_CMD_EXEC)
	./$(TEST_PARSE_WF)

.PHONY: clean test
