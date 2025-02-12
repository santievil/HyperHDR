# Define the current source locations
SET(CURRENT_HEADER_DIR ${CMAKE_SOURCE_DIR}/include/grabber/linux/amlogic)
SET(CURRENT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic)

# Grabber
FILE ( GLOB AMLOGIC_SOURCES "${CURRENT_HEADER_DIR}/*.h" "${CURRENT_SOURCE_DIR}/*.cpp" )

add_library(amlogic-grabber OBJECT ${AMLOGIC_SOURCES} )

#add_library(amlogic-grabber
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/include/grabber/amlogic/AmlogicGrabber.h
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/include/grabber/amlogic/AmlogicWrapper.h
#	${CMAKE_SOURCE_DIR}/include/grabber/linux/amlogic/AmlogicGrabber.h
#	${CMAKE_SOURCE_DIR}/include/grabber/linux/amlogic/AmlogicWrapper.h
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/AmlogicGrabber.cpp
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/AmlogicWrapper.cpp
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/Amvideocap.h
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/ion.h
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/meson_ion.h
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/IonBuffer.h
#	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/IonBuffer.cpp
#)

#target_include_directories(amlogic-grabber PRIVATE
   # ${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/include
   # ${CMAKE_SOURCE_DIR}/sources/include  # <- Carpeta donde debería estar GrabberWrapper.h
   # ${CMAKE_SOURCE_DIR}/sources  # <- Carpeta raíz de los headers
   # ${CMAKE_SOURCE_DIR}/include
   # ${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/include/hyperion
   #${CMAKE_SOURCE_DIR}/include
#)
set(CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake/Qt6")
target_include_directories(amlogic-grabber PRIVATE
	${CMAKE_SOURCE_DIR}/include/utils-amlogic
)

find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)

target_link_libraries(amlogic-grabber
	# hyperion
	Qt${Qt_VERSION}::Core
	Qt${Qt_VERSION}::Network
	Qt${Qt_VERSION}::Gui
)

if(USE_PRECOMPILED_HEADERS AND COMMAND target_precompile_headers)
	target_precompile_headers(amlogic-grabber REUSE_FROM precompiled_hyperhdr_headers)
endif()
