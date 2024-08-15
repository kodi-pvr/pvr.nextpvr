/*
 *  Copyright (C) 2005-2023 Team Kodi (https://kodi.tv)
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
#include "InstanceSettings.h"
#include "Timers.h"
#include "buffers/ClientTimeshift.h"
#include "buffers/DummyBuffer.h"
#include "buffers/RecordingBuffer.h"
#include "buffers/TranscodedBuffer.h"
#include <map>

enum eNowPlaying
{
  NotPlaying = 0,
  TV = 1,
  Radio = 2,
  Recording = 3,
  Transcoding
};

class ATTR_DLL_LOCAL cPVRClientNextPVR : public kodi::addon::CInstancePVRClient
{
public:
  /* Class interface */
  cPVRClientNextPVR(const CNextPVRAddon& base, const kodi::addon::IInstanceInfo& instance, bool first);

  ~cPVRClientNextPVR();

  // kodi::addon::CInstancePVRClient -> kodi::addon::IAddonInstance overrides
  ADDON_STATUS SetInstanceSetting(const std::string& settingName,
    const kodi::addon::CSettingValue& settingValue) override;

  /* Server handling */
  ADDON_STATUS Connect(bool sendWOL = true);
  void Disconnect();
  void ResetConnection();
  bool IsUp();
  PVR_ERROR OnSystemSleep() override;
  PVR_ERROR OnSystemWake() override;
  void SendWakeOnLan();

  /* General handling */
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetBackendHostname(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;

  //PVR_ERROR GetBackendTime(time_t* localTime, int* gmtOffset);
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;

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
  bool IsServerStreaming();
  bool IsServerStreamingLive(bool log = true);
  bool IsServerStreamingRecording(bool log = true);

  /* Record stream handling */
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recinfo) override;
  void CloseRecordedStream() override;
  int ReadRecordedStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekRecordedStream(int64_t position, int whence) override;
  int64_t LengthRecordedStream() override;

  void ForceRecordingUpdate() { m_lastRecordingUpdateTime = 0; }

  /* background connection monitoring */
  void Process();

  time_t m_lastRecordingUpdateTime;
  time_t m_lastEPGUpdateTime = 0;
  eNowPlaying m_nowPlaying = NotPlaying;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;

  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results) override;
  PVR_ERROR GetChannelStreamProperties(const kodi::addon::PVRChannel& channel, PVR_SOURCE source, std::vector<kodi::addon::PVRStreamProperty>& properties) override;
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

  bool m_bConnected;
  std::atomic<bool> m_running = { false };
  std::thread m_thread;
  bool m_supportsLiveTimeshift;

  int m_timeShiftBufferSeconds;
  timeshift::Buffer* m_timeshiftBuffer;
  timeshift::Buffer* m_livePlayer;
  timeshift::Buffer* m_realTimeBuffer;
  timeshift::RecordingBuffer* m_recordingBuffer;

  //Matrix changes
  std::shared_ptr<InstanceSettings> m_settings;
  Request m_request;
  Channels m_channels;
  EPG m_epg;
  MenuHook m_menuhook;
  Recordings m_recordings;
  Timers m_timers;

  void SetConnectionState(PVR_CONNECTION_STATE state, std::string displayMessage = "");
  void UpdateServerCheck();
  PVR_CONNECTION_STATE m_connectionState = PVR_CONNECTION_STATE_UNKNOWN;
  PVR_CONNECTION_STATE m_coreState = PVR_CONNECTION_STATE_UNKNOWN;
  time_t m_firstSessionInitiate = 0;
  time_t m_nextServerCheck = 0;
};
