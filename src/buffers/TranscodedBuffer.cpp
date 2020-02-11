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

#include "TranscodedBuffer.h"
#include "tinyxml.h"
#include "kodi/util/XMLUtils.h"

using namespace timeshift;
using namespace ADDON;

bool TranscodedBuffer::Open(const std::string inputUrl)
{
  if (m_channel_id != 0)
  {
    if (m_active)
    {
      SLEEP(1000);
      Close();
    }
    XBMC->Log(LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
    std::string response;
    std::string formattedRequest = "/services/service?method=channel.transcode.initiate&force=true&channel_id=" + std::to_string(m_channel_id) + m_profile.str();
    if (NextPVR::m_backEnd->DoRequest( formattedRequest.c_str(), response) != HTTP_OK)
    {
      return false;
    }
    int status;
    do
    {
      status = TranscodeStatus();
      SLEEP(1000);
    } while (status >= 0 && status < 100);

    if (status == 100)
    {
      m_active = true;
      m_nextLease = 0;
      m_nextStreamInfo = INT64_MAX;
      m_nextRoll = INT64_MAX;
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
      XBMC->Log(LOG_DEBUG, "%s:%d: %d", __FUNCTION__, __LINE__,m_leaseThread.joinable());
    }
    std::string response;
    NextPVR::m_backEnd->DoRequest("/services/service?method=channel.transcode.stop", response);
  }
}

int TranscodedBuffer::TranscodeStatus()
{
  int percentage = -1;
  std::string response;
  if (NextPVR::m_backEnd->DoRequest("/services/service?method=channel.transcode.status", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* rspNode = doc.FirstChildElement("rsp");
      if (rspNode != NULL)
      {
        bool final;
        XMLUtils::GetInt(rspNode,"percentage",percentage);
        XMLUtils::GetBoolean(rspNode,"final",final);
        if (final)
        {
          XBMC->Log(LOG_DEBUG, "%s:%d: %s", __FUNCTION__, __LINE__,response.c_str());
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
  XBMC->Log(LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
  m_nextStreamInfo = time(nullptr) + 5;
  return true;
}

bool TranscodedBuffer::GetStreamInfo()
{
  /* should only be called at exit*/
  XBMC->Log(LOG_DEBUG, "%s:%d: %d", __FUNCTION__, __LINE__,m_nextStreamInfo);
  Close();
  return true;
}
