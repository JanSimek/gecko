@AGENTS.md

## Claude Code

- The shared agent guidance is `AGENTS.md` (imported above); put new rules there, not here, so
  Codex reads them too. This file holds only what is specific to Claude Code.
- For questions about game data — what a map, proto, script, FRM or gvar contains — prefer the
  `gecko` MCP tools (`describe_map`, `proto_info`, `describe_script`, `find_gvar`, `render_map`, …)
  over reading binaries or grepping the script tree. They run the same
  `gecko_cli` code as `gecko-cli`.
- A fresh MCP server process is needed to pick up a rebuilt `gecko-mcp`.
