#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include <string>
using namespace std;

// 数据库操作类
class MySQL
{
public:
  // 初始化数据库连接
  MySQL();
  // 释放数据库连接资源
  ~MySQL();
  // 连接数据库（使用 setConfig 设置的全局配置）
  bool connect();
  // 连接数据库（显式指定配置，连接池用）
  bool connect(const string &srv, const string &usr,
               const string &pwd, const string &db);
  // 更新操作
  bool update(string sql);
  // 查询操作
  MYSQL_RES *query(string sql);
  // 转义字符串防 SQL 注入
  string escape(const string &str) const;
  // 获取原始连接
  MYSQL *rawConn() { return _conn; }
  // 检测连接是否存活（失败时需重建连接）
  bool ping() { return mysql_ping(_conn) == 0; }

  // 设置数据库配置（启动时调用一次）
  static void setConfig(const string &srv, const string &usr,
                        const string &pwd, const string &db);

private:
  MYSQL *_conn;

  static string server_;
  static string user_;
  static string password_;
  static string dbname_;
};

#endif