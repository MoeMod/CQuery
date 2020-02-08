#include <iostream>
#include <set>
#include <sstream>
#include <regex>

#include <cqcppsdk/cqcppsdk.h>
#include "TSourceEngineQuery.h"

using namespace cq;
using namespace std::chrono_literals;

std::string QueryServerInfo(const char *host, const char *port) noexcept(false)
{
    TSourceEngineQuery tseq;
    auto finfo = tseq.GetServerInfoDataAsync(host, port);
    auto fplayer = tseq.GetPlayerListDataAsync(host, port);

    if (finfo.wait_for(500ms) != std::future_status::timeout)
    {
        auto result = finfo.get(); // try
        std::ostringstream oss;
        oss << result.ServerName  << std::endl;
        oss << "\t" << result.Map << " (" << result.PlayerCount << "/" << result.MaxPlayers << ") - " << result.FromAddress << ":" << result.FromPort << std::endl;

        std::string myReply = oss.str();

        if (finfo.wait_for(1000ms) == std::future_status::ready)
        {
            try
            {
                auto playerlist = std::get<1>(fplayer.get().Results);
                for (const auto &player : playerlist)
                {
                    myReply += player.Name;
                    myReply += " [";
                    myReply += std::to_string(player.Score) + "分";
                    //int duration = static_cast<int>(player.Duration);
                    //myReply += std::to_string(duration / 60) + ":" + std::to_string(duration % 60);
                    myReply += "] / ";
                }
            }
            catch (const std::exception &)
            {
                // ...
            }
        }

        return myReply;
    }
    return "服务器未响应。";
}

std::pair<std::string, std::string> ParseHostPort(const std::string &msg)
{
    static std::regex r1(R"((^[0-9a-zA-Z]+[0-9a-zA-Z\.-]*\.[a-zA-Z]{2,4}):*(\d+)*)");
    static std::regex r2(R"((?:(?:25[0-5]|2[0-4]\d|((1\d{2})|([1-9]?\d)))\.){3}(?:25[0-5]|2[0-4]\d|((1\d{2})|([1-9]?\d)))(:\d+)*)");

    std::pair<std::string, std::string> ret { {}, "27015" };
    auto &[host, port] = ret;

    if (std::smatch sm; std::regex_match(msg, sm, r1) && sm.size() > 2)
    {
        // host:port, host, port
        host = sm[1].str();
        if (sm[2].str().size())
            port = sm[2].str();
    }
    else if (std::smatch sm; std::regex_match(msg, sm, r2) && sm.size() > 2)
    {
        // ip:port, ...
        const auto adr = sm[0].str();
        auto iter = std::find(adr.begin(), adr.end(), ':');
        host.assign(adr.begin(), iter);
        if (iter != adr.end())
            port.assign(iter + 1, adr.end());
    }
    return ret;
}

std::string ParseServerQueryMessage(const std::string &msg) try
{
    if (auto [host, port] = ParseHostPort(msg); !host.empty())
    {
        return QueryServerInfo(host.c_str(), port.c_str());
    }
    return {};
}
catch (const std::exception &e)
{
    return e.what();
}

CQ_INIT {
    on_enable([] { logging::info("启用", "插件已启用"); });

    on_message([](const MessageEvent &e) {
        if(std::string reply = ParseServerQueryMessage(e.message); !reply.empty())
        {
            send_message(e.target, reply);
            e.block(); // 阻止当前事件传递到下一个插件
        }
    });
}
