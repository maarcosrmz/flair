module optionals_mod
    implicit none

    ! derived type for the optional-derived-dummy case
    type :: pair_t
        integer(4) :: a = 1
    end type

    ! generic whose specifics carry trailing optionals
    interface combine
        module procedure combine_int, combine_str
    end interface

contains

    ! trailing optional scalars of assorted types
    function describe(a, b, c, s) result(code)
        integer(4), intent(in) :: a
        real(8), intent(in), optional :: b
        logical, intent(in), optional :: c
        character(*), intent(in), optional :: s
        integer(4) :: code
        code = a
        if (present(b)) code = code + 10 * int(b)
        if (present(c)) then
            if (c) code = code + 100
        end if
        if (present(s)) code = code + 1000 * len(s)
    end function

    ! optional array and optional explicit-length character
    function osum(v, w, tag3) result(s)
        real(8), intent(in) :: v(:)
        real(8), intent(in), optional :: w(:)
        character(3), intent(in), optional :: tag3
        real(8) :: s
        s = sum(v)
        if (present(w)) s = s + 2 * sum(w)
        if (present(tag3)) s = s + len(tag3)
    end function

    ! single optional: callable with no arguments at all
    function nopt(n) result(k)
        integer(4), intent(in), optional :: n
        integer(4) :: k
        k = -1
        if (present(n)) k = n
    end function

    ! optional derived-type dummy
    function pval(p) result(v)
        type(pair_t), intent(in), optional :: p
        integer(4) :: v
        v = 0
        if (present(p)) v = p%a
    end function

    function combine_int(a, b) result(r)
        integer(4), intent(in) :: a
        integer(4), intent(in), optional :: b
        integer(4) :: r
        r = a
        if (present(b)) r = a + b
    end function

    function combine_str(s, upper) result(r)
        character(*), intent(in) :: s
        logical, intent(in), optional :: upper
        integer(4) :: r
        r = len(s)
        if (present(upper)) then
            if (upper) r = -len(s)
        end if
    end function

end module
