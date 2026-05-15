#ifndef SENSORUI_RESOURCES_H
#define SENSORUI_RESOURCES_H

// Static libraries do not always cause Qt resource objects to be registered in
// the final executable. Call this once before loading sensorui qrc:/ URLs.
void initializeSensorUiResources();

#endif // SENSORUI_RESOURCES_H
