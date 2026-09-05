# Neo: minimal C agent. Depends on libcurl only.
# Build: make
# Run:   ./neo "your question"
#
# Layout: src/{cli,core,llm,capability,workflow,vendor}/
# Objects: build/ mirroring src/ (never write .o under src/)

CC      = cc
OBJDIR  = build
INCLUDES = -Isrc/core -Isrc/llm -Isrc/capability -Isrc/workflow -Isrc/vendor
CFLAGS  = -O2 -Wall -Wextra $(INCLUDES)
LDFLAGS = -lcurl

SRC = \
	src/cli/main.c \
	src/core/config.c \
	src/core/daemon.c \
	src/llm/llm.c \
	src/capability/agent_tools.c \
	src/capability/command_tools.c \
	src/capability/capability_matrix.c \
	src/capability/capability_dir.c \
	src/capability/mcp_stdio.c \
	src/workflow/workflow.c \
	src/workflow/workflow_dir.c \
	src/workflow/plan.c \
	src/vendor/yyjson.c

OBJ = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC))

neo: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# yyjson is third-party; silence noisy pedantic warnings from the amalgamation.
$(OBJDIR)/vendor/yyjson.o: src/vendor/yyjson.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -c -o $@ $<

clean:
	rm -rf $(OBJDIR)
	rm -f neo \
		tests/test_parse_commands \
		tests/test_command_exec \
		tests/test_parse_workflows \
		tests/test_workflow_loop \
		tests/test_workflow_template \
		tests/test_workflow_dag \
		tests/test_plan_extract \
		tests/test_capability_matrix

# Shared flags for test binaries that compile vendor yyjson from source.
TEST_CFLAGS = $(CFLAGS) -Wno-unused-function -Wno-unused-parameter

TEST_PARSE_CMD = tests/test_parse_commands
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/core/config.c src/capability/capability_dir.c \
		src/workflow/workflow_dir.c src/core/config.h src/vendor/yyjson.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_parse_commands.c src/core/config.c \
		src/capability/capability_dir.c src/workflow/workflow_dir.c src/vendor/yyjson.c

TEST_CMD_EXEC = tests/test_command_exec
$(TEST_CMD_EXEC): tests/test_command_exec.c src/capability/command_tools.c src/core/config.c \
		src/capability/capability_dir.c src/workflow/workflow_dir.c \
		src/capability/command_tools.h src/core/config.h src/vendor/yyjson.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_command_exec.c src/capability/command_tools.c \
		src/core/config.c src/capability/capability_dir.c src/workflow/workflow_dir.c src/vendor/yyjson.c

TEST_PARSE_WF = tests/test_parse_workflows
$(TEST_PARSE_WF): tests/test_parse_workflows.c src/core/config.c src/capability/capability_dir.c \
		src/workflow/workflow_dir.c src/core/config.h src/vendor/yyjson.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_parse_workflows.c src/core/config.c \
		src/capability/capability_dir.c src/workflow/workflow_dir.c src/vendor/yyjson.c

TEST_WF_SRCS = src/workflow/workflow.c src/workflow/workflow_dir.c src/capability/agent_tools.c \
	src/capability/command_tools.c src/core/config.c src/llm/llm.c src/capability/capability_matrix.c \
	src/capability/capability_dir.c src/capability/mcp_stdio.c src/vendor/yyjson.c

TEST_WF_LOOP = tests/test_workflow_loop
$(TEST_WF_LOOP): tests/test_workflow_loop.c $(TEST_WF_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_workflow_loop.c $(TEST_WF_SRCS) $(LDFLAGS)

TEST_WF_TMPL = tests/test_workflow_template
$(TEST_WF_TMPL): tests/test_workflow_template.c $(TEST_WF_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_workflow_template.c $(TEST_WF_SRCS) $(LDFLAGS)

TEST_WF_DAG = tests/test_workflow_dag
$(TEST_WF_DAG): tests/test_workflow_dag.c $(TEST_WF_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_workflow_dag.c $(TEST_WF_SRCS) $(LDFLAGS)

TEST_PLAN = tests/test_plan_extract
$(TEST_PLAN): tests/test_plan_extract.c src/workflow/plan.c $(TEST_WF_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_plan_extract.c src/workflow/plan.c $(TEST_WF_SRCS) $(LDFLAGS)

TEST_CAP_MATRIX = tests/test_capability_matrix
$(TEST_CAP_MATRIX): tests/test_capability_matrix.c src/capability/capability_matrix.c \
		src/capability/capability_dir.c src/workflow/workflow_dir.c src/capability/mcp_stdio.c \
		src/core/config.c src/capability/agent_tools.c src/capability/command_tools.c src/llm/llm.c \
		src/vendor/yyjson.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_capability_matrix.c src/capability/capability_matrix.c \
		src/capability/capability_dir.c src/workflow/workflow_dir.c src/capability/mcp_stdio.c \
		src/core/config.c src/capability/agent_tools.c src/capability/command_tools.c src/llm/llm.c \
		src/vendor/yyjson.c $(LDFLAGS)

test: $(TEST_PARSE_CMD) $(TEST_CMD_EXEC) $(TEST_PARSE_WF) $(TEST_WF_LOOP) $(TEST_WF_TMPL) $(TEST_WF_DAG) $(TEST_PLAN) $(TEST_CAP_MATRIX) neo
	./$(TEST_PARSE_CMD)
	./$(TEST_CMD_EXEC)
	./$(TEST_PARSE_WF)
	./$(TEST_WF_LOOP)
	./$(TEST_WF_TMPL)
	./$(TEST_WF_DAG)
	./$(TEST_PLAN)
	./$(TEST_CAP_MATRIX)
	./tests/cli_capability_matrix.sh

test-cli: neo
	./tests/cli_capability_matrix.sh

.PHONY: clean test test-cli
