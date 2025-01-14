cmake_minimum_required(VERSION 3.17)

project(alphazero)


if(CONDA_PREFIX)
    # Append to CMAKE_PREFIX_PATH and link_directories
    list(APPEND CMAKE_PREFIX_PATH "${CONDA_PREFIX}")
    link_directories(${CONDA_PREFIX}/lib)
endif()

# We enabled NDEBUG even for debug builds because a gcc bug leads to a spurious assert() deep
# in the eigen3 library
add_definitions(-DNDEBUG)

find_package (Eigen3 3.4 REQUIRED NO_MODULE)
find_package(Torch REQUIRED PATHS ${MY_TORCH_DIR})
find_package(Boost COMPONENTS program_options filesystem json log log_setup REQUIRED)

set(CMAKE_CXX_STANDARD  20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-array-bounds -O3 ${TORCH_CXX_FLAGS} -fconcepts-diagnostics-depth=5 -ftemplate-backtrace-limit=50 ${EXTRA_DEFINITIONS}")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH  ${CMAKE_BINARY_DIR}/lib)

SET(EXTERNAL_LIBS ${TORCH_LIBRARIES} ${Boost_LIBRARIES})
set(COMMON_LIBS "")  # each item of ALL_EXECUTABLES and ALL_FFI_LIBS will be linked with these
set(ALL_LIBS "")  # these are linked with each lib of EXTERNAL_LIBS, separate from ALL_FFI_LIBS
set(ALL_EXECUTABLES "")
set(ALL_FFI_LIBS "")  # these are linked with each lib of EXTERNAL_LIBS, separate from ALL_LIBS

include_directories("cpp/include")
include_directories("cpp")  # for inline/ subdirectory
include_directories(${EIGEN3_INCLUDE_DIRS})
include_directories(${MY_EIGENRAND_DIR})
include_directories(${MY_TINYEXPR_DIR})

add_subdirectory("cpp/src")
add_subdirectory("cpp/src/games/connect4")
add_subdirectory("cpp/src/games/othello")
add_subdirectory("cpp/src/games/tictactoe")

foreach(tgt ${ALL_LIBS} ${ALL_FFI_LIBS})
    foreach(lib ${EXTERNAL_LIBS})
        target_link_libraries(${tgt} ${lib})
    endforeach()
endforeach()

foreach(tgt ${ALL_EXECUTABLES} ${ALL_FFI_LIBS})
    foreach(lib ${COMMON_LIBS})
        target_link_libraries(${tgt} ${lib})
    endforeach()
    set_property(TARGET ${tgt} PROPERTY CXX_STANDARD 20)
endforeach()

# https://stackoverflow.com/a/31390758/543913
macro(print_all_variables)
    message(STATUS "print_all_variables------------------------------------------{")
    get_cmake_property(_variableNames VARIABLES)
    foreach (_variableName ${_variableNames})
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
    message(STATUS "print_all_variables------------------------------------------}")
endmacro()

# print_all_variables()
