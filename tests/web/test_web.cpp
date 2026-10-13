#include "web/WebRemote.h"
#include "CasterBoard.h"
#include "CasterPlayer.h"
#include <QApplication>
#include <QTabWidget>
#include <QTcpSocket>
#include <QLabel>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTest>
#include <QTemporaryFile>
#include <QDataStream>
#include <functional>
#include <cstdlib>

static void check(bool ok, const char *message) {
    if (!ok) { qCritical("FAIL: %s", message); std::exit(1); }
}
static bool until(std::function<bool()> predicate) {
    for (int i=0; i<300; ++i) { if (predicate()) return true; QTest::qWait(10); }
    return predicate();
}
static QByteArray request(const QByteArray &path, const QByteArray &method="GET", const QByteArray &headers={}) {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, 15052);
    check(until([&]{return socket.state()==QTcpSocket::ConnectedState;}), "connect");
    const QByteArray bytes=method+" "+path+" HTTP/1.1\r\nHost: localhost:15052\r\n"+headers+"\r\n";
    // Exercise request fragmentation.
    socket.write(bytes.left(8)); QTest::qWait(10); socket.write(bytes.mid(8));
    check(until([&]{return socket.state()==QTcpSocket::UnconnectedState;}), "HTTP response completes");
    return socket.readAll();
}
static QJsonArray boards() {
    auto response=request("/api/state");
    check(response.startsWith("HTTP/1.1 200"), "state status");
    return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n")+4)).object()["boards"].toArray();
}
int main(int argc, char **argv) {
    QApplication app(argc,argv);
    QTabWidget tabs;
    WebRemote remote(&tabs);
    check(remote.start(15052), "listen");
    WebRemote conflicting(&tabs);
    check(!conflicting.start(15052), "port conflict reported");
    check(boards().isEmpty(), "empty startup");
    check(request("/").contains("new EventSource('/events')"), "embedded page");
    check(request("/missing").startsWith("HTTP/1.1 404"), "unknown route");
    auto first=new CasterBoard; auto second=new CasterBoard;
    tabs.addTab(first,"First"); tabs.addTab(second,"Second");
    auto player=second->players->value(",");
    // A silent WAV exercises real QMediaPlayer state without audible test output.
    QTemporaryFile wav("/tmp/caster-web-XXXXXX.wav"); check(wav.open(), "fixture");
    QDataStream out(&wav);out.setByteOrder(QDataStream::LittleEndian);
    wav.write("RIFF",4);out << quint32(32036);wav.write("WAVEfmt ",8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    wav.write("data",4);out << quint32(32000);wav.write(QByteArray(32000,0));wav.flush();
    *player->soundFilePath=wav.fileName();
    player->player->setMedia(QUrl::fromLocalFile(wav.fileName()));
    check(until([&]{return player->player->mediaStatus()==QMediaPlayer::LoadedMedia;}), "WAV loaded");
    player->soundNameLabel->setText("<Sound> & café");
    player->setStyleSheet("background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #123456,stop:1 #123456);");
    auto state=boards();check(state.size()==2, "all tabs exposed");
    check(state[0].toObject()["players"].toArray().isEmpty(), "empty slots hidden");
    check(state[1].toObject()["players"].toArray()[0].toObject()["name"]=="<Sound> & café", "display label preserved");
    auto metadata=state[1].toObject()["players"].toArray()[0].toObject();
    check(metadata["color"]=="#123456", "applied colour exposed");
    check(metadata["row"].toInt()==3 && metadata["column"].toInt()==7, "comma slot position");
    check(state[1].toObject()["rows"].toInt()==4 && state[1].toObject()["columns"].toInt()==8, "full grid dimensions");
    if (app.arguments().contains("--preview")) return app.exec();
    QByteArray path="/api/toggle/"+state[1].toObject()["id"].toString().toUtf8()+"/%2C";
    QTcpSocket stream;stream.connectToHost(QHostAddress::LocalHost,15052);
    check(until([&]{return stream.state()==QTcpSocket::ConnectedState;}), "stream connect");
    stream.write("GET /events HTTP/1.1\r\nHost: localhost:15052\r\n\r\n");
    check(until([&]{return stream.bytesAvailable()>0;}), "initial stream");stream.readAll();
    player->setStyleSheet("background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #FFDDAA,stop:1 #FFDDAA);");
    auto grid=qobject_cast<QGridLayout *>(player->parentWidget()->layout());
    check(grid, "player layout");
    grid->removeWidget(player);grid->addWidget(player,4,2);
    QByteArray metadataEvents;
    check(until([&]{metadataEvents+=stream.readAll();return metadataEvents.contains("#FFDDAA") && metadataEvents.contains("\"row\":4");}), "colour and layout updates streamed");
    metadata=boards()[1].toObject()["players"].toArray()[0].toObject();
    check(metadata["row"].toInt()==4 && metadata["column"].toInt()==2, "position follows layout rather than key order");
    check(request(path,"POST").startsWith("HTTP/1.1 403"), "reject unguarded mutation");
    check(request(path,"POST","X-Caster-Remote: 1\r\nOrigin: http://other.invalid\r\n").startsWith("HTTP/1.1 403"), "reject cross origin");
    check(request(path,"POST","X-Caster-Remote: 1\r\n").startsWith("HTTP/1.1 204"), "toggle comma key");
    check(player->player->state()==QMediaPlayer::PlayingState, "real playback starts");
    QByteArray events;
    check(until([&]{events+=stream.readAll();return events.contains("Playing");}), "stream playing feedback");
    events.clear();
    check(until([&]{events+=stream.readAll();return events.contains("Stopped");}), "stream natural completion");
    tabs.removeTab(0);delete first;
    check(request(path,"POST","X-Caster-Remote: 1\r\n").startsWith("HTTP/1.1 204"), "identity survives tab removal");
    player->stopSound();events.clear();
    check(until([&]{events+=stream.readAll();return events.contains("Stopped");}), "desktop stop feedback");
    tabs.removeTab(0);delete second;
    check(request(path,"POST","X-Caster-Remote: 1\r\n").startsWith("HTTP/1.1 404"), "closed tab rejected");
    check(request("/","GET","Content-Length: 99\r\n").startsWith("HTTP/1.1 400"), "bodies rejected");
    check(request("/","GET","Long: "+QByteArray(9000,'x')+"\r\n").startsWith("HTTP/1.1 413"), "oversized headers rejected");
    qInfo("PASS: web remote integration tests");
}
