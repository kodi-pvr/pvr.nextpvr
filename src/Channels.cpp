/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Channels.h"
#include "utilities/XMLUtils.h"
#include "pvrclient-nextpvr.h"

#include <kodi/tools/StringUtils.h>
#include "zlib.h"

using namespace NextPVR;
using namespace NextPVR::utilities;

/** Channel handling */

Channels::Channels(const std::shared_ptr<InstanceSettings>& settings, Request& request) :
  m_settings(settings),
  m_request(request)
{
}

int Channels::GetNumChannels()
{
  // Kodi polls this while recordings are open avoid calls to backend
  int channelCount = m_channelDetails.size();
  if (channelCount == 0)
  {
    tinyxml2::XMLDocument doc;
    if (ReadCachedChannelList(doc) == tinyxml2::XML_SUCCESS)
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
  std::map<int, std::pair<bool, bool>>::iterator  itr = m_channelDetails.begin();
  while (itr != m_channelDetails.end())
  {
    if (itr->second.second == (radio == true))
      itr = m_channelDetails.erase(itr);
    else
      ++itr;
  }

  tinyxml2::XMLDocument doc;
  if (ReadCachedChannelList(doc) == tinyxml2::XML_SUCCESS)
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

      // V5 has the EPG source type info.
      std::string epg;
      if (XMLUtils::GetString(pChannelNode, "epg", epg))
        m_channelDetails[tag.GetUniqueId()] = std::make_pair(epg == "None", tag.GetIsRadio());
      else
        m_channelDetails[tag.GetUniqueId()] = std::make_pair(false, tag.GetIsRadio());

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
  if (m_channelDetails.count(uid) > 0 && m_channelDetails[uid].second == true)
    return PVR_RECORDING_CHANNEL_TYPE_RADIO;

  return PVR_RECORDING_CHANNEL_TYPE_TV;
}

PVR_ERROR Channels::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  if (radio && !m_settings->m_showRadio)
    return PVR_ERROR_NO_ERROR;

  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  int priority = 1;

  std::unordered_set<std::string>& selectedGroups = radio ? m_radioGroups : m_tvGroups;

  selectedGroups.clear();
  bool hasAllChannels = false;
  tinyxml2::XMLDocument doc;
  if (ReadCachedChannelList(doc) == tinyxml2::XML_SUCCESS)
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
    retCode = ReadCachedChannelList(doc);
  }
  else
  {
    const std::string encodedGroupName = UriEncode(group.GetGroupName());
    retCode = m_request.DoMethodRequest("channel.list&group_id=" + encodedGroupName, doc);
  }

  if (retCode == tinyxml2::XML_SUCCESS)
  {
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
bool Channels::CacheAllChannels(time_t updateTime)
{
  std::string response;
  const std::string filename = kodi::tools::StringUtils::Format("%s%s", m_settings->m_instanceDirectory.c_str(), "channel.cache");
  gzFile gz_file;
  struct { time_t update; unsigned long size; } header{0,0};
  if (kodi::vfs::FileExists(filename))
  {
    gz_file = gzopen(kodi::vfs::TranslateSpecialProtocol(filename).c_str(), "rb");
    gzread(gz_file, (void*)&header, sizeof(header));
    gzclose(gz_file);
    if (updateTime == header.update)
    {
      return true;
    }
  }
  if (m_request.DoRequest("/service?method=channel.list&extras=true", response) == HTTP_OK)
  {
    gz_file = gzopen(kodi::vfs::TranslateSpecialProtocol(filename).c_str(), "wb");
    header.size = sizeof(char) * response.size();
    header.update = updateTime - m_settings->m_serverTimeOffset;
    gzwrite(gz_file, (void*)&header, sizeof(header));
    gzwrite(gz_file, (void*)(response.c_str()), header.size);
    gzclose(gz_file);
    return true;
  }
  return false;
}

tinyxml2::XMLError Channels::ReadCachedChannelList(tinyxml2::XMLDocument& doc)
{
  auto start = std::chrono::steady_clock::now();
  std::string response;
  const std::string filename = kodi::tools::StringUtils::Format("%s%s", m_settings->m_instanceDirectory.c_str(), "channel.cache");
  struct { time_t update; unsigned long size; } header{0,0};
  gzFile gz_file = gzopen(kodi::vfs::TranslateSpecialProtocol(filename).c_str(), "rb");
  gzread(gz_file, (void*)&header, sizeof(header));
  response.resize(header.size / sizeof(char));
  gzread(gz_file, (void*)response.data(), header.size);
  gzclose(gz_file);
  tinyxml2::XMLError xmlCheck = doc.Parse(response.c_str());
  if (doc.Parse(response.c_str()) != tinyxml2::XML_SUCCESS)
    return m_request.DoMethodRequest("channel.list&extras=true", doc);
  int milliseconds = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
  kodi::Log(ADDON_LOG_DEBUG, "ReadCachedChannelList %d %d %d %d", m_settings->m_instanceNumber, xmlCheck, response.length(), milliseconds);
  return xmlCheck;
}
