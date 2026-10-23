# 动力学系统地图实验箱 · 双摆作为示例

这不是“再画一个双摆”，而是一个 **二维参数/初值地图（2D map）** 的实验框架：

> 写一个动力系统模型 → 选两个参数/初值作为屏幕 x/y → 每个像素积分一次 →
> 得到多个通道（最终角度、能量、最大角速度……）→ 选通道、后处理和调色板显示 → 右键选像素看轨迹。

双摆是第一个内置示例。模型层、实验描述层、采样层、通道层、视图层和 UI 已经分开；
下一步才是任意表达式模型和第二个模型。

![三窗口](docs/shot-windows.png)

## 快速开始

```powershell
cd pendulum-phase
pwsh -File build.ps1
Expand-Archive .\dist\dp-phase-1.4.0.zip -DestinationPath .\dist\run -Force
.\dist\run\dp-phase\dp-phase.exe
```

发布物是 **解压即用的文件夹 + zip**，不再做单 exe/内嵌 Tcl/Tk：

```text
dp-phase/
  dp-phase.exe
  tcl86.dll / tk86.dll / libgcc_s_dw2-1.dll / zlib1.dll
  lib/tcl8.6, lib/tk8.6
  ui/panel.tcl
  experiments/double-pendulum.lua
  docs/
```

Tcl/Tk 直接动态链接（exe 导入 `tcl86.dll`/`tk86.dll`），脚本库路径由 exe 目录决定，
不依赖 `C:\MinGW`、当前工作目录或系统安装。已在“没有 MinGW、PATH 只剩系统目录”的
干净副本里验证过面板仍能启动。

## 这一轮完成了什么

### 1. 双摆退成“示例模型”

- 参数表、轴、范围、通道、视图都在 `Experiment` 里；
- 双摆参数：`theta1, theta2, omega1, omega2, m1, m2, l1, l2, g, damping`；
- x/y 轴可以选任意两个 eligible 参数：
  - θ1/θ2：初值地图；
  - m1/m2：质量参数图；
  - 以后 l1/l2、damping、ω 等也可以直接做轴。

### 2. 参数在面板里改，不再重载 Lua

面板分三页：

- **View**：Color by、View（raw/gradient/range/variance）、Palette、Grid/Periodic/Circle、Reset view、Angle/Mass 预设。
- **Parameters**：X/Y 轴、X/Y 范围、分辨率、dt、Steps/frame、10 个参数、Apply parameters。参数改动通过 Apply 生效，不触发 Lua reload。
- **Script**：脚本路径、Reload palette（F5）、说明。

![View 面板](docs/shot-panel.png)

![参数面板](docs/shot-panel-params.png)

### 3. 多通道 + 局部敏感性

每个像素输出 4 个通道：

| 通道 | 含义 | 显示范围 |
| --- | --- | --- |
| `theta1` | 最终 θ1 | [-π, π]，循环色带 |
| `theta2` | 最终 θ2 | [-π, π]，循环色带 |
| `energy` | 总能量（动能 + 势能） | 当前按 ±50 J |
| `omega_max` | max(|ω1|, |ω2|) | [0, 20] rad/s |

View 后处理对选中通道做 3×3 局部统计：

- `raw`：原值；
- `gradient`：Sobel 幅值；
- `range`：3×3 极差；
- `variance`：3×3 标准差。

后处理会自动按当前画面做 min/max 映射，所以“敏感性”边界不会因为绝对尺度太小而看不见。

角度图（θ1/θ2，颜色 = 最终 θ1）：

![角度图](docs/shot-phase.png)

质量图（m1/m2，颜色 = 局部 omega_max 敏感性）：

![质量图](docs/shot-mass.png)

### 4. 物理验收

- 双摆 RK4 仍与 C 参考/原 Fortran 内核逐式对应；
- 新增能量函数 `dp_energy`，自检包含：
  - 静止能量 `-(m1+m2)g l1 - m2 g l2`；
  - damping=0、dt=0.001、2000 步能量相对漂移 < 1e-3；
- 当前默认 `theta1=0.5, theta2=0`，这样质量图在固定角度下不会退化成静止；
- 角度轴会覆盖这两个默认值。

### 5. 性能

- 64×64 地图：CPU 多线程采样，约 **142–147 fps**（每帧 1–8 步）；
- 500 步 64×64 离线渲染约几十毫秒；
- 质量图预设下仍保持 100+ fps；
- 256×256 时计算量约 16 倍，CPU 仍可用但会明显变慢；GPU compute 是下一阶段。

### 6. 低分辨率仍然锐利

历史问题是场纹理 `GL_LINEAR` 放大：64×64 拉到 700×700 时每个纹素和邻居双线性混合，
格子边界变糊。现在 atlas 纹理是 `GL_NEAREST`，显示层只做最近邻采样。

| 修复前 | 修复后 |
| --- | --- |
| ![before](docs/blur-before.png) | ![after](docs/blur-after.png) |

### 7. 打包

`build.ps1` 六步：检查 Tcl/Tk → 嵌入 shader → 编译 C → 编译 Fortran（保留参考内核）→
windres → 动态链接 Tcl/Tk + 静态 Lua/Fortran → staging → zip。

- `dist/dp-phase/`：可运行目录；
- `dist/dp-phase-1.4.0.zip`：约 3.6 MB；
- exe 约 713 KB；
- 导入表：KERNEL32/USER32/GDI32/OPENGL32/WINMM/MSVCRT + `tcl86.dll`/`tk86.dll`；
- Tcl/Tk 的 `libgcc_s_dw2-1.dll`、`zlib1.dll` 也在 zip 里。

## 控制面板

| 功能 | 面板 | 快捷键 |
| --- | --- | --- |
| 暂停/继续 | Pause/Resume | Space |
| 重置地图 | Reset | R |
| 调色板 | Palette | C |
| 格线 | Grid | G |
| 循环边界 | Periodic | P |
| 选中圆圈 | Circle | O |
| 重置视图 | Reset view | 0 |
| 重新读调色板脚本 | Reload palette | F5 |
| 退出 | Quit / 标题栏 × | Esc |

鼠标：左键拖动平移，滚轮以光标为中心缩放，右键选像素。选中像素后右侧双摆窗口显示
该格点的质量、最终角度/角速度、阻尼和能量。

![双摆检查器](docs/shot-pendulum.png)

## Lua 现在负责什么

`experiments/double-pendulum.lua` 现在是一份 **实验元数据 + 调色板脚本**：

- 标题、模型名、meta（轴/通道说明）；
- `palette()` / `set_scheme()` / `status()`；
- 参数默认值仍然在 C 的 `Experiment` 里，面板实时改，不需要 reload；
- F5 / Script 页的 Reload 只重新读调色板。

这是为下一步“Lua 描述实验 + 表达式模型”留的接口。当前还不是任意公式 DSL，也不是
AUTO/MatCont 式的连续化工具。

## 代码结构

```text
src/experiment.c/h   参数 schema、轴选择、范围、通道元数据
src/atlas.c/h        每像素 state+params、RK4 采样、通道、后处理、RGBA
src/app.c            窗口/帧循环/面板命令/选择/预设
src/gpu.c            OpenGL 4.3 上下文、atlas RGBA 显示、线叠加
src/phase_view.c     归一化 [0,1] 视图、pick、平移/缩放、格线/圆圈
src/panel.c          Tcl/Tk 宿主与 dp_action/dp_status/dp_query
ui/panel.tcl         三页动态面板
experiments/         实验脚本（当前只有双摆示例）
src/physics.c        C 参考 RK4、能量、调色板
src/sim.c, kernel_fortran.c, fortran/  旧 Fortran 参考/自检保留
src/selftest.c       CPU 65 项；含 GPU 的 71 项
```

## 验证

- `/selftest`：**65/65**
- `/gpu-selftest`：**71/71**
- 质量图预设、参数页、右键选点、双摆能量显示、无 MinGW 的干净副本启动都已实测；
- 详见 [docs/verification.md](docs/verification.md)，设计见 [docs/design.md](docs/design.md)。

## 已知边界 / 下一步

1. **不是任意公式工具**：模型仍是内置双摆；Lua 表达式模型、代码生成到 GLSL 是下一阶段。
2. **GPU 只负责显示**：GUI 的采样走 CPU 多线程；GPU compute atlas 是下一阶段。
3. **只有一个示例模型**：下一个应是 logistic/Hénon 这种可验证的 map，证明模型解耦。
4. **能量通道范围固定 ±50 J**，还没有 colorbar / 自动分位数 / 单位轴标签。
5. 没有保存/加载实验文件、导出 CSV、3D 切片滑块；这些都属于后续。

## 仓库

- 分支：`three-windows`
- 历史：`baseline-single-window` → 打包/动态 Tcl/Tk → 实验地图/动态参数
- 自检：`dp-phase.exe /selftest`、`/gpu-selftest`