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
  // 连接数据库
  bool connect();
  // 更新操作
  bool update(string sql);
  // 查询操作
  MYSQL_RES *query(string sql);
  // 获取连接
  MYSQL *getConnection();

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