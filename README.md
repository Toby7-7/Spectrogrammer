# 频谱仪 / Spectrogrammer

一个以 Android 的实时音频频谱分析器。由于 Spectroid 软件不开源且不足以满足部分需求，因此基于上游 `Spectrogrammer` 的原样镜像，围绕中文界面、后台持续分析、手机操作和频谱/瀑布图查看做较大重构。

## 主要特性
- 实时频谱曲线与瀑布图
- 频率轴支持 `线性`、`普通对数`、`音乐对数`、`Mel`、`Bark`、`ERB`
- 峰值保持曲线，支持自动回落或不回落
- 峰值标记，可切换显示 `实时峰值` 或 `短时峰值`
- 手动游标线，点击或拖动显示当前位置的频率与 dB
- 双指缩放和平移，便于查看局部频段
- 中文化单页设置，适合手机竖屏触控
- Android 后台采集、保持亮屏、返回键回主界面等行为适配
- 主体逻辑以 C/C++ 实现，Android 额外带一个前台服务 Java 类

## 快速上手
1. 安装 APK 并授予录音权限。
2. 主界面顶部三个按钮分别是 `暂停/继续`、`清除峰值`、`设置`。
3. 在主图上单指点击或拖动，可移动手动游标并查看当前频率与 dB。
4. 双指左右缩放或平移，可聚焦某个频段。
5. 进入 `设置` 页面后，可用页面顶部的 `返回主界面` 按钮，或直接使用 Android 返回键返回主界面。

## 设置速览
### 音频与处理
- `音频源`：切换 Android 录音 preset，如默认、通用、语音识别、摄像机、未处理。
- `采样率`：自动或固定采样率。更高档位是否可用取决于设备和驱动。
- `FFT 大小`：频率分辨率和刷新负担的平衡，当前范围为 `128` 到 `8192`。
- `抽取级数`：先降采样再做频谱，分辨率更细，但最高可显示频率会下降。
- `窗函数`：控制频谱泄漏与主瓣宽度。
- `指数平滑因子`：曲线平滑程度，数值越大越平滑。

### 频谱与瀑布图
- `频率轴刻度`：切换 `线性`、`普通对数`、`音乐对数`、`Mel`、`Bark`、`ERB`。
- `显示瀑布图`：关闭后只保留上方频谱图。
- `瀑布图高度`：设置瀑布图占屏比例。
- `滚动速度`：瀑布图滚动速度，范围 `2 ms` 到 `250 ms`，界面从慢到快调节。
- `显示峰值保持曲线`：显示或隐藏峰值保持曲线。
- `峰值回落时长`：峰值保持回落速度，`0` 表示不回落，最大 `120 s`。
- `峰值标记`：显示 `0 / 1 / 3 / 5` 个峰值点。
- `峰值标记来源`：从实时曲线或短时峰值曲线中找峰值。

### 运行与关于
- `后台采集`：切到后台时继续录音与处理。
- `保持亮屏`：运行时阻止屏幕自动熄灭。
- `关于`：设置页底部提供 GitHub 仓库链接。

## 默认配置
- 音频源：`默认`
- 采样率：`自动（48 kHz）`
- FFT 大小：`4096`
- 平滑因子：`0.10`
- 峰值标记来源：`短时峰值`
- 峰值保持曲线：开启，默认 `4 秒` 回落
- 后台采集：开启

## 截图
<img src="fastlane/metadata/android/en-US/phoneScreenshots/IMG_20260420_133439.jpg" alt="频谱仪 Android 实机截图" width="240"/>

## 构建
### Android
在仓库根目录执行：

```bash
make init-submodules
make doctor-android
make BUILD_ANDROID=y
```

常用命令：

```bash
make push
make run
make logcat
make clean
```

说明：
- `make BUILD_ANDROID=y` 会产出 `Spectrogrammer.apk`
- APK 默认使用仓库内测试签名，仅适合本地安装与调试
- `make doctor-android` 会检查 SDK、NDK、build-tools、子模块和必要命令
- 当前 Android 目标 API 为 `29`

### Linux
```bash
make BUILD_ANDROID=n
```

## 目录说明
- `src/app`：频谱 UI、坐标轴、瀑布图、配置和 FFT 相关逻辑
- `src/audio`：音频采集与平台驱动封装
- `src/java`：Android 前台服务
- `fastlane/metadata`：应用发布元数据和截图
- `submodules/imgui`、`submodules/kissfft`：上游子模块依赖

## 已知注意事项
- 高采样率和 `未处理` 音频源是否可用，完全取决于设备和驱动实现。
- 本仓库当前优先保证 Android 手机体验，Linux 路线仍保留但不是主要优化目标。
- 如果切换到设备不支持的高采样率档位，可能无法正常启动录音，应改回更低档位。

## 许可与来源
- 本仓库是 `aguaviva/Spectrogrammer` 的 fork 和衍生版本，当前仍包含继承和改写自上游的代码，不是从零重写。
- 当前仓库不对所有继承代码声明单一统一许可证；请以文件头声明、子目录许可证文件、[LICENSE](LICENSE) 和 [NOTICE](NOTICE) 为准。
- 已明确可识别的来源包括 Android Open Source Project 原生音频样例、`cnlohr/rawdrawandroid`、`Dear ImGui`、`KISS FFT` 以及本 fork 新增代码。
- 本 fork 新增且不含既有上游代码的独立文件，按 Apache License 2.0 提供；明确列表见 [NOTICE](NOTICE)。

## 致谢
- [aguaviva/spectrogrammer](https://github.com/aguaviva/spectrogrammer)
- [cnlohr/rawdrawandroid](https://github.com/cnlohr/rawdrawandroid)
- [mborgerding/kissfft](https://github.com/mborgerding/kissfft)
- [ocornut/imgui](https://github.com/ocornut/imgui)
