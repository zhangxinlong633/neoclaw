# Neo: minimal C agent. Depends on libcurl only.
# Build: make
# Run:   ./neo "your question"

CC     = cc
CFLAGS = -O2 -Wall -Wextra -I src
LDFLAGS = -lcurl

SRC = src/main.c src/config.c src/llm.c src/daemon.c src/skills.c src/agent_tools.c src/command_tools.c src/workflow.c
OBJ = $(SRC:.c=.o)

neo: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f neo $(OBJ) tests/test_parse_commands tests/test_command_exec tests/test_parse_workflows tests/test_workflow_loop tests/test_workflow_template

TEST_PARSE_CMD = tests/test_parse_commands
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/config.c src/config.h
	$(CC) $(CFLAGS) -o $@ tests/test_parse_commands.c src/config.c

TEST_CMD_EXEC = tests/test_command_exec
$(TEST_CMD_EXEC): tests/test_command_exec.c src/command_tools.c src/config.c src/command_tools.h src/config.h
	$(CC) $(CFLAGS) -o $@ tests/test_command_exec.c src/command_tools.c src/config.c

TEST_PARSE_WF = tests/test_parse_workflows
$(TEST_PARSE_WF): tests/test_parse_workflows.c src/config.c src/config.h
	$(CC) $(CFLAGS) -o $@ tests/test_parse_workflows.c src/config.c

TEST_WF_LOOP = tests/test_workflow_loop
$(TEST_WF_LOOP): tests/test_workflow_loop.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c
	$(CC) $(CFLAGS) -o $@ tests/test_workflow_loop.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c $(LDFLAGS)

TEST_WF_TMPL = tests/test_workflow_template
$(TEST_WF_TMPL): tests/test_workflow_template.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c
	$(CC) $(CFLAGS) -o $@ tests/test_workflow_template.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c $(LDFLAGS)

test: $(TEST_PARSE_CMD) $(TEST_CMD_EXEC) $(TEST_PARSE_WF) $(TEST_WF_LOOP) $(TEST_WF_TMPL)
	./$(TEST_PARSE_CMD)
	./$(TEST_CMD_EXEC)
	./$(TEST_PARSE_WF)
	./$(TEST_WF_LOOP)
	./$(TEST_WF_TMPL)

.PHONY: clean test
