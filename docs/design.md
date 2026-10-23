# 设计说明

## 1. 目标与约束

把单文件 raylib 版（`Double Pendulum Phase Space - Multi threaded - v1.2.cpp`，443 行）
重构成一个原生 Windows 程序，同时满足：

1. 只用 `C:\MinGW` 提供的前端和内建库；原生 PE、静态链接、0 个随程序分发的 DLL；
   一个构建脚本、一个二进制。
2. 按模块拆分；raylib 相关代码全部重写（这里直接用 Win32 + WGL + OpenGL）。
3. 只保留左侧相空间；窗口是正方形。
4. C 做骨架；两种计算内核：GLSL 计算着色器（默认）与 Fortran RK4（回退），
   Fortran 负责定长循环的密集计算。
5. 热逻辑用 Lua。

## 2. 模块边界

```
WinMain (src/main.c)
  │  命令行 / 消息循环
  └─ App (src/app.c)
       ├─ Platform (src/platform.c)   Win32 窗口、QPC、控制台、RCDATA
       ├─ LuaHot   (src/lua_hot.c)    hot.lua：默认值、调色板、步进、按键、标题
       ├─ Sim      (src/sim.c)        网格状态 + 线程池
       │    ├─ ThreadPool (src/pool.c)
       │    └─ Fortran kernel (fortran/kernel.f90)      ← CPU 回退
       └─ Gpu      (src/gpu.c)        WGL 4.3 core + compute/display  ← 默认
            └─ shaders/field.comp, display.vert, display.frag
```

分层原则：

* `sim.c` 不认识窗口，也不认识 GL；它只维护 `float state[4*n*n]` 与 `float field[n*n]`。
* `gpu.c` 不认识 Lua 和 App；它只接收状态、参数、调色板，画到当前窗口。
* `lua_hot.c` 只通过返回值和少量 `host.*` 查询和外界交互，不直接碰物理数组。
* `app.c` 是唯一知道“什么时候调哪个内核、什么时候上传/取回”的地方。

## 3. 数据流

```
            ┌──────────────── GPU ────────────────┐
state ─────►│ SSBO (vec4×n²) → field.comp 子步循环 │──► R32F 场纹理 ─┐
            └─────────────────────────────────────┘                 │
            ┌──────────────── CPU ────────────────┐                 ├─► 1024×1 LUT ─► 窗口
state ─────►│ 线程池 → Fortran 行带 RK4 → field[] │──► R32F 上传 ────┘        ▲
            └─────────────────────────────────────┘                          │
                                                                    Lua hot.palette()
```

场值只有一个分量：

```
u = fract( (θ1 + θ2) / (4π) + 0.5 )
```

原版颜色是 `HSV((hue1 + hue2) / 2, 0.8, 1.0)`，其中
`hue_i = (θ_i + π) / (2π) * 360`；两个 hue 的平均正好只依赖 `θ1 + θ2`，
所以整张相空间可以用**一维** 1024 项调色板表示。这样颜色逻辑集中在 Lua 一处，
CPU 与 GPU 用同一张表，切换内核/调色板都不需要改核。

## 4. 物理内核

与原版逐项一致的双摆方程（阻尼按原版的 `-c·ω` 处理）：

```
δ  = θ1 − θ2
D  = m1 + m2·sin²δ
θ̇1 = ω1
θ̇2 = ω2
ω̇1 = [ m2·g·sinθ2·cosδ − m2·sinδ·(l1·ω1²·cosδ + l2·ω2²) − (m1+m2)·g·sinθ1 ] / (l1·D) − c·ω1
ω̇2 = [ (m1+m2)·(l1·ω1²·sinδ − g·sinθ2 + g·sinθ1·cosδ) + m2·l2·ω2²·sinδ·cosδ ] / (l2·D) − c·ω2
```

RK4 固定四阶段；每步后把 `θ1, θ2` 归一化到 `[-π, π)`。
`dt`、质量、长度、重力、阻尼全部由 Lua 每帧推送（`hot.params()`），
所以改参数不需要重编译。

C 侧保留一份 `physics.c` 的参考 RK4（double + libm），只用于自测；
同文件的场值/着色函数供 CPU 路径与 `/shot` 使用。主计算路径只有 Fortran 或 GLSL。

## 5. GLSL 计算着色器（默认）

### 5.1 上下文

`gpu.c` 先建一个临时上下文取 `wglCreateContextAttribsARB`，请求
**OpenGL 4.3 core**（`WGL_CONTEXT_CORE_PROFILE_BIT_ARB`）。成功后加载本程序需要的最小
GL 4.3 函数表（MinGW 自带的 `GL/glext.h` 是 2000 年的 SGI 版本，没有 compute shader，
所以 `src/gl43.h` 自带一份声明）。失败则退回普通上下文 + 固定管线，见第 9 节。

### 5.2 片上资源

* **SSBO**：`layout(std430, binding=0) buffer StateBuf { vec4 cells[]; }`，
  每格 `(θ1, θ2, ω1, ω2)`。
* **场纹理**：`R32F`，计算着色器用 `imageStore` 直接写，显示着色器直接采样；
  正常帧没有任何 `glReadPixels`/PBO 回读。
* **shared memory**：每个 16×16 workgroup 先把本块状态搬进
  `shared vec4 tile[16][16]`（4 KB），整帧的所有子步都在 shared 上跑完，
  最后只回写一次 SSBO。这样每帧的 SSBO 读写从“每子步一次”降到“每帧一次”。
* **调色板**：1024×1 RGBA8 纹理，`GL_REPEAT` + 线性过滤，由 Lua 生成、C 上传。

### 5.3 调度

```glsl
layout(local_size_x = 16, local_size_y = 16) in;
...
for (int i = 0; i < uSteps; ++i) {
    vec4 k1 = deriv(s);
    vec4 k2 = deriv(s + 0.5 * uDt * k1);
    vec4 k3 = deriv(s + 0.5 * uDt * k2);
    vec4 k4 = deriv(s + uDt * k3);
    s += (uDt / 6.0) * (k1 + 2.0*k2 + 2.0*k3 + k4);
    s.x = norm_angle(s.x);
    s.y = norm_angle(s.y);
    if (any(isnan(s)) || any(isinf(s))) s = vec4(0.0);
}
```

一次 dispatch 处理整帧的 `uSteps` 个子步；dispatch 后
`glMemoryBarrier(SHADER_STORAGE | SHADER_IMAGE_ACCESS | TEXTURE_FETCH)`，
再画全屏三角形。网格不是 16 的倍数时，所有 invocation 都参与 `barrier()`，
越界者写 dummy，避免 workgroup 内提前 return 导致的 barrier 分歧。

### 5.4 显示

* 顶点着色器用 `gl_VertexID` 生成全屏三角形，不建 VBO；只建一个空 VAO。
* `v = 0` 对应窗口顶边，和网格 `y = 0` 一致。
* 片段着色器：`u = texture(uField, uv).r; color = texture(uLut, vec2(fract(u), 0.5)).rgb;`
* CPU 内核也把 `field[]` 上传成同一张 R32F 纹理，所以现代显示路径只有一种。

## 6. Fortran 行带内核（回退）

### 6.1 ABI

```fortran
subroutine dp_step_band(state, n, y0, y1, steps, m1, m2, l1, l2, g, damp, dt) &
           bind(C, name="dp_step_band")
```

C 传入 `float state[4*n*n]`（行主序）和本次负责的行区间 `[y0, y1)`。
每个 worker 只碰自己的行带，互不重叠。

### 6.2 为什么不是直接调 sin/cos

在这台机器上（1.7–1.9 GHz 的节能状态），msvcrt 的 `sin` 约 **115 ns/次**。
原版每步每格 4 个阶段、每阶段 4 次三角函数，16 次调用会把一个 RK4 子步推到微秒级。
Fortran 内核改了两件事：

1. **1024 项 sin/cos 表 + 小角重构**：表项在 L1（16 KB），
   `i = nint(x / Δ)`，余量 `r` 用 1−r²/2、r−r³/6 重构；截断误差在 1e-10 量级，远低于 float32 存储的 1e-7。
2. **和差化积**：`sinδ/cosδ` 由已算出的 `sinθ1,cosθ1,sinθ2,cosθ2` 得到：
   `sinδ = s1·c2 − c1·s2`，`cosδ = c1·c2 + s1·s2`。
   每阶段从 3 次查表降到 2 次。

角度归一化也不调 `fmod`：`k = int(θ/2π)`，`θ -= k·2π`，再做一次 ±2π 修正；
配 `|θ| > 1e6` 和 NaN 护栏。

### 6.3 定长循环与内联

* RK4 的 4 个阶段手工展开，每阶段固定 4 个标量导数（`dth1,dth2,dom1,dom2`），没有循环。
* `deriv` 是 host-associated 内部子程序；构建时 `-finline-limit=4000` 保证它被内联。
  可以用 `objdump -t` 验证 `kernel_fortran_f.o` 里没有 `_deriv` 符号。
* 全程 double 计算（表查询、和差化积、RK4、归一化），最后才写回 float，
  与 C 参考的单步偏差约 2.4e-7（来自 float 存储）。
* 编译参数：`-O3 -finline-limit=4000 -funroll-loops -ftree-vectorize
  -msse2 -mfpmath=sse -fno-math-errno`。

### 6.4 线程

`pool.c` 是固定线程池：

* 每个 worker 一对自动复位事件 `go`/`done`；
* 主线程按行数均分 `[y0,y1)`，`SetEvent` 全部 worker，`WaitForMultipleObjects` 等齐；
* worker 之间没有共享写，行带按格独立，所以 1 线程和 N 线程结果**逐位相同**（自测覆盖）。

线程数默认取逻辑处理器数（本机 8），Lua 的 `sim.threads` 可以覆盖。

## 7. Lua 热逻辑

### 7.1 契约

脚本返回一个表 `hot`：

| 函数 | 返回 | 调用时机 |
| --- | --- | --- |
| `defaults()` | 参数表 | 启动/重载 |
| `palette()` | 3072 个 0..255 整数 | 启动/重载/切调色板 |
| `params()` | `m1,m2,l1,l2,g,damping,dt,max_steps` | 每帧 |
| `tick(dt)` | 本帧子步数 | 每帧 |
| `key(name, down)` | `reset/pause/palette/kernel` 或 nil | 按键 |
| `title(kernel, fps, steps)` | UTF-8 标题 | 每 0.25 s |
| `reset()` | — | 网格重置后 |

宿主只暴露 `host.kernel() / host.res() / host.log(s) / host.time()`。
Lua 状态只开 `base, table, string, math`，不开 `io/os/package/debug`。

### 7.2 步进策略

`tick` 里是固定步长累加器：

```lua
M.acc = M.acc + dt
local n = 0
while M.acc >= step and n < cap do
    M.acc = M.acc - step
    n = n + 1
end
if M.acc > step then M.acc = 0 end   -- 跟不上了就丢弃欠账
return n
```

CPU 回退时把每帧上限压到 4，避免一帧里堆出长停顿；GPU 用 8。
所有上限在 C 侧还会再夹一次（0..32），脚本写错不会把进程带崩。

### 7.3 热重载

`F5`：优先读 exe 同目录的 `hot.lua`，没有就读 RCDATA 里的副本。
重载在**新的 `lua_State`** 里执行；失败时关闭新状态、保留旧状态。
`host.*` 的 upvalue 指向稳定的 `LuaHot`，不随 `lua_State` 一起换掉。

重载成功后：

* 物理参数、`dt`、上限、调色板、按键、标题立即生效；
* `res` / `range` / 初始角速度变化会重建 `Sim` 和 GPU 资源并重置；
* 其余情况保留当前状态，等玩家按 `R`。

## 8. 帧流水线

```
app_frame()
  dt = platform_dt()                       // QPC，夹 0..0.25 s
  params = lua.params(); steps = lua.tick(dt)
  if steps > 0:
      GPU:  gpu_step(steps)                // 一次 dispatch 跑完
      CPU:  sim_step_cpu(steps)            // 线程池 + Fortran
            gpu_upload_field() 或 legacy 上传
  gpu_draw(w, h)
  platform_frame_end()                     // SwapBuffers / 无 vsync 时 60 Hz 限速
  fps / title 每 0.5 s / 0.25 s 更新
```

内核切换（`K`）：

* GPU → CPU：`glGetBufferSubData` 取回状态，重算 field，再按显示路径上传；
* CPU → GPU：把 CPU 状态 `glBufferSubData` 上去，dispatch 0 步刷新场纹理。

正常帧不回读；只有切内核、`/shot`、`/gpu-selftest` 和 `/capture` 会回读。

## 9. 回退矩阵

| 环境 | 计算 | 显示 | 结果 |
| --- | --- | --- | --- |
| GL 4.3 core + compute 编译成功 | GLSL | 现代 shader | 默认，GPU |
| 有 4.3 但强制 `/cpu` | Fortran | 现代 shader | CPU 计算，GPU 上屏 |
| 没有 4.3 / `/legacy` | Fortran | GL 1.x 固定管线 + RGBA8 纹理 | 兼容 |
| 只有 GL 1.1 软件渲染 | Fortran | 固定管线，纹理回退 `GL_CLAMP` | 最慢但能跑 |
| `/gpu` 但没有 compute | — | — | 启动报错 |

窗口本身是必须的（OpenGL 需要一个 drawable）；计算和显示都不可用时程序无法上屏，
这属于 Windows 上没有可用 `opengl32` 的病态环境。

## 10. 窗口与显示

* 客户区正方形：取工作区短边 78%，夹在 480–1000；外框尺寸用 `AdjustWindowRectEx` 反算。
* 不可缩放（`WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX`），保证始终正方形。
* 清单：Per-Monitor V2 DPI + Win10 supportedOS + longPathAware；
  `WM_DPICHANGED` 直接返回 0，不让系统把窗口改成非正方形。
* `wglSwapIntervalEXT(1)` 开垂直同步；没有该扩展时用 QPC + `timeBeginPeriod(1)` 限到 60 Hz。
* 标题栏：`Double Pendulum Phase Space | GPU compute | 144 fps | 256x256 | phase`，
  由 Lua 的 `hot.title` 生成；暂停时显示 `phase | paused`。

## 11. 构建与资源

`build.ps1` 五步：

1. 把 `shaders/*.glsl` 转义成 `build/gen/shaders_gen.h` 里的 C 字符串；
2. `gcc` 编译 11 个 C 模块；
3. `gfortran` 编译 `fortran/kernel.f90`；
4. `windres` 编 `src/res/app.rc`：`1001 RCDATA "lua/hot.lua"`、DPI 清单、版本信息；
5. `gfortran` 链接：

```
-mwindows -static -static-libgcc -static-libgfortran
-Wl,--gc-sections -Wl,--enable-stdcall-fixup -s
-lopengl32 -lgdi32 -luser32 -lwinmm -llua -lm
```

链接后用 `objdump -p` 审计导入表，出现非系统 DLL 直接失败；随后跑 `/selftest`
和 `/gpu-selftest`。

## 12. 与原始单文件的差异

| 原版 | 重构后 |
| --- | --- |
| 443 行单文件 C++ | 11 个 C 模块 + Fortran + GLSL + Lua + 资源 |
| raylib 的窗口/纹理/文字 | Win32 + WGL + GLSL/PBO；无 raylib |
| 左侧相空间 + 右侧双摆 + 右下数值 | 只有相空间 |
| 1024×512 窗口 | 正方形客户区 |
| 每帧 `std::thread` × 16 | 常驻线程池 + 行带 |
| CPU 单线程逐格 RK4 + libm sin/cos | GLSL 默认；Fortran 查表 sincos + 和差化积 |
| 逐格 `ColorFromHSV` | Lua 1024 项 LUT，CPU/GPU 共用 |
| 编译期 `Configuration` | Lua 运行期配置，F5 热重载 |
| 点采样放大 | GPU 线性过滤（现代路径） |
| 无暂停/调色板/内核切换 | Space / C / K / F5 |

## 13. 已知取舍

* **32 位**：`C:\MinGW` 是 MinGW.org 的 i686 工具链，exe 是 PE32；
  `-msse2` 是硬要求（任何 Windows 10 机器都满足）。
* **GPU/CPU 数值分叉**：GPU float32、Fortran double；混沌系统下约 20 步后单格轨迹分叉。
  大尺度结构（对角带、四瓣、颜色统计）一致，见验证记录。需要严格一致时用 `/cpu`。
* **兼容路径滤波差异**：现代路径是“场纹理 + LUT”两段采样，兼容路径是“已着色 RGBA 纹理”一段采样；
  512×512 下 22% 像素有平均 1.5/255 的差，视觉一致。
* **CPU 回退的帧率**：256×256×1 步约 7–10 ms（8 线程），
  按真实时间 10 步/秒推进；低核机器会更慢，但不会雪崩（累加器丢欠账）。
* **没有屏幕文字**：按“只保留相空间”的要求，FPS/内核/调色板都放在标题栏。
* **没有图标**：资源里只有清单、版本和 hot.lua；exe 用系统默认图标。