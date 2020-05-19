#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <future>

#include "TSourceEngineQuery.h"
#include "GlobalContext.h"
#include "parsemsg.h"

using namespace std::chrono_literals;
using boost::asio::ip::udp;

struct TSourceEngineQuery::impl_t {
    std::shared_ptr<boost::asio::io_context> ioc = GlobalContextSingleton();
};

TSourceEngineQuery::TSourceEngineQuery() : pimpl(std::make_shared<impl_t>())
{

}

TSourceEngineQuery::~TSourceEngineQuery()
{

}

std::string UTF8_To_ANSI(const std::string &str)
{
    return str;
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
        throw std::runtime_error("不支持的服务器信息协议格式");
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
        throw std::runtime_error("不支持的玩家列表协议格式");
    }
    return result;
}

template<class T>
void try_set_exception(std::promise<T>& pro, std::exception_ptr exc)
{
    try {
        pro.set_exception(exc);
    }
    catch (std::future_error&) {
        // ...
    }
}

template<class Ret, class Handler, class NewRet = typename std::invoke_result<Handler, Ret>::type>
std::future<NewRet> then(boost::asio::io_context & ioc, std::future<Ret> &fut, Handler fn)
{
    struct Context {
        std::future<Ret> fut;
        std::promise<NewRet> pro;
    };
    std::shared_ptr<Context> con = std::make_shared<Context>();
    con->fut = std::move(fut);
    std::future<NewRet> ret = con->pro.get_future();
    ioc.dispatch([fn, con]{
        try {
            if constexpr (std::is_void_v<NewRet>)
                con->pro.set_value();
            else
                con->pro.set_value(fn(con->fut.get()));
        }
        catch (...) {
            con->pro.set_exception(std::current_exception());
        }
    });
    return ret;
}

// Reference: https://developer.valvesoftware.com/wiki/Server_queries#A2S_INFO
auto TSourceEngineQuery::GetServerInfoDataAsync(const char *host, const char *port, std::chrono::seconds timeout) -> std::future<ServerInfoQueryResult>
{
    // 发送查询包
    std::shared_ptr<std::promise<ServerInfoQueryResult>> pro = std::make_shared<std::promise<ServerInfoQueryResult>>();
    std::shared_ptr<boost::asio::io_context> ioc = pimpl->ioc;
    std::shared_ptr<udp::resolver> resolver = std::make_shared<udp::resolver>(*ioc);
    
    resolver->async_resolve(udp::v4(), host, port, [resolver, ioc, pro, timeout](boost::system::error_code ec, udp::resolver::results_type endpoints) {
        if(ec)
            return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "解析域名时发生错误"))), void();

        std::shared_ptr<udp::socket> socket = std::make_shared<udp::socket>(*ioc, udp::endpoint(udp::v4(), 0));
        for(auto &&endpoint : endpoints)
        {
            static constexpr char request1[] = "\xFF\xFF\xFF\xFF" "TSource Engine Query"; // Source / GoldSrc Steam
            //static constexpr char request2[] = "\xFF\xFF\xFF\xFF" "details"; // GoldSrc WON
            //static constexpr char request3[] = "\xFF\xFF\xFF\xFF" "info"; // Xash3D

            socket->async_send_to(boost::asio::buffer(request1, sizeof(request1)), endpoint, [socket, endpoint, pro](boost::system::error_code ec, std::size_t bytes_transferred) {
                if(ec)
                    return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "发送服务器信息查询包时发生错误"))), void();
                std::shared_ptr<char> buffer(new char[8192], std::default_delete<char[]>());
                std::shared_ptr<udp::endpoint> sender_endpoint = std::make_shared<udp::endpoint>(udp::v4(), 0);
                socket->async_receive_from(boost::asio::buffer(buffer.get(), 8192), *sender_endpoint, [socket, buffer, sender_endpoint, pro](boost::system::error_code ec, std::size_t reply_length) {
                    if (ec)
                        return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "接收服务器信息查询包时发生错误"))), void();

                    try{
                        return pro->set_value(MakeServerInfoQueryResultFromBuffer(buffer.get(), reply_length, sender_endpoint->address().to_string(), sender_endpoint->port())), void();
                    } catch(...) {
                        return try_set_exception(*pro, std::current_exception()), void();
                    }
                });
            });
        }

        std::shared_ptr<boost::asio::system_timer> ddl = std::make_shared<boost::asio::system_timer>(*ioc);
        ddl->expires_from_now(timeout);
        ddl->async_wait([ioc, socket, pro, ddl](boost::system::error_code ec){
            socket->close(ec);
            return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(boost::asio::error::make_error_code(boost::asio::error::timed_out), "查询服务器超时，可能是服务器挂了或者IP不正确。"))), void();
        });
    });
    return pro->get_future();
}

auto TSourceEngineQuery::GetPlayerListDataAsync(const char *host, const char *port, std::chrono::seconds timeout) -> std::future<PlayerListQueryResult>
{
    std::shared_ptr<std::promise<PlayerListQueryResult>> pro = std::make_shared<std::promise<PlayerListQueryResult>>();
    std::shared_ptr<boost::asio::io_context> ioc = pimpl->ioc;
    std::shared_ptr<udp::resolver> resolver = std::make_shared<udp::resolver>(*ioc);

    resolver->async_resolve(udp::v4(), host, port, [resolver, ioc, pro, timeout](boost::system::error_code ec, udp::resolver::results_type endpoints) {
        if(ec)
            return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "解析域名时发生错误"))), void();
        std::shared_ptr<udp::socket> socket = std::make_shared<udp::socket>(*ioc, udp::endpoint(udp::v4(), 0));
        for(auto &&endpoint : endpoints)
        {
            // first attempt
            constexpr char request_challenge[] = "\xFF\xFF\xFF\xFF" "U" "\xFF\xFF\xFF\xFF";
            socket->async_send_to(boost::asio::buffer(request_challenge, sizeof(request_challenge)), endpoint, [socket, endpoint, pro](boost::system::error_code ec, std::size_t bytes_transferred) {
                if(ec)
                    return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "发送玩家查询包时发生错误"))), void();
                std::shared_ptr<char> buffer(new char[4096], std::default_delete<char[]>());
                std::shared_ptr<udp::endpoint> sender_endpoint = std::make_shared<udp::endpoint>(udp::v4(), 0);
                socket->async_receive_from(boost::asio::buffer(buffer.get(), 4096), *sender_endpoint, [socket, endpoint, buffer, sender_endpoint, pro](boost::system::error_code ec, std::size_t reply_length) {
                    if (ec)
                        return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "无法接收玩家查询包"))), void();

                    PlayerListQueryResult first_result;
                    try{
                        first_result = MakePlayerListQueryResultFromBuffer(buffer.get(), reply_length, sender_endpoint->address().to_string(), sender_endpoint->port());
                        if(first_result.Results.index() == 1)
                            return pro->set_value(first_result), void();
                    } catch(...) {
                        return try_set_exception(*pro, std::current_exception()), void();
                    }

                    // second attempt
                    const int32_t challenge = std::get<int32_t>(first_result.Results);
                    const char(&accessor)[4] = reinterpret_cast<const char(&)[4]>(challenge);
                    char request3[10] = { '\xFF', '\xFF', '\xFF', '\xFF', 'U', accessor[0], accessor[1], accessor[2], accessor[3], '\0' };
                    socket->async_send_to(boost::asio::buffer(request3, sizeof(request3)), endpoint, [socket, buffer, sender_endpoint, endpoint, pro](boost::system::error_code ec, std::size_t bytes_transferred) {
                        if (ec)
                            return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "发送challenge玩家查询包时发生错误"))), void();
                        socket->async_receive_from(boost::asio::buffer(buffer.get(), 4096), *sender_endpoint, [socket, endpoint, buffer, sender_endpoint, pro](boost::system::error_code ec, std::size_t reply_length) {
                            if (ec)
                                return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(ec, "无法接收challenge玩家查询包"))), void();
                            try{
                                return pro->set_value(MakePlayerListQueryResultFromBuffer(buffer.get(), reply_length, sender_endpoint->address().to_string(), sender_endpoint->port())), void();
                            } catch(...) {
                                return try_set_exception(*pro, std::current_exception()), void();
                            }
                        });
                    });
                });
            });
        }
        std::shared_ptr<boost::asio::system_timer> ddl = std::make_shared<boost::asio::system_timer>(*ioc);
        ddl->expires_from_now(timeout);
        ddl->async_wait([ioc, socket, pro, ddl](boost::system::error_code ec){
            socket->close(ec);
            return try_set_exception(*pro, std::make_exception_ptr(boost::system::system_error(boost::asio::error::make_error_code(boost::asio::error::timed_out), "查询服务器超时，可能是服务器挂了或者IP不正确。"))), void();
        });
    });
    return pro->get_future();
}