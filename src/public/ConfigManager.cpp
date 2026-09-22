#include <ConfigManager.h>
#include <spdlog/spdlog.h>

ConfigManager::ConfigManager()
{
    const auto current_path = boost::filesystem::current_path();
    const auto config_path = current_path / "config.ini";
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
