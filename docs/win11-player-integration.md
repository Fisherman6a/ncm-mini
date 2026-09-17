# Win11 Player Integration

## 使用与边界

正式接入程序：`artifacts/win11-integration/NCMMini.exe`。不需要注册 DLL、安装软件或重启 Explorer。原有 `install.ps1` 仍是旧 DeskBand 安装流程，本轮不要用它安装 Win11 模式。

运行下列命令连接当前网易云，不会在退出控件时关闭网易云：

```powershell
& 'C:\Users\bao\project\ncm-mini\artifacts\win11-integration\NCMMini.exe' --taskbar win11 --no-launch --keep-player --no-lyrics
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

播放/暂停图标现在读取网易云 Chromium 页面通过 MSAA 暴露的底部传输按钮：同一组相邻的 `pre`、`play`/`pause`、`next` 才算有效。网易云显示 `pause` 时控件显示 ⏸，显示 `play` 时控件显示 ▶；找不到或出现歧义时显示未知状态，不按点击次数猜测。实测任务栏控件两次点击均在约 1.1～1.2 秒内完成 `paused -> playing -> paused`，截图分别显示 ⏸ 和 ▶；此计时包含 750 毫秒的稳定等待及截图，不代表纯状态读取延迟。CoreAudio 读取已移除，避免把“音频流仍在运行”误当成歌曲播放。

本机实际验证客户端为商店版网易云音乐 `3.1.23.204814`。发现过程限制子节点数、总节点数和深度，并设有 400 毫秒的软搜索预算；同步 COM 调用本身没有硬超时，客户端无响应时仍可能拖慢轮询或退出。未来客户端改变按钮名称或页面结构时，可能返回未知状态，需要重新验证适配。

控制命令沿用 `icon` 窗口的 `WM_COMMAND`，现以 `PostMessage` 排队。日志中的 `queued=1` 只表示排队成功，不能单独证明播放切换；状态由网易云按钮读回确认。回归测试覆盖按钮组识别、外部状态变化、窗口树重建、歧义和失败回退；真实任务栏点击测试另外验证了暂停和播放两个方向及初始状态恢复。

歌词沿用原有换歌后计时方式，尚未接入真实播放进度，暂停或拖动后的精确歌词同步不在本轮范围。封面下载仍在轮询线程同步执行，慢网络时状态更新可能延后，但绘制和按钮命令线程独立。新模式保持系统主题配色，旧 DeskBand 的自定义字体/颜色没有改动，尚未全部映射到新模式。

两行文字统一使用 `Microsoft YaHei UI` 常规字重。实际字形检查确认 Segoe UI 缺少截图里的中文，而微软雅黑 UI 包含该歌名、歌手及测试用日文；保留图标专用的 Segoe Fluent Icons。测试输出 `artifacts/taskbar-probe/font-regression-light.png` 和 `font-regression-dark.png` 包含 100% / 150% / 200% 的原问题文字。字体代码和自动检查已完成，最终视觉效果仍待用户观察确认。

## 已验证

- 本机 Windows 11、200% 缩放下，正式主程序显示了真实网易云歌名和歌手，截图位于 `artifacts/integration-tests/live-taskbar.png`。
- 真实任务栏鼠标测试通过：`paused -> playing` 用时约 1.1～1.2 秒，`playing -> paused` 用时约 1.1～1.2 秒，该轮测试恢复初始暂停状态；`live-playing.png` 显示 ⏸，`live-paused.png` 显示 ▶。测试之后又进行了本体操作，不以该轮恢复结果推断当前播放器状态。
- 网易云本体操作已确认其按钮名称会在 `play` 与 `pause` 间变化；独立点击本体“播放”后，控件日志在没有控件命令的情况下读到 Playing。随后用户需要占用桌面，停止所有桌面操作，因此本体“暂停”反向同步及完整双向截图验收尚未完成。
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

## References

- Chromium 91 cache structures: https://chromium.googlesource.com/chromium/src/+/91.0.4472.164/net/disk_cache/blockfile/disk_format.h
- MSAA `IAccessible`: https://learn.microsoft.com/en-us/windows/win32/api/oleacc/nn-oleacc-iaccessible
- Queued window messages: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew
