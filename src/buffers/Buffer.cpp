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

#include <sstream>

using namespace timeshift;
using namespace ADDON;

const int Buffer::DEFAULT_READ_TIMEOUT = 10;

bool Buffer::Open(const std::string inputUrl)
{
  return Buffer::Open(inputUrl, XFILE::READ_NO_CACHE);
}

bool Buffer::Open(const std::string inputUrl, int optFlag)
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
    m_inputHandle = XBMC->OpenFile(ss.str().c_str(), optFlag );
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
    XBMC->Log(LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
    handle = nullptr;
  }
}

void Buffer::LeaseWorker(void)
{
  while (m_isLeaseRunning == true)
  {
    time_t now = time(nullptr);
    bool complete = false;
    if ( m_nextLease <= now  && m_complete == false)
    {
      std::this_thread::yield();
      std::unique_lock<std::mutex> lock(m_mutex);
      int retval = Lease();
      if ( retval == HTTP_OK)
      {
        m_nextLease = now + 7;
      }
      else if (retval == HTTP_BADREQUEST)
      {
        complete = true;
        XBMC->QueueNotification(QUEUE_INFO, "Tuner required for recording");
      }
      else
      {
        XBMC->Log(LOG_ERROR, "channel.transcode.lease failed %lld", m_nextLease );
        m_nextLease = now + 1;
      }
    }
    if (m_nextStreamInfo <= now || m_nextRoll <= now || complete == true)
    {
      GetStreamInfo();
      if (complete) m_complete = true;
    }
    SLEEP(1000);
  }
}

int Buffer::Lease()
{
  std::string response;
  return NextPVR::m_backEnd->DoRequest("/service?method=channel.transcode.lease", response);
}
