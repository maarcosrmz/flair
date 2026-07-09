module field_mod
  implicit none
  type :: point
     real :: x
     real :: y
     ! Unwrappable component: rank-2 array -> should warn, not abort
     real :: grid(3,3)
  end type point
contains
  real function point_norm(self)
    class(point), intent(in) :: self
    point_norm = sqrt(self%x**2 + self%y**2)
  end function point_norm
end module field_mod
