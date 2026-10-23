# 动力学系统地图实验箱 · 双摆 / Logistic / Hénon

一个 **二维参数/初值地图（2D map）** 实验框架：

> 选模型 → 选两个参数/初值作为 x/y → 每个像素积分 → 多通道观测量 →
> 后处理 + 调色板 → 右键选像素看轨道/状态。

三个模型共享同一套 Experiment/Atlas/View/Panel：

| 模型 | 类型 | 默认 x/y 轴 | 参数 | 通道 |
| --- | --- | --- | --- | --- |
| Double pendulum | ODE | θ1 / θ2 | θ1, θ2, ω1, ω2, m1, m2, l1, l2, g, damping | θ1, θ2, energy, omega_max |
| Logistic map | map | r / x0 | r, x0 | x, lyapunov, mean, |x| |
| Hénon map | map | a / b | a, b, x0, y0 | x, y, radius, |x| |

双摆窗口只对双摆模型存在；Logistic/Hénon 是“地图 + 参数面板”两窗口。

![三窗口](docs/shot-windows.png)

## 快速开始

```powershell
cd pendulum-phase
pwsh -File build.ps1
Expand-Archive .\dist\dp-phase-1.6.0.zip -DestinationPath .\dist\run -Force
.\dist\run\dp-phase\dp-phase.exe

# 直接打开某个模型
.\dist\run\dp-phase\dp-phase.exe /model logistic
.\dist\run\dp-phase\dp-phase.exe /model henon
```

发布物是解压即用的文件夹 + zip；Tcl/Tk 动态链接，不做单 exe/内嵌运行时：

```text
dp-phase/
  dp-phase.exe
  tcl86.dll / tk86.dll / libgcc_s_dw2-1.dll / zlib1.dll
  lib/tcl8.6, lib/tk8.6
  ui/panel.tcl
  experiments/palette.lua
```

已验证“PATH 只剩系统目录、MINGW_ROOT 指向不存在目录”的干净副本可以启动面板。

## 三个模型

### Double pendulum（示例 ODE）

状态 `[θ1, θ2, ω1, ω2]`，参数 `[θ1, θ2, ω1, ω2, m1, m2, l1, l2, g, damping]`。
x/y 可以是任意两个 eligible 参数：

- θ1/θ2：初值地图（Angle map 预设）；
- m1/m2：质量参数图（Mass map 预设）；
- l1/l2、damping、ω 等也可直接做轴。

物理验收仍覆盖静止能量、damping=0 能量守恒、C/Fortran 参考对照。

### Logistic map

`x ← r x (1-x)`，初值 `x0`：

- 默认轴 r/x0，r ∈ [0,4]，x0 ∈ [0,1]；
- 通道：
  - `x`：最终状态；
  - `lyapunov`：有限时间 Lyapunov 指数，`mean(log|r(1-2x)|)`；
  - `mean`：轨道平均；
  - `|x|`。
- 自检覆盖 r=2.5 的稳定不动点 1-1/r、r=4 时 λ≈ln 2。

![Logistic map](docs/shot-logistic.png)

### Hénon map

`x' = 1 - a x² + y`，`y' = b x`：

- 参数 a,b,x0,y0；默认轴 a/b；
- 通道 x、y、radius、|x|；
- 自检覆盖 a=1.4,b=0.3 的有界吸引子。

![Hénon map](docs/shot-henon.png)

## 模型选择与面板

面板标题下是 **Model** 下拉框：

- 切换模型会重建参数 schema、轴、范围和通道；
- 双摆会重新创建右侧检查器，Logistic/Hénon 不创建；
- 参数在 **Parameters** 页改，Apply 后重建，不需要 reload Lua；
- **Angle map / Mass map** 预设是双摆专用；在 map 模型上点它会切回双摆。

![模型面板](docs/shot-panel.png)

**View**：Model、Color by、View（raw/gradient/range/variance）、Palette、颜色条、Grid/Periodic/Circle、Reset view、Angle/Mass 预设、Export PNG/CSV。

**Parameters**：X/Y 轴、范围、Res、dt、Steps/frame、模型参数、Apply parameters。
面板高度按屏幕自适应（1080p 下 620 客户区），双摆 10 个参数能完整显示。

**Script**：`experiments/palette.lua`、Reload palette（F5）。

**Inspect**：右键选中像素后显示 cell、x/y 轴值、状态和通道值；map 模型没有专用窗口，这个页就是它的检查器。

![参数面板](docs/shot-panel-params.png)

![选中值](docs/shot-panel-inspect.png)

## 颜色条、选中值与导出

- View 页底部有当前通道的颜色条，标签/单位/范围来自模型通道定义；
- 轴读数显示 `X name [min, max]  Y name [min, max]`；
- 右键点击地图，Inspect 页更新；
- Export PNG：把当前 CPU 颜色化的 atlas 写成 8-bit RGBA PNG；
- Export CSV：每格一行，列为
  `model,ix,iy,x_<axis>,y_<axis>,state_<...>,ch_<...>`；
- 导出目录是 exe 旁的 `exports/`，文件名带时间戳；面板 Last 显示完整路径；
- 没有文件选择对话框，PNG 只包含 atlas，不包含面板/叠加线。

## 通道与后处理

模型提供自己的通道范围/单位/是否循环；显示层统一处理：

| 后处理 | 计算 |
| --- | --- |
| raw | 原值，按模型范围映射 |
| gradient | 3×3 Sobel 幅值 |
| range | 3×3 极差 |
| variance | 3×3 标准差 |

gradient/range/variance 自动按当前画面 min/max 映射；角度差分使用 wrap_diff。

## Lua 现在负责什么

`experiments/palette.lua` 是共享的调色板 + 实验元数据脚本：

- 标题/meta；
- `palette()` / `set_scheme()` / `status()`；
- 参数 schema 仍在 C 的 Model 表里，面板实时改，不需要 reload；
- F5 只重读调色板。

下一步会把 experiment schema/表达式模型移到 Lua，再生成 GLSL/CPU 内核；当前不是任意公式 DSL。

## 代码结构

```text
src/model.h         通用 Model 接口（参数/状态/通道/回调）
src/model.c         模型注册表
src/model_dp.c      双摆 ODE
src/model_logistic.c Logistic map + Lyapunov
src/model_henon.c   Hénon map
src/experiment.c/h  模型无关的轴/范围/通道 schema
src/atlas.c/h       每像素 state+params、调用模型、通道、后处理、RGBA
src/app.c           窗口/帧循环/模型切换/选择/预设
src/gpu.c           OpenGL 4.3 上下文、atlas RGBA 显示、线叠加
src/phase_view.c    归一化 [0,1] 视图、pick、平移/缩放、格线/圆圈
src/panel.c         Tcl/Tk 宿主与 dp_action/dp_status/dp_query
src/png.c           最小 RGBA PNG 写入器（zlib）
ui/panel.tcl        四页动态面板 + Model 下拉 + Inspect
experiments/        共享调色板脚本
src/selftest.c      CPU 76 项；含 GPU 的 82 项
```

## 验证

- `/selftest`：**76/76**
- `/gpu-selftest`：**82/82**
- 三个模型 GUI 启动、面板切换、双摆检查器创建/销毁、Logistic/Hénon 地图都实测；
- 详见 [docs/verification.md](docs/verification.md)，设计见 [docs/design.md](docs/design.md)。

## 已知边界 / 下一步

1. Lua 仍是调色板/meta，不是模型 DSL。
2. GUI 采样是 CPU；GPU compute atlas 未接。
3. 地图模型没有独立窗口检查器；用面板 **Inspect** 页。
4. 没有文件选择对话框、实验保存/加载；导出固定到 exe 旁 exports/。
5. 能量通道范围固定 ±50 J；极端参数会饱和；PNG 不包含叠加线。
6. 没有 3D 切片/分岔连续化；目标是 2D 地图，不是 AUTO/MatCont 替代品。

## 仓库

- 分支：`three-windows`
- 自检：`dp-phase.exe /selftest`、`/gpu-selftest`
- 发布：`dist/dp-phase/`、`dist/dp-phase-1.4.0.zip`