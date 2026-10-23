# 设计说明：二维动力学地图实验箱

## 1. 目标

把原来的“双摆相空间程序”改造成一个 **二维参数/初值地图工具**：

- 模型、实验描述、采样、通道、视图、UI 分层；
- 双摆只是第一个内置模型；
- 参数在控制面板里实时编辑，不再靠改文件 + 重载；
- 发布为一个解压即用的文件夹 + zip，而不是单 exe 内嵌运行时；
- 先保证物理和数值可信，再谈任意公式/GPU。

当前是 M2 里程碑：双摆示例、动态参数/轴/通道、局部敏感性、Tk 面板、zip。
任意表达式模型、第二个模型、GPU compute、colorbar/轴标签还在后面。

## 2. 数据流

```text
Experiment (C schema, Lua palette)
  params[10], axes(x,y), ranges, res, dt, steps
        │
        ▼
Atlas reset: 每格 state[4] + params[6] = f(x,y,axes,fixed params)
        │
        ▼
Atlas step: 每格 RK4，行带线程池；每步后计算 channels[4]
        │
        ▼
Colorize: 选 channel → raw/gradient/range/variance → LUT → RGBA
        │
        ▼
Display: RGBA texture + normalized view + grid/selection overlay
        │
        ▼
Panel: dp_query schema / dp_action set+apply / dp_status
```

## 3. 地图语义

屏幕 x/y 是 **两个选定的参数/初值**，每个像素是一组参数/初值；颜色是积分后的观测量。
这不是传统相空间轨迹（θ vs ω），也不是分岔图的连续化曲线。

当前双摆参数：

| 参数 | 默认 | 范围 | 说明 |
| --- | --- | --- | --- |
| theta1 | 0.5 | [-π,π] | 初始角 |
| theta2 | 0.0 | [-π,π] | 初始角 |
| omega1 | 0 | [-10,10] | 初始角速度 |
| omega2 | 0 | [-10,10] | 初始角速度 |
| m1 | 1 | [0.2,3] | 质量 |
| m2 | 1 | [0.2,3] | 质量 |
| l1 | 1 | [0.2,2] | 长度 |
| l2 | 1 | [0.2,2] | 长度 |
| g | 9.81 | [0,20] | 重力 |
| damping | 0 | [0,1] | 阻尼 |

任意两个 eligible 参数可作为轴。当前轴映射在 CPU 的 `exp_axis_values`：

```text
u = (ix + 0.5) / res
v = (iy + 0.5) / res
x = xmin + u*(xmax-xmin)
y = ymin + v*(ymax-ymin)
```

角度轴是周期量，但“轴的范围”是用户选的；周期视图只是把归一化 [0,1] 视图平铺，
并不改变参数范围。

## 4. 通道

`Atlas` 每格存：

```text
state[4]  = theta1, theta2, omega1, omega2
params[6] = m1, m2, l1, l2, g, damping
```

每步 RK4 后计算 4 个通道：

| 通道 | 公式 |
| --- | --- |
| theta1/theta2 | 最终角度，自动归一到 [-π,π] |
| energy | KE + PE，见下 |
| omega_max | max(|ω1|,|ω2|) |

能量：

```text
KE = 0.5*(m1+m2)*l1^2*w1^2 + 0.5*m2*l2^2*w2^2
   + m2*l1*l2*w1*w2*cos(t1-t2)
PE = -(m1+m2)*g*l1*cos(t1) - m2*g*l2*cos(t2)
E  = KE + PE
```

自检覆盖静止能量和 damping=0 的能量漂移。damping=0、ω=0 时能量等于初始势能；
能量对 m1/m2 是线性的，所以它的 3×3 局部极差是常数，敏感性视图更适合 `omega_max` /
最终角度；这也是 mass 预设使用 `omega_max + range` 的原因。

## 5. 采样与线程

- `Atlas` 用固定线程池按行带并行（复用 `pool.c`）；
- 每格参数不同，所以不再使用 Fortran 的“统一参数行带”内核；
- 64×64、每帧 1–8 步在目标机器上约 140 fps；
- 500 步 64×64 离线渲染几十毫秒；
- 256×256 仍可算，但 CPU 成本约 16 倍；GPU compute 是下一阶段。

## 6. 视图与后处理

视图坐标统一成归一化 [0,1]：

- `PhaseView` 的 range 固定 1.0；
- 世界坐标 [-0.5,0.5]，采样 uv = world+0.5；
- 平移/缩放/周期平铺只作用于归一化视图；
- 轴的物理范围/单位在 Experiment/面板里。

后处理：

| 名称 | 计算 |
| --- | --- |
| raw | 通道原值，按通道范围映射 |
| gradient | 3×3 Sobel 幅值 |
| range | 3×3 极差 |
| variance | 3×3 标准差 |

gradient/range/variance 先在两遍中求当前画面 min/max，再映射到 [0,1]，避免固定缩放
导致整幅图过暗或饱和。角度通道的差分使用 wrap_diff，避免 ±π 接缝产生假边缘。

## 7. 显示

- GUI 的 display 是 **RGBA8 纹理 + atlas.frag**：CPU 已算好颜色，GPU 只做归一化视图采样；
- 这样 CPU/GPU 两条路径颜色一致，也避开了 GUI GPU compute 的多通道参数映射；
- 旧 `gpu.c` 的 compute/display/line 路径保留给 `/gpu-selftest` 和旧回归；
- 叠加层仍是线着色器（现代）或固定管线（legacy），格线/选中圆圈用归一化坐标。

## 8. 面板

`ui/panel.tcl` 分三页：

- **View**：通道、后处理、调色板、格线/周期/圆圈、Reset view、Angle/Mass 预设。
- **Parameters**：X/Y 轴 combo、X/Y 范围、Res/dt/Steps/frame、10 个参数（两列）、Apply。
- **Script**：脚本路径、Reload palette（F5）。

C 注册：

- `dp_action name ?value?`：reset/pause/palette/...、param_set、axis_set、range_set、
  res_set、dt_set、steps_set、channel_set、post_set、preset、apply；
- `dp_status`：paused/kernel/fps/res/selected/zoom/source/last；
- `dp_query params|axes|channels|view|integration|source|title`：schema 查询；
- `dp_quit`。

参数编辑走 `param_set`，只在 Apply 时重建；轴切换立即重建并重置。Reload 只重读
调色板脚本，不重置参数。

## 9. Lua

当前 `experiments/double-pendulum.lua` 只负责：

- 标题/meta（模型、轴、通道说明）；
- 调色板 `palette()`；
- `set_scheme()` / `status()`；
- `defaults()` 兼容旧 Lua 宿主创建。

参数 schema 暂时在 C 的 `experiment.c`。下一步把 schema 移到 Lua，并让 Lua 用表达式
描述模型/观测量；表达式需要解析 + GLSL 代码生成，不能每像素解释 Lua。

## 10. 打包

`build.ps1`：

1. 检查 `tcl86.dll`、`tk86.dll`、`libgcc_s_dw2-1.dll`、`zlib1.dll`、脚本树；
2. 编译内嵌 shader（含 atlas.frag）；
3. 编译 C（16 个对象）；
4. 编译 Fortran 参考内核；
5. windres；
6. 链接：Tcl/Tk 动态，Lua/Fortran 静态；staging；zip。

运行路径：

```text
TCL_LIBRARY = <exe_dir>\lib\tcl8.6
TK_LIBRARY  = <exe_dir>\lib\tk8.6
```

`Tcl_FindExecutable(exe)` + `Tcl_Init` + `Tk_Init`；不再解包、不写 LOCALAPPDATA。

## 11. 物理与数值边界

保留的旧内核：

- `physics.c` 的 RK4 仍是参考实现；
- `fortran/kernel.f90` 与 `sim.c` 保留作旧行带/回归；
- GUI 不再调用它们，避免“每格参数不同”与“统一参数行带”冲突。

已知限制：

- 双摆模型仍内置，未真正插件化；
- 只有 1 个模型；
- 无 colorbar/轴标签/单位；
- 能量范围固定 ±50 J；
- 无实验保存/CSV/PNG 导出；
- 无 GPU compute atlas；
- 3D 切片未做。

## 12. 路线

- M3：logistic/Hénon 第二个模型，验证 model/experiment 解耦；
- M4：Lua schema + 表达式模型 → GLSL 代码生成 + CPU 回退；
- M5：colorbar/轴标签/导出/保存实验；
- M6：GPU compute atlas、FTLE/盆地、Poincaré、切片。
