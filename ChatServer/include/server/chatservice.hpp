#ifndef CHATSERVICE_H
#define CHATSERVICE_H
#include <muduo/net/TcpConnection.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Timestamp.h>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <ctime>
#include "json.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
#include "redis/redis.hpp"
using namespace muduo;
using namespace muduo::net;
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;

class ChatService
{
public:
  static ChatService *instance();
  void login(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void reg(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time);
  MsgHandler getHandler(int msgid);
  void clientCloseException(const TcpConnectionPtr &conn);
  void oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void reset();
  void addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void updateName(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void heartbeat(const TcpConnectionPtr &conn, json &js, Timestamp time);
  void handleRedisSubscribeMessage(int userid, string msg);
  void notifyFriendState(int userid, const string &state);
  void startHeartbeatCheck(EventLoop *loop);
  void stopHeartbeatCheck();
  void updateConnTime(const TcpConnectionPtr &conn);

private:
  ChatService();
  ChatService(const ChatService &) = delete;
  ChatService &operator=(const ChatService &) = delete;
  void checkHeartbeat();

  muduo::net::EventLoop *_loop = nullptr;
  muduo::net::TimerId _heartbeatTimerId;

  unordered_map<int, MsgHandler> _msgHandlerMap;
  unordered_map<int, TcpConnectionPtr> _userConnMap;
  mutex _connMutex;

  unordered_map<string, time_t> _connectionLastTime;

  UserModel _userModel;
  OfflineMsgModel _offlineMsgModel;
  FriendModel _friendModel;
  GroupModel _groupModel;

  Redis _redis;
};

#endif
