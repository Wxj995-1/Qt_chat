#include "widget.h"
#include "chatclient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>

Widget::Widget(ChatClient *client, int myId, const QString &name,
               const QJsonArray &friends, const QJsonArray &groups,
               const QJsonArray &offlineMsgs, QWidget *parent)
    : QWidget(parent), m_client(client), m_myId(myId), m_userName(name)
{
    setWindowTitle("Chat Client");
    resize(860, 600);
    setMinimumSize(720, 400);

    // Split offline messages and count unread per friend/group
    {
        QJsonArray friendArr, groupArr;
        for (int i = 0; i < offlineMsgs.size(); ++i)
        {
            QJsonObject obj = QJsonDocument::fromJson(offlineMsgs[i].toString().toUtf8()).object();
            int msgid = obj["msgid"].toInt();
            if (msgid == ONE_CHAT_MSG)
            {
                friendArr.append(offlineMsgs[i]);
                int fromid = obj["id"].toInt();
                m_unreadFriend[fromid] = m_unreadFriend.value(fromid, 0) + 1;
            }
            else if (msgid == GROUP_CHAT_MSG)
            {
                groupArr.append(offlineMsgs[i]);
                int groupid = obj["groupid"].toInt();
                m_unreadGroup[groupid] = m_unreadGroup.value(groupid, 0) + 1;
            }
        }
        m_friendOfflineMsgs = friendArr;
        m_groupOfflineMsgs = groupArr;
    }

    setupUI();
    setupStyle();

    m_userLabel->setText(name);
    setStatus(true);
    loadFriends(friends);
    loadGroups(groups);

    // Apply unread badges for offline messages
    for (auto it = m_unreadFriend.begin(); it != m_unreadFriend.end(); ++it)
        updateFriendBadge(it.key());
    for (auto it = m_unreadGroup.begin(); it != m_unreadGroup.end(); ++it)
        updateGroupBadge(it.key());

    connect(m_client, &ChatClient::receivedChatMsg, this, &Widget::onChatMsg);
    connect(m_client, &ChatClient::receivedGroupMsg, this, &Widget::onGroupMsg);
    connect(m_client, &ChatClient::friendStateChanged, this, &Widget::onFriendStateChanged);

    connect(m_sendBtn, &QPushButton::clicked, this, &Widget::onSendClicked);
    connect(m_msgInput, &QLineEdit::returnPressed, this, &Widget::onSendClicked);
    connect(m_addFriendBtn, &QPushButton::clicked, this, &Widget::onAddFriendClicked);
    connect(m_createGroupBtn, &QPushButton::clicked, this, &Widget::onCreateGroupClicked);
    connect(m_addGroupBtn, &QPushButton::clicked, this, &Widget::onAddGroupClicked);
    connect(m_logoutBtn, &QPushButton::clicked, this, &Widget::onLogoutClicked);
    connect(m_switchBtn, &QPushButton::clicked, this, &Widget::onSwitchAccountClicked);
    connect(m_friendTree, &QTreeWidget::itemClicked, this, &Widget::onTreeItemClicked);
    connect(m_client, &ChatClient::disconnected, this, [this]() { setStatus(false); });
    connect(m_client, &ChatClient::loginSuccess, this, [this](int, const QString&, const QJsonArray&, const QJsonArray&, const QJsonArray& offlineMsgs) {
        setStatus(true);
        int friendStart = m_friendOfflineMsgs.size();
        int groupStart = m_groupOfflineMsgs.size();
        for (int i = 0; i < offlineMsgs.size(); ++i)
        {
            QJsonObject obj = QJsonDocument::fromJson(offlineMsgs[i].toString().toUtf8()).object();
            int msgid = obj["msgid"].toInt();
            if (msgid == ONE_CHAT_MSG)
            {
                m_friendOfflineMsgs.append(offlineMsgs[i]);
                int fromid = obj["id"].toInt();
                m_unreadFriend[fromid] = m_unreadFriend.value(fromid, 0) + 1;
            }
            else if (msgid == GROUP_CHAT_MSG)
            {
                m_groupOfflineMsgs.append(offlineMsgs[i]);
                int groupid = obj["groupid"].toInt();
                m_unreadGroup[groupid] = m_unreadGroup.value(groupid, 0) + 1;
            }
        }
        for (auto it = m_unreadFriend.begin(); it != m_unreadFriend.end(); ++it)
            updateFriendBadge(it.key());
        for (auto it = m_unreadGroup.begin(); it != m_unreadGroup.end(); ++it)
            updateGroupBadge(it.key());
        if (m_currentTargetId != -1)
        {
            QJsonArray &arr = m_currentIsGroup ? m_groupOfflineMsgs : m_friendOfflineMsgs;
            int start = m_currentIsGroup ? groupStart : friendStart;
            for (int i = start; i < arr.size(); ++i)
            {
                QJsonObject obj = QJsonDocument::fromJson(arr[i].toString().toUtf8()).object();
                int matchId = m_currentIsGroup ? obj["groupid"].toInt() : obj["id"].toInt();
                if (matchId != m_currentTargetId)
                    continue;
                addMessage(false, obj["name"].toString(), obj["time"].toString(), obj["msg"].toString());
            }
        }
    });
    connect(m_client, &ChatClient::friendAdded, this, &Widget::onFriendAdded);
    connect(m_client, &ChatClient::groupCreated, this, &Widget::onGroupCreated);
    connect(m_client, &ChatClient::groupJoined, this, &Widget::onGroupJoined);
    connect(m_editNameBtn, &QPushButton::clicked, this, &Widget::onEditNameClicked);
    connect(m_client, &ChatClient::nameUpdated, this, &Widget::onNameUpdated);
    connect(m_client, &ChatClient::nameUpdateFailed, this, &Widget::onNameUpdateFailed);

    m_userLabel->installEventFilter(this);
}

Widget::~Widget()
{
    // m_client 生命周期长于 Widget，断开所有信号避免 use-after-free
    disconnect(m_client, nullptr, this, nullptr);
}

void Widget::closeEvent(QCloseEvent *event)
{
    m_client->loginout();
    event->accept();
}

void Widget::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== Left panel ==========
    m_leftPanel = new QWidget;
    m_leftPanel->setObjectName("leftPanel");
    m_leftPanel->setFixedWidth(260);
    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    QWidget *userHeader = new QWidget;
    userHeader->setObjectName("userHeader");
    QHBoxLayout *headerLayout = new QHBoxLayout(userHeader);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    m_userLabel = new QLabel;
    m_userLabel->setObjectName("userName");
    m_userLabel->setTextFormat(Qt::RichText);
    m_logoutBtn = new QPushButton(QStringLiteral("\u2715"));
    m_logoutBtn->setObjectName("logoutBtn");
    m_logoutBtn->setFixedSize(28, 28);
    headerLayout->addWidget(m_userLabel);
    headerLayout->addStretch();
    m_editNameBtn = new QPushButton(QStringLiteral("\u270E"));
    m_editNameBtn->setObjectName("editNameBtn");
    m_editNameBtn->setFixedSize(28, 28);
    headerLayout->addWidget(m_editNameBtn);
    m_switchBtn = new QPushButton(QStringLiteral("\u21C4"));
    m_switchBtn->setObjectName("switchBtn");
    m_switchBtn->setFixedSize(32, 32);
    headerLayout->addWidget(m_switchBtn);
    headerLayout->addWidget(m_logoutBtn);
    leftLayout->addWidget(userHeader);

    QWidget *toolbar = new QWidget;
    toolbar->setObjectName("toolbar");
    QHBoxLayout *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(8, 6, 8, 6);
    toolLayout->setSpacing(6);
    m_addFriendBtn = new QPushButton("+ Friend");
    m_addFriendBtn->setObjectName("toolBtn");
    m_createGroupBtn = new QPushButton("+ Group");
    m_createGroupBtn->setObjectName("toolBtn");
    m_addGroupBtn = new QPushButton("Join");
    m_addGroupBtn->setObjectName("toolBtn");
    toolLayout->addWidget(m_addFriendBtn);
    toolLayout->addWidget(m_createGroupBtn);
    toolLayout->addWidget(m_addGroupBtn);
    toolLayout->addStretch();
    leftLayout->addWidget(toolbar);

    m_friendTree = new QTreeWidget;
    m_friendTree->setObjectName("friendTree");
    m_friendTree->setHeaderHidden(true);
    m_friendTree->setRootIsDecorated(true);
    m_friendTree->setAnimated(true);
    m_friendTree->setIndentation(16);
    m_friendTree->setItemDelegate(new BadgeDelegate(this));
    leftLayout->addWidget(m_friendTree, 1);

    mainLayout->addWidget(m_leftPanel);

    // ========== Right panel ==========
    m_rightPanel = new QWidget;
    m_rightPanel->setObjectName("rightPanel");
    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_chatTitle = new QLabel("Select a friend or group");
    m_chatTitle->setObjectName("chatTitle");
    m_chatTitle->setFixedHeight(44);

    m_chatScroll = new QScrollArea;
    m_chatScroll->setObjectName("chatScroll");
    m_chatScroll->setWidgetResizable(true);
    m_chatScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_chatContainer = new QWidget;
    m_chatContainer->setObjectName("chatContainer");
    m_chatLayout = new QVBoxLayout(m_chatContainer);
    m_chatLayout->setContentsMargins(16, 12, 16, 12);
    m_chatLayout->setSpacing(8);
    m_chatLayout->addStretch();
    m_chatScroll->setWidget(m_chatContainer);

    m_inputArea = new QWidget;
    m_inputArea->setObjectName("inputArea");
    QHBoxLayout *inputLayout = new QHBoxLayout(m_inputArea);
    inputLayout->setContentsMargins(12, 10, 12, 10);
    m_msgInput = new QLineEdit;
    m_msgInput->setObjectName("msgInput");
    m_msgInput->setPlaceholderText("Type a message...");
    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setObjectName("sendBtn");
    inputLayout->addWidget(m_msgInput);
    inputLayout->addWidget(m_sendBtn);

    rightLayout->addWidget(m_chatTitle);
    rightLayout->addWidget(m_chatScroll, 1);
    rightLayout->addWidget(m_inputArea);

    mainLayout->addWidget(m_rightPanel, 1);
}

void Widget::setupStyle()
{
    setStyleSheet(R"(
        * { font-family: "Segoe UI", "Microsoft YaHei", sans-serif; font-size: 14px; }

        #leftPanel { background: #2c2c3a; }

        #userHeader {
            background: #22222e;
            border-bottom: 1px solid #3a3a4a;
        }
        #userName { font-size: 16px; font-weight: bold; }
        #logoutBtn {
            background: transparent; color: #999; border: none;
            font-size: 16px; font-weight: normal; border-radius: 14px;
            padding: 0 0 4px 0;
        }
        #logoutBtn:hover { background: #ff4757; color: white; }
        #switchBtn {
            background: transparent; color: #999; border: none;
            font-size: 22px; border-radius: 16px;
        }
        #switchBtn:hover { background: #3a3a4a; color: white; }
        #editNameBtn {
            background: transparent; color: #999; border: none;
            font-size: 20px; border-radius: 14px;
        }
        #editNameBtn:hover { background: #3a3a4a; color: white; }

        #toolbar {
            background: #2c2c3a;
            border-bottom: 1px solid #3a3a4a;
        }
        #toolBtn {
            padding: 6px 14px; background: #3a3a4a; color: #ccc;
            border: none; border-radius: 4px; font-size: 14px;
        }
        #toolBtn:hover { background: #4a4a5a; color: white; }

        #friendTree {
            background: #2c2c3a; color: #ddd; border: none; outline: none;
            font-size: 14px;
        }
        #friendTree::item { padding: 6px 8px; border: none; }
        #friendTree::item:hover { background: #3a3a4e; }
        #friendTree::item:selected { background: #5a5a7a; color: white; }

        #rightPanel { background: #f5f5f5; }

        #chatTitle {
            background: white; border-bottom: 1px solid #e0e0e0;
            font-size: 16px; font-weight: bold; color: #333;
            padding: 0 16px; qproperty-alignment: 'AlignVCenter';
        }

        #chatScroll { background: #ededed; border: none; }
        #chatContainer { background: #ededed; }

        #inputArea { background: white; border-top: 1px solid #e0e0e0; }
        #msgInput {
            padding: 14px 18px; border: 1px solid #ddd; border-radius: 22px;
            font-size: 15px; background: #f5f5f5;
        }
        #msgInput:focus { border: 1px solid #667eea; background: white; }

        #sendBtn {
            padding: 12px 26px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #667eea, stop:1 #764ba2);
            color: white; border: none; border-radius: 22px;
            font-size: 15px; font-weight: bold;
        }
        #sendBtn:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #5a6fd6, stop:1 #6a4292);
        }
    )");
}

// ==================== Chat ====================

void Widget::onSendClicked()
{
    if (m_currentTargetId == -1 || !m_online)
        return;

    QString text = m_msgInput->text().trimmed();
    if (text.isEmpty())
        return;

    QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    if (!m_currentIsGroup)
        m_client->oneChat(m_currentTargetId, text);
    else
        m_client->groupChat(m_currentTargetId, text);

    addMessage(true, "", time, text);

    m_msgInput->clear();
}

void Widget::onChatMsg(int fromid, const QString &name,
                       const QString &msg, const QString &time)
{
    if (fromid == m_currentTargetId && !m_currentIsGroup)
    {
        addMessage(false, name, time, msg);
    }
    else
    {
        ChatMsg cm{false, name, time, msg};
        m_pendingFriendMsgs[fromid].append(cm);
        m_unreadFriend[fromid] = m_unreadFriend.value(fromid, 0) + 1;
        updateFriendBadge(fromid);
    }
}

void Widget::onGroupMsg(int groupid, int fromid, const QString &name,
                        const QString &msg, const QString &time)
{
    if (fromid == m_myId)
        return;

    if (groupid == m_currentTargetId && m_currentIsGroup)
    {
        addMessage(false, name, time, msg);
    }
    else
    {
        ChatMsg cm{false, name, time, msg};
        m_pendingGroupMsgs[groupid].append(cm);
        m_unreadGroup[groupid] = m_unreadGroup.value(groupid, 0) + 1;
        updateGroupBadge(groupid);
    }
}

// ==================== Left panel actions ====================

void Widget::onAddFriendClicked()
{
    bool ok;
    int id = QInputDialog::getInt(this, "Add Friend", "Friend ID:", 1, 1, 999999, 1, &ok);
    if (ok)
        m_client->addFriend(id);
}

void Widget::onCreateGroupClicked()
{
    bool ok;
    QString name = QInputDialog::getText(this, "Create Group", "Group Name:",
                                         QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;
    QString desc = QInputDialog::getText(this, "Create Group", "Description:",
                                         QLineEdit::Normal, "", &ok);
    if (ok)
        m_client->createGroup(name, desc);
}

void Widget::onAddGroupClicked()
{
    bool ok;
    int id = QInputDialog::getInt(this, "Join Group", "Group ID:", 1, 1, 999999, 1, &ok);
    if (ok)
        m_client->addGroup(id);
}

void Widget::onLogoutClicked()
{
    setStatus(false);
    m_client->loginout();
    close();
}

void Widget::onSwitchAccountClicked()
{
    m_client->loginout();
    m_client->resetLoginState();
    close();
    emit switchAccountRequested();
}

void Widget::onEditNameClicked()
{
    bool ok;
    QString defaultName = m_pendingNewName.isEmpty() ? m_userName : m_pendingNewName;
    QString newName = QInputDialog::getText(this, "Change Name",
                                            "New name:", QLineEdit::Normal,
                                            defaultName, &ok);
    if (ok && !newName.trimmed().isEmpty())
    {
        m_pendingNewName = newName.trimmed();
        m_client->updateName(m_myId, m_pendingNewName);
    }
    else
    {
        m_pendingNewName.clear();
    }
}

void Widget::onNameUpdated(int id, const QString &newName)
{
    if (id == m_myId)
    {
        m_pendingNewName.clear();
        m_userName = newName;
        setStatus(m_online);
    }
}

void Widget::onNameUpdateFailed(const QString &reason)
{
    QMessageBox::warning(this, "Change Name", reason);
    onEditNameClicked();
}

void Widget::onTreeItemClicked(QTreeWidgetItem *item, int)
{
    int type = item->data(0, Qt::UserRole).toInt();

    if (type == 0)
    {
        item->setExpanded(!item->isExpanded());
        return;
    }

    int id = item->data(0, Qt::UserRole + 1).toInt();
    bool isGroup = (type == 2);
    QString name;

    if (isGroup)
        name = item->text(0);
    else
        name = item->text(0).section(" [", 0, 0);

    switchToTarget(id, isGroup, name);
}

void Widget::switchToTarget(int targetId, bool isGroup, const QString &displayName)
{
    m_currentTargetId = targetId;
    m_currentTargetName = displayName;
    m_currentIsGroup = isGroup;

    // Clear unread badge for this target
    if (isGroup)
    {
        m_unreadGroup.remove(targetId);
        updateGroupBadge(targetId);
    }
    else
    {
        m_unreadFriend.remove(targetId);
        updateFriendBadge(targetId);
    }

    if (isGroup)
        m_chatTitle->setText(QString("Group: %1").arg(displayName));
    else
        m_chatTitle->setText(QString("Chat with %1").arg(displayName));

    // Clear chat container
    QLayoutItem *item;
    while ((item = m_chatLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_chatLayout->addStretch();
    m_msgInput->setFocus();

    showTargetOfflineMsgs(targetId, isGroup);

    // Flush pending messages that arrived while chatting elsewhere
    if (isGroup)
    {
        QVector<ChatMsg> &msgs = m_pendingGroupMsgs[targetId];
        for (const ChatMsg &cm : msgs)
            addMessage(cm.isRight, cm.name, cm.time, cm.text);
        msgs.clear();
    }
    else
    {
        QVector<ChatMsg> &msgs = m_pendingFriendMsgs[targetId];
        for (const ChatMsg &cm : msgs)
            addMessage(cm.isRight, cm.name, cm.time, cm.text);
        msgs.clear();
    }
}

void Widget::showTargetOfflineMsgs(int targetId, bool isGroup)
{
    QJsonArray &arr = isGroup ? m_groupOfflineMsgs : m_friendOfflineMsgs;

    for (int i = 0; i < arr.size(); ++i)
    {
        QJsonObject obj = QJsonDocument::fromJson(arr[i].toString().toUtf8()).object();
        int matchId = isGroup ? obj["groupid"].toInt() : obj["id"].toInt();
        if (matchId != targetId)
            continue;

        addMessage(false, obj["name"].toString(), obj["time"].toString(), obj["msg"].toString());
    }
}

void Widget::setStatus(bool online)
{
    m_online = online;
    QString color = online ? "#4ade80" : "#888";
    QString name = m_userName.toHtmlEscaped();
    m_userLabel->setText(QString("<span style='color:%1; font-size:18px; line-height:16px; vertical-align:middle;'>\u25cf</span> "
                                 "<span style='color:white; font-size:16px; vertical-align:middle;'> %2</span>"
                                 "<span style='color:#888; font-size:11px; vertical-align:middle;'>  ID:%3</span>")
                         .arg(color, name, QString::number(m_myId)));
    m_msgInput->setEnabled(online);
    m_sendBtn->setEnabled(online);
    if (!online)
        m_chatTitle->setText("You are offline");
    else if (m_currentTargetId != -1)
        m_chatTitle->setText(m_currentIsGroup
            ? QString("Group: %1").arg(m_currentTargetName)
            : QString("Chat with %1").arg(m_currentTargetName));
}

void Widget::toggleStatus()
{
    if (m_online)
    {
        m_client->loginout();
        setStatus(false);
    }
    else
    {
        m_client->relogin();
    }
}

void Widget::updateFriendBadge(int friendId)
{
    for (int i = 0; i < m_friendTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = m_friendTree->topLevelItem(i);
        if (root->text(0) != "Friends") continue;
        for (int j = 0; j < root->childCount(); ++j)
        {
            QTreeWidgetItem *child = root->child(j);
            if (child->data(0, Qt::UserRole + 1).toInt() == friendId)
            {
                child->setData(0, Qt::UserRole + 2, m_unreadFriend.value(friendId, 0));
                break;
            }
        }
    }
}

void Widget::updateGroupBadge(int groupId)
{
    for (int i = 0; i < m_friendTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = m_friendTree->topLevelItem(i);
        if (root->text(0) != "Groups") continue;
        for (int j = 0; j < root->childCount(); ++j)
        {
            QTreeWidgetItem *child = root->child(j);
            if (child->data(0, Qt::UserRole + 1).toInt() == groupId)
            {
                child->setData(0, Qt::UserRole + 2, m_unreadGroup.value(groupId, 0));
                break;
            }
        }
    }
}

bool Widget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_userLabel && event->type() == QEvent::MouseButtonPress)
    {
        toggleStatus();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void Widget::onFriendStateChanged(int friendid, const QString &state)
{
    for (int i = 0; i < m_friendTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *groupItem = m_friendTree->topLevelItem(i);
        if (!groupItem) continue;
        for (int j = 0; j < groupItem->childCount(); ++j)
        {
            QTreeWidgetItem *child = groupItem->child(j);
            if (child->data(0, Qt::UserRole + 1).toInt() == friendid)
            {
                QString name = child->text(0).section(" [", 0, 0);
                child->setText(0, QString("%1 [%2]").arg(name, state));
                child->setForeground(0, state == "online" ? QColor("#4ade80") : QColor("#888"));
                QFont f = child->font(0);
                f.setBold(state == "online");
                child->setFont(0, f);
                return;
            }
        }
    }
}

void Widget::onFriendAdded(int friendId, const QString &name, const QString &state)
{
    for (int i = 0; i < m_friendTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = m_friendTree->topLevelItem(i);
        if (root->text(0) != "Friends") continue;
        QTreeWidgetItem *item = new QTreeWidgetItem;
        item->setText(0, QString("%1 [%2]").arg(name, state));
        item->setData(0, Qt::UserRole, 1);
        item->setData(0, Qt::UserRole + 1, friendId);
        item->setForeground(0, state == "online" ? QColor("#4ade80") : QColor("#888"));
        QFont f = item->font(0);
        f.setBold(state == "online");
        item->setFont(0, f);
        item->setSizeHint(0, QSize(0, 36));
        root->addChild(item);
        break;
    }
}

void Widget::onGroupCreated(int groupId, const QString &groupName)
{
    for (int i = 0; i < m_friendTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = m_friendTree->topLevelItem(i);
        if (root->text(0) != "Groups") continue;
        QTreeWidgetItem *item = new QTreeWidgetItem;
        item->setText(0, groupName);
        item->setData(0, Qt::UserRole, 2);
        item->setData(0, Qt::UserRole + 1, groupId);
        item->setForeground(0, QColor("#fbbf24"));
        item->setSizeHint(0, QSize(0, 36));
        root->addChild(item);
        break;
    }
}

void Widget::onGroupJoined(int groupId, const QString &groupName)
{
    onGroupCreated(groupId, groupName);
}

// ==================== UI helpers ====================

void Widget::addMessage(bool isRight, const QString &name,
                        const QString &time, const QString &text)
{
    QFrame *bubble = new QFrame;
    bubble->setObjectName("chatBubble");
    QVBoxLayout *bl = new QVBoxLayout(bubble);
    bl->setContentsMargins(14, 8, 14, 8);
    bl->setSpacing(4);

    if (!isRight && !name.isEmpty())
    {
        QLabel *header = new QLabel(QString("%1  %2").arg(name, time));
        header->setStyleSheet("font-size:12px; color:#b2b2b2; background:transparent; border:none;");
        bl->addWidget(header);
    }
    else if (isRight)
    {
        QLabel *header = new QLabel(time);
        header->setStyleSheet("font-size:12px; color:#b2b2b2; background:transparent; border:none;");
        header->setAlignment(Qt::AlignRight);
        bl->addWidget(header);
    }

    QLabel *msgLabel = new QLabel(text);
    msgLabel->setTextFormat(Qt::PlainText);
    msgLabel->setWordWrap(true);
    msgLabel->setMaximumWidth(400);
    msgLabel->setStyleSheet("font-size:15px; color:#333; background:transparent; border:none;");
    bl->addWidget(msgLabel);

    if (isRight)
        bubble->setStyleSheet("#chatBubble { background-color:#95ec69; border-radius:16px; padding:0px; }");
    else
        bubble->setStyleSheet("#chatBubble { background-color:white; border:1px solid #e8e8e8; border-radius:16px; padding:0px; }");

    QWidget *row = new QWidget;
    QHBoxLayout *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    if (isRight)
        hl->addStretch();
    hl->addWidget(bubble);
    if (!isRight)
        hl->addStretch();

    m_chatLayout->insertWidget(m_chatLayout->count() - 1, row);

    QScrollBar *sb = m_chatScroll->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void Widget::loadFriends(const QJsonArray &friends)
{
    QTreeWidgetItem *friendRoot = new QTreeWidgetItem({"Friends"});
    friendRoot->setData(0, Qt::UserRole, 0);
    QFont rootFont = friendRoot->font(0);
    rootFont.setBold(true);
    friendRoot->setFont(0, rootFont);
    friendRoot->setForeground(0, QColor("#aaa"));
    friendRoot->setFlags(friendRoot->flags() | Qt::ItemIsAutoTristate);

    for (int i = 0; i < friends.size(); ++i)
    {
        QJsonObject obj = QJsonDocument::fromJson(friends[i].toString().toUtf8()).object();
        int fid = obj["id"].toInt();
        QString name = obj["name"].toString();
        QString state = obj["state"].toString();

        QTreeWidgetItem *item = new QTreeWidgetItem;
        item->setText(0, QString("%1 [%2]").arg(name, state));
        item->setData(0, Qt::UserRole, 1);
        item->setData(0, Qt::UserRole + 1, fid);
        item->setForeground(0, state == "online" ? QColor("#4ade80") : QColor("#888"));
        QFont f = item->font(0);
        f.setBold(state == "online");
        item->setFont(0, f);
        item->setSizeHint(0, QSize(0, 36));

        friendRoot->addChild(item);
    }
    m_friendTree->addTopLevelItem(friendRoot);
    friendRoot->setExpanded(true);
}

void Widget::loadGroups(const QJsonArray &groups)
{
    QTreeWidgetItem *groupRoot = new QTreeWidgetItem({"Groups"});
    groupRoot->setData(0, Qt::UserRole, 0);
    QFont rootFont = groupRoot->font(0);
    rootFont.setBold(true);
    groupRoot->setFont(0, rootFont);
    groupRoot->setForeground(0, QColor("#aaa"));

    for (int i = 0; i < groups.size(); ++i)
    {
        QJsonObject obj = QJsonDocument::fromJson(groups[i].toString().toUtf8()).object();
        int gid = obj["id"].toInt();
        QString gname = obj["groupname"].toString();

        QTreeWidgetItem *item = new QTreeWidgetItem;
        item->setText(0, gname);
        item->setData(0, Qt::UserRole, 2);
        item->setData(0, Qt::UserRole + 1, gid);
        item->setForeground(0, QColor("#fbbf24"));
        item->setSizeHint(0, QSize(0, 36));

        groupRoot->addChild(item);
    }
    m_friendTree->addTopLevelItem(groupRoot);
    groupRoot->setExpanded(true);
}
