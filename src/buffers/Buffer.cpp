/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Buffer.h"
#include <kodi/General.h>

#include <sstream>

using namespace timeshift;


const int Buffer::DEFAULT_READ_TIMEOUT = 10;

bool Buffer::Open(const std::string inputUrl)
{
  return Buffer::Open(inputUrl, ADDON_READ_NO_CACHE);
}

bool Buffer::Open(const std::string inputUrl, int optFlag)
{
  m_active = true;
  if (!inputUrl.empty())
  {
    // Append the read timeout parameter
    kodi::Log(ADDON_LOG_DEBUG, "Buffer::Open() called! [ %s ]", inputUrl.c_str());
    std::stringstream ss;
    if (inputUrl.rfind("http", 0) == 0)
    {
      ss << inputUrl << "|connection-timeout=" << m_readTimeout;
    }
    else
    {
      ss << inputUrl;
    }
    m_inputHandle.OpenFile(ss.str(), optFlag);
  }
  // Remember the start time and open the input
  m_startTime = time(nullptr);
  return m_inputHandle.IsOpen();
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

void Buffer::CloseHandle(kodi::vfs::CFile& handle)
{
  if (handle.IsOpen())
  {
    handle.Close();
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
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
      int retval = Buffer::Lease();
      if ( retval == HTTP_OK)
      {
        m_nextLease = now + 7;
      }
      else if (retval == HTTP_BADREQUEST)
      {
        complete = true;
        kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30190), kodi::GetLocalizedString(30053));
      }
      else
      {
        kodi::Log(ADDON_LOG_ERROR, "channel.transcode.lease failed %lld", m_nextLease );
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
  return m_request.DoRequest("/service?method=channel.transcode.lease", response);
}
