/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Channels.h"
#include "utilities/XMLUtils.h"
#include "pvrclient-nextpvr.h"

#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>
#include "zlib.h"

using namespace NextPVR;
using namespace NextPVR::utilities;

/** Channel handling */

Channels::Channels(const std::shared_ptr<InstanceSettings>& settings, Request& request) :
  m_settings(settings),
  m_request(request),
  m_channelCacheFile(kodi::vfs::TranslateSpecialProtocol(kodi::tools::StringUtils::Format("%s%s", settings->m_instanceDirectory.c_str(), "channel.cache")))
{
}

int Channels::GetNumChannels()
{
  // Kodi polls this while recordings are open avoid calls to backend
  std::lock_guard<std::recursive_mutex> lock(m_channelMutex);
  int channelCount = m_channelDetails.size();
  if (channelCount == 0)
  {
    tinyxml2::XMLDocument doc;
    if (GetChannelList(doc) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
      tinyxml2::XMLNode* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        channelCount++;
      }
    }
  }
  return channelCount;
}

std::string Channels::GetChannelIcon(int channelID)
{
  std::string iconFilename = GetChannelIconFileName(channelID);

  // do we already have the icon file?
  if (kodi::vfs::FileExists(iconFilename))
  {
    return iconFilename;
  }
  const std::string URL = "/service?method=channel.icon&channel_id=" + std::to_string(channelID);
  if (m_request.FileCopy(URL.c_str(), iconFilename) == HTTP_OK)
  {
    return iconFilename;
  }

  return "";
}

std::string Channels::GetChannelIconFileName(int channelID)
{
  return kodi::tools::StringUtils::Format("%snextpvr-ch%d.png",m_settings->m_instanceDirectory.c_str(), channelID);
}

void  Channels::DeleteChannelIcon(int channelID)
{
  kodi::vfs::DeleteFile(GetChannelIconFileName(channelID));
}

void Channels::DeleteChannelIcons()
{
  std::vector<kodi::vfs::CDirEntry> icons;
  if (kodi::vfs::GetDirectory(m_settings->m_instanceDirectory, "nextpvr-ch*.png", icons))
  {
    kodi::Log(ADDON_LOG_INFO, "Deleting %d channel icons", icons.size());
    for (auto const& it : icons)
    {
      const std::string deleteme = it.Path();
      kodi::Log(ADDON_LOG_DEBUG, "DeleteFile %s rc:%d", kodi::vfs::TranslateSpecialProtocol(deleteme).c_str(), kodi::vfs::DeleteFile(deleteme));
    }
  }
}

PVR_ERROR Channels::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  if (radio && !m_settings->m_showRadio)
    return PVR_ERROR_NO_ERROR;
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  std::string stream;

  tinyxml2::XMLDocument doc;
  if (GetChannelList(doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
    tinyxml2::XMLNode* pChannelNode;
    for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
    {
      kodi::addon::PVRChannel tag;
      tag.SetUniqueId(XMLUtils::GetUIntValue(pChannelNode, "id"));
      std::string buffer;
      XMLUtils::GetString(pChannelNode, "type", buffer);
      if ( buffer =="0xa")
      {
        tag.SetIsRadio(true);
        tag.SetMimeType("application/octet-stream");
      }
      else
      {
        tag.SetIsRadio(false);
        tag.SetMimeType("application/octet-stream");
        if (IsChannelAPlugin(tag.GetUniqueId()))
        {
          if (kodi::tools::StringUtils::EndsWithNoCase(m_liveStreams[tag.GetUniqueId()], ".m3u8"))
            tag.SetMimeType("application/x-mpegURL");
          else
            tag.SetMimeType("video/MP2T");
        }
      }
      if (radio != tag.GetIsRadio())
        continue;

      tag.SetChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "number"));
      tag.SetSubChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "minor"));

      buffer.clear();
      XMLUtils::GetString(pChannelNode, "name", buffer);
      if (m_settings->m_addChannelInstance)
        buffer += kodi::tools::StringUtils::Format(" (%d)", m_settings->m_instanceNumber);
      tag.SetChannelName(buffer);

      // check if we need to download a channel icon
      bool isIcon;
      if (XMLUtils::GetBoolean(pChannelNode, "icon", isIcon))
      {
        // only set when true;
        std::string iconFile = GetChannelIcon(tag.GetUniqueId());
        if (iconFile.length() > 0)
          tag.SetIconPath(iconFile);
      }
      // transfer channel to XBMC
      results.Add(tag);
    }
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  return returnValue;
}

/************************************************************/
/** Channel group handling **/

PVR_ERROR Channels::GetChannelGroupsAmount(int& amount)
{
  // this could be different from the number of backend groups if radio and TV are mixed or if groups are empty
  amount = m_radioGroups.size() + m_tvGroups.size();
  return PVR_ERROR_NO_ERROR;
}

PVR_RECORDING_CHANNEL_TYPE Channels::GetChannelType(unsigned int uid)
{
  // when uid is invalid we assume TV because Kodi will
  std::lock_guard<std::recursive_mutex> lock(m_channelMutex);
  if (m_channelDetails.count(uid) > 0 && m_channelDetails[uid].second == true)
    return PVR_RECORDING_CHANNEL_TYPE_RADIO;

  return PVR_RECORDING_CHANNEL_TYPE_TV;
}

PVR_ERROR Channels::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  if (radio && !m_settings->m_showRadio)
    return PVR_ERROR_NO_ERROR;
  std::lock_guard<std::recursive_mutex> lock(m_channelMutex);
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  int priority = 1;

  std::unordered_set<std::string>& selectedGroups = radio ? m_radioGroups : m_tvGroups;

  selectedGroups.clear();
  bool hasAllChannels = false;
  tinyxml2::XMLDocument doc;
  if (GetChannelList(doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
    tinyxml2::XMLNode* pChannelNode;
    for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
    {
      std::string buffer;
      XMLUtils::GetString(pChannelNode, "type", buffer);
      bool foundRadio = false;
      if ( buffer == "0xa")
      {
        foundRadio = true;
      }
      if (radio == foundRadio)
      {
        if (m_settings->m_allChannels && !hasAllChannels)
        {
          hasAllChannels = true;
          std::string allChannels = GetAllChannelsGroupName(radio);
          kodi::addon::PVRChannelGroup tag;
          tag.SetIsRadio(radio);
          tag.SetPosition(priority++);
          tag.SetGroupName(allChannels);
          results.Add(tag);
        }
        buffer.clear();
        if (XMLUtils::GetAdditiveString(pChannelNode->FirstChildElement("groups"), "group", "\t", buffer, true))
        {
          std::vector<std::string> groups = kodi::tools::StringUtils::Split(buffer, '\t');
          XMLUtils::GetString(pChannelNode, "type", buffer);
          for (auto const& group : groups)
          {
            if (selectedGroups.find(group) == selectedGroups.end())
            {
              selectedGroups.insert(group);
            }
          }
        }
      }
    }
  }
  else
  {
    return PVR_ERROR_SERVER_ERROR;
  }

  // Many users won't have radio groups
  if (selectedGroups.size() == 0)
    return PVR_ERROR_NO_ERROR;

  doc.Clear();
  if (m_request.DoMethodRequest("channel.groups", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* groupsNode = doc.RootElement()->FirstChildElement("groups");
    tinyxml2::XMLNode* pGroupNode;
    std::string group;
    for (pGroupNode = groupsNode->FirstChildElement("group"); pGroupNode; pGroupNode = pGroupNode->NextSiblingElement())
    {
      if (XMLUtils::GetString(pGroupNode, "name", group))
      {
        // "All Channels" won't match any group, skip empty NextPVR groups
        if (selectedGroups.find(group) != selectedGroups.end())
        {
          kodi::addon::PVRChannelGroup tag;
          tag.SetIsRadio(radio);
          tag.SetPosition(priority++);
          tag.SetGroupName(group);
          results.Add(tag);
        }
      }
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "No Channel Group");
    returnValue =  PVR_ERROR_SERVER_ERROR;
  }
  return returnValue;
}

PVR_ERROR Channels::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  PVR_ERROR returnValue = PVR_ERROR_SERVER_ERROR;

  tinyxml2::XMLDocument doc;
  tinyxml2::XMLError retCode;
  if (group.GetGroupName() == GetAllChannelsGroupName(group.GetIsRadio()))
  {
    retCode = GetChannelList(doc);
  }
  else
  {
    const std::string encodedGroupName = UriEncode(group.GetGroupName());
    retCode = m_request.DoMethodRequest("channel.list&group_id=" + encodedGroupName, doc);
  }

  if (retCode == tinyxml2::XML_SUCCESS)
  {
    std::lock_guard<std::recursive_mutex> lock(m_channelMutex);
    tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
    tinyxml2::XMLNode* pChannelNode;
    for (pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode = pChannelNode->NextSiblingElement())
    {
      kodi::addon::PVRChannelGroupMember tag;
      tag.SetChannelUniqueId(XMLUtils::GetUIntValue(pChannelNode, "id"));
      // ignore orphan channels in groups
      if (m_channelDetails.find(tag.GetChannelUniqueId()) != m_channelDetails.end()
        && group.GetIsRadio() == m_channelDetails[tag.GetChannelUniqueId()].second)
      {
        tag.SetGroupName(group.GetGroupName());
        tag.SetChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "number"));
        tag.SetSubChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "minor"));
        results.Add(tag);
      }
    }
    returnValue = PVR_ERROR_NO_ERROR;
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  return returnValue;
}

const std::string Channels::GetAllChannelsGroupName(bool radio)
{
  std::string allChannels;
  if (radio)
  {
    allChannels = kodi::tools::StringUtils::Format("%s %s",
      kodi::addon::GetLocalizedString(19216).c_str(), m_settings->m_instanceName.c_str());
  }
  else
  {
    allChannels = kodi::tools::StringUtils::Format("%s %s",
      kodi::addon::GetLocalizedString(19217).c_str(), m_settings->m_instanceName.c_str());
  }
  return allChannels;
}

bool Channels::IsChannelAPlugin(int uid)
{
  if (m_liveStreams.count(uid) != 0)
    if (kodi::tools::StringUtils::StartsWith(m_liveStreams[uid], "plugin:") || kodi::tools::StringUtils::EndsWithNoCase(m_liveStreams[uid], ".m3u8"))
      return true;

  return false;
}

/************************************************************/
void Channels::LoadLiveStreams()
{
  std::string response;
  const std::string URL = "/public/service.xml";
  m_liveStreams.clear();
  if (m_request.DoRequest(URL, response) == HTTP_OK)
  {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(response.c_str()) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* streamsNode = doc.FirstChildElement("streams");
      if (streamsNode)
      {
        tinyxml2::XMLElement* streamNode;
        for (streamNode = streamsNode->FirstChildElement("stream"); streamNode; streamNode = streamNode->NextSiblingElement())
        {
          const char* attrib = streamNode->Attribute("id");
          if (attrib != nullptr)
          {
            try
            {
              int channelID = std::atoi(attrib);
              kodi::Log(ADDON_LOG_DEBUG, "%d %s", channelID, streamNode->FirstChild()->Value());
              m_liveStreams[channelID] = streamNode->FirstChild()->Value();
            }
            catch (...)
            {
              kodi::Log(ADDON_LOG_DEBUG, "%s:%d", __FUNCTION__, __LINE__);
            }
          }
        }
      }
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "LiveStreams invalid xml");
    }
  }
}
bool Channels::ChannelCacheChanged(time_t updateTime)
{
  std::string checksum = m_checksumChannelList;
  std::string response;
  const time_t cacheTime = ReadChannelListCache(response);
  // on first load need to cache details
  if (cacheTime != 0 && m_channelDetails.empty())
    LoadChannelDetails();

  if (updateTime == cacheTime)
    return false;
  else 
  {
    // change EPG updateTime
    response.clear();
    // Get new channel list but return on error
    if (!ReloadChannelListCache(response, updateTime))
      return false;
  }
  // checksum will be empty on first call
  return checksum != m_checksumChannelList;
}

time_t Channels::ReadChannelListCache(std::string& response)
{
  time_t updateTime = 0;
  if (kodi::vfs::FileExists(m_channelCacheFile))
  {
    gzFile gz_file = gzopen(m_channelCacheFile.c_str(), "rb");
    if (gz_file != NULL)
    {
      CacheHeader header{ 0,0 };
      if (gzread(gz_file, (void*)&header, sizeof(CacheHeader)) == sizeof(CacheHeader))
      {
        response.resize(header.size);
        if (gzread(gz_file, (void*)response.data(), header.size) == header.size)
        {
          m_checksumChannelList = kodi::GetMD5(response);
          updateTime = header.updateTime;
        }
      }
    }
    gzclose(gz_file);
    if (updateTime == 0)
    {
      kodi::Log(ADDON_LOG_WARNING, "Remove invalid cache file.");
      kodi::vfs::DeleteFile(m_channelCacheFile);
    }
  }
  return updateTime;
}

bool Channels::ReloadChannelListCache(std::string& response, time_t updateTime)
{
  bool rc = false;
  gzFile gz_file;
  m_checksumChannelList.clear();
  if (m_request.DoRequest("/service?method=channel.list&extras=true", response) == HTTP_OK)
  {
    gz_file = gzopen(m_channelCacheFile.c_str(), "wb");
    if (gz_file != NULL)
    {
      CacheHeader header{ 0,0 };
      header.size = response.size();
      header.updateTime = updateTime;
      if (gzwrite(gz_file, (void*)&header, sizeof(CacheHeader)) == sizeof(CacheHeader))
      {
        if (gzwrite(gz_file, (void*)(response.c_str()), header.size) == header.size)
        {
          m_checksumChannelList = kodi::GetMD5(response);
          rc = true;
        }
      }
    }
    gzclose(gz_file);
  }
  if (!rc)
    kodi::Log(ADDON_LOG_ERROR, "Could not write channel cache");

  return rc;
}

tinyxml2::XMLError Channels::GetChannelList(tinyxml2::XMLDocument& doc)
{
  auto start = std::chrono::steady_clock::now();
  std::string response;
  if (ReadChannelListCache(response) != 0)
  {
    tinyxml2::XMLError xmlCheck = doc.Parse(response.c_str());
    if (xmlCheck == tinyxml2::XML_SUCCESS)
    {
      int milliseconds = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      kodi::Log(ADDON_LOG_DEBUG, "GetChannelList %d %d %d %d", m_settings->m_instanceNumber, xmlCheck, response.length(), milliseconds);
      return xmlCheck;
    }
  }
  kodi::Log(ADDON_LOG_ERROR, "Cannot read channel cache");
  return m_request.DoMethodRequest("channel.list&extras=true", doc);
}

bool Channels::ResetChannelCache(time_t updateTime)
{
  if (ChannelCacheChanged(updateTime) && !m_checksumChannelList.empty())
  {
    // m_checksumChannelList will be empty on error
    std::lock_guard<std::recursive_mutex> lock(m_channelMutex);
    auto oldDetails = m_channelDetails;
    m_channelDetails.clear();
    LoadChannelDetails();
    for( const auto &details : oldDetails)
    {
      if (m_channelDetails.find(details.first) == m_channelDetails.end())
      {
        DeleteChannelIcon(details.first);
      }
    }
    return true;
  }
  return false;
}

bool Channels::LoadChannelDetails()
{
  tinyxml2::XMLDocument doc;
  if (GetChannelList(doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
    tinyxml2::XMLNode* pChannelNode;
    for (pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode = pChannelNode->NextSiblingElement())
    {
      std::string buffer;
      bool isRadio = false;
      XMLUtils::GetString(pChannelNode, "type", buffer);
      if (buffer == "0xa")
      {
        if (!m_settings->m_showRadio)
          continue;
        isRadio = true;
      }
      std::string epg;
      if (XMLUtils::GetString(pChannelNode, "epg", epg))
        m_channelDetails[XMLUtils::GetUIntValue(pChannelNode, "id")] = std::make_pair(epg == "None", isRadio);
      else
        m_channelDetails[XMLUtils::GetUIntValue(pChannelNode, "id")] = std::make_pair(false, isRadio);
    }
    return true;
  }
  return false;
}
