cmake_minimum_required(VERSION 3.18)
project(scene-collection-manager VERSION 0.0.3)
set(PROJECT_FULL_NAME "Scene Collection Manager")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

include_directories(${scene-collection-manager_BINARY_DIR})

find_package(Qt5Widgets)

set(CMAKE_AUTOMOC ON)

set(scene-collection-manager_HEADERS
	scene-collection-manager.hpp
	version.h)
set(scene-collection-manager_SOURCES
	scene-collection-manager.cpp)

if(WIN32)
	get_filename_component(ISS_FILES_DIR "${CMAKE_BINARY_DIR}\\..\\package" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_FILES_DIR}" ISS_FILES_DIR)
	get_filename_component(ISS_PACKAGE_DIR "${CMAKE_PACKAGE_PREFIX}\\.." ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_PACKAGE_DIR}" ISS_PACKAGE_DIR)
	get_filename_component(ISS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_SOURCE_DIR}" ISS_SOURCE_DIR)
	configure_file("installer.iss.in"
			"${PROJECT_BINARY_DIR}/installer.iss"
		)
	configure_file(resource.rc.in scene-collection-manager.rc)
	list(APPEND scene-collection-manager_SOURCES
		scene-collection-manager.rc)
endif()

if(BUILD_OUT_OF_TREE)
    find_package(LibObs REQUIRED)
endif()

qt5_wrap_ui(scene-collection-manager_UI_HEADERS SceneCollectionManager.ui)

add_library(scene-collection-manager MODULE
	${scene-collection-manager_HEADERS}
	${scene-collection-manager_SOURCES}
	${scene-collection-manager_UI_HEADERS})

target_link_libraries(scene-collection-manager
		obs-frontend-api
		Qt5::Widgets
		libobs)

if(BUILD_OUT_OF_TREE)
    if(NOT LIB_OUT_DIR)
        set(LIB_OUT_DIR "/lib/obs-plugins")
    endif()
    if(NOT DATA_OUT_DIR)
        set(DATA_OUT_DIR "/share/obs/obs-plugins/scene-collection-manager")
    endif()
    set_target_properties(scene-collection-manager PROPERTIES PREFIX "")
    install(TARGETS scene-collection-manager
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${LIB_OUT_DIR})
    install(DIRECTORY data/locale
        DESTINATION ${CMAKE_INSTALL_PREFIX}/${DATA_OUT_DIR})
else()
    target_include_directories(scene-collection-manager PRIVATE
        "${CMAKE_SOURCE_DIR}/UI/obs-frontend-api")
    set_target_properties(scene-collection-manager PROPERTIES FOLDER "plugins/exeldro")
    install_obs_plugin_with_data(scene-collection-manager data)
endif()
