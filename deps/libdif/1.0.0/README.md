---
title: libdif 1.0.0
created: 2026-08-12
author: publish-deps.ps1
last_updated: 2026-08-12
updated_by: publish-deps.ps1
---

# libdif 1.0.0

| Field | Value |
|-------|-------|
| Origin | Workspace `base/libdif` |
| Artifact | `lib/libdif.lib` (x64, /MT, Release) |
| Headers | `include/` (flat) |
| Sources | Not shipped in `deps/`; reviewable at workspace `base/libdif` |
| Expiry (UTC end-of-day) | 2027-12-30 |
| Gate | embedded (system time + EXE mtime) |

## Headers

```
include/
  dif_loader.h
  cndefs.h
  base/shared.h
```

Include as `#include <dif_loader.h>`.

## Role

DIF parse to cad_document only. Does not rasterize or encode.

## License

[MIT](LICENSE)

## Contact

yuqp78@foxmail.com
