#include <stdexcept>
#include <string_view>

#include <ConfigManager.h>
#include <spdlog/spdlog.h>

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

namespace
{
constexpr std::string_view CONFIG_FLAG("--config");
}

ConfigManager::ConfigManager()
{
    // s_configPath 可能是相对路径，统一按进程当前工作目录解析成绝对路径，日志里才能看出实际读了哪份配置
    const auto config_path = boost::filesystem::absolute(s_configPath);

    // 文件不存在时给出明确路径，避免只剩 property_tree 的 ini_parser_error 而看不出缺的是哪个文件
    if (!boost::filesystem::exists(config_path))
    {
        throw std::runtime_error("[ConfigManager] config file not found: " + config_path.string() +
                                 " (use --config=<path> to specify one)");
    }

    s_configPath = config_path.string();
    spdlog::info("[ConfigManager] Config path: {}", config_path.string());

    boost::property_tree::ptree ptree;
    boost::property_tree::read_ini(config_path.string(), ptree);

    for (const auto &[name, tree]: ptree)
    {
        const std::string &section_name = name;
        const boost::property_tree::ptree &section_tree = tree;

        std::map<std::string, std::string> section_config;
        for (const auto &[key, value]: section_tree)
        {
            const auto value_str = value.get_value<std::string>();
            section_config[key] = value_str;
        }

        SectionInfo section_info;
        section_info.m_sectionDatas = section_config;
        m_configMap[name] = section_info;
    }

    for (const auto &[name, config]: m_configMap)
    {
        spdlog::debug("[ConfigManager] [{}]", name);
        for (const auto &[key, value]: config.m_sectionDatas)
        {
            spdlog::debug("[ConfigManager] {}={}", key, value);
        }
    }
}

SectionInfo ConfigManager::operator[](const std::string &section)
{
    if (!m_configMap.contains(section))
    {
        return {};
    }

    return m_configMap[section];
}

std::vector<std::string> ConfigManager::sectionNames() const
{
    std::vector<std::string> names;
    names.reserve(m_configMap.size());

    for (const auto &[name, section]: m_configMap)
    {
        names.emplace_back(name);
    }

    return names;
}

void ConfigManager::initFromArgs(const int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr)
        {
            continue;
        }

        const std::string_view arg(argv[i]);

        if (!arg.starts_with(CONFIG_FLAG))
        {
            continue;
        }

        // --config=<path>
        if (arg.size() > CONFIG_FLAG.size() && arg[CONFIG_FLAG.size()] == '=')
        {
            s_configPath = std::string(arg.substr(CONFIG_FLAG.size() + 1));
            continue;
        }

        // --config <path>
        if (arg == CONFIG_FLAG && i + 1 < argc && argv[i + 1] != nullptr)
        {
            s_configPath = argv[++i];
            continue;
        }

        spdlog::warn("[ConfigManager] ignore malformed argument: {}", arg);
    }
}

const std::string &ConfigManager::configPath()
{
    return s_configPath;
}
