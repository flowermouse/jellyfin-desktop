#ifndef IINAPLAYERCOMPONENT_H
#define IINAPLAYERCOMPONENT_H

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "ComponentManager.h"
#include "IinaSessionState.h"
#include "osx/IinaApplication.h"

class MpvIpcClient;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Plays video in the user's own installation of the official IINA application instead of in this
// window, and mirrors IINA's playback state back to Jellyfin Web over the WebChannel.
//
// Deliberately *not* derived from PlayerComponent: that class owns a window, an MpvQt render loop
// and local playback state, none of which apply to a separate application. Only the small subset
// of its interface that the web video adapter actually calls is reproduced here.
//
// Lifecycle of one playback session:
//
//   Idle -> Launching -> Connecting -> Loading -> Playing <-> Paused
//                                              -> Finished | Canceled | Error -> Idle
//
// Exactly one terminal signal (finished/canceled/error) is emitted per session, and late events
// arriving from a superseded session are discarded.
class IinaPlayerComponent : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(IinaPlayerComponent);

  // Lets JavaScript disable the local video surface, OSD takeover and video rectangle handling
  // without testing the platform itself.
  Q_PROPERTY(bool externalPlayback READ externalPlayback CONSTANT)
  // Readable synchronously from JavaScript, unlike the Q_INVOKABLE form: WebChannel invocations
  // only deliver their result through a callback, which is awkward on the playback start path.
  Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)

public:
  explicit IinaPlayerComponent(QObject* parent = nullptr);
  ~IinaPlayerComponent() override;

  const char* componentName() override { return "iinaPlayer"; }
  bool componentExport() override { return true; }
  bool componentInitialize() override;

  bool externalPlayback() const { return true; }

  // Discovery
  Q_INVOKABLE bool isAvailable() const;
  Q_INVOKABLE QString applicationPath() const;
  Q_INVOKABLE QString applicationVersion() const;
  Q_INVOKABLE void openDownloadPage();

  // Playback. Units crossing this boundary are always milliseconds.
  Q_INVOKABLE bool load(const QString& url,
                        const QVariantMap& options,
                        const QVariantMap& metadata,
                        const QVariant& audioStream = QVariant(),
                        const QVariant& subtitleStream = QVariant());
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void seekTo(qint64 milliseconds);
  Q_INVOKABLE void setVolume(int volume);
  Q_INVOKABLE void setMuted(bool muted);
  // `milliRate` is the playback rate multiplied by 1000, matching PlayerComponent.
  Q_INVOKABLE void setPlaybackRate(int milliRate);
  Q_INVOKABLE void setAudioStream(const QVariant& stream);
  Q_INVOKABLE void setSubtitleStream(const QVariant& stream);
  Q_INVOKABLE void setSubtitleDelay(qint64 milliseconds);
  Q_INVOKABLE qint64 getPosition() const { return m_positionMs; }
  Q_INVOKABLE qint64 getDuration() const { return m_durationMs; }

signals:
  void playing();
  void paused();
  void finished();
  void canceled();
  void error(const QString& message);
  void positionUpdate(quint64 milliseconds);
  void updateDuration(qint64 milliseconds);
  void bufferedRangesUpdated(const QVariantList& ranges);
  void availabilityChanged(bool available);

private slots:
  void onIpcConnected();
  void onIpcDisconnected();
  void onIpcConnectionFailed(const QString& reason);
  void onIpcEvent(const QString& name, const QJsonObject& event);
  void onIpcPropertyChanged(const QString& name, const QJsonValue& value);
  void onProcessErrorOccurred(QProcess::ProcessError processError);
  void onProcessFinished(int exitCode);

private:
  void startSession();
  void teardownSession();
  // Emits the signal the state machine produced, then tears the session down when it was a
  // terminal one. `message` is only used for Signal::Error.
  void emitSignal(IinaSessionState::Signal signal, const QString& message = QString());
  bool prepareSocketDirectory(QString& errorOut);

  IinaApplication::Info m_iina;

  MpvIpcClient* m_ipc = nullptr;
  QProcess* m_process = nullptr;

  IinaSessionState m_session;
  quint64 m_sessionId = 0;

  QString m_socketPath;

  qint64 m_positionMs = 0;
  qint64 m_durationMs = 0;
};

#endif // IINAPLAYERCOMPONENT_H
