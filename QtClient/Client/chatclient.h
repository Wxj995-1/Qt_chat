#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSettings>
#include <ctime>

enum EnMsgType
{
    LOGIN_MSG = 1,
    LOGIN_MSG_ACK,
    LOGINOUT_MSG,
    REG_MSG,
    REG_MSG_ACK,
    ONE_CHAT_MSG,
    ADD_FRIEND_MSG,
    CREATE_GROUP_MSG,
    ADD_GROUP_MSG,
    GROUP_CHAT_MSG,
    FRIEND_STATE_CHANGE_MSG,
    UPDATE_NAME_MSG,
    HEARTBEAT_MSG,
};

class ChatClient : public QObject
{
    Q_OBJECT
public:
    explicit ChatClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    bool isConnected() const;

    void login(int id, const QString &pwd);
    void reg(const QString &name, const QString &pwd);
    void oneChat(int toid, const QString &msg);
    void addFriend(int friendid);
    void createGroup(const QString &name, const QString &desc);
    void addGroup(int groupid);
    void groupChat(int groupid, const QString &msg);
    void updateName(int id, const QString &newName);
    void loginout();
    void resetLoginState();

    int myId() const { return m_myId; }
    QString myName() const { return m_myName; }
    QString serverIp() const { return m_serverIp; }
    quint16 serverPort() const { return m_serverPort; }
    void setServerIp(const QString &ip);
    void setServerPort(quint16 port);
    void relogin();

signals:
    void connected();
    void connectionError(const QString &msg);
    void loginSuccess(int id, const QString &name,
                      const QJsonArray &friends,
                      const QJsonArray &groups,
                      const QJsonArray &offlineMsgs);
    void loginFailed(const QString &reason);
    void regSuccess(int id);
    void regFailed(const QString &reason);
    void receivedChatMsg(int fromid, const QString &name,
                         const QString &msg, const QString &time);
    void receivedGroupMsg(int groupid, int fromid, const QString &name,
                          const QString &msg, const QString &time);
    void friendStateChanged(int friendid, const QString &state);
    void disconnected();
    void friendAdded(int friendId, const QString &name, const QString &state);
    void groupCreated(int groupId, const QString &groupName);
    void groupJoined(int groupId, const QString &groupName);
    void nameUpdated(int id, const QString &newName);
    void nameUpdateFailed(const QString &reason);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void onReconnectTimeout();
    void onLoginTimeout();
    void sendHeartbeat();
    void onHeartbeatTimeout();

private:
    void sendJson(const QJsonObject &obj);
    void handleMessage(const QJsonObject &js);

    QTcpSocket *m_socket;
    QTimer *m_reconnectTimer;
    QTimer *m_loginTimeoutTimer;
    QTimer *m_heartbeatTimer;
    QTimer *m_heartbeatTimeout;
    QString m_host;
    quint16 m_port;
    QString m_serverIp;
    quint16 m_serverPort;
    bool m_intentionalDisconnect = false;
    QByteArray m_recvBuf;
    time_t m_lastDataTime = 0;
    int m_myId = -1;
    QString m_myName;
    QString m_password;
};

#endif
