module ops_mod
  use vec_mod
  implicit none
  interface describe
     module procedure describe_vec, describe_int
  end interface describe
  interface tagof                      ! scalar dispatch incl. bool/str probes
     module procedure tagof_int, tagof_real, tagof_bool, tagof_str
  end interface tagof
contains
  subroutine translate(v, dx, dy)      ! cross-module derived arg, mutated in place
    type(vec2), intent(inout) :: v
    real(wp), intent(in) :: dx, dy
    v%x = v%x + dx
    v%y = v%y + dy
  end subroutine translate

  function biased(f, v, bias) result(r) ! optional cross-module arg, mid-list
    real(wp), intent(in) :: f
    type(vec2), intent(in), optional :: v
    real(wp), intent(in) :: bias
    real(wp) :: r
    r = f + bias
    if (present(v)) r = r + v%x
  end function biased

  subroutine describe_vec(v)           ! overload variant on cross-module type
    type(vec2), intent(in) :: v
    print '(a,f0.3,a,f0.3,a)', 'vec2(', v%x, ', ', v%y, ')'
  end subroutine describe_vec

  subroutine describe_int(n)           ! overload variant on intrinsic
    integer, intent(in) :: n
    print '(a,i0)', 'int ', n
  end subroutine describe_int

  function tagof_int(n) result(t)
    integer, intent(in) :: n
    integer :: t
    t = 1
    associate(unused => n)
    end associate
  end function tagof_int

  function tagof_real(x) result(t)
    real(wp), intent(in) :: x
    integer :: t
    t = 2
    associate(unused => x)
    end associate
  end function tagof_real

  function tagof_bool(b) result(t)
    logical, intent(in) :: b
    integer :: t
    t = 3
    associate(unused => b)
    end associate
  end function tagof_bool

  function tagof_str(s) result(t)
    character(*), intent(in) :: s
    integer :: t
    t = 4
    associate(unused => s)
    end associate
  end function tagof_str
end module ops_mod
