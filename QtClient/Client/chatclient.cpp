#include "chatclient.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QtEndian>
#include <QDebug>
#include <QNetworkProxy>

ChatClient::ChatClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_loginTimeoutTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_heartbeatTimeout(new QTimer(this))
    , m_lastDataTime(time(NULL))
{
    m_socket->setProxy(QNetworkProxy::NoProxy);
    m_reconnectTimer->setInterval(2000);
    m_reconnectTimer->setSingleShot(false);
    m_loginTimeoutTimer->setInterval(5000);
    m_loginTimeoutTimer->setSingleShot(true);

    m_heartbeatTimer->setInterval(15000);
    m_heartbeatTimer->setSingleShot(false);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ChatClient::sendHeartbeat);

    m_heartbeatTimeout->setInterval(35000);
    m_heartbeatTimeout->setSingleShot(true);
    connect(m_heartbeatTimeout, &QTimer::timeout, this, &ChatClient::onHeartbeatTimeout);

    connect(m_socket, &QTcpSocket::connected, this, &ChatClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ChatClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ChatClient::onDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &ChatClient::onError);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ChatClient::onReconnectTimeout);
    connect(m_loginTimeoutTimer, &QTimer::timeout, this, &ChatClient::onLoginTimeout);

    QSettings settings;
    m_serverIp = settings.value("server/ip", "192.168.31.111").toString();
    m_serverPort = settings.value("server/port", 6000).toUInt();
}

void ChatClient::setServerIp(const QString &ip)
{
    m_serverIp = ip;
    QSettings settings;
    settings.setValue("server/ip", ip);
}

void ChatClient::setServerPort(quint16 port)
{
    m_serverPort = port;
    QSettings settings;
    settings.setValue("server/port", port);
}

void ChatClient::connectToServer(const QString &host, quint16 port)
{
    qDebug() << "[ChatClient] connectToServer" << host << port;
    m_intentionalDisconnect = false;
    m_host = host;
    m_port = port;
    m_socket->abort();
    m_socket->connectToHost(host, port);
}

bool ChatClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void ChatClient::sendJson(const QJsonObject &obj)
{
    m_lastDataTime = time(NULL);
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    uint32_t len = qToBigEndian<uint32_t>(body.size());
    QByteArray packet;
    packet.append((const char*)&len, 4);
    packet.append(body);
    m_socket->write(packet);
    m_socket->flush();
}

void ChatClient::login(int id, const QString &pwd)
{
    qDebug() << "[ChatClient] login - id:" << id;
    m_myId = id;
    m_password = pwd;
    m_wantRelogin = true;
    QJsonObject obj;
    obj["msgid"] = LOGIN_MSG;
    obj["id"] = id;
    obj["password"] = pwd;
    sendJson(obj);
    m_loginTimeoutTimer->start();
}

void ChatClient::reg(const QString &name, const QString &pwd)
{
    QJsonObject obj;
    obj["msgid"] = REG_MSG;
    obj["name"] = name;
    obj["password"] = pwd;
    sendJson(obj);
}

void ChatClient::oneChat(int toid, const QString &msg)
{
    QJsonObject obj;
    obj["msgid"] = ONE_CHAT_MSG;
    obj["id"] = m_myId;
    obj["name"] = m_myName;
    obj["toid"] = toid;
    obj["msg"] = msg;
    obj["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    sendJson(obj);
}

void ChatClient::addFriend(int friendid)
{
    QJsonObject obj;
    obj["msgid"] = ADD_FRIEND_MSG;
    obj["id"] = m_myId;
    obj["friendid"] = friendid;
    sendJson(obj);
}

void ChatClient::createGroup(const QString &name, const QString &desc)
{
    QJsonObject obj;
    obj["msgid"] = CREATE_GROUP_MSG;
    obj["id"] = m_myId;
    obj["groupname"] = name;
    obj["groupdesc"] = desc;
    sendJson(obj);
}

void ChatClient::addGroup(int groupid)
{
    QJsonObject obj;
    obj["msgid"] = ADD_GROUP_MSG;
    obj["id"] = m_myId;
    obj["groupid"] = groupid;
    sendJson(obj);
}

void ChatClient::groupChat(int groupid, const QString &msg)
{
    QJsonObject obj;
    obj["msgid"] = GROUP_CHAT_MSG;
    obj["id"] = m_myId;
    obj["name"] = m_myName;
    obj["groupid"] = groupid;
    obj["msg"] = msg;
    obj["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    sendJson(obj);
}

void ChatClient::updateName(int id, const QString &newName)
{
    QJsonObject obj;
    obj["msgid"] = UPDATE_NAME_MSG;
    obj["id"] = id;
    obj["name"] = newName;
    sendJson(obj);
}

void ChatClient::loginout()
{
    m_intentionalDisconnect = true;
    m_wantRelogin = false;
    m_reconnectTimer->stop();
    m_loginTimeoutTimer->stop();
    m_heartbeatTimer->stop();
    m_heartbeatTimeout->stop();
    QJsonObject obj;
    obj["msgid"] = LOGINOUT_MSG;
    obj["id"] = m_myId;
    sendJson(obj);
    m_recvBuf.clear();
    m_socket->disconnectFromHost();
}

void ChatClient::resetLoginState()
{
    m_myId = -1;
    m_password.clear();
    m_wantRelogin = false;
}

void ChatClient::sendHeartbeat()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    if (time(NULL) - m_lastDataTime < 15)
        return;

    QJsonObject obj;
    obj["msgid"] = HEARTBEAT_MSG;
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    uint32_t len = qToBigEndian<uint32_t>(body.size());
    QByteArray packet;
    packet.append((const char*)&len, 4);
    packet.append(body);
    m_socket->write(packet);
    m_socket->flush();

    m_heartbeatTimeout->start();
}

void ChatClient::onHeartbeatTimeout()
{
    qDebug() << "[ChatClient] heartbeat timeout, reconnecting...";
    m_socket->abort();
    m_recvBuf.clear();
    if (!m_intentionalDisconnect)
    {
        m_reconnectTimer->start();
        emit connectionError("Server heartbeat timeout, reconnecting...");
    }
}

void ChatClient::relogin()
{
    if (m_myId > 0 && !m_password.isEmpty())
    {
        m_intentionalDisconnect = false;
        m_wantRelogin = true;
        m_socket->abort();
        connectToServer(m_host, m_port);
    }
}

void ChatClient::onConnected()
{
    qDebug() << "[ChatClient] onConnected - m_myId:" << m_myId << "m_password.isEmpty():" << m_password.isEmpty();
    m_reconnectTimer->stop();
    m_intentionalDisconnect = false;
    if (m_wantRelogin)
    {
        qDebug() << "[ChatClient] auto-relogin after reconnect";
        login(m_myId, m_password);
    }
    else
    {
        qDebug() << "[ChatClient] first connection, emit connected signal";
        emit connected();
    }
}

void ChatClient::onReadyRead()
{
    m_recvBuf.append(m_socket->readAll());
    m_lastDataTime = time(NULL);

    while (m_recvBuf.size() >= 4)
    {
        uint32_t len = qFromBigEndian<uint32_t>(
            *reinterpret_cast<const uint32_t*>(m_recvBuf.constData()));
        if (len == 0 || len > 1024 * 1024) {
            m_recvBuf.clear();
            break;
        }
        if (m_recvBuf.size() < 4 + len)
            break;

        QByteArray jsonBytes = m_recvBuf.mid(4, len);
        m_recvBuf.remove(0, 4 + len);

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "[ChatClient] JSON parse error:" << err.errorString();
            continue;
        }

        QJsonObject obj = doc.object();
        qDebug() << "[ChatClient] received msgid:" << obj["msgid"].toInt();
        handleMessage(obj);
    }
}

void ChatClient::onDisconnected()
{
    qDebug() << "[ChatClient] onDisconnected - intentional:" << m_intentionalDisconnect << "timer active:" << m_reconnectTimer->isActive();
    m_heartbeatTimer->stop();
    m_heartbeatTimeout->stop();
    m_recvBuf.clear();
    emit disconnected();
    if (!m_intentionalDisconnect && !m_reconnectTimer->isActive())
    {
        m_reconnectTimer->start();
    }
}

void ChatClient::onError(QAbstractSocket::SocketError error)
{
    qDebug() << "[ChatClient] onError - error:" << error << m_socket->errorString() << "timer active:" << m_reconnectTimer->isActive();
    if (m_intentionalDisconnect)
    {
        qDebug() << "[ChatClient] onError ignored - intentional disconnect";
        return;
    }
    emit connectionError(m_socket->errorString());
    if (!m_reconnectTimer->isActive())
        m_reconnectTimer->start();
}

void ChatClient::onReconnectTimeout()
{
    qDebug() << "[ChatClient] onReconnectTimeout - socket state:" << m_socket->state();
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        return;
    m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void ChatClient::onLoginTimeout()
{
    qDebug() << "[ChatClient] onLoginTimeout";
    m_loginTimeoutTimer->stop();
    m_socket->abort();
    m_recvBuf.clear();
    emit connectionError("Server not responding, will retry...");
}

void ChatClient::handleMessage(const QJsonObject &js)
{
    int msgid = js["msgid"].toInt();

    switch (msgid)
    {
    case LOGIN_MSG_ACK:
    {
        m_loginTimeoutTimer->stop();
        int errno_ = js["errno"].toInt();
        if (errno_ == 0)
        {
            m_myId = js["id"].toInt();
            m_myName = js["name"].toString();
            QJsonArray friends = js["friends"].toArray();
            QJsonArray groups = js["groups"].toArray();
            QJsonArray offlines;
            if (js.contains("offlinemsg"))
                offlines = js["offlinemsg"].toArray();
            emit loginSuccess(m_myId, m_myName, friends, groups, offlines);
            m_heartbeatTimer->start();
        }
        else
        {
            m_reconnectTimer->stop();
            resetLoginState();
            emit loginFailed(js["errmsg"].toString());
        }
        break;
    }
    case REG_MSG_ACK:
    {
        int errno_ = js["errno"].toInt();
        if (errno_ == 0)
            emit regSuccess(js["id"].toInt());
        else
            emit regFailed("register failed");
        break;
    }
    case ONE_CHAT_MSG:
    {
        int fromid = js["id"].toInt();
        QString name = js["name"].toString();
        QString msg = js["msg"].toString();
        QString time = js["time"].toString();
        emit receivedChatMsg(fromid, name, msg, time);
        break;
    }
    case GROUP_CHAT_MSG:
    {
        int groupid = js["groupid"].toInt();
        int fromid = js["id"].toInt();
        QString name = js["name"].toString();
        QString msg = js["msg"].toString();
        QString time = js["time"].toString();
        emit receivedGroupMsg(groupid, fromid, name, msg, time);
        break;
    }
    case FRIEND_STATE_CHANGE_MSG:
    {
        int friendid = js["friendid"].toInt();
        QString state = js["state"].toString();
        emit friendStateChanged(friendid, state);
        break;
    }
    case ADD_FRIEND_MSG:
    {
        int id = js["id"].toInt();
        QString name = js["name"].toString();
        QString state = js["state"].toString();
        emit friendAdded(id, name, state);
        break;
    }
    case CREATE_GROUP_MSG:
    {
        int groupid = js["groupid"].toInt();
        QString groupname = js["groupname"].toString();
        emit groupCreated(groupid, groupname);
        break;
    }
    case ADD_GROUP_MSG:
    {
        int groupid = js["groupid"].toInt();
        QString groupname = js["groupname"].toString();
        emit groupJoined(groupid, groupname);
        break;
    }
    case UPDATE_NAME_MSG:
    {
        if (js.contains("errno"))
        {
            emit nameUpdateFailed(js["errmsg"].toString());
        }
        else
        {
            int id = js["id"].toInt();
            QString name = js["name"].toString();
            emit nameUpdated(id, name);
        }
        break;
    }
    case HEARTBEAT_MSG:
    {
        m_heartbeatTimeout->stop();
        break;
    }
    }
}
