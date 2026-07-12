module geom_mod
    implicit none
    private
    public :: point_t, segment_t, box_t, box_t_init, midpoint_x
    public :: circle_t
    public :: operator(==)

    ! default-new case: no ctor / init
    type :: point_t
        real(8) :: x = 1d0
        real(8) :: y = 2d0
        character(8) :: name = "origin"
        logical :: visible = .true.
        complex(8) :: phase = (0d0, 0d0)
        real(8), allocatable :: tags(:)
        complex(8), allocatable :: modes(:)
    end type

    ! ctor case: generic interface named like the type
    type :: segment_t
        integer(4) :: id = 0
        type(point_t) :: a
        type(point_t) :: b
    contains
        procedure :: dist_to
    end type

    interface segment_t
        module procedure make_segment
    end interface

    ! defined-operator generic: has no Python-callable name, must be skipped
    interface operator(==)
        module procedure points_equal
    end interface

    ! ctor case: constructor specific returns the type by value
    type :: circle_t
        real(8) :: r = 1d0
    end type

    interface circle_t
        module procedure make_circle
    end interface

    ! init case: <type>_init subroutine with derived-type dummy
    type :: box_t
        type(point_t) :: corner
        real(8) :: w = 0d0
        character(16) :: label = ""
    end type

contains

    function points_equal(a, b) result(eq)
        type(point_t), intent(in) :: a, b
        logical :: eq
        eq = a%x == b%x .and. a%y == b%y
    end function

    function midpoint_x(a, b) result(x)
        type(point_t), intent(in) :: a, b
        real(8) :: x
        x = 0.5d0 * (a%x + b%x)
    end function

    function dist_to(self, p) result(d)
        class(segment_t), intent(in) :: self
        type(point_t), intent(in) :: p
        real(8) :: d
        d = abs(p%x - self%a%x)
    end function

    function make_segment(id, a, b) result(p)
        integer(4), intent(in) :: id
        type(point_t), intent(in) :: a, b
        type(segment_t), pointer :: p
        allocate(p)
        p%id = id
        p%a = a
        p%b = b
    end function

    function make_circle(r) result(c)
        real(8), intent(in) :: r
        type(circle_t) :: c
        c%r = r
    end function

    subroutine box_t_init(box, corner, w, label)
        type(box_t), intent(out) :: box
        type(point_t), intent(in) :: corner
        real(8), intent(in) :: w
        character(*), intent(in) :: label   ! assumed-length kwarg
        box%corner = corner
        box%w = w
        box%label = label
    end subroutine

end module
