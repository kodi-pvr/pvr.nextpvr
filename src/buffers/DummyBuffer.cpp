/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "DummyBuffer.h"

using namespace timeshift;

PVR_ERROR DummyBuffer::GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  return Buffer::GetStreamTimes(stimes);
}

