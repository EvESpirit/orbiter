// ==============================================================
// VkLog.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#define STRICT 1
#include "Orbitersdk.h"
#include "VkLog.h"
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {
std::mutex g_logMutex;
FILE *g_logFile = nullptr;
int g_maxLevel = 2;

const char *LevelTag(VkLogLevel lvl)
{
	switch (lvl) {
	case VkLogLevel::Always:  return "[VK]";
	case VkLogLevel::Error:   return "[VK][ERR]";
	case VkLogLevel::Warning: return "[VK][WRN]";
	case VkLogLevel::Info:    return "[VK][INF]";
	case VkLogLevel::Debug:   return "[VK][DBG]";
	}
	return "[VK]";
}
} // namespace

void VkLogInit(const char *logFilePath, int maxLevel)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	g_maxLevel = maxLevel;
	if (g_logFile) {
		std::fclose(g_logFile);
		g_logFile = nullptr;
	}
	if (logFilePath && logFilePath[0]) {
		g_logFile = std::fopen(logFilePath, "w");
		if (g_logFile) {
			std::fprintf(g_logFile, "==== OpenOrbiter VulkanClient Log ====\n");
			std::fflush(g_logFile);
		}
	}
}

void VkLogSetLevel(int maxLevel)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	g_maxLevel = maxLevel;
}

void VkLogClose()
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	if (g_logFile) {
		std::fprintf(g_logFile, "==== Log Closed ====\n");
		std::fclose(g_logFile);
		g_logFile = nullptr;
	}
}

void VkLogWrite(VkLogLevel level, const char *fmt, ...)
{
	if (!fmt)
		return;

	char msg[2048];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	msg[sizeof(msg) - 1] = '\0';

	bool mirrorToOrbiter = false;
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		if ((int)level > g_maxLevel && level != VkLogLevel::Always && level != VkLogLevel::Error)
			return;

		if (g_logFile) {
			std::fprintf(g_logFile, "%s %s\n", LevelTag(level), msg);
			std::fflush(g_logFile);
		}
		mirrorToOrbiter = (level <= VkLogLevel::Warning || g_maxLevel >= 3);
	}

	if (mirrorToOrbiter) {
		char orbLine[2100];
		std::snprintf(orbLine, sizeof(orbLine), "%s %s", LevelTag(level), msg);
		oapiWriteLog(orbLine);
	}
}
