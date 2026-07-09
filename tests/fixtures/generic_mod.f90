module generic_mod
    implicit none
    private
    public :: thing_t, describe, total, area, pick

    type :: thing_t
        real(8) :: v = 0d0
    end type

    ! dispatch over scalar category, derived type, and array dtype/rank;
    ! each specific returns a distinct code so tests can tell which ran
    interface describe
        module procedure describe_int, describe_real, describe_thing, &
                         describe_vec, describe_mat, describe_ivec
    end interface

    ! overloads differing only in scalar kind collapse to the widest one
    interface total
        module procedure total_i4, total_i8
    end interface

    ! single specific: dispatcher forwards unconditionally
    interface area
        module procedure area_circle
    end interface

    ! only the second argument discriminates
    interface pick
        module procedure pick_ii, pick_ir
    end interface

contains

    function describe_int(n) result(k)
        integer(4), intent(in) :: n
        integer(4) :: k
        k = 1
    end function

    function describe_real(x) result(k)
        real(8), intent(in) :: x
        integer(4) :: k
        k = 2
    end function

    function describe_thing(t) result(k)
        type(thing_t), intent(in) :: t
        integer(4) :: k
        k = 3
    end function

    function describe_vec(x) result(k)
        real(8), intent(in) :: x(:)
        integer(4) :: k
        k = 4
    end function

    function describe_mat(x) result(k)
        real(8), intent(in) :: x(:,:)
        integer(4) :: k
        k = 5
    end function

    function describe_ivec(x) result(k)
        integer(4), intent(in) :: x(:)
        integer(4) :: k
        k = 6
    end function

    function total_i4(a, b) result(s)
        integer(4), intent(in) :: a, b
        integer(4) :: s
        s = a + b
    end function

    function total_i8(a, b) result(s)
        integer(8), intent(in) :: a, b
        integer(8) :: s
        s = a + b
    end function

    function area_circle(r) result(a)
        real(8), intent(in) :: r
        real(8) :: a
        a = r * r
    end function

    function pick_ii(a, b) result(k)
        integer(4), intent(in) :: a, b
        integer(4) :: k
        k = 1
    end function

    function pick_ir(a, b) result(k)
        integer(4), intent(in) :: a
        real(8), intent(in) :: b
        integer(4) :: k
        k = 2
    end function

end module
