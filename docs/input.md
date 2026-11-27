# 键盘与手柄的输入路径

这份文档记录**键盘到底怎么接进来的**。键盘输入这块被来回改过好几次（拆分、迁移、脏 settings.ini），
每次坏掉的表现都一样："我按 X 没反应"。改之前先读这里，别再从头猜一遍。

## 1. 三条不要再改的结论

1. **键盘只驱动 port 1，port 2 只认手柄。** 两个人挤一块键盘必然抢键（P1 按住的键 P2 也要用），
   所以键盘是单人设备；"键盘 1P + 手柄 2P" 和 "双手柄" 才是支持的组合。
2. **只有一张键盘表。** `InputBindings::keyboard` 是 P1，没有 `keyboardPlayer2`，也没有 `key2_*` 配置项。
   设置页把 **Editing bindings for** 切到 Player 2 时，键列根本不画；在那页按键盘不会结束捕获（键被吞），它在等手柄按钮。
3. **菜单和游戏内是两个翻译层。** 菜单看 `launcherMenuKey()`，游戏内看 `LibretroRuntime::handleEvent()`。
   改一边不会影响另一边，所以"书架里能用、游戏里不能用"是正常现象，别急着合并两套逻辑。

## 2. 默认绑定

| 控制台动作 | 键 | 说明 |
|---|---|---|
| A | `X` | 传统 RetroArch 配对 |
| B | `Z` | |
| Select | `Right Shift` | |
| Start | `Return` | |
| 十字 | 方向键 | |
| X | `S` | 上半区，SNES/MD 才有 |
| Y | `A` | |
| L / R | `Q` / `W` | |
| Turbo A / B | `C` / `V` | MRG 自己的动作，不是主机键 |

表格的**唯一来源**是 `src/config/InputBindings.cpp:25-31` 的 `defaultInputBindings()`。
不在文档里另抄一份"设定值"，这里只解释形状。

游戏内还额外有这些不走绑定表的键：`Esc` 暂停、`Tab` 快进、`Left Shift` 慢放、
`Backspace` 回退、`.` 逐帧、`M` 静音、`F12` 截图。

配置文件里写的是 `key_<action>=<SDL keycode>`（`key_a`、`key_b`、… `key_turbo_a`、
`key_analog_deadzone` 等），位于 `settings.ini`。**旧版本残留的 `key2_*` 行会被静默忽略**——
它们是 2P 键盘方案的遗物，现在没有对应字段了。
复位默认：**设置 → 手柄 → 按 R**（RESET DEFAULT BINDINGS）。

## 3. 两条路径

### 菜单（书架、设置、管理抽屉、暂停菜单外壳）

`src/app/Application.cpp` 的 `SDL_KEYDOWN` 分支先过一次翻译：

```cpp
const SDL_Keycode key = config::launcherMenuKey(settings.input, event.key.keysym.sym);
```

`launcherMenuKey()`（`src/config/InputBindings.cpp:48`）只查 P1 的键盘表，映射规则：
A 或 Start → `Return`；B → `Escape`；X 或 Y → `x`；十字 → 方向键；其余原样。
所以书架上 `S` 和 `X` 都能开抽屉、`Z` 等同于 Esc、`X`（面键 A）等同于 Enter。
**只查 P1 的表**，所以 P2 的键不会驱动菜单。

### 游戏内

`src/app/Application.cpp:2116` 把事件原样交给 runtime：
`activeRuntime->handleEvent(event);`

`LibretroRuntime::handleEvent()`（`src/runtime/libretro/LibretroRuntime.cpp:424-440`）只查
`config::keyboardBindings(bindings_, 0)`，命中就 `setAction(0, action, pressed)`；
`joypadMask()` 再用 `kActionButtons`（同文件 :73-80）把动作位翻成
`RETRO_DEVICE_ID_JOYPAD_*` 交给核心。port 1 的键盘表是 `kNoKeys{}`，永远是 `SDLK_UNKNOWN`。

## 4. 它为什么反复坏（读这段再动手）

- **教训 1：别再往 settings.ini 里换字段名。** 有一版把键盘拆给两个玩家（P1 挪到 F/G/Q/E，P2 用 WASD），
  真机上的 `settings.ini` 被写成 `key_x=102`（F）、`key_y=103`（G）……
  于是用户按 `X` 什么都不发生——看起来像"键盘不生效"，其实是绑定不是那套。
  迁移函数收拾不干净，PR #51 已经把它们整套删掉；如果再引入新字段，同样会留下读不回来的老配置。
- **教训 2：`setInputBindings()` 会清空已送输入。** 它内部调 `releaseInput()`，把 `inputMask_`
  和 `axes_` 一起清掉。所以运行时测试必须**先装绑定、再送事件**，否则轴的断言会因为状态被清空而失败
  （曾经表现为 "L2/R2 did not reach the core joypad mask"）。
- **教训 3：别拿 NES《俄罗斯方块》验证 A 键。** 那个 ROM 转块走 B，A 按下去没反应。
  曾经据此误判成 "A 没送到核心"，实际日志里 `joypadMask()` 已经给出 `0x100`
  （`RETRO_DEVICE_ID_JOYPAD_A`）。验证用 `←` 位移或 `z` 转块。
- **教训 4：同一时序下截图字节相同是正常的。** 仿真是确定的，sha 相同不代表画面卡死，
  要判断输入是否生效，得看**同一局内**按下前后的差。

## 5. 怎么验证

**单测（最快的守门员）**：`tests/LibretroRuntimeTest.cpp` 装好默认绑定后，把 12 个 P1 键全部按住，
再从核心的电池存档里读回 `saveRam[8]`（stub core 写的 joypad mask），断言整张 mask 都在。

```sh
cmake --build build -j 10
ctest --test-dir build
```

**桌面端到端（没有真机也能跑）**：Xvfb + XTEST 注入真实按键。

```sh
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 XDG_DATA_HOME=/tmp/mrgtest2 ./build/mrg-player
# 另一个终端里用 libXtst 的 XTestFakeKeyEvent 发按键；没有 xdotool 也能用 ctypes 写十几行
```

要点：
- `XDG_DATA_HOME` 指向的数据目录里**必须有 `cores/`**，否则游戏起不来，`F12` 也不会有截图目录。
- `F12` 落在 `<数据根>/screenshots/mrg-<时间戳>.png`，比较"按键前/按键后"两张即可。
- 需要临时追踪时，在 `joypadMask()` 里打印 mask 变化。注意 port 1 恒为 0，日志会出现 `0 ↔ mask`
  交替，那是两个 port 共用同一个函数级静态量造成的假象，不是输入抖动。

## 6. 排查清单（"我按了没反应"）

1. 是不是在 **设置 → 手柄 → Player 2** 页？那页不认键盘。
2. `settings.ini` 里的 `key_*` 是不是你以为的那套？按 **R** 复位一次最快。
3. 换了 oracle 吗？有些 ROM 本来就不用那个键。
4. 在菜单还是游戏内？菜单键名会被 `launcherMenuKey()` 改写。
5. 手柄插着没有？PROMPT DEVICE 只影响**显示**哪个设备的键名，不影响功能。
