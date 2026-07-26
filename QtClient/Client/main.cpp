#include "logindialog.h"
#include "widget.h"
#include "chatclient.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    ChatClient client;

    for (;;)
    {
        LoginDialog dlg(&client);
        if (dlg.exec() != QDialog::Accepted)
            break;

        bool switchRequested = false;
        Widget w(&client, dlg.userId(), dlg.userName(),
                 dlg.friends(), dlg.groups(), dlg.offlineMsgs());
        QObject::connect(&w, &Widget::switchAccountRequested, [&]() {
            switchRequested = true;
            w.close();
        });
        w.show();

        a.exec();

        if (!switchRequested)
            break;
    }

    return 0;
}
