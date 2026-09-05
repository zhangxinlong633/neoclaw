# Config format: JSON5

Neo configs are **JSON5** (yyjson 0.12.0 `YYJSON_READ_JSON5`): comments, trailing commas, and unquoted keys are allowed. Strict JSON is also accepted.

## Paths

| File | Role |
|------|------|
| `config/config.json5` | Preferred main config |
| `config.json5` | Repo-root fallback |
| `config/profiles/<name>/neo.json5` | Profile config |

```bash
cp config/config.json5.example config/config.json5
```

`.yaml` / `.yml` paths are rejected. Prefer `.json5` for everything in-repo (`team.example.json5`, fixtures, examples).

## Shape notes

- Same section names: `model`, `skills`, `capability_matrix` (legacy `tools`), `workflows`, `plan`, …
- Lists must be arrays (`depends_on`, `then`, `else`, `paths`, `argv`, …).
- `bootstrap.paths` / `rules.paths` are string arrays.
- Step field `"tools": "off"` is a step field; top-level `capability_matrix` is the Capability Matrix object (legacy alias: `tools`).
- `neo plan` / `neo run` emit workflows as JSON (valid JSON5 on reload).
