# Standalone build of the SkirtSim replay harness (not part of the game build).
#
#   cmake -S tools/cloth -B <build dir> -A x64
#   cmake --build <build dir> --config Release
#
# Produces skirt_replay_f (float skirt_real, the game's choice) and skirt_replay_d
# (double, for bit-level parity against the JS reference).
cmake_minimum_required(VERSION 3.10)
project(skirt_replay C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

set(SKIRT_SRC ${CMAKE_CURRENT_LIST_DIR}/../../codemp/rd-common/cloth/skirt_sim.c
	${CMAKE_CURRENT_LIST_DIR}/../../codemp/rd-common/cloth/cloth_asset.c)   # the game's asset reader

foreach(variant f d)
	add_executable(skirt_replay_${variant} skirt_replay.c ${SKIRT_SRC})
	if(variant STREQUAL "d")
		target_compile_definitions(skirt_replay_${variant} PRIVATE SKIRT_REAL_DOUBLE)
	endif()
	if(MSVC)
		# strict IEEE: no contraction or reassociation, so results match the JS reference
		target_compile_options(skirt_replay_${variant} PRIVATE /fp:precise /W4 /wd4127 /wd4996)
	else()
		target_compile_options(skirt_replay_${variant} PRIVATE -Wall -Wextra -ffp-contract=off)
		target_link_libraries(skirt_replay_${variant} PRIVATE m)
	endif()
endforeach()
