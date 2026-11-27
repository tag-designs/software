#[[
  UIUCadxl367: the UIUCTag board fitted with an ADXL367 instead of an ADXL362.

  Builds UIUCTag's sources (see project.mk) and shares its board, protocol
  and external-flash loader. A development target, not distributed: the
  UIUCTag prototypes carry the ADXL362.
#]]
add_embedded_target(UIUCadxl367 uiuctag_proto)
add_dependencies(UIUCadxl367 board-uiuctag)
