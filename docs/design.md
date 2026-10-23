# 设计说明

## 1. 目标与约束

- 语言/工具链只用 `C:\MinGW`（GCC 6.3、gfortran、windres、Tcl/Tk 8.6.0）；
- 原生 Windows PE，静态链接，**一个构建脚本、一个 exe、0 个外部依赖**；
- C 做骨架；计算重的定长循环用 Fortran，或 GLSL compute（默认 GPU）；
- 热逻辑在 Lua；
- 相空间/双摆/控制面板是三个独立窗口，不是同一个窗口里的三个区域；
- 保持小而直接，不引入场景图、脚本绑定生成器或额外框架。

## 2. 窗口与模块

```text
main.c
 └─ App
     ├─ Platform        相空间顶层窗口、DPI、输入、RCDATA
     ├─ PhaseView       视图变换、pick、平移/缩放、格线/圆圈
     ├─ Gpu             场 SSBO/texture、display/line shader、读回
     ├─ PendulumView    双摆 GDI 顶层窗口
     ├─ Panel           Tcl/Tk 顶层窗口
     │   └─ TkRuntime   DPTCL86 解包 + 动态加载
     ├─ Sim             每步推进、选择读取
     │   ├─ Pool        固定线程池
     │   └─ kernel_fortran
     └─ LuaHot          配置/调色板/按键/tick/status
```

三个窗口都由同一个 UI 线程创建和泵消息。相空间窗口用 WGL/OpenGL 4.3 渲染；
双摆窗口用 GDI 双缓冲；面板用 Tk 自己的窗口和事件循环，通过 `Tcl_DoOneEvent` 接入主循环。

默认布局（1920×1080、125% 缩放）：

| 窗口 | 外框 | 客户区 | 位置 |
| --- | --- | --- | --- |
| Phase Space | 706×735 | 700×700 | (19,48) |
| Double Pendulum | 370×328 | 364×294 | (740,16) |
| Control Panel | 382×587 | 364×540 | (740,355) |

相空间边长按工作区的 0.68 取并 clamp 到 420–740，保证客户区正方形；右列宽度取相空间的
0.52 并 clamp 到 300–380。面板高度按剩余工作区 clamp 到 420–540。

## 3. 数据流

每帧：

1. `platform_pump` 处理三个 Win32 窗口的消息；`panel_pump` 让 Tk 处理定时器/空闲；
2. 若未暂停，按 `dt` 累计推进；GPU 路径调 `gpu_step`，CPU 路径调 `sim_step`；
3. 读回选中格（GPU 用 `glReadPixels` 单像素；CPU 直接 `memcpy` 4 个 double）；
4. `pendulum_view_set_state` 更新双摆窗口；`app_render_phase` 绘制场 + 叠加层；
5. 更新相空间标题（内核/FPS/分辨率/调色板）；面板每 250 ms 通过 `dp_status` 拉状态。

动作入口只有一个：`app_do_action(name, value)`。键盘、面板按钮、组合框都走它，
状态/错误统一写到 `last_action`，面板的 `Last:` 显示。

## 4. 物理内核

方程与原始单文件一致：双摆的 θ1/θ2/ω1/ω2，RK4 固定步长，阻尼项 `-damping*omega`。
每个像素是一个独立初值：

```text
theta1 = -range/2 + (x+0.5)/res*range
theta2 = -range/2 + (y+0.5)/res*range
omega1 = omega2 = 0
```

“场值”是经过 `max_steps` 步后的一个标量（角速度组合 + 归一化），再经 LUT 映射颜色。
C 参考实现 `dp_field_value` 用于自检和 CPU 回退的基准；Fortran/GPU 只追求与它同阶，
不要求逐位一致。

## 5. GLSL compute（默认）

- GL 4.3 core，WGL 上下文；`field.comp` 用 16×16 工作组，SSBO 读状态、写 R32F 图像；
- 每个工作组先把参数/常量放到 shared memory，再按像素做四段 RK4；
- 显示：全屏三角形 + `display.frag`，从 R32F 场采样，再查 1024×1 的调色板纹理；
- 叠加层：`line.vert/frag` 画 NDC 顶点（位置 + RGBA），每个顶点 6 个 float；
- `gpu_read_cell` 用 `glReadPixels` 读一个像素；`gpu_field_filter` 暴露过滤模式供自检。

GPU 只负责“算场”和“画场”；视图的平移/缩放/周期/格线在 shader uniform 和
`phase_view.c` 里做，不改变场的存储。

## 6. Fortran 行带内核（回退）

`fortran/dp_kernel.f90` 导出 `dp_fortran_bands`：每个线程处理若干行，行内像素连续。
RK4 的 `deriv` 内联；sin/cos 用 1024 项查表 + 角度差恒等式；固定 4 段展开；
`-finline-limit=4000`、`-O3`。C 侧 `pool.c` 用固定线程池按行带分发，每个 worker
有自己的 `go`/`done` 事件，主线程等全部完成。

选择 Fortran 回退的触发条件：GPU 创建/编译失败、用户按 K/面板选 `Fortran CPU`、
或 `/cpu` 命令行。两条内核都写同一套状态（CPU 的 double 数组 / GPU 的 SSBO），
上层不区分。

## 7. Lua 热逻辑

`lua/hot.lua` 是唯一的“策略”来源：配置（res/range/dt/max_steps/threads/阻尼/质量/长度）、
调色板、按键、每步推进函数。C 通过 `lua_hot_*` 调用，Lua 沙箱只开 base/table/string/math。

契约（新增）：

- `M.status() -> paused, scheme`：面板状态；
- `M.set_scheme(name) -> bool`：面板下拉直接设置调色板；
- `M.key(name) -> action`：R/Space/C/K 的语义；
- `M.tick(dt) -> steps`：累计时间并返回本帧应推进的步数；
- `M.params()`、`M.palette()`、`M.title(...)` 保持原契约。

加载顺序：exe 内嵌 RCDATA 1001 → 若 exe 同目录有 `hot.lua` 则覆盖。F5/面板 Reload
重新读磁盘并替换状态；失败时保留旧 `LuaHot`，把错误写到 `last_action`。改 res/range
会调用 `app_reconfig` 重建仿真；改 dt/max_steps/params 在下一帧生效；改 scheme 重新读 LUT。

## 8. 相空间视图

`PhaseView` 只存 `cx, cy, zoom, grid, periodic, circle`，不持有 GL 资源。

屏幕 → 世界：

```text
span = range / zoom
fx = (sx + 0.5) / w - 0.5
fy = (sy + 0.5) / h - 0.5
wx = cx + fx * span
wy = cy - fy * span
```

pick：

```text
cell = range / res
ix = floor((wx - (-range/2)) / cell)
iy = floor((wy - (-range/2)) / cell)
periodic: ix = mod(ix, res), iy = mod(iy, res)
否则 ix/iy 越界 -> 失败
```

滚轮以光标为中心：先取光标下的世界点 `(wx, wy)`，改 `zoom`，再反解
`cx = wx - fx*span'`、`cy = wy + fy*span'`。左键拖动按像素位移改 `cx/cy`。
视图被 clamp 到 `[-range, range]` 附近，避免完全丢出边界。

叠加层顶点在 `phase_view.c` 生成，NDC + RGBA：

- 网格步长自适应，保证屏幕间距 ≥ 8 px；每 `2π` 一条主格线（相对 0）；
- 基础范围边框；
- 选中圆圈：单元格中心，半径随 `cell_px` 变化但至少 6 px；周期视图下对相邻 tile 复制；
- `grid=0`、`circle=0` 时不生成对应顶点，面板可关掉以看完整图像。

显示 shader 的 uniform：`uCenter`、`uSpan`、`uRangeMin`、`uRange`、`uPeriodic`。
uv 先按视图变换，再按场范围归一化；periodic 用 `fract`，非周期越界画背景色。
场纹理 **NEAREST**，LUT 线性。

### 8.1 低分辨率模糊的根因

历史 bug：`gpu_set_res` 把场纹理的 `GL_TEXTURE_MIN_FILTER`/`MAG_FILTER` 都设成
`GL_LINEAR`。64×64 放大到 700×700 时，一个纹素覆盖约 11×11 屏幕像素，双线性过滤在
纹素边界附近混入邻居，看起来像糊的渐变；网格线还在，所以容易误判成网格/缩放问题。

修复：场纹理（现代 R32F 与 legacy RGBA）统一 `GL_NEAREST`；LUT 保留 `GL_LINEAR`。
LUT 的插值只影响同一格内的颜色值，而同一格只有一个场值，因此不会产生空间模糊。
legacy 路径同样设为 NEAREST，64×64 的固定管线截图也保持方块边界。

## 9. Tcl/Tk 控制面板与内嵌运行时

### 9.1 面板

`ui/panel.tcl`（RCDATA 1002）用 ttk/clam 画一个 364×540 的固定布局：标题、Pause/Reset/Quit、
Kernel/Palette 下拉、Grid/Periodic/Circle、Reset view、Hot logic（Source/Reload）、状态三行、
快捷键两行、Last。所有控件 `-takefocus 0`，动作后 `focus .` 把焦点还给面板根窗口；
`bind . <KeyPress-...>` 提供与相空间相同的快捷键。

C 注册三个 Tcl 命令：

- `dp_action name ?value?` → `app_do_action`，返回 `last_action`；
- `dp_status` → 返回偶数长度列表：paused/kernel/kernel_label/fps/res/scheme/grid/periodic/circle/
  zoom/sel/source/last；
- `dp_quit` → `app->quit=1` + `PostQuitMessage`。

`panel_pump` 每帧最多处理 64 个 Tk 事件（`TCL_DONT_WAIT`），不阻塞 GL 循环。
面板关闭（标题栏 ×）走 `WM_DELETE_WINDOW {dp_quit}`；Quit 按钮同路径。

### 9.2 运行时打包

工具链的 `libtcl86.a`/`libtk86.a` 是导入库，真正代码在 `C:\MinGW\bin\tcl86.dll`、
`tk86.dll`，脚本在 `C:\MinGW\lib\tcl8.6`、`tk8.6`。为了不产生 DLL 依赖：

```text
DPTCL86\0            8 B magic
u32 count
repeat:
  u16 path_len
  path              相对路径，如 tcl8.6/init.tcl
  u32 data_len
  data
```

`build.ps1` 把 2 个 DLL + 979 个脚本文件（共 981）打成 `build/gen/tcltk.bin`，
windres 以 RCDATA 1003 嵌进 exe。运行时：

1. 先试 `%LOCALAPPDATA%\dp-phase\tcl86-8.6.0`；不可写则试
   `%TEMP%\dp-phase-tcl86-8.6.0`；已有完整 key files 就跳过解包；
2. 解包时校验 magic、路径（拒绝绝对路径/..）、逐文件建父目录；
3. `SetEnvironmentVariable` + `_putenv` 设 `TCL_LIBRARY`/`TK_LIBRARY`
   （MinGW 的 `getenv` 只认 CRT 环境，必须 `_putenv`）；
4. `LoadLibraryEx(..., LOAD_WITH_ALTERED_SEARCH_PATH)` 加载 tcl86.dll/tk86.dll，
   `GetProcAddress` 取 `Tcl_*`/`Tk_Init`，`Tcl_CreateInterp` → `Tcl_Init` → `Tk_Init`；
5. 失败才回退 `C:\MinGW`（开发机兜底）。

因此 exe 没有 tcl/tk 的导入项，`dist` 只有一个文件；解包目录是缓存，不是外部安装。

## 10. 帧流水线与输入

```text
while !quit:
    platform_pump
    panel_pump
    app_frame:
        params = hot.params()
        steps = hot.tick(dt)
        sim_step/gpu_step
        read selected cell
        pendulum_view_set_state
        app_render_phase
        update title
    vsync or 60Hz limiter
```

相空间输入：左键 down/up = 拖动，move = 平移，wheel = 缩放，右键 down = pick；
键盘 R/Space/C/K 先走 `LuaHot.key`，G/P/O/0/F5/Esc 由 C 处理。面板动作直接调用
`app_do_action`，不模拟按键。双摆窗口只转发键给 `app_key`。

## 11. 回退矩阵

| GPU 4.3 | 用户选择 | 结果 |
| --- | --- | --- |
| 可用 | GPU | compute + R32F（默认） |
| 可用 | CPU | 固定管线 + CPU 场 |
| 不可用 | — | Fortran CPU + 固定管线 |
| 不可用 | — | C 参考 RK4（自检/极端兜底） |

Lua 加载失败 → 使用编译期默认配置/调色板；Lua 重载失败 → 保留旧状态。
Tk 运行时/脚本失败 → 只影响面板，相空间和双摆继续运行（错误写 `last_action`/调试输出）。

## 12. 构建与资源

`build.ps1` 六步：打包 Tcl/Tk → 编译 shader → 编译 C（15 个对象）→ 编译 Fortran →
windres（hot.lua 1001、panel.tcl 1002、tcltk.bin 1003、manifest、VERSIONINFO）→
静态链接（`-static -static-libgcc`，gfortran 运行库静态）→ 导入表审计 → 可选自测。

## 13. 与原始单文件的差异

| 方面 | 原始 | 现在 |
| --- | --- | --- |
| 窗口 | 一个窗口、右侧信息面板 | 相空间 / 双摆 / 控制面板三个顶层窗口 |
| 信息面板 | C/raylib 画 | Tcl/Tk 面板 + 独立双摆 GDI 窗口 |
| 相空间 | 固定视图 | 拖拽/缩放/周期/格线/选中圆圈 |
| 过滤 | 双线性 | 场纹理 NEAREST，低分辨率锐利 |
| 面板输入 | 键盘 | 面板控件覆盖全部功能，键盘只作快捷键 |
| Tcl/Tk | 无 | 内嵌运行时 + 动态加载，仍单 exe |
| 选中 | 无 | 右键像素 → 双摆窗口同步 |

## 14. 已知取舍

- 面板第一次启动要解包约 7.8 MB；之后走缓存。
- 面板高度按工作区 clamp；很矮的屏幕需要拉大窗口。
- GPU/Fortran 舍入不同，极端像素不保证逐位一致。
- legacy 固定管线功能完整，但没有现代路径的视图 uniform 性能。
- 没有实现滚动/多显示器面板停靠；三个窗口是独立顶层窗口。
- exe 未签名。
