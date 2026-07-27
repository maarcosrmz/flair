module leaf_mod                        ! depth 2 from top_mod: reachable, not named
  implicit none
contains
  function leaf_value() result(r)
    integer :: r
    r = 7
  end function leaf_value
end module leaf_mod
