module arrays_mod
    implicit none
contains

    ! intent(out) array: filled entirely by the callee
    subroutine fill_iota(x)
        real(8), intent(out) :: x(:)
        integer :: i
        do i = 1, size(x)
            x(i) = real(i, 8)
        end do
    end subroutine

    ! intent(inout) array: in-place mutation / writeback-if-copy
    subroutine scale_inplace(x, f)
        real(8), intent(inout) :: x(:)
        real(8), intent(in) :: f
        x = x * f
    end subroutine

    ! intent(in) array: read-only fast path
    function sum1(x) result(s)
        real(8), intent(in) :: x(:)
        real(8) :: s
        s = sum(x)
    end function

    ! rank-2 intent(in): detects transposed shape wiring
    function corner12(m) result(v)
        real(8), intent(in) :: m(:,:)
        real(8) :: v
        v = m(1, 2)
    end function

    ! rank-2 integer intent(inout)
    subroutine addk(m, k)
        integer(4), intent(inout) :: m(:,:)
        integer(4), intent(in) :: k
        m = m + k
    end subroutine

    ! integer(8) input array
    function isum(v) result(s)
        integer(8), intent(in) :: v(:)
        integer(8) :: s
        s = sum(v)
    end function

end module
