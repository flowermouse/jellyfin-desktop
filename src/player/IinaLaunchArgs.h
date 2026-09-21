#ifndef IINALAUNCHARGS_H
#define IINALAUNCHARGS_H

#include <QString>
#include <QStringList>
#include <QVariant>

///////////////////////////////////////////////////////////////////////////////////////////////////
// Pure helpers translating a Jellyfin playback request into the iina-cli command line. They
// deliberately hold no process, socket or UI state so that the encoding rules can be unit tested
// without IINA being installed.
//
// iina-cli is the only supported launcher. The iina:// URL scheme cannot carry
// input-ipc-server - IINA safelists the mpv options it accepts from a URL - so it could start
// playback but never report any of it back.
//
// Unit boundary: everything crossing the WebChannel is in milliseconds, everything handed to mpv
// is in seconds. The conversion lives here and nowhere else.
namespace IinaLaunch
{
  struct MediaRequest
  {
    QString url;
    QString ipcSocketPath;
    QString userAgent;
    qint64 startMilliseconds = 0;
    bool autoplay = true;
    bool fullscreen = false;
    bool pictureInPicture = false;

    // Encoded the same way PlayerComponent::reselectStream() expects it: an int is an mpv track id
    // (negative disables the track), "#<id>" selects an embedded track and "#,<url>" refers to an
    // external file. An invalid QVariant means "leave IINA's own default alone".
    QVariant audioStream;
    QVariant subtitleStream;
  };

  QString secondsArgument(qint64 milliseconds);
  qint64 secondsToMilliseconds(double seconds);

  // mpv aid/sid value for a stream selection, or an empty string when the selection carries no
  // usable track id (for example a purely external subtitle, which has to be added over IPC).
  QString trackArgument(const QVariant& stream);

  // The external file referenced by a "#,<url>" selection, or an empty string. Used to reject
  // such selections: only tracks carried by the media itself are supported, and the URL would
  // otherwise expose an access token on the command line.
  QString externalTrackUrl(const QVariant& stream);

  // Arguments for iina-cli, one element per argv entry. Never goes through a shell.
  QStringList buildCliArguments(const MediaRequest& request);

  // Per-session mpv IPC socket. Kept under a short private directory because macOS limits Unix
  // domain socket paths to ~104 bytes.
  QString socketDirectory(qint64 pid);
  QString socketPath(qint64 pid, quint64 sessionId);
}

#endif // IINALAUNCHARGS_H
