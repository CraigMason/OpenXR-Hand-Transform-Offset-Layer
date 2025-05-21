// SPDX-FileCopyrightText: 2021-2023 Arthur Brainville (Ybalrid) <ybalrid@ybalrid.info>
//
// SPDX-License-Identifier: MIT
//
// Initial Author: Arthur Brainville <ybalrid@ybalrid.info>

#include "layer_shims.hpp"

#include <cassert>
#include <iostream>

#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

constexpr float PI = 3.14159265358979323846f;

float gYawDeg = 180.0f;
float gPitchDeg = -90.0f;
XrVector3f translation = {
	0.0f,     // X
   -0.25f,    // Y (down 25 cm)
   -0.35f     // Z (forward 35 cm)
};

uint16_t gFrame = 0;

XrQuaternionf MultiplyQuaternions(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

void LoadRotationConfig() {
    const char* envPath = std::getenv("HAND_TRACKING_CONFIG_PATH");
    if (!envPath) {
        std::cerr << "HAND_TRACKING_CONFIG_PATH environment variable not set.\n";
        return;
    }

    std::string configPath(envPath);
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << configPath << "\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) continue;

        std::string key = line.substr(0, equalsPos);
        std::string valueStr = line.substr(equalsPos + 1);

        try {
            float value = std::stof(valueStr);

            if (key == "yaw") gYawDeg = value;
            else if (key == "pitch") gPitchDeg = value;
            else if (key == "translation_x") translation.x = value;
            else if (key == "translation_y") translation.y = value;
            else if (key == "translation_z") translation.z = value;

        } catch (const std::exception& e) {
            std::cerr << "Failed to convert value to float for key '" << key << "': " << valueStr << "\n";
        }
    }

    file.close();
}



XrQuaternionf FromEulerAngles(float yawDeg, float pitchDeg) {
    float yawRad = yawDeg * PI / 180.0f;
    float pitchRad = pitchDeg * PI / 180.0f;

    float halfYaw = yawRad / 2.0f;
    float halfPitch = pitchRad / 2.0f;

    XrQuaternionf qYaw   = { 0.0f, sinf(halfYaw), 0.0f, cosf(halfYaw) }; // Y-axis
    XrQuaternionf qPitch = { sinf(halfPitch), 0.0f, 0.0f, cosf(halfPitch) }; // X-axis

    return MultiplyQuaternions(qPitch, qYaw); // apply yaw then pitch
}


XrVector3f RotateVectorByQuaternion(const XrVector3f& v, const XrQuaternionf& q) {
    XrQuaternionf vQuat = { v.x, v.y, v.z, 0.0f };
    XrQuaternionf qConj = { -q.x, -q.y, -q.z, q.w };

    auto qv = MultiplyQuaternions(q, vQuat);
    auto result = MultiplyQuaternions(qv, qConj);

    return { result.x, result.y, result.z };
}

XrResult XRAPI_CALL thisLayer_xrLocateHandJointsEXT( XrHandTrackerEXT handTracker, const XrHandJointsLocateInfoEXT* locateInfo, XrHandJointLocationsEXT* locations) {

	//First time this runs, it will fetch the pointer from the loaded OpenXR dispatch table
	static PFN_xrLocateHandJointsEXT nextLayer_xrLocateHandJointsEXT = GetNextLayerFunction(xrLocateHandJointsEXT);

    XrResult result = nextLayer_xrLocateHandJointsEXT(handTracker, locateInfo, locations);
    if (result != XR_SUCCESS || !locations->isActive) return result;

    // Update the config once a second
    gFrame++;
    if(gFrame % 60 == 0) {
        LoadRotationConfig();
    }

	XrQuaternionf rotation = FromEulerAngles(gYawDeg, gPitchDeg);

    for (uint32_t i = 0; i < locations->jointCount; ++i) {

		auto& joint = locations->jointLocations[i];

		joint.pose.position = RotateVectorByQuaternion(joint.pose.position, rotation);

		// Apply translation
		joint.pose.position.x += translation.x;
		joint.pose.position.y += translation.y;
		joint.pose.position.z += translation.z;

		joint.pose.orientation = MultiplyQuaternions(rotation, joint.pose.orientation);
    }

    return XR_SUCCESS;
}

//IMPORTANT: to allow for multiple instance creation/destruction, the contect of the layer must be re-initialized when the instance is being destroyed.
//Hooking xrDestroyInstance is the best way to do that.
XRAPI_ATTR XrResult XRAPI_CALL thisLayer_xrDestroyInstance(XrInstance instance)
{
	PFN_xrDestroyInstance nextLayer_xrDestroyInstance = GetNextLayerFunction(xrDestroyInstance);

	OpenXRLayer::DestroyLayerContext();

	assert(nextLayer_xrDestroyInstance != nullptr);
	return nextLayer_xrDestroyInstance(instance);
}


//This functions returns the list of function pointers and name we implement, and is called during the initialization of the layer:
std::vector<OpenXRLayer::ShimFunction> ListShims()
{
	std::vector<OpenXRLayer::ShimFunction> functions;
	functions.emplace_back("xrDestroyInstance", PFN_xrVoidFunction(thisLayer_xrDestroyInstance));

	//List every functions that is callable on this API layer
	functions.emplace_back("xrLocateHandJointsEXT", PFN_xrVoidFunction(thisLayer_xrLocateHandJointsEXT));

	return functions;
}
