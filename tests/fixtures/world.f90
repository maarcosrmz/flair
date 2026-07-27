module world_mod
    use scene_mod, only: scene_t
    implicit none
    private
    public :: world_id

    ! Third link of the geom_mod -> scene_mod -> world_mod type chain: wrapping
    ! world_mod alone needs scene_mod's converters, which in turn need
    ! geom_mod's, so closing the wrap set takes more than one round.

contains

    function world_id(s) result(n)
        type(scene_t), intent(in) :: s
        integer(4) :: n
        n = s%id
    end function

end module
