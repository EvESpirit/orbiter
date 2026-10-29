// ==============================================================
// VkLog.h
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKLOG_H
#define __VKCLIENT_VKLOG_H

enum class VkLogLevel : int {
	Always  = 0,
	Error   = 1,
	Warning = 2,
	Info    = 3,
	Debug   = 4
};

void VkLogInit(const char *logFilePath, int maxLevel = 2);
void VkLogSetLevel(int maxLevel);
void VkLogClose();
void VkLogWrite(VkLogLevel level, const char *fmt, ...);

#define VkLogAlw(fmt, ...)  VkLogWrite(VkLogLevel::Always,  fmt, ##__VA_ARGS__)
#define VkLogErr(fmt, ...)  VkLogWrite(VkLogLevel::Error,   fmt, ##__VA_ARGS__)
#define VkLogWarn(fmt, ...) VkLogWrite(VkLogLevel::Warning, fmt, ##__VA_ARGS__)
#define VkLogInfo(fmt, ...) VkLogWrite(VkLogLevel::Info,    fmt, ##__VA_ARGS__)
#define VkLogDbg(fmt, ...)  VkLogWrite(VkLogLevel::Debug,   fmt, ##__VA_ARGS__)

#endif // !__VKCLIENT_VKLOG_H
