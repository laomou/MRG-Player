# MRG Player → TrimUI Brick Pro（吹米 Brick Pro）移植调研

> 结论先行：**可行，而且"能编译"这一关已经过了**。现有源码零修改即可交叉编译出 aarch64 可执行文件，只依赖设备自带的库。真正的工作量集中在**画布重排（1600×900 → 1280×720）**、**输入从键鼠切成摇杆按键**、**ROM 来源换掉 zenity/拖拽**这三件事上，合计约 11–15 人日。
>
> 已定的三条产品决策：
> 1. **交付形态＝pak 应用**（`/SDCARD/Apps/MRG.pak`），不做替换 MainUI 的 launcher，详见 §0；
> 2. **画布尺寸＝1280×720**，桌面与设备共用一套布局，详见 §3.3；
> 3. **发布矩阵＝Linux / Windows / macOS / TrimUI 四平台**，详见 §12。
> 核心与 ROM 随包内置（离线可用），设备端不下载、不弹系统文件对话框。

调研时间：2025-11-25。仓库基线：`/home/mourui/code/MRG-Player`（含未提交的 Primitives/Layout 改动）。

---

## 0. 交付形态：pak 应用，不是 launcher

TrimUI 的系统界面是 rootfs 里的 `/usr/trimui/bin/MainUI`（**不是 pak**），`/SDCARD/Apps/*.pak` 只是它扫描出来、以子进程方式启动的第三方应用。MinUI 那种"替换界面"靠的是另一套东西：`/SDCARD/.tmp_update/<platform>.sh` 开机钩子（`minui/workspace/tg5040/install/boot.sh` 第一行注释就是 "becomes .tmp_update/tg5040.sh"）→ `exec` 自己的 `MinUI.pak/launch.sh` → 循环跑 `minui.elf` → 退出时 `exec shutdown`。

本项目选择**应用形态**，由此确定几条硬约束：

| 约束 | 理由 |
|---|---|
| 不写 `/SDCARD/.tmp_update/` | 不抢开机流程，与 MinUI/CrossMix 等自制固件共存 |
| 不 `killall` 任何系统守护进程 | `trimui_inputd`/`keymon`/`MtpDaemon`/`wpa_supplicant` 都在为系统服务，杀掉会连累主界面 |
| 不动 CPU governor / tinymix / rfkill / LED / USB 模式 | 这些是系统设置，改了用户回到主界面会发现机器"变了个样" |
| 退出码 0 即回 MainUI | 不需要 `exec shutdown`，电源管理完全交给系统 |
| 数据写 `/mnt/SDCARD/.userdata/mrg/` | 覆盖 pak 升级不会丢存档；也符合本机 `.userdata/<platform>/` 的惯例 |

代价（已接受）：主界面和 MRG 各有一套游戏入口，用户从"应用"页点进我们。将来若要做"开机即 MRG"，在 `.tmp_update/tg5040.sh` 里包一层 exec 即可，但那时必须接手关机/休眠（`exec shutdown`）、并与 MinUI 的钩子互斥——那是固件项目的工作量，不在本次范围。

---

## 1. 目标设备

| 项 | 值 |
|---|---|
| SoC | Allwinner A133P，4×Cortex-A53 @1.8GHz |
| GPU | PowerVR GE8300 @660MHz |
| 内存 / 存储 | 1GB LPDDR3 / 8GB eMMC |
| 屏幕 | 3.95" IPS **1024×768** 60Hz，4:3，324ppi，**无触屏** |
| 输入 | 16 键 + 双霍尔摇杆（L3/R3）+ Menu/Select/Start/Home + F1/F2 |
| 系统 | Trim-UI Linux（Linux 4.9，glibc 2.25 左右） |

**没有触屏**这一点对本项目是好消息：现有 UI 本来就是键盘焦点式导航（方向键 + Enter/Esc），不需要触控重写；但所有"鼠标 hover / 点击 / 拖拽文件"的交互都必须去掉。

---

## 2. 能编译吗？—— 能，已实证

### 2.1 工具链配方（可直接照抄）

官方 SDK 提供的 Linaro 7.5.0 工具链**编不了本项目**：GCC 7 没有 `<filesystem>`，`Application.cpp` 等 19 个文件直接报"没有那个文件或目录"。已验证可用的组合是：

1. **应用本体**：Arm GNU Toolchain 8.3-2019.02（`gcc-arm-8.3-2019.02-x86_64-aarch64-linux-gnu`）+ SDK sysroot 合并
2. **模拟器核心**：官方 Linaro 7.5.0（核心是 C/C++ 老代码，不吃 `<filesystem>`，且产物 glibc 要求更低）

sysroot 合并必须带 `--ignore-existing`，否则 SDK 里的旧 `libstdc++` 会覆盖工具链自带的那份，链接行为不可预测：

```bash
TC=/path/to/gcc-arm-8.3-2019.02-x86_64-aarch64-linux-gnu
rsync -a --ignore-existing SDK_usr/ $TC/aarch64-linux-gnu/libc/usr/
```

### 2.2 实测结果

编译 32 个源文件 + 链接全部通过，唯一的坑是 `src/util/Image.cpp:19` 的 `#include "stb_image.h"` 需要额外 `-I third_party/stb`，而 CMakeLists.txt:132 里本来就有这条 include，属于命令行手工编译的假坑。

| 指标 | 结果 |
|---|---|
| 产物 | `mrg-player`（aarch64），strip 后 **1.45 MB** |
| NEEDED | libSDL2-2.0.so.0、libSDL2_ttf-2.0.so.0、libz.so.1、libdl、libpthread、libm、libc |
| 最高 GLIBC | **GLIBC_2.17**（设备 2.25+，安全） |
| C++ 运行库 | 用 `-static-libstdc++ -static-libgcc` 后**不再依赖** libstdc++.so.6 |
| interp | /lib/ld-linux-aarch64.so.1 |

推荐链接参数：`-lstdc++fs -static-libstdc++ -static-libgcc`（`std::filesystem` 在 GCC 8 需要 `libstdc++fs`；静态化后摆脱 GLIBCXX 版本依赖，代价是 +11MB 未 strip、strip 后影响很小）。

### 2.3 需要新增的构建配置

- 一个 CMake toolchain file（`cmake/toolchain-aarch64-brick.cmake`）
- 一个平台选项 `MRG_PLATFORM=brick`，控制：画布尺寸、禁用 zenity 文件选择器、禁用下载、禁用拖拽、设备默认数据目录
- 现有 CMakeLists.txt:36-50 强制静态编译自带 SDL2 子模块的逻辑，在设备构建下要改成"用 sysroot 里的 SDL2"

具体写法（option 表、toolchain 全文、imported target、设备端编译链接选项、`pak` 打包 target、产物自检、落地顺序）已整理在 **§13**。

---

## 3. 显示层：本次移植最大的坑

### 3.1 设备上的 SDL2 是定制分支

上游 SDL2 + KMSDRM **在设备上完全走不通**——SDK 的 `arm64.config.sh` 明确 `--disable-video-kmsdrm --disable-video-x11 --disable-video-offscreen --disable-video-dummy`，只保留 `--enable-video-mali`，视频驱动是 `src/video/mali-fbdev/`：

- `MALI_VideoInit` 直接 `open("/dev/fb0")` + `FBIOGET_VSCREENINFO`，把 xres/yres 当唯一显示模式，像素格式写死 RGBX8888，刷新率写死 60；
- 定义了 `SDL2_POWERVR_GE8300` 宏；
- **`SDL_malivideo.c:201-202` 里 `window->w = 1280; window->h = 720;` 是无视调用者的硬编码**（这段代码原本是为 1280×720 的 Smart Pro 写的）；
- 配置脚本里关掉了 kmsdrm/x11/offscreen/dummy，**但随包发布的 `libSDL2-2.0.so.0.2600.1` 里仍能搜到 offscreen 驱动的字符串**（`SDL offscreen video driver`、`SDL_VIDEO_OFFSCREEN_SAVE_FRAMES`）。所以"设备上完全没法无头截图"这个结论不成立，真机先用 `SDL_VIDEODRIVER=offscreen` 试一次再下判断（见 §12.2）。

### 3.2 由此产生的风险（必须真机确认）

应用现在按下面的顺序决定 UI 缩放（`src/app/RenderSurface.cpp` 的 `resolveOutputSize`）：

```cpp
SDL_GetRendererOutputSize(renderer, &reportedW, &reportedH);  // 可能被 mali 谎报
SDL_GetWindowSize(window, &windowW, &windowH);
OutputSize s = resolveOutputSize(reportedW, reportedH, windowW, windowH,
                                 std::getenv("MRG_OUTPUT_SIZE"));
ui::setOutputSize(s.width, s.height);
ui::setWindowSize(windowW, windowH);
```

优先级是 **`MRG_OUTPUT_SIZE` → renderer 输出尺寸 → 窗口尺寸 → 不动**，所以驱动谎报时只要在设备上 `export MRG_OUTPUT_SIZE=1024x768` 就能纠正，不必等新固件；格式只接受 `宽x高`（也认 `X`/`*`，上限 8192），拼错的值会被忽略而不是把界面搞成空白。

GLES2 后端的 `GetOutputSize` 走 `SDL_GL_GetDrawableSize`（`src/render/opengles2/SDL_render_gles2.c:322`），在 mali 驱动里很可能返回被硬编码的 **1280×720**，与真实面板 1024×768 不符。若如此，UI 会按 1.25 倍缩放渲染 → 右侧/底部被裁掉。

**探针已经内置**：`mrg-player --renderer-probe` 打印 video driver、全部 render driver、当前后端名/flags/纹理格式、renderer 输出尺寸、GL drawable/swap interval/glGetString、窗口与窗口像素尺寸、桌面模式、相关 hint、`MRG_OUTPUT_SIZE`，以及 Linux 下 `/dev/fb0` 的 `FBIOGET_VSCREENINFO`（xres/yres/bpp/色深）。这条路径不受 `MRG_ENABLE_SMOKE_TESTS` 限制，release 包里也能用。

**帧时间**：`MRG_FRAME_STATS=1` 时每 300 帧向 stderr 打印一次 `frame stats: … ms/frame (… fps), present … ms`，用来判断上机后是 vsync 在锁帧还是一次 present 就吃掉 16ms。

**设备无头截图（待真机确认）**：如果 offscreen 驱动确实可用，命令是

```bash
SDL_VIDEODRIVER=offscreen SDL_VIDEO_OFFSCREEN_SAVE_FRAMES=1 \
  ./mrg-player --smoke-test-screenshot=/mnt/SDCARD/Roms/xxx.nes
```

帧图由 SDL 写到当前工作目录（文件名形如 `SDL_offscreen_frame_000000.bmp`）；再配 `--renderer-probe` 就能把"渲染成什么样"和"尺寸报的是什么"一次拿全。

**兜底方案（推荐直接采用，不依赖探针结果）**：设备构建的画布固定成 **1280×720**（与桌面同一套布局，见 §3.3），**不信**任何 window/drawable 尺寸去反推缩放比例，最后统一 `SDL_RenderCopy(renderer, canvasTexture, NULL, NULL)` 贴满整屏（这是 MinUI 和 32blit 移植包验证过的做法）。

### 3.3 画布尺寸：全平台统一 1280×720（已定）

**决策：画布从 1600×900 改为 1280×720，四平台共用同一套坐标。** 这样布局只维护一份，桌面窗口按 `窗口尺寸/1280×720` 缩放，设备上按下面的 P0 结果选择呈现方式。

| 方案 | 效果 | 结论 |
|---|---|---|
| 沿用 1600×900 + letterbox | 缩到 1024×576，上下各 96px 黑边，字号从 14/20/28/36/44 变成 **9/13/18/23/28 px**，小字不可读（预览图 `/tmp/mrg-brick-letterbox.png` 已证实） | ❌ 否决 |
| 1024×768 画布 | canvas == panel，1:1 无黑边，字最清楚 | ❌ 否决（与"四平台一套布局"冲突，桌面要维护两套坐标） |
| **1280×720 画布** | 16:9、四平台统一；1280×720 正好是设备 SDL2 谎报的窗口尺寸（`SDL_malivideo.c:201-202`） | ✅ **采用** |

设备端（4:3 的 1024×768 面板）如何呈现这张 16:9 画布，取决于 P0 探针，三种情况及对应做法：

| P0 观测 | 做法 | 面板上看到的 |
|---|---|---|
| `drawable` 报 **1024×768** | 画布 1280×720 等比缩到 1024×576 居中 | 上下各 96px 黑边（= 现在的 letterbox 效果，但布局只维护一份） |
| `drawable` 报 **1280×720**，驱动自己铺满面板 | `SDL_RenderSetLogicalSize(1280,720)`，接受后端非等比缩放 | 满屏，圆形会被压成椭圆（x×0.8 / y×1.067） |
| `drawable` 报 **1280×720** 且驱动等比缩放 | 同上 | 满宽 1024×576 + 上下黑边 |

**安全区规则**：所有必须可读的内容（标题、列表、按钮、对话框）约束在画布中央 **1120×630** 以内（即 1280×720 四周各留 80px），这样无论最终是黑边、还是 8%/6.7% 的非等比拉伸，都不会把字切掉或贴边。

字号不受影响：`src/ui/Primitives.cpp:161` 的 `kFontSizes{0,14,20,28,36,44}` 是画布单位，1280×720 下最小 14px，比 1600×900 缩到面板的 9px 可读得多。

代价是要重排各屏的绝对坐标（它们现在按 1600×900 写死）。需要动的点（已定位）：

- `src/ui/Primitives.hpp:13-14`（kLogicalWidth/kLogicalHeight）
- `src/runtime/VideoViewport.hpp:12-13`（kCanvasWidth/kCanvasHeight）
- `src/app/Application.cpp:287-300`（建窗 + renderer）
- `src/ui/PauseMenuScreen.cpp:17`（面板 {976,64,552,772}）、`src/ui/SettingsScreen.cpp:94`（kCoreColumnCoreX=900）、`src/ui/HomeScreen.cpp:217`（kQuitDialogX）
- 新增的 `src/ui/Layout.hpp`（Rect/LinearLayout/equalColumns/panelLayout）正好可以承接这次重排，把绝对坐标换成相对布局

---

## 4. 输入层

### 4.1 设备事实

MinUI 的 tg5040 平台层是设备输入的唯一权威参考（`workspace/tg5040/platform/platform.c`）：只用 SDL 摇杆，`SDL_JoystickOpen(0)`，键盘 `CODE_*` 全是空（只有 POWER 键 102/116）。

| 功能 | Joystick button | 功能 | 轴 |
|---|---|---|---|
| B | 0 | 左摇杆 X/Y | 0 / 1 |
| A | 1 | L2（ABSZ） | 2 |
| Y | 2 | 右摇杆 X/Y | 3 / 4 |
| X | 3 | R2（RABSZ） | 5 |
| L1 / R1 | 4 / 5 | 量程 | ±30k |
| Select / Start | 6 / 7 | | |
| Menu | 8 | | |
| L3 / R3 | 9 / 10 | | |
| MINUS / PLUS | 13 / 14 | | |

### 4.2 现状与缺口

- 键盘事件散落在 `src/app/Application.cpp`（约 80 处 `SDLK_`，1527–1990 行）——设备上没有键盘，全部走不到；
- 手柄路径**已经存在**：`src/config/InputBindings.hpp:72-75` 有 `controller`/`controllerPlayer2` 表，还有 `ControllerStyle` 与按键提示图标；
- 但打开设备用的是 `SDL_GameControllerOpen`（Application.cpp:122/346/591/1474），**要求 SDL 里有该手柄的 mapping**。TrimUI 的手柄不在 SDL 内置 mapping 表里，MinUI 也刻意用 Joystick API 而非 GameController API——**这强烈暗示设备上 `SDL_GameControllerOpen` 会失败**。

### 4.3 处理办法

1. 启动时 `SDL_GameControllerAddMapping()` 注入 Brick 的映射字符串（GUID 需真机读取：`SDL_JoystickGetDeviceGUID` 或 `/proc/bus/input/devices`）；
2. 或者给 `Application.cpp` 的控制器发现逻辑加上 Joystick 回退路径（不依赖 mapping，直接按 button index 读）；
3. 保留键盘路径给桌面构建（两套 input profile，编译期切换）；
4. 去掉 `SDL_MOUSEMOTION` hover 逻辑与 `SDL_DROPFILE`（设备上永不触发）；
5. Menu 键接暂停菜单、Home 键接退出确认（现在退出走的是界面里的按钮）。

---

## 5. 数据、ROM 与核心目录

- 数据目录：桌面用 `SDL_GetPrefPath`（`src/config/AppPaths.cpp` 的 `preferenceRoot`），设备上 `launch.sh` 设 `MRG_DATA_DIR=/mnt/SDCARD/.userdata/mrg`，`dataRoot()` 优先读它（已实现）。放 SD 卡而不是 pak 内：覆盖 pak 升级不丢存档；卸载时用户可留档。
- 字体：`assemble-pak.cmake` 把 `assets/` 拷成 `res/`，所以字体在 `res/fonts/`；启动器依次尝试 `assets/fonts/`、`res/fonts/`、`../share/mrg-player/fonts/`（已实现），`DejaVuSans.ttf` 必须与中文全量字体同目录。
- ROM 导入：桌面是 `zenity`（`src/platform/RomFilePicker.cpp`）+ 窗口拖拽，设备两者皆无。**已实现**的做法：`MRG_ROM_DIRS`（`launch.sh` 设成 `$progdir/roms:$SDCARD/Roms`）指定扫描目录；首次启动库为空时自动扫描导入（`src/library/RomScanner.cpp`），此后 Home 的"导入游戏"在文件对话框不可用时改为重扫这些目录并 toast 结果。目录浏览器页面仍是可选增强，不是前提。
- 核心目录：`src/runtime/libretro/CoreDiscovery.cpp:70` 已支持 `MRG_CORES_DIR` 覆盖 → pak 内 `cores/` 直接生效，无需改核心发现逻辑。
- 在线下载核心（`CoreInstaller.cpp:27-35`，默认 base URL 按平台切换）：设备上**必须关掉**——设备 rootfs 没有 libcurl（sysroot 里 libcurl/openssl 都没有），而且 buildbot 的 aarch64 包 ABI 也不兼容（见 §6.1）。

---

## 6. 模拟器核心评估

### 6.1 官方 buildbot 包全部不可用

下载 buildbot `nightly/linux/aarch64/latest` 实测：

| 核心 | 体积 | 最高 GLIBC |
|---|---|---|
| fceumm | 4.3 MB | 2.33 |
| gambatte | 4.6 MB | 2.33 |
| snes9x | 2.7 MB | 2.33 |
| genesis_plus_gx | 12 MB | 2.33 |
| mgba | 3.2 MB | 2.35 |

高出设备 glibc（约 2.25）一大截，符号集中在 `stat/fstat/fstatat@GLIBC_2.33`、`pow/exp@GLIBC_2.29`——**不是源码依赖，纯粹是"用新 glibc 编译"造成的**。结论：核心必须自建（或直接用 MinUI 生态里已编好的）。

### 6.2 自建实测：Arm GNU 8.3 + SDK sysroot（已落地为 tools/build-cores.sh）

四个核心全部用和 launcher 同一套工具链交叉编译，全部 **GLIBC 2.17**、无 GLIBCXX 依赖：

| 系统 | 核心 | 构建方式 | strip 后体积 | NEEDED | 最高符号 |
|---|---|---|---|---|---|
| NES | fceumm | make platform=unix | 4.48 MB | libm / libc | GLIBC 2.17 |
| GB / GBC / GBA | mgba | cmake -DBUILD_LIBRETRO=ON | 3.29 MB | libm / libpthread / libc | GLIBC 2.17 |
| SNES | snes9x | make -C libretro platform=unix | 3.74 MB | libm / libc | GLIBC 2.17（libstdc++ 静态） |
| Genesis | genesis_plus_gx | make -f Makefile.libretro platform=unix | 12.69 MB | libm / libc | GLIBC 2.17 |

合计 **24.3 MB**，压缩后约 8–10 MB，是 pak 里最大的一块。三个踩过的坑：

1. **exp2f@GLIBC_2.27**：mgba 第一次编完最高符号是 2.27——链接器把数学函数解析到了工具链自带 glibc 2.28 的 libm，而不是 sysroot 里 2.23 那份。对策和 launcher 一样：把 `SYSROOT/usr/lib/libm.so.6` 显式加进链接命令行（LDFLAGS / CMAKE_SHARED_LINKER_FLAGS），立刻回到 2.17。
2. **snes9x 的 Makefile 不在仓库根目录**，在 libretro/ 子目录；根目录 make 直接「没有指明目标并且找不到 makefile」。
3. **C++ 核心要 -static-libstdc++**：设备 rootfs 里的 libstdc++ 版本未知，静态链进去以后 GLIBCXX max 变成 0，verify-pak 也就不用再放宽上限。

> gambatte 不再随包：它是 GPLv2，而 mGBA（MPL-2.0）本身就能跑 GB/GBC。代码里已把 mgba_libretro 加进 `src/runtime/libretro/CoreDiscovery.cpp:30-33` 的 kGameBoyCores 兜底末尾，以及 `src/runtime/libretro/CoreInstaller.cpp:53-57` 的候选表，所以只有 mGBA 的包里 .gb / .gbc 照样能启动。

### 6.2.1 tools/build-cores.sh

```sh
tools/build-cores.sh \
  --prefix /tmp/arm/aarch64-linux-gnu- \
  --sysroot /tmp/arm/aarch64-linux-gnu/libc \
  --out pack/cores        # 只编其中几个：末尾再追加 fceumm mgba
```

- 上游 commit **钉死**在脚本里（FCEUMM_REV / MGBA_REV / SNES9X_REV / GENPLUS_REV），按 SHA 做 git fetch --depth 1 + checkout FETCH_HEAD；源码缓存在 --src（默认 $TMPDIR/mrg-cores-src），SHA 没变就跳过拉取。
- 产物直接落到 pack/cores/，pack/cores/*.so 已进 .gitignore；cmake --build --target pak 会把它拷进 MRG.pak/cores/。
- tools/verify-pak.sh 现在对**每个核心**也做 NEEDED / GLIBC 上限检查，不再只看 launcher。上面那个 exp2f@GLIBC_2.27 就是第一次跑这条检查暴露出来的。
- CI 的 _brick.yml 里加了两步：actions/cache 缓存核心源码目录（key 取 tools/build-cores.sh 的 hash），然后调用本脚本；四个核心在 runner 上编完约 1 分钟（命中缓存后）。

### 6.3 性能

fceumm 在本机 x86_64 上实测 **0.163 ms/帧（≈6100 fps）**，NES 这种负载按 A53 大约慢 6–8 倍估算也就是 1–1.3 ms/帧，占用 16.7ms 预算的 6–8%，完全无压力。GBA/SNES 是重负载，但 MinUI（同一块 A133P）能跑 mgba/snes9x 到 60fps，属于"生态已验证"的区间——注意 MinUI 用的是 2000MHz cpufreq + 全屏 GL 直贴，这两点我们都要照做。

**真机验收时必须加帧时间 HUD**（core run + present 的耗时），因为 SDL 的 vsync 在 mali 驱动上行为未知。

### 6.4 许可（发布前必须确认）

- fceumm：GPLv2；mgba：MPL-2.0（同时覆盖 GB/GBC，故 gambatte 不再随包）；gambatte：GPLv2（只有用户自带核心时才会出现）
- **snes9x、genesis_plus_gx：非商业许可**（可自由分发源码/二进制用于非商业用途）
- 结论：自用/免费分发没问题；若要商业化，SNES/Genesis 需要换核心（snes9x2005 同样非商业；Genesis 无干净替代）或走"用户自行下载核心"的模式。

---

## 7. 打包

设备上的移植包结构（来自 32blit_trimui 的实际 release）：

```
/SDCARD/Apps/MRG.pak/
├── config.json        {"package":"com.mrg.player","label":"MRG Player","icon":"icon.png","launch":"launch.sh","description":"..."}
├── icon.png
├── launch.sh
├── mrg-player          # 主程序（~1.5 MB，NEEDED 只有系统库）
├── cores/              # 自建核心，配 MRG_CORES_DIR
├── roms/               # 内置 ROM（随包分发，见 §6.4 许可）
└── res/                # 字体/图标等
```

整个目录拷进 `/SDCARD/Apps/` 即可，**不要多套一层目录**（MainUI 只认 `Apps/*.pak/config.json`）。

`launch.sh` 参考（不要动系统全局设置，MinUI 那些 tinymix/cpufreq 操作交给系统）：

```sh
#!/bin/sh
# MRG Player - TrimUI Brick Pro
progdir=$(cd "$(dirname "$0")" && pwd)
SDCARD=${SDCARD_PATH:-/mnt/SDCARD}

# 系统库已含 SDL2/SDL2_ttf，无需自带；仅当 pak 内另附 .so 时才需要这行
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$progdir

# pak 内资源（核心、内置 ROM、字体）
export MRG_CORES_DIR=$progdir/cores

# 用户数据：存档/缩略图/配置，与 pak 分离，升级不丢
export MRG_DATA_DIR=$SDCARD/.userdata/mrg
mkdir -p "$MRG_DATA_DIR"

# 游戏来源：pak 自带的 roms/ 加上卡上的 Roms/，首次启动自动导入
export MRG_ROM_DIRS=$progdir/roms:$SDCARD/Roms

cd $progdir
exec ./mrg-player "$@"
```

要点：`progdir` 必须动态求值（32blit 那个包硬编码 `/mnt/SDCARD/Ports/match3`，换机器/换路径就废）；`exec` 让退出码直接回传 MainUI；不碰 governor/tinymix/rfkill/LED（见 §0）。

---

## 8. 风险清单

| 风险 | 影响 | 对策 |
|---|---|---|
| mali SDL2 硬编码 1280×720 | UI 缩放算错、画面裁切 | P0 探针确认；画布固定 1280×720（§3.3），不信 window/drawable 反推缩放，最终 `RenderCopy(NULL)` 贴满，内容限制在中央 1120×630 安全区 |
| `SDL_GameControllerOpen` 无 mapping | 设备上完全无输入 | 注入 mapping 字符串 / 加 Joystick 回退 |
| 真机 libstdc++ 版本未知 | C++ 核心加载失败 | 核心静态链 libstdc++ 或 pak 自带 |
| 无 zenity / 无拖拽 / 无 libcurl | 无法导入 ROM、无法装核心 | 扫描 `MRG_ROM_DIRS`（已实现）；核心随包分发 |
| offscreen 驱动是否真的可用 | 设备上能否跑 smoke test 截图 | 固件 SDL2 里能搜到 offscreen 字符串（§3.1），真机先 `SDL_VIDEODRIVER=offscreen` 试；不可用才退回真机拍照 |
| ALSA 音频延迟 | 音画不同步/爆音 | SDL_QueueAudio 缓冲调参；MinUI 有 tinymix 预设可参考 |
| vsync 行为不明 | 撕裂或锁不到 60 | 帧时间 HUD + 可选 software frame limiter |

---

## 9. 分阶段计划

| 阶段 | 内容 | 产出 | 估时 |
|---|---|---|---|
| P0 | 真机探针：显示尺寸/fb0/摇杆 GUID/音频设备/性能 | 一页实测数据 | 0.5–1 d |
| P1 | 构建系统：toolchain file、`MRG_PLATFORM=brick`、关掉 zenity/下载/smoke test | 能装进 pak 跑起来（黑屏/错位也算成功） | 1 d |
| P2 | 平台层：数据目录兜底、全屏、`MRG_CORES_DIR`、退出/信号、核心随包 | 能启动核心、出画面出声 | 1 d |
| P3 | 输入：mapping 注入 / joystick 回退、去掉鼠标与拖拽、Menu/Home 键、按键提示 | 手柄全流程可操作 | 1.5–2 d |
| P4 | 1280×720 重排（全平台共用）：四个屏 + 各面板 + 设置页 + 暂停菜单 + 对话框，绝对坐标改走 `src/ui/Layout.hpp` | 界面可用、字号可读 | 3–4 d |
| P5 | ROM 导入：`MRG_ROM_DIRS` 扫描 + 首次启动自动导入（已完成）；目录浏览器页面为可选增强 | 把 ROM 拷进卡上 `Roms/` 即可加游戏 | 0.5 d（已完成） |
| P6 | 性能/音频调优 + 真机验收 + 打包发布（含 §12 四平台产物） | 可分发 .pak / 四平台包 | 1–2 d |
| | | **合计（含 P5）** | **11–15 d** |
| | | **合计（首发砍 P5）** | **9–12 d** |

P0/P1 可以并行（P1 不依赖真机，交叉编译已经跑通）。

---

## 10. 附录：P0 真机探针（建议第一个跑）

显示部分现在由应用自带的 `mrg-player --renderer-probe` 覆盖（§3.2），所以剩下的自定义探针只需要管**输入与音频**（摇杆 GUID、mapping、音频设备）。

交叉编译一个小程序，输出到屏幕 + stdout，验证四件事：

```cpp
// probe.cpp —— 用同一套 Arm 8.3 + SDK sysroot 编译
#include <SDL.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cstdio>

int main() {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO);
  SDL_Window *w = SDL_CreateWindow("probe", 0, 0, 1024, 768, SDL_WINDOW_SHOWN);
  SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
  int ww, wh, dw, dh, ow, oh;
  SDL_GetWindowSize(w, &ww, &wh);
  SDL_GL_GetDrawableSize(w, &dw, &dh);
  SDL_GetRendererOutputSize(r, &ow, &oh);
  SDL_DisplayMode dm; SDL_GetDesktopDisplayMode(0, &dm);
  printf("window=%dx%d drawable=%dx%d output=%dx%d desktop=%dx%d@%d\n",
         ww, wh, dw, dh, ow, oh, dm.w, dm.h, dm.refresh_rate);

  int fd = open("/dev/fb0", O_RDONLY);
  fb_var_screeninfo vi{};
  if (fd >= 0 && ioctl(fd, FBIOGET_VSCREENINFO, &vi) == 0)
    printf("fb0=%ux%u bpp=%u\n", vi.xres, vi.yres, vi.bits_per_pixel);

  printf("joysticks=%d\n", SDL_NumJoysticks());
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    SDL_Joystick *j = SDL_JoystickOpen(i);
    printf("[%d] name=%s buttons=%d axes=%d\n", i, SDL_JoystickName(j),
           SDL_JoystickNumButtons(j), SDL_JoystickNumAxes(j));
    char guid[64]; SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
    printf("    guid=%s  gamecontroller=%s\n", guid,
           SDL_IsGameController(i) ? "yes" : "NO");
  }
  printf("audio_devices=%d\n", SDL_GetNumAudioDevices(0));
  SDL_Quit();
}
```

要重点看的两行：
- `window/drawable/output/desktop` 四个尺寸**是否一致**；
- `gamecontroller=` 是 `yes` 还是 `NO`（后者说明必须自己做 mapping）。

复现本文所有构建的命令（本机 /tmp/tui 下已验证）：

```bash
# 核心（以 fceumm 为例）
make -f Makefile.libretro platform=unix \
  CC=<linaro7.5>/bin/aarch64-linux-gnu-gcc \
  CXX=<linaro7.5>/bin/aarch64-linux-gnu-g++ \
  AR=<linaro7.5>/bin/aarch64-linux-gnu-ar -j8

# 应用
<arm83>/bin/aarch64-linux-gnu-g++ -std=c++17 -O2 -DMRG_HAVE_ZLIB=1 \
  -I src -I third_party/libretro -I third_party/stb \
  -I <sysroot>/include/SDL2 -I <sysroot>/include \
  <所有 .cpp> -o mrg-player \
  -L<sysroot>/lib -lSDL2 -lSDL2_ttf -lz -ldl -lpthread \
  -lstdc++fs -static-libstdc++ -static-libgcc
```

---

## 11. 安装与升级（给最终用户的操作）

### 11.1 安装（无安装器，纯拷贝）

1. 用读卡器把 SD 卡挂到电脑上（或设备进 USB 存储模式）；
2. 把整个 `MRG.pak` 目录拷到 `/SDCARD/Apps/`，路径必须正好是 `/SDCARD/Apps/MRG.pak/config.json`——**不要多套一层 `MRG/MRG.pak`**；
3. 弹出 SD 卡、插回设备、开机，在"应用"页出现 **MRG Player** 图标；
4. 点开即运行。首次启动会创建 `/mnt/SDCARD/.userdata/mrg/`（配置、存档、截图都在这）。

没有权限位问题：SD 卡是 FAT32/exFAT，所有文件天然可执行，`chmod +x` 只对 Linux 原生分区有意义（做开发机 rsync 时才需要）。

### 11.2 升级

- 关掉 MRG → 用新的 `MRG.pak` **整个覆盖**旧的（设置/存档在 `.userdata/mrg/`，不受影响）→ 开机运行。
- 版本号写在 `config.json` 的 `description` 与程序内的 about 页；建议同时在 pak 根放 `VERSION` 文件，方便用户肉眼确认。

### 11.3 卸载

- 删 `/SDCARD/Apps/MRG.pak/` 即卸载；
- 想连存档一起清就再删 `/mnt/SDCARD/.userdata/mrg/`；只删 pak 则重新装回来仍能续档。

### 11.4 开发期迭代（比拔卡快）

设备开 WiFi + SSH（TrimUI/CrossMix 自带 `dropbear`/`telnetd`，视固件而定），然后：

```bash
# 主机上（设备 IP 假设 192.168.1.50）
rsync -av --delete build-brick/pak/ root@192.168.1.50:/mnt/SDCARD/Apps/MRG.pak/
# 设备上
/mnt/SDCARD/Apps/MRG.pak/launch.sh
```

日志走 `stderr`，直接看 SSH 终端输出；建议加 `--log` 参数把 `SDL_Log` 也写到 `.userdata/mrg/log.txt`，真机崩溃后能取回。

### 11.5 发布前先让用户体验什么

首发 pak 里应当**零配置可用**：内置核心 + 内置 ROM（§6），开机点开就能玩；不放"请放入 ROM"之类的空状态引导。用户自己加 ROM 是 P5 的目录浏览器，属于增强而非前提。

---

## 12. 多平台发布矩阵（Linux / Windows / macOS / TrimUI）

一套源码、一份 1280×720 布局、四种产物。平台差异全部收敛在 `MRG_PLATFORM` 编译期开关 + 运行时路径选择上，不写第二套 UI。

### 12.1 产物与依赖

| 平台 | 构建方式 | 图形/音频依赖 | 交付格式 | 用户数据目录 |
|---|---|---|---|---|
| **Linux x86_64** | CMake + 自带 SDL2 静态子模块（现状） | 无外部依赖（静态 SDL2/SDL2_ttf，ALSA/Pulse 运行时二选一） | `mrg-player-<ver>-linux-x86_64.tar.gz`，内含 `bin/` + `share/mrg-player/`；进阶再出 `.AppImage` | `$XDG_DATA_HOME/MRG/MRGPlayer`（`SDL_GetPrefPath`，现状） |
| **Windows x64** | CMake + MSVC 2019/2022（或 MinGW-w64 交叉） | 静态 SDL2；`winhttp` 走系统 | `mrg-player-<ver>-windows-x64.zip`（可执行 + DLL + 资源目录） | `%APPDATA%\MRG\MRGPlayer`（`SDL_GetPrefPath`，现状） |
| **macOS** | CMake + Xcode 工具链，universal2（arm64 + x86_64） | 静态 SDL2；CoreAudio/Metal 走系统 | `mrg-player-<ver>-macos-arm64.zip`（`ditto` 保留 bundle 元数据）；要分发给外部用户再加 ad-hoc 签名 + 公证，否则 Gatekeeper 拦 | `~/Library/Application Support/MRG/MRGPlayer`（`SDL_GetPrefPath`，现状） |
| **TrimUI Brick Pro** | Arm GNU 8.3 交叉 + SDK sysroot（§2.1），`-DMRG_PLATFORM=brick` | **系统自带** SDL2 2.0.26 / SDL2_ttf 2.0.10；静态 libstdc++/libgcc | `mrg-player-<ver>-trimui-brick.zip`（内含 `MRG.pak/`，§11） | `/mnt/SDCARD/.userdata/mrg`（`MRG_DATA_DIR`） |

共同点：核心（`.so`）与 ROM 都跟着产物走，`MRG_CORES_DIR` 指向包内 `cores/`，四平台用同一套核心发现逻辑（`CoreDiscovery.cpp:70` 已支持该变量）。

### 12.2 功能开关矩阵

`MRG_PLATFORM` 取 `desktop`（默认）或 `brick`，控制下列开关：

| 能力 | desktop | brick | 说明 |
|---|---|---|---|
| 画布 | 1280×720，窗口可拉伸 | 1280×720，全屏 | 同一套坐标，`gScale = 窗口/1280` |
| 输入 | 键盘 + 鼠标 + 手柄 | 仅摇杆按键 | brick 关掉 `SDLK_*`/`SDL_MOUSEMOTION`/`SDL_DROPFILE` 分支 |
| ROM 导入 | zenity 文件对话框 + 拖拽 | 内置 ROM + 目录浏览器（P5） | brick 无 zenity、无拖拽 |
| 在线下载核心 | 开（`MRG_ENABLE_CORE_DOWNLOAD=ON`） | 关（同名 option 被强制为 OFF，代码编译期切除） | brick 无 curl，buildbot 的 aarch64 包 glibc 又太新（§6.1）；四个核心已随 pak 内置 |
| smoke test 截图 | 开（`MRG_ENABLE_SMOKE_TESTS`） | 关 | 设备 SDL2 没编 dummy，offscreen 待真机确认（§3.1） |
| 窗口/全屏切换 | 开 | 恒全屏 | |
| 退出方式 | 关窗口 / 菜单 | Menu 键暂停 + 退出确认 | brick 没有窗口关闭按钮 |

### 12.3 CI 矩阵（已落地）

两个入口工作流：`ci.yml`（push / PR 到 `main`，只验证不发布）与 `release.yml`（`v*` tag 发布，`workflow_dispatch` 手动跑只构建不上传）。设备构建本身放在可复用工作流 `.github/workflows/_brick.yml`（`on: workflow_call`）里，两处都调用它：**ci 上每次 push / PR 都会交叉编译一次并跑 `pak-verify`**，release 上额外把 zip 传成 artifact。一份配方，CI 检的就是要发的那份。

| job | 所在工作流 | runner | 命令要点 | 产物 |
|---|---|---|---|---|
| `linux` | ci / release | `ubuntu-24.04` | Release + Ninja，`ctest` + 6 个 smoke | `mrg-player-<ver>-linux-x86_64.tar.gz` |
| `platform` | ci | `windows-latest` / `macos-latest` | MSVC x64 / Apple Clang，`ctest` + 6 个 smoke | 仅验证 |
| `windows` | release | `windows-latest` | vcpkg 静态 zlib + `cmake --install` | `mrg-player-<ver>-windows-x64.zip` |
| `macos` | release | `macos-latest` | `ditto` 打包 `.app` | `mrg-player-<ver>-macos-arm64.zip` |
| `brick` | ci / release | `ubuntu-24.04` | Arm GNU 8.3 + SDK sysroot 交叉，`--target pak pak-verify` | ci 仅验证；release 出 `mrg-player-<ver>-trimui-brick.zip` |
| `release` | release | `ubuntu-24.04` | 汇总 artifact + `sha256sum` | Release 附件 + `SHA256SUMS.txt` |

#### 为什么抽成可复用工作流

一开始 brick 整个 job 内联在 `release.yml` 里，结果是**只有打 tag 或手动触发 Release 才编译设备包**，PR 上看不到——`.github/workflows/ci.yml` 的 3 个 job 全是桌面，设备链接一旦回归（比如又链上 `powf@GLIBC_2.27`），要等到发版那天才炸。抽成 `_brick.yml` 后：

- `ci.yml` 加一个 `brick: { name: TrimUI Brick (cross), uses: ./.github/workflows/_brick.yml }`，PR 与 push 到 `main` 都跑，不传 artifact（省额度，也避免把 2.9 MB 包挂在每个 PR 上）。
- `release.yml` 里同一个 job 只写 `uses: ... with: { upload: true }`，`release` job 的 `needs` 不变。
- 工作流里 `actions/upload-artifact@v4` 那一步用 `if: inputs.upload` 开关，artifact 名来自 `inputs.artifact-name`（默认 `brick-package`）。
- 版本号解析留在被调用工作流内部：`github.ref_type == 'tag'` 对两种调用方都成立，不需要由调用方传参。

实测（run [45746982080](https://github.com/laomou/MRG-Player/actions/runs/45746982080)，PR #15 的 push）：4 个 job 全绿，`TrimUI Brick (cross) / TrimUI Brick package` **2 分 26 秒**（工具链命中 cache），日志里 `-- Using system SDL2: ...`、`GLIBC max: 2.17, GLIBCXX max: 0`、`verify-pak: ok` 全部照旧。

#### brick job 的六个决定

1. **工具链不用 TrimUI 官方那套**：官方 release 里的 Linaro 7.5 是 GCC 7，没有 `<filesystem>`（§2.2），直接用 Arm GNU 8.3-2019.02 + 把 `SDK_usr_tg5040_a133p` 合并进它的 sysroot。这两个 tarball 都是公开直链（Arm developer.com 302 到 armkeil blob，`curl -fL` 能拿）。
2. **缓存只放 tarball，不放解压结果**：两个包合计 376 MB；解压 + `rsync` 合并不到一分钟，而缓存 1.3 GB 的合并树会把仓库 10 GB 额度吃掉一大块。缓存 key 写死成 `trimui-brick-arm-gnu-8.3-2019.02-sdk-20231018-v2`，换工具链版本必须一起改 key。
3. **下载先校验再解压**：两个 tarball 的 SHA-256 钉在 `_brick.yml` 里（`toolchain.tar.xz` = `07d8ae8f…42fde`，`sdk-usr.tgz` = `b6b615d0…f3c79`），缓存命中也校验一遍——缓存可能来自旧 key 或被截断，坏包绝不能进 sysroot。
3. **合并方向不能反**：仍用 `rsync -a --ignore-existing SDK_usr/ <tc>/aarch64-linux-gnu/libc/usr/`。反过来会让 SDK 的 glibc 2.23 stub 覆盖工具链库，链接直接 `undefined reference to __libc_dl_error_tsd@GLIBC_PRIVATE`（§13.9 记过）。
4. **`pak-verify` 是 CI 里唯一的硬门禁**：x86 runner 跑不了 aarch64 二进制（装 `qemu-user` 也跑不过 SDL 初始化），所以只能静态断言——`readelf -d` 的 NEEDED 必须落在固件库集合内，`objdump -T` 的最高符号版本必须 ≤ `GLIBC_2.17`。§2.2 的 `powf@GLIBC_2.27` 那一类回归只有这条能挡住。
5. **版本号来自 tag，手动运行降级**：`github.ref_name` 去掉 `v`；`workflow_dispatch` 时用 `0.0.0-dev`，和桌面三个 job 完全一致，避免出现"版本号对不上产物名"。
6. **打包就是 `zip -r ... MRG.pak`**：解压得到一个 `MRG.pak/` 目录，整个拷进 `/SDCARD/Apps/` 即可（§11.1）。不套额外目录层级，用户不需要理解结构。

#### CI 覆盖不到的部分

这四个 job 全绿也不代表能上真机。CI 里没有模拟器核心、没有 ROM、没有面板、没有摇杆、没有 ALSA，所以下面这些只能在真机上过一遍（§10 的 P0 探针 + §12.4 清单）：mali 驱动谎报的窗口尺寸（§3.3 三选一）、`SDL_GameController` 在 `trimui_inputd` 抢设备后能否拿到 0 号、音频延迟、以及 2000 MHz governor 下的实际掉帧。

### 12.4 发布检查清单

- [ ] 四平台版本号一致（`MRG_VERSION` 单一来源）
- [ ] brick 产物：`readelf -d mrg-player | grep NEEDED` 只出现 `libSDL2-2.0.so.0`、`libSDL2_ttf-2.0.so.0`、`libz`、`libdl`、`libpthread`、`libm`、`libc`；无 `libstdc++.so.6`
- [ ] brick 产物：`aarch64-linux-gnu-objdump -T` 最高符号版本 ≤ `GLIBC_2.17` / `GLIBCXX_3.4.21`
- [ ] 核心许可（§6.4）：随包的 fceumm（GPLv2）与 mgba（MPL-2.0）要附许可证文本；snes9x / genesis_plus_gx 的**非商业**条款要在发布页写明，或首发不带它们
- [ ] 内置 ROM 的版权状态（§6.4）：只放自有/公有领域/授权 ROM
- [ ] `config.json` 的 `label`/`icon`/`description` 四平台文案一致
- [ ] CHANGELOG 一段话版，附在 Release 正文
---

## 13. 编译配置与打包（已落地）

> 状态：**已落地并验证**（桌面 9 个测试全过；brick 交叉构建出 `MRG.pak` 并通过 `pak-verify`）。改动只碰构建配置与打包文件，**`src/` 一行未改**。实际实现与下面草案的四处出入、以及实测数字见 §13.9。

新增/修改的文件：

| 文件 | 说明 |
|---|---|
| `CMakeLists.txt` | 平台 profile、`mrg_configure_target()`、SDL2 二选一、`pak`/`pak-verify` target |
| `cmake/toolchain-aarch64-brick.cmake` | 交叉工具链（新） |
| `cmake/assemble-pak.cmake` | 组装 pak 布局的 CMake 脚本（新） |
| `pack/{config.json,launch.sh,icon.png}` | pak 元数据与图标（新） |
| `pack/{cores,roms}/README.md` | 占位说明，核心与 ROM 由 CI 拷入 |
| `tools/verify-pak.sh` | 产物自检（新） |

现有 `CMakeLists.txt` 骨架是好的（16-19 行默认 Release、25-34 行警告集、47-50 行子模块检查、57 行 smoke test 开关、148-157 行 Windows 专属库、271-322 行 `-static` 目标兜底），**不用推倒重来**，只在上面加四类东西：交叉编译、SDL2 来源切换、设备端编译/链接选项、pak 打包 target。全部改动只碰 `CMakeLists.txt` + 新目录 `cmake/`、`pack/`、`tools/`，**一行 `src/` 都不用改**。

### 13.1 新增的 option / 变量

| 名字 | 取值 | 作用 |
|---|---|---|
| `MRG_PLATFORM` | `desktop`（默认）/ `brick` | 平台 profile：画布宏、禁 zenity/下载/smoke test、测试关 |
| `MRG_USE_SYSTEM_SDL2` | ON/OFF | ON 时不 `add_subdirectory(third_party/SDL2*)`，改用 sysroot 里的 SDL2 |
| `MRG_SDL2_ROOT` | 路径 | 手动指定 SDL2 根（`include/` + `lib/`），覆盖 sysroot 探测 |
| （无） | — | 画布尺寸不再是编译开关：`#16` 之后它就是 `src/ui/Primitives.hpp:13-14` 的 `kLogicalWidth = 1280` / `kLogicalHeight = 720` 源码常量，桌面与 brick 共用一套坐标（§3.3） |
| `MRG_STRICT_WARNINGS` | ON/OFF | 追加 `-Wshadow -Wconversion -Wold-style-cast`，只在桌面开发时开 |
| `MRG_ENABLE_LTO` | ON/OFF | 桌面发布开 IPO；brick 默认关（8.3 交叉下收益不明显、编得慢） |
| `MRG_ENABLE_ASAN` | ON/OFF | 桌面开发 `-fsanitize=address,undefined` |
| `MRG_ENABLE_PAK_TARGET` | ON/OFF | 生成 `pak` 自定义 target（§13.5） |
| `MRG_ENABLE_CORE_DOWNLOAD` | ON（桌面默认）/ OFF（brick 强制） | 是否编译运行时核心下载：OFF 时 `CoreInstaller.cpp` 的 `installCore` 变成直接返回错误的桩，`util/Download.hpp`/`Sha256.hpp` 不再参与编译，`Application.cpp` 也不再发起下载线程，设置页「核心」只剩查看已装核心 |
| `MRG_PAK_DIR` | 路径 | pak 组装目录，默认 `${CMAKE_BINARY_DIR}/pak/MRG.pak` |

### 13.2 `cmake/toolchain-aarch64-brick.cmake`（新文件，全文）

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Arm GNU 8.3（MinUI 在用的那版）。官方 TrimUI Linaro 7.5 没有 <filesystem>，编不过本仓库（§2.1）。
set(MRG_CROSS_PREFIX aarch64-linux-gnu- CACHE STRING "cross tool prefix")
set(CMAKE_C_COMPILER   ${MRG_CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${MRG_CROSS_PREFIX}g++)
set(CMAKE_AR           ${MRG_CROSS_PREFIX}ar)
set(CMAKE_RANLIB       ${MRG_CROSS_PREFIX}ranlib)
set(CMAKE_STRIP        ${MRG_CROSS_PREFIX}strip)
set(CMAKE_READELF      ${MRG_CROSS_PREFIX}readelf)
set(CMAKE_OBJDUMP      ${MRG_CROSS_PREFIX}objdump)

# sysroot = 工具链自带 glibc + rsync 合并进来的 SDK_usr
set(MRG_SYSROOT "" CACHE PATH "TrimUI SDK sysroot (toolchain libc, with usr/ merged)")
if(NOT MRG_SYSROOT)
  message(FATAL_ERROR "pass -DMRG_SYSROOT=<path>/aarch64-linux-gnu/libc")
endif()
set(CMAKE_SYSROOT ${MRG_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${MRG_SYSROOT} ${MRG_SYSROOT}/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 设备根文件系统是 glibc 2.23 级别的老系统，稳定优先：不做 -march 优化，只加 gc-sections 减体积。
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--as-needed -Wl,-O1")

# 不用 pkg-config：设备上的 .pc 文件与 sysroot 前缀对不上，SDL2 在 §13.3 里手搓 imported target。
set(PKG_CONFIG_EXECUTABLE "" CACHE FILEPATH "" FORCE)
```

实际文件在此之上多了 §13.9 第 1 条的两处改动：转发 `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`、从编译器路径反推 sysroot；并且每个工具变量都用 `if(NOT CMAKE_CXX_COMPILER)` 之类的空值判断保护，避免嵌套 configure 把绝对路径覆盖成裸名字。

说明（都来自实测，不是猜的）：

- `-march=armv8-a+crc+crypto` 对 A53 安全，但本项目没有可测收益，**不加**——省得以后换工具链踩坑；
- 工具链自带 glibc 2.28，SDK 的 `libm.so` 是 2.23，**ld 默认会从 `=/lib64` 摸到 2.28 的 `libm`**，所以 §13.4 必须显式钉 `libm.so.6`；
- 千万不要把整个 `${CMAKE_SYSROOT}/usr/lib` 塞进 `-Wl,-rpath-link`：SDK 里那个 `libpthread.so.0` 会带出 `undefined reference to __libc_dl_error_tsd@GLIBC_PRIVATE`，链接直接失败。
### 13.3 SDL2 来源切换（改 `CMakeLists.txt:36-69` 那一段）

把现在"强制静态 + 无条件 `add_subdirectory`"的两段包进 `if`：

```cmake
option(MRG_USE_SYSTEM_SDL2 "Link the SDL2 that ships with the target system" OFF)
set(MRG_SDL2_ROOT "" CACHE PATH "Root of an SDL2 install (include/ + lib/)")

if(MRG_USE_SYSTEM_SDL2)
  # 设备固件自带 SDL2 2.0.26 / SDL2_ttf 2.0.10，且不带 CMake 配置文件：手搓 imported target。
  set(_sdl2_roots ${MRG_SDL2_ROOT} ${CMAKE_SYSROOT}/usr)
  find_path(SDL2_INCLUDE_DIR SDL.h
    PATHS ${_sdl2_roots} PATH_SUFFIXES include/SDL2 include NO_DEFAULT_PATH)
  find_library(SDL2_LIBRARY NAMES SDL2-2.0 SDL2
    PATHS ${_sdl2_roots} PATH_SUFFIXES lib NO_DEFAULT_PATH)
  find_library(SDL2TTF_LIBRARY NAMES SDL2_ttf-2.0 SDL2_ttf
    PATHS ${_sdl2_roots} PATH_SUFFIXES lib NO_DEFAULT_PATH)
  if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY OR NOT SDL2TTF_LIBRARY)
    message(FATAL_ERROR "system SDL2/SDL2_ttf not found under: ${_sdl2_roots}")
  endif()
  add_library(SDL2::SDL2 UNKNOWN IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION "${SDL2_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
  add_library(SDL2_ttf::SDL2_ttf UNKNOWN IMPORTED)
  set_target_properties(SDL2_ttf::SDL2_ttf PROPERTIES
    IMPORTED_LOCATION "${SDL2TTF_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
  # 设备上没有 SDL2main 静态库，也不需要有：入口就是普通 main。
else()
  # —— 以下是现有的 36-50 行，原样保留 ——
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  # ... SDL_STATIC / SDL_TEST / BUILD_SHARED_LIBS / SDL2TTF_* 同理 ...
  # 子模块存在性检查（47-50 行）
  # add_subdirectory(third_party/SDL2 ...) / add_subdirectory(third_party/SDL2_ttf ...)
endif()
```

为什么不用 CMake 自带的 `find_package(SDL2)`：CMake 不提供 `FindSDL2.cmake`，SDL2 2.0.26 的系统安装里也没有可用的 `SDL2Config.cmake`（SDK sysroot 只给了 `.so` 和头文件）。`find_path`+`find_library` 是这里唯一可靠的路子，而且能精确锁 `NO_DEFAULT_PATH`，避免摸到宿主机的 SDL2。

三个配套注意点：

1. 现有代码用 `if(TARGET SDL2::SDL2-static)` 兜底（83-87、176-180、271-282、318-322 行），系统库路径下没有 `-static` 别名，会自动落进 `SDL2::SDL2` 分支——**那几处 if 不用改**；
2. `346-348` 行的 `if(TARGET SDL2::SDL2main)` 同理，brick 下自然为假；
3. SDL2_ttf 在设备上是 **2.0.10**，只导出 48 个 `TTF_*`。本仓库当前只用到 `TTF_OpenFont` / `TTF_SizeUTF8` / `TTF_RenderUTF8_Blended` / `TTF_SetFontHinting` / `TTF_FontAscent` / `TTF_Init` / `TTF_Quit` / `TTF_CloseFont` / `TTF_WasInit`，**全部在 2.0.10 里**；将来不要新增 `TTF_SetFontSize` / `TTF_MeasureUTF8` / `TTF_GetFontHeight` / `TTF_OpenFontIO`，设备上会链接失败。

### 13.4 平台 profile 与设备端编译/链接选项

`project()` 之后加：

```cmake
set(MRG_PLATFORM "desktop" CACHE STRING "desktop | brick")
set_property(CACHE MRG_PLATFORM PROPERTY STRINGS desktop brick)
if(MRG_PLATFORM STREQUAL "brick")
  set(MRG_USE_SYSTEM_SDL2 ON CACHE BOOL "" FORCE)
  set(MRG_ENABLE_SMOKE_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)   # 设备侧不跑单测（需 dummy 视频驱动）
endif()
```

统一的目标配置函数（替换各 target 现有的 `target_compile_options(... ${MRG_WARNING_FLAGS})`、`target_compile_features(... cxx_std_17)` 重复行）：

```cmake
function(mrg_configure_target target)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE ${MRG_WARNING_FLAGS})
  if(MRG_PLATFORM STREQUAL "brick")
    target_compile_definitions(${target} PRIVATE MRG_PLATFORM_BRICK=1)
    target_compile_options(${target} PRIVATE
      -ffunction-sections -fdata-sections -fvisibility=hidden)
    target_link_options(${target} PRIVATE
      -static-libstdc++ -static-libgcc
      -Wl,--gc-sections -Wl,--as-needed)
  endif()
  if(MRG_ENABLE_LTO AND NOT MRG_PLATFORM STREQUAL "brick")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo LANGUAGES CXX)
    if(_ipo)
      set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
  endif()
  if(MRG_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
  if(MRG_STRICT_WARNINGS AND NOT MSVC)
    target_compile_options(${target} PRIVATE -Wshadow -Wconversion -Wold-style-cast)
  endif()
endfunction()
```

**必须显式钉住 libm**（§2.2 的 `powf@GLIBC_2.27` 坑）——放在 `mrg-player` 链接 SDL2 之后：

```cmake
if(MRG_PLATFORM STREQUAL "brick")
  # 只钉单个库文件；绝不能把整个 ${CMAKE_SYSROOT}/usr/lib 塞进 rpath-link（§13.2 说明）
  target_link_libraries(mrg-player PRIVATE ${CMAKE_SYSROOT}/usr/lib/libm.so.6)
endif()
```

为什么需要 `-static-libstdc++ -static-libgcc`：SDK sysroot 里**没有** `libstdc++.so.6`（§2.2），而设备固件虽然有 `libstdc++`，版本无从确认；静态链进去后 NEEDED 里就没有 `libstdc++.so.6`，最省心。

编译选项的效果（实测数据，供对照）：

| 项目 | 手工 g++ 命令行 | 加 `-ffunction-sections/-fdata-sections/-Wl,--gc-sections` + strip |
|---|---|---|
| 体积 | 1,454,752 B（strip 后） | 预计再省 5–10%（可执行本身不大，收益有限，但成本为零） |
| NEEDED | SDL2 / SDL2_ttf / z / dl / pthread / m / c | 同左，无 `libstdc++.so.6` |

### 13.5 `pak` 打包 target（`MRG_ENABLE_PAK_TARGET=ON`）

```cmake
option(MRG_ENABLE_PAK_TARGET "Generate the TrimUI .pak layout" OFF)
if(MRG_ENABLE_PAK_TARGET)
  set(MRG_PAK_DIR "${CMAKE_BINARY_DIR}/pak/MRG.pak" CACHE PATH "assembled pak directory")
  add_custom_target(pak
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${MRG_PAK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MRG_PAK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mrg-player> "${MRG_PAK_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${MRG_PAK_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/pack/cores" "${MRG_PAK_DIR}/cores"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/pack/config.json" "${MRG_PAK_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/pack/icon.png" "${MRG_PAK_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/pack/launch.sh" "${MRG_PAK_DIR}/"
    COMMAND ${CMAKE_STRIP} "${MRG_PAK_DIR}/mrg-player"
    DEPENDS mrg-player
    COMMENT "Assembling MRG.pak"
    VERBATIM)
endif()
```

`pack/` 建议入库**只有三个小文件**：`config.json`、`launch.sh`（§7 的模板）、`icon.png`；`pack/cores/` 与 `roms/` 不入库，由 CI 在打包前从各自的构建产物拷进去（避免几百 MB 二进制进 git）。

### 13.6 产物自检（防回归，挡掉 90% 的链接事故）

```cmake
add_custom_target(pak-verify
  COMMAND sh "${CMAKE_SOURCE_DIR}/tools/verify-pak.sh" "${MRG_PAK_DIR}"
    "${CMAKE_READELF}" "${CMAKE_OBJDUMP}"
  DEPENDS pak
  COMMENT "Verifying MRG.pak linkage"
  VERBATIM)
```

`tools/verify-pak.sh` 检查四件事，任何一条不满足就非零退出：

1. `readelf -d` 的 NEEDED 只能是 `libSDL2-2.0.so.0`、`libSDL2_ttf-2.0.so.0`、`libz.so.1`、`libdl.so.2`、`libpthread.so.0`、`libm.so.6`、`libc.so.6`；出现 `libstdc++.so.6` 就失败；
2. `objdump -T` 里最高的 `GLIBC_x.y` ≤ **2.17**（设备 glibc 未知，就按 MinUI 的 2.17 卡）；
3. 最高的 `GLIBCXX_x.y.z` ≤ **3.4.21**；
4. `cores/` 里每个 `.so` 都能被 `readelf -d` 读通且没有缺失的 NEEDED（用 `-Wl,--no-undefined` 编核心更早暴露问题）。

### 13.7 桌面侧顺手的开关（可选项，不阻塞移植）

| 需求 | 做法 |
|---|---|
| 编译加速 | `find_program(CCACHE ccache)` + `set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})` |
| clangd / IDE | `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` |
| 最快构建系统 | 命令里加 `-G Ninja`（CI 也用它，比 Make 快 20–30%） |
| 发布瘦身 | `-DCMAKE_BUILD_TYPE=MinSizeRel` + `MRG_ENABLE_LTO=ON` |
| 内存/越界排查 | `-DMRG_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug` |
| CI 零警告 | 已有 `MRG_WARNINGS_AS_ERRORS=ON`（31-34 行），CI 里打开即可 |

### 13.8 落地顺序（累计约 1.5 人日，都在 P1 里）

| 步骤 | 内容 | 产出 | 估时 |
|---|---|---|---|
| 1 | `cmake/toolchain-aarch64-brick.cmake` + `MRG_PLATFORM` + `MRG_USE_SYSTEM_SDL2` | 能 configure、能链接出可执行文件 | 0.5 d |
| 2 | `mrg_configure_target()` + 钉 `libm.so.6` + `-static-libstdc++` + strip | NEEDED 只剩系统库，`powf@GLIBC_2.17` | 0.5 d |
| 3 | `pak` target + `tools/verify-pak.sh` | `cmake --build . --target pak` 直接出可拷的 `MRG.pak` | 0.5 d |
| 4 | 可选项（LTO/ccache/ASan/Ninja） | 开发体验 | 随时 |

改完之后的完整命令（`MRG_SYSROOT` 指向合并过 SDK_usr 的工具链 libc）：

```bash
cmake -S . -B build-brick -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-brick.cmake \
  -DMRG_SYSROOT=/path/to/aarch64-linux-gnu-8.3/aarch64-linux-gnu/libc \
  -DMRG_PLATFORM=brick \
  -DMRG_ENABLE_PAK_TARGET=ON \
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-brick --target pak pak-verify
```

### 13.9 落地记录与实测（2025-11-26）

实际实现与 §13.1–13.8 草案有四处出入，都是踩出来的，顺序即踩坑顺序：

#### 1. toolchain 不能硬性要求 `-DMRG_SYSROOT`（否则 configure 直接失败）

`project()` 会先做一次 `try_compile` 探编译器 ABI，那次嵌套 configure **拿不到命令行上的 `-D` 用户变量**，于是工具链文件里 `if(NOT MRG_SYSROOT) message(FATAL_ERROR ...)` 必然触发：

```text
CMake Error at cmake/toolchain-aarch64-brick.cmake:28 (message):
  Pass -DMRG_SYSROOT=<toolchain>/aarch64-linux-gnu/libc
Call Stack (most recent call first):
  /tmp/mrg-brick13/CMakeFiles/3.28.3/CMakeSystem.cmake:6 (include)
```

两个修法**都要有**：

- `set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MRG_SYSROOT MRG_CROSS_PREFIX)`，把这两个变量转发进嵌套 configure；
- sysroot 缺省值从编译器路径反推（`<tc>/bin/aarch64-linux-gnu-g++` → `<tc>/aarch64-linux-gnu/libc`），只在拿不到绝对路径时才报错。

风险提示：只在嵌套 configure 里"忽略"这个错误是最坏做法——那次探测会静默用宿主机的头文件算出 ABI。

#### 2. 必须显式 `find_package(Threads)`

`src/ui/CoverCache.cpp` 用了工作线程，桌面构建碰巧能过是因为本机 glibc ≥ 2.34 把 pthread 并进了 libc；设备那版没有：

```text
ld: CMakeFiles/mrg-player.dir/src/ui/CoverCache.cpp.o: undefined reference to symbol pthread_create@@GLIBC_2.17
ld: .../libc/lib64/libpthread.so.0: error adding symbols: DSO missing from command line
```

修法：`find_package(Threads REQUIRED)` + `target_link_libraries(mrg-platform PUBLIC Threads::Threads)`。这条对桌面也无害。

#### 3. GCC 8 要额外链 `stdc++fs`

`src/config/AppPaths.cpp`、`src/game/GamePlatform.cpp`、`src/util/Utf8Path.cpp` 都用了 `<filesystem>`。GCC 9 起它并进 libstdc++，GCC 8 还在单独库里：

```text
ld: AppPaths.cpp:(.text...dataSubdirectory...)+0x68: undefined reference to std::filesystem::__cxx11::path::_M_split_cmpts()
```

修法：`if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9) target_link_libraries(${target} PUBLIC stdc++fs)`。

#### 4. `pak` target 用 CMake 脚本而不是一长串 `-E copy`

`cmake -E copy` 没有条件判断，`pack/cores`、`pack/roms` 可能是空的、`pack/icon.png` 可能还没做出来。改成 `cmake -P cmake/assemble-pak.cmake`，脚本里用 `if(IS_DIRECTORY)` / `if(EXISTS)` 逐项判断，也让这个脚本能脱离构建单独调布局。

### 13.10 验证结果

桌面（`-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`，本机 CMake 3.28.3 / 自带 SDL2 子模块）：

| 项目 | 结果 |
|---|---|
| 配置 + 构建 | 397 个目标全部完成，无警告（`-Wall -Wextra -Wpedantic`） |
| `ctest` | **9/9 通过**（4.85 s） |

brick 交叉（Arm GNU 8.3 + 合并 sysroot，`-DMRG_PLATFORM=brick -DCMAKE_BUILD_TYPE=MinSizeRel`）：

| 项目 | 结果 |
|---|---|
| 配置 | `-- Using system SDL2: <sysroot>/usr/lib/libSDL2.so` |
| 构建 | `mrg-player` 815,536 B（`-Os` + gc-sections + strip，比之前手工命令行的 1.45 MB 小 44%） |
| `pak` target | 组装出 `/tmp/mrg-brick13/pak/MRG.pak`，5.5 MB（大头是 3.9 MB 的 `DroidSansFallbackFull.ttf`） |
| `pak-verify` | NEEDED 仅 `libc/libm/libpthread/libSDL2-2.0/libSDL2_ttf-2.0/libz`；**GLIBC max 2.17**；GLIBCXX 无（已静态链接）；`verify-pak: ok` |

也就是说 §2.2 那个 `powf@GLIBC_2.27` 的坑，在 CMake 里靠钉 `libm.so.6` 确实被摁住了；`-static-libstdc++` 生效后 NEEDED 里也没有 `libstdc++.so.6`。

剩下的只有真机能验证的部分：mali 驱动报的窗口尺寸（§3.3 三选一）、摇杆 mapping、ALSA 延迟。

### 13.11 CI 里的 Brick 发布包（2025-11-26）

`release.yml` 的第 4 个 job `brick` 与 `linux` / `windows` / `macos` 并列，`release` job 的 `needs` 加上它，所以桌面三个包 + 设备包一起打完才发 Release。真正干活的是可复用工作流 `.github/workflows/_brick.yml`（`workflow_call`）；`ci.yml` 在每次 push / PR 时也调用同一个工作流（不传 `upload`），设备侧的链接回归在 PR 阶段就能看到（§12.3）。

| 项目 | 值 |
|---|---|
| 工具链 | Arm GNU 8.3-2019.02（`developer.arm.com` 直链）+ `SDK_usr_tg5040_a133p`（TrimUI GitHub release 20231018） |
| 缓存 | 只缓存两个 tarball（376 MB），key `trimui-brick-arm-gnu-8.3-2019.02-sdk-20231018-v2`；每次运行都用钉死的 SHA-256 校验 |
| 配置 | `-DMRG_PLATFORM=brick -DCMAKE_BUILD_TYPE=MinSizeRel -DMRG_VERSION_STRING=<tag>` |
| 核心 | `tools/build-cores.sh` 现场交叉编译 fceumm / mgba / snes9x / genesis_plus_gx，源码目录另外缓存（§6.2.1） |
| 构建 | `cmake --build build-brick --parallel --target pak pak-verify` |
| 产物 | `mrg-player-<ver>-trimui-brick.zip`（约 6.5 MB，含四个核心），解压得到 `MRG.pak/`，整个拷进 `/SDCARD/Apps/`（§11.1） |

状态：**已在 GitHub 上真跑通**。带核心构建后的实测：run [45752223507](https://github.com/laomou/MRG-Player/actions/runs/45752223507)（push `0f120b8`），4 个检查全绿，brick job 3 分 0 秒，其中四个核心从零编译 97 秒。

| 项目 | CI 实测 |
|---|---|
| 配置 | `-- Found Threads: TRUE`、`-- Using system SDL2: /home/runner/work/_temp/trimui/tc/aarch64-linux-gnu/libc/usr/lib/libSDL2.so` |
| 核心编译 | 03:59:23 fceumm → 04:01:00 genesis_plus_gx，共 97 秒；四个 `.so` 体积与本地一致（4,476,792 / 3,288,632 / 3,735,000 / 12,687,456 B） |
| `pak-verify` | `verify-pak: ok`，`cores checked: 4`；launcher 与每个核心都是 `GLIBC max: 2.17`、`GLIBCXX max: 0` |
| 产物 | `mrg-player-0.0.0-dev-trimui-brick.zip` **6,773,225 B**（`MRG.pak` 解压后 29 MB：核心 24.3 MB + 字体 4.8 MB） |
| 解包核对 | `MRG.pak/{config.json,icon.png,launch.sh,mrg-player,cores/,roms/,res/fonts/}`；`mrg-player` 是 `ELF 64-bit LSB executable, ARM aarch64`，815,536 B，`+x` 位在 zip 里保留 |

踩到的一条：job 级 `env:` 不能用 `${{ runner.temp }}`（actionlint 报 *context "runner" is not allowed here*），改成每个 `run` 块里自己取 `TC="$RUNNER_TEMP/trimui"`。
