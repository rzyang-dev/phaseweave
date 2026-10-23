# 双摆相空间 · Double Pendulum Phase Space

把工作区里的[原始单文件](<../Double Pendulum Phase Space - Multi threaded - v1.2.cpp>)重构成一个原生 Windows 程序：
**C 骨架 + GLSL 计算着色器（默认）+ Fortran RK4（回退）+ Lua 热逻辑**。
只保留左侧相空间，窗口是正方形；一个构建脚本，一个静态链接的 PE。

![运行中的窗口](docs/shot-window.png)

## 交付物

| 项目 | 位置 | 说明 |
| --- | --- | --- |
| 二进制 | [dist/dp-phase.exe](dist/dp-phase.exe) | PE32 GUI，269,312 字节，静态链接 |
| 构建脚本 | [build.ps1](build.ps1) | 唯一构建入口：编译 C/Fortran、嵌入着色器与 hot.lua、链接、导入表审计、自测 |
| 源码 | [src/](src/) · [fortran/](fortran/) · [shaders/](shaders/) · [lua/](lua/) | 约 3.6k 行 |
| 说明 | [docs/design.md](docs/design.md) · [docs/verification.md](docs/verification.md) | 设计、契约、实测记录 |

导入表只有 6 个 Windows 自带 DLL：

```
KERNEL32.dll  USER32.dll  GDI32.dll  OPENGL32.dll  WINMM.dll  msvcrt.dll
```

没有 raylib、lua52.dll、libgfortran-3.dll、libgcc_s_*.dll 或 VC 运行库；Lua 5.2、
libgfortran、libgcc 都是静态链进去的。`hot.lua`、着色器、清单和版本信息都编在 exe 的资源里，
所以只复制 `dp-phase.exe` 也能运行。

## 构建

只需要 `C:\MinGW`（MinGW.org GCC 6.3.0）：

```powershell
cd pendulum-phase
pwsh -File build.ps1
```

脚本用到的工具链前端：

| 工具 | 用途 |
| --- | --- |
| `gcc` 6.3.0 | C 骨架、Win32/WGL 平台层、Lua 宿主 |
| `gfortran` 6.3.0 | `fortran/kernel.f90` 的 RK4 行带内核 |
| `windres` | 嵌入 `lua/hot.lua`（RCDATA）、DPI 清单、版本信息 |
| `objdump` | 链接后审计导入表 |

其它参数：`-Clean` 清空 `build/`、`dist/`；`-NoTest` 跳过自测；`-NoGpuTest` 跳过 GPU 自测。
工具链不在 `C:\MinGW` 时设置 `MINGW_ROOT`。

构建完成后会自动跑 `/selftest`，再跑 `/gpu-selftest`（没有 GPU 时只告警，不影响构建）。

## 运行

```powershell
.\dist\dp-phase.exe                 # 窗口模式：GPU 计算着色器优先，不可用则 Fortran
.\dist\dp-phase.exe /cpu            # 强制 Fortran 行带内核
.\dist\dp-phase.exe /legacy         # 强制兼容 GL 上下文（验证回退显示路径）
```

窗口客户区是正方形：取工作区短边的 78%，夹在 480–1000 像素之间；1000 像素以上的窗口
再从 256×256 的相空间场线性放大。没有右侧双摆示意、没有右下状态文字，整块窗口就是相空间。

### 按键

| 按键 | 动作 |
| --- | --- |
| `R` | 重置相空间网格 |
| `Space` | 暂停 / 继续 |
| `C` | 切换调色板（phase / ember / ice / mono） |
| `K` | 在 GPU 计算与 Fortran CPU 之间切换 |
| `F5` | 重新载入 `hot.lua` |
| `Esc` | 退出 |

### 无窗口模式

```powershell
.\dist\dp-phase.exe /selftest          # CPU 内核、Lua 契约、BMP 编码（36 项）
.\dist\dp-phase.exe /gpu-selftest      # 再跑 GPU 一步同步/多步/取回/离屏绘制（41 项）
.\dist\dp-phase.exe /shot a.bmp /cpu /steps 0
.\dist\dp-phase.exe /shot b.bmp /gpu /steps 120
.\dist\dp-phase.exe /capture c.bmp /gpu /steps 120 /size 512
```

`/shot` 把“状态 + CPU 着色公式”直接写成 256×256 BMP（GPU 模式先取回状态），
适合跨内核逐字节比较；`/capture` 走真实窗口/GL 绘制路径，适合验证显示链路。

### 热逻辑

`hot.lua` 编在 exe 里。把一份同名文件放到 exe 同目录即可覆盖它，改完按 `F5` 重载：

```lua
M.cfg = {
    physics = { m1 = 1.0, m2 = 1.0, l1 = 1.0, l2 = 1.0, g = 9.81, damping = 0.1 },
    grid    = { res = 256, range = 4 * math.pi, omega1 = 10.0, omega2 = 10.0 },
    sim     = { dt = 0.1, max_steps = 8, threads = 0 },
}
```

脚本负责：默认参数、每帧推进多少子步（固定步长累加器，掉帧时丢弃欠账）、
1024 项调色板、按键映射、窗口标题。物理参数和 `dt` 立即生效；
`res` / `range` / 初始角速度改变会重建网格。完整契约见 [docs/design.md](docs/design.md)。

## 代码结构

```
src/main.c            WinMain、命令行、消息循环                      159 行
src/app.c             帧流水线、内核切换、热重载、截图               544 行
src/platform.c        Win32 窗口、QPC、控制台、RCDATA                315 行
src/sim.c             网格状态、线程池调度、场计算                   157 行
src/pool.c            固定线程池（每 worker 一对事件）               113 行
src/kernel_fortran.c  Fortran bind(C) 的薄适配层                      23 行
src/gpu.c             WGL 4.3 core、GLSL 计算/显示、回退、截图       506 行
src/gl43.h            自带的 GL 4.3 最小函数表                        88 行
src/physics.c         C 参考 RK4、场值、调色板、颜色化               144 行
src/lua_hot.c         Lua 5.2 沙箱宿主、热重载                       414 行
src/bmp.c             24 位 BMP 输出                                  62 行
src/selftest.c        无窗口自测                                     445 行
fortran/kernel.f90    RK4 行带内核（查表 sincos、和差化积）          138 行
shaders/field.comp    计算着色器（SSBO + shared + R32F image）        74 行
shaders/display.*    全屏三角形 + 调色板采样                         23 行
lua/hot.lua           热逻辑                                         131 行
src/res/app.rc        资源：hot.lua、清单、版本                       28 行
```

数据通路只有一条：

```
state[4*n*n] --RK4 子步--> 场值 u = fract((θ1+θ2)/(4π)+0.5)
      --Lua 的 1024 项 LUT--> RGB --> 正方形窗口
```

GPU 路径里 `state` 常驻 SSBO、场值直接写 R32F 纹理，正常帧不往 CPU 回读；
CPU 路径由 C 线程池按行带调度 Fortran，结果以同一张 LUT 上屏。

## 验证摘要

本机（Windows 10 19045，GTX 1650 Ti，GCC 6.3.0 / gfortran 6.3.0 / Lua 5.2.0）实测：

* 自测：CPU 36/36，带 GPU 41/41。
* Fortran 单步与 C 参考的最大偏差 **2.37e-7**，10 步 **3.25e-7**；
  1 线程与 4 线程结果**逐位相同**。
* GPU 单步与 C 参考最大偏差 **3.72e-7**；同一次 dispatch 跑 3 子步与分 3 次 dispatch **逐位相同**。
* CPU 与 GPU 的 `/shot` 在 0 步时**逐字节相同**（hash `DC8270B2`），
  1 步时只有 1 个像素差 1，120 步时 61% 像素不同——这是 float32/float64 在混沌系统里的必然分叉，
  大尺度结构仍然一致（见 [docs/verification.md](docs/verification.md)）。
* 性能：256×256 单步，GPU **0.14–0.17 ms**（约 400–470 Mcell-steps/s），
  Fortran 8 线程 **7.7–9.4 ms**（约 7.0–8.5 Mcell-steps/s），GPU 约快 50–65 倍。
* 窗口：客户区 803×803，标题栏显示 `GPU compute | 144 fps | 256x256 | phase`。

![初始相空间](docs/shot-initial.png)
![120 步后](docs/shot-phase.png)

## 已知取舍

* **GPU 与 CPU 不追求长期逐位一致。** GPU 是 float32，Fortran 是 double；
  dt=0.1 的步长下大约 20 步后单格轨迹就会出现可见分叉。场景的统计结构、颜色分布和演化节奏一致，
  但每个格子的相位不能一一对应。需要严格一致时用 `/cpu` 或 `/shot /cpu`。
* **兼容显示路径的滤波不同。** 没有 4.3 core 时用 GL 1.x 固定管线 + 已着色的 RGBA 纹理；
  现代路径是“场纹理 + 调色板”两段采样，所以缩放到 512 时约 22% 像素有 ≤1.5/255 的平均差，
  结构完全一致。真正只有 1.1 的软件渲染器上纹理还会回退到 `GL_CLAMP`。
* **只有相空间。** 右侧双摆动画和右下角数值面板按需求删掉了；窗口标题承担内核/FPS/调色板状态显示。
* **热重载的边界。** 物理参数、`dt`、上限、调色板、按键、标题立即生效；
  分辨率/范围/初始角速度变化会重建网格；其余状态保留到下一次 `R`。
* **CPU 回退的每帧上限是 4 子步**（Lua 里按 `host.kernel()` 收紧），
  避免一帧里堆出长停顿；GPU 路径保持 8。

## 文档

* [docs/design.md](docs/design.md) —— 模块边界、RK4、GPU on-chip 策略、Fortran 内核、Lua 契约、回退矩阵。
* [docs/verification.md](docs/verification.md) —— 构建输出、导入表、自测、跨内核对比、性能与开发中修掉的缺陷。