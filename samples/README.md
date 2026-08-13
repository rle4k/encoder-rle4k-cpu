# Sample inputs

This repository does **not** ship Gerber files or `gbrcli`.
`redcli` reads **DIF** only.

## 1. Download Gerber samples

Public dataset (64 Gerber RS-274X files):

| Source | URL |
|--------|-----|
| IEEE DataPort (DOI) | https://dx.doi.org/10.21227/t0cy-rb10 |
| GitHub mirror | https://github.com/disking-cn/gerber_sample_files |
| Gitee mirror | https://gitee.com/disking-cn/gerber_sample_files |

## 2. Convert Gerber → DIF

Use external **`gbrcli`** (not bundled). Produce `DIF01.dif` … `DIF64.dif`
(or any names), then place them under `data/` or another folder.

## 3. Run

```powershell
.\build\Release\redcli.exe run --input data\DIF50.dif
.\scripts\test-single-file.ps1 data\DIF50.dif
.\scripts\test-folder.ps1 data
```

Bundled smoke fixture: `data/DIF50.dif`. See [data/README.md](../data/README.md).
