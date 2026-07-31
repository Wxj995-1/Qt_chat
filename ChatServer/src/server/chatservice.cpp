#include "chatservice.hpp"
#include "public.hpp"
#include "AsyncLog.hpp"
#include <vector>
#include <functional>
#include <ctime>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
  static ChatService service;
  return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
  // 用户基本业务管理相关事件处理回调注册
  _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
  _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
  _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
  _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
  _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

  // 群组业务管理相关事件处理回调注册
  _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
  _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
  _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});
  _msgHandlerMap.insert({UPDATE_NAME_MSG, std::bind(&ChatService::updateName, this, _1, _2, _3)});
  _msgHandlerMap.insert({HEARTBEAT_MSG, std::bind(&ChatService::heartbeat, this, _1, _2, _3)});

  // 连接redis服务器
  if (_redis.connect())
  {
    // 设置上报消息的回调
    _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
  }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
  // 把online状态的用户，设置成offline
  _userModel.resetState();
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
  // 记录错误日志，msgid没有对应的事件处理回调
  auto it = _msgHandlerMap.find(msgid);
  if (it == _msgHandlerMap.end())
  {
    // 返回一个默认的处理器，空操作
    return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
    {
      LOGE("msgid:%d can not find handler!", msgid);
    };
  }
  else
  {
    return it->second;
  }
}

// 处理登录业务  id  pwd   pwd
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id") || !js.contains("password"))
  {
    json response;
    response["msgid"] = LOGIN_MSG_ACK;
    response["errno"] = 1;
    response["errmsg"] = "invalid fields";
    sendJson(conn, response);
    return;
  }
  int id = js["id"].get<int>();
  string pwd = js["password"];

  User user = _userModel.query(id);
  if (user.getId() == id && user.getPwd() == pwd)
  {
    if (user.getState() == "online")
    {
      LOGI("user already online, force relogin: id=%d", id);
      // 强制踢掉旧连接
      {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
          it->second->shutdown();
          _userConnMap.erase(it);
        }
      }
      _redis.unsubscribe(id);
    }

    {
      LOGI("user login: id=%d", id);
      // 登录成功，记录用户连接信息
      {
        lock_guard<mutex> lock(_connMutex);
        _userConnMap.insert({id, conn});
      }

      // id用户登录成功后，向redis订阅channel(id)
      _redis.subscribe(id);

      // 登录成功，更新用户状态信息 state offline=>online
      user.setState("online");
      _userModel.updateState(user);

      json response;
      response["msgid"] = LOGIN_MSG_ACK;
      response["errno"] = 0;
      response["id"] = user.getId();
      response["name"] = user.getName();
      // 查询该用户是否有离线消息
      vector<string> vec = _offlineMsgModel.query(id);
      if (!vec.empty())
      {
        response["offlinemsg"] = vec;
      }

      // 查询该用户的好友信息并返回
      vector<User> userVec = _friendModel.query(id);
      if (!userVec.empty())
      {
        vector<string> vec2;
        for (User &user : userVec)
        {
          json js;
          js["id"] = user.getId();
          js["name"] = user.getName();
          js["state"] = user.getState();
          vec2.push_back(js.dump());
        }
        response["friends"] = vec2;
      }

      // 查询用户的群组信息
      vector<Group> groupuserVec = _groupModel.queryGroups(id);
      if (!groupuserVec.empty())
      {
        // group:[{groupid:[xxx, xxx, xxx, xxx]}]
        vector<string> groupV;
        for (Group &group : groupuserVec)
        {
          json grpjson;
          grpjson["id"] = group.getId();
          grpjson["groupname"] = group.getName();
          grpjson["groupdesc"] = group.getDesc();
          vector<string> userV;
          for (GroupUser &user : group.getUsers())
          {
            json js;
            js["id"] = user.getId();
            js["name"] = user.getName();
            js["state"] = user.getState();
            js["role"] = user.getRole();
            userV.push_back(js.dump());
          }
          grpjson["users"] = userV;
          groupV.push_back(grpjson.dump());
        }

        response["groups"] = groupV;
      }

      sendJson(conn, response);
      // 发送成功后删除离线消息，避免连接断开时丢失（宁重勿丢）
      if (!vec.empty())
      {
        _offlineMsgModel.remove(id);
      }
      // 通知好友该用户上线
      notifyFriendState(id, "online");
    }
  }
  else
  {
    LOGI("login failed: id=%d", id);
    // 该用户不存在，用户存在但是密码错误，登录失败
    json response;
    response["msgid"] = LOGIN_MSG_ACK;
    response["errno"] = 1;
    response["errmsg"] = "id or password is invalid!";
    sendJson(conn, response);
  }
}

// 处理注册业务  name  password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("name") || !js.contains("password"))
  {
    json response;
    response["msgid"] = REG_MSG_ACK;
    response["errno"] = 1;
    response["errmsg"] = "invalid fields";
    sendJson(conn, response);
    return;
  }
  string name = js["name"];
  string pwd = js["password"];

  User user;
  user.setName(name);
  user.setPwd(pwd);
  bool state = _userModel.insert(user);
  if (state)
  {
    LOGI("user register: id=%d name=%s", user.getId(), name.c_str());
    // 注册成功
    json response;
    response["msgid"] = REG_MSG_ACK;
    response["errno"] = 0;
    response["id"] = user.getId();
    sendJson(conn, response);
  }
  else
  {
    // 注册失败
    json response;
    response["msgid"] = REG_MSG_ACK;
    response["errno"] = 1;
    sendJson(conn, response);
  }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id"))
    return;
  int userid = js["id"].get<int>();

  {
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
      _userConnMap.erase(it);
    }
  }

  // 用户注销，相当于就是下线，在redis中取消订阅通道
  _redis.unsubscribe(userid);

  {
    lock_guard<mutex> lock(_connMutex);
    _connectionLastTime.erase(conn->name());
  }

  LOGI("user loginout: id=%d", userid);
  // 更新用户的状态信息
  User user(userid, "", "", "offline");
  _userModel.updateState(user);
  notifyFriendState(userid, "offline");
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
  User user;
  {
    lock_guard<mutex> lock(_connMutex);
    for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
    {
      if (it->second == conn)
      {
        // 从map表删除用户的链接信息
        user.setId(it->first);
        _userConnMap.erase(it);
        break;
      }
    }
  }

  // 用户注销，相当于就是下线，在redis中取消订阅通道
  _redis.unsubscribe(user.getId());

  {
    lock_guard<mutex> lock(_connMutex);
    _connectionLastTime.erase(conn->name());
  }

  // 更新用户的状态信息
  if (user.getId() != -1)
  {
    LOGI("client close exception: id=%d", user.getId());
    user.setState("offline");
    _userModel.updateState(user);
    notifyFriendState(user.getId(), "offline");
  }
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id") || !js.contains("toid"))
    return;
  int toid = js["toid"].get<int>();
  int fromid = js["id"].get<int>();

  TcpConnectionPtr toConn;
  {
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(toid);
    if (it != _userConnMap.end())
      toConn = it->second;
  }

  if (toConn)
  {
    LOGI("oneChat online: %d -> %d", fromid, toid);
    sendJson(toConn, js);
    return;
  }

  // 查询toid是否在线
  User user = _userModel.query(toid);
  if (user.getState() == "online")
  {
    LOGI("oneChat cross: %d -> %d", fromid, toid);
    _redis.publish(toid, js.dump());
    return;
  }

  LOGI("oneChat offline: %d -> %d", fromid, toid);
  // toid不在线，存储离线消息
  _offlineMsgModel.insert(toid, js.dump());
}

// 添加好友业务 msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id") || !js.contains("friendid"))
    return;
  int userid = js["id"].get<int>();
  int friendid = js["friendid"].get<int>();

  LOGI("addFriend: %d -> %d", userid, friendid);
  // 存储好友信息（双向，使好友关系对等）
  _friendModel.insert(userid, friendid);
  _friendModel.insert(friendid, userid);

  // 响应客户端，返回好友信息
  User friendUser = _userModel.query(friendid);
  json response;
  response["msgid"] = ADD_FRIEND_MSG;
  response["id"] = friendUser.getId();
  response["name"] = friendUser.getName();
  response["state"] = friendUser.getState();
  sendJson(conn, response);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id") || !js.contains("groupname"))
    return;
  int userid = js["id"].get<int>();
  string name = js["groupname"];
  string desc = js["groupdesc"];

  // 存储新创建的群组信息
  Group group(-1, name, desc);
  if (_groupModel.createGroup(group))
  {
    LOGI("createGroup: user=%d id=%d name=%s", userid, group.getId(), name.c_str());
    // 存储群组创建人信息
    _groupModel.addGroup(userid, group.getId(), "creator");

    // 响应客户端，返回群组信息
    json response;
    response["msgid"] = CREATE_GROUP_MSG;
    response["groupid"] = group.getId();
    response["groupname"] = group.getName();
    response["groupdesc"] = group.getDesc();
    sendJson(conn, response);
  }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id") || !js.contains("groupid"))
    return;
  int userid = js["id"].get<int>();
  int groupid = js["groupid"].get<int>();
  _groupModel.addGroup(userid, groupid, "normal");

  LOGI("addGroup: user=%d groupid=%d", userid, groupid);

  // 响应客户端，返回群组信息
  Group group = _groupModel.queryGroup(groupid);
  json response;
  response["msgid"] = ADD_GROUP_MSG;
  response["groupid"] = group.getId();
  response["groupname"] = group.getName();
  sendJson(conn, response);
}

// 修改名字业务
void ChatService::updateName(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id") || !js.contains("name"))
    return;
  int userid = js["id"].get<int>();
  string newName = js["name"];

  LOGI("updateName: user=%d newName=%s", userid, newName.c_str());

  json response;
  response["msgid"] = UPDATE_NAME_MSG;

  if (_userModel.updateName(userid, newName))
  {
    response["id"] = userid;
    response["name"] = newName;
  }
  else
  {
    response["errno"] = 1;
    response["errmsg"] = "Name already exists";
  }
  sendJson(conn, response);
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  if (!js.contains("id") || !js.contains("groupid"))
    return;
  int userid = js["id"].get<int>();
  int groupid = js["groupid"].get<int>();
  vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

  vector<int> offlineIds;
  vector<int> crossIds;

  {
    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
      auto it = _userConnMap.find(id);
      if (it != _userConnMap.end())
      {
        // 本服务器在线 → 直接转发
        sendJson(it->second, js);
      }
      else
      {
        // 暂存到离线列表，后续判断是跨服还是真离线
        offlineIds.push_back(id);
      }
    }
  }

  // 锁外处理：MySQL 查询 + Redis publish + 离线消息存储
  for (int id : offlineIds)
  {
    User user = _userModel.query(id);
    if (user.getState() == "online")
    {
      crossIds.push_back(id);
    }
    else
    {
      _offlineMsgModel.insert(id, js.dump());
    }
  }

  for (int id : crossIds)
  {
    _redis.publish(id, js.dump());
  }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
  LOGI("redis msg: userid=%d", userid);

  TcpConnectionPtr conn;
  {
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
      conn = it->second;
  }

  if (conn)
  {
    try
    {
      sendJson(conn, json::parse(msg));
    }
    catch (const json::exception &e)
    {
      LOGE("redis online msg parse error, userid=%d: %s", userid, e.what());
    }
    return;
  }

  // 好友状态通知为瞬态消息，离线时丢弃（登录后从 DB 查询当前状态即可）
  json parsed;
  try { parsed = json::parse(msg); } catch (...) {}
  if (parsed.is_object() && parsed.contains("msgid") && parsed["msgid"].get<int>() == FRIEND_STATE_CHANGE_MSG)
  {
    return;
  }

  // 存储该用户的离线消息
  _offlineMsgModel.insert(userid, msg);
}

// 通知好友上线/下线状态
void ChatService::notifyFriendState(int userid, const string &state)
{
  vector<User> friends = _friendModel.query(userid);
  json notify;
  notify["msgid"] = FRIEND_STATE_CHANGE_MSG;
  notify["friendid"] = userid;
  notify["state"] = state;

  vector<TcpConnectionPtr> onlineConns;
  vector<int> crossIds;
  {
    lock_guard<mutex> lock(_connMutex);
    for (User &friendUser : friends)
    {
      int fid = friendUser.getId();
      auto it = _userConnMap.find(fid);
      if (it != _userConnMap.end())
      {
        onlineConns.push_back(it->second);
      }
      else
      {
        crossIds.push_back(fid);
      }
    }
  }

  for (auto &conn : onlineConns)
    sendJson(conn, notify);

  // 通知跨服好友（发布到 Redis 通道，由对方服务器转发）
  for (int fid : crossIds)
  {
    _redis.publish(fid, notify.dump());
  }
}

void ChatService::sendJson(const TcpConnectionPtr &conn, const json &js)
{
  string body = js.dump();
  uint32_t len = htonl((uint32_t)body.size());
  string packet((char*)&len, 4);
  packet += body;
  conn->send(packet);
}

void ChatService::updateConnTime(const TcpConnectionPtr &conn)
{
  lock_guard<mutex> lock(_connMutex);
  _connectionLastTime[conn->name()] = time(NULL);
}

void ChatService::heartbeat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
  updateConnTime(conn);
  json response;
  response["msgid"] = HEARTBEAT_MSG;
  sendJson(conn, response);
}

void ChatService::startHeartbeatCheck(EventLoop *loop)
{
  _loop = loop;
  _heartbeatTimerId = loop->runEvery(15.0, std::bind(&ChatService::checkHeartbeat, this));
}

void ChatService::stopHeartbeatCheck()
{
  if (_loop)
    _loop->cancel(_heartbeatTimerId);
}

void ChatService::checkHeartbeat()
{
  vector<TcpConnectionPtr> toClose;

  {
    lock_guard<mutex> lock(_connMutex);
    time_t now = time(NULL);
    for (auto &kv : _connectionLastTime)
    {
      if (now - kv.second > 30)
      {
        for (auto &uc : _userConnMap)
        {
          if (uc.second->name() == kv.first)
          {
            toClose.push_back(uc.second);
            break;
          }
        }
      }
    }
  }

  for (auto &conn : toClose)
  {
    LOGE("heartbeat timeout, force close connection: %s", conn->name().c_str());
    conn->shutdown();
  }
}