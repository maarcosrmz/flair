module poly_mod
    implicit none
    private
    public :: shape_t, circle_t, area_of, type_code

    type :: shape_t
        real(8) :: s = 1d0
    contains
        procedure :: whoami => shape_whoami
        procedure :: meet => shape_meet
    end type shape_t

    type, extends(shape_t) :: circle_t
        real(8) :: r = 2d0
    end type circle_t

contains

    !flair$ instantiate shape_t circle_t
    real(8) function area_of(sh) result(a)
        class(shape_t), intent(in) :: sh
        select type (sh)
        type is (circle_t)
            a = 3d0 * sh%r
        class default
            a = sh%s * sh%s
        end select
    end function area_of

    !flair$ instantiate shape_t circle_t
    integer(4) function type_code(obj) result(k)
        class(*), intent(in) :: obj
        select type (obj)
        type is (circle_t)
            k = 2
        type is (shape_t)
            k = 1
        class default
            k = 0
        end select
    end function type_code

    !flair$ instantiate shape_t circle_t
    integer(4) function shape_whoami(self) result(k)
        class(shape_t), intent(in) :: self
        select type (self)
        type is (circle_t)
            k = 2
        type is (shape_t)
            k = 1
        class default
            k = 0
        end select
    end function shape_whoami

    !flair$ instantiate shape_t circle_t
    integer(4) function shape_meet(self, other) result(k)
        class(shape_t), intent(in) :: self
        class(shape_t), intent(in) :: other
        k = 10 * shape_whoami(self) + shape_whoami(other)
    end function shape_meet

end module poly_mod
