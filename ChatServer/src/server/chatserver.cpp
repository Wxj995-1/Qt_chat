#include <functional>
#include <algorithm>
#include <arpa/inet.h>
#include "json.hpp"
#include "AsyncLog.hpp"
#include "public.hpp"
#include "chatserver.hpp"
#include "chatservice.hpp"
using namespace std;
using namespace placeholders;
using json = nlohmann::json;
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
  // 注册绑定回调
  _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

  // 注册消息回调
  _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

  // 设置线程数量
  _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
  _server.start();
}

// 上报连接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
  // 客服端断开连接
  if (!conn->connected())
  {
    ChatService::instance()->clientCloseException(conn);
    conn->shutdown();
  }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time)
{
  while (buffer->readableBytes() >= 4)
  {
    uint32_t totalLen = ntohl(*(uint32_t *)buffer->peek());
    if (totalLen > 1024 * 1024 || totalLen == 0)
    {
      buffer->retrieveAll();
      break;
    }
    if (buffer->readableBytes() < 4 + totalLen)
      break;

    buffer->retrieve(4);
    string msg(buffer->peek(), totalLen);
    buffer->retrieve(totalLen);

    try
    {
      json js = json::parse(msg);
      ChatService::instance()->updateConnTime(conn);
      auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
      msgHandler(conn, js, time);
    }
    catch (json::parse_error &e)
    {
      LOGE("JSON parse error: %s, msg=%s", e.what(), msg.c_str());
    }
    catch (json::exception &e)
    {
      LOGE("JSON exception: %s, msg=%s", e.what(), msg.c_str());
    }
    catch (std::exception &e)
    {
      LOGE("Unknown exception: %s, msg=%s", e.what(), msg.c_str());
    }
  }
}
