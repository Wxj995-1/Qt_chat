#include "offlinemessagemodel.hpp"
#include "pooledmysql.hpp"
#include <cstdio>

// 存储用户的离线消息
void OfflineMsgModel::insert(int userid, string msg)
{
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return;

    string escaped = mysql->escape(msg);
    char sql[2048] = {0};
    snprintf(sql, sizeof(sql), "insert into OfflineMessage values(%d, '%s')",
             userid, escaped.c_str());

    mysql->update(sql);
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from OfflineMessage where userid=%d", userid);

    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return;

    mysql->update(sql);
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select message from OfflineMessage where userid = %d", userid);

    vector<string> vec;
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return vec;

    auto res = mysql->query(sql);
    if (!res)
        return vec;

    // 把userid用户的所有离线消息放入vec中返回
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res.get())) != nullptr)
    {
        vec.push_back(row[0]);
    }
    return vec;
}