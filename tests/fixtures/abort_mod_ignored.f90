module abort_mod
  implicit none
contains
  real function good(x)
    real, intent(in) :: x
    good = x * 2.0
  end function good

  !flair$ ignore
  subroutine bad(y)
    integer, intent(out) :: y
    y = 42
  end subroutine bad
end module abort_mod
