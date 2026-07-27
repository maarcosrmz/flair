module mid_mod
  use leaf_mod, only: leaf_value       ! used internally; stays out of the API
  implicit none
  private
  public :: mid_double
contains
  function mid_double() result(r)
    integer :: r
    r = 2 * leaf_value()
  end function mid_double
end module mid_mod
