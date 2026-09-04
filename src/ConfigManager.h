#ifndef GATESERVER_CONFIGMANAGER_H
#define GATESERVER_CONFIGMANAGER_H

#include "const.h"

/// 底层键值对
struct SectionInfo
{
    SectionInfo() = default;
    ~SectionInfo()
    {
        m_sectionDatas.clear();
    }

    SectionInfo(const SectionInfo& other)
    {
        m_sectionDatas = other.m_sectionDatas;
    }

    SectionInfo& operator=(const SectionInfo& other)
    {
        if (&other == this) // 自己拷贝自己
        {
            return *this;
        }

        m_sectionDatas = other.m_sectionDatas;
        return *this;
    }

    std::map<std::string, std::string> m_sectionDatas;

    std::string operator[](const std::string& key)
    {
        if (!m_sectionDatas.contains(key))
        {
            return "";
        }

        return m_sectionDatas[key];
    }
};

class ConfigManager
{
public:
    ConfigManager();

    ~ConfigManager()
    {
        m_configMap.clear();
    }

    ConfigManager(const ConfigManager& other)
    {
        m_configMap = other.m_configMap;
    }

    ConfigManager& operator=(const ConfigManager& other)
    {
        if (&other == this)
        {
            return *this;
        }

        m_configMap = other.m_configMap;
        return *this;
    }

    SectionInfo operator[](const std::string& section)
    {
        if (!m_configMap.contains(section))
        {
            return SectionInfo();
        }

        return m_configMap[section];
    }

private:
    std::map<std::string, SectionInfo> m_configMap;
};


#endif //GATESERVER_CONFIGMANAGER_H
