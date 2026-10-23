# 验证记录：三模型通用地图实验箱

本机：Windows 10，1920×1080，125% 缩放；MinGW.org GCC 6.3、gfortran 6.3、
Tcl/Tk 8.6.0、Lua 5.2、NVIDIA GTX 1650 Ti。

## 1. 构建

```text
[3/6] compiling C skeleton (21 objects)
[6/6] linking (dynamic Tcl/Tk, static C/Fortran)
OK -> dist/dp-phase/dp-phase.exe (763,392 bytes)
      staged 10.0 MB
      dist/dp-phase-1.6.0.zip (3.6 MB)
```

| 项目 | 值 |
| --- | --- |
| exe | 763,392 B |
| zip | 约 3.7 MB（docs 改动会改变 zip 字节数） |
| 导入表 | KERNEL32/USER32/GDI32/OPENGL32/WINMM/MSVCRT + tcl86.dll/tk86.dll（zlib 静态） |
| 随包 DLL | tcl86、tk86、libgcc_s_dw2-1、zlib1 |
| 脚本 | lib/tcl8.6、lib/tk8.6、ui/panel.tcl、experiments/palette.lua |

（GNU ld 的 PE 时间戳使每次链接的 SHA256 不同，不固定哈希。）

## 2. 自检

```text
-- experiment / atlas
  [ok]   double-pendulum schema has named parameters
  [ok]   axes can be switched to masses
  [ok]   atlas cell preserves per-pixel masses
  [ok]   rest energy matches m g l
  [ok]   RK4 conserves energy for damping=0
-- map models
  [ok]   three models are registered
  [ok]   logistic r=2.5 converges to 1-1/r
  [ok]   logistic r=4 Lyapunov ~= ln 2
  [ok]   henon state stays bounded
  [ok]   logistic parameter map is non-uniform
selftest: 76 passed, 0 failed
```

`/gpu-selftest`：**82/82**（76 CPU + 旧 GPU 回归）。
GUI 采样仍是 CPU；GPU 只负责 atlas RGBA 显示和线叠加。

## 3. 双摆三窗口

![三窗口](shot-windows.png)

- 默认 θ1/θ2 角度图，64×64；
- 右键中心格，双摆检查器显示 cell、m1/m2、θ/ω、damping、energy；
- 参数页在 1080p 下完整显示 10 个参数（面板客户区 620）。

![双摆检查器](shot-pendulum.png)

## 4. Logistic map

![Logistic map](shot-logistic.png)

- 默认 r/x0 轴，r∈[0,4]，x0∈[0,1]；
- 每像素 200 步（离屏）/连续帧（GUI）；
- Color by = x 时是经典 Logistic 参数图：周期窗、混沌带、边界清晰；
- Color by = lyapunov 时 r=4 附近约 ln 2。

自检：

- r=2.5 从 x0=0.5 收敛到 1-1/r=0.6；
- r=4,x0=0.3,1000 步 → 状态在 [0,1]，λ≈0.693；
- atlas 的 lyapunov 通道在 r 方向非均匀。

## 5. Hénon map

![Hénon map](shot-henon.png)

- 默认 a/b 轴，a=1.4,b=0.3,x0=y0=0；
- Color by = x，64×64，连续帧；
- 自检：1000 步后状态有限、|x|<2、|y|<1，radius 通道有效。

## 6. 模型切换

面板标题下 Model 下拉：

- 启动默认 Double pendulum；
- 选 Logistic map：
  - 相空间标题变为 `Logistic map Atlas | CPU | ... | x`；
  - 双摆窗口消失；
  - Color by 变为 x，参数页只剩 r/x0；
  - Last: Logistic map；
- 选回 Double pendulum 会重新创建双摆窗口。

已用真实鼠标点击 combo popdown 的实测输出：

```text
after open: popdown
pop rect 804,456 301x67
titles after:
Control Panel
Logistic map Atlas | CPU | 122 fps | 64x64 | x
```

这里修掉了一个关键 bug：程序化销毁双摆窗口时 `WM_DESTROY` 会调用 closefn，
导致一次模型切换直接退出程序。现在 `pendulum_view_destroy` 先清空 closefn/keyfn。

![模型面板](shot-model-panel.png)

## 7. 面板参数页

![参数页](shot-panel-params.png)

### 7.1 颜色条 / Inspect / 导出

![View 颜色条](shot-panel.png)

- 颜色条来自当前调色板 LUT，标签/单位/范围来自模型通道；
- 轴读数 `X theta1 [-3.142, 3.142]  Y theta2 [-3.142, 3.142]`；
- 右键选中后 **Inspect** 页显示 cell、x/y、状态、通道；
- 这是 Logistic/Hénon 的检查器，不再需要独立窗口。

![Inspect](shot-panel-inspect.png)

导出实测（双摆 64×64）：

```text
exports/atlas-20261024-011301.png  7,163 B  PNG signature 89 50 4E 47, 64×64
exports/atlas-20261024-011302.csv  567,991 B
model,ix,iy,x_theta1,y_theta2,state_theta1,state_theta2,...,ch_theta1,ch_theta2,...
```

PNG 是 zlib 压缩的最小 RGBA writer；CSV 列有 `x_`/`y_`/`state_`/`ch_` 前缀，
避免模型同名通道混淆。导出固定到 exe 旁 `exports/`，面板 Last 显示路径。

- X/Y 轴 combo；
- 范围、Res、dt、Steps/frame；
- 双摆 10 个参数两列布局；
- Apply parameters；
- 面板高度从 540 提到 620，解决模型行加入后 l2/g/damping 被裁切的问题。
- View 页加入颜色条、轴读数和 Export PNG/CSV；选中值移到独立 **Inspect** 页，避免 View 高度溢出。

## 8. 性能

- 双摆 64×64：142–147 fps；
- Logistic/Hénon 64×64：140+ fps；
- 64×64 双摆每步约 2 ms（8 线程）；
- 256×256 CPU 仍可算，但约 16 倍成本，GPU compute 未接 GUI。

## 9. 本次修掉的问题

| 问题 | 根因 | 修复 |
| --- | --- | --- |
| 通用 Atlas 质量/颜色错误 | 把 double state/vals 用 memcpy 写进 float cells | 逐元素转换 |
| 模型切换后程序退出 | 销毁双摆窗口触发 closefn → app 退出 | destroy 前清空 closefn/keyfn |
| 模型下拉只显示 “Double” | models 列表里 title 未加花括号，Tcl lassign 截断 | 返回 `{id {title}}` |
| 参数页底部被裁 | 新增 Model 行占用高度 | 面板客户区 620；参数页完整 |
| 模型切换后双摆窗口不复现 | 只在 app_create 创建 pview | app_set_model 中按模型创建/销毁 |
| 颜色条 Tcl 报错 | ttk::canvas 不存在 | 改用经典 canvas |
| 轴读数/选中值不更新 | Tcl 双引号里 `[%.4g...]` 被当命令 | 用 `format {..}` + 分离 catch |
| 选中值控件路径写错 | `.sel.l` vs `.self.l` | 改为 Inspect 页 `.f.nb.insp.txt` |
| CSV 列名冲突 | x/y/state/channel 同名 | 加 `x_/y_/state_/ch_` 前缀 |
| Lua 脚本名与多模型不符 | 原 double-pendulum.lua | 改为共享 experiments/palette.lua |

## 10. 已知边界

1. Lua 仍是 palette/meta，不是模型 DSL。
2. 只有三个内置模型，无表达式模型。
3. GUI 采样 CPU；GPU compute 未接。
4. Logistic/Hénon 只有面板 Inspect，没有独立窗口 inspector。
5. 没有文件选择对话框、实验保存/加载；PNG 只包含 atlas。
6. Hénon 未算 Lyapunov；map 的 Lyapunov 是有限时间平均。
7. 没有 3D 切片/连续化。

## 11. 复现

```powershell
pwsh -File build.ps1
.\dist\dp-phase\dp-phase.exe
.\dist\dp-phase\dp-phase.exe /model logistic
.\dist\dp-phase\dp-phase.exe /model henon
.\dist\dp-phase\dp-phase.exe /selftest
.\dist\dp-phase\dp-phase.exe /gpu-selftest

# 离屏
.\dist\dp-phase\dp-phase.exe /shot logi.bmp /model logistic /steps 200 /channel lyapunov
.\dist\dp-phase\dp-phase.exe /shot henon.bmp /model henon /steps 200 /channel radius
```