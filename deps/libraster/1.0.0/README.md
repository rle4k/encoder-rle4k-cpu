---
title: libraster 1.0.0
created: 2026-08-12
author: publish-deps.ps1
last_updated: 2026-08-12
updated_by: publish-deps.ps1
---

# libraster 1.0.0

| Field | Value |
|-------|-------|
| Origin | Workspace `base/libraster` |
| Artifact | `lib/libraster.lib` (x64, /MT, Release) |
| Headers | `include/` (flat) |
| Sources | Not shipped in `deps/`; reviewable at workspace `base/libraster` / <https://github.com/rle4k/encoder-rle4k-cpu> |
| Expiry (UTC end-of-day) | 2027-12-30 |
| Gate | embedded (system time + EXE mtime) |

## Headers

```
include/
  bitmap_info.h      # bitmap container / formats
  rasterizer.h       # block rasterizer API
  dif_loader.h       # bundled libdif public header (dependency)
  cndefs.h
  base/shared.h
```

Include as `#include <bitmap_info.h>`, `#include <rasterizer.h>`, `#include <dif_loader.h>`.

## Role

Rasterize cad_document to bitmap_info only. Does not encode/compare codecs.

## License

[MIT](LICENSE)

## Contact

yuqp78@foxmail.com
