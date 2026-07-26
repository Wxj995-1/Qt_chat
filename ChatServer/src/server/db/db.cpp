#include "db.hpp"
/*
#include <muduo/base/Logging.h>
*/
#include "AsyncLog.hpp"

// 数据库配置信息（默认空，启动时由 setConfig 从配置文件读取）
string MySQL::server_;
string MySQL::user_;
string MySQL::password_;
string MySQL::dbname_;

// 初始化数据库连接
MySQL::MySQL()
{
    _conn = mysql_init(nullptr);
}

// 释放数据库连接资源
MySQL::~MySQL()
{
    if (_conn != nullptr)
        mysql_close(_conn);
}

// 设置数据库配置
void MySQL::setConfig(const string &srv, const string &usr,
                      const string &pwd, const string &db)
{
    server_ = srv;
    user_ = usr;
    password_ = pwd;
    dbname_ = db;
}

// 连接数据库
bool MySQL::connect()
{
    MYSQL *p = mysql_real_connect(_conn, server_.c_str(), user_.c_str(),
                                  password_.c_str(), dbname_.c_str(), 3306, nullptr, 0);
    if (p != nullptr)
    {
        // C和C++代码默认的编码字符是ASCII，如果不设置，从MySQL上拉下来的中文显示？
        mysql_query(_conn, "set names utf8mb4");
        // LOG_INFO << "connect mysql success!";
        LOGI("connect mysql success!");
    }
    else
    {
        // LOG_INFO << "connect mysql fail!";
        LOGI("connect mysql fail!");
    }

    return p;
}

// 更新操作
bool MySQL::update(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        // LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "更新失败!";
        LOGE("%s:%d:%s 更新失败! error: %s", __FILE__, __LINE__, sql.c_str(), mysql_error(_conn));
        return false;
    }

    return true;
}

// 查询操作
MYSQL_RES *MySQL::query(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        // LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "查询失败!";
        LOGE("%s:%d:%s 查询失败! %s", __FILE__, __LINE__, sql.c_str(), mysql_error(_conn));
        return nullptr;
    }

    return mysql_use_result(_conn);
}

// 获取连接
MYSQL *MySQL::getConnection()
{
    return _conn;
}