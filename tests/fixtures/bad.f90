module bad_mod
    use iso_fortran_env, only: real64
    use iso_c_binding, only: c_ptr
    implicit none
    private
    public :: inner_t, outer_t



    type :: inner_t
        real(real64) :: v = 0d0
    end type

    type :: hidden_t   ! private -> not wrapped
        real(real64) :: h = 0d0
    end type

    type :: outer_t
        type(c_ptr) :: handle                        ! intrinsic-module type -> skip
        type(inner_t) :: ok_field
        type(hidden_t) :: bad_field                  ! unwrapped type -> skip
        type(inner_t), pointer :: ptr_field => null() ! pointer -> skip
        type(inner_t), allocatable :: alloc_field     ! allocatable -> skip
        type(inner_t) :: arr_field(3)                 ! rank 1 inline -> skip
    end type

end module
