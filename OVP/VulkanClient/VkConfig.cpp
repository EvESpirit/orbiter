// ==============================================================
// VkConfig.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#define STRICT 1
#include "Orbitersdk.h"
#include "GraphicsAPI.h"
#include "VkConfig.h"
#include "VkLog.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

VkConfig *g_vkConfig = nullptr;

namespace {
std::string Trim(const std::string &s)
{
	size_t b = 0;
	while (b < s.size() && std::isspace((unsigned char)s[b])) ++b;
	size_t e = s.size();
	while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
	return s.substr(b, e - b);
}

bool EqualsIgnoreCase(const std::string &a, const char *b)
{
	return _stricmp(a.c_str(), b) == 0;
}
} // namespace

VkConfig::VkConfig()
{
	Reset();
	ReadParams();
}

void VkConfig::Reset()
{
	Anisotropy          = 8;
	SceneMSAA           = 1;
	bFlats              = 1;
	ValidationLayers    = 1;
	EnableLimiter       = 0;
	FrameRate           = 120.0;
	DebugLvl            = 2;

	PlanetPreloadMode   = 0;
	PlanetLoadFrequency = 50;
	PlanetTileLoadFlags = 0x3;
	NearClipPlane       = 1;
	VCNearPlane         = 0.1;
	SketchpadFont       = 2;
	ShaderCacheUse      = 1;
}

bool VkConfig::ReadParams(const char *cfgFile)
{
	if (!cfgFile || !cfgFile[0])
		return false;

	std::ifstream ifs(cfgFile);
	if (!ifs.is_open()) {
		WriteParams(cfgFile);
		return true;
	}

	std::string line;
	bool bFlatsExplicit = false;

	while (std::getline(ifs, line)) {
		size_t commentPos = line.find(';');
		if (commentPos != std::string::npos) line.erase(commentPos);
		commentPos = line.find("//");
		if (commentPos != std::string::npos) line.erase(commentPos);

		size_t eqPos = line.find('=');
		if (eqPos == std::string::npos) continue;

		std::string key = Trim(line.substr(0, eqPos));
		std::string val = Trim(line.substr(eqPos + 1));
		if (key.empty() || val.empty()) continue;

		if (EqualsIgnoreCase(key, "Anisotropy"))               Anisotropy = std::clamp(std::atoi(val.c_str()), 1, 16);
		else if (EqualsIgnoreCase(key, "SceneMSAA"))           SceneMSAA = std::clamp(std::atoi(val.c_str()), 1, 8);
		else if (EqualsIgnoreCase(key, "bFlats"))            { bFlats = std::atoi(val.c_str()) ? 1 : 0; bFlatsExplicit = true; }
		else if (EqualsIgnoreCase(key, "ValidationLayers"))    ValidationLayers = std::atoi(val.c_str()) ? 1 : 0;
		else if (EqualsIgnoreCase(key, "EnableLimiter"))       EnableLimiter = std::atoi(val.c_str()) ? 1 : 0;
		else if (EqualsIgnoreCase(key, "FrameRate"))           FrameRate = std::clamp(std::atof(val.c_str()), 10.0, 1000.0);
		else if (EqualsIgnoreCase(key, "DebugLvl"))            DebugLvl = std::clamp(std::atoi(val.c_str()), 0, 4);
		else if (EqualsIgnoreCase(key, "PlanetPreloadMode"))   PlanetPreloadMode = std::clamp(std::atoi(val.c_str()), 0, 1);
		else if (EqualsIgnoreCase(key, "PlanetLoadFrequency")) PlanetLoadFrequency = std::clamp(std::atoi(val.c_str()), 1, 1000);
		else if (EqualsIgnoreCase(key, "PlanetTileLoadFlags")) PlanetTileLoadFlags = std::clamp(std::atoi(val.c_str()), 1, 3);
		else if (EqualsIgnoreCase(key, "NearClipPlane"))       NearClipPlane = std::atoi(val.c_str()) ? 1 : 0;
		else if (EqualsIgnoreCase(key, "VCNearPlane"))         VCNearPlane = std::clamp(std::atof(val.c_str()), 0.01, 1.0);
		else if (EqualsIgnoreCase(key, "SketchpadFont"))       SketchpadFont = std::clamp(std::atoi(val.c_str()), 0, 3);
		else if (EqualsIgnoreCase(key, "ShaderCacheUse"))      ShaderCacheUse = std::atoi(val.c_str()) ? 1 : 0;
	}

	if (!bFlatsExplicit) {
		std::ifstream coreCfg("Orbiter.cfg");
		while (coreCfg.is_open() && std::getline(coreCfg, line)) {
			size_t eqPos = line.find('=');
			if (eqPos == std::string::npos) continue;
			std::string key = Trim(line.substr(0, eqPos));
			std::string val = Trim(line.substr(eqPos + 1));
			if (EqualsIgnoreCase(key, "EnableTerrainFlats")) {
				bFlats = (EqualsIgnoreCase(val, "FALSE") || val == "0") ? 0 : 1;
				break;
			}
		}
	}

	return true;
}

bool VkConfig::WriteParams(const char *cfgFile) const
{
	if (!cfgFile || !cfgFile[0])
		return false;

	std::ofstream ofs(cfgFile, std::ios::trunc);
	if (!ofs.is_open()) {
		VkLogWarn("Failed to write %s", cfgFile);
		return false;
	}

	ofs << "; === OpenOrbiter VulkanClient Configuration File ===\n";
	ofs << "Anisotropy = " << Anisotropy << "\n";
	ofs << "SceneMSAA = " << SceneMSAA << "\n";
	ofs << "bFlats = " << bFlats << "\n";
	ofs << "ValidationLayers = " << ValidationLayers << "\n";
	ofs << "EnableLimiter = " << EnableLimiter << "\n";
	ofs << "FrameRate = " << FrameRate << "\n";
	ofs << "DebugLvl = " << DebugLvl << "\n";
	ofs << "PlanetPreloadMode = " << PlanetPreloadMode << "\n";
	ofs << "PlanetLoadFrequency = " << PlanetLoadFrequency << "\n";
	ofs << "PlanetTileLoadFlags = " << PlanetTileLoadFlags << "\n";
	ofs << "NearClipPlane = " << NearClipPlane << "\n";
	ofs << "VCNearPlane = " << VCNearPlane << "\n";
	ofs << "SketchpadFont = " << SketchpadFont << "\n";
	ofs << "ShaderCacheUse = " << ShaderCacheUse << "\n";
	return true;
}

void VkConfig::SyncFromCore(const oapi::GraphicsClient *gc)
{
	if (!gc)
		return;
	if (const bool *pCoreFlats = static_cast<const bool *>(gc->GetConfigParam(CFGPRM_TERRAINFLATS))) {
		if (!(*pCoreFlats))
			bFlats = 0;
	}
}
