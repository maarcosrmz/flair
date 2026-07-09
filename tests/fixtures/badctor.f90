module badctor_mod
    implicit none
    private
    public :: over_t, cls_t, holder_t

    type :: over_t
        integer :: n = 0
    end type

    type :: cls_t
        integer :: n = 0
    end type

    type :: holder_t
        integer :: k = 0
        type(over_t) :: o     ! references a skipped type
    end type

    interface over_t          ! overloaded -> not wrappable
        module procedure make_over_i, make_over_r
    end interface

    interface cls_t           ! class dummy -> not wrappable
        module procedure make_cls
    end interface

contains

    function make_over_i(n) result(p)
        integer, intent(in) :: n
        type(over_t), pointer :: p
        allocate(p); p%n = n
    end function

    function make_over_r(x) result(p)
        real(8), intent(in) :: x
        type(over_t), pointer :: p
        allocate(p); p%n = int(x)
    end function

    function make_cls(c) result(p)
        class(cls_t), pointer :: c
        type(cls_t), pointer :: p
        allocate(p); p%n = c%n
    end function

end module
