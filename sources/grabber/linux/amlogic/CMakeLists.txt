add_library(amlogic-grabber
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/include/grabber/amlogic/AmlogicGrabber.h
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/include/grabber/amlogic/AmlogicWrapper.h
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/AmlogicGrabber.cpp
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/AmlogicWrapper.cpp
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/Amvideocap.h
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/ion.h
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/meson_ion.h
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/IonBuffer.h
	${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/IonBuffer.cpp
)

target_include_directories(amlogic-grabber PRIVATE
    ${CMAKE_SOURCE_DIR}/sources/grabber/linux/amlogic/include
    ${CMAKE_SOURCE_DIR}/sources/include  # <- Carpeta donde debería estar GrabberWrapper.h
    ${CMAKE_SOURCE_DIR}/sources  # <- Carpeta raíz de los headers
	${CMAKE_SOURCE_DIR}/include
)

find_package(Qt5 REQUIRED COMPONENTS Core Network Gui)

target_link_libraries(amlogic-grabber
	# hyperion
	Qt${Qt_VERSION}::Core
	Qt${Qt_VERSION}::Network
	Qt${Qt_VERSION}::Gui
)
