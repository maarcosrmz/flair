module ops_mod
  use vec_mod
  implicit none
  interface describe
     module procedure describe_vec, describe_int
  end interface describe
contains
  subroutine translate(v, dx, dy)      ! cross-module derived arg, mutated in place
    type(vec2), intent(inout) :: v
    real(wp), intent(in) :: dx, dy
    v%x = v%x + dx
    v%y = v%y + dy
  end subroutine translate

  subroutine describe_vec(v)           ! overload variant on cross-module type
    type(vec2), intent(in) :: v
    print '(a,f0.3,a,f0.3,a)', 'vec2(', v%x, ', ', v%y, ')'
  end subroutine describe_vec

  subroutine describe_int(n)           ! overload variant on intrinsic
    integer, intent(in) :: n
    print '(a,i0)', 'int ', n
  end subroutine describe_int
end module ops_mod
