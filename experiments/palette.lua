-- palette.lua - shared palette + experiment metadata
--
-- This milestone keeps the model and parameter schema in C (double pendulum is
-- the built-in example).  The Lua script owns the palette and experiment
-- metadata; parameter values are edited in the panel and applied without a
-- reload.  Reload (F5) only re-reads this file.
--
-- The next milestone will let experiment scripts define params/axes/channels and
-- expression models; this palette is shared by the built-in models.

local M = {}

M.title = "Palette"
M.scheme = "phase"
M.schemes = { "phase", "ember", "ice", "mono" }
M.meta = {
    model = "all",
    x = "theta1",
    y = "theta2",
    channels = { "theta1", "theta2", "energy", "omega_max" },
}

local TWO_PI = 2 * math.pi

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

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
        else
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

function M.status()
    return false, M.scheme
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

-- lua_hot_create currently validates defaults(); keep the table shape so the
-- palette host can create the state.  The app does not apply these values.
function M.defaults()
    return {
        physics = { m1 = 1.0, m2 = 1.0, l1 = 1.0, l2 = 1.0, g = 9.81, damping = 0.0 },
        grid = { res = 64, range = 2 * math.pi, omega1 = 0.0, omega2 = 0.0 },
        sim = { dt = 0.01, max_steps = 8, threads = 0 },
    }
end

return M