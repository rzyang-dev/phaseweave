-- experiment.lua - experiment schema + palette
--
-- The Model callbacks (state/step/observe) stay in C.  This file owns the
-- experiment metadata: title, parameter labels/ranges/defaults, axis eligibility,
-- channel labels/ranges and the palette.  F5 re-reads this file; parameter edits
-- in the panel still do not require a reload.
--
-- This is metadata, not an arbitrary expression DSL: names must match the
-- selected built-in Model.  Expression models/codegen are a later milestone.

local M = {}

M.title = "Experiment"
M.scheme = "phase"
M.schemes = { "phase", "ember", "ice", "mono" }
M.meta = {
    model = "all",
    x = "theta1",
    y = "theta2",
    channels = { "theta1", "theta2", "energy", "omega_max" },
}

local PI = math.pi

M.models = {
    double_pendulum = {
        title = "Double pendulum",
        axes = { "theta1", "theta2" },
        params = {
            { name="theta1",  label="θ1",  unit="rad",   default=0.5,  min=-PI, max=PI, step=0.01, axis=true },
            { name="theta2",  label="θ2",  unit="rad",   default=0.0,  min=-PI, max=PI, step=0.01, axis=true },
            { name="omega1",  label="ω1",  unit="rad/s", default=0.0,  min=-10, max=10, step=0.01, axis=true },
            { name="omega2",  label="ω2",  unit="rad/s", default=0.0,  min=-10, max=10, step=0.01, axis=true },
            { name="m1",      label="m1",      unit="kg",    default=1.0,  min=0.2, max=3.0, step=0.01, axis=true },
            { name="m2",      label="m2",      unit="kg",    default=1.0,  min=0.2, max=3.0, step=0.01, axis=true },
            { name="l1",      label="l1",      unit="m",     default=1.0,  min=0.2, max=2.0, step=0.01, axis=true },
            { name="l2",      label="l2",      unit="m",     default=1.0,  min=0.2, max=2.0, step=0.01, axis=true },
            { name="g",       label="g",       unit="m/s2",  default=9.81, min=0.0, max=20.0, step=0.1, axis=true },
            { name="damping", label="damping", unit="",      default=0.0,  min=0.0, max=1.0, step=0.01, axis=true },
        },
        channels = {
            { name="theta1",   label="θ1",   unit="rad",   min=-PI, max=PI, cyclic=true },
            { name="theta2",   label="θ2",   unit="rad",   min=-PI, max=PI, cyclic=true },
            { name="energy",   label="energy",   unit="J",     min=-50, max=50, cyclic=false },
            { name="omega_max", label="max |omega|", unit="rad/s", min=0, max=20, cyclic=false },
        },
    },
    logistic = {
        title = "Logistic map",
        axes = { "r", "x0" },
        params = {
            { name="r",  label="r",  unit="", default=3.7, min=0.0, max=4.0, step=0.001, axis=true },
            { name="x0", label="x0", unit="", default=0.2, min=0.0, max=1.0, step=0.001, axis=true },
        },
        channels = {
            { name="x",        label="x",        unit="", min=0.0, max=1.0, cyclic=false },
            { name="lyapunov", label="lyapunov", unit="", min=-2.0, max=2.0, cyclic=false },
            { name="mean",     label="mean x",   unit="", min=0.0, max=1.0, cyclic=false },
            { name="x_abs",    label="|x|",      unit="", min=0.0, max=1.0, cyclic=false },
        },
    },
    henon = {
        title = "Hénon map",
        axes = { "a", "b" },
        params = {
            { name="a",  label="a",  unit="", default=1.4, min=0.0, max=1.5, step=0.001, axis=true },
            { name="b",  label="b",  unit="", default=0.3, min=-1.0, max=1.0, step=0.001, axis=true },
            { name="x0", label="x0", unit="", default=0.0, min=-2.0, max=2.0, step=0.001, axis=true },
            { name="y0", label="y0", unit="", default=0.0, min=-2.0, max=2.0, step=0.001, axis=true },
        },
        channels = {
            { name="x",      label="x",      unit="", min=-2.0, max=2.0, cyclic=false },
            { name="y",      label="y",      unit="", min=-2.0, max=2.0, cyclic=false },
            { name="radius", label="radius", unit="", min=0.0, max=3.0, cyclic=false },
            { name="x_abs",  label="|x|",    unit="", min=0.0, max=2.0, cyclic=false },
        },
    },
}

-- Custom models are 2-state maps by default.  To make an ODE instead, use
--   type = "ode", deriv = { x = "y", y = "-x" },  -- instead of step
-- and keep init/channels.  CPU and GLSL both use RK4 with the experiment dt.
M.custom = {
    title = "Lua Hénon map",
    state = { "x", "y" },
    axes = { "a", "b" },
    params = {
        { name="a",  label="a",  unit="", default=1.4, min=0.0, max=1.5, step=0.001, axis=true },
        { name="b",  label="b",  unit="", default=0.3, min=-1.0, max=1.0, step=0.001, axis=true },
        { name="x0", label="x0", unit="", default=0.0, min=-2.0, max=2.0, step=0.001, axis=true },
        { name="y0", label="y0", unit="", default=0.0, min=-2.0, max=2.0, step=0.001, axis=true },
    },
    init = { x = "x0", y = "y0" },
    step = { x = "1 - a*x*x + y", y = "b*x" },
    channels = {
        { name="x",      label="x",      unit="", min=-2.0, max=2.0, cyclic=false, expr="x" },
        { name="y",      label="y",      unit="", min=-2.0, max=2.0, cyclic=false, expr="y" },
        { name="radius", label="radius", unit="", min=0.0, max=3.0, cyclic=false, expr="sqrt(x*x + y*y)" },
        { name="x_abs",  label="|x|",    unit="", min=0.0, max=2.0, cyclic=false, expr="if(x < 0, -x, x)" },
    },
}
M.models.custom = M.custom

function M.schema(model_id)
    return M.models[model_id]
end

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