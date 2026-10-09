#ifndef GATESERVER_CONFIG_H
#define GATESERVER_CONFIG_H

#include <ConfigManager.h>
#include <string>
#include <vector>

/// 单实例部署：一个进程只运行一个 ChatServer 实例，跑哪一个由 --config 指向的配置文件决定。
/// 约定该配置文件中恰好存在一个 [ChatServerN] 段，段名即本实例的身份
[[nodiscard]] static std::string ResolveSelfSection(const ConfigManager& config)
{
    constexpr std::string_view prefix("ChatServer");

    std::vector<std::string> matched;
    for (const auto& name: config.sectionNames())
    {
        if (name.starts_with(prefix))
        {
            matched.emplace_back(name);
        }
    }

    if (matched.size() != 1)
    {
        throw std::runtime_error("config file must contain exactly one [" + std::string(prefix) +
                                 "N] section, but found " + std::to_string(matched.size()));
    }

    return matched.front();
}

#endif //GATESERVER_CONFIG_H
