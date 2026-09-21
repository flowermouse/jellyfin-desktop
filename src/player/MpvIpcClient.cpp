#include "MpvIpcClient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <QtGlobal>

namespace
{
  const int kRetryIntervalMs = 100;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QVector<QJsonObject> MpvIpcProtocol::extractMessages(QByteArray& buffer)
{
  QVector<QJsonObject> messages;

  qsizetype start = 0;
  while (true)
  {
    const qsizetype newline = buffer.indexOf('\n', start);
    if (newline < 0)
      break;

    const QByteArray line = buffer.mid(start, newline - start).trimmed();
    start = newline + 1;

    if (line.isEmpty())
      continue;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
      // Never log the line itself: mpv echoes back paths, which carry access tokens.
      qWarning() << "Ignoring malformed mpv IPC message:" << parseError.errorString();
      continue;
    }

    messages.append(doc.object());
  }

  buffer.remove(0, start);
  return messages;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QByteArray MpvIpcProtocol::encodeCommand(const QJsonArray& command, qint64 requestId)
{
  QJsonObject message;
  message["command"] = command;
  message["request_id"] = requestId;

  return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

///////////////////////////////////////////////////////////////////////////////////////////////////
MpvIpcClient::MpvIpcClient(QObject* parent) : QObject(parent)
{
  m_socket = new QLocalSocket(this);
  connect(m_socket, &QLocalSocket::connected, this, &MpvIpcClient::onConnected);
  connect(m_socket, &QLocalSocket::readyRead, this, &MpvIpcClient::onReadyRead);
  connect(m_socket, &QLocalSocket::errorOccurred, this, &MpvIpcClient::onSocketError);
  connect(m_socket, &QLocalSocket::disconnected, this, [this] {
    if (!m_connected)
      return;
    m_connected = false;
    emit disconnected();
  });

  m_retryTimer = new QTimer(this);
  m_retryTimer->setInterval(kRetryIntervalMs);
  connect(m_retryTimer, &QTimer::timeout, this, &MpvIpcClient::onRetryTimeout);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
MpvIpcClient::~MpvIpcClient()
{
  close();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MpvIpcClient::connectToSocket(const QString& path, int timeoutMs)
{
  close();

  m_path = path;
  m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
  m_buffer.clear();

  m_retryTimer->start();
  m_socket->connectToServer(m_path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MpvIpcClient::close()
{
  m_retryTimer->stop();
  m_connected = false;
  m_buffer.clear();

  if (m_socket->state() != QLocalSocket::UnconnectedState)
  {
    m_socket->abort();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool MpvIpcClient::isConnected() const
{
  return m_connected;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MpvIpcClient::onConnected()
{
  m_retryTimer->stop();
  m_connected = true;
  emit connected();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MpvIpcClient::onSocketError()
{
  // While the retry timer is running the socket errors once per failed attempt; that is expected
  // until IINA has created the socket, so only a hard failure after connecting is interesting.
  if (m_connected)
  {
    m_connected = false;
    emit disconnected();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MpvIpcClient::onRetryTimeout()
{
  if (m_connected)
  {
    m_retryTimer->stop();
    return;
  }

  if (QDateTime::currentMSecsSinceEpoch() >= m_deadlineMs)
  {
    m_retryTimer->stop();
    m_socket->abort();
    emit connectionFailed(tr("Timed out waiting for the IINA playback connection."));
    return;
  }

  if (m_socket->state() == QLocalSocket::UnconnectedState)
    m_socket->connectToServer(m_path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MpvIpcClient::onReadyRead()
{
  m_buffer.append(m_socket->readAll());

  // A single read can contain several messages, and a message can be split across reads; both are
  // handled by keeping the remainder in m_buffer.
  const QVector<QJsonObject> messages = MpvIpcProtocol::extractMessages(m_buffer);
  for (const QJsonObject& message : messages)
    dispatch(message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MpvIpcClient::dispatch(const QJsonObject& message)
{
  const QString event = message.value("event").toString();
  if (!event.isEmpty())
  {
    if (event == QLatin1String("property-change"))
    {
      const QString name = message.value("name").toString();
      if (!name.isEmpty())
        emit propertyChanged(name, message.value("data"));
      return;
    }

    emit eventReceived(event, message);
    return;
  }

  if (message.contains("request_id"))
  {
    const qint64 requestId = static_cast<qint64>(message.value("request_id").toDouble());
    const bool success = message.value("error").toString() == QLatin1String("success");
    emit commandResponse(requestId, success, message.value("data"));
  }

  // Anything else (unknown shapes from future mpv versions) is intentionally ignored.
}

///////////////////////////////////////////////////////////////////////////////////////////////////
qint64 MpvIpcClient::sendCommand(const QJsonArray& command)
{
  if (!m_connected)
    return -1;

  const qint64 requestId = m_nextRequestId++;
  m_socket->write(MpvIpcProtocol::encodeCommand(command, requestId));
  m_socket->flush();
  return requestId;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
qint64 MpvIpcClient::setProperty(const QString& name, const QJsonValue& value)
{
  return sendCommand(QJsonArray{QStringLiteral("set_property"), name, value});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
qint64 MpvIpcClient::observeProperty(const QString& name)
{
  return sendCommand(QJsonArray{QStringLiteral("observe_property"), m_nextObserveId++, name});
}
