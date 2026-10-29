// ==============================================================
// VkConfig.h
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKCONFIG_H
#define __VKCLIENT_VKCONFIG_H

namespace oapi { class GraphicsClient; }

class VkConfig {
public:
	VkConfig();
	~VkConfig() = default;

	void Reset();
	bool ReadParams(const char *cfgFile = "VulkanClient.cfg");
	bool WriteParams(const char *cfgFile = "VulkanClient.cfg") const;
	void SyncFromCore(const oapi::GraphicsClient *gc);

	int Anisotropy;            ///< Anisotropic filtering factor (1..16)
	int SceneMSAA;             ///< Scene MSAA sample count (1=off, 2, 4, 8)
	int bFlats;                ///< Enable planetary .flt terrain flattening (0/1)
	int ValidationLayers;      ///< Enable VK_LAYER_KHRONOS_validation when present (0/1)
	int EnableLimiter;         ///< Software frame-rate limiter toggle (0/1)
	double FrameRate;          ///< Target frame-rate limit [Hz]
	int DebugLvl;              ///< Log verbosity (0..4)

	int PlanetPreloadMode;     ///< 0=on demand, 1=preload
	int PlanetLoadFrequency;   ///< Background tile loader frequency [Hz]
	int PlanetTileLoadFlags;   ///< 0x1=cache dir, 0x2=archive, 0x3=both
	int NearClipPlane;         ///< Dynamic near clip plane adjustment (0/1)
	double VCNearPlane;        ///< Virtual cockpit near clip distance [m]
	int SketchpadFont;         ///< Sketchpad font quality preset (0..3)
	int ShaderCacheUse;        ///< Persist VkPipelineCache under Cache/VulkanClient (0/1)
};

extern VkConfig *g_vkConfig;

#endif // !__VKCLIENT_VKCONFIG_H
