! Root of an inheritance chain whose extending type lives in another file.
module base_mod
    implicit none
    private
    public :: base_t

    type :: base_t
        integer(4) :: tag = 7
        real(8)    :: weight = 1.5d0
    contains
        procedure :: scale  => base_scale
        ! calls a binding on the passed object, so wrapping this against an
        ! extending type has to preserve the actual's dynamic type
        procedure :: reduce => base_reduce
        procedure :: bump   => base_bump
        procedure, private :: secret => base_secret
    end type base_t

contains

    integer(4) function base_scale(self) result(k)
        class(base_t), intent(in) :: self
        k = 1
    end function base_scale

    integer(4) function base_reduce(self) result(k)
        class(base_t), intent(in) :: self
        k = 10 * self%scale()
    end function base_reduce

    subroutine base_bump(self, by)
        class(base_t), intent(inout) :: self
        integer(4), intent(in) :: by
        self%tag = self%tag + by
    end subroutine base_bump

    integer(4) function base_secret(self) result(k)
        class(base_t), intent(in) :: self
        k = 99
    end function base_secret

end module base_mod
