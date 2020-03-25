/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <string>

namespace OS
{
  class CFile
  {
  public:
    static bool Exists(const std::string& strFileName);
  };
};
