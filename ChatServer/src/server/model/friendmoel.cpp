#include "friendmodel.hpp"
#include "pooledmysql.hpp"
#include <cstdio>
#include <cstdlib>

// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert ignore into Friend values(%d, %d)", userid, friendid);

    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return;

    mysql->update(sql);
}

// 返回用户好友列表
vector<User> FriendModel::query(int userid)
{
    // 1.组装sql语句
    char sql[1024] = {0};

    sprintf(sql, "select a.id,a.name,a.state from User a inner join Friend b on b.friendid = a.id where b.userid=%d", userid);

    vector<User> vec;
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return vec;

    auto res = mysql->query(sql);
    if (!res)
        return vec;

    // 把userid用户的所有好友放入vec中返回
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res.get())) != nullptr)
    {
        User user;
        user.setId(atoi(row[0]));
        user.setName(row[1]);
        user.setState(row[2]);
        vec.push_back(user);
    }
    return vec;
}