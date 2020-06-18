/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "pvrclient-nextpvr.h"

#include "BackendRequest.h"
#include "kodi/util/XMLUtils.h"
#include "kodi/General.h"
#include <kodi/Network.h>
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

cPVRClientNextPVR::cPVRClientNextPVR(const CNextPVRAddon& base, KODI_HANDLE instance, const std::string& kodiVersion) :
  kodi::addon::CInstancePVRClient(instance, kodiVersion), m_base(base)
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

  kodi::Log(ADDON_LOG_DEBUG, "->~cPVRClientNextPVR()");
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
        kodi::Log(ADDON_LOG_DEBUG, "session.initiate returns: sid=%s salt=%s", m_sid, salt.c_str());

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
            kodi::Log(ADDON_LOG_DEBUG, "session.login successful");
            status = ADDON_STATUS_OK;
          }
          else
          {
            g_pvrclient->ConnectionStateChange("Version failure", PVR_CONNECTION_STATE_VERSION_MISMATCH, kodi::GetLocalizedString(30050));
            m_bConnected = false;
            status = ADDON_STATUS_PERMANENT_FAILURE;
          }
        }
        else
        {
          kodi::Log(ADDON_LOG_DEBUG, "session.login failed");
          g_pvrclient->ConnectionStateChange("Access denied", PVR_CONNECTION_STATE_ACCESS_DENIED, kodi::GetLocalizedString(30052));
          m_bConnected = false;
          status = ADDON_STATUS_LOST_CONNECTION;
        }
      }
    }
  }
  else
  {
    g_pvrclient->ConnectionStateChange("Could not connect to server", PVR_CONNECTION_STATE_SERVER_UNREACHABLE, "");
    status = ADDON_STATUS_PERMANENT_FAILURE;
  }

  return status;
}

void cPVRClientNextPVR::Disconnect()
{
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

  const bool liveStreams = kodi::GetSettingBoolean("uselivestreams");
  if (liveStreams)
      m_channels.LoadLiveStreams();
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
            g_pvrclient->TriggerRecordingUpdate();
            g_pvrclient->TriggerTimerUpdate();
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
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  }
  return nullptr;
}

PVR_ERROR cPVRClientNextPVR::OnSystemSleep()
{
  m_lastRecordingUpdateTime = MAXINT64;
  Disconnect();
  g_pvrclient->ConnectionStateChange("sleeping", PVR_CONNECTION_STATE_DISCONNECTED, "");
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::OnSystemWake()
{
  g_pvrclient->ConnectionStateChange("waking", PVR_CONNECTION_STATE_CONNECTING, "");
  int count = 0;
  for (; count < 5; count++)
  {
    if (Connect())
    {
      g_pvrclient->ConnectionStateChange("connected", PVR_CONNECTION_STATE_CONNECTED, "");
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  kodi::Log(ADDON_LOG_INFO, "On NextPVR Wake %d %d", m_bConnected, count);
  return PVR_ERROR_NO_ERROR;
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
      kodi::network::WakeOnLan(m_settings.m_hostMACAddress);
      kodi::Log(ADDON_LOG_DEBUG, "WOL sent %d", count);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
}

/************************************************************/
/** General handling */

// Used among others for the server name string in the "Recordings" view
PVR_ERROR cPVRClientNextPVR::GetBackendName(std::string& name)
{
  if (!m_bConnected)
  {
    PVR_ERROR_SERVER_ERROR;
  }

  kodi::Log(ADDON_LOG_DEBUG, "->GetBackendName()");

  if (m_BackendName.length() == 0)
  {
    m_BackendName = "NextPVR (";
    m_BackendName += m_settings.m_hostname;
    m_BackendName += ")";
    name = m_BackendName;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetBackendVersion(std::string& version)
{
  if (!m_bConnected)
    return PVR_ERROR_SERVER_ERROR;

  kodi::Log(ADDON_LOG_DEBUG, "->GetBackendVersion()");
  version = std::to_string(m_settings.m_backendVersion);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetConnectionString(std::string& connection)
{
  connection = "Connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  total = 0;
  used = 0;

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

PVR_ERROR cPVRClientNextPVR::GetChannelStreamProperties(const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  bool liveStream = m_channels.IsChannelAPlugin(channel.GetUniqueId());
  if (liveStream)
  {
    properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, m_channels.m_liveStreams[channel.GetUniqueId()]);
    properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, "true");
    return PVR_ERROR_NO_ERROR;
  }
  else if (m_settings.m_liveStreamingMethod == Transcoded && !channel.GetIsRadio())
  {
    if (m_livePlayer != nullptr)
    {
      m_livePlayer->Close();
      m_nowPlaying = NotPlaying;
      m_livePlayer = nullptr;
    }
    const std::string line = StringUtils::Format("http://%s:%d/services/service?method=channel.transcode.m3u8&sid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, m_sid);
    m_livePlayer = m_timeshiftBuffer;
    m_livePlayer->Channel(channel.GetUniqueId());
    if (m_livePlayer->Open(line))
    {
      m_nowPlaying = Transcoding;
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "Transcoding Error");
      return PVR_ERROR_FAILED;
    }
    properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, line);
    properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, "true");
    properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/x-mpegURL");
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_NOT_IMPLEMENTED;
}

/************************************************************/
/** Live stream handling */
bool cPVRClientNextPVR::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  std::string line;
  if (channel.GetIsRadio() == false)
  {
    m_nowPlaying = TV;
  }
  else
  {
    m_nowPlaying = Radio;
  }
  if (m_channels.m_liveStreams.count(channel.GetUniqueId()) != 0)
  {
    line = m_channels.m_liveStreams[channel.GetUniqueId()];
    m_livePlayer = m_realTimeBuffer;
    return m_livePlayer->Open(line, ADDON_READ_CACHED);
  }
  else if (channel.GetIsRadio() == false && m_supportsLiveTimeshift && m_settings.m_liveStreamingMethod == Timeshift)
  {
    line = StringUtils::Format("GET /live?channeloid=%d&mode=liveshift&client=XBMC-%s HTTP/1.0\r\n", channel.GetUniqueId(), m_sid);
    m_livePlayer = m_timeshiftBuffer;
  }
  else if (m_settings.m_liveStreamingMethod == RollingFile)
  {
    line = StringUtils::Format("http://%s:%d/live?channeloid=%d&client=XBMC-%s&epgmode=true", m_settings.m_hostname.c_str(), m_settings.m_port, channel.GetUniqueId(), m_sid);
    m_livePlayer = m_timeshiftBuffer;
  }
  else if (m_settings.m_liveStreamingMethod == ClientTimeshift)
  {
    line = StringUtils::Format("http://%s:%d/live?channeloid=%d&client=%s&sid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, channel.GetUniqueId(), m_sid, m_sid);
    m_livePlayer = m_timeshiftBuffer;
    m_livePlayer->Channel(channel.GetUniqueId());
  }
  else
  {
    line = StringUtils::Format("http://%s:%d/live?channeloid=%d&client=XBMC-%s", m_settings.m_hostname.c_str(), m_settings.m_port, channel.GetUniqueId(), m_sid);
    m_livePlayer = m_realTimeBuffer;
  }
  kodi::Log(ADDON_LOG_INFO, "Calling Open(%s) on tsb!", line.c_str());
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
  kodi::Log(ADDON_LOG_DEBUG, "CloseLiveStream");
  if (m_livePlayer != nullptr)
  {
    m_livePlayer->Close();
    m_livePlayer = nullptr;
  }
  m_nowPlaying = NotPlaying;
}


int64_t cPVRClientNextPVR::SeekLiveStream(int64_t iPosition, int iWhence)
{
  int64_t retVal;
  kodi::Log(ADDON_LOG_DEBUG, "calling seek(%lli %d)", iPosition, iWhence);
  retVal = m_livePlayer->Seek(iPosition, iWhence);
  kodi::Log(ADDON_LOG_DEBUG, "returned from seek()");
  return retVal;
}


int64_t cPVRClientNextPVR::LengthLiveStream(void)
{
  kodi::Log(ADDON_LOG_DEBUG, "seek length(%lli)", m_livePlayer->Length());
  return m_livePlayer->Length();
}

PVR_ERROR cPVRClientNextPVR::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
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


bool cPVRClientNextPVR::OpenRecordedStream(const kodi::addon::PVRRecording& recording)
{
  kodi::addon::PVRRecording copyRecording = recording;
  m_nowPlaying = Recording;
  copyRecording.SetDirectory(m_recordings.m_hostFilenames[recording.GetRecordingId()]);
  const std::string line = StringUtils::Format("http://%s:%d/live?recording=%s&client=XBMC-%s", m_settings.m_hostname.c_str(), m_settings.m_port, recording.GetRecordingId().c_str(), m_sid);
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

int64_t cPVRClientNextPVR::SeekRecordedStream(int64_t iPosition, int iWhence)
{
  return m_recordingBuffer->Seek(iPosition, iWhence);
}

int64_t cPVRClientNextPVR::LengthRecordedStream(void)
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

PVR_ERROR cPVRClientNextPVR::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  PVR_ERROR rez;
  if (m_nowPlaying == Recording)
    rez = m_recordingBuffer->GetStreamTimes(stimes);
  else
    rez = m_livePlayer->GetStreamTimes(stimes);
  return rez;
}

PVR_ERROR cPVRClientNextPVR::GetStreamReadChunkSize(int& chunksize)
{
  PVR_ERROR rez = PVR_ERROR_NO_ERROR;
  if (m_nowPlaying == Recording)
    chunksize = m_settings.m_chunkRecording * 1024;
  else if (m_nowPlaying == Radio)
    chunksize = 4096;
  else
    rez = m_livePlayer->GetStreamReadChunkSize(chunksize);
  return rez;
}

/*
PVR_ERROR cPVRClientNextPVR::GetBackendName(std::string& name)
{
  name = m_settings.m_hostname;
  return PVR_ERROR_NO_ERROR;
}
*/

PVR_ERROR cPVRClientNextPVR::CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRChannel& item)
{
    return m_menuhook.CallChannelMenuHook(menuhook, item);
}

PVR_ERROR cPVRClientNextPVR::CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRRecording& item)
{
    return m_menuhook.CallRecordingsMenuHook(menuhook, item);
}

PVR_ERROR cPVRClientNextPVR::CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
    return m_menuhook.CallSettingsMenuHook(menuhook);
}

/*******************************************/
/** PVR EPG Functions                     **/

PVR_ERROR cPVRClientNextPVR::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  return m_epg.GetEPGForChannel(channelUid, start, end, results);
}


/*******************************************/
/** PVR Channel Functions                 **/
PVR_ERROR cPVRClientNextPVR::GetChannelsAmount(int& amount)
{
  amount = m_channels.GetNumChannels();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  return m_channels.GetChannels(radio, results);
}


/*******************************************/
/** PVR Channel group Functions           **/

PVR_ERROR cPVRClientNextPVR::GetChannelGroupsAmount(int& amount)
{
  m_channels.GetChannelGroupsAmount(amount);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  return m_channels.GetChannelGroups(radio, results);
}

PVR_ERROR cPVRClientNextPVR::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  return m_channels.GetChannelGroupMembers(group, results);
}

/*******************************************/
/** PVR Recording Functions               **/

PVR_ERROR cPVRClientNextPVR::GetRecordingsAmount(bool deleted, int& amount)
{
  return m_recordings.GetRecordingsAmount(deleted, amount);
}

PVR_ERROR cPVRClientNextPVR::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  return m_recordings.GetRecordings(deleted, results);
}

PVR_ERROR cPVRClientNextPVR::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  return m_recordings.DeleteRecording(recording);
}

PVR_ERROR cPVRClientNextPVR::GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl)
{
  return m_recordings.GetRecordingEdl(recording, edl);
}

PVR_ERROR cPVRClientNextPVR::GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position)
{
  return m_recordings.GetRecordingLastPlayedPosition(recording, position);
}

PVR_ERROR cPVRClientNextPVR::SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int lastplayedposition)
{
  return m_recordings.SetRecordingLastPlayedPosition(recording, lastplayedposition);
}

PVR_ERROR cPVRClientNextPVR::SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count)
{
  kodi::Log(ADDON_LOG_DEBUG, "Play count %s %d", recording.GetTitle().c_str(), count);
  return PVR_ERROR_NO_ERROR;
}

/*******************************************/
/** PVR Timer Functions                   **/
PVR_ERROR cPVRClientNextPVR::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  return m_timers.GetTimerTypes(types);
}

PVR_ERROR cPVRClientNextPVR::GetTimersAmount(int& amount)
{
  return m_timers.GetTimersAmount(amount);
}

PVR_ERROR cPVRClientNextPVR::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  return m_timers.GetTimers(results);
}

PVR_ERROR cPVRClientNextPVR::AddTimer(const kodi::addon::PVRTimer& timer)
{
  return m_timers.AddTimer(timer);
}

PVR_ERROR cPVRClientNextPVR::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  return m_timers.DeleteTimer(timer, forceDelete);
}

PVR_ERROR cPVRClientNextPVR::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  return m_timers.UpdateTimer(timer);
}

//-- GetCapabilities -----------------------------------------------------
// Tell XBMC our requirements
//-----------------------------------------------------------------------------

PVR_ERROR cPVRClientNextPVR::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  kodi::Log(ADDON_LOG_DEBUG, "->GetCapabilities()");

  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsRecordingSize(m_settings.m_showRecordingSize);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(m_settings.m_showRadio);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetHandlesInputStream(true);
  capabilities.SetHandlesDemuxing(false);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetSupportsLastPlayedPosition(true);
  capabilities.SetSupportsRecordingEdl(true);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsRecordingPlayCount(true);
  return PVR_ERROR_NO_ERROR;
}
