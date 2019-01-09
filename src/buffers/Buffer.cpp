/*
*      Copyright (C) 2015 Sam Stenvall
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

#include "Buffer.h"
#include "Filesystem.h"
#include <sstream>

using namespace timeshift;
using namespace ADDON;

const int Buffer::DEFAULT_READ_TIMEOUT = 10;

bool Buffer::Open(const std::string inputUrl)
{
  m_active = true;
  if (!inputUrl.empty())
  {
    // Append the read timeout parameter
    XBMC->Log(LOG_DEBUG, "Buffer::Open() called! [ %s ]", inputUrl.c_str());
    std::stringstream ss;
    if (inputUrl.rfind("http", 0) == 0)
    {
    ss << inputUrl << "|connection-timeout=" << m_readTimeout;
    }
    else
    {
      ss << inputUrl;
    }
    m_inputHandle = XBMC->OpenFile(ss.str().c_str(), READ_NO_CACHE );
  }
  // Remember the start time and open the input
  m_startTime = time(nullptr);

  return m_inputHandle != nullptr;
}

Buffer::~Buffer()
{
  Buffer::Close();
}

void Buffer::Close()
{
  m_active = false;
  CloseHandle(m_inputHandle);
}

void Buffer::CloseHandle(void *&handle)
{
  if (handle)
  {
    XBMC->CloseFile(handle);
    handle = nullptr;
  }
}
