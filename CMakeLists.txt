include(FindPkgConfig)

#Standard stuff
cmake_minimum_required(VERSION 2.6)
project(net_device_listener)

#Library check
pkg_check_modules(LIBUDEV REQUIRED libudev>=175)

add_executable(net_device_listener net_device_listener.c)
target_link_libraries(net_device_listener ${LIBUDEV_LIBRARIES})
