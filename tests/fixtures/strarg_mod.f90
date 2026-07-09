module strarg_mod
    implicit none
contains

    ! character arguments are not wrappable yet: flair must abort, not
    ! silently emit broken marshalling code.
    subroutine hello(name)
        character(len=*), intent(in) :: name
        print *, 'hello, ', name
    end subroutine

end module
