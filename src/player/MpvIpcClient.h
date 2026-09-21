#ifndef MPVIPCCLIENT_H
#define MPVIPCCLIENT_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVector>

class QLocalSocket;
class QTimer;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Framing rules of the mpv JSON IPC protocol, kept separate from the socket so they can be unit
// tested with synthetic byte streams.
namespace MpvIpcProtocol
{
  // Removes every complete newline-terminated JSON object from `buffer` and returns them in order.
  // A trailing partial message is left in `buffer` for the next read. Malformed or empty lines are
  // skipped rather than aborting the stream, because mpv may emit lines we do not model.
  QVector<QJsonObject> extractMessages(QByteArray& buffer);

  QByteArray encodeCommand(const QJsonArray& command, qint64 requestId);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// A minimal client for mpv's JSON IPC socket, as exposed by IINA via --mpv-input-ipc-server.
//
// The socket does not exist yet when the player is launched, so connecting retries on a timer
// until a deadline is reached. Nothing here blocks the Qt main thread.
class MpvIpcClient : public QObject
{
  Q_OBJECT

public:
  explicit MpvIpcClient(QObject* parent = nullptr);
  ~MpvIpcClient() override;

  // Starts retrying until connected or `timeoutMs` has elapsed.
  void connectToSocket(const QString& path, int timeoutMs);

  // Drops the connection and cancels all pending retries. Safe to call in any state; emits
  // nothing, so callers stay in control of their own lifecycle signals.
  void close();

  bool isConnected() const;

  // Returns the request id used, or -1 when not connected.
  qint64 sendCommand(const QJsonArray& command);
  qint64 setProperty(const QString& name, const QJsonValue& value);
  qint64 observeProperty(const QString& name);

signals:
  void connected();
  void disconnected();
  void connectionFailed(const QString& reason);

  // `name` is the mpv event name; `event` is the whole message for fields we do not model.
  void eventReceived(const QString& name, const QJsonObject& event);
  void propertyChanged(const QString& name, const QJsonValue& value);
  void commandResponse(qint64 requestId, bool success, const QJsonValue& data);

private slots:
  void onConnected();
  void onReadyRead();
  void onSocketError();
  void onRetryTimeout();

private:
  void dispatch(const QJsonObject& message);

  QLocalSocket* m_socket = nullptr;
  QTimer* m_retryTimer = nullptr;
  QString m_path;
  QByteArray m_buffer;
  qint64 m_nextRequestId = 1;
  qint64 m_deadlineMs = 0;
  bool m_connected = false;

  // mpv echoes the observe id back in property-change messages, but the name is also included, so
  // we only need the map to keep observe ids unique.
  qint64 m_nextObserveId = 1;
};

#endif // MPVIPCCLIENT_H
