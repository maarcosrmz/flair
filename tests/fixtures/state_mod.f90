module state_mod
    implicit none

    ! wrapped type for the derived-variable view case
    type :: cfg_t
        real(8) :: scale = 1d0
        logical :: verbose = .false.
    end type

    ! derived-type variable -> live view object
    type(cfg_t) :: config

    ! intrinsic scalars -> live read-only values via module __getattr__
    real(8) :: gravity = 9.81d0
    integer(4) :: counter = 0
    character(8) :: tag = "initial"
    logical :: flag = .true.
    complex(8) :: phase = (1d0, -1d0)

    ! intrinsic arrays -> writable numpy views aliasing the Fortran storage
    real(8) :: grid(3) = [1d0, 2d0, 3d0]
    integer(4) :: table(2, 2) = reshape([1, 2, 3, 4], [2, 2])
    character(6) :: labels(2) = ["alpha ", "beta  "]

    ! annotated variables stay hidden
    !flair$ ignore
    integer(4) :: hidden = 7

    ! not exposable (reallocation would dangle a live view): warn + skip
    real(8), allocatable :: dyn(:)

contains

    ! mutates module state so checks can observe liveness from Python
    subroutine bump()
        counter = counter + 1
        config%scale = config%scale * 2
        grid = grid + 1
    end subroutine

    function first_label() result(s)
        character(6) :: s
        s = labels(1)
    end function

    function grid_sum() result(g)
        real(8) :: g
        g = sum(grid)
    end function

end module
