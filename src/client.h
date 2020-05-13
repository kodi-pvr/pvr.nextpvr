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

extern ADDON::CHelper_libXBMC_addon *XBMC;
extern CHelper_libXBMC_pvr          *PVR;
extern class cPVRClientNextPVR *g_pvrclient;

typedef unsigned char byte;

#define READ_NO_CACHE 0

std::string UriEncode(const std::string sSrc);

#endif /* CLIENT_H */
