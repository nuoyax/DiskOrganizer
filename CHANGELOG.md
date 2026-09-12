# Changelog

本文档记录 DiskOrganizer 的版本变更。推送形如 `vX.Y.Z` 的 tag 时，GitHub Actions 会自动打包，并把本文件中对应版本章节（若存在）写入 Release 说明。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 计划

- （在此记录尚未发版的改动）

## [1.0.0] - 2026-09-12

### 新增

- 浅色 UI 对齐 stitch 稿：概览 / 清理 / 大文件 / 重复文件 / 空间分析 / 碎片整理
- 大文件页：操作按钮集中、列表贴底加高、系统文件置灰不可选、行右键菜单
- 概览快捷维护「分析 / 清理」跳转时同步左侧导航

### 修复

- 清理分类卡与明细树勾选双向同步；hero 容量单位与底部一致
- 删除结果核验（拦截 pagefile 等锁定文件的假成功）
- 概览快捷维护按钮文案裁切；驱动器对比展示可用空间

### 性能

- 垃圾清理批量 `SHFileOperation` 删除，失败时逐项回退
