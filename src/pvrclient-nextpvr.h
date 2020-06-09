/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <vector>

/* Master defines for client control */
#include <kodi/AddonBase.h>
#include <kodi/addon-instance/PVR.h>

/* Local includes */

#include "Channels.h"
#include "EPG.h"
#include "MenuHook.h"
#include "Recordings.h"
#include "Settings.h"
#include "Timers.h"
#include "buffers/ClientTimeshift.h"
#include "buffers/DummyBuffer.h"
#include "buffers/RecordingBuffer.h"
#include "buffers/RollingFile.h"
#include "buffers/TimeshiftBuffer.h"
#include "buffers/TranscodedBuffer.h"
#include "p8-platform/threads/mutex.h"
#include "p8-platform/threads/threads.h"
#include "tinyxml.h"
#include <map>

#define SAFE_DELETE(p) \
  do \
  { \
    delete (p); \
    (p) = nullptr; \
  } while (0)

enum eNowPlaying
{
  NotPlaying = 0,
  TV = 1,
  Radio = 2,
  Recording = 3,
  Transcoding
};

class ATTRIBUTE_HIDDEN cPVRClientNextPVR : public kodi::addon::CInstancePVRClient, P8PLATFORM::CThread
{
public:
  /* Class interface */
   cPVRClientNextPVR(const CNextPVRAddon& base, KODI_HANDLE instance, const std::string& kodiVersion);

  ~cPVRClientNextPVR();


  /* Server handling */
  ADDON_STATUS Connect();
  void Disconnect();
  bool IsUp();
  PVR_ERROR OnSystemSleep() override;
  PVR_ERROR OnSystemWake() override;

  /* General handling */
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;

  //PVR_ERROR GetBackendTime(time_t* localTime, int* gmtOffset);
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;

  int XmlGetInt(TiXmlElement* node, const char* name, const int setDefault = 0);
  unsigned int XmlGetUInt(TiXmlElement* node, const char* name, const unsigned setDefault = 0);

  bool IsChannelAPlugin(int uid);

  /* Live stream handling */
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override;


  int ReadLiveStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekLiveStream(int64_t position, int whence) override;
  int64_t LengthLiveStream() override;
  bool CanPauseStream() override;
  void PauseStream(bool paused) override;
  bool CanSeekStream() override;
  bool IsTimeshifting();
  bool IsRealTimeStream() override;
  PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& times) override;
  PVR_ERROR GetStreamReadChunkSize(int& chunksize) override;
  bool IsRadio() { return m_nowPlaying == Radio; };

  /* Record stream handling */
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recinfo) override;
  void CloseRecordedStream() override;
  int ReadRecordedStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekRecordedStream(int64_t position, int whence) override;
  int64_t LengthRecordedStream() override;

  void ForceRecordingUpdate() { m_lastRecordingUpdateTime = 0; }

  /* background connection monitoring */
  void* Process(void);

  Channels& m_channels = Channels::GetInstance();
  EPG& m_epg = EPG::GetInstance();
  MenuHook& m_menuhook = MenuHook::GetInstance();
  Recordings& m_recordings = Recordings::GetInstance();
  Timers& m_timers = Timers::GetInstance();
  int64_t m_lastRecordingUpdateTime;
  char m_sid[64];

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;

  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetChannelGroupsAmount(int& amount);
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results) override;
  PVR_ERROR GetChannelStreamProperties(const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results) override;
  //PVR_ERROR SetEPGTimeFrame(int epgMaxDays) override;

  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl) override;
  PVR_ERROR SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count) override;
  PVR_ERROR GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position) override;
  PVR_ERROR SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int lastplayedposition) override;

  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;

  PVR_ERROR CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRChannel& item) override;
  PVR_ERROR CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRRecording& item) override;
  PVR_ERROR CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook) override;

protected:

private:
  void ConfigurePostConnectionOptions();
  const CNextPVRAddon& m_base;
  void Close();

  bool m_bConnected;
  std::string m_BackendName;
  P8PLATFORM::CMutex m_mutex;

  bool m_supportsLiveTimeshift;

  time_t m_tsbStartTime;
  int m_timeShiftBufferSeconds;
  timeshift::Buffer* m_timeshiftBuffer;
  timeshift::Buffer* m_livePlayer;
  timeshift::Buffer* m_realTimeBuffer;
  timeshift::RecordingBuffer* m_recordingBuffer;

  //Matrix changes
  NextPVR::Settings& m_settings = NextPVR::Settings::GetInstance();
  NextPVR::Request& m_request = NextPVR::Request::GetInstance();

  eNowPlaying m_nowPlaying = NotPlaying;

  void SendWakeOnLan();

};
