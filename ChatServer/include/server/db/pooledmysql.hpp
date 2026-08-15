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
    time_t _acquireTime; // 记录借出时间，用于超时告警
    bool _released = false;

    friend class DbPool;
};

#endif

/*

2.1 启动流程（main.cpp）
main()
  │
  ├─ DbPool::instance().init(host,user,pwd,db, 8, 16)
  │   │
  │   ├─ 参数校验 (poolSize>0, maxPoolSize>=poolSize)
  │   ├─ 保存配置 (_host/_user/_pwd/_db/_poolSize/_maxPoolSize)
  │   ├─ _shutdown = false
  │   ├─ 加锁 _mtx
  │   └─ 循环 poolSize 次:
  │       ├─ createConnection()
  │       │   ├─ make_shared<MySQL>()     → mysql_init
  │       │   ├─ conn->connect(host,...)  → mysql_real_connect + set names utf8mb4
  │       │   └─ 返回 shared_ptr<MySQL> (失败返回 nullptr)
  │       │
  │       └─ _idleConns.push({conn, time(nullptr)})  ← 入空闲队列
  │
  ├─ loop.runEvery(10.0, checkStaleConnections)  ← 定时扫描
  │
  └─ ... (服务运行)
2.2 获取连接流程（PooledMySQL::acquire）
Model 层调用: auto mysql = PooledMySQL::acquire();
  │
  ├─ 获取 DbPool 单例引用
  ├─ 计算 deadline = now + 3000ms
  ├─ unique_lock 加锁 _mtx
  │
  └─ while (true)
      │
      ├─【分支1】_idleConns 非空?
      │   ├─ 取队头 entry = front()
      │   ├─ pop() 弹出队列
      │   ├─ _activeCount++
      │   │
      │   ├─ 空闲 > 30s? (now - lastUsedTime > 30)
      │   │   ├─ 否 → 记录借出, 返回 PooledMySQL ✓
      │   │   └─ 是 → mysql_ping(rawConn)
      │   │       ├─ 成功(==0) → 记录借出, 返回 PooledMySQL ✓
      │   │       └─ 失败(!=0) → createConnection() 重建
      │   │           ├─ 成功 → 记录借出, 返回新 PooledMySQL ✓
      │   │           └─ 失败 → _activeCount--, continue 重试
      │   │
      │   └─ 记录借出: _borrowedConns[rawConn] = now
      │      返回 unique_ptr<PooledMySQL>(new PooledMySQL(conn, &pool, now))
      │
      ├─【分支2】_activeCount < _maxPoolSize?
      │   ├─ _activeCount++ (先占位)
      │   ├─ lock.unlock() (锁外建连)
      │   ├─ createConnection()
      │   ├─ lock.lock()
      │   ├─ 成功 → 记录借出, 返回 PooledMySQL ✓
      │   └─ 失败 → _activeCount--, continue 重试
      │
      └─【分支3】达上限, 等待
          └─ _cv.wait_until(lock, deadline)
              ├─ 超时 → 返回 nullptr (业务降级)
              └─ 唤醒 → 循环重试 (回分支1)
2.3 业务使用流程
auto mysql = PooledMySQL::acquire();
  │
  ├─ mysql->query(sql)
  │   └─ mysql_query(_conn->rawConn(), sql)
  │   └─ mysql_store_result(_conn->rawConn())
  │   └─ 返回 ResultPtr(res, MySqlResDeleter{})  ← RAII 管理
  │
  ├─ mysql->update(sql)
  │   └─ _conn->update(sql)  → mysql_query
  │
  ├─ mysql->escape(str)
  │   └─ _conn->escape(str)  → mysql_real_escape_string
  │
  └─ mysql->rawConn()
      └─ _conn->rawConn()  → 返回 MYSQL* (供 mysql_insert_id 等)

出作用域 (或显式 release())
  │
  ├─ ~PooledMySQL()
  │   └─ if (!_released) release()
  │
  └─ release()
      ├─ if (_released) return        ← 幂等检查
      ├─ _released = true
      │
      ├─ holdTime = now - _acquireTime   ← 计算持有时长
      ├─ if (holdTime > 5) LOGE(...)      ← 泄漏报错
      ├─ if (holdTime > 1) LOGW(...)      ← 慢查询告警
      │
      └─ _pool->returnConnection(_conn, rawConn)
          │
          ├─ 加锁 _mtx
          ├─ _borrowedConns.erase(raw)    ← 移除借出记录
          ├─ if (_shutdown) return        ← 关闭中不归还
          ├─ _idleConns.push({conn, now}) ← 放回空闲队列
          ├─ _activeCount--
          └─ _cv.notify_one()            ← 唤醒等待者

2.5 泄漏扫描流程（checkStaleConnections）
main.cpp: loop.runEvery(10.0, ...)
  │
  └─ DbPool::instance().checkStaleConnections()
      ├─ 加锁 _mtx
      ├─ now = time(nullptr)
      └─ 遍历 _borrowedConns:
          ├─ holdTime = now - borrowTime
          ├─ if (holdTime > 5) LOGE("conn %p held %lds, likely leaked")
          └─ if (holdTime > 1) LOGW("conn %p held %lds, possible slow query")
2.6 退出流程（main.cpp）
main() 退出阶段:
  │
  ├─ ChatService::instance()->reset()
  │
  ├─ DbPool::instance().shutdown()
  │   ├─ 加锁 _mtx
  │   ├─ _shutdown = true
  │   ├─ _idleConns = 空 queue     ← 空闲连接 shared_ptr 引用计数→0
  │   │                              → ~MySQL() → mysql_close (自动关闭)
  │   ├─ _borrowedConns.clear()
  │   └─ _cv.notify_all()         ← 唤醒所有等待者 (返回 nullptr)
  │
  └─ 借出连接的后续 (异步):
      PooledMySQL 出作用域
        → ~PooledMySQL() → release()
          → returnConnection()
            → if (_shutdown) return  ← 不入队列
          → _conn 析构 (shared_ptr 引用计数→0)
            → ~MySQL() → mysql_close  ← 自动关闭

PooledMySQL 出作用域
    │
    ├─ ① ~PooledMySQL()           [PooledMySQL 的析构函数]
    │   └─ 调用 release()
    │
    ├─ ② release()                [PooledMySQL 的归还方法]
    │   ├─ 幂等检查 (_released)
    │   ├─ 持有时长告警
    │   └─ 调用 _pool->returnConnection()
    │
    ├─ ③ returnConnection()       [DbPool 的归还方法]
    │   ├─ 加锁
    │   ├─ 移除借出记录
    │   ├─ if (_shutdown) return   ← 关闭中，不入队列
    │   ├─ _idleConns.push(...)    ← 正常归还
    │   ├─ _activeCount--
    │   └─ _cv.notify_one()
    │
    ├─ ④ ~PooledMySQL() 继续       [成员析构阶段]
    │   └─ _conn 析构 (shared_ptr<MySQL>)
    │
    ├─ ⑤ shared_ptr 析构           [引用计数机制]
    │   ├─ 引用计数 -1
    │   └─ if (引用计数 == 0) → 调用 ~MySQL()
    │
    └─ ⑥ ~MySQL()                 [MySQL 的析构函数]
        └─ mysql_close(_conn)      ← 真正关闭 TCP 连接
*/