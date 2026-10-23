!=====================================================================
! kernel.f90 - 双摆相空间 RK4 计算内核（Fortran 侧）
!
! 设计要点：
!   * 状态按 C 的行主序 float[4*ncells] 传入，本文件用 1 基偏移访问；
!   * 每个 RK4 子步固定 4 个阶段（手工展开，无循环开销）；
!   * sin/cos 用 1024 项查表 + 小角多项式重构：表项在 L1 内，
!     避免 msvcrt 每次 ~400 周期的 sin/cos 调用；
!   * sin(t1-t2)/cos(t1-t2) 由已算出的 sin/cos(t1)、sin/cos(t2)
!     通过和差化积得到，每阶段只要 2 次查表；
!   * 角度归一化用截断整数乘法代替 fmod，避免两次库调用；
!   * 全程 double 计算，结果落回 float 存储（与 GPU 的 float32 接口一致）。
!
! ABI: 与 src/kernel_fortran.c 中的声明一一对应，bind(C)。
!=====================================================================
module dp_kernel_mod
  use iso_c_binding
  implicit none

  integer, parameter :: TRIG_N = 1024
  real(c_double), parameter :: TRIG_DELTA = 6.283185307179586476925286766559d0 / real(TRIG_N, c_double)
  real(c_double), parameter :: TRIG_INV   = real(TRIG_N, c_double) / 6.283185307179586476925286766559d0
  real(c_double), parameter :: PI_D        = 3.141592653589793238462643383279d0
  real(c_double), parameter :: TWO_PI_D    = 6.283185307179586476925286766559d0
  real(c_double), parameter :: INV_TWO_PI  = 0.159154943091895335768883763373d0

  real(c_double), save :: STAB(0:TRIG_N-1)
  real(c_double), save :: CTAB(0:TRIG_N-1)

contains

  ! 只在线程池启动前调用一次；sin/cos 各 1024 次，开销可忽略。
  subroutine dp_trig_init()
    integer :: i
    do i = 0, TRIG_N - 1
      STAB(i) = sin(real(i, c_double) * TRIG_DELTA)
      CTAB(i) = cos(real(i, c_double) * TRIG_DELTA)
    end do
  end subroutine dp_trig_init

end module dp_kernel_mod


subroutine dp_kernel_init() bind(C, name="dp_kernel_init")
  use dp_kernel_mod
  implicit none
  call dp_trig_init()
end subroutine dp_kernel_init


! state   : C 侧 float[4*n*n]，行主序
! n       : 每边格数
! y0,y1   : 本次处理的行区间 [y0, y1)
! steps   : 固定步长子步数
subroutine dp_step_band(state, n, y0, y1, steps, m1, m2, l1, l2, g, damp, dt) &
           bind(C, name="dp_step_band")
  use iso_c_binding
  use dp_kernel_mod
  implicit none

  integer(c_int), value :: n, y0, y1, steps
  real(c_float), intent(inout) :: state(*)
  real(c_double), value :: m1, m2, l1, l2, g, damp, dt

  integer :: y, x, s, base, k, j
  real(c_double) :: t1, t2, w1, w2
  real(c_double) :: st1, ct1, st2, ct2, sd, cd, den, rid, w1s, w2s
  real(c_double) :: k11,k12,k13,k14, k21,k22,k23,k24
  real(c_double) :: k31,k32,k33,k34, k41,k42,k43,k44
  real(c_double) :: a1, il1, il2, r, rr, sr, cr, si, ci

  il1 = 1.0d0 / l1
  il2 = 1.0d0 / l2

  do y = y0, y1 - 1
    do x = 0, n - 1
      base = (y * n + x) * 4
      t1 = dble(state(base + 1))
      t2 = dble(state(base + 2))
      w1 = dble(state(base + 3))
      w2 = dble(state(base + 4))

      do s = 1, steps

        ! ---- k1 -------------------------------------------------------
        call deriv(t1, t2, w1, w2, k11, k12, k13, k14)

        ! ---- k2 -------------------------------------------------------
        call deriv(t1 + 0.5d0*dt*k11, t2 + 0.5d0*dt*k12, &
                   w1 + 0.5d0*dt*k13, w2 + 0.5d0*dt*k14, &
                   k21, k22, k23, k24)

        ! ---- k3 -------------------------------------------------------
        call deriv(t1 + 0.5d0*dt*k21, t2 + 0.5d0*dt*k22, &
                   w1 + 0.5d0*dt*k23, w2 + 0.5d0*dt*k24, &
                   k31, k32, k33, k34)

        ! ---- k4 -------------------------------------------------------
        call deriv(t1 + dt*k31, t2 + dt*k32, w1 + dt*k33, w2 + dt*k34, &
                   k41, k42, k43, k44)

        a1 = dt / 6.0d0
        t1 = t1 + a1 * (k11 + 2.0d0*k21 + 2.0d0*k31 + k41)
        t2 = t2 + a1 * (k12 + 2.0d0*k22 + 2.0d0*k32 + k42)
        w1 = w1 + a1 * (k13 + 2.0d0*k23 + 2.0d0*k33 + k43)
        w2 = w2 + a1 * (k14 + 2.0d0*k24 + 2.0d0*k34 + k44)

        ! 角度归一化到 [-pi, pi)。先挡住病态大值，避免整数转换溢出。
        if (t1 > 1.0d6 .or. t1 < -1.0d6) t1 = 0.0d0
        if (t2 > 1.0d6 .or. t2 < -1.0d6) t2 = 0.0d0
        k = int(t1 * INV_TWO_PI)
        t1 = t1 - real(k, c_double) * TWO_PI_D
        if (t1 < -PI_D) t1 = t1 + TWO_PI_D
        if (t1 >= PI_D) t1 = t1 - TWO_PI_D
        k = int(t2 * INV_TWO_PI)
        t2 = t2 - real(k, c_double) * TWO_PI_D
        if (t2 < -PI_D) t2 = t2 + TWO_PI_D
        if (t2 >= PI_D) t2 = t2 - TWO_PI_D

      end do

      ! 极端情况下（发散成 NaN）把该格清零，避免污染后续帧。
      if (t1 /= t1 .or. t2 /= t2 .or. w1 /= w1 .or. w2 /= w2) then
        t1 = 0.0d0; t2 = 0.0d0; w1 = 0.0d0; w2 = 0.0d0
      end if

      state(base + 1) = real(t1, c_float)
      state(base + 2) = real(t2, c_float)
      state(base + 3) = real(w1, c_float)
      state(base + 4) = real(w2, c_float)
    end do
  end do

contains

  ! 与 physics.c 的 DERIV 宏逐项对应。
  ! st1/ct1/st2/ct2 手工查表 + 小角重构；delta 的三角函数由和差化积得到。
  subroutine deriv(t1, t2, w1, w2, dth1, dth2, dom1, dom2)
    real(c_double), intent(in)  :: t1, t2, w1, w2
    real(c_double), intent(out) :: dth1, dth2, dom1, dom2

    j = int(t1 * TRIG_INV + 0.5d0)
    r = t1 - real(j, c_double) * TRIG_DELTA
    rr = r * r
    sr = r * (1.0d0 - rr / 6.0d0)
    cr = 1.0d0 - 0.5d0 * rr
    j = iand(j, TRIG_N - 1)
    si = STAB(j)
    ci = CTAB(j)
    st1 = si * cr + ci * sr
    ct1 = ci * cr - si * sr

    j = int(t2 * TRIG_INV + 0.5d0)
    r = t2 - real(j, c_double) * TRIG_DELTA
    rr = r * r
    sr = r * (1.0d0 - rr / 6.0d0)
    cr = 1.0d0 - 0.5d0 * rr
    j = iand(j, TRIG_N - 1)
    si = STAB(j)
    ci = CTAB(j)
    st2 = si * cr + ci * sr
    ct2 = ci * cr - si * sr

    sd = st1 * ct2 - ct1 * st2
    cd = ct1 * ct2 + st1 * st2

    den = m1 + m2 * sd * sd
    rid = 1.0d0 / den
    w1s = w1 * w1
    w2s = w2 * w2

    dth1 = w1
    dth2 = w2
    dom1 = (m2*g*st2*cd - m2*sd*(l1*w1s*cd + l2*w2s) - (m1+m2)*g*st1) * rid * il1 &
           - damp * w1
    dom2 = ((m1+m2)*(l1*w1s*sd - g*st2 + g*st1*cd) + m2*l2*w2s*sd*cd) * rid * il2 &
           - damp * w2
  end subroutine deriv

end subroutine dp_step_band
