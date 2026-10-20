# EFF Android

安卓 ARM64 端的实验性 EFF Mod 加载器，当前版本 **0.5.0**。通过兼容 libxposed API 102 的框架加载，在手机上导入 EFF 格式 2 作者资源 ZIP，使用游戏自身 VFS 编译并切换角色模型。

本仓库包含 Android Java/C++ 源码、Gradle Wrapper、原生编译器、VFS 工具、离线验证工具，以及构建所需的第三方源码。无需本项目原作者的本地 `source-review` 目录。

## 当前功能与验证范围

- 手机直接导入原始 EFF 格式 2 ZIP；最多选择四个 Mod 并合并编译，共用 CAB 只重建一次。
- 在已进入大世界时应用或恢复 Mod；已打开的角色界面需要关闭后重新打开。
- 保存已选择的 Mod；提供作者已声明的形态键滑条。无形态键数据的 Mod 不会自动生成体型变化。
- 已在国际服 1.6.4、ARM64 Android 16、libxposed API 102 环境验证 si，以及 si + Liino 两个角色同时加载；日志确认刷新两名角色。
- 四个不同角色同屏、真实形态键滑条效果、长时间战斗与全部 LOD 尚未完成验证。
- **Liino 转换版的裙摆、腿部存在银白反光等材质偏差，尚未修复。** 模型加载成功不代表与 EFMI 外观一致。

## 手机使用

1. 安装自行构建的 APK，在兼容框架中启用 `dev.eff.probe`，作用域选终末地。
2. **更新插件 APK 前先强制停止终末地，再安装新版并重新启动游戏。** 直接覆盖安装后仅返回游戏可能继续使用旧进程中的模块。
3. 在加载器中导入包含一个 `mod.ini`、`source-inputs.bin` 及其声明资源的 EFF ZIP。
4. 勾选最多四个 Mod，点击“应用所选 MOD”，返回游戏等待编译。首次编译耗时更长。
5. 点击“全部恢复原版”可恢复模型；已打开的角色界面需重新进入。

Mod、游戏 VFS、APK 资源包和解包后的游戏模型不随源码发布。EFMI `.buf` 包需要先转换；不能直接作为 EFF ZIP 导入。

## 构建

工具版本：Gradle 9.3.1、AGP 9.1.1、SDK 37、NDK 30.0.16248370、CMake 4.4.4。应用最低 Android 10，仅构建 `arm64-v8a`。工作站验证使用 Android Studio JDK 25；Java 源码目标为 17。

Windows 示例，替换为自己的工具路径：

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_android.ps1 `
  -Jdk C:\Android\jbr -Sdk C:\Android\sdk `
  -Ndk C:\Android\sdk\ndk\30.0.16248370 `
  -CmakeRoot C:\Android\sdk\cmake\4.4.4 -BuildTemp C:\Temp\eff-build
```

脚本执行 `assembleDebug` 和 `lintDebug`，生成 `dist/EFF-Android-0.5.0.apk`。也可在 Android Studio 打开 `android/`，安装指定 SDK/NDK/CMake 后构建。可用 `-PeffNdkPath=...` 指定独立 NDK，未指定时使用 SDK 管理的 NDK。

Debug 构建使用构建者自己的 Android debug key。源码不含本地签名密钥，因此自行构建的 APK 不能保证覆盖此前发布者签名的 APK；签名不一致时需先备份 Mod 库再卸载旧版。保留包名 `dev.eff.probe`，更换包名需要同步 provider、JNI 和框架配置。

## 开发工具

`tools/requirements-vfs.txt` 为 VFS 脚本依赖；`tools/requirements-conversion.txt` 为转换工具依赖。ADB UI 测试工具只在显式运行时操作连接的设备。

在 Visual Studio x64 Native Tools Command Prompt 中执行 `tools\build_author_tools.cmd`，可构建离线来源选择、合并编译验证工具及 Python 使用的 `eff_author_core.dll`。DLL 由仓库内源码生成，不需要随仓库提交二进制。

`tools/convert_liino_eff.py` 是针对 Moonlit Violet 包的**默认穿搭转换实验**，不是通用 EFMI 转换器。需自行提供原 EFMI 目录以及 AnimeStudio 导出的完整 EFFSOURCE 基线。例如：

```powershell
python -m pip install -r tools/requirements-conversion.txt
New-Item -ItemType Directory -Force reports
python tools/convert_liino_eff.py --source C:\Mods\Liino --baseline C:\Exports\liino.eff --output C:\Converted\liino-eff
```

输出目录必须不存在。转换不会运行包内 INI/HLSL 代码；仅解析支持的数值表达式和绘制列表。原菜单、穿搭按键及 HLSL 动态叠加效果未移植。当前转换结果仍有上述材质问题。

## 目录

| 目录 | 内容 |
| --- | --- |
| `android/` | 应用、框架入口、JNI、运行时加载、原生 EFF 编译器及 Gradle Wrapper |
| `tools/` | VFS 检查与补全、EFF 转换、来源选择、离线编译及 ADB 验证工具 |
| `third_party/eff/` | EFF 1.8.1 编译器头文件、作者工具及许可证 |
| `third_party/eff_blender/` | 转换工具所需的纯 Python EFF 网格读取器及许可证 |
| `third_party/dobby/` | Dobby 源码及许可证 |

诊断日志位于游戏外部文件目录的 `eff-android/`，如 `bootstrap.log`、`hot-loader.log`、`hot-status.json`。日志追加写入，按最新 BOOT/PID 区分运行。提交 Issue 前请移除 UID、账号信息、设备标识和私人路径。

## 已知限制

资源源版本、精确方法签名或依赖关系不匹配时会拒绝加载。已加载的 Bundle generation 保留到游戏退出，切换多个不同 Mod 可能增加内存占用。APK 缺失 CHK 补全分支在手机上的完整覆盖尚未验证。转换工具与桌面来源选择工具主要针对 Windows。

## 许可证

本项目按 **AGPL-3.0** 发布，见 [LICENSE](LICENSE)。使用了 EFF、Better-Endfield、Dobby 和 libxposed；来源、修改和各自许可见 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)。Mod 素材的授权独立于本项目代码授权。
