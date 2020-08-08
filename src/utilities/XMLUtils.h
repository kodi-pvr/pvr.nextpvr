/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <tinyxml2.h>
#include <vector>

namespace NextPVR
{
namespace utilities
{
namespace XMLUtils
{

/* \brief To get a text string value stored inside XML.

   \param[in] pRootNode TinyXML related node field
   \param[in] strTag XML identification tag
   \param[out] value The read value from XML
   \return true if available and successfully done
*/
inline bool GetString(const tinyxml2::XMLNode* pRootNode, const std::string& strTag, std::string& value)
{
  const tinyxml2::XMLElement* pElement = pRootNode->FirstChildElement(strTag.c_str());
  if (!pElement)
    return false;
  const tinyxml2::XMLNode* pNode = pElement->FirstChild();
  if (pNode != nullptr)
  {
    value = pNode->Value();
    return true;
  }
  value.clear();
  return false;
}

 /* \brief Get multiple tags, concatenating the values together.
   Transforms
     <tag>value1</tag>
     <tag clear="true">value2</tag>
     ...
     <tag>valuen</tag>
   into value2<sep>...<sep>valuen, appending it to the value string. Note that <value1> is overwritten by the clear="true" tag.

   \param rootNode    the parent containing the <tag>'s.
   \param tag         the <tag> in question.
   \param separator   the separator to use when concatenating values.
   \param value [out] the resulting string. Remains untouched if no <tag> is available, else is appended (or cleared based on the clear parameter).
   \param clear       if true, clears the string prior to adding tags, if tags are available. Defaults to false.
   */
inline bool GetAdditiveString(const tinyxml2::XMLNode* pRootNode, const std::string strTag, const std::string& strSeparator, std::string& strStringValue, bool clear)
{
  bool bResult = false;
  if (pRootNode != nullptr)
  {
    std::string strTemp;
    const tinyxml2::XMLElement* node = pRootNode->FirstChildElement(strTag.c_str());
    if (node && node->FirstChild() && clear)
      strStringValue.clear();
    while (node)
    {
      if (node->FirstChild())
      {
        bResult = true;
        strTemp = node->FirstChild()->Value();
        const char* clear = node->Attribute("clear");
        if (strStringValue.empty() || (clear && StringUtils::CompareNoCase(clear, "true") == 0))
          strStringValue = strTemp;
        else
          strStringValue += strSeparator + strTemp;
      }
      node = node->NextSiblingElement(strTag.c_str());
    }
  }
  return bResult;
}

/* \brief To get a 32 bit signed integer value stored inside XML.
   \param[in] pRootNode TinyXML related node field
   \param[in] strTag XML identification tag
   \param[out] value The read value from XML
   \return true if available and successfully done
*/
inline bool GetInt(const tinyxml2::XMLNode* pRootNode, const std::string& strTag, int32_t& value)
{
  const tinyxml2::XMLNode* pNode = pRootNode->FirstChildElement(strTag.c_str());
  if (!pNode || !pNode->FirstChild())
    return false;
  value = atoi(pNode->FirstChild()->Value());
  return true;
}

/* \brief To return a 32 bit integer value stored inside XML.
   \param[in] pRootNode TinyXML related node field
   \param[in] strTag XML identification tag
   \param[out] value The read value from XML
   \param[in] setDefault The value to return if node not found
   \return the found value or default
*/
inline int GetIntValue(const tinyxml2::XMLNode* pRootNode, const std::string& strTag, const int setDefault =  0)
{
  const tinyxml2::XMLNode* pNode = pRootNode->FirstChildElement(strTag.c_str());
  if (!pNode || !pNode->FirstChild())
    return setDefault;
  return atoi(pNode->FirstChild()->Value());
}

/* \brief To get a 32 bit unsigned integer value stored inside XML.
   \param[in] pRootNode TinyXML related node field
   \param[in] strTag XML identification tag
   \param[out] value The read value from XML
   \return true if available and successfully done
*/
inline bool GetUInt(const tinyxml2::XMLElement* pRootNode, const std::string& strTag, uint32_t& value)
{
  const tinyxml2::XMLNode* pNode = pRootNode->FirstChildElement(strTag.c_str());
  if (!pNode || !pNode->FirstChild())
    return false;
  value = atol(pNode->FirstChild()->Value());
  return true;
}

/* \brief To return an unsigned 32 bit integer value stored inside XML.
   \param[in] pRootNode TinyXML related node field
   \param[in] strTag XML identification tag
   \param[out] value The read value from XML
      \param[in] setDefault The value to return if node not found
   \return the found value or default
*/
inline int GetUIntValue(const tinyxml2::XMLNode* pRootNode, const std::string& strTag, const unsigned int setDefault = 0)
{
  const tinyxml2::XMLNode* pNode = pRootNode->FirstChildElement(strTag.c_str());
  if (!pNode || !pNode->FirstChild())
    return setDefault;
  return atol(pNode->FirstChild()->Value());
}

/* \brief To get a 64 bit integer value stored inside XML.

   \param[in] pRootNode TinyXML related node field
   \param[in] strTag XML identification tag
   \param[out] value The read value from XML
   \return true if available and successfully done
*/
inline bool GetLong(const tinyxml2::XMLNode* pRootNode, const std::string& strTag, int64_t& value)
{
  const tinyxml2::XMLNode* pNode = pRootNode->FirstChildElement(strTag.c_str());
  if (!pNode || !pNode->FirstChild())
    return false;
  value = atoll(pNode->FirstChild()->Value());
  return true;
}

/* \brief To get a boolean value stored inside XML.
   \param[in] pRootNode TinyXML related node field
   \param[in] strTag XML identification tag
   \param[out] value The read value from XML
   \return true if available and successfully done
*/
inline bool GetBoolean(const tinyxml2::XMLNode* pRootNode, const std::string& strTag, bool& value)
{
  const tinyxml2::XMLNode* pNode = pRootNode->FirstChildElement(strTag.c_str());
  if (!pNode || !pNode->FirstChild())
    return false;
  std::string strEnabled = pNode->FirstChild()->Value();
  std::transform(strEnabled.begin(), strEnabled.end(), strEnabled.begin(), ::tolower);
  if (strEnabled == "off" || strEnabled == "no" || strEnabled == "disabled" ||
    strEnabled == "false" || strEnabled == "0")
    value = false;
  else
  {
    value = true;
    if (strEnabled != "on" && strEnabled != "yes" && strEnabled != "enabled" &&
      strEnabled != "true")
      return false; // invalid bool switch - it's probably some other string.
  }
  return true;
}
//------------------------------------------------------------------------------

} /* namespace XMLUtils */
} /* namespace utilities */
} /* namespace NextPVR */
