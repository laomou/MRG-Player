# TrimUI Brick 原厂桌面是怎么启动游戏的（MainUI → Emus → RetroArch）

> 调研时间：2025-11-26。对象：TrimUI Brick 官方固件（TinaLinux）+ 官方 SD 基础包。
> 方法：静态读固件里的 `MainUI` 字符串与 SQLite schema、从官方资源包里取 Emus 包原文、在
> **trimui-brick-emu（QEMU 模拟整机）** 里逐条实测（截图 + `/proc/<pid>/cmdline` + `pidof`）。
> 所有路径都是设备上的真实路径（`/mnt/SDCARD` 是卡）。

## 结论先行

原厂桌面**不自己模拟游戏**，它只做三件事：**发现**（扫卡上的 `Emus/` 目录）、**列出**（按包的
`config.json` 建磁贴、按 `rompath`+`extlist` 扫 ROM 并建 SQLite 缓存）、**转交**（把 ROM 路径当参数
执行该包的 `launch.sh`）。真正的模拟器是包里的 `launch.sh` 拉起的 **RetroArch**（`ra64.trimui`）+
libretro 核心。

因此替换 RetroArch 的入口只有一处：**把 `Emus/<SYS>/launch.sh` 换成别的前端**。MRG 只要有"命令行
直启某个 ROM"的能力就能整体接管（见 §7，对应 PR #26）。

---

## 1. 三层结构

```
MainUI（/usr/trimui/bin/MainUI，固件内置，不是 pak）
  └─ 扫 /mnt/SDCARD/Emus/<SYS>/config.json    ← 每个子目录 = 一个磁贴
       └─ 按 rompath + extlist 扫 ROM          ← 目录索引，写 <dir>_cacheN.db
            └─ 点 ROM → 执行 Emus/<SYS>/<launch> "<裸 ROM 路径>"
                 └─ launch.sh 里的 exec：
                      /mnt/SDCARD/RetroArch/ra64.trimui -L .../fceumm_libretro.so "<ROM>"
                        └─ libretro 核心（fceumm / snes9x / genesis_plus_gx / mgba / ...）
```

三层的职责边界很清楚：

| 层 | 拥有什么 | 不知道什么 |
|---|---|---|
| MainUI | 磁贴外观、ROM 列表、拼音搜索、最近游玩、收藏 | 完全不懂模拟器；不知道 core 是什么 |
| `Emus/<SYS>` | 系统名、ROM 目录、扩展名白名单、多个 launch 变体、CPU 调频策略 | 不知道 RetroArch 怎么用 core |
| `RetroArch` | core 加载、视频/音频/输入、存档、金手指 | 不知道磁贴长什么样 |

---

## 2. MainUI 侧：它怎么发现游戏

### 2.1 扫描哪些目录

字符串里写死三个根：

```
/mnt/SDCARD/Emus
/mnt/UDISK/Emus
/mnt/exUDISK/Emus
```

每个根目录下的**每个子目录**是一个模拟器包（一个磁贴）。相关符号：`loadEmusApps()`
（`_Z12loadEmusAppsv`）与 `clearEmusApps()`（`_Z13clearEmusAppsv`）。

### 2.2 `config.json`：磁贴的全部输入

以官方 FC 包（`Emus/FC/config.json`）为例：

```json
{
  "label": "FC",
  "icon": "",
  "icontop": "../_theme/ic-fc.png",
  "background": "../_theme/bg-fc.png",
  "themecolor": "991615",
  "launch": "launch.sh",
  "rompath": "../../Roms/FC",
  "imgpath": "../../Imgs/FC",
  "useswap": 0,
  "shortname": 0,
  "hidebios": 0,
  "extlist": "nes|fds|unf|unif|zip",
  "launchlist": [
    { "name": "FCEUmm",  "launch": "launch.sh" },
    { "name": "Nestopia", "launch": "launch_nestopia.sh" }
  ]
}
```

| 字段 | 作用 | 备注 |
|---|---|---|
| `label` | 磁贴标题 | 也决定 ROM 列表页的标题 |
| `icon` / `icontop` | 磁贴图，**两条不同的渲染路径**，见 §2.3 | 两个都填时 `icon` 生效 |
| `background` | 该系统的列表页背景图 | 缺省时用全局背景 |
| `themecolor` | 选中态环/高亮颜色 | 6 位十六进制，无 `#` |
| `launch` | 默认启动脚本 | 相对包目录 |
| `rompath` | ROM 目录 | 相对包目录，`../../Roms/XXX` |
| `imgpath` | 封面目录 | 约定文件名 = ROM 主名 + `.png` |
| `extlist` | 扩展名白名单，`\|` 分隔 | 不在表里的文件不会被列出 |
| `launchlist` | 启动方式列表（弹菜单让用户选 core） | 每项一个 `name` + `launch` |
| `useswap`/`shortname`/`hidebios` | 遗留开关 | 本仓库用的两个包里都是 0 |

### 2.3 图标的两个字段，渲染规则不同（实测）

在 1024×768、磁贴 295×295 的桌面上用内置包与自制包对照，得到两条硬规则
（磁贴中心 `(831.5, 234)`，标题行 `y=321`）：

| `config.json` | 图片规格 | 实际渲染 | 结果 |
|---|---|---|---|
| `icon` | 256×256 满铺 | 256×256，`y106..361` | 压住标题 |
| `icon` | 300 画布 / 160 图形 | 160×160，`y124..283` | 正常 |
| `icon` | 300 画布 / 200 图形 | 200×200，`y134..333` | 压住标题 |
| `icontop` | 300 画布 / 200 图形 | 160×160，`y124..283` | 与内置图标同框 ✅ |
| `icontop` | 256 满铺 | 239×233，`y90..322` | 压住标题 |
| `icon` + `icontop` | 300 / 200 | 200×200 | `icon` 优先 |

- `icon`：**按原图像素 1:1 贴**，所以图片本身必须是最终尺寸。
- `icontop`：把整张画布 `fit` 进约 240 的 icon well（300 画布 → 240，200 图形 → 160）。

内置 8 个 app（`/usr/trimui/apps/*/ic-*.png`）规格完全一致：
**300×300 RGBA 画布、可见图形 200×200、四边各留 50px 透明、圆角 31px（15.5%）**，
并且它们全部是 `"icon": ""` + `"icontop": "<file>.png"`。

> 推论：自制的 pak 图标想和内置图标一样"缩在格子里、不压标题"，必须**同时**满足
> `icontop` 字段 + 300/200/50 的几何，两者缺一不可。

### 2.4 ROM 索引缓存：`Roms/<dir>/<dir>_cacheN.db`

第一次进某个系统的列表页时，MainUI 会在 ROM 目录下建一个 SQLite 库（我们实测的是
`/mnt/SDCARD/Roms/FC/FC_cache7.db`，12 KB）。字符串证据：

```
%s/%s_cache%d.db
CREATE TABLE %s_roms (id INTEGER PRIMARY KEY AUTOINCREMENT, disp TEXT NOT NULL,
  path TEXT NOT NULL, imgpath TEXT NOT NULL, type INTEGER DEFAULT 0,
  ppath TEXT NOT NULL, pinyin TEXT NOT NULL, cpinyin TEXT NOT NULL,
  opinyin TEXT NOT NULL);
INSERT INTO %s_roms (disp, path, imgpath, type, ppath, pinyin, cpinyin, opinyin) VALUES (...)
DROP TABLE %s_roms;
SELECT COUNT(*) FROM %s_roms WHERE ppath='%s'
handleDir end newly add %d roms, total %d roms now
```

实测两行内容：

| disp | path | imgpath | type | ppath | pinyin |
|---|---|---|---|---|---|
| F1赛车 | `/mnt/SDCARD/Emus/FC/../../Roms/FC/F1赛车.nes` | `.../Imgs/FC/F1赛车.png` | 0 | `.` | `F1sc` |
| 俄罗斯方块 | `.../俄罗斯方块.nes` | `.../俄罗斯方块.png` | 0 | `.` | `elsfk` |

要点：

- `pinyin/cpinyin/opinyin` 是**搜索**用的（`pinyin like %elsfk%` 能搜到"俄罗斯方块"）；
- `imgpath` 按约定推导，图片不存在也照写（所以往 `Imgs/FC/` 放一张同名 png 就能立刻显示封面）；
- `path` 里的 `Emus/FC/../../` 是相对 `rompath` 展开后**没有规范化**的结果；
- 库名里的 `7` 是 schema 版本，版本/结构不匹配时 MainUI 会 `DROP TABLE` 重建 ——
  **删掉这个文件是安全的**，下次进列表自动重建；它只是派生缓存，真源是 `config.json` + 目录内容。

### 2.5 其它落盘文件

| 文件 | 内容 |
|---|---|
| `/mnt/SDCARD/Roms/recentlist.json` | 最近游玩 / 最近启动，条目形如 `{"label","iconpath","rompath","hostip","gametype","gamelabel"}`；实测 `type=23` 是"来自 Emus 的 ROM"，`type=3` 是内置 app |
| `/mnt/SDCARD/Roms/favourite2.json` | 收藏（未操作时不会生成） |

### 2.6 目录语义：只列"当层文件"，子目录显示成文件夹

这一点决定了"能不能一屏列出所有游戏"，实测（见 §7.2）：`rompath` 指到 `Roms` 根目录时，
列表里出现的是一个 **FC 文件夹** + 两个直接位于 `Roms` 根的游戏，也就是说：

> MainUI 的列表 = **该目录下的文件**；子目录一律渲染成文件夹条目，不会平铺展开。

想"点进去就是全部游戏"，只能把所有 ROM 放在同一个目录里。

---

## 3. Emus 包侧：`launch.sh` 是怎么把 ROM 交给 RetroArch 的

官方 FC 包目录内容（`Brick_Emus_compatibility_replacement.zip` → `Emus_legacy_for_theme/FC/`）：

```
config.json  launch.sh  launch_nestopia.sh
cpufreq.sh   cpuswitch.sh  effect.sh
```

`launch.sh` 原文（去掉注释）：

```sh
#!/bin/sh
RA_DIR=/mnt/SDCARD/RetroArch
EMU_DIR=/mnt/SDCARD/Emus/FC

cd $RA_DIR/
$EMU_DIR/cpufreq.sh
$EMU_DIR/cpuswitch.sh

HOME=$RA_DIR/ $RA_DIR/ra64.trimui -v $NET_PARAM \
  -L $RA_DIR/.retroarch/cores/fceumm_libretro.so "$*"
```

逐行含义：

| 行 | 作用 |
|---|---|
| `cd $RA_DIR/` | RetroArch 的相对配置路径都以 `/mnt/SDCARD/RetroArch` 为根 |
| `cpufreq.sh` | 写 `ondemand` + `1008000`（1.008 GHz） |
| `cpuswitch.sh` | 关掉 cpu2/cpu3，把机器压到双核省电 |
| `HOME=$RA_DIR/` | 让 RetroArch 把配置/存档写到卡上，而不是 rootfs |
| `-v` | verbose |
| `-L <core.so>` | 指定 libretro 核心 |
| `"$*` | **MainUI 传来的裸 ROM 路径** |

由此得到两个关键事实：

1. **传参形式是"裸路径"**：`launch.sh "<rom path>"`，不是 `--rom <path>`。任何想接管的前端都必须
   接受一个裸路径位置参数，否则原厂包一行都改不动就得重写全部 `launch.sh`。
2. **换核心 = 换 `launchlist` 里那一行**：`launch_nestopia.sh` 与 `launch.sh` 唯一的差别就是
   `-L .../nestopia_libretro.so`，所以"一个系统多个 core"在这套设计里是**多个脚本**，不是运行期选项。

> 副作用提醒：`cpufreq.sh`/`cpuswitch.sh` 会改 CPU governor 与在线核数，游戏退出后**不会自己恢复**，
> 由 MainUI 回到桌面时重新调度。自制前端若想接管，需要自己决定是否复用这两步。

---

## 4. RetroArch 侧：它根本不在固件里

| 位置 | 内容 |
|---|---|
| `/usr/trimui/bin/` | `MainUI`、`keymon`、`trimui_inputd`、`hardwareservice`、`runtrimui.sh` … **没有 retroarch** |
| `/usr/trimui/lib/` | SDL1.2 / SDL2 / SDL2_image,mixer,ttf / fftw3 / freeimage / mpg123 / libgamename … **没有任何 `*_libretro.so`** |
| `/mnt/SDCARD/RetroArch/` | `ra64.trimui`（9,141,584 B，aarch64）、`retroarch.cfg`（107 KB）、`.retroarch/cores/fceumm_libretro.so`（4,033,280 B） |

也就是说 **RetroArch 完全由 SD 基础包提供**（`tg3040_Brick_SD_base_package_20241105.zip`，
内含约 40 个系统包、cores 合计 ~569 MB、assets ~199 MB、system ~135 MB、shaders ~86 MB）。

这解释了一个很容易踩的坑：**空白卡开机时 Games 页是空的（显示 `None`）**，因为固件里既没有
`Emus/` 包，也没有 `RetroArch/`。MainUI 里虽然硬编码了

```
cd /usr/trimui/bin/; HOME=%s /usr/trimui/bin/retroarch -C %s -L /usr/trimui/bin/fceumm_libretro.so "%s"
```

以及 `fbalpha2012_libretro.so`、`fbalpha2012_neogeo_libretro.so` 这几条内置路径，但对应的文件在
整个 rootfs 里都不存在 —— 那是给"出厂预装卡"准备的遗 code path。

官方资源（两者内层都是 **zstd**，`unzip` 会失败，必须 `7z x`）：
<https://github.com/trimui/assets_brick/releases/tag/20241105>

- `Brick_Emus_compatibility_replacement.zip`（363,082,272 B）→ Emus 包三套主题
- `tg3040_Brick_SD_base_package_20241105.zip`（610,820,082 B）→ RetroArch + cores + assets

---

## 5. 完整调用链（实测）

以原厂 FC 包 + fceumm 为例：

1. 开机 → MainUI 扫 `/mnt/SDCARD/Emus`，找到 `Emus/FC/`，按 `icontop` 画磁贴；
2. Games 页按 A → 进 FC 列表，首次进入建 `Roms/FC/FC_cache7.db`；
3. 选中 `俄罗斯方块` 按 A → MainUI `fork/exec`：
   `/mnt/SDCARD/Emus/FC/launch.sh /mnt/SDCARD/Emus/FC/../../Roms/FC/俄罗斯方块.nes`
4. `launch.sh` 调频后 exec `ra64.trimui -L fceumm_libretro.so "<ROM>"`；
5. 退出 RetroArch → `launch.sh` 结束 → MainUI 拿回屏幕。

模拟器里的验证方式（QEMU monitor + guest 串口）：

```sh
# 截图
python3 -I tools/qmon.py out/run/mon.sock "screendump /tmp/x.png -f png"
# 按键（x = Brick 的 A，z = B）
python3 -I tools/qmon.py out/run/mon.sock "sendkey x 600"
# 看它到底 exec 了什么
pidof ra64.trimui
tr "\0" " " < /proc/$(pidof ra64.trimui)/cmdline
```

---

## 6. 一个系统一个包，还是一个包列全部？

这是产品形态问题，实测结论（模拟器，`rompath` 分别指向 `Roms/FC` 与 `Roms`）：

| 方案 | Games 页第一层 | 第二层 | 评价 |
|---|---|---|---|
| 每系统一个包（官方做法） | NES / SNES / Genesis / GBA … | 该系统下的游戏 | 与官方一致，多一次点击 |
| 单包 `rompath=../../Roms` | 只有一个磁贴 | 分系统的**文件夹** + 根目录下散落的 ROM | 不满足"一屏列全部" |
| 单包 + ROM 全部平铺在一个目录 | 只有一个磁贴 | **全部游戏平铺** | 唯一能做到"点进去就是全部游戏"的形态，代价是放弃按系统分目录 |

---

## 7. 替换成 MRG：最小改动

### 7.1 每系统一个包，只改 `launch.sh`

原厂 Games 页第一级必然是机种（一个 `Emus/<SYS>` = 一个磁贴），所以照官方命名给每个系统做一个包：
`Emus/{FC,SFC,MD,GB,GBA}/`。`config.json` 原样抄官方包（`label`/`rompath`/`extlist`/`themecolor`/`hidebios`），
只把 `launchlist` 收成一条、`launch.sh` 换成四行转发：

```json
{ "label": "FC", "icon": "", "icontop": "icon.png",
  "themecolor": "991615", "launch": "launch.sh",
  "rompath": "../../Roms/FC", "imgpath": "../../Imgs/FC",
  "extlist": "nes|fds|unf|unif|zip",
  "launchlist": [{ "name": "MRG Player", "launch": "launch.sh" }] }
```

```sh
#!/bin/sh
SDCARD=${SDCARD_PATH:-/mnt/SDCARD}
exec "$SDCARD/Apps/MRG.pak/launch.sh" "$*"
```

配合 PR #26（`mrg-player <裸路径>` 直启、玩完即退出、与库内共用 content-hash 存档身份），
就完全不需要 RetroArch：实测 `pidof ra64.trimui` 为空、`pidof mrg-player` 有值，
`cmdline` 为 `./mrg-player /mnt/SDCARD/Emus/FC/../../Roms/FC/俄罗斯方块.nes`。

仓库里这套包放在 `extras/emus/`（PR #28），装机就是把目录拷到卡上：

```bash
cp -r extras/emus/* /mnt/SDCARD/Emus/
```

磁贴图默认用我们自绘的 300×300（可见图形 200、圆角 31，与内置 app 图标同规格，走 `icontop`）。
想要 1:1 的官方观感，就把官方包的 `_theme/` 拷过来、把 `icontop` 换成
`"icon": "../_theme/poster-fc.png"`（官方美术是 TrimUI 的资产，不进本仓库）。

### 7.2 为什么不是"一个包列全部"

单包（`rompath=../../Roms`）实测只能列出**当层文件**，子目录会显示成文件夹条目（§2.6），
想一屏列全部就得把所有 ROM 混在同一个目录；而且第一级磁贴名会变成包名 `MRG Player`，
与官方的机种命名不一致。所以放弃单包，采用 7.1 的每系统一包。

### 7.3 实测记录（2025-11-26，1024×768，`--gpu soft`）

| 步骤 | 结果 |
|---|---|
| 单包 `Emus/MRG/`（`rompath=../../Roms`） | Games 页 1 个磁贴 `MRG Player`；按 A 列出 `FC` 文件夹 + 两个直接放在 `Roms` 根的 ROM |
| 五个包 `Emus/{FC,SFC,MD,GB,GBA}` | Games 页 5 个磁贴 FC / GB / GBA / MD / SFC，FC 带 `2 Games` 计数 |
| 从 FC 磁贴按 A | 列出 `俄罗斯方块` / `F1赛车`（同目录的 `README.md` 被 `extlist` 过滤掉） |
| 选中游戏按 A | MRG 直接进游戏，`pidof mrg-player` 有值、`pidof ra64.trimui` 为空 |
| 游戏内 Esc → RETURN HOME | MRG 退出、回 MainUI 列表，`pidof mrg-player` 为空 |
| 官方整套 35 个包全装 | 6 页磁贴，但只有 FC/SFC/MD/GB/GBA 能真的启动（其余无 core），故最终只发 5 个 |

---

## 8. 复现这套环境时的坑（都踩过）

| 坑 | 现象 | 处理 |
|---|---|---|
| 官方 zip 内层是 zstd | `unzip` 退出码 81 | 用 `7z x` |
| 空腹卡没有 `Emus/` | Games 页显示 `None` | 从基础包拷 `Emus/<SYS>` + `RetroArch/` |
| mtools 写 FAT 分区 | `mcopy -i img` 报 `init :: non DOS media` | 必须带偏移：`-i img@@1M` |
| 图标改了不生效 | 重启 MainUI 也没用 | MainUI 会缓存解码后的图标，**要整机重启** |
| `pgrep -c qemu-system-aarch64` | 永远返回 0 | 进程名超 15 字符被截断，用 `pgrep -f` |
| `pkill -f qemu-system-aarch64` | 把自己那条 bash 一起杀了 | 模式里加字符类，如 `qemu-system-[a]arch64` |
| 从 guest shell 直接跑 `launch.sh` | MRG 与 MainUI 抢 scanout（画面闪烁/被覆盖） | 必须从 MainUI 磁贴启动，MainUI 会主动让出屏幕 |
