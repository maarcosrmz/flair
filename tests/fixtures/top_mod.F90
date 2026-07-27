module top_mod
  use mid_mod, only: mid_double
  implicit none
  private
  public :: top_triple
contains
  function top_triple() result(r)
    integer :: r
    r = 3 * mid_double()
  end function top_triple
end module top_mod
