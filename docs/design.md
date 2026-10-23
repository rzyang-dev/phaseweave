# 设计说明：通用 Model + 二维地图实验箱

## 1. 目标

把“双摆相空间程序”变成一个 **模型可切换的二维参数/初值地图工具**：

- Model / Experiment / Atlas / View / Panel 分层；
- 双摆是 ODE 示例，Logistic/Hénon 是 map 示例；
- 参数在面板改，不靠 reload 脚本；
- 解压即用的文件夹 + zip，Tcl/Tk 动态链接。

当前是 M3：三个模型、通用 Model 接口、模型下拉、动态 schema、82 项自检。
Lua 表达式模型、GPU compute、专用 map inspector 仍在后面。

## 2. 数据流

```text
Model registry
  DP_MODEL / LOGISTIC_MODEL / HENON_MODEL
        │ params + channels + init/step/observe
        ▼
Experiment
  values[], axes(x,y), ranges, res, dt, max_steps
        │
        ▼
Atlas reset
  per cell: state[4] + vals[12] = model.init(exp axis values)
        │
        ▼
Atlas step
  per cell: model.step(state, vals, dt, steps)
            model.observe(state, vals, ch[4])
        │
        ▼
Colorize
  selected channel + postprocess + LUT → RGBA8
        │
        ▼
Display
  normalized [0,1] view + grid/pick
        │
        ▼
Panel
  dp_query schema/model/channels; dp_action model_set/param_set/...
```

## 3. Model 接口

`model.h` 定义：

```c
#define MODEL_MAX_PARAMS   12
#define MODEL_MAX_CHANNELS 4
#define MODEL_MAX_STATE    4

typedef struct {
    const char *name, *label, *unit;
    double def, min, max, step;
    int group;      /* 0 state, 1 parameter */
    int axis_ok;
} ModelParamDef;

typedef struct {
    const char *name, *label, *unit;
    double min, max;
    int cyclic;
} ModelChannelDef;

typedef struct Model {
    const char *id, *title;
    int state_dim, nparams;
    const ModelParamDef *params;
    int nchannels;
    const ModelChannelDef *channels;
    void (*init)(...);
    void (*step)(...);
    void (*observe)(...);
} Model;
```

`model.c` 是注册表：`model_count()`、`model_at()`、`model_by_id()`。

| 模型 | 文件 | 状态 | 参数 |
| --- | --- | --- | --- |
| Double pendulum | model_dp.c | θ1,θ2,ω1,ω2 | 10 个 |
| Logistic | model_logistic.c | x + λ/mean/count 累加器 | r,x0 |
| Hénon | model_henon.c | x,y | a,b,x0,y0 |

Experiment 不再知道双摆字段：它只从 Model 复制参数/通道定义，按名字找轴。

## 4. 三类模型的地图语义

### 双摆（ODE）

x/y 是任意两个参数/初值；每像素一组参数，RK4 走 `steps` 步。
默认 θ1/θ2；质量图可把 m1/m2 当轴。能量：

```text
KE = 0.5*(m1+m2)*l1^2*w1^2 + 0.5*m2*l2^2*w2^2
   + m2*l1*l2*w1*w2*cos(t1-t2)
PE = -(m1+m2)*g*l1*cos(t1) - m2*g*l2*cos(t2)
```

### Logistic（map）

`x ← r x (1-x)`，初值 x0；参数 r,x0。每像素用 `steps` 次迭代。
状态槽 0 是 x，1 累加 log|f'|，2 累加 x，3 计数：

```text
lyapunov = sum(log|r(1-2x)|) / n
mean     = sum(x) / n
```

r=2.5 的不动点 1-1/r=0.6 可验证；r=4 时 λ→ln 2。

### Hénon（map）

```text
x' = 1 - a x^2 + y
y' = b x
```

参数 a,b,x0,y0；a=1.4,b=0.3 有界。通道 x/y/radius/|x|。

## 5. Atlas

每格：

```text
state[4] + vals[12]   // 12 = MODEL_MAX_PARAMS
channels[4]
```

- reset：`exp_axis_values` 生成 vals，`model->init` 生成 state；
- step：行带线程池，逐格 `model->step`、`model->observe`；
- cell 存 float，模型回调用 double；拷贝必须逐元素转换（不能 memcpy double→float）；
- 64×64 默认；256×256 仍可算，CPU 成本约 16 倍。

## 6. 通道与后处理

模型自带通道范围/单位/循环标志：

| 模型 | 通道 |
| --- | --- |
| 双摆 | θ1, θ2, energy, omega_max |
| Logistic | x, lyapunov, mean, |x| |
| Hénon | x, y, radius, |x| |

后处理：raw / gradient / range / variance；后三者按当前画面自动 min/max；
角度差分 wrap_diff；能量范围目前固定 ±50 J。

## 7. 视图与显示

- `PhaseView` 的 range 固定 1.0；世界 [-0.5,0.5]；采样 uv=world+0.5；
- 轴物理范围/单位在 Experiment；
- display 是 CPU 颜色化的 RGBA8 纹理 + `atlas.frag`，GPU 只做最近邻采样；
- 格线/选中圆圈是归一化坐标上的线叠加。

## 8. 模型切换

`app_set_model(id)`：

1. 销毁当前双摆 inspector（清 closefn，避免程序化 DestroyWindow 触发退出）；
2. `exp_init(&exp, model)` 重建参数/轴/范围/通道；
3. 重建 Atlas；
4. 仅双摆创建 PendulumView；
5. reset view / colorize / title / last。

面板 Model 下拉调用 `dp_action model_set <id>`，然后 `rebuild_schema`。
Logistic/Hénon 只有地图 + 面板两窗口；双摆是三窗口。
模型切换已实测：从双摆切 Logistic，双摆窗口关闭，面板 schema/标题更新。

## 9. 面板、颜色条、Inspect 与导出

View 页有当前通道的颜色条（调色板 LUT 采样 + 模型范围/单位），底部轴读数显示
`X axis [min,max]  Y axis [min,max]`。右键选点后，**Inspect** 页显示 cell、轴值、
状态和通道值；map 模型没有独立窗口检查器，Inspect 就是它的检查器。

Export PNG 写当前 CPU 颜色化的 RGBA8 atlas（最小 zlib PNG writer，不写叠加线/面板）；
Export CSV 每格一行，列名前缀 `x_`/`y_`/`state_`/`ch_`，避免不同模型的同名通道冲突。
文件写入 exe 旁的 `exports/`，带时间戳；没有文件选择对话框。

## 10. 面板

标题下 Model 下拉；四个 tab：

- **View**：通道、后处理、调色板、Kernel CPU/GPU、颜色条、轴读数、Export PNG/CSV、Grid/Periodic/Circle、Reset view、Angle/Mass 预设；
- **Parameters**：X/Y 轴、范围、Res/dt/Steps、模型参数、Apply；1080p 下面板 620 客户区；
- **Script**：`experiments/experiment.lua`、Reload script（schema + palette）。
- **Inspect**：选中像素的轴值/状态/通道值；所有模型通用。

Angle/Mass 是双摆专用预设；在 map 模型上点击会先切回双摆。
面板通过 `dp_query params|axes|channels|models|model|view|integration|palette|selected` 拉 schema，
不把模型字段硬编码进 Tcl。

## 10b. 实验保存/加载

`.dps` 是行格式（`key=value`），保存模型/轴/范围/参数/res/dt/steps/通道/
后处理/palette/Grid/Periodic/Circle/CPU-GPU 后端/视图中心与 zoom。

- GUI：Script 页 Save/Load，原生 `GetSaveFileNameA/GetOpenFileNameA`；
- CLI：`/save path`、`/load path`（可和 `/shot` 组合做回归）；
- 加载：按 model id 重建 Experiment/Atlas/检查器，应用 schema 默认后用文件覆盖值，
  通过 `panel_eval("rebuild_schema")` 刷新 Tcl；
- custom 模型需要当前 Lua 仍定义它；GPU 后端请求失败自动回退 CPU。
- 自检覆盖 save/load roundtrip；CLI save→load 的离屏图 hash 一致。

## 11. Lua schema + palette

`experiments/experiment.lua` 现在同时提供 schema 元数据和调色板：

```lua
M.models.double_pendulum = {
    title = "Double pendulum",
    axes = { "theta1", "theta2" },
    params = { { name="theta1", label="θ1", unit="rad",
                 default=0.5, min=-math.pi, max=math.pi,
                 step=0.01, axis=true }, ... },
    channels = { { name="energy", label="energy", unit="J",
                   min=-50, max=50, cyclic=false }, ... },
}
function M.schema(id) return M.models[id] end
```

- Model 仍拥有动力学/状态/step/observe；Lua 是 **metadata override**，按 name 匹配
  C 参数/通道，不能新增字段或重排。
- `lua_hot_schema()` 解析成 `Schema`；`exp_apply_schema()` 覆盖标题、label/unit/
  range/default/axis 和通道元数据。
- 启动/模型切换时 `defaults=1`；F5 时 `defaults=0`，保留参数值和用户轴，更新元数据并裁剪范围。
- F5 会重建 atlas，Tcl 的 `act reload` 调 `rebuild_schema`；已实测 θ1/θ2→A1/A2 后
  参数页立即更新。
- 参数编辑仍不 reload；只有 schema/palette 变化才 F5。

表达式模型/CPU/GLSL 代码生成是 M5；当前每像素不解释 Lua。

## 12. 打包

`build.ps1` 六步；C 24 个对象；Tcl/Tk 动态，Lua/Fortran 静态。
TCL_LIBRARY/TK_LIBRARY 指向 exe 目录下 lib/，不解包、不写 LOCALAPPDATA。
zip 约 3.7 MB，exe 约 802 KB。

## 13. 物理与数值边界

- 双摆 RK4、能量、C/Fortran 参考对照保留；
- Logistic/Hénon 有已知不动点/有界性/Lyapunov 测试；
- map 的 `dt` 不参与迭代，`Steps/frame` 是每帧迭代次数；
- map 的 Lyapunov 是有限时间平均，不是收敛证明；
- Hénon 暂未算 Lyapunov（需要切向量/QR），只显示状态/半径。

已知限制：

- 只有三个内置模型，无 Lua 表达式 DSL；
- GUI 采样 CPU，GPU 只显示；
- map 只有面板 Inspect，没有独立窗口 inspector；
- 无文件选择对话框、实验保存/加载；PNG 只包含 atlas；
- 能量范围固定；极端参数会饱和；
- 无 3D 切片/分岔连续化。

## 14. 路线

- M4a（已完成）：colorbar、Inspect、轴读数、PNG/CSV 导出；
- M4b（已完成）：Lua schema 元数据 + F5 热重载；
- M5a（已完成）：Lua 2D map 表达式 → CPU 字节码；
- M5b（已完成）：GLSL 代码生成 + GPU 单格验证；
- M6（已完成）：GPU compute atlas + CPU 回退（自定义模型）；
- M7a（已完成）：.dps 实验保存/加载 + 文件对话框 + CLI；
- 后续：控制流/ODE 表达式、全模型 GPU、地图轴刻度。
- M6：GPU compute atlas、FTLE/盆地、Poincaré、切片；
- 可选：map inspector、Hénon Lyapunov（切向量+QR）。