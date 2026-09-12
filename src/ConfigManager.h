#ifndef GATESERVER_CONFIGMANAGER_H
#define GATESERVER_CONFIGMANAGER_H

#include "const.h"

/// 底层键值对
struct SectionInfo
{
    SectionInfo() = default;
    ~SectionInfo() = default;
    SectionInfo(const SectionInfo &other) = default;
    SectionInfo &operator=(const SectionInfo &other) = default;

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
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager &other) = delete;
    ConfigManager &operator=(const ConfigManager &other) = delete;
    SectionInfo operator[](const std::string &section);

private:
    ConfigManager();

    std::map<std::string, SectionInfo> m_configMap;
};


#endif //GATESERVER_CONFIGMANAGER_H
