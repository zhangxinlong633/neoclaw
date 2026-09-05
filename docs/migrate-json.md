# Migrate config from YAML to JSON5

Neo configs are **JSON5** (yyjson 0.12.0 `YYJSON_READ_JSON5`): comments, trailing commas, and unquoted keys are allowed.

## Paths

| Old | New (preferred) | Also accepted |
|-----|-----------------|---------------|
| `config/config.yaml` | `config/config.json5` | `config/config.json` |
| `config.yaml` | `config.json5` | `config.json` |
| `config/profiles/<name>/neo.yaml` | `…/neo.json5` | `…/neo.json` |

Copy the example:

```bash
cp config/config.json5.example config/config.json5
```

## Shape changes

- Same section names: `model`, `skills`, `tools`, `workflows`, `plan`, …
- Lists must be JSON arrays (`depends_on`, `then`, `else`, `paths`, `argv`, …).
- `bootstrap.paths` / `rules.paths` are string arrays (not YAML `- path:` lists).
- Step field `"tools": "off"` stays a step field; top-level `tools` is still the tools object.
- `neo plan` / `neo run` emit and consume **strict JSON** for workflows (valid JSON5 on reload).

## YAML

`.yaml` / `.yml` paths are rejected with a migration hint. There is no dual-read.
