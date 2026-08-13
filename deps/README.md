---
title: deps
created: 2026-08-12
author: publish-deps.ps1
last_updated: 2026-08-13
updated_by: AI-assisted
---

# deps/

Vendored **headers + static `.lib` + README/VERSION/LICENSE**.
Version pins: [`versions.json`](versions.json).

No `.cpp`/`.c` source trees. Day-to-day builds of `redcli` link against this
tree only. Author rebuild/harvest: `..\scripts\publish-deps.ps1`.

See root [README.md](../README.md) and [docs/DEPENDENCIES.md](../docs/DEPENDENCIES.md).
