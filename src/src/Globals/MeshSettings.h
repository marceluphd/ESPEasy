#ifndef GLOBALS_MESH_SETTINGS_H
#define GLOBALS_MESH_SETTINGS_H

#include "../DataStructs/MeshSettingsStruct.h"

#ifdef USES_WIFI_MESH

#include "src/WiFi/ESPEasyWiFiMesh.h"
class FloodingMesh;

extern MeshSettingsStruct MeshSettings;

extern FloodingMesh     *floodingMesh;

#endif // ifdef USES_WIFI_MESH

#endif // GLOBALS_MESH_SETTINGS_H