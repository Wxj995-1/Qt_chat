#ifndef POOLEDMYSQL_H
#define POOLEDMYSQL_H

#include "db.hpp"
#include <mysql/mysql.h>
#include <memory>
#include <string>
#include <ctime>

class DbPool;

// RAII 释放 MYSQL_RES，杜绝 result 泄漏
struct MySqlResDeleter
{
    void operator()(MYSQL_RES *res) const
    {
        if (res)
            mysql_free_result(res);
    }
};

using ResultPtr = std::unique_ptr<MYSQL_RES, MySqlResDeleter>;

// 连接池连接包装类，析构自动归还连接
class PooledMySQL
{
public:
    // 从池获取连接（超时返回 nullptr）
    static std::unique_ptr<PooledMySQL> acquire(int timeoutMs = 3000);

    // 显式归还（可重复调用，幂等）
    void release();

    // 转发 MySQL 操作
    bool update(const std::string &sql);
    ResultPtr query(const std::string &sql);
    std::string escape(const std::string &str);
    MYSQL *rawConn();

    // 析构自动归还（兜底）
    ~PooledMySQL();

    // 禁拷贝，强制一对一所有权
    PooledMySQL(const PooledMySQL &) = delete;
    PooledMySQL &operator=(const PooledMySQL &) = delete;

private:
    PooledMySQL(std::shared_ptr<MySQL> conn, DbPool *pool, time_t acquireTime);

    std::shared_ptr<MySQL> _conn;
    DbPool *_pool;
    time_t _acquireTime;    // 记录借出时间，用于超时告警
    bool _released = false;

    friend class DbPool;
};

#endif