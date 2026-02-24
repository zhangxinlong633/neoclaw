# Memory (context for Neo)

This file is loaded into the agent's system prompt (see `config.yaml` → `memory.path`). Keep it short; only the first `memory.max_chars` characters are sent to the model.

## Format

- **Facts**: `- YYYY-MM-DD: One-line fact or decision.`
- **Tasks**: `- [ ] Short task description` or `- [x] Done task.`
- **Preferences**: e.g. `- User prefers answers in Chinese.`

## Example

- 2025-02-23: Neo daemon mode uses stdin or Unix socket; session history is kept in memory.
- [ ] Try daemon with `neo daemon` and a few turns.
- User prefers concise replies unless they ask for detail.
