# AGENTS.md

- `Source/AngelscriptTest/Native/` 是 Native Core 测试层，只使用 `AngelscriptInclude.h` / `angelscript.h` 暴露的公共 API；不要把 `FAngelscriptEngine` 或 `source/as_*.h` 带进这个目录。
- 测试放置与分层规则以 `Documents/Guides/Test.md` 为准；新增测试先选层级，再选具体主题目录。
