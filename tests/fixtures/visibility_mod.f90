module visibility_mod
    implicit none
    private
    public :: pub_t, pub_fn

    type :: pub_t
        real(8) :: val = 0d0
    end type

    type :: priv_t          ! private -> must not be wrapped
        real(8) :: h = 0d0
    end type

contains

    function pub_fn(x) result(y)
        real(8), intent(in) :: x
        real(8) :: y
        y = helper(x) + 1d0
    end function

    function priv_fn(x) result(y)   ! private -> must not be wrapped
        real(8), intent(in) :: x
        real(8) :: y
        y = -x
    end function

    function helper(x) result(y)    ! private -> must not be wrapped
        real(8), intent(in) :: x
        real(8) :: y
        y = 2d0 * x
    end function

end module
