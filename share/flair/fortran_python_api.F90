module python_api_mod
    use iso_c_binding
    implicit none

    ! ===== Python C API numeric constants (Python 3.14 / x86-64) =====

    ! PyType_Slot.slot numbers
    integer(c_int), parameter :: Py_tp_dealloc    = 52
    integer(c_int), parameter :: Py_tp_repr       = 66
    integer(c_int), parameter :: Py_tp_init       = 60
    integer(c_int), parameter :: Py_tp_new        = 65
    integer(c_int), parameter :: Py_tp_methods    = 64
    integer(c_int), parameter :: Py_tp_getset     = 73
    integer(c_int), parameter :: Py_mp_subscript  = 5
    integer(c_int), parameter :: Py_sq_ass_item   = 39
    integer(c_int), parameter :: Py_sq_item       = 44
    integer(c_int), parameter :: Py_sq_length     = 45

    ! PyMethodDef.ml_flags
    integer(c_int), parameter :: METH_VARARGS  = 1
    integer(c_int), parameter :: METH_KEYWORDS = 2
    integer(c_int), parameter :: METH_NOARGS   = 4
    integer(c_int), parameter :: METH_O        = 8

    ! PyType_Spec.flags
    integer(c_int), parameter :: Py_TPFLAGS_DEFAULT = 0

    ! PYTHON_ABI_VERSION: pass to PyModule_Create2 for stable ABI (all 3.x)
    integer(c_int), parameter :: PYTHON_ABI_VERSION = 3

    ! Py_GetConstant() identifiers (Python 3.13+)
    integer(c_int), parameter :: Py_CONSTANT_NONE  = 0
    integer(c_int), parameter :: Py_CONSTANT_FALSE = 1
    integer(c_int), parameter :: Py_CONSTANT_TRUE  = 2

    ! PyObject_GetBuffer() flags
    integer(c_int), parameter :: PyBUF_SIMPLE  = 0   ! flat, contiguous
    integer(c_int), parameter :: PyBUF_STRIDES = 24  ! n-D shape + strides (0x10|0x08)

    ! ===== NumPy constants (x86-64 Linux / CPython 3.14) =====

    ! Supported ABI and API versions
    integer(c_int), parameter :: NPY_VERSION         = int(Z'02000000', c_int) ! NPY_API_VERSION 2
    integer(c_int), parameter :: NPY_FEATURE_VERSION = int(Z'00000010', c_int) ! NPY_1_23_API_VERSION (see default NPY_FEATURE_VERSION in numpyconfig.h)
    character(kind=c_char, len=*), parameter :: NPY_FEATURE_VERSION_STRING = "1.23"

    ! Type codes (x86-64 Linux; long = 8 bytes)
    integer(c_int), parameter :: NPY_INT8   =  1  ! NPY_BYTE
    integer(c_int), parameter :: NPY_INT16  =  3  ! NPY_SHORT
    integer(c_int), parameter :: NPY_INT32  =  5  ! NPY_INT
    integer(c_int), parameter :: NPY_INT64  =  7  ! NPY_LONG
    integer(c_int), parameter :: NPY_FLOAT32 = 11 ! NPY_FLOAT
    integer(c_int), parameter :: NPY_FLOAT64 = 12 ! NPY_DOUBLE
    integer(c_int), parameter :: NPY_COMPLEX64  = 14 ! NPY_CFLOAT
    integer(c_int), parameter :: NPY_COMPLEX128 = 15 ! NPY_CDOUBLE
    integer(c_int), parameter :: NPY_STRING     = 18 ! flexible bytes; itemsize = element length

    ! Array flags
    integer(c_int), parameter :: NPY_ARRAY_C_CONTIGUOUS = 1     ! 0x001
    integer(c_int), parameter :: NPY_ARRAY_F_CONTIGUOUS = 2     ! 0x002
    integer(c_int), parameter :: NPY_ARRAY_OWNDATA      = 4     ! 0x004
    integer(c_int), parameter :: NPY_ARRAY_WRITEABLE    = 1024  ! 0x400
    integer(c_int), parameter :: NPY_ARRAY_ALIGNED      = 256   ! 0x100
    integer(c_int), parameter :: NPY_ARRAY_ENSURECOPY   = 32    ! 0x020
    integer(c_int), parameter :: NPY_ARRAY_WRITEBACKIFCOPY = 8192 ! 0x2000
    integer(c_int), parameter :: NPY_ARRAY_BEHAVED      = NPY_ARRAY_WRITEABLE + NPY_ARRAY_ALIGNED

    ! ===== C API struct mirrors =====
    ! All layouts verified against Python 3.14.5 / x86-64 using offsetof().

    ! sizeof=32; name@0(8), basicsize@8(4), itemsize@12(4), flags@16(4), pad@20(4), slots@24(8)
    type, bind(C) :: PyType_Spec_t
        type(c_ptr)    :: name
        integer(c_int) :: basicsize
        integer(c_int) :: itemsize
        integer(c_int) :: flags
        integer(c_int) :: pad
        type(c_ptr)    :: slots
    end type

    ! sizeof=16; slot@0(4), pad@4(4), pfunc@8(8)
    type, bind(C) :: PyType_Slot_t
        integer(c_int) :: slot
        integer(c_int) :: pad
        type(c_ptr)    :: pfunc
    end type

    ! sizeof=32; ml_name@0(8), ml_meth@8(8), ml_flags@16(4), ml_pad@20(4), ml_doc@24(8)
    type, bind(C) :: PyMethodDef_t
        type(c_ptr)    :: ml_name
        type(c_ptr)    :: ml_meth
        integer(c_int) :: ml_flags
        integer(c_int) :: ml_pad
        type(c_ptr)    :: ml_doc
    end type

    ! sizeof=40; name@0(8), get@8(8), set@16(8), doc@24(8), closure@32(8)
    type, bind(C) :: PyGetSetDef_t
        type(c_ptr) :: name
        type(c_ptr) :: get
        type(c_ptr) :: set
        type(c_ptr) :: doc
        type(c_ptr) :: closure
    end type

    ! sizeof=104; stable ABI: PyModuleDef_Base (40 bytes) kept opaque.
    ! PyModule_Create2 calls _PyModuleDef_Init when m_index==0 (i.e. zero-init is correct).
    ! offsets: m_base@0(40), m_name@40(8), m_doc@48(8), m_size@56(8),
    !          m_methods@64(8), m_slots@72(8), m_traverse@80(8), m_clear@88(8), m_free@96(8)
    type, bind(C) :: PyModuleDef_t
        integer(c_int8_t)    :: m_base(40)
        type(c_ptr)          :: m_name
        type(c_ptr)          :: m_doc
        integer(c_ptrdiff_t) :: m_size
        type(c_ptr)          :: m_methods
        type(c_ptr)          :: m_slots
        type(c_ptr)          :: m_traverse
        type(c_ptr)          :: m_clear
        type(c_ptr)          :: m_free
    end type

    ! NumPy ndarray internal layout (numpy/ndarraytypes.h: PyArrayObject_fields).
    ! Mirrored so the PyArray_* accessors below can read it directly. sizeof=96.
    ! offsets: data@16(8), nd@24(4), dimensions@32(8), strides@40(8), base@48(8),
    !          descr@56(8), flags@64(4), weakreflist@72(8), buffer_info@80(8), mem_handler@88(8)
    type, bind(C) :: PyArrayObject_fields_t
        integer(c_int8_t) :: ob_base(16)   ! PyObject_HEAD
        type(c_ptr)       :: data          ! char *
        integer(c_int)    :: nd
        integer(c_int)    :: pad0
        type(c_ptr)       :: dimensions    ! npy_intp *
        type(c_ptr)       :: strides       ! npy_intp *
        type(c_ptr)       :: base
        type(c_ptr)       :: descr
        integer(c_int)    :: flags
        integer(c_int)    :: pad1
        type(c_ptr)       :: weakreflist
        type(c_ptr)       :: buffer_info   ! NPY_FEATURE_VERSION >= 1.20
        type(c_ptr)       :: mem_handler   ! NPY_FEATURE_VERSION >= 1.22
    end type

    ! Minimal mirror of an arbitrary received object's PyObject header, used to
    ! reach its type at runtime. On 64-bit CPython: ob_refcnt@0(8), ob_type@8(8).
    type, bind(C) :: PyObject_t
        integer(c_ptrdiff_t) :: ob_refcnt
        type(c_ptr)          :: ob_type    ! PyTypeObject*
    end type

    ! Partial mirror of PyTypeObject up to tp_name. PyObject_VAR_HEAD is
    ! ob_refcnt@0(8) + ob_type@8(8) + ob_size@16(8); tp_name@24(8).
    type, bind(C) :: PyTypeObject_t
        integer(c_ptrdiff_t) :: ob_refcnt
        type(c_ptr)          :: ob_type    ! metatype
        integer(c_ptrdiff_t) :: ob_size
        type(c_ptr)          :: tp_name    ! const char* -- "<module>.<name>"
    end type

    ! ===== Instance layout of every generated wrapper type =====
    ! Each wrapper class stores an opaque pointer to a heap-allocated Fortran
    ! object of the wrapped derived type. The layout does not depend on that
    ! type, so one shared definition serves every wrapper: the generated code
    ! only ever c_f_pointer's `data` to its own type.
    !
    ! `owner` distinguishes the two ownership modes:
    !   null     -- the instance owns `data`; tp_dealloc deallocates it.
    !   non-null -- a view: `data` points inside the Fortran object owned by
    !               `owner`, and the instance holds a reference on it
    !               (NumPy's `base` pattern). tp_dealloc only drops that ref.
    type, bind(C) :: FLAIR_object_t
        integer(c_int8_t) :: ob_head(16)   ! opaque PyObject_HEAD
        type(c_ptr)       :: data          ! -> heap-allocated Fortran object
        type(c_ptr)       :: owner         ! non-null: view; the owning PyObject
    end type

    ! ===== Exception objects (exported symbols from the interpreter) =====
    type(c_ptr), bind(C, name="PyExc_TypeError")      :: PyExc_TypeError
    type(c_ptr), bind(C, name="PyExc_ValueError")     :: PyExc_ValueError
    type(c_ptr), bind(C, name="PyExc_AttributeError") :: PyExc_AttributeError
    type(c_ptr), bind(C, name="PyExc_IndexError")     :: PyExc_IndexError
    type(c_ptr), bind(C, name="PyExc_RuntimeError")   :: PyExc_RuntimeError
    type(c_ptr), bind(C, name="PyExc_ImportError")    :: PyExc_ImportError

    ! Null-terminated strings used by import_array (target+save so c_loc stays valid)
    character(kind=c_char, len=31), target, save :: s_numpy_core   = &
        "numpy._core._multiarray_umath"//c_null_char
    character(kind=c_char, len=29), target, save :: s_numpy_legacy = &
        "numpy.core._multiarray_umath"//c_null_char
    character(kind=c_char, len=11), target, save :: s_array_api    = "_ARRAY_API"//c_null_char
    character(kind=c_char, len=20), target, save :: s_numpy_req    = "numpy not available"//c_null_char
    character(kind=c_char, len=40), target, save :: s_import_error = &
        "numpy._core.multiarray failed to import"//c_null_char
    character(kind=c_char, len=16), target, save :: s_bool_req = "expected a bool"//c_null_char

    ! ===== C API interfaces =====
    interface

        ! --- float ---
        function PyFloat_AsDouble(obj) bind(C, name="PyFloat_AsDouble") result(r)
            import :: c_ptr, c_double
            type(c_ptr), value :: obj
            real(c_double) :: r
        end function

        function PyFloat_FromDouble(v) bind(C, name="PyFloat_FromDouble") result(r)
            import :: c_ptr, c_double
            real(c_double), value :: v
            type(c_ptr) :: r
        end function

        ! --- complex ---
        function PyComplex_RealAsDouble(op) bind(C, name="PyComplex_RealAsDouble") result(r)
            import :: c_ptr, c_double
            type(c_ptr), value :: op
            real(c_double) :: r
        end function

        function PyComplex_ImagAsDouble(op) bind(C, name="PyComplex_ImagAsDouble") result(r)
            import :: c_ptr, c_double
            type(c_ptr), value :: op
            real(c_double) :: r
        end function

        function PyComplex_FromDoubles(re, im) bind(C, name="PyComplex_FromDoubles") result(r)
            import :: c_ptr, c_double
            real(c_double), value :: re, im
            type(c_ptr) :: r
        end function

        ! --- long ---
        function PyLong_FromLongLong(v) bind(C, name="PyLong_FromLongLong") result(r)
            import :: c_ptr, c_long_long
            integer(c_long_long), value :: v
            type(c_ptr) :: r
        end function

        function PyLong_AsLongLong(op) bind(C, name="PyLong_AsLongLong") result(r)
            import :: c_ptr, c_long_long
            type(c_ptr), value :: op
            integer(c_long_long) :: r
        end function

        ! --- bool ---
        function PyBool_FromLong(v) bind(C, name="PyBool_FromLong") result(r)
            import :: c_ptr, c_long
            integer(c_long), value :: v
            type(c_ptr) :: r
        end function

        ! --- tuple ---
        function PyTuple_GetItem(op, i) bind(C, name="PyTuple_GetItem") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr),          value :: op
            integer(c_ptrdiff_t), value :: i
            type(c_ptr) :: r
        end function

        function PyTuple_Size(op) bind(C, name="PyTuple_Size") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr), value :: op
            integer(c_ptrdiff_t) :: r
        end function

        function PyTuple_New(len) bind(C, name="PyTuple_New") result(r)
            import :: c_ptr, c_ptrdiff_t
            integer(c_ptrdiff_t), value :: len
            type(c_ptr) :: r
        end function

        function PyTuple_SetItem(op, i, newitem) bind(C, name="PyTuple_SetItem") result(r)
            import :: c_ptr, c_ptrdiff_t, c_int
            type(c_ptr),          value :: op, newitem
            integer(c_ptrdiff_t), value :: i
            integer(c_int) :: r   ! steals reference to newitem; 0 on success
        end function

        ! --- list ---
        function PyList_New(len) bind(C, name="PyList_New") result(r)
            import :: c_ptr, c_ptrdiff_t
            integer(c_ptrdiff_t), value :: len
            type(c_ptr) :: r
        end function

        function PyList_SetItem(list, i, item) bind(C, name="PyList_SetItem") result(r)
            import :: c_ptr, c_ptrdiff_t, c_int
            type(c_ptr),          value :: list
            integer(c_ptrdiff_t), value :: i
            type(c_ptr),          value :: item
            integer(c_int) :: r
        end function

        ! --- unicode (for character(len=*) arguments) ---
        function PyUnicode_AsUTF8AndSize(obj, size) &
                bind(C, name="PyUnicode_AsUTF8AndSize") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr),          value    :: obj
            integer(c_ptrdiff_t), intent(out) :: size
            type(c_ptr) :: r   ! const char* into the object; NULL on error
        end function

        function PyUnicode_FromString(str) &
                bind(C, name="PyUnicode_FromString") result(r)
            import :: c_ptr
            type(c_ptr), value :: str   ! null-terminated UTF-8 C string
            type(c_ptr) :: r
        end function

        function PyUnicode_FromStringAndSize(u, size) &
                bind(C, name="PyUnicode_FromStringAndSize") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr),          value :: u   ! UTF-8 buffer, not null-terminated
            integer(c_ptrdiff_t), value :: size
            type(c_ptr) :: r
        end function

        function PyUnicode_Concat(left, right) &
                bind(C, name="PyUnicode_Concat") result(r)
            import :: c_ptr
            type(c_ptr), value :: left, right
            type(c_ptr) :: r   ! new reference
        end function

        function PyUnicode_CompareWithASCIIString(uni, str) &
                bind(C, name="PyUnicode_CompareWithASCIIString") result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: uni
            type(c_ptr), value :: str   ! null-terminated ASCII C string
            integer(c_int) :: r         ! 0 if equal
        end function

        ! --- dict ---
        function PyDict_GetItemString(dp, key) bind(C, name="PyDict_GetItemString") result(r)
            import :: c_ptr
            type(c_ptr), value :: dp
            type(c_ptr), value :: key
            type(c_ptr) :: r
        end function

        function PyDict_SetItemString(dp, key, item) &
                bind(C, name="PyDict_SetItemString") result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: dp
            type(c_ptr), value :: key   ! null-terminated ASCII C string
            type(c_ptr), value :: item  ! does not steal: increfs on success
            integer(c_int) :: r         ! 0 on success, -1 on error
        end function

        function PyDict_Size(dp) bind(C, name="PyDict_Size") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr), value :: dp
            integer(c_ptrdiff_t) :: r
        end function

        function PyDict_Next(dp, ppos, pkey, pvalue) bind(C, name="PyDict_Next") result(r)
            import :: c_ptr, c_ptrdiff_t, c_int
            type(c_ptr),          value       :: dp
            integer(c_ptrdiff_t), intent(inout) :: ppos
            type(c_ptr),          intent(out) :: pkey, pvalue   ! borrowed refs
            integer(c_int) :: r                                 ! 0 when exhausted
        end function

        ! --- sequence ---
        function PySequence_Check(op) bind(C, name="PySequence_Check") result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: op
            integer(c_int) :: r
        end function

        function PySequence_Size(op) bind(C, name="PySequence_Size") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr), value :: op
            integer(c_ptrdiff_t) :: r
        end function

        function PySequence_GetItem(op, i) bind(C, name="PySequence_GetItem") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr),          value :: op
            integer(c_ptrdiff_t), value :: i
            type(c_ptr) :: r
        end function

        ! --- refcount / constants ---
        function Py_GetConstant(constant_id) bind(C, name="Py_GetConstant") result(r)
            import :: c_ptr, c_int
            integer(c_int), value :: constant_id
            type(c_ptr) :: r
        end function

        subroutine Py_IncRef(op) bind(C, name="Py_IncRef")
            import :: c_ptr
            type(c_ptr), value :: op
        end subroutine

        subroutine Py_DecRef(op) bind(C, name="Py_DecRef")
            import :: c_ptr
            type(c_ptr), value :: op
        end subroutine

        function PyObject_Repr(o) bind(C, name="PyObject_Repr") result(r)
            import :: c_ptr
            type(c_ptr), value :: o
            type(c_ptr) :: r
        end function

        function PyObject_IsInstance(inst, cls) bind(C, name="PyObject_IsInstance") result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: inst, cls
            integer(c_int) :: r
        end function

        ! --- errors ---
        subroutine PyErr_SetString(exc, msg) bind(C, name="PyErr_SetString")
            import :: c_ptr
            type(c_ptr), value :: exc
            type(c_ptr), value :: msg
        end subroutine

        subroutine PyErr_SetObject(exc, val) bind(C, name="PyErr_SetObject")
            import :: c_ptr
            type(c_ptr), value :: exc
            type(c_ptr), value :: val
        end subroutine

        function PyErr_Occurred() bind(C, name="PyErr_Occurred") result(r)
            import :: c_ptr
            type(c_ptr) :: r
        end function

        subroutine PyErr_Clear() bind(C, name="PyErr_Clear")
        end subroutine

        subroutine PyErr_Print() bind(C, name="PyErr_Print")
        end subroutine

        function PyNumber_AsSsize_t(o, exc) bind(C, name="PyNumber_AsSsize_t") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr), value :: o
            type(c_ptr), value :: exc
            integer(c_ptrdiff_t) :: r
        end function

        function PySlice_Unpack(slice, start, stop, step) bind(C, name="PySlice_Unpack") result(r)
            import :: c_ptr, c_ptrdiff_t, c_int
            type(c_ptr),          value       :: slice
            integer(c_ptrdiff_t), intent(out) :: start, stop, step
            integer(c_int) :: r
        end function

        function PySlice_AdjustIndices(length, start, stop, step) bind(C, name="PySlice_AdjustIndices") result(r)
            import :: c_ptrdiff_t
            integer(c_ptrdiff_t), value         :: length, step
            integer(c_ptrdiff_t), intent(inout) :: start, stop
            integer(c_ptrdiff_t) :: r
        end function

        ! --- object / memory ---
        function PyType_GenericAlloc(type_ptr, nitems) bind(C, name="PyType_GenericAlloc") result(r)
            import :: c_ptr, c_ptrdiff_t
            type(c_ptr),          value :: type_ptr
            integer(c_ptrdiff_t), value :: nitems
            type(c_ptr) :: r
        end function

        subroutine PyObject_Free(ptr) bind(C, name="PyObject_Free")
            import :: c_ptr
            type(c_ptr), value :: ptr
        end subroutine

        ! --- type creation ---
        function PyType_FromSpec(spec) bind(C, name="PyType_FromSpec") result(r)
            import :: c_ptr
            type(c_ptr), value :: spec
            type(c_ptr) :: r
        end function

        ! --- buffer protocol (PEP 3118 / stable ABI) ---
        function PyObject_GetBuffer(exporter, view, flags) bind(C, name="PyObject_GetBuffer") result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: exporter, view
            integer(c_int), value :: flags
            integer(c_int) :: r
        end function

        subroutine PyBuffer_Release(view) bind(C, name="PyBuffer_Release")
            import :: c_ptr
            type(c_ptr), value :: view
        end subroutine

        ! --- import / attribute / capsule (needed to load NumPy API table) ---
        function PyImport_ImportModule(name) bind(C, name="PyImport_ImportModule") result(r)
            import :: c_ptr
            type(c_ptr), value :: name
            type(c_ptr) :: r
        end function

        function PyImport_GetModuleDict() bind(C, name="PyImport_GetModuleDict") result(r)
            import :: c_ptr
            type(c_ptr) :: r   ! borrowed reference to sys.modules
        end function

        function PyObject_GetAttrString(obj, name) bind(C, name="PyObject_GetAttrString") result(r)
            import :: c_ptr
            type(c_ptr), value :: obj, name
            type(c_ptr) :: r
        end function

        function PyCapsule_GetPointer(capsule, name) bind(C, name="PyCapsule_GetPointer") result(r)
            import :: c_ptr
            type(c_ptr), value :: capsule, name
            type(c_ptr) :: r
        end function

        ! --- module ---
        function PyModule_Create2(def, api_version) bind(C, name="PyModule_Create2") result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: def
            integer(c_int), value :: api_version
            type(c_ptr) :: r
        end function

        function PyModule_AddObjectRef(m, name, obj) bind(C, name="PyModule_AddObjectRef") result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: m
            type(c_ptr), value :: name
            type(c_ptr), value :: obj
            integer(c_int) :: r
        end function

    end interface

    ! ===== Abstract interfaces for NumPy function pointers =====
    abstract interface
        ! PyArray_GetNDArrayCVersion: Get runtime C API version
        function PyArray_GetNDArrayCVersion_iface() bind(C) result(r)
            import :: c_int
            integer(c_int) :: r
        end function

        ! PyArray_NewFromDescr: steals a reference to descr
        function PyArray_NewFromDescr_iface(subtype, descr, nd, dims, strides, data, &
                                            flags, obj) bind(C) result(r)
            import :: c_ptr, c_int
            type(c_ptr),    value :: subtype, descr, dims, strides, data, obj
            integer(c_int), value :: nd, flags
            type(c_ptr) :: r
        end function

        ! PyArray_New: builds the descr from type_num + itemsize (flexible types)
        function PyArray_New_iface(subtype, nd, dims, type_num, strides, data, &
                                   itemsize, flags, obj) bind(C) result(r)
            import :: c_ptr, c_int
            type(c_ptr),    value :: subtype, dims, strides, data, obj
            integer(c_int), value :: nd, type_num, itemsize, flags
            type(c_ptr) :: r
        end function

        ! PyArray_DescrFromType: returns a new reference
        function PyArray_DescrFromType_iface(type_num) bind(C) result(r)
            import :: c_ptr, c_int
            integer(c_int), value :: type_num
            type(c_ptr) :: r
        end function

        ! PyArray_SetBaseObject: steals a reference to obj
        function PyArray_SetBaseObject_iface(arr, obj) bind(C) result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: arr, obj
            integer(c_int) :: r
        end function

        ! PyArray_FromAny: steals a reference to newtype
        function PyArray_FromAny_iface(op, newtype, min_depth, max_depth, &
                                       requirements, context) bind(C) result(r)
            import :: c_ptr, c_int
            type(c_ptr),    value :: op, newtype, context
            integer(c_int), value :: min_depth, max_depth, requirements
            type(c_ptr) :: r
        end function

        ! PyArray_ResolveWritebackIfCopy: flush a WRITEBACKIFCOPY copy back into
        ! its base and clear the flag; returns 1 if copied back, 0 if nothing to
        ! do, -1 on error.
        function PyArray_ResolveWritebackIfCopy_iface(arr) bind(C) result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: arr
            integer(c_int) :: r
        end function
    end interface

    ! ===== NumPy API runtime state =====
    ! Populated once by numpy_api_init(); shared across all modules that use python_api_mod.
    ! API table indices: C 0-based → Fortran 1-based (= C + 1).
    type(c_ptr),    save :: numpy_api_ptr            = c_null_ptr
    type(c_ptr),    save :: PyArray_Type_ptr         = c_null_ptr
    procedure(PyArray_GetNDArrayCVersion_iface),  pointer :: PyArray_GetNDArrayCVersion
    procedure(PyArray_GetNDArrayCVersion_iface),  pointer :: PyArray_GetNDArrayCFeatureVersion
    procedure(PyArray_NewFromDescr_iface),        pointer :: PyArray_NewFromDescr
    procedure(PyArray_New_iface),                 pointer :: PyArray_New
    procedure(PyArray_DescrFromType_iface),       pointer :: PyArray_DescrFromType
    procedure(PyArray_SetBaseObject_iface),       pointer :: PyArray_SetBaseObject
    procedure(PyArray_FromAny_iface),             pointer :: PyArray_FromAny
    procedure(PyArray_ResolveWritebackIfCopy_iface), pointer :: PyArray_ResolveWritebackIfCopy

contains

    ! Fortran equivalent of the NumPy import_array() macro.
    ! Returns 0 on success.  On failure: prints the pending exception, replaces it
    ! with PyExc_ImportError, and returns -1 — matching the C macro's _RETURN_VALUE path.
    function import_array() result(r)
        integer(c_int) :: r
        type(c_ptr) :: numpy_mod, c_api_obj
        type(c_ptr), pointer :: api(:)
        integer(c_int) :: PyArray_ABI_VERSION, PyArray_RUNTIME_VERSION
        integer :: n
        type(c_funptr), save :: fp_PyArray_GetNDArrayCVersion        = c_null_funptr
        type(c_funptr), save :: fp_PyArray_GetNDArrayCFeatureVersion = c_null_funptr
        type(c_funptr), save :: fp_PyArray_NewFromDescr       = c_null_funptr
        type(c_funptr), save :: fp_PyArray_New                = c_null_funptr
        type(c_funptr), save :: fp_PyArray_DescrFromType      = c_null_funptr
        type(c_funptr), save :: fp_PyArray_SetBaseObject      = c_null_funptr
        type(c_funptr), save :: fp_PyArray_FromAny            = c_null_funptr
        type(c_funptr), save :: fp_PyArray_ResolveWritebackIfCopy = c_null_funptr
        character(kind=c_char, len=512), save, target :: err_msg

        r = -1_c_int

        numpy_mod = PyImport_ImportModule(c_loc(s_numpy_core))
        if (.not. c_associated(numpy_mod)) then
            call PyErr_Clear()
            numpy_mod = PyImport_ImportModule(c_loc(s_numpy_legacy))
            if (.not. c_associated(numpy_mod)) then
                call PyErr_Print()
                call PyErr_SetString(PyExc_ImportError, c_loc(s_import_error))
                return
            end if
        end if

        c_api_obj = PyObject_GetAttrString(numpy_mod, c_loc(s_array_api))
        call Py_DecRef(numpy_mod)
        if (.not. c_associated(c_api_obj)) then
            call PyErr_Print()
            call PyErr_SetString(PyExc_ImportError, c_loc(s_import_error))
            return
        end if

        numpy_api_ptr = PyCapsule_GetPointer(c_api_obj, c_null_ptr)
        call Py_DecRef(c_api_obj)
        if (.not. c_associated(numpy_api_ptr)) then
            call PyErr_Print()
            call PyErr_SetString(PyExc_ImportError, c_loc(s_import_error))
            return
        end if

        ! Expose void** as an array of c_ptr; indices below are C 0-based + 1
        call c_f_pointer(numpy_api_ptr, api, [400_c_ptrdiff_t])
        PyArray_Type_ptr         = api(3)                                         ! C index 2
        fp_PyArray_GetNDArrayCVersion        = transfer(api(1),   c_null_funptr); ! C index 0
        fp_PyArray_DescrFromType             = transfer(api(46),  c_null_funptr)  ! C index 45
        fp_PyArray_FromAny                   = transfer(api(70),  c_null_funptr)  ! C index 69
        fp_PyArray_New                       = transfer(api(94),  c_null_funptr)  ! C index 93
        fp_PyArray_NewFromDescr              = transfer(api(95),  c_null_funptr)  ! C index 94
        fp_PyArray_GetNDArrayCFeatureVersion = transfer(api(212), c_null_funptr); ! C index 211
        fp_PyArray_SetBaseObject             = transfer(api(283), c_null_funptr)  ! C index 282
        fp_PyArray_ResolveWritebackIfCopy    = transfer(api(303), c_null_funptr)  ! C index 302

        call c_f_procpointer(fp_PyArray_GetNDArrayCVersion, PyArray_GetNDArrayCVersion)
        call c_f_procpointer(fp_PyArray_DescrFromType,      PyArray_DescrFromType)
        call c_f_procpointer(fp_PyArray_FromAny,            PyArray_FromAny)
        call c_f_procpointer(fp_PyArray_New,                PyArray_New)
        call c_f_procpointer(fp_PyArray_NewFromDescr,       PyArray_NewFromDescr)
        call c_f_procpointer(fp_PyArray_GetNDArrayCFeatureVersion, PyArray_GetNDArrayCFeatureVersion)
        call c_f_procpointer(fp_PyArray_SetBaseObject,      PyArray_SetBaseObject)
        call c_f_procpointer(fp_PyArray_ResolveWritebackIfCopy, PyArray_ResolveWritebackIfCopy)

        ! Perform runtime check of C API version.  As of now NumPy 2.0 is ABI
        ! backwards compatible (in the exposed feature subset!) for all practical
        ! purposes.
        PyArray_ABI_VERSION = PyArray_GetNDArrayCVersion()
        if (NPY_VERSION < PyArray_ABI_VERSION) then
            write(err_msg, '(a,z8.8,a,z8.8)') &
                'module compiled against ABI version 0x', NPY_VERSION, &
                ' but this version of numpy is 0x', PyArray_ABI_VERSION
            n = len_trim(err_msg)
            err_msg(n+1:n+1) = c_null_char
            call PyErr_SetString(PyExc_RuntimeError, c_loc(err_msg))
            return
        end if

        PyArray_RUNTIME_VERSION = PyArray_GetNDArrayCFeatureVersion()
        if (NPY_FEATURE_VERSION > PyArray_RUNTIME_VERSION) then
            write(err_msg, '(a,z8.8,a,a,a,z8.8,a)') &
                'module was compiled against NumPy C-API version 0x', NPY_FEATURE_VERSION, &
                ' (NumPy ', NPY_FEATURE_VERSION_STRING, &
                ') but the running NumPy has C-API version 0x', PyArray_RUNTIME_VERSION, &
                '. Check the section C-API incompatibility at the Troubleshooting ImportError ' // &
                'section at https://numpy.org/devdocs/user/troubleshooting-importerror.html' // &
                '#c-api-incompatibility for indications on how to solve this problem.'
            n = len_trim(err_msg)
            err_msg(n+1:n+1) = c_null_char
            call PyErr_SetString(PyExc_RuntimeError, c_loc(err_msg))
            return
        end if

        r = 0_c_int
    end function

    ! ===== NumPy array accessors =====
    ! Fortran re-implementations of the inline PyArray_* accessors (which are not
    ! in the capsule API table, so cannot be called from Fortran). They read the
    ! mirrored PyArrayObject_fields directly. `arr` is a PyObject* to an ndarray;
    ! DIM/STRIDE take a 0-based dimension index, like the C macros.

    function PyArray_DATA(arr) result(r)
        type(c_ptr), value :: arr
        type(c_ptr) :: r
        type(PyArrayObject_fields_t), pointer :: f
        call c_f_pointer(arr, f)
        r = f%data
    end function

    function PyArray_DIM(arr, idim) result(r)
        type(c_ptr), value :: arr
        integer(c_int), value :: idim
        integer(c_ptrdiff_t) :: r
        type(PyArrayObject_fields_t), pointer :: f
        integer(c_ptrdiff_t), pointer :: dims(:)
        call c_f_pointer(arr, f)
        call c_f_pointer(f%dimensions, dims, [int(f%nd, c_ptrdiff_t)])
        r = dims(idim + 1)
    end function

    function PyArray_STRIDE(arr, istride) result(r)
        type(c_ptr), value :: arr
        integer(c_int), value :: istride
        integer(c_ptrdiff_t) :: r
        type(PyArrayObject_fields_t), pointer :: f
        integer(c_ptrdiff_t), pointer :: strd(:)
        call c_f_pointer(arr, f)
        call c_f_pointer(f%strides, strd, [int(f%nd, c_ptrdiff_t)])
        r = strd(istride + 1)
    end function

    ! Number of dimensions of an ndarray (PyArray_NDIM macro).
    function PyArray_NDIM(arr) result(r)
        type(c_ptr), value :: arr
        integer(c_int) :: r
        type(PyArrayObject_fields_t), pointer :: f
        call c_f_pointer(arr, f)
        r = f%nd
    end function

    ! The dtype descriptor of an ndarray (PyArray_DESCR macro). Builtin dtypes
    ! are singletons, so this can be pointer-compared against
    ! PyArray_DescrFromType(<code>) to identify the element type.
    function PyArray_DESCR(arr) result(r)
        type(c_ptr), value :: arr
        type(c_ptr) :: r
        type(PyArrayObject_fields_t), pointer :: f
        call c_f_pointer(arr, f)
        r = f%descr
    end function

    ! .true. iff the null-terminated C string `cs` equals the Fortran string
    ! `fs` exactly (same length, byte-for-byte). Used for tp_name dispatch.
    function c_string_eq(cs, fs) result(eq)
        type(c_ptr),      value      :: cs
        character(len=*), intent(in) :: fs
        logical :: eq
        character(kind=c_char), pointer :: p(:)
        integer :: i
        eq = .false.
        if (.not. c_associated(cs)) return
        call c_f_pointer(cs, p, [len(fs) + 1])
        do i = 1, len(fs)
            if (iachar(p(i)) /= iachar(fs(i:i))) return
        end do
        eq = (p(len(fs) + 1) == c_null_char)
    end function

    ! .true. iff two C pointers refer to the same address.
    function c_ptr_eq(a, b) result(eq)
        type(c_ptr), value :: a, b
        logical :: eq
        eq = transfer(a, 0_c_intptr_t) == transfer(b, 0_c_intptr_t)
    end function

    ! ===== Safe argument converters for intrinsic scalars =====
    ! Counterparts of the generated <dtype>_from_PyObject converters.
    ! CPython reports conversion failure in-band (-1 result + pending
    ! exception), so the result alone cannot carry the error; `ok` does.
    ! On failure the exception set by the C API call is left pending and the
    ! caller bails with its fail value. Precondition (as in C): no exception
    ! pending on entry.

    function FLAIR_double_from_PyObject(obj, ok) result(v)
        type(c_ptr), value   :: obj
        logical, intent(out) :: ok
        real(c_double)       :: v
        v = PyFloat_AsDouble(obj)
        ok = .not. (v == -1.0_c_double .and. c_associated(PyErr_Occurred()))
    end function

    function FLAIR_int64_from_PyObject(obj, ok) result(v)
        type(c_ptr), value   :: obj
        logical, intent(out) :: ok
        integer(c_long_long) :: v
        v = PyLong_AsLongLong(obj)
        ok = .not. (v == -1_c_long_long .and. c_associated(PyErr_Occurred()))
    end function

    ! complex, float, int and anything else PyComplex_*AsDouble accepts
    ! (__complex__/__float__/__index__) convert; the imaginary part of a
    ! non-complex is 0.
    function FLAIR_dcomplex_from_PyObject(obj, ok) result(v)
        type(c_ptr), value   :: obj
        logical, intent(out) :: ok
        complex(c_double_complex) :: v
        real(c_double) :: re, im
        v = cmplx(0, 0, kind=c_double_complex)
        re = PyComplex_RealAsDouble(obj)
        ok = .not. (re == -1.0_c_double .and. c_associated(PyErr_Occurred()))
        if (.not. ok) return
        im = PyComplex_ImagAsDouble(obj)
        ok = .not. (im == -1.0_c_double .and. c_associated(PyErr_Occurred()))
        if (ok) v = cmplx(re, im, kind=c_double_complex)
    end function

    ! Strict: only True/False convert (bool is an int subclass, so a truthiness
    ! or PyLong-based conversion would also accept integers). True and False
    ! are singletons, so identity comparison is the exact-type check; unlike
    ! the C API converters this sets the TypeError itself.
    function FLAIR_logical_from_PyObject(obj, ok) result(v)
        type(c_ptr), value   :: obj
        logical, intent(out) :: ok
        logical(c_bool)      :: v
        v  = c_ptr_eq(obj, Py_GetConstant(Py_CONSTANT_TRUE))
        ok = v .or. c_ptr_eq(obj, Py_GetConstant(Py_CONSTANT_FALSE))
        if (.not. ok) call PyErr_SetString(PyExc_TypeError, c_loc(s_bool_req))
    end function

    ! UTF-8 bytes of a str, copied as-is into a Fortran string (multi-byte
    ! sequences arrive as individual chars). Non-str raises TypeError inside
    ! PyUnicode_AsUTF8AndSize.
    function FLAIR_str_from_PyObject(obj, ok) result(v)
        type(c_ptr), value        :: obj
        logical, intent(out)      :: ok
        character(:), allocatable :: v
        type(c_ptr) :: cs
        integer(c_ptrdiff_t) :: n, i
        character(kind=c_char), pointer :: buf(:)
        cs = PyUnicode_AsUTF8AndSize(obj, n)
        ok = c_associated(cs)
        if (.not. ok) then
            v = ""
            return
        end if
        allocate(character(len=n) :: v)
        if (n > 0) then
            call c_f_pointer(cs, buf, [n])
            do i = 1, n
                v(i:i) = buf(i)
            end do
        end if
    end function

    ! New str from a Fortran string interpreted as UTF-8; disassociated result
    ! with the error pending if the bytes are not valid UTF-8.
    function FLAIR_PyObject_from_str(s) result(r)
        character(len=*), intent(in) :: s
        type(c_ptr) :: r
        ! max(1, ...): keep c_loc off a zero-size object; "" passes size 0.
        character(kind=c_char, len=max(1, len(s))), target :: buf
        buf = s
        r = PyUnicode_FromStringAndSize(c_loc(buf), int(len(s), c_ptrdiff_t))
    end function

    ! New complex from a Fortran complex value (a helper rather than an inline
    ! PyComplex_FromDoubles call so the wrapped expression is evaluated once).
    function FLAIR_PyObject_from_dcomplex(z) result(r)
        complex(c_double_complex), intent(in) :: z
        type(c_ptr) :: r
        r = PyComplex_FromDoubles(real(z, c_double), aimag(z))
    end function

    ! ===== Error reporting =====
    ! Messages are assembled at run time rather than interned per call site by
    ! the generator, so a wrapper only carries the argument names themselves.

    ! A null-terminated C string as a Fortran string ("" when null). Only used
    ! on error paths, so the scan cap costs nothing in the common case.
    function FLAIR_c_str(p) result(s)
        type(c_ptr), value        :: p
        character(:), allocatable :: s
        character(kind=c_char), pointer :: buf(:)
        integer, parameter :: cap = 1024
        integer :: n, i
        if (.not. c_associated(p)) then
            s = ""
            return
        end if
        call c_f_pointer(p, buf, [cap])
        n = 0
        do i = 1, cap
            if (buf(i) == c_null_char) exit
            n = i
        end do
        allocate(character(len=n) :: s)
        do i = 1, n
            s(i:i) = buf(i)
        end do
    end function

    ! Decimal text of an integer, for assembling messages.
    function FLAIR_itoa(v) result(s)
        integer, value            :: v
        character(:), allocatable :: s
        character(len=32) :: buf
        write (buf, '(i0)') v
        s = trim(buf)
    end function

    ! PyErr_SetString from a Fortran string. PyErr_SetString copies the bytes
    ! into a new str, so the automatic target need not outlive the call.
    subroutine FLAIR_err_str(exc, msg)
        type(c_ptr), value           :: exc
        character(len=*), intent(in) :: msg
        character(kind=c_char, len=len(msg) + 1), target :: buf
        buf = msg//c_null_char
        call PyErr_SetString(exc, c_loc(buf))
    end subroutine

    ! prefix // <the C string `name`> // suffix.
    subroutine FLAIR_err_named(exc, prefix, name, suffix)
        type(c_ptr), value           :: exc, name
        character(len=*), intent(in) :: prefix, suffix
        call FLAIR_err_str(exc, prefix//FLAIR_c_str(name)//suffix)
    end subroutine

    ! ===== Argument marshalling =====

    ! Bind the call's positional and keyword arguments to the dummies named by
    ! `names`, filling objs(1:n) with borrowed references.
    !
    ! An argument that is absent, or an optional one given as None, yields
    ! c_null_ptr -- so the caller tests presence with c_associated alone. None
    ! is *not* folded away for a required dummy: that must reach the converter
    ! and fail there, as passing None where a value is required is a type
    ! error, not a missing argument.
    !
    ! Sets the exception and returns .false. on a surplus positional argument,
    ! an argument given both by name and by position, a missing required
    ! argument, or an unrecognised keyword (named in the message).
    function FLAIR_parse_args(args, kwds, names, required, objs) result(ok)
        type(c_ptr), value       :: args, kwds
        type(c_ptr), intent(in)  :: names(:)
        logical,     intent(in)  :: required(:)
        type(c_ptr), intent(out) :: objs(:)
        logical :: ok

        integer :: n, i, nkw
        integer(c_ptrdiff_t) :: nargs, pos
        type(c_ptr) :: v, key, val, nonep
        logical :: known

        ok = .false.
        n = size(objs)
        nonep = Py_GetConstant(Py_CONSTANT_NONE)

        nargs = 0_c_ptrdiff_t
        if (c_associated(args)) nargs = PyTuple_Size(args)
        if (nargs > int(n, c_ptrdiff_t)) then
            call FLAIR_err_str(PyExc_TypeError, &
                "takes at most "//FLAIR_itoa(n)//" positional argument(s) but "// &
                FLAIR_itoa(int(nargs))//" were given")
            return
        end if

        nkw = 0
        do i = 1, n
            objs(i) = c_null_ptr
            if (int(i, c_ptrdiff_t) <= nargs) &
                objs(i) = PyTuple_GetItem(args, int(i - 1, c_ptrdiff_t))
            if (c_associated(kwds)) then
                v = PyDict_GetItemString(kwds, names(i))
                if (c_associated(v)) then
                    if (c_associated(objs(i))) then
                        call FLAIR_err_named(PyExc_TypeError, "argument '", &
                            names(i), "' given by name and position")
                        return
                    end if
                    objs(i) = v
                    nkw = nkw + 1
                end if
            end if
            if (.not. required(i)) then
                ! An explicit None selects the absent branch of an optional.
                if (c_associated(objs(i))) then
                    if (c_ptr_eq(objs(i), nonep)) objs(i) = c_null_ptr
                end if
            else if (.not. c_associated(objs(i))) then
                call FLAIR_err_named(PyExc_TypeError, &
                    "missing required argument '", names(i), "'")
                return
            end if
        end do

        ! Every keyword must have been claimed above; if the counts disagree,
        ! walk the dict to name the offending key.
        if (c_associated(kwds)) then
            if (PyDict_Size(kwds) /= int(nkw, c_ptrdiff_t)) then
                pos = 0_c_ptrdiff_t
                do while (PyDict_Next(kwds, pos, key, val) /= 0)
                    known = .false.
                    do i = 1, n
                        if (PyUnicode_CompareWithASCIIString(key, names(i)) == 0) &
                            known = .true.
                    end do
                    if (.not. known) then
                        call FLAIR_err_str(PyExc_TypeError, "'"// &
                            FLAIR_str_key(key)//"' is an invalid keyword argument")
                        return
                    end if
                end do
                ! Counts disagreed but every key matched: a non-str key.
                call FLAIR_err_str(PyExc_TypeError, "keywords must be strings")
                return
            end if
        end if
        ok = .true.
    end function

    ! A dict key as text, for error messages; falls back to a placeholder for
    ! a non-str key rather than letting its exception escape.
    function FLAIR_str_key(key) result(s)
        type(c_ptr), value        :: key
        character(:), allocatable :: s
        logical :: ok
        s = FLAIR_str_from_PyObject(key, ok)
        if (.not. ok) then
            call PyErr_Clear()
            s = "?"
        end if
    end function

    ! Reject a string too long for an explicit-length character dummy, which
    ! would otherwise be truncated silently by the assignment.
    function FLAIR_check_len(s, maxlen, name) result(ok)
        character(len=*), intent(in) :: s
        integer,     value :: maxlen
        type(c_ptr), value :: name
        logical :: ok
        ok = len(s) <= maxlen
        if (.not. ok) call FLAIR_err_str(PyExc_ValueError, "argument '"// &
            FLAIR_c_str(name)//"' exceeds character length "//FLAIR_itoa(maxlen))
    end function

    ! ===== Wrapper instances =====

    ! The Fortran object held by a wrapper instance. No type check: callers
    ! below have already established that `obj` is one of our instances.
    function FLAIR_data_ptr(obj) result(p)
        type(c_ptr), value :: obj
        type(c_ptr) :: p
        type(FLAIR_object_t), pointer :: o
        call c_f_pointer(obj, o)
        p = o%data
    end function

    ! .true. if obj is an instance of type_obj. A failed check leaves the
    ! exception pending: either the one IsInstance itself raised (it returns
    ! -1 in that case, which must not be clobbered) or `msg`.
    function FLAIR_check_instance(obj, type_obj, msg) result(ok)
        type(c_ptr), value :: obj, type_obj, msg
        logical :: ok
        ok = PyObject_IsInstance(obj, type_obj) == 1
        if (ok) return
        if (.not. c_associated(PyErr_Occurred())) &
            call PyErr_SetString(PyExc_TypeError, msg)
    end function

    ! isinstance-checked unwrap of a wrapper instance to the address of the
    ! Fortran object it holds. c_null_ptr with TypeError pending on mismatch.
    function FLAIR_unwrap(obj, type_obj, msg) result(p)
        type(c_ptr), value :: obj, type_obj, msg
        type(c_ptr) :: p
        p = c_null_ptr
        if (FLAIR_check_instance(obj, type_obj, msg)) p = FLAIR_data_ptr(obj)
    end function

    ! As FLAIR_unwrap, with the message built from the two names the wrapper
    ! already carries: the dummy argument's name and the expected class name.
    function FLAIR_unwrap_arg(obj, type_obj, name, cls) result(p)
        type(c_ptr), value :: obj, type_obj, name, cls
        type(c_ptr) :: p
        p = c_null_ptr
        if (PyObject_IsInstance(obj, type_obj) /= 1) then
            if (.not. c_associated(PyErr_Occurred())) &
                call FLAIR_err_str(PyExc_TypeError, "argument '"// &
                    FLAIR_c_str(name)//"' must be a "//FLAIR_c_str(cls)// &
                    " instance")
            return
        end if
        p = FLAIR_data_ptr(obj)
    end function

    ! A wrapper module's type objects are set by its PyInit, so a null one
    ! means a consumer reached this type before the module defining it was
    ! imported. Reported rather than dereferenced.
    function FLAIR_type_ready(type_obj, noinit_msg) result(ok)
        type(c_ptr), value :: type_obj, noinit_msg
        logical :: ok
        ok = c_associated(type_obj)
        if (.not. ok) call PyErr_SetString(PyExc_RuntimeError, noinit_msg)
    end function

    ! Body of a generated <t>_from_PyObject converter.
    function FLAIR_unwrap_checked(obj, type_obj, msg, noinit_msg) result(p)
        type(c_ptr), value :: obj, type_obj, msg, noinit_msg
        type(c_ptr) :: p
        p = c_null_ptr
        if (.not. FLAIR_type_ready(type_obj, noinit_msg)) return
        p = FLAIR_unwrap(obj, type_obj, msg)
    end function

    ! Body of a generated <t>_view_PyObject converter: a new instance aliasing
    ! `data`, which lives inside the Fortran object owned by `owner`. The view
    ! holds a reference on the owner for as long as it lives, so the storage it
    ! points into cannot be freed underneath it (NumPy's `base` pattern).
    function FLAIR_new_view(type_obj, data, owner, noinit_msg) result(r)
        type(c_ptr), value :: type_obj, data, owner, noinit_msg
        type(c_ptr) :: r
        type(FLAIR_object_t), pointer :: o
        r = c_null_ptr
        if (.not. FLAIR_type_ready(type_obj, noinit_msg)) return
        r = PyType_GenericAlloc(type_obj, 0_c_ptrdiff_t)
        if (.not. c_associated(r)) return
        call c_f_pointer(r, o)
        o%data = data
        o%owner = owner
        call Py_IncRef(owner)
    end function

    ! The type-independent half of tp_dealloc. For a view, drops the keep-alive
    ! reference on the owner and returns c_null_ptr (the storage is not ours);
    ! otherwise returns the Fortran object for the caller to deallocate, which
    ! is the one step that needs the concrete type.
    function FLAIR_dealloc_data(self) result(p)
        type(c_ptr), value :: self
        type(c_ptr) :: p
        type(FLAIR_object_t), pointer :: o
        p = c_null_ptr
        call c_f_pointer(self, o)
        if (c_associated(o%owner)) then
            call Py_DecRef(o%owner)
        else
            p = o%data
        end if
    end function

    ! ===== C API tables =====
    ! Every row is several assignments because c_loc of a saved target is not a
    ! constant expression, so the tables cannot be initialised declaratively.
    ! Filling them here keeps one call per row at the call site.

    subroutine FLAIR_set_method(tbl, i, name, fn, flags)
        type(PyMethodDef_t), intent(inout) :: tbl(*)
        integer,        value :: i
        type(c_ptr),    value :: name
        type(c_funptr), value :: fn
        integer(c_int), value :: flags
        tbl(i)%ml_name  = name
        tbl(i)%ml_meth  = transfer(fn, c_null_ptr)
        tbl(i)%ml_flags = flags
        tbl(i)%ml_pad   = 0
        tbl(i)%ml_doc   = c_null_ptr
    end subroutine

    ! The all-null row that terminates a method table.
    subroutine FLAIR_end_methods(tbl, i)
        type(PyMethodDef_t), intent(inout) :: tbl(*)
        integer, value :: i
        tbl(i)%ml_name  = c_null_ptr
        tbl(i)%ml_meth  = c_null_ptr
        tbl(i)%ml_flags = 0
        tbl(i)%ml_pad   = 0
        tbl(i)%ml_doc   = c_null_ptr
    end subroutine

    subroutine FLAIR_set_getset(tbl, i, name, get, set)
        type(PyGetSetDef_t), intent(inout) :: tbl(*)
        integer,        value :: i
        type(c_ptr),    value :: name
        type(c_funptr), value :: get, set
        tbl(i)%name    = name
        tbl(i)%get     = transfer(get, c_null_ptr)
        tbl(i)%set     = transfer(set, c_null_ptr)
        tbl(i)%doc     = c_null_ptr
        tbl(i)%closure = c_null_ptr
    end subroutine

    subroutine FLAIR_end_getset(tbl, i)
        type(PyGetSetDef_t), intent(inout) :: tbl(*)
        integer, value :: i
        tbl(i)%name    = c_null_ptr
        tbl(i)%get     = c_null_ptr
        tbl(i)%set     = c_null_ptr
        tbl(i)%doc     = c_null_ptr
        tbl(i)%closure = c_null_ptr
    end subroutine

    subroutine FLAIR_set_slot(tbl, i, slot, pfunc)
        type(PyType_Slot_t), intent(inout) :: tbl(*)
        integer,        value :: i
        integer(c_int), value :: slot
        type(c_funptr), value :: pfunc
        tbl(i)%slot  = slot
        tbl(i)%pad   = 0
        tbl(i)%pfunc = transfer(pfunc, c_null_ptr)
    end subroutine

    ! A slot holding a table address rather than a function pointer.
    subroutine FLAIR_set_slot_ptr(tbl, i, slot, p)
        type(PyType_Slot_t), intent(inout) :: tbl(*)
        integer,        value :: i
        integer(c_int), value :: slot
        type(c_ptr),    value :: p
        tbl(i)%slot  = slot
        tbl(i)%pad   = 0
        tbl(i)%pfunc = p
    end subroutine

    ! The zero slot that terminates a slot table.
    subroutine FLAIR_end_slots(tbl, i)
        type(PyType_Slot_t), intent(inout) :: tbl(*)
        integer, value :: i
        tbl(i)%slot  = 0
        tbl(i)%pad   = 0
        tbl(i)%pfunc = c_null_ptr
    end subroutine

    ! m_size = -1: no per-interpreter state, so the module does not support
    ! sub-interpreters -- matching what the generated wrappers assume.
    subroutine FLAIR_init_moduledef(def, name, methods)
        type(PyModuleDef_t), intent(inout) :: def
        type(c_ptr), value :: name, methods
        def%m_name     = name
        def%m_doc      = c_null_ptr
        def%m_size     = -1_c_ptrdiff_t
        def%m_methods  = methods
        def%m_slots    = c_null_ptr
        def%m_traverse = c_null_ptr
        def%m_clear    = c_null_ptr
        def%m_free     = c_null_ptr
    end subroutine

    ! Build a heap type from `slots` and bind it into `md` under `attr`.
    ! Returns the type (a borrowed view of the module's reference, which keeps
    ! it alive for view getters and isinstance checks), or c_null_ptr with the
    ! exception pending. The caller owns unwinding `md` on failure.
    !
    ! The spec is a local because PyType_FromSpec reads it only during the
    ! call. What it *does* retain is `qualname` (straight into tp_name) and the
    ! method/getset tables the slots point at, so those must outlive the type
    ! -- which is why the generated wrapper keeps all of them `target, save`.
    function FLAIR_add_type(md, qualname, attr, basicsize, slots) result(type_obj)
        type(c_ptr), value :: md, qualname, attr
        integer(c_int), value :: basicsize
        type(PyType_Slot_t), intent(in), target :: slots(*)
        type(c_ptr) :: type_obj
        type(PyType_Spec_t), target :: spec
        integer(c_int) :: rc
        spec%name      = qualname
        spec%basicsize = basicsize
        spec%itemsize  = 0
        spec%flags     = Py_TPFLAGS_DEFAULT
        spec%pad       = 0
        spec%slots     = c_loc(slots(1))
        type_obj = PyType_FromSpec(c_loc(spec))
        if (.not. c_associated(type_obj)) return
        rc = PyModule_AddObjectRef(md, attr, type_obj)
        if (rc < 0) then
            call Py_DecRef(type_obj)
            type_obj = c_null_ptr
        end if
    end function

    ! ===== NumPy arrays =====

    ! Coerce to an F-contiguous array of exactly `npy_type` and `rank`, and
    ! report its extents. `writeback` adds WRITEBACKIFCOPY so that a coercion
    ! copy is flushed back into the caller's array by FLAIR_array_release (a
    ! no-op when no copy was made, i.e. the zero-copy fast path).
    ! c_null_ptr with the exception pending on failure.
    function FLAIR_array_from_PyObject(obj, npy_type, rank, writeback, dims) result(arr)
        type(c_ptr),    value :: obj
        integer(c_int), value :: npy_type, rank
        logical,        value :: writeback
        integer(c_ptrdiff_t), intent(out) :: dims(*)
        type(c_ptr) :: arr
        integer(c_int) :: reqs, k
        arr = c_null_ptr
        if (.not. c_associated(numpy_api_ptr)) then
            call PyErr_SetString(PyExc_RuntimeError, c_loc(s_numpy_req))
            return
        end if
        reqs = NPY_ARRAY_F_CONTIGUOUS
        if (writeback) reqs = reqs + NPY_ARRAY_WRITEBACKIFCOPY
        arr = PyArray_FromAny(obj, PyArray_DescrFromType(npy_type), rank, rank, &
                              reqs, c_null_ptr)
        if (.not. c_associated(arr)) return
        do k = 1_c_int, rank
            dims(k) = PyArray_DIM(arr, k - 1_c_int)
        end do
    end function

    ! Flush any writeback copy and drop the reference. Safe on c_null_ptr, so
    ! it doubles as the cleanup for an array that was never acquired.
    subroutine FLAIR_array_release(arr)
        type(c_ptr), value :: arr
        integer(c_int) :: rc
        if (.not. c_associated(arr)) return
        ! Resolve before decref: an unresolved writeback array warns and drops
        ! the write on decref. A no-op (0) for a non-writeback array.
        if (c_associated(numpy_api_ptr)) rc = PyArray_ResolveWritebackIfCopy(arr)
        call Py_DecRef(arr)
    end subroutine

end module python_api_mod
