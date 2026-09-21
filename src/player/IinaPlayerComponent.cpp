#include "IinaPlayerComponent.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QUrl>

#include "IinaLaunchArgs.h"
#include "MpvIpcClient.h"

namespace
{
  // IINA has to launch, create a player core and let mpv bind the socket. Ten seconds is generous
  // for a cold start while still failing before the user gives up on the spinner.
  const int kIpcConnectTimeoutMs = 10000;

  double jsonToDouble(const QJsonValue& value, double fallback = 0.0)
  {
    return value.isDouble() ? value.toDouble() : fallback;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaPlayerComponent::IinaPlayerComponent(QObject* parent) : ComponentBase(parent)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaPlayerComponent::~IinaPlayerComponent()
{
  teardownSession();
  QDir(IinaLaunch::socketDirectory(QCoreApplication::applicationPid())).removeRecursively();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool IinaPlayerComponent::componentInitialize()
{
  m_iina = IinaApplication::Discover();

  if (m_iina.installed)
    qInfo() << "Found IINA" << m_iina.version << "at" << m_iina.applicationPath;
  else
    qInfo() << "IINA is not installed; external video playback is unavailable";

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool IinaPlayerComponent::isAvailable() const
{
  return m_iina.installed && !m_iina.cliPath.isEmpty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString IinaPlayerComponent::applicationPath() const
{
  return m_iina.applicationPath;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString IinaPlayerComponent::applicationVersion() const
{
  return m_iina.version;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::openDownloadPage()
{
  QDesktopServices::openUrl(QUrl(QString::fromLatin1(IinaApplication::kDownloadUrl)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool IinaPlayerComponent::prepareSocketDirectory(QString& errorOut)
{
  const QString path = IinaLaunch::socketDirectory(QCoreApplication::applicationPid());

  QDir dir;
  if (!dir.exists(path) && !dir.mkpath(path))
  {
    errorOut = tr("Could not create the IINA playback socket directory.");
    return false;
  }

  // The socket lives in the shared /tmp, so restrict it to this user: anyone able to connect to
  // an mpv IPC socket can drive playback and read the media path.
  if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner))
  {
    errorOut = tr("Could not secure the IINA playback socket directory.");
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool IinaPlayerComponent::load(const QString& url,
                               const QVariantMap& options,
                               const QVariantMap& metadata,
                               const QVariant& audioStream,
                               const QVariant& subtitleStream)
{
  if (url.isEmpty())
    return false;

  // A new request supersedes the old session. The previous IINA player is stopped first so two
  // windows cannot play at once, but its terminal signal is suppressed: Jellyfin Web has already
  // moved on to the new item and a late "stopped" would be attributed to the wrong one.
  if (!m_session.isTerminal() && m_ipc && m_ipc->isConnected())
    m_ipc->sendCommand(QJsonArray{QStringLiteral("stop")});
  teardownSession();

  if (!isAvailable())
  {
    m_session.begin();
    emitSignal(m_session.onFailed(), tr("IINA is not installed."));
    return false;
  }

  QString socketError;
  if (!prepareSocketDirectory(socketError))
  {
    m_session.begin();
    emitSignal(m_session.onFailed(), socketError);
    return false;
  }

  m_sessionId++;
  m_session.begin();
  m_positionMs = 0;
  m_durationMs = 0;
  m_socketPath = IinaLaunch::socketPath(QCoreApplication::applicationPid(), m_sessionId);

  // A stale socket from a crashed session would make us connect to nothing. Only ever the exact
  // path this process just generated is removed.
  QFile::remove(m_socketPath);

  IinaLaunch::MediaRequest request;
  request.url = url;
  request.ipcSocketPath = m_socketPath;
  request.startMilliseconds = options.value(QStringLiteral("startMilliseconds")).toLongLong();
  request.autoplay = options.value(QStringLiteral("autoplay"), true).toBool();
  request.userAgent = metadata.value(QStringLiteral("headers"))
                          .toMap()
                          .value(QStringLiteral("User-Agent"))
                          .toString();
  request.audioStream = audioStream;
  request.subtitleStream = subtitleStream;

  m_process = new QProcess(this);
  connect(m_process, &QProcess::errorOccurred, this, &IinaPlayerComponent::onProcessErrorOccurred);
  connect(m_process, &QProcess::finished, this,
          [this](int exitCode, QProcess::ExitStatus) { onProcessFinished(exitCode); });
  // iina-cli's output would echo the media path back at us; keep it out of the log entirely.
  m_process->setStandardOutputFile(QProcess::nullDevice());
  m_process->setStandardErrorFile(QProcess::nullDevice());

  // Arguments are passed as a list, never through a shell, so the URL is not subject to quoting.
  m_process->start(m_iina.cliPath, IinaLaunch::buildCliArguments(request));

  // A failure to start is reported synchronously; there is then nothing to connect to.
  if (m_session.isTerminal())
    return false;

  startSession();
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::startSession()
{
  m_ipc = new MpvIpcClient(this);
  connect(m_ipc, &MpvIpcClient::connected, this, &IinaPlayerComponent::onIpcConnected);
  connect(m_ipc, &MpvIpcClient::disconnected, this, &IinaPlayerComponent::onIpcDisconnected);
  connect(m_ipc, &MpvIpcClient::connectionFailed, this,
          &IinaPlayerComponent::onIpcConnectionFailed);
  connect(m_ipc, &MpvIpcClient::eventReceived, this, &IinaPlayerComponent::onIpcEvent);
  connect(m_ipc, &MpvIpcClient::propertyChanged, this, &IinaPlayerComponent::onIpcPropertyChanged);

  m_session.onConnecting();
  m_ipc->connectToSocket(m_socketPath, kIpcConnectTimeoutMs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::teardownSession()
{
  // Disconnecting before deleting guarantees no queued signal from the outgoing session can be
  // delivered against the incoming one.
  if (m_ipc)
  {
    m_ipc->disconnect(this);
    m_ipc->close();
    m_ipc->deleteLater();
    m_ipc = nullptr;
  }

  if (m_process)
  {
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;
  }

  if (!m_socketPath.isEmpty())
  {
    QFile::remove(m_socketPath);
    m_socketPath.clear();
  }

  m_session.reset();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::emitSignal(IinaSessionState::Signal signal, const QString& message)
{
  switch (signal)
  {
  case IinaSessionState::Signal::None:
    return;
  case IinaSessionState::Signal::Playing:
    emit playing();
    return;
  case IinaSessionState::Signal::Paused:
    emit paused();
    return;
  case IinaSessionState::Signal::Finished:
    emit finished();
    break;
  case IinaSessionState::Signal::Canceled:
    emit canceled();
    break;
  case IinaSessionState::Signal::Error:
    // The message never contains the media URL or any argument derived from it.
    qWarning() << "IINA playback failed:" << message;
    emit error(message);
    break;
  }

  // Only terminal signals fall through to here.
  teardownSession();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::onProcessErrorOccurred(QProcess::ProcessError processError)
{
  // Report the enum, not the process arguments: those contain the media URL.
  emitSignal(m_session.onFailed(),
             tr("Could not start IINA (error %1).").arg(static_cast<int>(processError)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::onProcessFinished(int exitCode)
{
  // iina-cli hands the media to IINA and exits immediately; that is the normal case and says
  // nothing about playback. Only a non-zero exit before the IPC channel came up is a failure.
  if (exitCode == 0 || m_session.state() != IinaSessionState::State::Connecting)
    return;

  emitSignal(m_session.onFailed(),
             tr("IINA refused the playback request (exit code %1).").arg(exitCode));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::onIpcConnectionFailed(const QString& reason)
{
  emitSignal(m_session.onFailed(), reason);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::onIpcConnected()
{
  m_session.onConnected();

  for (const char* property : {"time-pos", "duration", "pause", "speed", "volume", "mute",
                               "core-idle", "eof-reached", "demuxer-cache-state"})
  {
    m_ipc->observeProperty(QString::fromLatin1(property));
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::onIpcDisconnected()
{
  // IINA quit, or the player core was destroyed, without an end-file reaching us first.
  emitSignal(m_session.onDisconnected());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::onIpcEvent(const QString& name, const QJsonObject& event)
{
  if (name == QLatin1String("file-loaded") || name == QLatin1String("playback-restart"))
  {
    emitSignal(m_session.onFileLoaded());
    return;
  }

  if (name == QLatin1String("end-file"))
  {
    emitSignal(m_session.onEndFile(event.value("reason").toString()),
               tr("IINA could not play this item."));
    return;
  }

  if (name == QLatin1String("shutdown"))
  {
    emitSignal(m_session.onDisconnected());
    return;
  }

  // Unknown events are ignored on purpose so a newer mpv does not break playback.
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::onIpcPropertyChanged(const QString& name, const QJsonValue& value)
{
  if (name == QLatin1String("time-pos"))
  {
    if (!value.isDouble())
      return;
    m_positionMs = IinaLaunch::secondsToMilliseconds(value.toDouble());
    emit positionUpdate(static_cast<quint64>(m_positionMs));
    return;
  }

  if (name == QLatin1String("duration"))
  {
    if (!value.isDouble())
      return;
    m_durationMs = IinaLaunch::secondsToMilliseconds(value.toDouble());
    emit updateDuration(m_durationMs);
    return;
  }

  if (name == QLatin1String("pause"))
  {
    if (!value.isBool())
      return;

    emitSignal(m_session.onPauseChanged(value.toBool()));
    return;
  }

  if (name == QLatin1String("demuxer-cache-state"))
  {
    if (!value.isObject())
      return;

    // Jellyfin Web expects buffered ranges in 100ns ticks, the same as the built-in player.
    constexpr double ticksPerSecond = 10000000.0;
    QVariantList ranges;
    const QJsonArray seekable = value.toObject().value("seekable-ranges").toArray();
    for (const QJsonValue& entry : seekable)
    {
      const QJsonObject range = entry.toObject();
      QVariantMap converted;
      converted[QStringLiteral("start")] =
          static_cast<qint64>(jsonToDouble(range.value("start")) * ticksPerSecond);
      converted[QStringLiteral("end")] =
          static_cast<qint64>(jsonToDouble(range.value("end")) * ticksPerSecond);
      ranges.append(converted);
    }

    emit bufferedRangesUpdated(ranges);
    return;
  }

  // speed/volume/mute/core-idle/eof-reached are observed so that mpv keeps us in sync, but the
  // web client is the authority for them and does not need an echo.
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::play()
{
  if (m_ipc)
    m_ipc->setProperty(QStringLiteral("pause"), false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::pause()
{
  if (m_ipc)
    m_ipc->setProperty(QStringLiteral("pause"), true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::stop()
{
  if (m_session.isTerminal())
    return;

  if (m_ipc && m_ipc->isConnected())
  {
    // Closing the window is IINA's job; "stop" only ends the current media and leaves the user's
    // application in whatever state they had it.
    m_ipc->sendCommand(QJsonArray{QStringLiteral("stop")});
  }

  emitSignal(m_session.requestStop());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::seekTo(qint64 milliseconds)
{
  if (!m_ipc)
    return;

  m_ipc->sendCommand(QJsonArray{QStringLiteral("seek"), milliseconds / 1000.0,
                                QStringLiteral("absolute")});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::setVolume(int volume)
{
  if (m_ipc)
    m_ipc->setProperty(QStringLiteral("volume"), qBound(0, volume, 100));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::setMuted(bool muted)
{
  if (m_ipc)
    m_ipc->setProperty(QStringLiteral("mute"), muted);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::setPlaybackRate(int milliRate)
{
  if (m_ipc && milliRate > 0)
    m_ipc->setProperty(QStringLiteral("speed"), milliRate / 1000.0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::setAudioStream(const QVariant& stream)
{
  if (!m_ipc)
    return;

  const QString aid = IinaLaunch::trackArgument(stream);
  if (aid.isEmpty())
    return;

  m_ipc->setProperty(QStringLiteral("aid"), aid);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::setSubtitleStream(const QVariant& stream)
{
  if (!m_ipc)
    return;

  // Only tracks carried by the media itself are selectable. Direct Play delivers the original
  // file, so every subtitle the server reports is already in the container; sidecar subtitle files
  // on the server are out of scope, and a local one is dragged straight into IINA.
  const QString sid = IinaLaunch::trackArgument(stream);
  if (sid.isEmpty())
    return;

  m_ipc->setProperty(QStringLiteral("sid"), sid);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaPlayerComponent::setSubtitleDelay(qint64 milliseconds)
{
  if (m_ipc)
    m_ipc->setProperty(QStringLiteral("sub-delay"), milliseconds / 1000.0);
}
