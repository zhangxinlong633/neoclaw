#!/usr/bin/env python3
"""Minimal MCP stdio mock: tools/list + tools/call echo."""
import json
import sys


def send(obj):
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        msg = json.loads(line)
        mid = msg.get("id")
        method = msg.get("method")
        if method == "initialize":
            send(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "result": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {"tools": {}},
                        "serverInfo": {"name": "mock", "version": "1.0"},
                    },
                }
            )
        elif method == "notifications/initialized":
            continue
        elif method == "tools/list":
            send(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "result": {
                        "tools": [
                            {
                                "name": "echo",
                                "description": "Echo text argument",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {"text": {"type": "string"}},
                                    "required": ["text"],
                                },
                            }
                        ]
                    },
                }
            )
        elif method == "tools/call":
            params = msg.get("params") or {}
            args = params.get("arguments") or {}
            text = args.get("text", "")
            send(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "result": {
                        "content": [{"type": "text", "text": str(text)}],
                        "isError": False,
                    },
                }
            )
        else:
            send(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "error": {"code": -32601, "message": "Method not found"},
                }
            )


if __name__ == "__main__":
    main()
