# Define the current source locations
SET(CURRENT_HEADER_DIR ${CMAKE_SOURCE_DIR}/include/grabber/linux/framebuffer)
SET(CURRENT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/sources/grabber/linux/framebuffer)

# Grabber
FILE ( GLOB FRAMEBUFFER_SOURCES "${CURRENT_HEADER_DIR}/*.h" "${CURRENT_SOURCE_DIR}/*.cpp" )

add_library(framebuffer-grabber OBJECT ${FRAMEBUFFER_SOURCES} )

message(STATUS "Qt5Core_DIR: ${Qt5Core_DIR}")
message(STATUS "Qt5Core_DIR: ${Qt5Network_DIR}")
message(STATUS "Qt5Gui_DIR: ${Qt5Gui_DIR}")

target_link_libraries(framebuffer-grabber
	Qt${Qt_VERSION}::Core
	Qt${Qt_VERSION}::Network
)

IF ( Qt5Gui_FOUND OR Qt6Gui_FOUND )
	MESSAGE (STATUS "Found Qt Gui library")
	target_link_libraries(framebuffer-grabber Qt${Qt_VERSION}::Gui)
else()
	MESSAGE (WARNING "Not found Qt Gui Library.")
endif()

if(USE_PRECOMPILED_HEADERS AND COMMAND target_precompile_headers)
	target_precompile_headers(framebuffer-grabber REUSE_FROM precompiled_hyperhdr_headers)
endif()
