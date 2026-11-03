cmake_minimum_required(VERSION 3.20)

# check BOARD variable
if(NOT BOARD)
    set(BOARD native_sim/native/64)
    message("BOARD not defined, use default value: ${BOARD}")
else()
    message("Use BOARD: ${BOARD}")
endif()

# import zephyr library
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# define cmake project
project(breeze-module-referee)

add_library(app_interface INTERFACE)
target_link_libraries(app_interface
	INTERFACE
		ramenx
		breeze::module::models
)

target_sources(app PRIVATE
	./src/referee/referee_test.cpp
)
target_link_libraries(app
	PRIVATE
		app_interface
		breeze_module_referee
		robotpilots::algorithm
)
