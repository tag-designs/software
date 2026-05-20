# Compatibility marker for shared test/diagnostic entry points.
#
# The monitor-facing test() dispatcher now lives in tag_core. Device-specific
# test hooks still live with their device modules. Keep this no-op module so
# existing TAG_MODULES lists do not need churn while the build structure settles.
