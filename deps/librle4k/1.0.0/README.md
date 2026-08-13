---
title: librle4k 1.0.0
created: 2026-08-12
author: publish-deps.ps1
last_updated: 2026-08-12
updated_by: publish-deps.ps1
---

# librle4k 1.0.0

| Field | Value |
|-------|-------|
| Origin | Workspace `base/librle4k` |
| Artifact | `lib/librle4k.lib` (x64, /MT, Release) |
| Headers | `include/` (flat: `rle4k.h`) |
| Sources | Not shipped in `deps/`; reviewable at workspace `base/librle4k` / <https://github.com/rle4k/encoder-rle4k-cpu> |
| Gate | embedded (system time + EXE mtime) |

## Headers

```
include/
  rle4k.h
  cndefs.h
  base/shared.h
```

Include as `#include <rle4k.h>`.

## Role

RLE4K encode/decode only. Does not load DIF or rasterize geometry.

## License

[MIT](LICENSE)

## Contact

yuqp78@foxmail.com
