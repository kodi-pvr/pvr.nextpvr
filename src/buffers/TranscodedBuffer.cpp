/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#include "TranscodedBuffer.h"
#include "../utilities/XMLUtils.h"
#include "tinyxml.h"
#include <limits>

using namespace NextPVR::utilities;
using namespace timeshift;

bool TranscodedBuffer::Open(const std::string inputUrl)
{
  if (m_channel_id != 0)
  {
    if (m_active)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      Close();
    }
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
    std::string response;
    std::string formattedRequest = "/services/service?method=channel.transcode.initiate&force=true&channel_id=" + std::to_string(m_channel_id) + "&profile=" + m_settings.m_resolution + "p";
    if (m_request.DoRequest(formattedRequest.c_str(), response) != HTTP_OK)
    {
      return false;
    }
    int status;
    do
    {
      status = TranscodeStatus();
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    } while (status >= 0 && status < 100);

    if (status == 100)
    {
      m_active = true;
      m_nextLease = 0;
      m_nextStreamInfo = std::numeric_limits<time_t>::max();
      m_nextRoll = std::numeric_limits<time_t>::max();
      m_isLeaseRunning = true;
      m_complete = false;
      m_leaseThread = std::thread([this]()
      {
        LeaseWorker();
      });
      return true;
    }
  }
  return false;
}

void TranscodedBuffer::Close()
{
  if (m_active)
  {
    m_active = false;
    m_complete = true;
    m_isLeaseRunning = false;
    if (m_leaseThread.joinable()){
      m_leaseThread.detach();
      kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %d", __FUNCTION__, __LINE__, m_leaseThread.joinable());
    }
    std::string response;
    m_request.DoRequest("/services/service?method=channel.transcode.stop", response);
  }
}

int TranscodedBuffer::TranscodeStatus()
{
  int percentage = -1;
  std::string response;
  if (m_request.DoRequest("/services/service?method=channel.transcode.status", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* rspNode = doc.FirstChildElement("rsp");
      if (rspNode != nullptr)
      {
        bool final;
        XMLUtils::GetInt(rspNode, "percentage", percentage);
        XMLUtils::GetBoolean(rspNode, "final", final);
        if (final)
        {
          kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %s", __FUNCTION__, __LINE__, response.c_str());
          if (percentage != 100)
            percentage = -1;
        }
      }
    }
  }

  return percentage;
}

int TranscodedBuffer::Lease()
{
  m_nextStreamInfo = time(nullptr) + 5;
  return true;
}

bool TranscodedBuffer::GetStreamInfo()
{
  /* should only be called at exit*/
  kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %d", __FUNCTION__, __LINE__, m_nextStreamInfo);
  Close();
  return true;
}
