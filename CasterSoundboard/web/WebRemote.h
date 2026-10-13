#ifndef WEBREMOTE_H
#define WEBREMOTE_H
#include <QObject>
#include <QSet>
#include <QTcpServer>
class QMediaPlayer;
class QTabWidget;
class QTcpSocket;

// Deliberately small HTTP/SSE server; all access stays on the GUI event loop.
class WebRemote : public QObject
{
public:
    explicit WebRemote(QTabWidget *tabs, QObject *parent = nullptr);
    ~WebRemote() override;
    bool start(quint16 port = 5052);
    QString errorString() const { return server.errorString(); }
private:
    QByteArray snapshot();
    void acceptConnections();
    void readRequest(QTcpSocket *socket);
    void broadcast();
    QTabWidget *tabs;
    QTcpServer server;
    QSet<QTcpSocket *> clients;
    QSet<QTcpSocket *> streams;
    QSet<QMediaPlayer *> watchedPlayers;
    QByteArray previous;
    int ticks = 0;
};
#endif
