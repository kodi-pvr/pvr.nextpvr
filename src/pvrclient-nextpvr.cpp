/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "pvrclient-nextpvr.h"

#include "BackendRequest.h"
#include "client.h"
#include "kodi/util/XMLUtils.h"
#include "md5.h"

#include <ctime>
#include <memory>
#include <stdio.h>
#include <stdlib.h>

#include <p8-platform/util/StringUtils.h>

#if defined(TARGET_WINDOWS)
#define atoll(S) _atoi64(S)
#else
#define MAXINT64 ULONG_MAX
#endif

#include <algorithm>

using namespace std;
using namespace ADDON;
using namespace NextPVR;

extern "C"
{
  ADDON_STATUS ADDON_SetSetting(const char* settingName, const void* settingValue);
}

const char SAFE[256] = {
    /*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
    /* 0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 1 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 2 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,

    /* 4 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    /* 6 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,

    /* 8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 9 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* A */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* B */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /* C */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* D */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* E */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* F */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

std::string UriEncode(const std::string sSrc)
{
  const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
  const unsigned char* pSrc = (const unsigned char*)sSrc.c_str();
  const int SRC_LEN = sSrc.length();
  unsigned char* const pStart = new unsigned char[SRC_LEN * 3];
  unsigned char* pEnd = pStart;
  const unsigned char* const SRC_END = pSrc + SRC_LEN;

  for (; pSrc < SRC_END; ++pSrc)
  {
    if (SAFE[*pSrc])
    {
      *pEnd++ = *pSrc;
    }
    else
    {
      // escape this char
      *pEnd++ = '%';
      *pEnd++ = DEC2HEX[*pSrc >> 4];
      *pEnd++ = DEC2HEX[*pSrc & 0x0F];
    }
  }

  std::string sResult((char*)pStart, (char*)pEnd);
  delete[] pStart;
  return sResult;
}

/************************************************************/
/** Class interface */

cPVRClientNextPVR::cPVRClientNextPVR()
{
  m_bConnected = false;

  m_supportsLiveTimeshift = false;

  m_lastRecordingUpdateTime = MAXINT64; // time of last recording check - force forever
  m_timeshiftBuffer = new timeshift::DummyBuffer();
  m_recordingBuffer = new timeshift::RecordingBuffer();
  m_realTimeBuffer = new timeshift::DummyBuffer();
  m_livePlayer = nullptr;
  m_nowPlaying = NotPlaying;
  CreateThread();
}

cPVRClientNextPVR::~cPVRClientNextPVR()
{
  StopThread();

  XBMC->Log(LOG_DEBUG, "->~cPVRClientNextPVR()");
  if (m_bConnected)
    Disconnect();
  delete m_timeshiftBuffer;
  delete m_recordingBuffer;
  delete m_realTimeBuffer;
  m_timers.m_epgOidLookup.clear();
  m_recordings.m_hostFilenames.clear();
  m_channels.m_channelDetails.clear();
  m_channels.m_liveStreams.clear();
}

ADDON_STATUS cPVRClientNextPVR::Connect()
{
  string result;
  m_bConnected = false;
  ADDON_STATUS status = ADDON_STATUS_UNKNOWN;
  // initiate session
  std::string response;

  SendWakeOnLan();
  if (m_request.DoRequest("/service?method=session.initiate&ver=1.0&device=xbmc", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      std::string salt;
      std::string sid;
      if (XMLUtils::GetString(doc.RootElement(), "salt", salt) && XMLUtils::GetString(doc.RootElement(), "sid", sid))
      {
        // extract and store sid
        strcpy(m_sid, sid.c_str());
        m_request.setSID(m_sid);

        // a bit of debug
        XBMC->Log(LOG_DEBUG, "session.initiate returns: sid=%s salt=%s", m_sid, salt.c_str());

        std::string pinMD5 = PVRXBMC::XBMC_MD5::GetMD5(m_settings.m_PIN);
        StringUtils::ToLower(pinMD5);

        // calculate combined MD5
        std::string combinedMD5;
        combinedMD5.append(":");
        combinedMD5.append(pinMD5);
        combinedMD5.append(":");
        combinedMD5.append(salt);

        // get digest
        std::string md5 = PVRXBMC::XBMC_MD5::GetMD5(combinedMD5);

        // login session
        std::string loginResponse;
        std::string request = StringUtils::Format("/service?method=session.login&sid=%s&md5=%s", m_sid, md5.c_str());
        if (m_request.DoRequest(request.c_str(), loginResponse) == HTTP_OK)
        {
          if (m_settings.ReadBackendSettings() == ADDON_STATUS_OK)
          {
            // set additional options based on the backend
            ConfigurePostConnectionOptions();
            m_bConnected = true;
            XBMC->Log(LOG_DEBUG, "session.login successful");
            status = ADDON_STATUS_OK;
          }
          else
          {
            PVR->ConnectionStateChange("Version failure", PVR_CONNECTION_STATE_VERSION_MISMATCH, XBMC->GetLocalizedString(30050));
            m_bConnected = false;
            status = ADDON_STATUS_PERMANENT_FAILURE;
          }
        }
        else
        {
          XBMC->Log(LOG_DEBUG, "session.login failed");
          PVR->ConnectionStateChange("Access denied", PVR_CONNECTION_STATE_ACCESS_DENIED, XBMC->GetLocalizedString(30052));
          m_bConnected = false;
          status = ADDON_STATUS_LOST_CONNECTION;
        }
      }
    }
  }
  else
  {
    PVR->ConnectionStateChange("Could not connect to server", PVR_CONNECTION_STATE_SERVER_UNREACHABLE, nullptr);
    status = ADDON_STATUS_PERMANENT_FAILURE;
  }

  return status;
}

void cPVRClientNextPVR::Disconnect()
{
  string result;

  m_bConnected = false;
}

void cPVRClientNextPVR::ConfigurePostConnectionOptions()
{
  m_settings.SetVersionSpecificSettings();
  if (m_settings.m_liveStreamingMethod != eStreamingMethod::RealTime)
  {
    delete m_timeshiftBuffer;
    m_supportsLiveTimeshift = true;
    if (m_settings.m_liveStreamingMethod == eStreamingMethod::Transcoded)
    {
      m_supportsLiveTimeshift = false;
      m_timeshiftBuffer = new timeshift::TranscodedBuffer();
    }
    else if (m_settings.m_liveStreamingMethod == eStreamingMethod::ClientTimeshift)
    {
      m_timeshiftBuffer = new timeshift::ClientTimeShift();
    }
    else if (m_settings.m_liveStreamingMethod != eStreamingMethod::Timeshift)
    {
      m_timeshiftBuffer = new timeshift::RollingFile();
    }
    else
    {
      m_timeshiftBuffer = new timeshift::TimeshiftBuffer();
    }
  }

  bool liveStreams;
  if (XBMC->GetSetting("uselivestreams", &liveStreams))
  {
    if (liveStreams)
      m_channels.LoadLiveStreams();
  }

}

/* IsUp()
 * \brief   Check if we have a valid session to nextpvr
 * \return  True when a session is active
 */
bool cPVRClientNextPVR::IsUp()
{
  // check time since last time Recordings were updated, update if it has been awhile
  if (m_bConnected == true && m_nowPlaying == NotPlaying && m_lastRecordingUpdateTime != MAXINT64 && time(0) > (m_lastRecordingUpdateTime + 60))
  {
    TiXmlDocument doc;
    const std::string request = "/service?method=recording.lastupdated";
    std::string response;
    if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
    {
      if (doc.Parse(response.c_str()) != nullptr)
      {
        TiXmlElement* last_update = doc.RootElement()->FirstChildElement("last_update");
        if (last_update != nullptr)
        {
          int64_t update_time = atoll(last_update->GetText());
          if (update_time > m_lastRecordingUpdateTime)
          {
            m_lastRecordingUpdateTime = MAXINT64;
            PVR->TriggerRecordingUpdate();
            PVR->TriggerTimerUpdate();
          }
          else
          {
            m_lastRecordingUpdateTime = time(0);
          }
        }
        else
        {
          m_lastRecordingUpdateTime = MAXINT64;
        }
      }
    }
    else
    {
      m_lastRecordingUpdateTime = time(0);
    }
  }
  else if (m_nowPlaying == Transcoding)
  {
    if (m_livePlayer->IsRealTimeStream() == false)
    {
      //m_livePlayer->Close();
      m_nowPlaying = NotPlaying;
      m_livePlayer = nullptr;
    }
  }
  return m_bConnected;
}

void* cPVRClientNextPVR::Process(void)
{
  while (!IsStopped())
  {
    IsUp();
    Sleep(2500);
  }
  return nullptr;
}

void cPVRClientNextPVR::OnSystemSleep()
{
  m_lastRecordingUpdateTime = MAXINT64;
  Disconnect();
  PVR->ConnectionStateChange("sleeping", PVR_CONNECTION_STATE_DISCONNECTED, nullptr);
  Sleep(1000);
}

void cPVRClientNextPVR::OnSystemWake()
{
  PVR->ConnectionStateChange("waking", PVR_CONNECTION_STATE_CONNECTING, nullptr);
  int count = 0;
  for (; count < 5; count++)
  {
    if (Connect())
    {
      PVR->ConnectionStateChange("connected", PVR_CONNECTION_STATE_CONNECTED, nullptr);
      break;
    }
    Sleep(500);
  }

  XBMC->Log(LOG_INFO, "On NextPVR Wake %d %d", m_bConnected, count);
}

void cPVRClientNextPVR::SendWakeOnLan()
{
  if (m_settings.m_enableWOL == true)
  {
    if (m_settings.m_hostname == "127.0.0.1" || m_settings.m_hostname == "localhost" || m_settings.m_hostname == "::1")
    {
      return;
    }
    int count = 0;
    for (; count < m_settings.m_timeoutWOL; count++)
    {
      if (m_request.PingBackend())
      {
        return;
      }
      XBMC->WakeOnLan(m_settings.m_hostMACAddress.c_str());
      XBMC->Log(LOG_DEBUG, "WOL sent %d", count);
      Sleep(1000);
    }
  }
}

/************************************************************/
/** General handling */

// Used among others for the server name string in the "Recordings" view
const char* cPVRClientNextPVR::GetBackendName(void)
{
  if (!m_bConnected)
  {
    return m_settings.m_hostname.c_str();
  }

  XBMC->Log(LOG_DEBUG, "->GetBackendName()");

  if (m_BackendName.length() == 0)
  {
    m_BackendName = "NextPVR (";
    m_BackendName += m_settings.m_hostname.c_str();
    m_BackendName += ")";
  }

  return m_BackendName.c_str();
}

const char* cPVRClientNextPVR::GetBackendVersion()
{
  if (!m_bConnected)
    return "Unknown";

  XBMC->Log(LOG_DEBUG, "->GetBackendVersion()");
  std::string version = to_string(m_settings.m_backendVersion);
  return version.c_str();
}

const char* cPVRClientNextPVR::GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

PVR_ERROR cPVRClientNextPVR::GetDriveSpace(long long* iTotal, long long* iUsed)
{
  string result;
  vector<string> fields;

  *iTotal = 0;
  *iUsed = 0;

  if (!m_bConnected)
    return PVR_ERROR_SERVER_ERROR;

  return PVR_ERROR_NO_ERROR;
}


int cPVRClientNextPVR::XmlGetInt(TiXmlElement* node, const char* name, const int setDefault)
{
  int retval = setDefault;
  XMLUtils::GetInt(node, name, retval);
  return retval;
}

unsigned int cPVRClientNextPVR::XmlGetUInt(TiXmlElement* node, const char* name, const unsigned int setDefault)
{
  unsigned int retval = setDefault;
  XMLUtils::GetUInt(node, name, retval);
  return retval;
}

PVR_ERROR cPVRClientNextPVR::GetChannelStreamProperties(const PVR_CHANNEL& channel, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  bool liveStream = m_channels.IsChannelAPlugin(channel.iUniqueId);
  if (liveStream || (m_settings.m_liveStreamingMethod == Transcoded && !channel.bIsRadio))
  {
    strncpy(properties[0].strName, PVR_STREAM_PROPERTY_STREAMURL, sizeof(properties[0].strName) - 1);
    if (liveStream)
    {
      strncpy(properties[0].strValue, m_channels.m_liveStreams[channel.iUniqueId].c_str(), sizeof(properties[0].strValue) - 1);
      strcpy(properties[1].strName, PVR_STREAM_PROPERTY_ISREALTIMESTREAM);
      strcpy(properties[1].strValue, "true");
      *iPropertiesCount = 2;
    }
    else
    {
      if (m_livePlayer != nullptr)
      {
        m_livePlayer->Close();
        m_nowPlaying = NotPlaying;
        m_livePlayer = nullptr;
      }
      const std::string line = StringUtils::Format("http://%s:%d/services/service?method=channel.transcode.m3u8&sid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, m_sid);
      m_livePlayer = m_timeshiftBuffer;
      m_livePlayer->Channel(channel.iUniqueId);
      if (m_livePlayer->Open(line))
      {
        m_nowPlaying = Transcoding;
      }
      else
      {
        XBMC->Log(LOG_ERROR, "Transcoding Error");
        return PVR_ERROR_FAILED;
      }
      strncpy(properties[0].strValue, line.c_str(), sizeof(properties[0].strValue) - 1);
      strcpy(properties[1].strName, PVR_STREAM_PROPERTY_ISREALTIMESTREAM);
      strcpy(properties[1].strValue, "true");
      strcpy(properties[2].strName, PVR_STREAM_PROPERTY_MIMETYPE);
      strcpy(properties[2].strValue, "application/x-mpegURL");
      *iPropertiesCount = 3;
    }
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_NOT_IMPLEMENTED;
}

/************************************************************/
/** Live stream handling */
bool cPVRClientNextPVR::OpenLiveStream(const PVR_CHANNEL& channelinfo)
{
  std::string line;
  if (channelinfo.bIsRadio == false)
  {
    m_nowPlaying = TV;
  }
  else
  {
    m_nowPlaying = Radio;
  }
  if (m_channels.m_liveStreams.count(channelinfo.iUniqueId) != 0)
  {
    line = m_channels.m_liveStreams[channelinfo.iUniqueId];
    m_livePlayer = m_realTimeBuffer;
    return m_livePlayer->Open(line, XFILE::READ_CACHED);
  }
  else if (channelinfo.bIsRadio == false && m_supportsLiveTimeshift && m_settings.m_liveStreamingMethod == Timeshift)
  {
    line = StringUtils::Format("GET /live?channeloid=%d&mode=liveshift&client=XBMC-%s HTTP/1.0\r\n", channelinfo.iUniqueId, m_sid);
    m_livePlayer = m_timeshiftBuffer;
  }
  else if (m_settings.m_liveStreamingMethod == RollingFile)
  {
    line = StringUtils::Format("http://%s:%d/live?channeloid=%d&client=XBMC-%s&epgmode=true", m_settings.m_hostname.c_str(), m_settings.m_port, channelinfo.iUniqueId, m_sid);
    m_livePlayer = m_timeshiftBuffer;
  }
  else if (m_settings.m_liveStreamingMethod == ClientTimeshift)
  {
    line = StringUtils::Format("http://%s:%d/live?channeloid=%d&client=%s&sid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, channelinfo.iUniqueId, m_sid, m_sid);
    m_livePlayer = m_timeshiftBuffer;
    m_livePlayer->Channel(channelinfo.iUniqueId);
  }
  else
  {
    line = StringUtils::Format("http://%s:%d/live?channeloid=%d&client=XBMC-%s", m_settings.m_hostname.c_str(), m_settings.m_port, channelinfo.iUniqueId, m_sid);
    m_livePlayer = m_realTimeBuffer;
  }
  XBMC->Log(LOG_INFO, "Calling Open(%s) on tsb!", line.c_str());
  if (m_livePlayer->Open(line))
  {
    return true;
  }
  return false;
}

int cPVRClientNextPVR::ReadLiveStream(unsigned char* pBuffer, unsigned int iBufferSize)
{
  return m_livePlayer->Read(pBuffer, iBufferSize);
}

void cPVRClientNextPVR::CloseLiveStream(void)
{
  XBMC->Log(LOG_DEBUG, "CloseLiveStream");
  if (m_livePlayer != nullptr)
  {
    m_livePlayer->Close();
    m_livePlayer = nullptr;
  }
  m_nowPlaying = NotPlaying;
}


long long cPVRClientNextPVR::SeekLiveStream(long long iPosition, int iWhence)
{
  long long retVal;
  XBMC->Log(LOG_DEBUG, "calling seek(%lli %d)", iPosition, iWhence);
  retVal = m_livePlayer->Seek(iPosition, iWhence);
  XBMC->Log(LOG_DEBUG, "returned from seek()");
  return retVal;
}


long long cPVRClientNextPVR::LengthLiveStream(void)
{
  XBMC->Log(LOG_DEBUG, "seek length(%lli)", m_livePlayer->Length());
  return m_livePlayer->Length();
}

PVR_ERROR cPVRClientNextPVR::GetSignalStatus(PVR_SIGNAL_STATUS* signalStatus)
{
  // Not supported yet
  if (m_nowPlaying == Transcoding)
  {
    m_livePlayer->Lease();
  }
  return PVR_ERROR_NO_ERROR;
}


bool cPVRClientNextPVR::CanPauseStream(void)
{
  if (m_nowPlaying == Recording)
  {
    return true;
  }
  else
  {
    bool retval = m_livePlayer->CanPauseStream();
    return retval;
  }
}

void cPVRClientNextPVR::PauseStream(bool bPaused)
{
  if (m_nowPlaying == Recording)
    m_recordingBuffer->PauseStream(bPaused);
  else
    m_livePlayer->PauseStream(bPaused);
}

bool cPVRClientNextPVR::CanSeekStream(void)
{
  if (m_nowPlaying == Recording)
    return true;
  else
    return m_livePlayer->CanSeekStream();
}

/************************************************************/
/** Record stream handling */


bool cPVRClientNextPVR::OpenRecordedStream(const PVR_RECORDING& recording)
{
  PVR_RECORDING copyRecording = recording;
  m_nowPlaying = Recording;
  strcpy(copyRecording.strDirectory, m_recordings.m_hostFilenames[recording.strRecordingId].c_str());
  const std::string line = StringUtils::Format("http://%s:%d/live?recording=%s&client=XBMC-%s", m_settings.m_hostname.c_str(), m_settings.m_port, recording.strRecordingId, m_sid);
  return m_recordingBuffer->Open(line, copyRecording);
}

void cPVRClientNextPVR::CloseRecordedStream(void)
{
  m_recordingBuffer->Close();
  m_recordingBuffer->SetDuration(0);
  m_nowPlaying = NotPlaying;
}

int cPVRClientNextPVR::ReadRecordedStream(unsigned char* pBuffer, unsigned int iBufferSize)
{
  iBufferSize = m_recordingBuffer->Read(pBuffer, iBufferSize);
  return iBufferSize;
}

long long cPVRClientNextPVR::SeekRecordedStream(long long iPosition, int iWhence)
{
  return m_recordingBuffer->Seek(iPosition, iWhence);
}

long long cPVRClientNextPVR::LengthRecordedStream(void)
{
  return m_recordingBuffer->Length();
}

bool cPVRClientNextPVR::IsTimeshifting()
{
  if (m_nowPlaying == Recording)
    return false;
  else
    return m_livePlayer->IsTimeshifting();
}

bool cPVRClientNextPVR::IsRealTimeStream()
{
  bool retval;
  if (m_nowPlaying == Recording)
  {
    retval = m_recordingBuffer->IsRealTimeStream();
  }
  else
  {
    retval = m_livePlayer->IsRealTimeStream();
  }
  return retval;
}

PVR_ERROR cPVRClientNextPVR::GetStreamTimes(PVR_STREAM_TIMES* stimes)
{
  PVR_ERROR rez;
  if (m_nowPlaying == Recording)
    rez = m_recordingBuffer->GetStreamTimes(stimes);
  else
    rez = m_livePlayer->GetStreamTimes(stimes);
  return rez;
}

PVR_ERROR cPVRClientNextPVR::GetStreamReadChunkSize(int* chunksize)
{
  PVR_ERROR rez = PVR_ERROR_NO_ERROR;
  if (m_nowPlaying == Recording)
    *chunksize = m_settings.m_chunkRecording * 1024;
  else if (m_nowPlaying == Radio)
    *chunksize = 4096;
  else
    rez = m_livePlayer->GetStreamReadChunkSize(chunksize);
  return rez;
}
