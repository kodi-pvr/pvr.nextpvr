/*
 *  Copyright (C) 2015-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2015 Sam Stenvall
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "ClientTimeshift.h"
#include  "../BackendRequest.h"
#include "../utilities/XMLUtils.h"
#include <kodi/General.h>

using namespace timeshift;
using namespace NextPVR::utilities;

bool ClientTimeShift::Open(const std::string inputUrl)
{
  m_isPaused = false;
  m_stream_length = 0;
  m_stream_duration = 0;
  m_nextLease = 0;
  m_nextRoll = 0;
  m_nextStreamInfo = 0;
  m_isLive = true;
  m_rollingStartSeconds = 0;
  m_bytesPerSecond = 0;
  m_complete = false;

  m_prebuffer = m_settings->m_prebuffer5;

  if (m_channel_id != 0)
  {
    std::string timeshift = "channel.stream.start&channel_id=" + std::to_string(m_channel_id);
    if (!m_request.DoActionRequest(timeshift))
    {
      return false;
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Missing channel for ClientTImeShift");
    return false;
  }

  time_t timeout = 20;

  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    timeout--;
    if ( ClientTimeShift::GetStreamInfo())
    {
      if  (m_stream_length > 50000)
      {
        break;
      }
    }
    if (timeout == 10)
      Lease();
  } while (!m_complete && timeout != 0);

  if (m_complete || timeout ==  0)
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not buffer stream");
    StreamStop();
    return false;
  }

  timeout = time(nullptr) + m_prebuffer;

  while (timeout > time(nullptr))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  if (Buffer::Open(inputUrl, 0 ) == false)
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not open streaming file");
    StreamStop();
    return false;
  }
  m_sourceURL = inputUrl + "&seek=";
  m_rollingStartSeconds = m_streamStart = time(nullptr);
  m_isLeaseRunning = true;
  m_leaseThread = std::thread([this]()
  {
    LeaseWorker();
  });

  return true;
}

PVR_ERROR ClientTimeShift::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  stimes.SetStartTime(m_streamStart);
  stimes.SetPTSStart(0);
  stimes.SetPTSBegin(static_cast<int64_t>(m_rollingStartSeconds - m_streamStart) * STREAM_TIME_BASE);
  stimes.SetPTSEnd(static_cast<int64_t>(time(nullptr) - m_streamStart) * STREAM_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

void ClientTimeShift::Close()
{
  if (m_active)
    Buffer::Close();
  m_isLeaseRunning = false;

  if (m_leaseThread.joinable())
    m_leaseThread.join();

  StreamStop();
  kodi::Log(ADDON_LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
  m_lastClose = time(nullptr);
}

void ClientTimeShift::Resume()
{
  ClientTimeShift::GetStreamInfo();
  if (m_stream_duration > m_settings->m_timeshiftBufferSeconds)
  {
    int64_t startSlipBuffer = m_stream_length - (m_settings->m_timeshiftBufferSeconds * m_stream_length / m_stream_duration);
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %lld %lld", __FUNCTION__, __LINE__, startSlipBuffer, m_streamPosition, m_stream_length.load());
    if (m_streamPosition < startSlipBuffer)
    {
      Seek(m_streamPosition, 0);
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
  }
}

void ClientTimeShift::StreamStop()
{
  if (!m_request.DoActionRequest("channel.stream.stop"))
  {
    kodi::Log(ADDON_LOG_ERROR, "%s:%d:", __FUNCTION__, __LINE__);
  }
}

int64_t ClientTimeShift::Seek(int64_t position, int whence)
{
  if (m_complete) return -1;
  if (m_active)
    Buffer::Close();
  ClientTimeShift::GetStreamInfo();

  if (m_stream_duration > m_settings->m_timeshiftBufferSeconds)
  {
    int64_t startSlipBuffer = m_stream_length - (m_settings->m_timeshiftBufferSeconds * m_stream_length/m_stream_duration);
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %lld %lld", __FUNCTION__, __LINE__, startSlipBuffer, position, m_stream_length.load());
    if (position < startSlipBuffer)
      position = startSlipBuffer;
  }

  kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %d %lld %d", __FUNCTION__, __LINE__, position, whence, m_stream_duration.load(), m_isPaused);
  if ( m_isPaused == true)
  {
    // skip while paused new restart position
    m_streamPosition = position;
  }
  const std::string seekingInput = m_sourceURL + std::to_string(position ) + "-";
  if ( Buffer::Open(seekingInput, 0) == false)
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not open file on seek");
    return  -1;
  }
  return position;
}

bool ClientTimeShift::GetStreamInfo()
{
  enum infoReturns
  {
    OK,
    XML_PARSE,
    HTTP_ERROR
  };
  int64_t stream_duration;
  infoReturns infoReturn = HTTP_ERROR;
  std::string response;

  if (m_complete)
  {
    kodi::Log(ADDON_LOG_ERROR, "NextPVR not updating completed rolling file");
    return ( m_stream_length != 0 );
  }
  // this call sends raw xml not a method response
  if (m_request.DoRequest("/service?method=channel.stream.info", response) == HTTP_OK)
  {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(response.c_str()) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* filesNode = doc.FirstChildElement("map");
      if (filesNode != nullptr)
      {
        stream_duration = strtoll(filesNode->FirstChildElement("stream_duration")->GetText(), nullptr, 10);
        if (stream_duration != 0)
        {
          m_stream_length = strtoll(filesNode->FirstChildElement("stream_length")->GetText(), nullptr, 10);
          m_stream_duration = stream_duration / 1000;
          if (m_stream_duration > m_settings->m_timeshiftBufferSeconds)
          {
              m_rollingStartSeconds = m_streamStart + m_stream_duration - m_settings->m_timeshiftBufferSeconds;
          }
          XMLUtils::GetBoolean(filesNode, "complete", m_complete);
          if (m_complete == false)
          {
            if (m_nextRoll < time(nullptr))
            {
              m_nextRoll = time(nullptr) + m_settings->m_timeshiftBufferSeconds/3 + m_settings->m_serverTimeOffset;
            }
          }
          else
          {
            kodi::QueueNotification(QUEUE_ERROR, kodi::addon::GetLocalizedString(30190), kodi::addon::GetLocalizedString(30053));
          }
        }
        kodi::Log(ADDON_LOG_DEBUG, "CT channel.stream.info %lld %lld %d %lld", m_stream_length.load(), stream_duration, m_complete, m_rollingStartSeconds.load());
        infoReturn = OK;
      }
    }
    else
    {
      infoReturn = XML_PARSE;
    }

  }
  m_nextStreamInfo = time(nullptr) + 10;
  return infoReturn == OK;
}
