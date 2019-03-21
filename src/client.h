#pragma once
/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef CLIENT_H
#define CLIENT_H

#include "libXBMC_addon.h"
#include "libXBMC_pvr.h"
#include "libKODI_guilib.h"

enum eStreamingMethod
{
  Timeshift = 0,
  RollingFile = 1,
  RealTime = 2
};

enum eNowPlaying
{
  NotPlaying = 0,
  TV = 1,
  Radio = 2,
  Recording = 3
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
extern bool             g_bRadioEnabled;
extern bool             g_bUseTimeshift;
extern bool             g_KodiLook;
extern int16_t          g_timeShiftBufferSeconds;
extern eStreamingMethod g_livestreamingmethod;
extern eNowPlaying g_NowPlaying;

extern ADDON::CHelper_libXBMC_addon *XBMC;
extern CHelper_libXBMC_pvr          *PVR;

extern int              g_iTVServerXBMCBuild;

extern int g_ServerTimeOffset;

typedef unsigned char byte;

/*!
 * @brief PVR macros for string exchange
 */
#define PVR_STRCPY(dest, source) do { strncpy(dest, source, sizeof(dest)-1); dest[sizeof(dest)-1] = '\0'; } while(0)
#define PVR_STRCLR(dest) memset(dest, 0, sizeof(dest))

#endif /* CLIENT_H */
