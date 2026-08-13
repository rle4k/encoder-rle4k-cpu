# DIF inputs

Local DIF files for `redcli`. Other files under `data/` are gitignored; keep your
full corpus here or pass another path via `--input`.

## Bundled smoke fixture

| File | Notes |
|------|--------|
| `DIF50.dif` | One converted sample from the public Gerber set. For local smoke only. |

```powershell
.\build\Release\redcli.exe run --input data\DIF50.dif
.\scripts\test-single-file.ps1 data\DIF50.dif
```

## Paper corpus

Experiments use **64** Gerber RS-274X samples converted to `DIF01`–`DIF64`:

| Source | URL |
|--------|-----|
| IEEE DataPort | https://dx.doi.org/10.21227/t0cy-rb10 |
| GitHub | https://github.com/disking-cn/gerber_sample_files |
| Gitee | https://gitee.com/disking-cn/gerber_sample_files |

Convert with external `gbrcli` (not shipped). Steps: [samples/README.md](../samples/README.md).
