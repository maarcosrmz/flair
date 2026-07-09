module vec_mod
  implicit none
  integer, parameter :: wp = 8
  type :: vec2
     real(wp) :: x = 0.0_wp
     real(wp) :: y = 0.0_wp
   contains
     procedure :: scale
  end type vec2
contains
  subroutine scale(self, f)
    class(vec2), intent(inout) :: self
    ! real(wp), intent(in) :: f
    real(8), intent(in) :: f
    self%x = self%x * f
    self%y = self%y * f
  end subroutine scale
end module vec_mod
