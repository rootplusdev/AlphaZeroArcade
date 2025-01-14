add_executable(c4_tests
        main/UnitTests.cpp
        Game.cpp
        )

add_executable(c4_exe
        main/Connect4.cpp
        Game.cpp
        PerfectPlayer.cpp
        )

add_library(c4_ffi
        SHARED shared/Connect4_ffi.cpp
        Game.cpp
)

set_target_properties(c4_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EXECUTABLE_OUTPUT_PATH}/tests")
set_target_properties(c4_exe PROPERTIES OUTPUT_NAME "c4")
set_target_properties(c4_ffi PROPERTIES OUTPUT_NAME "c4")

add_custom_target(c4
    DEPENDS c4_exe c4_ffi c4_tests
)

set(LOCAL_EXECUTABLES c4_exe c4_tests)
set(ALL_EXECUTABLES ${ALL_EXECUTABLES} ${LOCAL_EXECUTABLES} PARENT_SCOPE)

set(LOCAL_FFI_LIBS c4_ffi)
set(ALL_FFI_LIBS ${ALL_FFI_LIBS} ${LOCAL_FFI_LIBS} PARENT_SCOPE)
