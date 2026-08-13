# libcore (header-only)

Minimal headers for `redcli`. CMake adds this directory to the include path.

| Header | Purpose |
|--------|---------|
| `cndefs.h` | Common typedefs |
| `base/shared.h` | Namespace macros (`rle4k::cam`, `rle4k::fill`) |

```cpp
#include "cndefs.h"
#include "base/shared.h"
```

Codec / DIF / raster implementations come from `deps/` (`librle4k`, `libdif`, `libraster`), not from this folder.
