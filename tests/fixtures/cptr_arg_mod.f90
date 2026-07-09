module cptr_arg_mod
    use iso_c_binding, only: c_ptr
    implicit none
contains

    ! intrinsic-module derived type as dummy: not wrappable, must abort
    subroutine take_handle(h)
        type(c_ptr), intent(in) :: h
    end subroutine

end module
