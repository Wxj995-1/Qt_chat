#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonArray>

class ChatClient;

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(ChatClient *client, QWidget *parent = nullptr);

    int userId() const { return m_userId; }
    QString userName() const { return m_userName; }
    QJsonArray friends() const { return m_friends; }
    QJsonArray groups() const { return m_groups; }
    QJsonArray offlineMsgs() const { return m_offlineMsgs; }

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onCancelClicked();
    void onConnected();
    void onLoginSuccess(int id, const QString &name,
                        const QJsonArray &friends,
                        const QJsonArray &groups,
                        const QJsonArray &offlineMsgs);
    void onLoginFailed(const QString &reason);
    void onRegSuccess(int id);
    void onRegFailed(const QString &reason);

private:
    void setupUI();
    void setupStyle();
    void setInputsEnabled(bool enabled);
    void showServerSettings();
    ChatClient *m_client;

    int m_userId = -1;
    QString m_userName;
    QJsonArray m_friends;
    QJsonArray m_groups;
    QJsonArray m_offlineMsgs;

    enum PendingAction { None, Login, Reg };
    PendingAction m_pending = None;
    int m_pendingLoginId = -1;
    QString m_pendingLoginPwd;
    QString m_pendingRegName;
    QString m_pendingRegPwd;

    QLineEdit *m_loginId;
    QLineEdit *m_loginPwd;
    QPushButton *m_loginBtn;
    QLabel *m_loginStatus;

    QLineEdit *m_regName;
    QLineEdit *m_regPwd;
    QPushButton *m_regBtn;
    QLabel *m_regStatus;

    QTimer *m_retryTimeout;
    QPushButton *m_serverBtn;
    QPushButton *m_cancelBtn;
};

#endif
