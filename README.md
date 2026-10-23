# 双摆相空间 · 三窗口版

把原来的单文件程序重构成三个独立的原生 Windows 窗口：

- **Phase Space**：相空间场、格线、选中圆圈、拖拽平移、滚轮缩放；
- **Double Pendulum**：选中像素对应的双摆和 θ/ω/阻尼信息；
- **Control Panel**：Tcl/Tk 控制面板，所有操作都有控件，键盘只是快捷键。

计算仍然是 **C 骨架 + GLSL compute（默认）+ Fortran RK4（回退）**，热逻辑仍然在 **Lua** 里。
交付物仍然是一个静态链接的 exe；Tcl/Tk 运行时也编在 exe 里，首次启动解包到用户缓存目录。

![三个窗口](docs/shot-windows.png)

## 快速开始

```powershell
cd pendulum-phase
pwsh -File build.ps1
.\dist\dp-phase.exe
```

构建需要 `C:\MinGW` 里的 32 位 GCC 6.3、gfortran、windres，以及工具链自带的
Tcl/Tk 8.6.0（构建时打包，运行时不需要工具链）。默认构建会跑 `/selftest` 和
`/gpu-selftest`；只编译用 `pwsh -File build.ps1 -NoTest`。

## 交付物

| 项目 | 位置 | 说明 |
| --- | --- | --- |
| 二进制 | [dist/dp-phase.exe](dist/dp-phase.exe) | PE32 GUI，8,929,280 B，静态链接，导入表只有 6 个系统 DLL |
| 构建脚本 | [build.ps1](build.ps1) | 唯一入口：打包 Tcl/Tk、编译 C/Fortran、嵌入资源、链接、审计、自测 |
| 源码 | [src/](src/) · [fortran/](fortran/) · [shaders/](shaders/) · [lua/](lua/) · [ui/](ui/) | 模块见下文 |
| 文档 | [docs/design.md](docs/design.md) · [docs/verification.md](docs/verification.md) | 设计、取舍和实测记录 |

导入表（没有 tcl86.dll/tk86.dll 的链接期导入）：

```
KERNEL32.dll  USER32.dll  GDI32.dll  OPENGL32.dll  WINMM.dll  msvcrt.dll
```

## 低分辨率为什么糊，怎么修的

根因不是网格、也不是相位范围，而是**相空间纹理的放大过滤**：`GL_TEXTURE_MAG_FILTER`
原来是 `GL_LINEAR`。64×64 的场放大到 700×700 时，每个纹素会与邻居做双线性混合，
格子边界被抹成渐变。

修复在 [src/gpu.c](src/gpu.c) 的 `gpu_set_res`：场纹理统一 `GL_NEAREST`（现代路径和
legacy 路径都改）；LUT 仍保留线性，因为一个格子只有一种场值，LUT 插值不会造成空间模糊。

| 修复前：64×64 + GL_LINEAR | 修复后：64×64 + GL_NEAREST |
| --- | --- |
| ![模糊](docs/blur-before.png) | ![锐利](docs/blur-after.png) |

现在 64×64 下每个像素有清晰的方块边界；[docs/shot-phase.png](docs/shot-phase.png) 是
带格线和选中圆圈的实拍。

## 热重载怎么操作

热逻辑默认是 exe 里的 `lua/hot.lua`（RCDATA 1001）。要覆盖它：

1. 把 `hot.lua` 放到 `dp-phase.exe` **旁边**；
2. 看面板的 `Hot logic (Lua)`：
   - `Source: embedded (RCDATA)`：正在用 exe 内嵌脚本；
   - `Source: hot.lua (disk override)`：正在用 exe 旁边的脚本；
3. 点 `Reload hot.lua (F5)`（或按 F5）。面板的 `Last:` 会显示结果，标题会反映新的
   分辨率/调色板。

改 `params`、`dt`、`max_steps` 下一次 tick 就生效；改 `res`/`range` 会重建仿真；
改 `scheme` 立即换调色板。按 **R** 重置初值。加载失败时旧 Lua 状态保留，`Last:` 显示错误。

![带磁盘 hot.lua 的面板](docs/shot-panel-hot.png)

## 控制面板

面板里每个键盘功能都有对应控件；键盘只是快捷方式：

| 功能 | 面板控件 | 快捷键 |
| --- | --- | --- |
| 暂停/继续 | `Pause` / `Resume` | Space |
| 重置初值 | `Reset` | R |
| 调色板 | `Palette` 下拉 | C（循环） |
| 计算内核 | `Kernel` 下拉 | K |
| 格线 | `Grid` | G |
| 循环边界视图 | `Periodic` | P |
| 选中圆圈 | `Circle` | O |
| 重置视图 | `Reset view` | 0 |
| 热重载 | `Reload hot.lua (F5)` | F5 |
| 退出 | `Quit` / 标题栏 × | Esc |

面板状态行显示内核、FPS、分辨率、选中单元格、缩放和调色板；`Last:` 显示最近一次动作或错误。
面板可调整大小，默认按工作区高度 clamp（1080p 下客户区 364×540）。

![控制面板](docs/shot-panel.png)

## 相空间操作

- **左键拖动**：平移视图；
- **滚轮**：以光标为中心缩放；
- **右键**：选中该像素对应的双摆，黄色圆圈标出；
- **Grid**：灰色格线、2π 主格线和边框；
- **Periodic**：按周期平铺相空间，缩放/平移出基础范围时不出现黑边；
- **Circle**：关闭选中圆圈，方便看完整图像；
- **Reset view**：回到中心、1× 缩放。

![64×64 格线与选中](docs/shot-phase.png)

循环边界视图（缩小到 0.76×，可以看到平铺）：

![循环边界](docs/shot-periodic.png)

## 双摆窗口

显示选中单元格的 `theta1/theta2`、`omega1/omega2`、阻尼和坐标；随仿真实时更新。
没有选中时提示右键选像素。

![双摆窗口](docs/shot-pendulum.png)

## 结构

```text
src/main.c            入口、选项、/selftest、/capture、/shot
src/app.c             三窗口编排、输入、动作、选中同步、面板回调
src/platform.c        Win32 相空间窗口、DPI、事件、资源
src/phase_view.c      视图变换、pick、平移/缩放、格线/圆圈顶点
src/pendulum_view.c   双摆 GDI 窗口
src/panel.c           Tcl/Tk 面板宿主
src/tk_runtime.c      DPTCL86 归档解包 + 动态加载
ui/panel.tcl          面板 UI（RCDATA 1002）
lua/hot.lua           热逻辑（RCDATA 1001）
src/physics.c         C 参考 RK4、场值、调色板、哈希
src/sim.c             仿真状态、逐步
src/pool.c            固定线程池
src/kernel_fortran.c  Fortran 行带 RK4 内核
src/lua_hot.c         Lua 沙箱、热重载、status/set_scheme
src/gpu.c             GL 4.3 场、线叠加、视图、读回
src/selftest.c        CPU/GPU 自检
shaders/field.comp    SSBO → R32F 场
shaders/display.*     场 + 视图 + LUT
shaders/line.*        格线/圆圈
build.ps1             唯一构建脚本
```

## 构建流程

[build.ps1](build.ps1) 六步：

1. 把 `tcl86.dll`、`tk86.dll`、`tcl8.6/`、`tk8.6/` 打成 `build/gen/tcltk.bin`
   （981 个文件，约 7.8 MB）；
2. 编译内嵌 shader；
3. 编译 C 骨架（15 个对象）；
4. 编译 Fortran 内核；
5. 编译资源（hot.lua 1001、panel.tcl 1002、运行时 1003、manifest、版本）；
6. 静态链接并审计导入表。

## Tcl/Tk 运行时

为满足“一个 exe、0 个外部依赖”，Tcl/Tk 不是链接进来的：

- 构建时把 DLL 和脚本树放入 exe 的 RCDATA 1003；
- 首次启动解包到 `%LOCALAPPDATA%\dp-phase\tcl86-8.6.0`；受限环境回退到
  `%TEMP%\dp-phase-tcl86-8.6.0`；
- 用 `LoadLibraryEx(..., LOAD_WITH_ALTERED_SEARCH_PATH)` + `GetProcAddress` 调用，
  exe 不产生 Tcl/Tk 导入项；
- 脚本库路径通过 `SetEnvironmentVariable` + `_putenv` 同步给 Tcl 的 `getenv`（只设
  Win32 环境变量不够，MinGW 的 `getenv` 看不到）。

`dist` 里只有一个 exe；解包目录是运行缓存，不是随包分发的外部依赖。本次验证把
`MINGW_ROOT` 指向不存在的目录、清掉 TEMP 缓存后启动，程序仍从 exe 解出 981 个文件并显示面板。

## 验证摘要

最近一次构建：

- `/selftest`：**53/53**（视图 pick/缩放/平移、周期 pick、叠加层、Lua status/set_scheme、BMP 等）；
- `/gpu-selftest`：**59/59**（含 `GL_NEAREST` 断言）；
- 256² CPU：约 7–8 ms/step，8.5–9.5 Mcell-steps/s（8 线程）；
- 256² GPU：约 0.14–0.17 ms/step，400–470 Mcell-steps/s；
- 三窗口关闭、面板 Quit、面板 ×、双摆 ×、相空间 × 都 exit 0；
- 磁盘 hot.lua：128×128/ice →（面板 Reload）96×96/ember；
- legacy CPU 路径 64×64 截图仍正确（固定管线 + NEAREST）。

完整记录见 [docs/verification.md](docs/verification.md)，设计和取舍见
[docs/design.md](docs/design.md)。

## 已知取舍

- 面板高度按工作区 clamp 到 540 px；1080p 下完整，更矮的屏幕可能要拉大窗口（面板可调整大小）。
- GPU 和 Fortran 的舍入不同，极端像素的场值可能不完全一致；默认 GPU。
- legacy 路径仍是固定管线，过滤为 NEAREST，但没有现代路径的完整视图 uniform 性能。
- 面板/运行时首次启动要解包约 7.8 MB，之后走缓存。
- exe 没有签名，Windows 首次运行可能出现 SmartScreen 提示。

## 仓库

- 基线：tag `baseline-single-window`（单窗口原版）；
- 当前分支：`three-windows`；
- 自检：`dist\dp-phase.exe /selftest`、`/gpu-selftest`；离屏图：`/capture out.bmp ...`、`/shot`。