cmake_minimum_required(VERSION 3.20)

# check BOARD variable
if(NOT BOARD)
    set(BOARD damiao_mc02)
    message("BOARD not defined, use default value: ${BOARD}")
else()
    message("Use BOARD: ${BOARD}")
endif()

# import zephyr library
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# define cmake project
project(breeze-module-referee)

target_sources(app PRIVATE
    ./src/main.cpp
)

target_link_libraries(app PRIVATE
    breeze_module_msgpipe
    breeze_module_referee
	breeze::module::models
    robotpilots::algorithm
)
