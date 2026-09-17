# Win11 任务栏控件验证

> 本文记录独立 UI 验证程序。后续已将其接入正式主程序，数据与控制的最新范围见 [Win11 播放器对接](win11-player-integration.md)；下面的“尚未连接”仅指独立 `NCMMiniTaskbarProbe.exe`。

## 当前范围

独立程序 `NCMMiniTaskbarProbe.exe` 完成：UI Automation 查询空位、SetParent 嵌入主任务栏、透明绘制、封面、上一首/播放暂停/下一首按钮、悬停和按下反馈、真实鼠标测试、退出清理。

**尚未连接网易云音乐。** 上一首/下一首记录按钮事件，播放/暂停切换演示状态；文字显示 `NCM Mini / 未连接`，不是实际歌名。未替换正式 `NCMMini.exe`，未修改原有 DeskBand 源码或协议，未运行安装脚本、重启 Explorer、修改系统主题或安装依赖。

## 外观与交互

- GDI+ 绘制预乘 Alpha，文字使用 Windows 自带 Microsoft YaHei UI 常规字重（2026-09-17 替换原 Segoe UI），图标使用 Segoe Fluent Icons（后备 Segoe MDL2 Assets）。不使用旧的整块底色，也不把半透明像素强制改为不透明。
- 空闲时透出任务栏背景，圆角外完全透明。控件内部空白保留 **1/255 Alpha**，肉眼近乎透明，但能够接收鼠标；完全为零的像素会让鼠标穿透。
- 鼠标进入显示轻微圆角高亮，按钮有更亮的局部高亮，按住有不同反馈。鼠标离开恢复原背景，按下后拖出再松开取消点击。
- 读取系统明暗主题；大小为 `360x40 DIP`，封面 `32x32 DIP`，按钮点击范围各 `32x32 DIP`。歌名和状态分两行，过长文字省略，不挤占按钮。
- 内置唱片风格默认封面，支持 `--cover` 加载本地图片。缺失、空路径或损坏图片回退到默认封面；图片解码后不占用源文件。
- 按钮使用中文悬停提示；右键退出。封面和文字不触发播放按钮。

## 运行方法

在 PowerShell 7 中运行：

```powershell
Set-Location 'C:\Users\bao\project\ncm-mini'
.\scripts\build-taskbar-probe.ps1 -Configuration Release
.\artifacts\taskbar-probe\NCMMiniTaskbarProbe.exe --duration 60
```

试用自己的封面：

```powershell
.\artifacts\taskbar-probe\NCMMiniTaskbarProbe.exe --cover 'C:\path\cover.png' --duration 60
```

默认 30 秒后退出，`--duration` 可设置 1 到 3600 秒。空间不足或 UIA 无法确认边界时保持隐藏并重试，不覆盖任务栏已有按钮。只查询布局、不显示控件：

```powershell
.\artifacts\taskbar-probe\NCMMiniTaskbarProbe.exe --scan-only
```

## 真实鼠标测试

```powershell
.\artifacts\taskbar-probe\NCMMiniTaskbarProbe.exe --self-test --duration 20
```

约 8 秒内不要移动鼠标。测试用 `SetCursorPos` 移动真实指针，用 `SendInput` 分步发送按下和松开事件；每步之间由窗口正常消息循环处理，不向窗口伪造 `WM_MOUSELEAVE`。

检查按钮的空白边缘也可点击、上一首/下一首不切换播放状态、播放再暂停、拖出取消、封面不误触发、真实移出清除高亮，以及截图中背景像素的变化和还原。有前台参照窗口时验证不抢焦点；建立不了参照窗口明确输出 `SKIP`，不计作通过。检测到外部鼠标移动则取消测试。

完成或失败后退出，清理子窗口，只在鼠标仍停在测试点时恢复原位置。成功退出码为 0，失败为 1，参数错误或重复实例为 2。

截图保存在 `artifacts/taskbar-probe`：`probe-idle.bmp`、`probe-hover.bmp`、`probe-pressed.bmp`、`probe-playing.bmp`、`probe-leave.bmp`。仅截取控件矩形；最后一帧应恢复空闲状态，播放状态帧显示暂停图标。

## 实测记录

2026-09-16，本机 Windows 11 25H2，build `26200.9457`，主任务栏左对齐，200% 缩放（DPI 192）。

| 项目 | 结果 |
| --- | --- |
| Release 和 Debug 构建 | 通过，WinLibs GCC 16.1.0，无新增依赖安装 |
| 布局与命中检查 | 19 项通过 |
| 绘制、透明度、封面解码与回退 | 70 项断言通过 |
| 100% / 150% / 200%，浅色 / 深色 | 离屏绘制测试通过，不等于切换系统后的实机验证 |
| 长文字不覆盖按钮、按下反馈、播放暂停差异 | 像素断言通过 |
| 最终实际显示 | `(998,1832)`，`720x80` 物理像素，父窗口为 `Shell_TrayWnd`；空位随应用图标数量变化 |
| 真实鼠标点击 | 上一首 1 次、播放暂停 2 次、下一首 1 次 |
| 按钮透明边缘命中、拖出取消、封面不误触发 | 通过 |
| 真实移出、截图背景变化及还原 | 通过，收到 2 次实际鼠标移出消息 |
| 前台焦点保持 | 本次成功建立参照窗口并通过 |
| 故意失焦的负向测试 | 在鼠标移动前明确失败，不能降级为跳过 |
| 清理 | 子窗口销毁，测试进程退出 |

最终样式的成功测试输出摘要：

```text
PASS real hover reaches full target including padding
PASS hover changes actual screen pixels
PASS pressed changes actual screen pixels
PASS drag out cancels click
PASS real mouse leave clears hover
PASS screen background restored after leave
PASS foreground focus preserved
self_test previous=1 play=2 next=1 mouse_leaves=2 focus=PASS result=PASS
cleanup child_destroyed=1
```

复测中有运行因外部鼠标移动而中止；没有计作通过，重新运行后通过。浅色任务栏实际截图已查看；`view-light.png`、`view-dark.png` 是离屏绘制预览，不能当作深色任务栏实机截图。

焦点检查固定使用显示控件前确认的参照窗口，每个测试阶段都核对，不在挂接后重新判定是否可检查。使用 `build-taskbar-probe.ps1 -Configuration Release -BuildFocusRegression` 可额外构建 `taskbar_focus_loss.exe`；运行时主测试程序必须已退出。这个负向用例故意传入非前台句柄，预期输出 `FAIL foreground matches startup witness` 并返回 1，这才代表它正确拦截了失焦。已实际运行确认。

## 待验证与后续

- 网易云歌曲/封面/歌词数据和真正播放命令接入，正式程序的模式选择与安装。
- Win10 DeskBand 实际加载与交互。本轮未修改旧路径；此前构建通过不能代替 Win10 实测。
- 系统深色主题切换、其他实际显示缩放、居中任务栏、自动隐藏、主显示器切换、Explorer 重启恢复。
- 当前固定宽度空间不足就隐藏，后续实现紧凑布局；高对比度主题和屏幕阅读器也尚未验证。

## 文件职责

- `src/NCMMini.HostCpp/TaskbarLayout.*`：空位计算、命中。
- `src/NCMMini.HostCpp/TaskbarScanner.*`：后台 UIA 查询、边界校验。
- `src/NCMMini.HostCpp/TaskbarView.*`：封面缓存、内容布局、GDI+ 绘制。
- `src/NCMMini.HostCpp/Win11TaskbarWindow.*`：窗口嵌入、鼠标状态、工具提示、生命周期。
- `tools/TaskbarProbe.cpp`：命令行入口、单实例和测试参照窗口。
- `tools/TaskbarMouseTest.cpp`：分步鼠标自检和屏幕截图。
- `tests/taskbar_layout_tests.cpp`、`tests/taskbar_view_tests.cpp`：可重复的布局和绘制检查。
- `scripts/build-taskbar-probe.ps1`：构建并运行两套检查，不安装或注册控件。

此前核实的 SPlayer-Next 固定提交 `dc8eab18496faa41d5c8e72d12927b5fcb29e845` 提供窗口嵌入思路；此处是独立 C++ 实现，并非向 WinUI 的系统按钮集合插入原生按钮。
