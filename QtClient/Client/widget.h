#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFrame>
#include <QTreeWidget>
#include <QMap>
#include <QVector>
#include <QJsonArray>
#include <QStyledItemDelegate>
#include <QPainter>

class ChatClient;

struct ChatMsg;

class BadgeDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);

        int count = index.data(Qt::UserRole + 2).toInt();
        if (count <= 0)
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        int diameter = 16;
        int x = option.rect.right() - diameter - 6;
        int y = option.rect.top() + (option.rect.height() - diameter) / 2 + 1;

        QRect badgeRect(x, y, diameter, diameter);

        painter->setBrush(QColor("#ff4757"));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(badgeRect);

        QString text = count > 99 ? "99+" : QString::number(count);
        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPixelSize(11);
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(badgeRect, Qt::AlignCenter, text);

        painter->restore();
    }
};

struct ChatMsg
{
    bool isRight;
    QString name;
    QString time;
    QString text;
};

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(ChatClient *client, int myId, const QString &name,
           const QJsonArray &friends, const QJsonArray &groups,
           const QJsonArray &offlineMsgs, QWidget *parent = nullptr);
    ~Widget();

signals:
    void switchAccountRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSendClicked();
    void onAddFriendClicked();
    void onCreateGroupClicked();
    void onAddGroupClicked();
    void onLogoutClicked();
    void onSwitchAccountClicked();
    void onTreeItemClicked(QTreeWidgetItem *item, int column);

    void onChatMsg(int fromid, const QString &name,
                   const QString &msg, const QString &time);
    void onGroupMsg(int groupid, int fromid, const QString &name,
                    const QString &msg, const QString &time);
    void onFriendStateChanged(int friendid, const QString &state);
    void onFriendAdded(int friendId, const QString &name, const QString &state);
    void onGroupCreated(int groupId, const QString &groupName);
    void onGroupJoined(int groupId, const QString &groupName);
    void onEditNameClicked();
    void onNameUpdated(int id, const QString &newName);
    void onNameUpdateFailed(const QString &reason);

private:
    void setupUI();
    void setupStyle();
    void addMessage(bool isRight, const QString &name, const QString &time, const QString &text);
    void appendToHistory(int targetId, bool isGroup, const ChatMsg &cm);
    void rebuildChatFromHistory(int targetId, bool isGroup);
    void loadFriends(const QJsonArray &friends);
    void loadGroups(const QJsonArray &groups);
    void switchToTarget(int targetId, bool isGroup, const QString &displayName);
    void setStatus(bool online);
    void toggleStatus();
    void updateFriendBadge(int friendId);
    void updateGroupBadge(int groupId);
    bool eventFilter(QObject *obj, QEvent *event) override;

    ChatClient *m_client;
    int m_myId;
    QString m_userName;
    QString m_pendingNewName;
    int m_currentTargetId = -1;
    QString m_currentTargetName;
    bool m_currentIsGroup = false;
    bool m_online = true;
    QMap<int, int> m_unreadFriend;
    QMap<int, int> m_unreadGroup;
    QMap<int, QVector<ChatMsg>> m_friendMsgHistory;
    QMap<int, QVector<ChatMsg>> m_groupMsgHistory;

    // Left panel
    QWidget *m_leftPanel;
    QLabel *m_userLabel;
    QPushButton *m_logoutBtn;
    QPushButton *m_switchBtn;
    QPushButton *m_editNameBtn;
    QTreeWidget *m_friendTree;
    QPushButton *m_addFriendBtn;
    QPushButton *m_createGroupBtn;
    QPushButton *m_addGroupBtn;

    // Right panel - chat
    QWidget *m_rightPanel;
    QLabel *m_chatTitle;
    QScrollArea *m_chatScroll;
    QWidget *m_chatContainer;
    QVBoxLayout *m_chatLayout;
    QLineEdit *m_msgInput;
    QPushButton *m_sendBtn;
    QWidget *m_inputArea;
};

#endif
