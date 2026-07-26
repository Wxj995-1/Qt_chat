#include "offlinemessagemodel.hpp"
#include "db.hpp"
#include <cstdio>
#include <vector>

// 存储用户的离线消息
void OfflineMsgModel::insert(int userid, string msg)
{
    MySQL mysql;
    if (!mysql.connect())
        return;

    size_t escapedLen = msg.size() * 2 + 1;
    vector<char> escaped(escapedLen);
    mysql_real_escape_string(mysql.getConnection(), escaped.data(), msg.c_str(), msg.size());

    size_t sqlLen = msg.size() * 2 + 120;
    vector<char> sql(sqlLen, 0);
    snprintf(sql.data(), sqlLen, "insert into OfflineMessage values(%d, '%s')", userid, escaped.data());

    mysql.update(string(sql.data()));
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from OfflineMessage where userid=%d", userid);

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select message from OfflineMessage where userid = %d", userid);

    vector<string> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            // 把userid用户的所有离线消息放入vec中返回
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}