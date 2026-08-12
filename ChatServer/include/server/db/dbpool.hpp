#ifndef DBPOOL_H
#define DBPOOL_H

#include "db.hpp"
#include <mysql/mysql.h>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <string>
#include <ctime>

class PooledMySQL;

// MySQL 连接池（单例），替代每业务一次 connect/close
class DbPool
{
public:
    static DbPool &instance();

    // 启动时初始化（替代 MySQL::setConfig），失败返回 false
    bool init(const std::string &host, const std::string &user,
              const std::string &pwd, const std::string &db,
              int poolSize = 8, int maxPoolSize = 16);

    // 退出清理
    void shutdown();

    // PooledMySQL::release 内部调用：归还连接 + 移除借出记录
    void returnConnection(std::shared_ptr<MySQL> conn, MYSQL *raw);

    // 泄漏扫描（muduo runEvery 调用）
    void checkStaleConnections();

    // 创建新连接
    std::shared_ptr<MySQL> createConnection();

    // 常量（public 供 PooledMySQL 访问）
    static constexpr int IDLE_PING_THRESHOLD = 30; // 空闲 30s 才 ping
    static constexpr int LEAK_WARN_THRESHOLD = 1;  // 持有 >1s 告警
    static constexpr int LEAK_ERROR_THRESHOLD = 5; // 持有 >5s 报错

private:
    DbPool();
    ~DbPool();
    DbPool(const DbPool &) = delete;
    DbPool &operator=(const DbPool &) = delete;

    struct ConnEntry
    {
        std::shared_ptr<MySQL> conn;
        time_t lastUsedTime;
    };

    std::queue<ConnEntry> _idleConns;                  // 空闲队列
    std::unordered_map<MYSQL *, time_t> _borrowedConns; // 借出记录（泄漏扫描用）
    int _activeCount = 0;

    std::mutex _mtx;
    std::condition_variable _cv;

    std::string _host, _user, _pwd, _db;
    int _poolSize;
    int _maxPoolSize;
    std::atomic<bool> _shutdown{false};

    friend class PooledMySQL;
};

#endif