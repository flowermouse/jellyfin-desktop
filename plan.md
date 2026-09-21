# Jellyfin Desktop 调用外部 IINA 播放视频：实现计划

## 1. 目标与结论

在 macOS 上，Jellyfin Desktop 不再在自身窗口中播放视频，而是把 Jellyfin Web 选出的实际播放流交给用户已经安装的官方 IINA。IINA 以独立应用和独立窗口运行。

实现不得修改、编译或随包分发 `external/iina`。该目录只用于核对 IINA 的公开行为。运行时依赖用户安装的官方 IINA，bundle identifier 为 `com.colliderli.iina`。

推荐分两层实现：

1. 使用 IINA 官方 URL Scheme 或 `iina-cli` 启动播放。
2. 使用 `iina-cli` 设置 mpv JSON IPC socket，由 Jellyfin Desktop 与 IINA 内的 mpv 双向同步播放状态。

仅完成第 1 层只能“打开视频”，无法可靠上报 Jellyfin 播放进度、停止原因和自然播放结束。产品级实现应完成第 2 层。

## 2. 已确定的产品范围

### 包含

- 仅在 macOS 上将视频交给外部 IINA。
- 音频继续使用现有 `mpvAudioPlayer` 和内置 `PlayerComponent`。
- 支持官方稳定版 IINA，不依赖本仓库内的 IINA 源码。
- 支持从 Jellyfin 指定的恢复位置开始播放。
- 将播放、暂停、Seek、音量、静音、倍速、时长、当前位置、自然结束、用户停止和错误状态同步回 Jellyfin Web。
- Jellyfin Web 继续负责 PlaybackStart、PlaybackProgress、PlaybackStopped 等服务器会话上报。
- IINA 未安装或启动失败时给出明确错误，不回退到 HTML5 播放器。
- 非 macOS 构建继续使用现有内置 MPV 视频播放器，除非后续明确决定这个 fork 只支持 macOS。

### 暂不包含

- 将 IINA 嵌入 Jellyfin Desktop 窗口。
- 修改或 fork IINA。
- 将 IINA.app 打包进 Jellyfin Desktop。
- 多个并发 IINA 播放会话。
- SyncPlay 的完整兼容保证。
- 将音乐播放交给 IINA。
- IINA 插件、IINA 自定义菜单或 IINA 内的 Jellyfin 浏览界面。
- 跨应用无缝复用 Jellyfin 的原有网页 OSD；播放控制界面以 IINA 为准。

## 3. 当前代码边界

- `native/mpvVideoPlayer.js` 是 Jellyfin Web 的视频播放器适配器，负责把原生播放器信号转换成 Jellyfin Web 的 `playing`、`timeupdate`、`pause`、`stopped` 等事件。
- `native/mpvAudioPlayer.js` 继续使用 `window.api.player`，不要改为 IINA。
- `src/player/PlayerComponent.*` 和 `src/player/MpvVideoItem.*` 保留，音频和非 macOS 视频仍依赖它们。
- `src/core/ComponentManager.cpp` 负责把原生组件注册到 WebChannel。
- `src/system/SystemComponent::openExternalUrl()` 已能通过 `QDesktopServices` 打开 `iina://` URL，但产品级 IPC 集成需要单独的原生组件。
- 当前代码已经删除 `enableMPV`/HTML5 回退开关。实现时必须保留 `native/nativeshell.js` 和 `resources/settings/settings_description.json` 中的这项行为。
- `external/iina/` 当前是一个未跟踪的嵌套 Git 仓库，不要修改、提交或自动转成 submodule。

## 4. 推荐架构

新增一个 macOS 原生组件 `IinaPlayerComponent`，通过 WebChannel 暴露为 `window.api.iinaPlayer`。它负责：

- 检测官方 IINA 是否安装。
- 定位 IINA.app 和其中的 `iina-cli`。
- 为每次播放创建唯一的 mpv IPC socket。
- 使用 `QProcess` 启动 `iina-cli`，绝不通过 shell 拼接命令。
- 使用 `QLocalSocket` 连接 mpv JSON IPC。
- 将 WebChannel 调用翻译成 mpv JSON 命令。
- 将 mpv IPC 事件翻译成 Qt 信号。
- 管理一次播放的状态机、超时、清理和错误。

数据流：

```text
Jellyfin Web
  -> mpvVideoPlayer.js
  -> WebChannel: iinaPlayer
  -> iina-cli
  -> 官方 IINA.app / libmpv

IINA / mpv JSON IPC
  -> IinaPlayerComponent
  -> Qt signals
  -> mpvVideoPlayer.js events
  -> Jellyfin Web playback session reporting
```

不要让 `IinaPlayerComponent` 继承 `PlayerComponent`。两者生命周期和渲染模型不同，强行继承会把大量窗口、MpvQt 和内部播放状态带入外部播放器。为 JavaScript 提供相似的最小接口即可。

## 5. 实施阶段

### 阶段 A：先验证官方 IINA 的启动和 IPC 行为

在正式改播放器适配器前，制作一个临时的、不可提交的验证程序或聚焦测试，验证以下场景：

1. IINA 未运行时，通过 app bundle 中的 `iina-cli` 启动视频。
2. IINA 已运行且已有窗口时，再次启动视频。
3. 传入 `--mpv-input-ipc-server=<socket>` 后 socket 是否稳定创建。
4. 是否能通过 socket 观察 `time-pos`、`duration` 和 `pause`。
5. 自然播放结束、点击 IINA 停止、关闭播放窗口、退出 IINA 时分别收到什么事件。
6. `--mpv-start=<seconds>`、音轨、字幕和 HTTP headers 是否应用于正确的播放器实例。
7. 连续播放两集时旧 socket 是否释放，新会话是否连接到正确的 IINA 播放器。

这是继续采用“不修改 IINA”路线的门槛。如果官方 IINA 在已运行场景中不能可靠为新播放器应用 `input-ipc-server`，先记录验证结果，不要直接修改 IINA。备选方案依次为：

1. 使用 IINA 官方插件提供本地 WebSocket 桥。
2. 维护一个极小的 IINA 插件，而不是修改 IINA 主程序。
3. 最后才考虑 IINA 分叉。

### 阶段 B：增加 IINA 安装检测和定位

新增建议文件：

- `src/player/IinaPlayerComponent.h`
- `src/player/IinaPlayerComponent.cpp`
- `src/player/osx/IinaApplication.h`
- `src/player/osx/IinaApplication.mm`
- 对应的 `src/player/CMakeLists.txt` macOS 条件编译配置

在 Objective-C++ helper 中使用 `NSWorkspace` 按 bundle identifier `com.colliderli.iina` 查找应用，不要硬编码 `/Applications/IINA.app`。需要返回：

- 是否安装。
- IINA.app 的规范化路径。
- `Contents/MacOS/iina-cli` 的路径。
- 可选的 IINA 版本号，用于诊断信息，但不要以版本字符串作为主要功能判断。

公开接口建议：

```cpp
Q_INVOKABLE bool isAvailable() const;
Q_INVOKABLE QString applicationPath() const;
Q_INVOKABLE void openDownloadPage();
```

如果 IINA 不存在，JavaScript 端应停止 loading 状态并显示可本地化的错误/确认框。不得静默使用 HTML5。是否允许回退到内置 MPV，应作为后续独立产品决定，本计划默认不回退。

### 阶段 C：实现 URL Scheme 启动器

URL Scheme 作为最小启动方式和 CLI 不可用时的诊断工具。IINA 支持：

```text
iina://open?url=<encoded-url>&new_window=1&full_screen=1&mpv_start=<seconds>
```

必须使用 `QUrl` 和 `QUrlQuery` 构造嵌套 URL，禁止手工字符串拼接。支持的第一批参数：

- `url`
- `new_window=1`
- `full_screen=0|1`
- `pip=0|1`
- `mpv_start=<seconds>`
- `mpv_pause=yes|no`
- `mpv_user-agent=<value>`
- 必要时使用 IINA safelist 已允许的 `mpv_http-header-fields`

URL Scheme 没有稳定的返回通道，因此不能作为最终播放状态来源。若 IPC 初始化失败，不要假装播放成功并持续伪造进度。应该发出明确的 `integrationUnavailable` 错误，或者仅在显式的“单向打开模式”中使用 URL Scheme。

### 阶段 D：实现 CLI 启动

使用 `QProcess`，程序路径必须是检测到的 `iina-cli` 绝对路径，参数通过 `QStringList` 逐项传递。禁止执行 `/bin/sh -c`、`system()` 或拼接带引号的命令。

建议参数：

```text
--no-stdin
--separate-windows
--mpv-input-ipc-server=<socket-path>
--mpv-start=<seconds>
--mpv-user-agent=<user-agent>
<playback-url>
```

按需要增加：

- `--mpv-pause=yes|no`
- `--mpv-aid=<relative-id|no>`
- `--mpv-sid=<relative-id|no>`
- `--mpv-sub-file=<external-subtitle-url>`，前提是已验证网络字幕 URL 和鉴权
- `--mpv-speed=<rate>`
- `--mpv-volume=<0-100>`
- `--pip`

不要依赖 `iina-cli --keep-running` 判断单个视频何时结束。它等待的是 IINA 应用进程，而不是当前媒体。播放状态必须来自 mpv IPC。

socket 路径应短且唯一，例如 `/tmp/jfd-iina-<pid>-<counter>.sock`，以避免 macOS Unix domain socket 路径长度限制。创建前只允许删除当前进程自己生成并记录的精确 socket 文件，禁止使用宽泛 glob。

### 阶段 E：实现 mpv JSON IPC 客户端

`IinaPlayerComponent` 使用 `QLocalSocket` 连接 socket。启动 CLI 后用非阻塞 `QTimer` 重试连接，建议总超时 5～10 秒。不要阻塞 Qt 主线程。

协议处理必须支持：

- 一次读取包含多条以换行分隔的 JSON 消息。
- 半条 JSON 跨多个 `readyRead` 到达。
- request/response 与异步 event 交错。
- 每个 command 使用递增 `request_id`。
- 未知事件和未知字段向前兼容。
- socket 断开、IINA 退出和 IPC 超时。

连接成功后至少观察：

```text
time-pos
duration
pause
volume
mute
speed
aid
sid
track-list
demuxer-cache-state
cache-buffering-state
core-idle
eof-reached
path
```

同时处理 mpv 事件：

- `file-loaded`
- `playback-restart`
- `seek`
- `end-file`
- `idle`
- `shutdown`
- `log-message` 只在诊断级别启用，且必须做 URL/Token 脱敏

WebChannel 接口建议：

```cpp
Q_INVOKABLE void load(const QString& url,
                      const QVariantMap& options,
                      const QVariantMap& metadata,
                      const QVariant& audioStream,
                      const QVariant& subtitleStream);
Q_INVOKABLE void play();
Q_INVOKABLE void pause();
Q_INVOKABLE void stop();
Q_INVOKABLE void seekTo(qint64 milliseconds);
Q_INVOKABLE void setVolume(int volume);
Q_INVOKABLE void setMuted(bool muted);
Q_INVOKABLE void setPlaybackRate(int milliRate);
Q_INVOKABLE void setAudioStream(const QVariant& stream);
Q_INVOKABLE void setSubtitleStream(const QVariant& stream);
Q_INVOKABLE qint64 getPosition() const;
Q_INVOKABLE qint64 getDuration() const;
```

Qt signals 尽量与现有视频适配器需要的接口一致：

```cpp
void playing();
void paused();
void finished();
void canceled();
void error(const QString& message);
void positionUpdate(quint64 milliseconds);
void updateDuration(qint64 milliseconds);
void bufferedRangesUpdated(const QVariantList& ranges);
void availabilityChanged(bool available);
```

时间单位边界必须固定：WebChannel 和 JavaScript 使用毫秒，mpv IPC 的 `time-pos`/`duration` 使用秒。转换集中放在 C++ IPC 层并添加测试。

状态机建议：

```text
Idle -> Launching -> Connecting -> Loading -> Playing <-> Paused
                                           -> Finished -> Idle
                                           -> Canceled -> Idle
                                           -> Error -> Idle
```

只有收到 `file-loaded`/`playback-restart` 后才发出 `playing`。`end-file.reason=eof` 映射为 `finished`；主动调用 `stop()` 后的结束映射为 `canceled`；`error`、socket 超时和异常退出映射为 `error`。每个会话只能发出一次终止信号。

### 阶段 F：注册 WebChannel 组件

在 `ComponentManager::initialize()` 中仅在 macOS 注册 `IinaPlayerComponent`。组件名建议为 `iinaPlayer`，对应 JavaScript：

```javascript
window.api.iinaPlayer
```

非 macOS 不注册该对象。JavaScript 必须容忍对象不存在，并继续使用现有 `window.api.player`。

不要用 `iinaPlayer` 替换全局 `player` 名称，因为：

- `mpvAudioPlayer.js` 仍需要内置 PlayerComponent。
- input/MPRIS/taskbar 等现有代码依赖 `PlayerComponent`。
- 独立命名能避免把外部 IINA 的状态错误地当成本地渲染状态。

### 阶段 G：调整 Jellyfin Web 视频适配器

优先复用 `native/mpvVideoPlayer.js` 的 Jellyfin Web 协议适配逻辑，避免复制整份大文件。增加一个集中选择 native video backend 的 helper：

```javascript
function getNativeVideoBackend() {
    return window.api.iinaPlayer || window.api.player;
}
```

然后仅把视频适配器内部直接访问 `window.api.player` 的位置改成该 helper。不要修改 `mpvAudioPlayer.js`。

macOS 使用外部 IINA 时还要调整以下行为：

- 不创建黑色 `.videoPlayerContainer` 来承载本地视频。
- 不调用 `setVideoRectangle()`。
- 不跳转到 Jellyfin 的本地视频 OSD，或者确保该页面不会覆盖/劫持外部 IINA 的交互。
- IINA 启动后可以让 Jellyfin Desktop 保持在详情页；是否自动最小化作为后续设置，不放进第一版。
- 保留 Jellyfin Web 所需的 `syncPlayWrapAs = 'htmlvideoplayer'`。这只是播放器接口兼容标识，不是启用 HTML5 解码。
- 继续触发 `playing`、`timeupdate`、`pause`、`unpause`、`volumechange`、`waiting`、`stopped` 和错误事件，让 Jellyfin Web 自己执行服务器上报。
- `destroy()` 必须对称断开所有 Qt signal，避免连续播放后重复回调。
- 播放器名称可以改为 `IINA Video Player`，ID 建议保持稳定；如果更改 ID，需要验证 Jellyfin Web 对播放器选择和已保存状态的兼容性。

为了避免到处判断平台，可由 `IinaPlayerComponent` 提供 `externalPlayback=true` 属性，JavaScript 根据该属性禁用本地视频 UI 行为。

### 阶段 H：音轨、字幕和队列

第一版至少处理 Jellyfin 已选中的初始音轨和字幕：

- 复用 `mpvVideoPlayer.js` 当前从 Jellyfin stream index 映射到 mpv 相对 `aid`/`sid` 的算法。
- 嵌入音轨使用 `set_property aid/sid`。
- 关闭字幕使用 `sid=no`。
- 外挂字幕优先通过 mpv IPC 的 `sub-add` 命令加载，避免把长 URL 放进命令行。
- 所有外挂字幕 URL 同样视为敏感信息，不完整写日志。

播放队列第一版由 Jellyfin Web 驱动：当前项目在收到 `finished` 后决定是否播放下一集，每一集重新调用 `load()`。不要同时启用 IINA 自己的自动下一项和 Jellyfin Web 的自动下一集，避免重复播放。

## 6. 鉴权和隐私要求

Jellyfin 播放 URL 通常可能包含长期有效的 `api_key` 或其他访问参数，这是本功能最高风险点。

必须做到：

- Jellyfin Desktop 日志不得打印完整播放 URL、subtitle URL、HTTP header 或 CLI 参数。
- 复用或扩展现有 `Log.cpp` Token 脱敏策略，覆盖 `api_key`、`X-Emby-Token`、`Authorization`。
- `QProcess` 错误日志只记录退出码和脱敏后的诊断信息。
- 不把 URL 存进设置文件。
- socket 权限限制在当前用户，并在会话结束后删除。
- IPC server 只使用本地 Unix socket，不监听 TCP 端口。

已知限制：URL Scheme 会被 IINA 自身记录，CLI 参数也可能短暂出现在本机进程列表中。优先验证以下更安全的启动顺序：

1. 使用 CLI 只传 IPC socket 和非敏感 mpv 参数，先启动空播放器。
2. IPC 连接成功后再通过 `loadfile` 命令传递播放 URL。

如果官方 IINA 无法在无媒体 URL 时建立该 socket，则必须在 PR/交付说明中明确记录 Token 暴露边界。不要未经验证就声称该路径安全。更彻底的方案是本地鉴权代理，但它需要正确代理 Range、HEAD、重定向和 HLS 清单，不应混入第一版。

## 7. 错误处理

至少覆盖：

- IINA 未安装。
- 找到 IINA.app 但缺少或不能执行 `iina-cli`。
- `QProcess` 启动失败。
- IPC socket 在超时内未出现。
- IPC JSON 格式错误。
- IINA 拒绝 URL 或 mpv 参数。
- Jellyfin URL 返回 401/403/404。
- IINA 在播放中退出。
- 用户关闭 IINA 播放窗口。
- 新播放请求到来时旧会话仍然存在。

新 `load()` 到来时，先终止旧的 IPC 播放会话并完成清理，再启动新会话。状态机必须忽略来自旧 socket 的迟到事件。

## 8. 测试计划

### 自动化测试

新增 Qt Test，至少覆盖：

- IINA URL Scheme 的嵌套 URL 编码。
- 毫秒与秒转换。
- CLI 参数数组构造，不经过 shell。
- mpv IPC 单条、批量和分片 JSON 解析。
- request ID 与异步事件交错。
- `end-file` 原因到 finished/canceled/error 的映射。
- 重复终止事件只发出一次。
- 连接超时和 socket 断开。
- 日志脱敏，确保测试 URL 中的 Token 不出现在输出。
- 连续两次 load 时旧会话事件不会污染新会话。

尽量把 URL 构造、参数构造、IPC framing 和状态转换做成无 UI 的小类，以便单元测试，不要依赖真实 IINA 跑全部测试。

### 手动验证矩阵

- 官方 IINA 未安装。
- IINA 已安装但未运行。
- IINA 已运行且没有播放。
- IINA 已在播放其他本地文件。
- Jellyfin Direct Play。
- Jellyfin Transcode。
- 从 0 开始与从历史位置恢复。
- 暂停、恢复、Seek、倍速、音量、静音。
- 默认音轨、切换音轨、内嵌字幕、外挂字幕、关闭字幕。
- 播放自然结束。
- 在 IINA 中点击停止或关闭窗口。
- 播放中退出 IINA。
- 自动下一集，确认不会启动两份相同视频。
- 连续开始/停止/重新播放至少 10 次，确认无重复事件和遗留 socket。
- URL 含 Token 时检查 Jellyfin Desktop 日志。
- 窗口模式、IINA 全屏和 PiP。

每轮实现后执行：

```sh
node --check native/mpvVideoPlayer.js
dev/macos/build.sh
dev/macos/test.sh --output-on-failure
```

## 9. 验收标准

满足以下条件才算完成：

1. 用户只需安装官方 IINA，不需要编译定制版本。
2. 点击 Jellyfin 视频播放只启动一个正确的 IINA 播放会话，Jellyfin Desktop 内部不显示视频。
3. 恢复位置误差不超过 1 秒。
4. Jellyfin Web 能收到真实的播放、暂停、位置、Seek、结束和错误事件。
5. Jellyfin 服务器端播放进度能持续更新，停止后位置正确，自然结束能标记已播放。
6. 用户从 IINA 停止或关闭播放时，Jellyfin 不会永久停留在 loading/playing 状态。
7. IINA 未安装时显示明确提示，不使用 HTML5 回退。
8. 音频播放仍使用当前内置 MPV，不受影响。
9. 非 macOS 构建和现有测试不回归。
10. Jellyfin Desktop 日志中不出现完整 Token、认证 header 或未脱敏播放 URL。
11. 播放结束后不遗留本次创建的 socket、timer、signal connection 或活动 IPC 请求。

## 10. 建议提交拆分

为了便于审查和回退，按以下顺序提交：

1. `macOS: add IINA discovery and URL builder`
2. `player: add mpv JSON IPC client`
3. `macOS: add external IINA player component`
4. `web: route video playback through IINA on macOS`
5. `tests: cover IINA launch, IPC, lifecycle, and redaction`
6. `docs: document official IINA runtime dependency`

每个提交都不要包含 `external/iina`、构建产物、IINA.app、用户配置或日志。

## 11. 实现前必须回答的验证问题

另一个 agent 开始正式改造前，应先把以下结果记录在实现说明或 PR 中：

- 官方 IINA 在“应用已运行”时是否仍能为新窗口应用 `input-ipc-server`。
- 无媒体 URL 启动时能否先建立 IPC socket，再通过 IPC `loadfile`。
- 关闭单个 IINA 播放窗口会产生哪个 mpv 事件，是否会断开 socket。
- IINA 对 Jellyfin Direct Play/Transcode URL 是否保存历史，历史中是否包含 Token。
- 外挂字幕 URL 是否需要独立认证 header。
- 自动下一集应复用现有 IINA 窗口还是每集新建窗口。

如果这些验证结果与本计划假设冲突，应优先调整集成边界，而不是直接修改 IINA 主程序。
