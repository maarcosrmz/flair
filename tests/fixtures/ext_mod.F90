! Extends a type defined in base_mod.F90, two levels deep. Only this file's
! module is wrapped in the cross-file cases, so everything the extending types
! inherit has to be recovered from base_mod's symbols alone.
module ext_mod
    use base_mod
    implicit none
    private
    public :: derived_t, leaf_t

    type, extends(base_t) :: derived_t
        integer(4) :: extra = 3
    contains
        procedure :: scale => derived_scale   ! overrides base_scale
        procedure :: total => derived_total
    end type derived_t

    type, extends(derived_t) :: leaf_t
        integer(4) :: depth = 2
    contains
        procedure :: total => leaf_total      ! overrides derived_total
    end type leaf_t

contains

    integer(4) function derived_scale(self) result(k)
        class(derived_t), intent(in) :: self
        k = 2
    end function derived_scale

    integer(4) function derived_total(self) result(k)
        class(derived_t), intent(in) :: self
        k = self%tag + self%extra
    end function derived_total

    integer(4) function leaf_total(self) result(k)
        class(leaf_t), intent(in) :: self
        k = self%tag + self%extra + self%depth
    end function leaf_total

end module ext_mod
