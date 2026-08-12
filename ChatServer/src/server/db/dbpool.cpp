#include "dbpool.hpp"
#include "pooledmysql.hpp"
#include "AsyncLog.hpp"
#include <chrono>
#include <utility>

// ============================ DbPool ============================

DbPool::DbPool() : _poolSize(8), _maxPoolSize(16)
{
}

DbPool::~DbPool()
{
    shutdown();
}

DbPool &DbPool::instance()
{
    static DbPool pool;
    return pool;
}

// 创建新连接（使用 init 传入的配置）
std::shared_ptr<MySQL> DbPool::createConnection()
{
    std::shared_ptr<MySQL> conn = std::make_shared<MySQL>();
    if (conn->connect(_host, _user, _pwd, _db))
        return conn;

    LOGE("createConnection failed, host=%s db=%s", _host.c_str(), _db.c_str());
    return nullptr;
}

// 启动时初始化：预创建 poolSize 条连接
bool DbPool::init(const std::string &host, const std::string &user,
                  const std::string &pwd, const std::string &db,
                  int poolSize, int maxPoolSize)
{
    if (poolSize <= 0 || maxPoolSize < poolSize)
    {
        LOGE("DbPool init: invalid poolSize=%d maxPoolSize=%d", poolSize, maxPoolSize);
        return false;
    }

    _host = host;
    _user = user;
    _pwd = pwd;
    _db = db;
    _poolSize = poolSize;
    _maxPoolSize = maxPoolSize;
    _shutdown = false;

    std::lock_guard<std::mutex> lock(_mtx);
    for (int i = 0; i < _poolSize; ++i)
    {
        auto conn = createConnection();
        if (conn)
            _idleConns.push({conn, time(nullptr)});
        else
            LOGE("init: failed to create connection %d/%d", i + 1, _poolSize);
    }

    if (_idleConns.empty())
    {
        LOGF("DbPool init: no connection available");
        return false;
    }

    LOGI("DbPool init: %zu connections ready", _idleConns.size());
    return true;
}

// 退出清理：断开所有空闲连接，剩余借出连接随所有权释放
void DbPool::shutdown()
{
    std::lock_guard<std::mutex> lock(_mtx);
    if (_shutdown)
        return;
    _shutdown = true;
    _idleConns = std::queue<ConnEntry>();
    _borrowedConns.clear();
    _cv.notify_all();
}

// 归还连接 + 移除借出记录
void DbPool::returnConnection(std::shared_ptr<MySQL> conn, MYSQL *raw)
{
    std::lock_guard<std::mutex> lock(_mtx);
    _borrowedConns.erase(raw);
    if (_shutdown)
        return;

    _idleConns.push({conn, time(nullptr)});
    _activeCount--;
    _cv.notify_one();
}

// 泄漏扫描（muduo runEvery 10s 调用）
void DbPool::checkStaleConnections()
{
    std::lock_guard<std::mutex> lock(_mtx);
    time_t now = time(nullptr);
    for (auto it = _borrowedConns.begin(); it != _borrowedConns.end(); ++it)
    {
        time_t holdTime = now - it->second;
        if (holdTime > LEAK_ERROR_THRESHOLD)
            LOGE("conn %p held %lds, likely leaked", it->first, holdTime);
        else if (holdTime > LEAK_WARN_THRESHOLD)
            LOGW("conn %p held %lds, possible slow query", it->first, holdTime);
    }
}

// ============================ PooledMySQL ============================

PooledMySQL::PooledMySQL(std::shared_ptr<MySQL> conn, DbPool *pool, time_t acquireTime)
    : _conn(std::move(conn)), _pool(pool), _acquireTime(acquireTime)
{
}

// 从池获取连接（while 循环 + 剩余超时）
std::unique_ptr<PooledMySQL> PooledMySQL::acquire(int timeoutMs)
{
    auto &pool = DbPool::instance();
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(pool._mtx);
    while (true)
    {
        // 1. 有空闲连接
        if (!pool._idleConns.empty())
        {
            auto entry = pool._idleConns.front();
            pool._idleConns.pop();
            pool._activeCount++;

            // 空闲超阈值才 ping，避免频繁探活开销
            if (time(nullptr) - entry.lastUsedTime > DbPool::IDLE_PING_THRESHOLD)
            {
                if (mysql_ping(entry.conn->rawConn()) != 0)
                {
                    LOGE("ping failed, recreating connection");
                    auto newConn = pool.createConnection();
                    if (newConn)
                    {
                        pool._borrowedConns[newConn->rawConn()] = time(nullptr);
                        return std::unique_ptr<PooledMySQL>(new PooledMySQL(newConn, &pool, time(nullptr)));
                    }
                    pool._activeCount--;
                    continue; // 重建失败，重试
                }
            }

            pool._borrowedConns[entry.conn->rawConn()] = time(nullptr);
            return std::unique_ptr<PooledMySQL>(new PooledMySQL(entry.conn, &pool, time(nullptr)));
        }

        // 2. 未达上限，扩容（锁外建连，避免阻塞持锁）
        if (pool._activeCount < pool._maxPoolSize)
        {
            pool._activeCount++;
            lock.unlock();
            auto conn = pool.createConnection();
            lock.lock();
            if (conn)
            {
                pool._borrowedConns[conn->rawConn()] = time(nullptr);
                return std::unique_ptr<PooledMySQL>(new PooledMySQL(conn, &pool, time(nullptr)));
            }
            pool._activeCount--;
            continue;
        }

        // 3. 达上限，等待（带剩余超时）
        if (pool._cv.wait_until(lock, deadline) == std::cv_status::timeout)
        {
            LOGE("acquire timeout, active=%d", pool._activeCount);
            return nullptr;
        }
        // 被唤醒，循环重试
    }
}

// 显式归还（持有时长告警）
void PooledMySQL::release()
{
    if (_released)
        return;
    _released = true;

    time_t holdTime = time(nullptr) - _acquireTime;
    if (holdTime > DbPool::LEAK_ERROR_THRESHOLD)
        LOGE("PooledMySQL held %lds, likely leaked", holdTime);
    else if (holdTime > DbPool::LEAK_WARN_THRESHOLD)
        LOGW("PooledMySQL held %lds, possible slow query", holdTime);

    _pool->returnConnection(_conn, _conn->rawConn());
}

// 析构自动归还（兜底）
PooledMySQL::~PooledMySQL()
{
    if (!_released)
        release();
}

// 更新操作
bool PooledMySQL::update(const std::string &sql)
{
    return _conn->update(sql);
}

// 查询操作，返回 RAII 管理的 result
ResultPtr PooledMySQL::query(const std::string &sql)
{
    if (mysql_query(_conn->rawConn(), sql.c_str()) != 0)
    {
        LOGE("query failed: %s, error: %s", sql.c_str(), mysql_error(_conn->rawConn()));
        return ResultPtr(nullptr, MySqlResDeleter{});
    }
    MYSQL_RES *res = mysql_store_result(_conn->rawConn());
    return ResultPtr(res, MySqlResDeleter{});
}

// 转义字符串防 SQL 注入
std::string PooledMySQL::escape(const std::string &str)
{
    return _conn->escape(str);
}

// 获取原始连接
MYSQL *PooledMySQL::rawConn()
{
    return _conn->rawConn();
}