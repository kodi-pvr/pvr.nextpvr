/*
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

#include "ClientTimeshift.h"
#include  "../BackendRequest.h"
#include "tinyxml.h"
#include "kodi/util/XMLUtils.h"

//#define FMODE 1

using namespace timeshift;

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

  if (g_NowPlaying == TV)
  {
    m_chunkSize = m_liveChunkSize;
  }
  else
    m_chunkSize = 4;

  XBMC->Log(LOG_DEBUG, "%s:%d: %d", __FUNCTION__, __LINE__, m_chunkSize);

  if (m_channel_id != 0)
  {
    std::string timeshift = "/services/service?method=channel.stream.start&channel_id=" + std::to_string(m_channel_id);
    std::string response;
    if (NextPVR::m_backEnd->DoRequest(timeshift.c_str(), response) != HTTP_OK)
    {
      return false;
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Missing channel for ClientTImeShift");
    return false;
  }

  time_t timeout = time(nullptr) + 20;
  do {
    SLEEP(1000);
    if ( ClientTimeShift::GetStreamInfo())
    {
      if  ( m_stream_duration  > m_prebuffer )
      {
        break;
      }
    }
    Lease();
  } while (!m_complete && (timeout > time(nullptr)));

  if (m_complete || m_stream_duration == 0)
  {
    XBMC->Log(LOG_ERROR,"Could not buffer stream");
    StreamStop();
    return false;
  }

  if (Buffer::Open(inputUrl, 0 ) == false)
  {
    XBMC->Log(LOG_ERROR,"Could not open streaming file");
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
  Buffer::Close();
  m_isLeaseRunning = false;

  if (m_leaseThread.joinable())
    m_leaseThread.join();

  StreamStop();
  XBMC->Log(LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
  m_lastClose = time(nullptr);
}

void ClientTimeShift::StreamStop()
{
  std::string response;
  if (NextPVR::m_backEnd->DoRequest("/services/service?method=channel.stream.stop", response) != HTTP_OK)
  {
    XBMC->Log(LOG_ERROR, "%s:%d:", __FUNCTION__, __LINE__);
  }
}

int64_t ClientTimeShift::Seek(int64_t position, int whence)
{
  if (m_complete) return -1;
  if (m_active)
    Buffer::Close();
  ClientTimeShift::GetStreamInfo();

  if (m_stream_duration > g_timeShiftBufferSeconds)
  {
    int64_t startSlipBuffer = m_stream_length - (g_timeShiftBufferSeconds * m_stream_length/m_stream_duration);
    XBMC->Log(LOG_DEBUG, "%s:%d: %lld %lld %lld", __FUNCTION__, __LINE__, startSlipBuffer, position, m_stream_length.load());
    if (position < startSlipBuffer)
      position = startSlipBuffer;
  }

  XBMC->Log(LOG_DEBUG, "%s:%d: %lld %d %lld %d", __FUNCTION__, __LINE__, position, whence, m_stream_duration.load(), m_isPaused);
  if ( m_isPaused == true)
  {
    // skip while paused
    m_streamPosition = position;
  }
  else
  {
    std::string seekingInput = m_sourceURL + std::to_string(position ) + "-";
    if ( Buffer::Open(seekingInput.c_str(), 0) == false)
    {
      XBMC->Log(LOG_ERROR, "Could not open file on seek");
      return  -1;
    }
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
    XBMC->Log(LOG_ERROR, "NextPVR not updating completed rolling file");
    return ( m_stream_length != 0 );
  }
  if (NextPVR::m_backEnd->DoRequest("/services/service?method=channel.stream.info", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* filesNode = doc.FirstChildElement("map");
      if (filesNode != NULL)
      {
        stream_duration = strtoll(filesNode->FirstChildElement("stream_duration")->GetText(),nullptr,0);
        if (stream_duration != 0)
        {
          m_stream_length = strtoll(filesNode->FirstChildElement("stream_length")->GetText(),nullptr,0);
          m_stream_duration = stream_duration/1000;
          if (m_stream_duration > g_timeShiftBufferSeconds)
          {
              m_rollingStartSeconds = m_streamStart + m_stream_duration - g_timeShiftBufferSeconds;
          }
          XMLUtils::GetBoolean(filesNode,"complete",m_complete);
          if (m_complete == false)
          {
            if (m_nextRoll < time(nullptr))
            {
              m_nextRoll = time(nullptr) + g_timeShiftBufferSeconds/3 + g_ServerTimeOffset;
            }
          }
          else
          {
            XBMC->QueueNotification(QUEUE_INFO, "Tuner required.  Navigation disabled");
          }
        }
        XBMC->Log(LOG_DEBUG,"CT channel.stream.info %lld %lld %d %lld",m_stream_length.load(), stream_duration,m_complete, m_rollingStartSeconds.load());
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
