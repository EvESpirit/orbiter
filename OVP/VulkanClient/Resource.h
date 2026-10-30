// ==============================================================
// Resource.h
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_RESOURCE_H
#define __VKCLIENT_RESOURCE_H

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

// Launchpad discovery string IDs (required by orbiter::DefVideoTab::ScanDir in TabVideo.cpp)
#define IDS_INFO                        1000 ///< Module info text shown in Launchpad (do not change ID)
#define IDS_TYPE                        1001 ///< Must equal "Graphics engines" for Launchpad discovery (do not change ID)

// Dialog template IDs
#define IDD_VK_ADVANCED                 104  ///< VulkanClient Advanced Setup modal dialog

// Advanced setup dialog control IDs
#define IDC_VK_ANISO                    2001 ///< Anisotropic filtering combo box
#define IDC_VK_MSAA                     2002 ///< Multisample anti-aliasing combo box
#define IDC_VK_VALIDATION               2003 ///< Khronos validation layers checkbox
#define IDC_VK_FLATS                    2004 ///< Planetary terrain flattening (.flt) checkbox
#define IDC_VK_FPSLIMIT                 2005 ///< Software frame-rate limiter toggle checkbox
#define IDC_VK_FPSVAL                   2006 ///< Target frame-rate edit box
#define IDC_VK_DBGLVL                   2007 ///< Log verbosity combo box
#define IDC_VK_DEVINFO                  2008 ///< Selected physical device summary label

#endif // !__VKCLIENT_RESOURCE_H
