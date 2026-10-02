cmake_minimum_required(VERSION 3.15)

# STM32CubeProgrammer external loaders, one directory per board and part.
# See README.md.

add_subdirectory(AT25XE_PresTagv3)
add_subdirectory(AT25XE_CompassTagv1)
add_subdirectory(AT25XE_UIUCTag)
add_subdirectory(RV3028_PresTagv3)
add_subdirectory(RV3028_UIUCTag)
