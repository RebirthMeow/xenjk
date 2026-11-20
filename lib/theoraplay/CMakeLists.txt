# Bundled TheoraPlay (icculus.org) for xenjk. zlib license. Multithreaded
# Theora/Vorbis decoder glue used by cl_cin_theora.cpp.

include_directories(. ${OGG_INCLUDE_DIR} ${VORBIS_INCLUDE_DIR} ${THEORA_INCLUDE_DIR})

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# TheoraPlay's worker thread is native: Win32 threads on Windows, pthreads
# elsewhere, so link the platform's thread library there.
add_definitions(-DTHEORAPLAY_NO_THREAD_SAFETY=0)

add_library(bundled_theoraplay STATIC theoraplay.c)
if(NOT WIN32)
	set(THREADS_PREFER_PTHREAD_FLAG ON)
	find_package(Threads REQUIRED)
	target_link_libraries(bundled_theoraplay Threads::Threads)
endif()

set(THEORAPLAY_LIBRARIES   bundled_theoraplay PARENT_SCOPE)
set(THEORAPLAY_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/theoraplay PARENT_SCOPE)
mark_as_advanced(THEORAPLAY_LIBRARIES THEORAPLAY_INCLUDE_DIR)

set_property(TARGET bundled_theoraplay PROPERTY POSITION_INDEPENDENT_CODE True)
