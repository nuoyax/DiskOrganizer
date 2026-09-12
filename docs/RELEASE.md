# 发版说明

面向维护者。用户文档见根目录 [README.md](../README.md)。

## 流程

1. 在 [CHANGELOG.md](../CHANGELOG.md) 写好 `## [X.Y.Z]` 章节并提交  
2. 打 tag 并推送：

```bash
git tag vX.Y.Z
git push origin vX.Y.Z
```

3. GitHub Actions（[`.github/workflows/release.yml`](../.github/workflows/release.yml)）会：
   - 在 `windows-2022` 用 Qt 6.5.3 编译（`-DDISKORGANIZER_STATIC_RUNTIME=OFF`）
   - `windeployqt` 打包 `DiskOrganizer-vX.Y.Z-windows-x64.zip`
   - 创建 Release，正文 = CHANGELOG 对应章节 + 相对上一 tag 的 commit 列表

也可在 Actions 中手动运行 **Release**（可选 dry-run：只构建不发版）。

## 注意

- CI 产物为**共享 Qt**包；本地 `/MT` 全静态单文件仍按 README 构建方式产出  
- tag 需匹配 `v*.*.*`（例如 `v1.0.1`）；带 `-` 的 tag 会标为 prerelease  
