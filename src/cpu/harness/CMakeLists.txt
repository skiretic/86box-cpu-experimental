#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script for the CPU test harness: the real CPU core,
#          recompiler, timing models and memory layer linked against stubs
#          for everything else, so a test blob can be run under the
#          interpreter and the recompiler and the two states compared.
#

add_executable(cpu_harness
    harness.c
    gen.c
    stubs.c
    ${CMAKE_SOURCE_DIR}/src/mem/mem.c
    ${CMAKE_SOURCE_DIR}/src/mem/mmu_2386.c
    ${CMAKE_SOURCE_DIR}/src/mem/smram.c
    ${CMAKE_SOURCE_DIR}/src/timer.c
)

set_target_properties(cpu_harness PROPERTIES MACOSX_BUNDLE FALSE)
target_include_directories(cpu_harness PRIVATE ${CMAKE_SOURCE_DIR}/src/cpu ${CMAKE_SOURCE_DIR}/src/codegen)
target_link_libraries(cpu_harness cpu cgt dynarec softfloat3e)
