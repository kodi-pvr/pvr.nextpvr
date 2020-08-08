/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
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

  m_prebuffer = m_settings.m_prebuffer5;

  if (m_channel_id != 0)
  {
    std::string timeshift = "/services/service?method=channel.stream.start&channel_id=" + std::to_string(m_channel_id);
    std::string response;
    if (m_request.DoRequest(timeshift.c_str(), response) != HTTP_OK)
    {
      return false;
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Missing channel for ClientTImeShift");
    return false;
  }

  time_t timeout = time(nullptr) + 20;
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    if ( ClientTimeShift::GetStreamInfo())
    {
      if  ( m_stream_duration  > m_settings.m_prebuffer )
      {
        break;
      }
    }
    Lease();
  } while (!m_complete && (timeout > time(nullptr)));

  if (m_complete || m_stream_duration == 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not buffer stream");
    StreamStop();
    return false;
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
  if (m_stream_duration > m_settings.m_timeshiftBufferSeconds)
  {
    int64_t startSlipBuffer = m_stream_length - (m_settings.m_timeshiftBufferSeconds * m_stream_length / m_stream_duration);
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
  std::string response;
  if (m_request.DoRequest("/services/service?method=channel.stream.stop", response) != HTTP_OK)
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

  if (m_stream_duration > m_settings.m_timeshiftBufferSeconds)
  {
    int64_t startSlipBuffer = m_stream_length - (m_settings.m_timeshiftBufferSeconds * m_stream_length/m_stream_duration);
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
  if (m_request.DoRequest("/services/service?method=channel.stream.info", response) == HTTP_OK)
  {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(response.c_str()) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* filesNode = doc.FirstChildElement("map");
      if (filesNode != nullptr)
      {
        stream_duration = strtoll(filesNode->FirstChildElement("stream_duration")->GetText(), nullptr, 0);
        if (stream_duration != 0)
        {
          m_stream_length = strtoll(filesNode->FirstChildElement("stream_length")->GetText(), nullptr, 0);
          m_stream_duration = stream_duration/1000;
          if (m_stream_duration > m_settings.m_timeshiftBufferSeconds)
          {
              m_rollingStartSeconds = m_streamStart + m_stream_duration - m_settings.m_timeshiftBufferSeconds;
          }
          XMLUtils::GetBoolean(filesNode, "complete", m_complete);
          if (m_complete == false)
          {
            if (m_nextRoll < time(nullptr))
            {
              m_nextRoll = time(nullptr) + m_settings.m_timeshiftBufferSeconds/3 + m_settings.m_serverTimeOffset;
            }
          }
          else
          {
            kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30190), kodi::GetLocalizedString(30053));
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
