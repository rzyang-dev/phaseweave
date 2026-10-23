-- hot.lua - 热逻辑（编进 exe 的 RCDATA；同目录放一份 hot.lua 可覆盖，F5 重载）
--
-- 这里负责所有"每次运行都可能改"的东西：
--   * 物理/网格/步进默认值
--   * 每帧推进多少子步（带累加器和上限，掉帧时不会越积越多）
--   * 颜色查找表（1024 项，GPU 和 CPU 用同一张表）
--   * 按键 -> 动作，窗口标题
-- 计算本身不在这里：GLSL 计算着色器（默认）或 Fortran 行带内核（回退）。

local M = {}

M.cfg = {
    physics = {
        m1 = 1.0, m2 = 1.0,
        l1 = 1.0, l2 = 1.0,
        g = 9.81,
        damping = 0.0,
    },
    grid = {
        res = 256,
        range = 4 * math.pi,
        omega1 = 0.0,
        omega2 = 0.0,
    },
    sim = {
        dt = 0.1,
        max_steps = 8,
        threads = 0,          -- 0 = 用满逻辑核
    },
}

M.schemes = { "phase", "ember", "ice", "mono" }
M.scheme = "phase"
M.paused = false
M.acc = 0.0

local TWO_PI = 2 * math.pi

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- HSV -> RGB，s=0.8 / v=1.0 时与原始程序的 ColorFromHSV 观感一致
local function hsv(h, s, v)
    h = h % 360
    local c = v * s
    local x = c * (1 - math.abs((h / 60) % 2 - 1))
    local r, g, b = 0, 0, 0
    if h < 60 then       r, g, b = c, x, 0
    elseif h < 120 then  r, g, b = x, c, 0
    elseif h < 180 then  r, g, b = 0, c, x
    elseif h < 240 then  r, g, b = 0, x, c
    elseif h < 300 then  r, g, b = x, 0, c
    else                 r, g, b = c, 0, x
    end
    local m = v - c
    return (r + m) * 255, (g + m) * 255, (b + m) * 255
end

-- 返回 1024*3 个 0..255 的整数；C 侧先校验长度再逐项夹取
function M.palette()
    local t = {}
    local scheme = M.scheme
    for i = 0, 1023 do
        local u = i / 1024
        local r, g, b
        if scheme == "phase" then
            r, g, b = hsv(u * 360, 0.8, 1.0)
        elseif scheme == "ember" then
            local k = 0.5 + 0.5 * math.cos(TWO_PI * u)
            r = 255 * (0.10 + 0.90 * k)
            g = 255 * (0.02 + 0.55 * k * k)
            b = 255 * (0.01 + 0.10 * k * k * k)
        elseif scheme == "ice" then
            local k = 0.5 + 0.5 * math.sin(TWO_PI * u)
            r = 255 * (0.02 + 0.35 * k * k)
            g = 255 * (0.08 + 0.60 * k)
            b = 255 * (0.15 + 0.85 * k)
        else -- mono
            local k = 0.5 + 0.5 * math.cos(TWO_PI * u)
            local v = 0.05 + 0.95 * k
            r, g, b = v * 255, v * 255, v * 255
        end
        t[#t + 1] = math.floor(clamp(r, 0, 255) + 0.5)
        t[#t + 1] = math.floor(clamp(g, 0, 255) + 0.5)
        t[#t + 1] = math.floor(clamp(b, 0, 255) + 0.5)
    end
    return t
end

function M.params()
    local p = M.cfg.physics
    local s = M.cfg.sim
    return p.m1, p.m2, p.l1, p.l2, p.g, p.damping, s.dt, s.max_steps
end

-- 固定步长累加器。CPU 回退时把每帧上限压到 4，避免一帧里堆出可见的长停顿。
function M.tick(dt)
    if M.paused then return 0 end
    local step = M.cfg.sim.dt
    local cap = M.cfg.sim.max_steps
    if host.kernel() == "cpu" and cap > 4 then cap = 4 end

    M.acc = M.acc + dt
    local n = 0
    while M.acc >= step and n < cap do
        M.acc = M.acc - step
        n = n + 1
    end
    -- 跟不上了就丢弃欠账，宁可慢放也不要雪崩
    if M.acc > step then M.acc = 0 end
    return n
end

function M.reset()
    M.acc = 0
end

function M.status()
    return M.paused, M.scheme
end

function M.set_scheme(name)
    for _, s in ipairs(M.schemes) do
        if s == name then
            M.scheme = s
            return true
        end
    end
    return false
end

function M.key(name, down)
    if not down then return nil end
    if name == "R" then
        M.acc = 0
        return "reset"
    elseif name == "SPACE" then
        M.paused = not M.paused
        return "pause"
    elseif name == "C" then
        for i, s in ipairs(M.schemes) do
            if s == M.scheme then
                M.scheme = M.schemes[i % #M.schemes + 1]
                break
            end
        end
        return "palette"
    elseif name == "K" then
        return "kernel"
    end
    return nil
end

function M.title(kernel, fps, steps)
    local label = (kernel == "gpu") and "GPU compute" or "Fortran CPU"
    local state = M.scheme
    if M.paused then state = state .. " | paused" end
    return string.format("Phase Space | %s | %d fps | %dx%d | %s",
                         label, fps, M.cfg.grid.res, M.cfg.grid.res, state)
end

function M.defaults()
    return M.cfg
end

return M