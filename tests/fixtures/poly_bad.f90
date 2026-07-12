module poly_bad_mod
    implicit none
    private
    public :: pb_base_t, pb_other_t
    public :: take_concrete, take_missing, take_unrelated

    type :: pb_base_t
        real(8) :: s = 1d0
    end type pb_base_t

    type :: pb_other_t
        real(8) :: v = 2d0
    end type pb_other_t

contains

    ! invalid: no polymorphic dummy argument
    !flair$ instantiate pb_base_t
    real(8) function take_concrete(x) result(r)
        type(pb_base_t), intent(in) :: x
        r = x%s
    end function take_concrete

    ! invalid: no such wrapped type in this module
    !flair$ instantiate nosuch_t
    real(8) function take_missing(x) result(r)
        class(pb_base_t), intent(in) :: x
        r = x%s
    end function take_missing

    ! invalid: pb_other_t does not extend pb_base_t
    !flair$ instantiate pb_other_t
    real(8) function take_unrelated(x) result(r)
        class(pb_base_t), intent(in) :: x
        r = x%s
    end function take_unrelated

end module poly_bad_mod
