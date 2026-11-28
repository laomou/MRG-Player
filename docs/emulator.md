# 在 PC 上跑 TrimUI Brick（QEMU 模拟器）

没有掌机的时候，用 [trimui-brick-emu](https://github.com/imrmnabil/trimui-brick-emu) 在 PC 上启动原厂固件，
验证 MRG 的启动链路、界面布局和按键。它跑的是 1024×768 的真实固件，ARM 指令全部由 QEMU 翻译，
所以只适合看功能与布局，不能拿来测帧率或性能。

模拟器本身不需要 MRG 做任何改动：对它来说，MRG 就是一个普通的 pak。

## 1. 需要什么

- Linux x86_64 桌面（X11 或 Wayland），8 GB 内存、约 15 GB 磁盘。
- Docker，且当前用户能直接 `docker run`（`sudo usermod -aG docker $USER` 之后要重新登录）。
- Ubuntu 24.04 / Debian 12 以上：

```bash
sudo apt update
sudo apt install git docker.io python3 python3-gi python3-gi-cairo python3-cairo \
    gir1.2-gtk-3.0 gdisk e2fsprogs dosfstools mtools p7zip-full \
    qemu-system-arm qemu-system-gui qemu-system-modules-opengl
```

Ubuntu 22.04 的 QEMU 太旧，可以跳过 qemu 那几项，`./brick setup` 会自己编一个。
首次构建要联网：内核和 Debian 的 arm64 库是在构建时下载的。

## 2. 下载与首次构建

```bash
git clone https://github.com/imrmnabil/trimui-brick-emu.git
cd trimui-brick-emu
./brick setup
```

`setup` 做四件事：

1. 从 TrimUI 的 releases 下载原厂固件（约 112 MB），解到 `firmware/`；
2. 构建 Docker 构建容器（arm64 交叉编译器 + Debian arm64 库）；
3. 编 guest 内核 `out/Image` 和加料盘 `out/brick-emu.img`（Mesa、EGL shim、Wi-Fi 模拟）；
4. 建 8 GB 稀疏 eMMC `out/emmc.img`。

第一次 15–30 分钟。之后改了内核或加料盘用 `./brick update`，恢复出厂内部存储用 `./brick reset`。

## 3. 做一张 SD 卡

```bash
./brick sd out/sd-mrg.img 8G
```

生成 MBR + 单个 FAT32 的卡镜像，跟真卡一样。QEMU 的卡模型要求大小是 2 的幂，脚本会自动向上取整。

把 MRG 装进去，两种方式：

- 管理器：`./brick manager` → **SD card** → 把 `MRG.pak` 拖进去（运行中也能拷，走的是 guest）。
- 直接写镜像，更快，但**必须先停掉 QEMU**，运行中写会被静默丢弃：

```bash
docker run --rm --user $(id -u):$(id -g) -v "$PWD":/work:z brick-emu-builder bash -ec '
  export MTOOLS_SKIP_CHECK=1
  mmd -i /work/out/sd-mrg.img@@1M ::/Apps ::/Emus 2>/dev/null || true
  mcopy -s -o -Q -i /work/out/sd-mrg.img@@1M /work/out/pak/MRG.pak ::/Apps/'
```

`brick-emu-builder` 是 `./brick setup` 建出来的构建镜像；宿主机装了 mtools 的话直接 `mcopy` 就行。
`@@1M` 是分区偏移。

想在桌面 Games 页出现入口，把仓库里的磁贴包也拷进去（否则从 Apps 里点 MRG Player 也能启动）：

```bash
mmd -i out/sd-mrg.img@@1M ::/Emus 2>/dev/null || true
mcopy -s -o -Q -i out/sd-mrg.img@@1M \
  /path/to/MRG-Player/platforms/trimui/emus/* ::/Emus/
```

## 4. 启动

```bash
./brick run out/sd-mrg.img \
  --display vnc \
  --monitor out/run/mon.sock \
  --serial-sock out/run/ser.sock \
  --log out/run/serial.log \
  --gpu soft
```

- 屏幕走 VNC：`127.0.0.1:5900`。
- 串口日志写进 `out/run/serial.log`，同时挂在 `out/run/ser.sock`。
- 一定要用 `--display vnc`（或 `gtk` / `skin`）。`--headless` 下 QEMU 会把 `sendkey` 静默丢掉，
  画面看起来是冻的，其实只是没收到键。
- `--gpu soft` 最稳；显卡正常可以试 `--gpu virgl`。
- 到桌面约 2–3 分钟（整机 TCG 翻译）。

## 5. 让 MRG 占屏

从桌面磁贴启动最稳。走命令行时 MainUI 会抢屏，要先让它停住：

```bash
pidof MainUI runtrimui.sh
kill -9 <MainUI 的 pid>
cd /mnt/SDCARD/Apps/MRG.pak
LD_LIBRARY_PATH=/usr/trimui/lib ./launch.sh > /tmp/mrg.log 2>&1 </dev/null &
```

guest 的 shell 不会展开 `$( )` 和反引号，命令要写死；pid 每次都变，先 `pidof` 再抄进去。
MRG 的数据目录在卡上：`/mnt/SDCARD/.userdata/mrg/`（`launch.sh` 导出 `MRG_DATA_DIR` 并 `mkdir -p`）。

## 6. 截图与按键

`tools/qmon.py` 是给 monitor socket 发 HMP 命令的小工具：

```bash
python3 -I tools/qmon.py out/run/mon.sock "screendump /tmp/brick.png -f png"
python3 -I tools/qmon.py out/run/mon.sock --press x 200
```

`sendkey` 的键名是 QEMU 的，小写；`Left` 这种大写会报 invalid parameter。仿真器把 PC 键盘映射成 Brick 手柄：

| sendkey | Brick | sendkey | Brick |
|---|---|---|---|
| `x` | A | `ret` | START |
| `z` | B | `backspace` / `shift` | SELECT |
| `s` | X | `esc` / `m` | MENU |
| `a` | Y | `1` / `2` | F1 / F2 |
| `q` / `w` | L1 / R1 | `up` / `down` / `left` / `right` | 十字键 |
| `e` / `r` | L2 / R2 | `p` | 电源 |

所以截「按 A」要发 `x`，截「返回」要发 `z`。

## 7. 串口

`--serial-sock` 是 guest 的 ttyAMA0。手动进：

```bash
./brick shell
```

脚本里发命令就连这个 unix socket。它是一根裸 tty，最好先发一个回车、读掉回显，再发命令；
命令里同样不要带 `$( )`。

## 8. 交叉编译 pak

掌机包用 Arm GNU 8.3 + TrimUI SDK 头文件交叉编译。工具链的来源和合并方式见
`.github/workflows/_brick.yml`：Arm GNU 8.3-2019.02 官方 tar.xz，加上 `trimui/toolchain_sdk_smartpro`
的 `SDK_usr_tg5040_a133p.tgz`；把 SDK 的 `usr/` 以「只补缺」的方式并进工具链 sysroot，
反过来覆盖会撞 `GLIBC_PRIVATE`。

```bash
cmake -S . -B build-brick -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-brick.cmake \
  -DMRG_CROSS_PREFIX=<tc>/bin/aarch64-linux-gnu- \
  -DMRG_SYSROOT=<tc>/aarch64-linux-gnu/libc \
  -DMRG_PLATFORM=trimui \
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-brick --parallel --target pak pak-verify
```

产物在 `build-brick/pak/MRG.pak`。`pak-verify` 检查 NEEDED 与 GLIBC 版本上限（2.17），本机跑不过就别往卡上拷。

## 9. 常见问题

- `permission denied` 连 Docker：加组后要重新登录，或 `newgrp docker`。
- 只有 VNC 没有窗口：缺 `python3-gi`、`python3-gi-cairo`、`gir1.2-gtk-3.0`。
- 整机像冻住：内存不够（模拟器要 ~1 GB），`free -m` 看看有没有 swap。
- 写了新 pak 但界面没变：确认 QEMU 已停；改图标还要整机重启（MainUI 有缓存）。
- 截图 sha 一样：TCG 是确定性的，不代表按键没生效；配合串口日志或换一个明显的界面动作验证。

## 10. 相关文档

- 数据目录：docs/paths.md
- 键盘/手柄映射：docs/input.md
- 原厂桌面磁贴：docs/brick-launcher-emus.md
