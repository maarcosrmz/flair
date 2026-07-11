module strarg_mod
    implicit none
contains

    ! character scalars are wrappable, character arrays are not (no numpy
    ! dtype): flair must abort, not silently emit broken marshalling code.
    subroutine hello(names)
        character(len=8), intent(in) :: names(:)
        print *, 'hello, ', names(1)
    end subroutine

end module
