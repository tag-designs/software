/**
 * @file spi_bus.c
 * @brief SPI descriptor lifecycle and backend selector.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "spi_bus.h"

#if defined(HAL_USE_SPI) && (HAL_USE_SPI == TRUE)
#include "spi_bus_chibios.inc"
#else
#include "spi_bus_polled.inc"
#endif
