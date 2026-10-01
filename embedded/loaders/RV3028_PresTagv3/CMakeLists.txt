# RV3028 register probe for PresTagv3 (and CompassTagv1, which uses the same
# RTC pins). Read-only by construction; see src/rv3028_probe.c.

add_embedded_loader(RV3028_PresTagv3)
