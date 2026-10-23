# 动力学系统地图实验箱 · 双摆 / Logistic / Hénon / Standard map / Lua 表达式模型

一个 **二维参数/初值地图（2D map）** 实验框架：

> 选模型 → 选两个参数/初值作为 x/y → 每个像素积分 → 多通道观测量 →
> 后处理 + 调色板 → 右键选像素看轨道/状态。

四种内置模型和 Lua 自定义表达式模型共享同一套 Experiment/Atlas/View/Panel：

| 模型 | 类型 | 默认 x/y 轴 | 参数 | 通道 |
| --- | --- | --- | --- | --- |
| Double pendulum | ODE | θ1 / θ2 | θ1, θ2, ω1, ω2, m1, m2, l1, l2, g, damping | θ1, θ2, energy, omega_max |
| Logistic map | map | r / x0 | r, x0 | x, lyapunov, mean, |x| |
| Hénon map | map | a / b | a, b, x0, y0 | x, y, radius, |x| |
| Standard map | map | k / p0 | k, theta0, p0 | theta, p, |p|, cos(theta) |
| Lua Hénon (custom) | Lua map | a / b | a, b, x0, y0 | x, y, radius, |x| |

双摆窗口只对双摆模型存在；Logistic/Hénon/Lua 自定义模型是“地图 + 参数面板”两窗口。

![三窗口](docs/shot-windows.png)

## 快速开始

```powershell
cd pendulum-phase
pwsh -File build.ps1
Expand-Archive .\dist\dp-phase-1.30.0.zip -DestinationPath .\dist\run -Force
.\dist\run\dp-phase\dp-phase.exe

# 直接打开某个模型
.\dist\run\dp-phase\dp-phase.exe /model logistic
.\dist\run\dp-phase\dp-phase.exe /model henon
.\dist\run\dp-phase\dp-phase.exe /model standard
.\dist\run\dp-phase\dp-phase.exe /model custom
```

发布物是解压即用的文件夹 + zip；Tcl/Tk 动态链接，不做单 exe/内嵌运行时：

```text
dp-phase/
  dp-phase.exe
  tcl86.dll / tk86.dll / libgcc_s_dw2-1.dll / zlib1.dll
  lib/tcl8.6, lib/tk8.6
  ui/panel.tcl
  experiments/experiment.lua
```

已验证“PATH 只剩系统目录、MINGW_ROOT 指向不存在目录”的干净副本可以启动面板。

## 内置模型 + Lua 自定义

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

### Standard map

Chirikov 标准映射：

```text
p'     = p + k sin(theta)
theta' = theta + p'   (mod 2*pi)
```

- 默认轴：k / p0；theta0 固定，可在 Parameters 页改；
- 通道：theta、p、|p|、cos(theta)；
- 64/256 地图可见 KAM 岛与混沌海；
- CPU 与 GPU shader 均已接入。

![Standard map](docs/shot-standard.png)

### Lua 表达式模型（M5a/M5b）

`experiment.lua` 里的 `M.custom` 定义 2 状态 map：

- `state = { "x", "y" }`；
- `step = { x = "1 - a*x*x + y", y = "b*x" }`；
- 参数 a,b,x0,y0，默认轴 a/b；
- channels 用表达式：`x`、`y`、`sqrt(x*x + y*y)`、`abs(x)`。

C 的 `expr.c` 在加载时把字符串编译成 **postfix bytecode**，每像素执行字节码；
**不逐像素调用 Lua**。自检将 custom 与内置 Hénon 的前 10 步逐值比较。

M5b 又加了 **GLSL 代码生成**：`expr_to_glsl()` 把 postfix 字节码转成 GLSL，
`custom_model_glsl()` 生成 compute shader，`gpu_compile_custom/gpu_run_custom` 编译并执行。
GPU 单格结果与 CPU 逐值匹配（双精度 CPU vs float GPU，容差 1e-5）。

**M6/M8：GPU compute atlas 已覆盖全部内置模型 + Lua 自定义模型**。View 页的 **Kernel** 可选 CPU/GPU：

- GPU 后端把整张 Atlas cells 上传，dispatch 模型 compute，再读回 state/channels，
  交给现有 CPU colorize；
- 双摆/Logistic/Hénon/Standard/Lua custom 都有 GPU shader；
- 所有模型可选 GPU；编译/上下文失败自动回退 CPU 并提示；
- 标题/状态显示 GPU；CPU/GPU 结果在 2 步 atlas 测试中一致；
- GPU float 与 CPU double 在长时间混沌轨道上会分叉，这是预期；
- 2 步 atlas GPU/CPU 对照：双摆 < 1.2e-5，Logistic/Hénon/Standard < 5e-7。

![GPU 自定义模型](docs/shot-custom-gpu.png)

![GPU 面板](docs/shot-custom-gpu-panel.png)

自定义模型支持两种类型：

- `type="map"`（默认）：`step = { x="...", y="..." }`；
- `type="ode"`：`deriv = { x="y", y="-x" }`，CPU/GLSL 都用 RK4，`dt` 来自实验。

表达式支持：

- 比较：`< <= > >= == !=`；
- 逻辑：`&& || !`；
- 三元：`cond ? a : b`；
- `if(cond, a, b)`。

GLSL 的条件必须是 bool（比较/逻辑结果），CPU 仍接受数值真值。自定义 `x_abs` 通道
已改为 `if(x < 0, -x, x)`，CPU/GPU 都通过测试。

当前限制：自定义模型固定 2 状态；ODE 是 opt-in（默认示例仍为 Hénon map）；
所有模型支持 GPU，但 CPU/GPU 长时间混沌轨道会因精度分叉。

![Lua 表达式模型](docs/shot-custom.png)

![Lua 表达式面板](docs/shot-custom-panel.png)

## 实验保存 / 加载（M7a）

Script 页新增 **Save / Load / Load last / Recent**，使用 Windows 原生文件对话框，
扩展名 `.dps`。保存内容：

- 模型 id、轴名、轴范围；
- 所有参数值；
- res/dt/steps、通道、后处理、palette；
- Grid/Periodic/Circle；
- CPU/GPU 后端；
- 视图中心/zoom；
- 选中格（selected/sel_x/sel_y），加载后直接恢复 Inspect；
- 最近实验记录在 `recent.txt`（最多 8 条）。

文件是简单的行格式：

```text
DPEXPERIMENT 1
model=logistic
axis_x=r
axis_y=x0
xmin=0
xmax=4
res=32
param.r=3.7
...
```

命令行也可用：

```powershell
.\dist\dp-phase\dp-phase.exe /save session.dps
.\dist\dp-phase\dp-phase.exe /load session.dps
.\dist\dp-phase\dp-phase.exe /shot out.bmp /load session.dps
```

`Recent` 下拉列出 exe 旁 `recent.txt` 中最近 8 个实验（显示 basename），`Open` 加载选中项；
`Load last` 读取最近一次路径。`Save/Load` 会更新这两个文件。
实测 `/save` 后再 `/load` 的离屏图 hash 完全一致。加载会重建 atlas/检查器，
并刷新面板 schema；custom 模型需要当前 `experiment.lua` 仍定义它。

![Script 页](docs/shot-panel-script.png)

## 导出偏好

- PNG 导出分辨率可选 **1x/2x/4x**，最近邻放大，不重新采样模型；
- 导出使用原生 **Save As**，默认路径是 `<exe>\exports\atlas-YYYYMMDD-HHMMSS.png/.csv`；
- `PNG ov`（默认开）会把 Grid/选中圈/轴刻度/轴名栅格化进 PNG，<192px 时只画 Grid/选中圈；
- CLI 也可 `/shot out.bmp /export_png atlas.png`；
- CSV 在 `post=ftle` 时追加 `ftle` 列；
- 偏好保存在 `.dps` 的 `export_scale/export_overlay`。

![导出缩放](docs/shot-export.png)

## FTLE / 有限时间拉伸

View 下拉的 **ftle** 对每个初始 cell 做：

- 以 base state 为起点，扰动第一个状态分量 `eps=1e-6*(1+|x|)`；
- 两条轨道各走 `steps` 步；
- `log(|delta final| / eps) / steps`，按当前 post 自动定色标范围。

FTLE 在 atlas reset 后缓存，参数/轴变化才重算；播放时不会每帧重算，64×64 仍约 130 fps。
这是有限差分拉伸估计，不是切空间/变分方程 FTLE。

![FTLE](docs/shot-ftle.png)

![FTLE 面板](docs/shot-ftle-panel.png)

## 逃逸时间 / escape

View 下拉的 **escape** 对每个 cell：

- 从初始 state 起逐步积分，最多 `steps` 步；
- 任一分量 `|state| > Escape r` 或 NaN 时停止；默认半径 10，可在 Parameters 页改；
- 颜色表示逃逸步数（未逃逸 = 最大步数）。

适合 Hénon / Standard map 这类可逃逸 map；Logistic/双摆没有该阈值逃逸，显示为常数。
escape 结果和 FTLE 一样缓存，reset 后重算；半径存入 `.dps`。

![escape 半径](docs/shot-params-escape.png)

![escape](docs/shot-escape.png)

## 最终状态 / final

View 下拉的 **final**：

- 每个 cell 从初始 state 积分 `steps` 步；
- 颜色是最终 `state[0]`（双摆是归一化后的 theta1）；
- 自动定色标范围，可作为 basin/最终角度图的近似；
- 结果缓存，reset/参数/轴变化重算。

它**不是**严格的吸引子聚类：没有对最终状态做离散分类，只是连续最终状态。
Hénon 的 final 图在逃逸角落会显示不同终态。

![final](docs/shot-final.png)

## 吸引子分箱 / basin

View 下拉的 **basin** 把最终状态量化为 **8 个离散 bin**：

- 双摆：最终 theta1 归一化后 8 个扇区；
- Logistic：最终 x ∈ [0,1] 分 8 箱；
- Hénon / Standard / custom：最终 `atan2(y, x)` 的 8 个扇区；
- 颜色走离散 LUT，不做连续插值；
- 结果缓存，reset/参数/轴变化重算。

这是“最终状态分箱”的 basin 近似，不是按吸引子做 k-means/收敛聚类。

![basin](docs/shot-basin.png)

## k-means 最终状态聚类 / cluster

View 下拉的 **cluster** 先积分每个 cell 到最终状态，再对最终状态做 **k-means**：

- 双摆：`(theta1, theta2)`（归一化角度）；
- Logistic：`x`（一维）；
- Hénon / Standard / custom：`(x, y)`；
- `Clusters` 可在 Parameters 页设 2..16，默认 4；
- 12 次迭代，空簇保留旧中心；
- 颜色用离散 LUT，label 0..K-1。

这是**最终状态聚类**，不是收敛检测/吸引子数量证明。

![cluster](docs/shot-cluster.png)

![cluster 参数](docs/shot-params-cluster.png)

## 收敛检测 / converge

View 下拉的 **converge**：

- 积分 `steps` 步；
- 记录倒数 `steps/4` 步前与最终状态；
- 两点距离 < `Conv eps`（默认 1e-6）→ converged（紫），否则绿色；
- 双摆角度差用周期差；
- 缓存，reset/参数/轴变化重算。

窗口固定为 `steps/4`，`Conv eps` 可在 Parameters 页改，并存入 `.dps`。
这是固定窗口阈值的收敛检测，不报告收敛速度或吸引子数量。

![converge](docs/shot-converge.png)

![converge 参数](docs/shot-params-converge.png)

## 地图刻度

地图窗口在底边/左边绘制 min/mid/max 数字刻度与短 tick；顶部左右直接画
`X theta1` / `Y theta2` 轴名。数值按当前缩放/平移换算，范围仍在面板读数里。

![地图刻度](docs/shot-axis.png)

## 模型选择与面板

面板标题下是 **Model** 下拉框：

- 切换模型会重建参数 schema、轴、范围和通道；
- 双摆会重新创建右侧检查器，Logistic/Hénon/Lua 自定义不创建；
- 参数在 **Parameters** 页改，Apply 后重建，不需要 reload Lua；
- **Angle map / Mass map** 预设是双摆专用；在 map 模型上点它会切回双摆。
- Lua 自定义模型只有 `experiment.lua` 里存在 `M.custom` 时才出现。

![模型面板](docs/shot-panel.png)

**View**：Model、Color by、View（raw/gradient/range/variance/ftle/escape/final/basin/cluster/**converge**）、Palette、Kernel CPU/GPU、颜色条、Grid/Periodic/Circle、Reset view、Angle/Mass 预设、Export PNG/CSV；PNG 旁边的 `1x/2x/4x` 控制最近邻放大，CSV 在 ftle 模式多一列 `ftle`。地图边缘有数字刻度（min/mid/max），顶部直接画 X/Y 轴名；范围在面板读数里。

**Parameters**：X/Y 轴、范围、Res、dt、Steps/frame、模型参数、Apply parameters。
面板高度按屏幕自适应（1080p 下 620 客户区），双摆 10 个参数能完整显示。

**Script**：`experiments/experiment.lua`、Reload script（F5）。脚本现在同时提供 schema 元数据和调色板。

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
- 导出默认目录是 exe 旁的 `exports/`，Save As 可改路径；面板 Last 显示完整路径；
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

`experiments/experiment.lua` 现在同时提供**实验 schema 元数据**和调色板：

```lua
M.models.double_pendulum = {
    title = "Double pendulum",
    axes = { "theta1", "theta2" },
    params = {
        { name="theta1", label="θ1", unit="rad",
          default=0.5, min=-math.pi, max=math.pi, step=0.01, axis=true },
        ...
    },
    channels = {
        { name="energy", label="energy", unit="J",
          min=-50, max=50, cyclic=false },
        ...
    },
}
function M.schema(id) return M.models[id] end
```

- C 的 Model 仍拥有动力学/状态/step；Lua 只覆盖标题、参数 label/unit/range/default/axis、
  通道 label/unit/range 和默认轴。
- 名字必须匹配当前 Model；这不是任意字段/表达式模型。
- 面板改参数仍然不需要 reload；F5 重读 schema + palette，重建 atlas，并刷新面板。
- 参数值、用户轴在 F5 时保留；范围按 schema 更新/裁剪。
- 已实测把 θ1/θ2 改成 A1/A2 后 F5，参数页立即显示 A1/A2。

表达式模型、CPU/GLSL 代码生成是下一阶段 M5；当前不是任意公式 DSL。

## 代码结构

```text
src/model.h         通用 Model 接口（参数/状态/通道/回调）
src/model.c         模型注册表
src/model_dp.c      双摆 ODE
src/model_logistic.c Logistic map + Lyapunov
src/model_henon.c   Hénon map
src/expr.c/h        表达式 → postfix bytecode + GLSL 生成
src/session.c/h     .dps 实验保存/加载
src/model_custom.c/h Lua 表达式 2D map
src/experiment.c/h  模型无关的轴/范围/通道 schema + Lua metadata 应用
src/schema.h        Lua↔Experiment 的 schema 元数据
src/atlas.c/h       每像素 state+params、调用模型、通道、后处理、RGBA
src/app.c           窗口/帧循环/模型切换/选择/预设
src/gpu.c           OpenGL 4.3 上下文、动态 compute、GPU atlas、显示
src/model_glsl.c    双摆/Logistic/Hénon 的 GPU shader
src/phase_view.c    归一化 [0,1] 视图、pick、平移/缩放、格线/圆圈
src/panel.c         Tcl/Tk 宿主与 dp_action/dp_status/dp_query
src/png.c           最小 RGBA PNG 写入器（zlib）
ui/panel.tcl        四页动态面板 + Model 下拉 + Inspect
src/lua_hot.c       Lua 宿主 + palette/schema 桥
experiments/        experiment.lua：schema/表达式 + 调色板
src/selftest.c      CPU 139 项；含 GPU 的 175 项
```

## 验证

- `/selftest`：**139/139**
- `/gpu-selftest`：**175/175**
- 三个内置模型 + Lua 表达式模型 GUI 启动、面板切换、双摆检查器创建/销毁、自定义地图都实测；
- 详见 [docs/verification.md](docs/verification.md)，设计见 [docs/design.md](docs/design.md)。

## 已知边界 / 下一步

1. Lua 自定义模型支持 map/ODE、控制流、CPU+GLSL；所有模型已有 GPU；还缺地图轴刻度。
2. GPU compute atlas 覆盖所有内置模型 + Lua custom；CPU 仍是回退/验证基线。
3. 地图/自定义模型没有独立窗口检查器；用面板 **Inspect** 页。
4. 实验保存/加载已有；PNG/CSV 导出默认在 exe 旁 exports/，可 Save As。
5. 能量通道范围固定 ±50 J；极端参数会饱和；PNG 不包含叠加线。
6. 控制流/ODE、全模型 GPU、轴刻度/轴名、选中格、最近实验、FTLE、escape、final、basin、cluster、可调 converge、导出、Standard map 已完成；还缺 Poincaré/切片；目标是 2D 地图。

## 仓库

- 分支：`three-windows`
- 自检：`dp-phase.exe /selftest`、`/gpu-selftest`
- 发布：`dist/dp-phase/`、`dist/dp-phase-1.30.0.zip`