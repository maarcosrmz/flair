module cfg_mod
#ifdef USE_VEC
  use vec_mod
#endif
  implicit none
contains
#ifdef USE_VEC
  subroutine reset(v)                  ! cross-module arg behind a define
    type(vec2), intent(inout) :: v
    v%x = 0.0_wp
    v%y = 0.0_wp
  end subroutine reset
#else
  subroutine noop()
  end subroutine noop
#endif
end module cfg_mod
