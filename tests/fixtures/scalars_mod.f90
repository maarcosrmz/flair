module scalars_mod
    implicit none
contains

    function add_d(a, b) result(c)
        real(8), intent(in) :: a, b
        real(8) :: c
        c = a + b
    end function

    function addi(a, b) result(c)
        integer(4), intent(in) :: a, b
        integer(4) :: c
        c = a + b
    end function

    ! real(4) result: exercises kind conversion in the wrapper
    function half(x) result(h)
        real(4), intent(in) :: x
        real(4) :: h
        h = x * 0.5
    end function

    ! integer(8) round-trip for values beyond 32 bits
    function big_id(n) result(m)
        integer(8), intent(in) :: n
        integer(8) :: m
        m = n
    end function

    ! subroutine wrapper must return None
    subroutine noop(n)
        integer(8), intent(in) :: n
        associate(unused => n)
        end associate
    end subroutine

    ! no-argument function -> METH_NOARGS
    function answer() result(a)
        integer(4) :: a
        a = 42
    end function

end module
