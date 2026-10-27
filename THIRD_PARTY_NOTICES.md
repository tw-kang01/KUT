# Third-party notices

This repository contains no copied third-party source code. The plugin builds against and runs with the following external software, none of which is redistributed here.

| Component | Role | Terms |
|---|---|---|
| Unreal Engine 5.8 (Epic Games), including the engine plugins `ToolsetRegistry` and `ModelContextProtocol` | Editor host, `UToolsetDefinition`/`AICallable` tool registration, HTTP and HTTP server modules, Slate | Unreal Engine EULA. Engine source and template content are not included. The `TutorSandbox` host project keeps only the Target.cs files and a minimal game module from the UE project template (Copyright Epic Games, Inc.). |
| Claude Code CLI (`claude.exe`, Anthropic) | Launched as a child process by `FKHUClaudeRuntime`; connects to the university gateway with the student's own key | Anthropic's terms. The binary is not bundled; its location is configured in `Config/ClaudeRuntime.json` or the editor settings. |
| ChatKHU API Gateway (Kyung Hee University / Mindlogic) | Model access with the student's personal API key and credits | University service terms. |

Projects consulted for comparison only, without copying code: [chongdashu/unreal-mcp](https://github.com/chongdashu/unreal-mcp) (MIT) and [kevinpbuckley/VibeUE](https://github.com/kevinpbuckley/VibeUE) (MIT).

`Resources/KHUMark.svg` reproduces the Kyung Hee University mark for this university course project. It is a trademark of the university and is not licensed for other use.
