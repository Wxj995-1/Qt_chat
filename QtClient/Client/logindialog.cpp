#include "logindialog.h"
#include "chatclient.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QIntValidator>
#include <QDebug>

LoginDialog::LoginDialog(ChatClient *client, QWidget *parent)
    : QDialog(parent), m_client(client)
{
    setWindowTitle("Chat Login");
    setFixedSize(420, 580);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    m_retryTimeout = new QTimer(this);
    m_retryTimeout->setSingleShot(true);
    connect(m_retryTimeout, &QTimer::timeout, this, [this]() {
        m_client->loginout();
        PendingAction was = m_pending;
        m_pending = None;
        setInputsEnabled(true);
        if (was == Login)
        {
            m_loginStatus->setStyleSheet("font-size:12px;color:#e74c3c;");
            m_loginStatus->setText("Connection timeout, try again");
        }
        else if (was == Reg)
        {
            m_regStatus->setStyleSheet("font-size:12px;color:#e74c3c;");
            m_regStatus->setText("Connection timeout, try again");
        }
    });

    setupUI();
    setupStyle();

    connect(m_client, &ChatClient::connected, this, &LoginDialog::onConnected);
    connect(m_client, &ChatClient::loginSuccess, this, &LoginDialog::onLoginSuccess);
    connect(m_client, &ChatClient::loginFailed, this, &LoginDialog::onLoginFailed);
    connect(m_client, &ChatClient::regSuccess, this, &LoginDialog::onRegSuccess);
    connect(m_client, &ChatClient::regFailed, this, &LoginDialog::onRegFailed);
    connect(m_client, &ChatClient::connectionError, this, [this](const QString &msg) {
        qDebug() << "[LoginDialog] connectionError - pending:" << m_pending << "msg:" << msg;
        if (m_pending == Login)
            m_loginStatus->setText("Cannot reach server, retrying...");
        else if (m_pending == Reg)
            m_regStatus->setText("Cannot reach server, retrying...");
    });
}

void LoginDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setAlignment(Qt::AlignCenter);

    QWidget *container = new QWidget;
    container->setObjectName("loginContainer");
    container->setFixedWidth(360);
    QVBoxLayout *cl = new QVBoxLayout(container);
    cl->setSpacing(12);
    cl->setContentsMargins(28, 32, 28, 32);

    QLabel *logo = new QLabel("Chat");
    logo->setObjectName("logo");
    logo->setAlignment(Qt::AlignCenter);
    cl->addWidget(logo);
    cl->addSpacing(4);

    m_serverBtn = new QPushButton("\u2699");
    m_serverBtn->setObjectName("serverBtn");
    m_serverBtn->setCursor(Qt::PointingHandCursor);
    m_serverBtn->setAutoDefault(false);
    m_serverBtn->setFocusPolicy(Qt::NoFocus);
    QHBoxLayout *serverRow = new QHBoxLayout;
    serverRow->addStretch();
    serverRow->addWidget(m_serverBtn);
    cl->addLayout(serverRow);
    cl->addSpacing(8);
    connect(m_serverBtn, &QPushButton::clicked, this, &LoginDialog::showServerSettings);

    m_loginId = new QLineEdit;
    m_loginId->setPlaceholderText("User ID");
    m_loginId->setObjectName("input");
    m_loginId->setFixedHeight(40);

    m_loginPwd = new QLineEdit;
    m_loginPwd->setPlaceholderText("Password");
    m_loginPwd->setEchoMode(QLineEdit::Password);
    m_loginPwd->setObjectName("input");
    m_loginPwd->setFixedHeight(40);

    m_loginBtn = new QPushButton("Log In");
    m_loginBtn->setObjectName("primaryBtn");
    m_loginBtn->setFixedHeight(44);
    m_loginBtn->setDefault(true);

    m_loginStatus = new QLabel;
    m_loginStatus->setObjectName("status");
    m_loginStatus->setAlignment(Qt::AlignCenter);
    m_loginStatus->setWordWrap(true);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setObjectName("cancelBtn");
    m_cancelBtn->setFixedHeight(36);
    m_cancelBtn->setVisible(false);

    cl->addWidget(m_loginId);
    cl->addWidget(m_loginPwd);
    cl->addWidget(m_loginBtn);
    cl->addWidget(m_loginStatus);
    cl->addWidget(m_cancelBtn);

    cl->addSpacing(8);
    QLabel *sep = new QLabel("\u2500 or \u2500");
    sep->setAlignment(Qt::AlignCenter);
    sep->setStyleSheet("color:#bbb;font-size:12px;");
    cl->addWidget(sep);
    cl->addSpacing(4);

    m_regName = new QLineEdit;
    m_regName->setPlaceholderText("New username");
    m_regName->setObjectName("input");
    m_regName->setFixedHeight(40);

    m_regPwd = new QLineEdit;
    m_regPwd->setPlaceholderText("New password");
    m_regPwd->setEchoMode(QLineEdit::Password);
    m_regPwd->setObjectName("input");
    m_regPwd->setFixedHeight(40);

    m_regBtn = new QPushButton("Create Account");
    m_regBtn->setObjectName("secondaryBtn");
    m_regBtn->setFixedHeight(44);

    m_regStatus = new QLabel;
    m_regStatus->setObjectName("status");
    m_regStatus->setAlignment(Qt::AlignCenter);
    m_regStatus->setWordWrap(true);

    cl->addWidget(m_regName);
    cl->addWidget(m_regPwd);
    cl->addWidget(m_regBtn);
    cl->addWidget(m_regStatus);

    mainLayout->addWidget(container);

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(m_loginPwd, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(m_regBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &LoginDialog::onCancelClicked);
}

void LoginDialog::setupStyle()
{
    setStyleSheet(R"(
        * { font-family: "Segoe UI", "Microsoft YaHei", sans-serif; font-size: 13px; }
        LoginDialog {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #667eea, stop:1 #764ba2);
        }
        #loginContainer { background: white; border-radius: 12px; }
        #logo {
            font-size: 32px; font-weight: bold; color: #667eea; padding: 0;
        }
        #input {
            padding: 10px 14px; border: 1px solid #e0e0e0; border-radius: 6px;
            font-size: 14px; background: #f8f9fa;
        }
        #input:focus { border: 1px solid #667eea; background: white; }
        #primaryBtn {
            padding: 0 10px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #667eea, stop:1 #764ba2);
            color: white; border: none; border-radius: 6px;
            font-size: 15px; font-weight: bold;
        }
        #primaryBtn:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #5a6fd6, stop:1 #6a4292);
        }
        #secondaryBtn {
            padding: 0 10px; background: transparent; color: #667eea;
            border: 1px solid #667eea; border-radius: 6px; font-size: 14px;
        }
        #secondaryBtn:hover { background: #f0f0ff; }
        #status { font-size: 12px; color: #e74c3c; padding: 2px; }
        #serverBtn {
            background: transparent; border: none; color: #999;
            font-size: 12px; padding: 2px 6px;
        }
        #serverBtn:hover { color: #667eea; }
        #cancelBtn {
            background: transparent; color: #999; border: 1px solid #ddd;
            border-radius: 6px; font-size: 13px;
        }
        #cancelBtn:hover { color: #e74c3c; border-color: #e74c3c; }
    )");
}

void LoginDialog::onLoginClicked()
{
    qDebug() << "[LoginDialog] onLoginClicked - id:" << m_loginId->text() << "connected:" << m_client->isConnected();
    if (m_loginId->text().isEmpty() || m_loginPwd->text().isEmpty())
    {
        m_loginStatus->setText("Please fill all fields");
        return;
    }
    setInputsEnabled(false);
    m_loginStatus->setStyleSheet("font-size:12px;color:#667eea;");
    m_loginStatus->setText("Connecting...");

    m_pendingLoginId = m_loginId->text().toInt();
    m_pendingLoginPwd = m_loginPwd->text();
    m_pending = Login;
    m_retryTimeout->start(10000);

    if (!m_client->isConnected())
        m_client->connectToServer(m_client->serverIp(), m_client->serverPort());
    else
        m_client->login(m_pendingLoginId, m_pendingLoginPwd);
}

void LoginDialog::onRegisterClicked()
{
    if (m_regName->text().isEmpty() || m_regPwd->text().isEmpty())
    {
        m_regStatus->setText("Please fill all fields");
        return;
    }
    setInputsEnabled(false);
    m_regStatus->setStyleSheet("font-size:12px;color:#667eea;");
    m_regStatus->setText("Connecting...");

    m_pendingRegName = m_regName->text();
    m_pendingRegPwd = m_regPwd->text();
    m_pending = Reg;
    m_retryTimeout->start(10000);

    if (!m_client->isConnected())
        m_client->connectToServer(m_client->serverIp(), m_client->serverPort());
    else
        m_client->reg(m_pendingRegName, m_pendingRegPwd);
}

void LoginDialog::onCancelClicked()
{
    m_retryTimeout->stop();
    m_client->loginout();
    m_pending = None;
    setInputsEnabled(true);
    m_loginStatus->setText("");
    m_regStatus->setText("");
}

void LoginDialog::onConnected()
{
    qDebug() << "[LoginDialog] onConnected - pending:" << m_pending;
    if (m_pending == Login)
        m_client->login(m_pendingLoginId, m_pendingLoginPwd);
    else if (m_pending == Reg)
        m_client->reg(m_pendingRegName, m_pendingRegPwd);
}

void LoginDialog::onLoginSuccess(int id, const QString &name,
                                  const QJsonArray &friends,
                                  const QJsonArray &groups,
                                  const QJsonArray &offlineMsgs)
{
    qDebug() << "[LoginDialog] onLoginSuccess - id:" << id << "name:" << name;
    m_userId = id;
    m_userName = name;
    m_friends = friends;
    m_groups = groups;
    m_offlineMsgs = offlineMsgs;
    m_pending = None;
    m_retryTimeout->stop();
    accept();
}

void LoginDialog::onLoginFailed(const QString &reason)
{
    qDebug() << "[LoginDialog] onLoginFailed - reason:" << reason;
    m_retryTimeout->stop();
    setInputsEnabled(true);
    m_pending = None;
    m_loginStatus->setStyleSheet("font-size:12px;color:#e74c3c;");
    m_loginStatus->setText(reason);
}

void LoginDialog::onRegSuccess(int id)
{
    m_retryTimeout->stop();
    setInputsEnabled(true);
    m_pending = None;
    m_regStatus->setStyleSheet("font-size:12px;color:#27ae60;");
    m_regStatus->setText(QString("Registered! ID: %1").arg(id));
    m_regName->clear();
    m_regPwd->clear();
}

void LoginDialog::onRegFailed(const QString &reason)
{
    m_retryTimeout->stop();
    setInputsEnabled(true);
    m_pending = None;
    m_regStatus->setStyleSheet("font-size:12px;color:#e74c3c;");
    m_regStatus->setText(reason);
}

void LoginDialog::showServerSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Server Settings");
    dlg.setFixedSize(320, 180);

    QFormLayout *form = new QFormLayout(&dlg);
    form->setContentsMargins(24, 24, 24, 24);
    form->setSpacing(12);

    QLineEdit *ipEdit = new QLineEdit(m_client->serverIp());
    QLineEdit *portEdit = new QLineEdit(QString::number(m_client->serverPort()));
    portEdit->setValidator(new QIntValidator(1, 65535, this));

    form->addRow("Server IP:", ipEdit);
    form->addRow("Port:", portEdit);

    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted)
    {
        m_client->setServerIp(ipEdit->text());
        m_client->setServerPort(portEdit->text().toUShort());
    }
}

void LoginDialog::setInputsEnabled(bool enabled)
{
    m_loginBtn->setEnabled(enabled);
    m_regBtn->setEnabled(enabled);
    m_cancelBtn->setVisible(!enabled);
}
