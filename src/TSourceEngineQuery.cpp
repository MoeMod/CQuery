#include <iostream>
#include <memory>
#include <boost/asio.hpp>

#include "TSourceEngineQuery.h"
#include "parsemsg.h"

using namespace std::chrono_literals;
using boost::asio::ip::udp;

struct TSourceEngineQuery::impl_t {
    boost::asio::io_context io_context;
    udp::socket socket_info{ io_context, udp::endpoint(udp::v4(), 0) };
    udp::socket socket_player{ io_context, udp::endpoint(udp::v4(), 0) };

};

std::string UTF8_To_ANSI(const std::string &str)
{
    return str;
}

TSourceEngineQuery::TSourceEngineQuery() : pimpl(std::make_shared<impl_t>())
{

}

auto TSourceEngineQuery::MakeServerInfoQueryResultFromBuffer(const char *reply, std::size_t reply_length, std::string address, uint16_t port) -> ServerInfoQueryResult
{
    BufferReader buf(reply, reply_length);
    ServerInfoQueryResult result{};
    result.header1 = buf.ReadLong(); // header -1
    result.header2 = buf.ReadByte(); // header ('I')

    if (result.header1 != -1)
        throw std::runtime_error("错误的返回数据头部(-1)");

    if (result.header2 == 'I')
    {
        // Steam版
        result.Protocol = buf.ReadByte();
        result.ServerName = UTF8_To_ANSI(buf.ReadString());
        result.Map = UTF8_To_ANSI(buf.ReadString());
        result.Folder = UTF8_To_ANSI(buf.ReadString());
        result.Game = UTF8_To_ANSI(buf.ReadString());
        result.SteamID = buf.ReadShort();
        result.PlayerCount = buf.ReadByte();
        result.MaxPlayers = buf.ReadByte();
        result.BotCount = buf.ReadByte();
        result.ServerType = static_cast<ServerType_e>(buf.ReadByte());
        result.Environment = static_cast<Environment_e>(buf.ReadByte());
        result.Visibility = static_cast<Visibility_e>(buf.ReadByte());
        result.VAC = buf.ReadByte();
        result.GameVersion = UTF8_To_ANSI(buf.ReadString());

        int EDF = buf.ReadByte();
        if (EDF & 0x80)
            result.Port = buf.ReadShort();
        if (EDF & 0x10)
            result.SteamIDExtended = { buf.ReadLong(), buf.ReadLong() };
        if (EDF & 0x40)
            result.SourceTVData = { buf.ReadShort(), UTF8_To_ANSI(buf.ReadString()) };
        if (EDF & 0x20)
            result.Keywords = UTF8_To_ANSI(buf.ReadString());
        if (EDF & 0x01)
            result.GameID = { buf.ReadLong(), buf.ReadLong() };
    }
    else if (result.header2 == 'm')
    {
        // 非Steam版
        result.LocalAddress = UTF8_To_ANSI(buf.ReadString());
        result.ServerName = UTF8_To_ANSI(buf.ReadString());
        result.Map = UTF8_To_ANSI(buf.ReadString());
        result.Folder = UTF8_To_ANSI(buf.ReadString());
        result.Game = UTF8_To_ANSI(buf.ReadString());
        result.PlayerCount = buf.ReadByte();
        result.MaxPlayers = buf.ReadByte();
        result.Protocol = buf.ReadByte();
        result.ServerType = static_cast<ServerType_e>(buf.ReadByte());
        result.Environment = static_cast<Environment_e>(buf.ReadByte());
        result.Visibility = static_cast<Visibility_e>(buf.ReadByte());

        if ((result.Mod = buf.ReadByte()) == true)
        {
            result.ModData = {
                    UTF8_To_ANSI(buf.ReadString()),
                    UTF8_To_ANSI(buf.ReadString()),
                    buf.ReadByte(),
                    buf.ReadLong(),
                    buf.ReadLong(),
                    static_cast<ServerInfoQueryResult::ModData_s::ModType_e>(buf.ReadByte()),
                    static_cast<bool>(buf.ReadByte())
            };
        }

        result.VAC = buf.ReadByte();
        result.BotCount = buf.ReadByte();
    }
    else
    {
        throw std::runtime_error("不支持的协议格式");
    }
    result.FromAddress = std::move(address);
    result.FromPort = port;
    return result;
}

auto TSourceEngineQuery::MakePlayerListQueryResultFromBuffer(const char *reply, std::size_t reply_length, std::string address, uint16_t port) -> PlayerListQueryResult
{
    PlayerListQueryResult result;
    BufferReader buf(reply, reply_length);
    result.header1 = buf.ReadLong();

    if (result.header1 != -1)
        throw std::runtime_error("错误的返回数据头部(-1)");

    result.header2 = buf.ReadByte();
    if (result.header2 == 'A')
    {
        result.Results.emplace<0>(buf.ReadLong());
    }
    else if (result.header2 == 'D')
    {
        volatile auto Players = buf.ReadByte();
        std::vector<PlayerListQueryResult::PlayerInfo_s> infos;
        while (!buf.Eof())
        {
            auto Index = buf.ReadByte();
            auto Name = UTF8_To_ANSI(buf.ReadString());
            auto Score = buf.ReadLong();
            float Duration = buf.ReadFloat();
            // 不能换成emplace_back因为要求大括号里面求值顺序从左到右
            infos.push_back({ Index, std::move(Name), Score, Duration });
        }
        result.Results.emplace<1>(std::move(infos));
    }
    else
    {
        throw std::runtime_error("不支持的协议格式");
    }
    return result;
}

template<class T>
auto ReceiveServerQueryResultAsync(std::shared_ptr<udp::socket> spSocket, std::function<T(const char *reply, std::size_t reply_length, std::string address, uint16_t port)> handler) -> std::shared_future<T>
{
    struct HandleData
    {
        std::array<char, 4096> buffer;
        udp::endpoint sender_endpoint;
        std::shared_future<T> saved_future; // 保存避免阻塞...
        std::function<T(const char *reply, std::size_t reply_length, std::string address, uint16_t port)> handler;
    };

    auto hd = std::make_shared<HandleData>();
    hd->handler = std::move(handler);

    std::shared_future<T> sf = std::async( std::launch::async, [spSocket, hd]() -> T {
        auto reply_length = spSocket->receive_from(boost::asio::buffer(hd->buffer.data(), 4096), hd->sender_endpoint);
        return hd->handler(hd->buffer.data(), reply_length, hd->sender_endpoint.address().to_string(), hd->sender_endpoint.port());
    });
    hd->saved_future = sf;
    // 为了NRVO优化不建议与上一行合并
    return sf;
}

// Reference: https://developer.valvesoftware.com/wiki/Server_queries#A2S_INFO
auto TSourceEngineQuery::GetServerInfoDataAsync(const char *host, const char *port) -> std::shared_future<ServerInfoQueryResult>
{
    // 发送查询包
    constexpr char request1[] = "\xFF\xFF\xFF\xFF" "TSource Engine Query"; // Source / GoldSrc Steam
    //constexpr char request2[] = "\xFF\xFF\xFF\xFF" "details"; // GoldSrc WON
    //constexpr char request3[] = "\xFF\xFF\xFF\xFF" "info"; // Xash3D
    for (auto endpoint : udp::resolver(pimpl->io_context).resolve(udp::v4(), host, port))
    {
        pimpl->socket_info.send_to(boost::asio::buffer(request1, sizeof(request1)), endpoint);
        //pimpl->socket_info.send_to(boost::asio::buffer(request2, sizeof(request2)), endpoint);
        //pimpl->socket_info.send_to(boost::asio::buffer(request3, sizeof(request3)), endpoint);
    }

    return ReceiveServerQueryResultAsync<ServerInfoQueryResult>(std::shared_ptr<udp::socket>(pimpl, &pimpl->socket_info), MakeServerInfoQueryResultFromBuffer);
}

auto TSourceEngineQuery::GetPlayerListDataAsync(const char *host, const char *port) -> std::shared_future<PlayerListQueryResult>
{
    // 同步发送challenge查询包
    constexpr char request[] = "\xFF\xFF\xFF\xFF" "U" "\xFF\xFF\xFF\xFF";
    auto resolve_result = udp::resolver(pimpl->io_context).resolve(udp::v4(), host, port);
    for (auto endpoint : resolve_result)
    {
        pimpl->socket_player.send_to(boost::asio::buffer(request, sizeof(request)), endpoint);
    }

    // 同步接收challenge查询包
    std::shared_future<PlayerListQueryResult> result = ReceiveServerQueryResultAsync<PlayerListQueryResult>(std::shared_ptr<udp::socket>(pimpl, &pimpl->socket_player), MakePlayerListQueryResultFromBuffer);

    if (result.wait_for(500ms) != std::future_status::ready)
        return std::async([]() -> PlayerListQueryResult { throw std::runtime_error("服务器未响应，未接收到challenge number。"); });

    // 再次发送查询
    auto &&await_result = result.get();
    if (await_result.Results.index() == 1)
        return result;

    const int32_t challenge = std::get<int32_t>(await_result.Results);
    const char(&accessor)[4] = reinterpret_cast<const char(&)[4]>(challenge);
    char request3[10] = { '\xFF', '\xFF', '\xFF', '\xFF', 'U', accessor[0], accessor[1], accessor[2], accessor[3], '\0' };

    for (auto endpoint : resolve_result)
        pimpl->socket_player.send_to(boost::asio::buffer(request3, sizeof(request3)), endpoint);

    return result = ReceiveServerQueryResultAsync<PlayerListQueryResult>(std::shared_ptr<udp::socket>(pimpl, &pimpl->socket_player), MakePlayerListQueryResultFromBuffer);
}