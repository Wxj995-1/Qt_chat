#include "usermodel.hpp"
#include "pooledmysql.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
using namespace std;

// User表的增加方法
bool UserModel::insert(User &user)
{
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return false;

    string name = mysql->escape(user.getName());
    string pwd = mysql->escape(user.getPwd());
    string state = mysql->escape(user.getState());

    char sql[1024] = {0};
    sprintf(sql, "insert into User(name, password, state) values('%s', '%s', '%s')",
            name.c_str(), pwd.c_str(), state.c_str());

    if (mysql->update(sql))
    {
        // 获取插入成功的用户数据生成的主键id
        user.setId(mysql_insert_id(mysql->rawConn()));
        return true;
    }

    return false;
}

// 根据用户号码查询用户信息
User UserModel::query(int id)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from User where id = %d", id);

    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return User();

    auto res = mysql->query(sql);
    if (!res)
        return User();

    MYSQL_ROW row = mysql_fetch_row(res.get());
    if (row != nullptr)
    {
        User user;
        user.setId(atoi(row[0]));
        user.setName(row[1]);
        user.setPwd(row[2]);
        user.setState(row[3]);
        return user;
    }

    return User();
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return false;

    string state = mysql->escape(user.getState());
    char sql[1024] = {0};
    sprintf(sql, "update User set state = '%s' where id = %d", state.c_str(), user.getId());

    return mysql->update(sql);
}

bool UserModel::updateName(int id, const string &name)
{
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return false;

    string escaped = mysql->escape(name);
    char sql[1024] = {0};
    sprintf(sql, "update User set name = '%s' where id = %d", escaped.c_str(), id);

    return mysql->update(sql);
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 1.组装sql语句
    char sql[1024] = "update User set state = 'offline' where state = 'online'";

    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return;

    mysql->update(sql);
}