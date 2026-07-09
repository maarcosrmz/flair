module scene_mod
    use geom_mod
    implicit none
    private
    public :: scene_t, scene_t_init, origin_x

    ! point_t is use-associated but NOT re-exported (default private),
    ! exercising the generated `use geom_mod, only: point_t` import.
    type :: scene_t
        integer(4) :: id = 0
        type(point_t) :: origin
    end type

contains

    subroutine scene_t_init(s, id, origin)
        type(scene_t), intent(out) :: s
        integer(4), intent(in) :: id
        type(point_t), intent(in) :: origin
        s%id = id
        s%origin = origin
    end subroutine

    function origin_x(p) result(x)
        type(point_t), intent(in) :: p
        real(8) :: x
        x = p%x
    end function

end module
