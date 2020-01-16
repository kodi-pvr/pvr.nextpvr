#pragma once
/*
*      Copyright (C) 2020
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
*  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
*  MA 02110-1301  USA
*  http://www.gnu.org/copyleft/gpl.html
*
*/

#include  "../BackendRequest.h"
#include "DummyBuffer.h"
#include <sstream>

namespace timeshift {

  class TranscodedBuffer : public DummyBuffer
  {
  public:
    TranscodedBuffer() : DummyBuffer()
    {
      m_profile << "&profile=" << g_iResolution << "p+-+" << g_iBitrate << "Kbps";
      XBMC->Log(LOG_NOTICE, "TranscodedBuffer created! %s", m_profile.str().c_str());
    }

    ~TranscodedBuffer() {}

    bool Open(const std::string inputUrl);

    void Close();

    int TranscodeStatus();

    int Lease();

    bool CheckStatus();

    bool GetStreamInfo();

  private:
    std::ostringstream m_profile;

  };

}
