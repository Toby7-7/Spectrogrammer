# Spectrogrammer 后续优化与功能规划清单

本文档记录在完成主包名统一（`org.nanoorg.spectrogrammer`）、动态 `versionCode` 注入、编译警告清零与固定签名配置之后，后续值得继续推进的工程优化与功能扩展建议，按推荐优先级与实施难度整理。

---

## 一、已完成基线（不在此列表中重复）

- 双采样率拼接 FFT（`MultirateFFT`）与 Hann 交叉淡化
- 单/双声道（Mono / L / R / Mixed / Independent / Difference）分析与基线对比（含瀑布图差分色阶）
- 阈值报警（含 Android 振动）、时间平均（`average_frames`）、Peak Marker、PNG 导出、`.spec` 快照保存与本地音频回放
- 编译零警告（清理 `LOGW` 宏重定义、冗余变量、`CaptureForegroundService` 新 API 适配）
- 前台服务专属单色通知图标（`src/res/drawable/ic_notification.xml`）与固定 Release 签名规范（`keystore.properties.example` + `build_apk.ps1` / `Makefile` 支持）

---

## 二、剩余工程优化建议

### 1. Windows 本地可运行 C++ DSP 单元测试入口
- **工作量**：小~中（约半天）
- **风险等级**：低（不影响线上 Android 产物）
- **现状与问题**：[Makefile](file:///d:/projects/Spectrogrammer/Makefile) 中的 `test` 目标（编译 `tests/run_tests.cpp`）依赖 POSIX shell 工具链（`mkdir -p`、`uname`、MinGW/Clang），在原生 Windows PowerShell 下无法直接运行。
- **建议方案**：补充一个 `run_tests.ps1`（使用仓库内已有的 NDK Clang 或本机编译器，或编译成 Windows 控制台程序），覆盖 `AppConfig`、`fft`、`colormaps`、`auformat` 等核心模块的快速回归测试。

### 2. 拆分 `src/app/Spectrogrammer.cpp` 超大单文件
- **工作量**：中（1~2 天，纯重构）
- **风险等级**：中（全局状态与锁同步回归风险）
- **现状与问题**：[Spectrogrammer.cpp](file:///d:/projects/Spectrogrammer/src/app/Spectrogrammer.cpp) 超过 4500 行，同时耦合了 DSP 状态调度、手势交互、游标计算、设置页 UI 与渲染循环。
- **建议方案**：按职责拆分为独立模块（例如 `SettingsUI.cpp`、`SpectrumPlot.cpp`、`FileSourceUI.cpp`、`AppState.h`），并整理跨线程锁边界与状态结构体。

### 3. Android 13+（API 33+）`POST_NOTIFICATIONS` 权限适配
- **工作量**：小（1~2 小时）
- **风险等级**：低
- **现状与问题**：当前 `targetSdk=29`，若后续升级 `targetSdk >= 33`，后台采集通知需要显式申请 `android.permission.POST_NOTIFICATIONS` 运行时权限，否则通知栏无法展示前台服务状态。
- **建议方案**：在 `SpectrogrammerActivity` 启动后台服务前补充 `POST_NOTIFICATIONS` 动态权限请求（拒绝时仍保持降级兼容并给出 UI 提示）。

---

## 三、值得新增的实用功能清单

### 1. 谐波光标（Harmonic Cursor）与音高/音分标注
- **工作量**：小（约半天，纯 UI 与频率映射计算）
- **风险等级**：低
- **涉及文件**：[Spectrogrammer.cpp](file:///d:/projects/Spectrogrammer/src/app/Spectrogrammer.cpp)（`draw_cursor` / `update_cursor_state`）
- **实现要点**：在现有十字游标基础上增加“谐波模式”开关，沿基频 `f` 自动叠加绘制 `2f, 3f, 4f, ...` 谐波竖线（超出 Nyquist 上限时自动裁剪），并在浮窗中换算最近十二平均律音名及音分偏差（如 `A4 +3 cents`）。

### 2. 双游标差值测量（`Δf` / `ΔdB`）
- **工作量**：小~中（半天~1 天）
- **风险等级**：低~中（主要风险为移动端触控热区与双指缩放、基线学习按钮的冲突）
- **涉及文件**：[Spectrogrammer.cpp](file:///d:/projects/Spectrogrammer/src/app/Spectrogrammer.cpp)
- **实现要点**：扩展 Cursor A / Cursor B 两条独立竖线，在顶部或浮窗显示两游标各自的频点、幅度以及频差 `Δf`（含倍频比 `f2/f1`）和电平差 `ΔdB`。

### 3. CSV 数值导出与系统分享
- **工作量**：小~中（半天~1 天）
- **风险等级**：低（通过 SAF 创建文档导出可免去旧式存储权限问题；若走 `ACTION_SEND` 分享需补 `FileProvider`）
- **涉及文件**：[Spectrogrammer.cpp](file:///d:/projects/Spectrogrammer/src/app/Spectrogrammer.cpp)、[SpectrogrammerActivity.java](file:///d:/projects/Spectrogrammer/src/java/org/nanoorg/spectrogrammer/SpectrogrammerActivity.java)、[SpectrumFile.h](file:///d:/projects/Spectrogrammer/src/app/SpectrumFile.h)
- **实现要点**：增加“导出 CSV”操作，将当前帧 / 时间平均曲线 / 基线参考曲线 / 差值曲线按 `freq_hz,live_db,ref_db,diff_db` 格式导出，便于在 Excel 或 Python 中二次制图。

### 4. A / C 计权滤波与 dB SPL 声压偏置标定
- **工作量**：中（约 1 天，需同步修改 `AppConfig` 版本迁移）
- **风险等级**：中（需确保 Spectrum、PeakMarker、Alarm、Waterfall 与 Baseline 差值的量纲全链路一致）
- **涉及文件**：[AppConfig.h](file:///d:/projects/Spectrogrammer/src/app/AppConfig.h)、[AppConfig.cpp](file:///d:/projects/Spectrogrammer/src/app/AppConfig.cpp)、[Spectrogrammer.cpp](file:///d:/projects/Spectrogrammer/src/app/Spectrogrammer.cpp)
- **实现要点**：在频域按 IEC 61672 公式叠加 A/C/Z 计权曲线，并提供用户可调的校准偏置 `spl_offset_db`（例如借助外部校准声级计对齐后近似读出 `dBA / dBC`）。

### 5. 瀑布图冻结回溯（历史时间切片联动）
- **工作量**：中（1~2 天）
- **风险等级**：中（增加每通道约 1~2 MB 环形浮点缓冲，双通道独立模式需兼顾内存占用与基线对比交互）
- **涉及文件**：[waterfall.h](file:///d:/projects/Spectrogrammer/src/app/waterfall.h)、[waterfall.cpp](file:///d:/projects/Spectrogrammer/src/app/waterfall.cpp)、[Spectrogrammer.cpp](file:///d:/projects/Spectrogrammer/src/app/Spectrogrammer.cpp)
- **现状说明**：目前瀑布图内部主要维护用于 GPU 上传的 RGB565 像素缓冲，无法从像素颜色直接反推精确 dB 值。
- **实现要点**：在 `waterfall` 模块旁路保存一份环形浮点 dB 行缓冲；当开启 `waterfall_freeze` 后触摸瀑布图任意一行，在上方实时频谱图中叠加绘制该历史时刻的频谱曲线，并高亮对应时间游标。

### 6. 实时异响抓录闭环（MIC 录制 WAV 并切入文件分析）
- **工作量**：中~大（2~3 天）
- **风险等级**：中~高（涉及实时音频流与磁盘写盘解耦）
- **涉及文件**：[audio_recorder.h](file:///d:/projects/Spectrogrammer/src/audio/audio_recorder.h)、[ChunkerProcessor.h](file:///d:/projects/Spectrogrammer/src/app/ChunkerProcessor.h)、[file_source.cpp](file:///d:/projects/Spectrogrammer/src/audio/file_source.cpp)
- **实现要点**：必须在 `ChunkerProcessor` / 应用队列消费端（绝不能在 OpenSL ES 回调内执行文件 I/O）通过独立无锁环形缓冲与后台写盘线程写入 WAV，支持一键停止并在结束时回填 WAV RIFF 数据块长度后直接切到文件分析模式循环回放。

### 7. 1/3 倍频程实时柱状图（RTA）
- **工作量**：中~大（约 2 天）
- **风险等级**：中（低频 20~100 Hz 窄带分辨率依赖双采样率拼接前端，否则低频柱体易抖动/空洞）
- **涉及文件**：[Spectrogrammer.cpp](file:///d:/projects/Spectrogrammer/src/app/Spectrogrammer.cpp)、[ScaleUI.cpp](file:///d:/projects/Spectrogrammer/src/app/ScaleUI.cpp)
- **实现要点**：按 IEC 61260 标准中心频率（1/1 或 1/3 倍频程）对 FFT bin 能量进行带内功率累加，增加柱状图渲染视图及峰值保持顶帽。
