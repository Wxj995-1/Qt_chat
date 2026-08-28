# Qt Chat

一个基于 C++ 的即时通信练习项目，由 Linux 服务端和 Qt 桌面客户端组成。服务端使用 Muduo 处理 TCP 连接，MySQL 保存业务数据，Redis Pub/Sub 支持多实例间消息转发；客户端通过 Qt Widgets 提供登录、好友和群聊界面。

## 功能

- 用户注册、登录、退出、切换账号和修改昵称
- 一对一聊天与离线消息
- 双向添加好友、好友在线状态通知
- 创建群组、加入群组和群聊
- 未读消息计数及当前会话内存历史（每个会话最多 500 条）
- 心跳检测、断线重连和异地重新登录
- 服务端异步日志、MySQL 连接池和 Redis 跨服务器转发
- TCP 粘包/拆包处理：4 字节网络字节序长度 + JSON 消息体

> 客户端聊天记录只保存在进程内，关闭程序后不会持久化；离线消息由服务端保存至 MySQL，并在用户登录后下发。

## 技术栈

| 部分 | 技术 |
| --- | --- |
| 服务端 | C++11、CMake、Muduo、nlohmann/json、MySQL、hiredis、pthread |
| 客户端 | C++11、Qt Widgets、Qt Network、qmake |
| 协议 | TCP、自定义长度头、JSON |

## 项目结构

```text
Qt_chat/
├── ChatServer/
│   ├── etc/chatserver.conf       # 服务端配置
│   ├── include/                  # 公共、业务、数据库及日志头文件
│   ├── src/server/               # 网络层与聊天业务
│   │   ├── db/                   # MySQL 连接及连接池
│   │   ├── model/                # 用户、好友、群组、离线消息模型
│   │   └── redis/                # Redis 发布/订阅
│   ├── src/asynclog/             # 异步日志
│   ├── thirdparty/json.hpp       # nlohmann/json 单头文件
│   └── autobuild.sh              # 服务端构建脚本
└── QtClient/Client/
    ├── Client.pro                # qmake 工程
    ├── chatclient.*              # 网络与协议处理
    ├── logindialog.*             # 登录/注册/服务器设置
    └── widget.*                  # 主聊天界面
```

## 环境要求

服务端代码使用 Linux API（如 `/proc/self/exe`、`poll`），应在 Linux 环境构建。需要准备：

- 支持 C++11 的 GCC/G++
- CMake 3.0+
- Muduo（`muduo_net`、`muduo_base`）
- MySQL Server 及 C 客户端开发库（`mysqlclient`）
- Redis Server 及 hiredis 开发库
- pthread

客户端可在 Windows、Linux 等 Qt 支持的平台构建，需要 Qt 5（代码使用了 Qt 5 风格的 socket error 信号）及带 C++11 支持的编译器。

以 Ubuntu/Debian 为例，MySQL、Redis 和 hiredis 可安装为：

```bash
sudo apt update
sudo apt install build-essential cmake mysql-server libmysqlclient-dev redis-server libhiredis-dev
```

Muduo 请按其项目说明安装，确保头文件及库位于编译器默认搜索路径，或自行在 `ChatServer/CMakeLists.txt` 中补充路径。

## 初始化数据库

创建数据库和项目所需的五张表：

```sql
CREATE DATABASE chat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chat;

CREATE TABLE User (
    id INT PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(50) NOT NULL UNIQUE,
    password VARCHAR(100) NOT NULL,
    state ENUM('online', 'offline') NOT NULL DEFAULT 'offline'
);

CREATE TABLE Friend (
    userid INT NOT NULL,
    friendid INT NOT NULL,
    PRIMARY KEY (userid, friendid),
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE,
    FOREIGN KEY (friendid) REFERENCES User(id) ON DELETE CASCADE
);

CREATE TABLE AllGroup (
    id INT PRIMARY KEY AUTO_INCREMENT,
    groupname VARCHAR(50) NOT NULL,
    groupdesc VARCHAR(200) DEFAULT ''
);

CREATE TABLE GroupUser (
    groupid INT NOT NULL,
    userid INT NOT NULL,
    grouprole ENUM('creator', 'normal') NOT NULL DEFAULT 'normal',
    PRIMARY KEY (groupid, userid),
    FOREIGN KEY (groupid) REFERENCES AllGroup(id) ON DELETE CASCADE,
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE
);

CREATE TABLE OfflineMessage (
    userid INT NOT NULL,
    message TEXT NOT NULL,
    INDEX idx_offline_userid (userid),
    FOREIGN KEY (userid) REFERENCES User(id) ON DELETE CASCADE
);
```

表名大小写应与上面保持一致；部分 Linux MySQL 配置会区分表名大小写。

## 配置服务端

编辑 `ChatServer/etc/chatserver.conf`：

```ini
serverip=127.0.0.1
serverport=8000

logfiledir=logs/
logfilename=chatserver

dbserver=127.0.0.1
dbuser=root
dbpassword=123456
dbname=chat

dbpoolsize=8
dbmaxpoolsize=16
```

- 实际监听地址由 `serverip` 和 `serverport` 控制；配置中的 `listenip`、`listenport` 当前未被代码使用。
- 命令行参数可临时覆盖监听地址：`ChatServer <ip> <port>`。
- Redis 地址目前在 `redis.cpp` 中固定为 `127.0.0.1:6379`。
- 配置文件由程序根据可执行文件位置读取，因此请保留 `bin/` 与 `etc/` 的相对目录关系。

不要把生产环境密码提交到仓库；部署时请修改示例凭据并限制配置文件权限。

## 构建与运行服务端

```bash
cd ChatServer
mkdir -p build
./autobuild.sh

# 确保 MySQL、Redis 已启动，然后运行
./bin/ChatServer
```

也可以手动构建：

```bash
cmake -S ChatServer -B ChatServer/build
cmake --build ChatServer/build -j
./ChatServer/bin/ChatServer
```

日志默认写入 `ChatServer/logs/`。使用 `Ctrl+C` 可触发服务端清理连接状态并安全退出。

## 构建与运行 Qt 客户端

使用 Qt Creator 打开 `QtClient/Client/Client.pro`，选择 Qt 5 Kit 后构建运行；或在已配置 Qt 环境的终端中执行：

```bash
cd QtClient/Client
qmake Client.pro
make                 # Windows MinGW 环境通常使用 mingw32-make
./Client             # Windows 下运行生成的 Client.exe
```

在登录窗口右上角的服务器设置中填写服务端 IP 和端口。该设置会通过 `QSettings` 保存。仓库当前客户端默认值为 `192.168.31.111:6000`，而服务端配置默认监听 `127.0.0.1:8000`，首次运行时必须将二者改为一致；跨机器连接时还需使用服务端可访问的网卡地址并放行对应 TCP 端口。

## 使用流程

1. 启动 MySQL 和 Redis。
2. 启动 `ChatServer`。
3. 启动一个或多个 Qt 客户端，并设置相同的服务端地址。
4. 注册用户；注册成功后会得到数字用户 ID。
5. 使用用户 ID 和密码登录，然后通过工具栏添加好友、创建群组或加入群组。

## 扩展多个服务端实例

服务端利用 Redis 频道转发不在本进程内的在线用户消息。可让多个实例连接同一 MySQL 与 Redis，并通过命令行监听不同端口：

```bash
./bin/ChatServer 0.0.0.0 8000
./bin/ChatServer 0.0.0.0 8001
```

生产部署通常还需在实例前增加 TCP 负载均衡器。当前 Redis 地址是硬编码值，跨主机部署前应先将其改为可配置项。

## 已知限制

- 密码以明文保存，项目不应直接用于生产环境。
- 添加好友和加入群组目前没有申请确认、权限校验或完整错误提示。
- Redis 连接失败不会阻止服务端启动，但跨实例消息转发将不可用。
- 项目尚未包含自动化测试、数据库迁移脚本和 Qt 部署打包配置。
- `ChatServer/src/client` 是旧的命令行客户端源码，当前 CMake 已禁用它，推荐使用 `QtClient`。

## 协议概览

每个 TCP 数据包由以下两部分组成：

```text
+----------------------+--------------------------+
| 4-byte body length   | compact JSON body        |
| big-endian uint32    | length bytes             |
+----------------------+--------------------------+
```

`msgid` 定义在 `ChatServer/include/public.hpp` 和客户端 `chatclient.h` 中，覆盖登录、注册、单聊、好友、群组、状态通知、改名及心跳等消息。服务端对单个 JSON 消息体设置了 1 MiB 上限。
