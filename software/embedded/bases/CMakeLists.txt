cmake_minimum_required(VERSION 3.15)

#
# Define make variables relative to current directory
#

message("Configure Bases")

add_subdirectory(bittag-base-jlcpcb-v3)
add_subdirectory(bittag-base-v7)
add_subdirectory(tag-breakout-base-jlcpcb32-v1)
