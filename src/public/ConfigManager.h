#ifndef GATESERVER_CONFIGMANAGER_H
#define GATESERVER_CONFIGMANAGER_H

#include <string>
#include <vector>

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

    /// 已加载的 section 名列表，便于按前缀定位配置段（如 [ChatServerN]）
    std::vector<std::string> sectionNames() const;

    /// 从命令行解析配置文件路径，必须在首次 GetInstance() 之前调用，否则不生效
    /// 支持 --config=<path> 与 --config <path> 两种写法
    static void initFromArgs(int argc, char *argv[]);

    /// 当前生效的配置文件路径（解析后为绝对路径）
    static const std::string &configPath();

private:
    ConfigManager();

    /// 默认回落：进程当前工作目录下的 config.ini
    inline static std::string s_configPath = "config.ini";

    std::map<std::string, SectionInfo> m_configMap;
};


#endif //GATESERVER_CONFIGMANAGER_H
