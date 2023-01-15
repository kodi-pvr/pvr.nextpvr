/*
 *  Copyright (C) 2015-2023 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2015 Sam Stenvall
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "DummyBuffer.h"

using namespace timeshift;

PVR_ERROR DummyBuffer::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  return Buffer::GetStreamTimes(stimes);
}
