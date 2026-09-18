# Win11 Player Integration

## 使用与边界

当前 Win11 验证产物：`artifacts/win11-page-recovery/NCMMini.exe`。不需要注册 DLL、安装软件或重启 Explorer。原有 `install.ps1` 仍是旧 DeskBand 安装流程，本轮不要用它安装 Win11 模式。

2026-09-18 的空树恢复试用产物位于 `artifacts/win11-page-recovery/NCMMini.exe`。为避免覆盖正在运行的控件，使用独立输出目录；先右键退出旧控件，再启动下列命令。构建不会自动替换或重启运行中的版本。

运行下列命令连接当前网易云，不会在退出控件时关闭网易云：

```powershell
& 'C:\Users\bao\project\ncm-mini\artifacts\win11-page-recovery\NCMMini.exe' --taskbar win11 --no-launch --keep-player --no-lyrics
```

`--no-launch` 只连接已运行的网易云，没运行时显示未连接并继续等待；右键菜单可打开设置或退出控件。`--keep-player` 保留网易云。`--no-lyrics` 可让第二行一直显示歌手。`--duration 60` 可用于限时试运行。

封面读取顺序：网易云自己的图片缓存 -> HTTPS 下载 -> 默认封面。按同一图片的完整地址匹配，兼容网易云的已知缩略图参数，不用其他歌曲的封面冒充。`--no-cover-download` 只禁止联网，仍然读取本地封面。2026-09-16 用户已明确允许访问封面地址，联网解码实测成功；2026-09-17 已验证完全禁止下载时仍能从真实网易云缓存解码出 40x40 BGRA 图片。

本地缓存只读支持已验证的普通版/商店版 `NetEase/CloudMusic/webapp91x64/Cache`，按 Chromium 91 blockfile 索引解析。拒绝损坏、写入中、过期、过大及身份不匹配的记录；其他缓存格式暂时回退下载，不修改网易云缓存。单目录最多访问 2048 条记录、读取 32 MiB。

## 数据流

```text
网易云窗口标题 + 普通版/商店版本地缓存
    -> 主程序读取歌名、歌手、歌词和封面
    -> 最新状态快照（进程内，带锁）
    -> Win11 窗口线程更新文字和封面

Win11 鼠标按钮
    -> 有界命令队列
    -> 独立命令线程
    -> 原 PlayerController::Send
    -> 网易云 icon 窗口 WM_COMMAND
```

这里复用项目已有的桌面客户端读取/控制方式，不是网易云官方开放 HTTP 播放接口。不读取或上传账号凭据；旧 DeskBand 继续走原有 v1 命名管道，协议字段未修改。

播放/暂停图标现在读取网易云 Chromium 页面通过 MSAA 暴露的底部传输按钮：同一组相邻的 `pre`、`play`/`pause`、`next` 才算有效。网易云显示 `pause` 时控件显示 ⏸，显示 `play` 时控件显示 ▶；找不到或出现歧义时内部状态为 Unknown，不按点击次数猜测。Unknown 目前也绘制 ▶，但悬停提示为“播放/暂停”，不能把此图标单独作为已暂停的证据。实测任务栏控件两次点击均在约 1.1～1.2 秒内完成 `paused -> playing -> paused`，截图分别显示 ⏸ 和 ▶；此计时包含 750 毫秒的稳定等待及截图，不代表纯状态读取延迟。CoreAudio 读取已移除，避免把“音频流仍在运行”误当成歌曲播放。

最小化到任务栏和隐藏到托盘不是同一种窗口状态。本机最小化后，`Chrome_RenderWidgetHostHWND` 从 `OrpheusBrowserHost` 移到了同一进程的独立 `Chrome_WidgetWin_0` 下；原逻辑只查主窗口子树，因此丢失播放状态。现在主窗口最小化时补查同一 PID 的 CEF 宿主，同时保留附着页面，仍核对完整播放按钮组；多个有效播放页面或超出扫描上限时返回 Unknown，不采用旧状态或音频会话猜测。

**2026-09-18 最小化刷新已接入。** 在商店版 `3.1.23.204814` 上，用户曾在 `win11-msaa-refresh` 产物上复测显示、最小化和隐藏到托盘三种状态，结果正常；随后报告锁屏后状态再次失效。该历史验收不等于新产物已通过锁屏恢复全流程验证。

正式逻辑只在主窗口最小化时，对同 PID 的附着渲染器和脱离到 `Chrome_WidgetWin_0` 下的渲染器执行只读刷新：先用 `accLocation` 取范围，再用中心点 `accHitTest` 请求 Chromium 更新 MSAA 页面，下一轮轮询读取 `pre` / `play|pause` / `next`。每个渲染器最短 500 毫秒请求一次；失败、结果未到达、候选不完整或出现多个有效页面时返回 Unknown，不沿用旧状态猜测。缓存只保存窗口句柄、路径和时间，不保存 COM 对象；正常显示和托盘路径不额外执行命中查询。该调用可能异步完成，因此它不是“同步刷新全部状态”的保证，仍保留后续轮询和真实客户端验证。

**2026-09-18 锁屏恢复补充。** Chromium 在无障碍模式被重置后可能只返回一个空的 MSAA 根节点。正式逻辑发现根节点子数为 0 时，先读取根名称，再用 100 毫秒超时向 `Chrome_RenderWidgetHostHWND` 发送只读的 `WM_GETOBJECT` 无障碍握手；这是 Chromium Windows 无障碍实现用来重新启用基础页面树的入口。每个渲染器最多每 2 秒请求一次，恢复前仍返回 Unknown；下一轮读取到完整的 `pre` / `play|pause` / `next` 组后才恢复图标状态。该路径不保存旧播放状态，也不点击或恢复网易云窗口。

本机实际验证客户端为商店版网易云音乐 `3.1.23.204814`。发现过程限制子节点数、总节点数和深度，并设有 400 毫秒的软搜索预算；同步 COM 调用本身没有硬超时，客户端无响应时仍可能拖慢轮询或退出。未来客户端改变按钮名称或页面结构时，可能返回未知状态，需要重新验证适配。

控制命令沿用 `icon` 窗口的 `WM_COMMAND`，现以 `PostMessage` 排队。日志中的 `queued=1` 只表示排队成功，不能单独证明播放切换；状态由网易云按钮读回确认。回归测试覆盖按钮组识别、外部状态变化、窗口树重建、歧义和失败回退；真实任务栏点击测试另外验证了暂停和播放两个方向及初始状态恢复。

歌词沿用原有换歌后计时方式，尚未接入真实播放进度，暂停或拖动后的精确歌词同步不在本轮范围。封面下载仍在轮询线程同步执行，慢网络时状态更新可能延后，但绘制和按钮命令线程独立。新模式保持系统主题配色，旧 DeskBand 的自定义字体/颜色没有改动，尚未全部映射到新模式。

两行文字统一使用 `Microsoft YaHei UI` 常规字重。实际字形检查确认 Segoe UI 缺少截图里的中文，而微软雅黑 UI 包含该歌名、歌手及测试用日文；保留图标专用的 Segoe Fluent Icons。测试输出 `artifacts/taskbar-probe/font-regression-light.png` 和 `font-regression-dark.png` 包含 100% / 150% / 200% 的原问题文字。字体代码和自动检查已完成，最终视觉效果仍待用户观察确认。

## 已验证

- 本机 Windows 11、200% 缩放下，最终主程序显示了真实网易云歌名和歌手，截图位于 `artifacts/integration-tests/live-taskbar.png`。
- 锁屏后网易云窗口恢复显示时，临时探针先读到 MSAA 根节点 `children=0`；发送无障碍握手后，页面树恢复并读到真实页面内容，正式恢复回归测试通过。
- 在网易云恢复显示并播放时，SMTC 探针返回 `COUNT=0`；本次运行没有可供外部程序读取的网易云媒体会话，因此 SMTC 暂不作为播放状态来源。网易云内部插件可以创建并发布自己的 SMTC 会话，但这需要插件在网易云进程内配合，不能由任务栏控件单方面开启。
- 真实任务栏鼠标测试通过：`paused -> playing` 用时约 1.1～1.2 秒，`playing -> paused` 用时约 1.1～1.2 秒，该轮测试恢复初始暂停状态；`live-playing.png` 显示 ⏸，`live-paused.png` 显示 ▶。测试之后又进行了本体操作，不以该轮恢复结果推断当前播放器状态。
- 当前播放状态回归共 60 项检查通过，包含显示/最小化下的空树异步恢复、反复失效及两秒重试限流；绑定检查及命令 2 项通过，Release 主程序/DLL/控制器构建成功。
- 本轮实机空树经临时探针请求后恢复，生产读取函数读到 Playing；用户随后点击暂停，新编译的正式诊断程序读到 Paused。没有自动切换窗口或锁屏；新产物的再次锁屏、最小化及托盘完整实机回归仍待验证。
- 生成测试数据验证 200 次连续更新显示最后一次；封面红蓝通道正确，实际屏幕像素可见；断开清空旧歌，禁用播放命令；窗口退出无残留。
- 隐藏启动、状态查询、带冲突模式参数的重复启动、定时退出和三次重复启动/退出通过。`--taskbar-status` 返回 0 表示可见、3 表示等待/隐藏、1 表示未运行或查询失败，只查询现代窗口，不启动播放器。
- 正式主程序 DPI 不感知的环境曾暴露后台扫描坐标错误；回归用例先失败，扫描线程单独使用物理坐标后通过。现代 DPI API 使用运行时查询，旧系统无需导入这些新函数才能启动旧模式。
- 原 DeskBand 源码和 v1 协议未修改，正式主程序/DLL/控制器均构建。Win10 实机加载、安装升级、自动隐藏、Explorer 重启和多屏仍未验证。

## 复跑检查

```powershell
.\scripts\test-taskbar-binding.ps1
.\scripts\test-media-cache.ps1 -VerifyLocalCache
.\scripts\test-cover-cache.ps1 -VerifyLocalCache
.\scripts\test-player-control.ps1
.\scripts\test-playback-accessibility.ps1
.\scripts\build-player-diagnostics.ps1
.\scripts\build-transport-tests.ps1
.\artifacts\integration-tests\taskbar_transport_tests.exe
.\scripts\test-host-lifecycle.ps1
.\scripts\test-live-player.ps1 -PlaybackOnly -DownloadCover
.\scripts\test-live-player.ps1 -SnapshotOnly
```

`-PlaybackOnly` 会真实鼠标点击两次播放/暂停，逐次读回状态，保存 `live-playing.png` / `live-paused.png` 并核对恢复初始状态。去掉 `-DownloadCover` 仍会读取本地封面。用 `-ClickButtons` 还会测试上一首、下一首；若初始暂停，切歌可能开始播放，因此建议播放中使用完整切歌测试。失败后脚本会提示状态可能已改变，不会自动补发切换命令。

`-SnapshotOnly` 只读状态和截图，不移动鼠标、不发送播放命令，可连接已有控件且不退出它；不存在控件时临时启动并退出自己的实例。所有真实桌面测试必须在解锁状态下运行，锁屏时直接失败，不把空白截图算作通过。测试保留网易云，不关闭播放器。

## Scope

Continue the approved Win11 plan: connect the tested taskbar UI to the existing player backend inside NCMMini.exe. Keep DeskBand and its v1 pipe protocol unchanged. No installation or Explorer restart in this task.

## Implementation Checklist

- [x] Extend the view to accept current BandState text and 40x40 BGRA album pixels; default artwork on disconnect or invalid image.
- [x] Publish state through a bounded latest-state mailbox to the UI thread; execute player commands on a separate worker, never inside the window procedure.
- [x] Add auto/win11/deskband mode selection, preserve no-show-band, duplicate-instance behavior and settings/exit actions.
- [x] Support the actual installed NetEase cache location without changing user data.
- [x] Build and test state mapping, binary cover data, mode parsing, live metadata and actual command delivery.
- [x] Verify live album artwork download after explicit network approval, and native-cache decoding with downloads disabled.

## Decisions

- Use in-process snapshots for Win11, existing named pipe for DeskBand; do not create a second pipe client or alter protocol fields.
- Read the NetEase MSAA transport-button group for the Win11 play/pause icon; preserve Unknown for missing or ambiguous trees. Do not infer state from click counts. Keep the legacy v1 wire structure unchanged, and skip the new accessibility read in legacy DeskBand mode.
- Preserve the approved transparent/theme-following appearance. Pass song/artist/lyric and cover data; old DeskBand colors/layout remain untouched.
- Independent probe and its mouse self-test remain runnable. Test-only input injection must not be linked into the production host.
- UI-only tests may use generated data, but live verification must identify actual NetEase state and separately report unverified cases.

## Progress

2026-09-16: Read Host.cpp, Media.cpp, main.cpp, PipeServer and existing probe. Normal data path and packaged NetEase installation differ on this machine; delegated cache compatibility while integrating the host/view locally.

2026-09-16: 21 binding assertions plus aggregate check passed; 68 cache assertions and 3 local read-only cache checks passed. Production transport tests, lifecycle tests and real next/previous commands passed. Corrected scan-thread DPI coordinates, shutdown notification synchronization, duplicate-instance mode routing and track/artist/album ID collisions. No git commits, installations, Explorer restarts or account changes.

2026-09-17: Added native cover-cache reuse, NetEase MSAA playback-state readback and YaHei UI text rendering. Fixed missing visual.playing propagation and common-controls v5 tooltip TOOLINFO size. Replaced the slow CoreAudio state guess with validated `pre`/`play|pause`/`next` discovery and added bounded discovery tests. Real taskbar clicks verified both playback transitions and restored the initial state in that test run. External-player Play propagation was observed in the host log; the full external-player two-way screenshot test was paused at the user's request. A final compatibility change restricts accessibility reads to Win11 mode; the Release build and offline regression tests passed after that change, while live tests preceded it. The legacy DeskBand source and protocol remain unchanged. Final font appearance remains subject to user acceptance.

2026-09-18: Added minimized-page refresh and empty-tree recovery. Release build and 60 playback assertions passed. Prior user validation of displayed/minimized/tray states used win11-msaa-refresh, before the lock-screen failure. The recovery probe restored the real empty tree, and production readback observed Playing followed by the user's Pause. The new win11-page-recovery executable still needs a full live lock/unlock and window-state regression. SMTC returned zero sessions in the normal user session and is not integrated. The legacy DeskBand source and v1 protocol remain unchanged.

## References

- Chromium 91 cache structures: https://chromium.googlesource.com/chromium/src/+/91.0.4472.164/net/disk_cache/blockfile/disk_format.h
- MSAA `IAccessible`: https://learn.microsoft.com/en-us/windows/win32/api/oleacc/nn-oleacc-iaccessible
- Chromium 91 MSAA 命中查询入口： https://github.com/chromium/chromium/blob/91.0.4472.164/ui/accessibility/platform/ax_platform_node_win.cc#L760
- Chromium 91 向渲染端请求新查询： https://github.com/chromium/chromium/blob/91.0.4472.164/content/browser/accessibility/browser_accessibility_manager.cc#L1611
- Chromium 91 Windows `WM_GETOBJECT` 无障碍入口： https://github.com/chromium/chromium/blob/91.0.4472.164/content/browser/renderer_host/legacy_render_widget_host_win.cc#L192
- Chromium 91 Windows 无障碍握手与 `get_accName`： https://github.com/chromium/chromium/blob/91.0.4472.164/content/browser/accessibility/browser_accessibility_state_impl_win.cc#L39
- Queued window messages: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew
