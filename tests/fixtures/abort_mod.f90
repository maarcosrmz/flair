module abort_mod
  implicit none
contains
  ! Wrappable: real -> real
  real function good(x)
    real, intent(in) :: x
    good = x * 2.0
  end function good

  ! Unwrappable: scalar intent(out) cannot be written back
  subroutine bad(y)
    integer, intent(out) :: y
    y = 42
  end subroutine bad
end module abort_mod
