# 在 PC 上跑 TrimUI Brick（QEMU）

这份文档写给要替换固件里的 pak、然后用截图和按键验证界面的人。它不依赖会话记忆：
照第 0 节的最短路径走一遍，最快十分钟内能在模拟器里看到 MRG Player 起来。

先把两个仓库分清楚，命令写错目录是第一次上手最常见的卡点：

| 变量 | 仓库 | 作用 |
| --- | --- | --- |
| `$MRG` | 本仓库（MRG-Player） | 播放器源码、`tools/brick-exec.py`、构建产物 |
| `$EMU` | [trimui-brick-emu](https://github.com/imrmnabil/trimui-brick-emu) | QEMU 整机、固件镜像、`./brick` 入口、`tools/qmon.py` |

```bash
export MRG="$HOME/code/MRG-Player"
export EMU="$HOME/code/trimui-brick-emu"
```

模拟器把 `$EMU/out/sd-mrg.img` 当作 SD 卡，屏幕 1024×768，固件版本 v1.1.1-20251126。
它跑的是完整固件（MainUI、Emus、RetroArch 都在），所以这里既能验 MRG 自己，
也能验从桌面磁贴启动的那条链路。纯布局改动不要在这里磨：那台机器是 TCG 全软件模拟，
一次重启两三分钟，先做第 9 节的宿主机 smoke 测试。

## 0. 最短路径（环境已经装好时）

`$EMU` 下执行，四个步骤：起机、等 MainUI、换掉 MainUI、看画面。

```bash
cd "$EMU"

# 1. 起机（必须有 --display vnc，否则 sendkey 会被静默丢弃，见第 7 节）
./brick run out/sd-mrg.img --display vnc --monitor out/run/mon.sock \
  --serial-sock out/run/ser.sock --log out/run/serial.log --gpu soft &

# 2. 等 MainUI 起来（约 120–180 秒，不要用固定 sleep 猜）
for i in $(seq 1 48); do
  [ -n "$("$MRG/tools/brick-exec.py" out/run/shell.sock 'pidof MainUI' 2>/dev/null)" ] && break
  sleep 5
done

# 3. 停守护、杀 MainUI、清掉残留的 MRG，再启动（顺序反了会抢屏，首帧冻结）
runner=$("$MRG/tools/brick-exec.py" out/run/shell.sock 'pidof runtrimui.sh')
[ -n "$runner" ] && "$MRG/tools/brick-exec.py" out/run/shell.sock "kill -STOP $runner"
mainui=$("$MRG/tools/brick-exec.py" out/run/shell.sock 'pidof MainUI')
[ -n "$mainui" ] && "$MRG/tools/brick-exec.py" out/run/shell.sock "kill -9 $mainui"
"$MRG/tools/brick-exec.py" out/run/shell.sock 'kill -9 $(pidof mrg-player) 2>/dev/null; echo clean'
"$MRG/tools/brick-exec.py" out/run/shell.sock \
  'cd /mnt/SDCARD/Apps/MRG.pak && LD_LIBRARY_PATH=/usr/trimui/lib ./launch.sh > /tmp/mrg.log 2>&1 </dev/null &'

# 4. 截图
python3 tools/qmon.py out/run/mon.sock 'screendump /tmp/home.png -f png'
```

第 3 步先在 guest 里 `kill -STOP` 掉 `runtrimui.sh`：它是 MainUI 的守护进程，不冻住的话
MainUI 会被拉起来重新占住 DRM，MRG 就只剩一张黑帧。`kill -9 $(pidof mrg-player)` 清掉的是
上一次测试留下的实例，多个 MRG 同时开会互相抢屏。

guest 的 root shell 会正常展开 `$( )` 和变量，但只用 guest 自己的；宿主机变量要在宿主机
先展开再拼进命令。路径里可能有空格，拼进去时记得加引号。

换 pak、改镜像和第一次构建都在后面几节。

## 1. 前置条件

- Linux 宿主机，能跑 Docker；`git` 和 `python3`（跑 `tools/qmon.py`、`tools/brick-exec.py`）。
- `/dev/kvm` 不是必需的；没有 KVM 时用 TCG，慢但能跑。
- 磁盘约 15 GB（固件、eMMC、SD 镜像、构建缓存），内存 8 GB 以上。
- 不需要系统装 QEMU：`./brick setup` 会把 QEMU 编到 `out/qemu/`。宿主机也不需要
  mtools，写镜像用 `brick-emu-builder` 容器。
- 网络：`./brick setup` 会下约 112 MB 官方固件；交叉工具链另算（第 3 节）。

## 2. 第一次构建模拟器

只做一次，之后直接 `./brick run`。

```bash
cd "$EMU"
./brick setup
```

`setup` 做四件事，全部成功才会有 `out/Image`、`out/brick-emu.img`、`out/emmc.img`：

1. 下载 TrimUI Brick 官方固件（默认 v1.1.1-20251126，可传 tag 换版本）。
2. 构建 `brick-emu-builder` Docker 镜像（Debian + mtools + 交叉工具）。
3. 编译内核与 initramfs（`out/Image`、`out/brick-emu.img`）。
4. 生成 eMMC 镜像 `out/emmc.img`。

全程约 35 分钟，主要耗在 Docker 构建和内核编译。之后再 `./brick setup` 会复用缓存。

## 3. 交叉工具链（要改代码、重新编 pak 时才需要）

产物是 aarch64 的 `mrg-player`，必须用和固件一致的 glibc（2.17）工具链。
这台开发机上通常已经有一份：

```bash
test -x "$HOME/trimui/tc/bin/aarch64-linux-gnu-gcc" && echo ready
```

没有就按下面准备，和 CI（`.github/workflows/_brick.yml`）用的完全一致。

```bash
mkdir -p "$HOME/trimui/dl" && cd "$HOME/trimui/dl"
curl -fL --retry 3 -o gcc.tar.xz \
  "https://developer.arm.com/-/media/Files/downloads/gnu-a/8.3-2019.02/gcc-arm-8.3-2019.02-x86_64-aarch64-linux-gnu.tar.xz"
curl -fL --retry 3 -o sdk-usr.tgz \
  "https://github.com/trimui/toolchain_sdk_smartpro/releases/download/20231018/SDK_usr_tg5040_a133p.tgz"

echo '07d8ae8fa505ccadfc8cbac8b77d6f47740ecec2079267ad1d0a20bdc5342fde  gcc.tar.xz' | sha256sum -c -
echo 'b6b615d03204e9d9bb1a91c31de0c3402434f6b8fc780743fa88a5f1da6f3c79  sdk-usr.tgz' | sha256sum -c -

mkdir -p "$HOME/trimui/tc"
tar -xJf gcc.tar.xz -C "$HOME/trimui/tc" --strip-components=1
mkdir -p /tmp/trimui-sdk && tar -xzf sdk-usr.tgz -C /tmp/trimui-sdk
mkdir -p "$HOME/trimui/tc/aarch64-linux-gnu/libc"
rsync -a --ignore-existing /tmp/trimui-sdk/usr/ \
  "$HOME/trimui/tc/aarch64-linux-gnu/libc/usr/"
```

`rsync --ignore-existing` 的方向不能反：SDK 的 `usr/` 只补工具链 sysroot 里缺的头文件和库，
反过来用工具链覆盖 SDK 会带进 glibc 2.33 的符号，链接时报 `GLIBC_PRIVATE`。

编 pak：

```bash
cmake -S "$MRG" -B "$MRG/build-brick" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$MRG/cmake/toolchain-aarch64-brick.cmake" \
  -DMRG_CROSS_PREFIX="$HOME/trimui/tc/bin/aarch64-linux-gnu-" \
  -DMRG_SYSROOT="$HOME/trimui/tc/aarch64-linux-gnu/libc" \
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build "$MRG/build-brick" --parallel --target pak pak-verify
```

- `pak` 输出到 `$MRG/build-brick/pak/MRG.pak`（`MRG_PAK_DIR` 可覆盖）。
- `pak-verify` 用 `tools/verify-pak.sh` 检查 GLIBC 符号版本不超过 2.17。
- 核心不随仓库走：`platforms/trimui/pak/cores/*.so` 在 `.gitignore` 里。全新克隆后先
  `tools/build-cores.sh --prefix ... --sysroot ...` 生成四个核心（fceumm、mgba、snes9x、
  genesis_plus_gx），否则打出的 pak 里没有 `cores/`，进游戏会报找不到核心。
- 只想验证界面时可以先用宿主机构建（第 9 节），不必等交叉编译。

## 4. 做卡、装 pak

### 4.1 生成空卡

```bash
cd "$EMU"
./brick sd out/sd-mrg.img 8G
```

得到一个带 MBR + FAT32 的镜像，根目录已经有 `Apps/`、`Roms/`、`Imgs/`、`.userdata/` 等。
以后只换 pak 不需要重做卡。

### 4.2 把 pak 拷进去

QEMU 必须先停：运行中写镜像会静默失败，看起来像"改了没生效"。宿主机没装 mtools 时用
`brick-emu-builder` 容器；`@@1M` 是分区偏移，不能省。

```bash
# pak 在 build-brick 里，容器只挂载 $EMU，所以先放进 out/
mkdir -p "$EMU/out/pak"
rm -rf "$EMU/out/pak/MRG.pak"
cp -a "$MRG/build-brick/pak/MRG.pak" "$EMU/out/pak/"

docker run --rm --user "$(id -u):$(id -g)" -v "$EMU":/work:z brick-emu-builder bash -ec '
  export MTOOLS_SKIP_CHECK=1
  IMG=/work/out/sd-mrg.img@@1M
  mdeltree -i "$IMG" ::/Apps/MRG.pak 2>/dev/null || true
  mcopy -s -o -Q -i "$IMG" /work/out/pak/MRG.pak ::/Apps/
  mdir -i "$IMG" ::/Apps
'
```

先 `mdeltree` 再 `mcopy`：`mcopy -s -o` 只覆盖同名文件，旧版本删掉的文件会留在卡上。
`mdir` 应能看到 `MRG.pak` 目录。

桌面磁贴（`platforms/trimui/emus/`）是可选的，要测从 Games 页启动才需要：

```bash
rm -rf "$EMU/out/emus" && cp -a "$MRG/platforms/trimui/emus" "$EMU/out/emus"
docker run --rm --user "$(id -u):$(id -g)" -v "$EMU":/work:z brick-emu-builder bash -ec '
  export MTOOLS_SKIP_CHECK=1
  IMG=/work/out/sd-mrg.img@@1M
  mmd -i "$IMG" ::/Emus 2>/dev/null || true
  mcopy -s -o -Q -i "$IMG" /work/out/emus/* ::/Emus/
'
```

先在宿主机把 `emus` 复制到容器挂载点里，避免在容器命令里拼宿主机路径。

## 5. 启动模拟器

```bash
cd "$EMU"
pkill -x qemu-system-aar 2>/dev/null || true   # 旧实例会占住 5900、mon.sock；comm 被截到 15 字符
./brick run out/sd-mrg.img \
  --display vnc \
  --monitor out/run/mon.sock \
  --serial-sock out/run/ser.sock \
  --log out/run/serial.log \
  --gpu soft
```

- `--display vnc` 在 `127.0.0.1:5900` 提供画面，VNC 客户端可看；截图不需要连它。
- `--gpu soft` 用软件光栅，稳定但慢。
- `--headless`（或 `--display none`）只留串口控制台，**不能用来测按键**：QEMU 会静默丢掉
  `sendkey`，表现为画面冻结、怎么按都没反应。这是最容易踩的一个坑。
- 启动到 MainUI 约 120–180 秒。用第 0 节的轮询判断就绪，别用固定 sleep。
- 串口日志在 `out/run/serial.log`，内核 panic、init 失败都写在这里。

## 6. 在 guest 里执行命令

MRG 占屏、换设置、清状态都要在 guest 里跑命令。给自动化用的入口是 hvc1 root shell 的
Unix socket（`out/run/shell.sock`）：

```bash
"$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" 'pidof MainUI'
"$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" 'ls -l /mnt/SDCARD/Apps/MRG.pak'
"$MRG/tools/brick-exec.py" --timeout 120 "$EMU/out/run/shell.sock" 'cat /tmp/mrg.log'
```

它连上 socket、发命令、用一个标记读回显，打印去掉 ANSI 和提示符后的输出；退出码只表示
宿主机这一侧是否连上，不表示 guest 命令成功。

`./brick shell` 是给人用的交互式终端（同一个 socket，Ctrl-] 退出），脚本里不要用它：
它不会返回，管道也拿不到输出。没有 `brick-exec.py` 时也可以自己写十几行 socket 客户端，
协议就是"发命令 + 发 `echo TAG`，读到 TAG"。

### 让 MRG 占屏

顺序是：冻住守护进程 → 杀 MainUI → 清掉残留的 MRG → 启动。反了会互相抢 DRM，
表现为黑屏或首帧冻结。

```bash
runner=$("$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" 'pidof runtrimui.sh')
"$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" "kill -STOP $runner"
mainui=$("$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" 'pidof MainUI')
"$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" "kill -9 $mainui"
"$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" 'kill -9 $(pidof mrg-player) 2>/dev/null; echo clean'
"$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" \
  'cd /mnt/SDCARD/Apps/MRG.pak && LD_LIBRARY_PATH=/usr/trimui/lib ./launch.sh > /tmp/mrg.log 2>&1 </dev/null &'
```

从桌面磁贴启动则不需要手动杀 MainUI（磁贴的 `launch.sh` 会转给 `Apps/MRG.pak/launch.sh`），
但残留的 MRG 实例仍然要先清掉。

## 7. 截图与按键

`tools/qmon.py` 在 `$EMU` 里，不是 MRG 仓库：

```bash
cd "$EMU"
python3 tools/qmon.py out/run/mon.sock 'screendump /tmp/shots/home.png -f png'
python3 tools/qmon.py out/run/mon.sock --press x 200
```

`screendump` 的路径是**宿主机**路径，目录要先存在。`--press` 等价于 HMP 的
`sendkey KEY DURATION`，键名小写（`left`、`ret`、`esc`），写成 `Left` 会报
`invalid parameter`。宿主机的键盘不会进 guest，所有按键都要走这条命令。

仿真器把宿主机键盘映射成 Brick 手柄（`kernel/brick-hw/brick-hw.c`）：

| sendkey | Brick 键 | sendkey | Brick 键 |
| --- | --- | --- | --- |
| `x` | A（确认） | `ret` | START |
| `z` | B（返回） | `backspace` / `shift` | SELECT |
| `s` | X（选项） | `esc` / `m` | MENU |
| `a` | Y | `1` / `2` | F1 / F2 |
| `q` / `w` | L1 / R1 | `p` | 电源 |
| `e` / `r` | L2 / R2 | 方向键 | D-pad |

时序：状态切换后等 0.6–1 秒再截图；同一个画面、同一时刻的截图内容完全一致（TCG 是确定性的），
不要拿 sha 相同当作"按键没生效"的证据。

## 8. 清掉上一次测试的状态

镜像会被反复使用，`.userdata/mrg` 里的旧 `settings.ini` 和 `library/metadata.tsv` 会留在卡上。
症状很像程序 bug：书架显示已经删掉的 ROM、选中框停在列表外、"NOT FOUND"、语言不是默认值。

停掉 MRG 后，在 guest 里清：

```bash
"$MRG/tools/brick-exec.py" "$EMU/out/run/shell.sock" \
  'kill -9 $(pidof mrg-player) 2>/dev/null; rm -rf /mnt/SDCARD/.userdata/mrg/library /mnt/SDCARD/.userdata/mrg/settings.ini'
```

需要连存档一起清就再删 `saves/`、`states/`；要保留卡上的 ROM 和封面，别动 `Roms/`、`Imgs/`。
改完重新按第 6 节启动。改 pak 自带的图标或资源后必须整机重启，MainUI 有缓存。

## 9. 快速迭代：先在宿主机看布局

模拟器一轮两三分钟，只适合验证集成（启动链路、暂停菜单、输入、存档）。布局、字号、对齐
这种改动在宿主机上用 smoke 模式秒级出图：

```bash
cmake -S "$MRG" -B "$MRG/build" -G Ninja \
  -DMRG_CANVAS_WIDTH=1024 -DMRG_CANVAS_HEIGHT=768 \
  -DMRG_ENABLE_SMOKE_TESTS=ON
cmake --build "$MRG/build" --parallel

cd /tmp/shots
SDL_VIDEODRIVER=dummy XDG_DATA_HOME=/tmp/shots/data \
  "$MRG/build/mrg-player" --smoke-test-home
```

- 截图是 `*-screenshot.bmp`，落在**当前工作目录**，不在 `XDG_DATA_HOME`。
- `--smoke-test-home` 出书架和退出框；`--smoke-test-settings` 出设置各页（含控制器、核心、主题）。
- 要 1024×768 掌机面板就传 `-DMRG_CANVAS_WIDTH=1024 -DMRG_CANVAS_HEIGHT=768` 重新配置；
  这只是画布尺寸，不是 `MRG_PLATFORM=trimui`（掌机专属行为要另加
  `-DCMAKE_CXX_FLAGS=-DMRG_PLATFORM_TRIMUI`）。
- smoke 模式**不会导入 ROM**，也不读真实库；要验证导入、元数据、存档路径还是得用模拟器或
  真实数据目录跑一次。

## 10. 常见问题和排查顺序

1. **画面不动、按键没反应**：是不是用了 `--headless` / `--display none`？换成 `--display vnc`。
2. **黑屏但进程在**：`runtrimui.sh` 没冻住，MainUI 又抢回了 DRM；或者 MRG 启动早于
   MainUI 退出。按第 6 节顺序重来，先 `kill -STOP` 守护进程。
3. **改了 pak 没变化**：QEMU 还开着写镜像，或 `mcopy` 没带 `-o`，或旧文件没删。停 QEMU，
   `mdeltree` 后重拷。
4. **进游戏报找不到核心**：pak 里没有 `cores/`。用 `tools/build-cores.sh` 生成后重打 pak。
5. **启动即退出**：`cat /tmp/mrg.log`；`GLIBC_2.xx not found` 说明工具链用错（第 3 节），
   不是固件问题。
6. **书架内容对不上**：先按第 8 节清状态，再判断是不是代码问题。
7. **VNC 连不上**：5900 被上一个 QEMU 占着；`pkill -x qemu-system-aar` 后重开。
   QEMU 的进程名被内核截到 15 字符，所以要写 `qemu-system-aar`；不要用 `pkill -f`，
   那会连命令行里含这个字符串的 shell 一起杀掉。
8. **串口没输出**：确认传了 `--serial-sock` 和 `--log`；内核早崩时只有 `out/run/serial.log` 有线索。

## 11. 相关文档

- `docs/input.md`：键盘和手柄怎么进到核心。
- `docs/paths.md`：卡上目录、数据根、存档位置。
- `docs/ui-style.md`：界面布局与底部提示条的规则。
- emulator 仓库的 `BUILD.md` 与 `scripts/run.sh`：`./brick` 各子命令和 QEMU 参数的原始定义。
