# macOS 开发环境配置

## 前置条件

- Apple Silicon：macOS 14 或更高版本
- Intel：macOS 12 或更高版本
- Homebrew
- Xcode Command Line Tools：

```sh
xcode-select --install
```

克隆仓库后，在项目根目录执行：

```sh
git submodule update --init --recursive
dev/macos/setup.sh
```

`setup.sh` 会安装 CMake、Ninja、mpv、create-dmg 和 aqtinstall，并下载项目固定的 Qt
6.10.1（含 WebEngine、WebChannel 和 Positioning）。依赖和构建产物分别位于
`dev/macos/deps/` 与 `build/`，均不提交到 Git。

## 构建与验证

```sh
dev/macos/build.sh   # 构建
dev/macos/test.sh    # 运行单元测试
dev/macos/run.sh     # 启动开发版
```

开发版位于 `build/src/Jellyfin Desktop.app`。制作发布包时执行：

```sh
dev/macos/bundle.sh
```

## 常见问题

- `deno` 或 Homebrew 提示需要完整 Xcode：当前 Homebrew 已停止为 macOS 14 的部分新版依赖提供
  bottle。安装 Xcode 15+，执行 `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`
  后重试；也可以升级 macOS。
- Qt 下载超时：重新运行 `dev/macos/setup.sh`；已安装的 Qt 目录会被保留。
- 黑屏或 GPU 异常：运行 `dev/macos/run.sh --software-rendering`。
- 需要干净构建：删除 `build/` 后重新执行 `dev/macos/build.sh`。
- 日志目录：`~/Library/Logs/Jellyfin Desktop/<profile-id>/`。
