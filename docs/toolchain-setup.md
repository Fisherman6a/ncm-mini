# C++ 工具链安装与验证

## 当前状态

2026-09-16 检查结果：

- 用户已安装 PowerShell 7.6.6 和 WinLibs；编码代理使用的内置 PowerShell 为 7.6.5。
- 已确认 GCC 16.1.0 和 Binutils 2.47.20260726 可用，目标架构为 `x86_64-w64-mingw32`。
- 项目当前 `scripts/build.ps1` 使用 MinGW 的 `g++.exe`，Release 构建还需要 `strip.exe`。
- 初次自动安装请求被审批拦截，之后用户自行完成安装；本轮已经实际编译和运行验证。

## 选定工具链

使用 WinLibs 的 64 位 POSIX/UCRT 版本，与项目现有构建命令匹配。

| 项目 | 值 |
| --- | --- |
| WinGet 包 ID | `BrechtSanders.WinLibs.POSIX.UCRT` |
| 版本 | `16.1.0-14.0.0-r4` |
| GCC 版本 | `16.1.0` |
| MinGW-w64 版本 | `14.0.0` |
| 架构 | `x64` |
| 安装范围 | 当前用户 |
| 发布者 | Brecht Sanders |
| 安装形式 | ZIP 便携工具包，由 WinGet 管理 |
| ZIP SHA256 | `c406a22f8cac82559a3a1d96b62ff603f666499fb5ff4784e87b4eb6fa37dede` |

来源：[WinLibs 网站](https://winlibs.com/)、[本版本发布页](https://github.com/brechtsanders/winlibs_mingw/releases/tag/16.1.0posix-14.0.0-ucrt-r4)。

安装会下载并解压第三方开发工具，写入当前用户的工具目录，并可能更新用户 PATH 或命令链接。它不会修改本项目功能代码，也不需要改动 VS2022 的工作负载。

## 手动安装步骤

1. 打开 PowerShell 7 的终端，运行 `$PSVersionTable.PSVersion`，确认主版本为 `7`。使用普通用户终端即可，不必主动以管理员身份打开。
2. 执行下面命令：

```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT --exact --version 16.1.0-14.0.0-r4 --source winget --scope user --architecture x64 --accept-package-agreements --accept-source-agreements --disable-interactivity
if ($LASTEXITCODE -ne 0) { throw "WinLibs installation failed: $LASTEXITCODE" }
```

3. 安装成功后重新打开 PowerShell 7，运行：

```powershell
Get-Command g++.exe, strip.exe
g++ --version
g++ -dumpmachine
strip --version
```

预期 `g++` 报告版本 `16.1.0`，目标为 `x86_64-w64-mingw32`，`strip` 能输出版本。若安装成功但命令找不到，保留报错，继续检查 WinGet 的实际安装路径，不要重复安装其他编译器。

若 GitHub 下载确实遇到网络问题，可以在当前终端临时设置用户提供的代理后重试；仅在本机代理服务已启动时使用：

```powershell
$env:HTTP_PROXY = 'http://127.0.0.1:7897'
$env:HTTPS_PROXY = 'http://127.0.0.1:7897'
```

这只设置当前进程的环境变量，不修改系统代理；WinGet 是否使用该代理仍需以下载结果为准。

## 验证结果

以下项目已实际完成：

- `NCMMiniTaskbarProbe.exe` 编译成功，并通过 UI Automation 扫描本机任务栏。
- 原有 `NCMMiniBand.dll`、`NCMMiniBandCtl.exe`、`NCMMini.exe` 的 Release 构建成功，输出到独立的 `artifacts/toolchain-check` 目录。
- 验证程序 Debug/Release 均构建成功，Release 的 `strip` 操作成功。
- 四个产物均为 `pei-x86-64`，导入表未发现需要额外分发的 MinGW 运行时 DLL；列出的依赖为 Windows 系统库和 UCRT。
- 19 项布局/命中检查、70 项绘制/封面断言通过，真实任务栏嵌入、鼠标点击、焦点保持和清理自检通过。具体范围见 `win11-taskbar-validation.md`。

`scripts/build.ps1` 已移除停止程序、注册 DLL、重启 Explorer 的操作，支持 `-OutputDirectory` 和 `-CompilerPath`。工具链查找会优先使用 PATH，也能找到此次 WinGet 安装目录，因此旧终端不必修改系统 PATH。

## 常用命令

在项目根目录使用 PowerShell 7：

```powershell
.\scripts\build.ps1 -Configuration Release -OutputDirectory .\artifacts\toolchain-check
.\scripts\build-taskbar-probe.ps1 -Configuration Release
.\artifacts\taskbar-probe\NCMMiniTaskbarProbe.exe --scan-only
```

交互验证和已测范围见 [Win11 验证记录](win11-taskbar-validation.md)。目前无需再安装其他工具。
