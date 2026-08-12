#include "groupmodel.hpp"
#include "pooledmysql.hpp"
#include <cstdio>
#include <cstdlib>

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return false;

    string name = mysql->escape(group.getName());
    string desc = mysql->escape(group.getDesc());

    char sql[1024] = {0};
    sprintf(sql, "insert into AllGroup(groupname, groupdesc) values('%s', '%s')",
            name.c_str(), desc.c_str());

    if (mysql->update(sql))
    {
        group.setId(mysql_insert_id(mysql->rawConn()));
        return true;
    }

    return false;
}

// 加入群组
void GroupModel::addGroup(int userid, int groupid, string role)
{
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return;

    string escapedRole = mysql->escape(role);
    char sql[1024] = {0};
    sprintf(sql, "insert into GroupUser values(%d, %d, '%s')",
            groupid, userid, escapedRole.c_str());

    mysql->update(sql);
}

// 查询用户所在群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    /*
    1. 先根据userid在groupuser表中查询出该用户所属的群组信息
    2. 在根据群组信息，查询属于该群组的所有用户的userid，并且和user表进行多表联合查询，查出用户的详细信息
    */
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.groupname,a.groupdesc from AllGroup a inner join \
         GroupUser b on a.id = b.groupid where b.userid=%d",
            userid);

    vector<Group> groupVec;

    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return groupVec;

    auto res = mysql->query(sql);
    if (!res)
        return groupVec;

    MYSQL_ROW row;
    // 查出userid所有的群组信息
    while ((row = mysql_fetch_row(res.get())) != nullptr)
    {
        Group group;
        group.setId(atoi(row[0]));
        group.setName(row[1]);
        group.setDesc(row[2]);
        groupVec.push_back(group);
    }
    // res 自动 free

    // 查询群组的用户信息（复用同一连接，避免 N 次 acquire/release）
    for (Group &group : groupVec)
    {
        sprintf(sql, "select a.id,a.name,a.state,b.grouprole from User a \
            inner join GroupUser b on b.userid = a.id where b.groupid=%d",
                group.getId());

        auto res2 = mysql->query(sql);
        if (!res2)
            continue;

        MYSQL_ROW row2;
        while ((row2 = mysql_fetch_row(res2.get())) != nullptr)
        {
            GroupUser user;
            user.setId(atoi(row2[0]));
            user.setName(row2[1]);
            user.setState(row2[2]);
            user.setRole(row2[3]);
            group.getUsers().push_back(user);
        }
    }
    return groupVec;
    // mysql 自动归还
}

// 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其它成员群发消息
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from GroupUser where groupid = %d and userid != %d", groupid, userid);

    vector<int> idVec;
    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return idVec;

    auto res = mysql->query(sql);
    if (!res)
        return idVec;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res.get())) != nullptr)
    {
        idVec.push_back(atoi(row[0]));
    }
    return idVec;
}

// 根据groupid查询群组信息
Group GroupModel::queryGroup(int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select id, groupname, groupdesc from AllGroup where id=%d", groupid);

    auto mysql = PooledMySQL::acquire();
    if (!mysql)
        return Group();

    auto res = mysql->query(sql);
    if (!res)
        return Group();

    MYSQL_ROW row = mysql_fetch_row(res.get());
    if (row != nullptr)
    {
        Group group;
        group.setId(atoi(row[0]));
        group.setName(row[1]);
        group.setDesc(row[2]);
        return group;
    }
    return Group();
}