/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#ifndef CLIENT_H
#define CLIENT_H

#include "kodi/libXBMC_addon.h"
#include "kodi/libXBMC_pvr.h"
#include "kodi/libKODI_guilib.h"

enum eStreamingMethod
{
  Timeshift = 0,
  RollingFile = 1,
  RealTime = 2,
  Transcoded = 3,
  ClientTimeshift = 4
};

enum eNowPlaying
{
  NotPlaying = 0,
  TV = 1,
  Radio = 2,
  Recording = 3,
  Transcoding
};

#define DEFAULT_HOST                  "127.0.0.1"
#define DEFAULT_PORT                  8866
#define DEFAULT_PIN                   "0000"
#define DEFAULT_RADIO                 true
#define DEFAULT_USE_TIMESHIFT         false
#define DEFAULT_GUIDE_ARTWORK         false
#define DEFAULT_LIVE_STREAM           RealTime

extern std::string      g_szUserPath;         ///< The Path to the user directory inside user profile
extern std::string      g_szClientPath;       ///< The Path where this driver is located

/* Client Settings */
extern std::string      g_szHostname;
extern int              g_iPort;
extern std::string      g_szPin;
extern std::string      g_host_mac;
extern int              g_wol_timeout;
extern bool             g_wol_enabled;
extern bool             g_KodiLook;
extern bool             g_eraseIcons;
extern int16_t          g_timeShiftBufferSeconds;
extern eStreamingMethod g_livestreamingmethod;
extern eNowPlaying g_NowPlaying;
extern int              g_iResolution;

extern ADDON::CHelper_libXBMC_addon *XBMC;
extern CHelper_libXBMC_pvr          *PVR;

extern int              g_iTVServerXBMCBuild;

extern int g_ServerTimeOffset;

typedef unsigned char byte;

#define READ_NO_CACHE 0

/*!
 * @brief PVR macros for string exchange
 */
#define PVR_STRCPY(dest, source) do { strncpy(dest, source, sizeof(dest)-1); dest[sizeof(dest)-1] = '\0'; } while(0)
#define PVR_STRCLR(dest) memset(dest, 0, sizeof(dest))

#endif /* CLIENT_H */
