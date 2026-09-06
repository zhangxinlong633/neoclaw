# src/cli/

命令行入口：解析全局选项（含 `-v` / `--verbose`），分发单次查询、`daemon`、`dag run`、`plan` / `run`（已知图名走 DAG）等子命令。`workflow run` 为弃用别名。

主文件为 `main.c`。本目录无子目录。
