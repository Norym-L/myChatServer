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

    SectionInfo(const SectionInfo &other)
    {
        m_sectionDatas = other.m_sectionDatas;
    }

    SectionInfo &operator=(const SectionInfo &other)
    {
        if (&other == this) // 自己拷贝自己
        {
            return *this;
        }

        m_sectionDatas = other.m_sectionDatas;
        return *this;
    }

    std::map<std::string, std::string> m_sectionDatas;

    std::string operator[](const std::string &key)
    {
        if (!m_sectionDatas.contains(key))
        {
            return "";
        }

        return m_sectionDatas[key];
    }
};

class ConfigManager : public TSingLeton<ConfigManager>
{
    friend class TSingLeton;

public:
    ~ConfigManager();
    ConfigManager(const ConfigManager &other);
    ConfigManager &operator=(const ConfigManager &other);
    SectionInfo operator[](const std::string &section);

private:
    ConfigManager();

    std::map<std::string, SectionInfo> m_configMap;
};


#endif //GATESERVER_CONFIGMANAGER_H
