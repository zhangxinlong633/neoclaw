# Neo: minimal C agent. Depends on libcurl only.
# Build: make
# Run:   ./neo "your question"

CC     = cc
CFLAGS = -O2 -Wall -Wextra -I src
LDFLAGS = -lcurl

SRC = src/main.c src/config.c src/llm.c src/daemon.c src/skills.c src/agent_tools.c src/command_tools.c src/workflow.c src/plan.c src/capability_matrix.c src/yyjson.c
OBJ = $(SRC:.c=.o)

neo: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# yyjson is third-party; silence noisy pedantic warnings from the amalgamation.
src/yyjson.o: src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -c -o $@ $<

clean:
	rm -f neo $(OBJ) tests/test_parse_commands tests/test_command_exec tests/test_parse_workflows tests/test_workflow_loop tests/test_workflow_template tests/test_workflow_dag tests/test_plan_extract tests/test_capability_matrix

TEST_PARSE_CMD = tests/test_parse_commands
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/config.c src/config.h src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_parse_commands.c src/config.c src/yyjson.c

TEST_CMD_EXEC = tests/test_command_exec
$(TEST_CMD_EXEC): tests/test_command_exec.c src/command_tools.c src/config.c src/command_tools.h src/config.h src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_command_exec.c src/command_tools.c src/config.c src/yyjson.c

TEST_PARSE_WF = tests/test_parse_workflows
$(TEST_PARSE_WF): tests/test_parse_workflows.c src/config.c src/config.h src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_parse_workflows.c src/config.c src/yyjson.c

TEST_WF_LOOP = tests/test_workflow_loop
$(TEST_WF_LOOP): tests/test_workflow_loop.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c src/capability_matrix.c src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_workflow_loop.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c src/capability_matrix.c src/yyjson.c $(LDFLAGS)

TEST_WF_TMPL = tests/test_workflow_template
$(TEST_WF_TMPL): tests/test_workflow_template.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c src/capability_matrix.c src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_workflow_template.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c src/capability_matrix.c src/yyjson.c $(LDFLAGS)

TEST_WF_DAG = tests/test_workflow_dag
$(TEST_WF_DAG): tests/test_workflow_dag.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c src/capability_matrix.c src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_workflow_dag.c src/workflow.c src/agent_tools.c src/command_tools.c src/config.c src/llm.c src/capability_matrix.c src/yyjson.c $(LDFLAGS)

TEST_PLAN = tests/test_plan_extract
$(TEST_PLAN): tests/test_plan_extract.c src/plan.c src/config.c src/llm.c src/workflow.c src/agent_tools.c src/command_tools.c src/capability_matrix.c src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_plan_extract.c src/plan.c src/config.c src/llm.c src/workflow.c src/agent_tools.c src/command_tools.c src/capability_matrix.c src/yyjson.c $(LDFLAGS)

TEST_CAP_MATRIX = tests/test_capability_matrix
$(TEST_CAP_MATRIX): tests/test_capability_matrix.c src/capability_matrix.c src/config.c src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_capability_matrix.c src/capability_matrix.c src/config.c src/yyjson.c

test: $(TEST_PARSE_CMD) $(TEST_CMD_EXEC) $(TEST_PARSE_WF) $(TEST_WF_LOOP) $(TEST_WF_TMPL) $(TEST_WF_DAG) $(TEST_PLAN) $(TEST_CAP_MATRIX)
	./$(TEST_PARSE_CMD)
	./$(TEST_CMD_EXEC)
	./$(TEST_PARSE_WF)
	./$(TEST_WF_LOOP)
	./$(TEST_WF_TMPL)
	./$(TEST_WF_DAG)
	./$(TEST_PLAN)
	./$(TEST_CAP_MATRIX)

.PHONY: clean test
