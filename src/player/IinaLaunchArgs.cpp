#include "IinaLaunchArgs.h"

#include <cmath>

namespace
{
  ///////////////////////////////////////////////////////////////////////////////////////////////
  // IINA's own argument parser splits "--mpv-<name>=<value>" on the first '=' only, so values
  // containing '=' (base64 tokens in query strings, for example) survive unharmed.
  QString mpvArgument(const QString& name, const QString& value)
  {
    return QStringLiteral("--mpv-%1=%2").arg(name, value);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString IinaLaunch::secondsArgument(qint64 milliseconds)
{
  if (milliseconds <= 0)
    return QString();

  // Three decimals is the finest resolution the millisecond input can express. Trailing zeroes are
  // dropped so whole seconds read as "42" rather than "42.000".
  QString text = QString::number(milliseconds / 1000.0, 'f', 3);
  while (text.endsWith('0'))
    text.chop(1);
  if (text.endsWith('.'))
    text.chop(1);

  return text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
qint64 IinaLaunch::secondsToMilliseconds(double seconds)
{
  if (!std::isfinite(seconds) || seconds <= 0)
    return 0;

  return static_cast<qint64>(std::llround(seconds * 1000.0));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString IinaLaunch::trackArgument(const QVariant& stream)
{
  if (!stream.isValid() || stream.isNull())
    return QString();

  // Integers come straight from the web adapter's relative-index mapping.
  bool isNumber = false;
  const int index = stream.toInt(&isNumber);
  if (isNumber && stream.typeId() != QMetaType::QString)
    return index < 0 ? QStringLiteral("no") : QString::number(index);

  const QString text = stream.toString();
  if (text.isEmpty())
    return QStringLiteral("no");

  if (!text.startsWith('#'))
  {
    // Plain numeric string, e.g. "2".
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (ok)
      return parsed < 0 ? QStringLiteral("no") : QString::number(parsed);
    return QString();
  }

  const qsizetype comma = text.indexOf(',');
  const QString id = comma < 0 ? text.mid(1) : text.mid(1, comma - 1);
  if (id.isEmpty())
    return QString(); // External file without an id: has to be resolved after it is added.

  return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString IinaLaunch::externalTrackUrl(const QVariant& stream)
{
  if (!stream.isValid() || stream.typeId() != QMetaType::QString)
    return QString();

  const QString text = stream.toString();
  if (!text.startsWith('#'))
    return QString();

  const qsizetype comma = text.indexOf(',');
  if (comma < 0)
    return QString();

  return text.mid(comma + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QStringList IinaLaunch::buildCliArguments(const MediaRequest& request)
{
  QStringList args;

  // Never let iina-cli block on or consume our stdin, and always give the media its own window so
  // an unrelated IINA window is not hijacked.
  args << QStringLiteral("--no-stdin");
  args << QStringLiteral("--separate-windows");

  if (request.pictureInPicture)
    args << QStringLiteral("--pip");

  if (!request.ipcSocketPath.isEmpty())
    args << mpvArgument(QStringLiteral("input-ipc-server"), request.ipcSocketPath);

  const QString start = secondsArgument(request.startMilliseconds);
  if (!start.isEmpty())
    args << mpvArgument(QStringLiteral("start"), start);

  if (!request.autoplay)
    args << mpvArgument(QStringLiteral("pause"), QStringLiteral("yes"));

  if (request.fullscreen)
    args << mpvArgument(QStringLiteral("fullscreen"), QStringLiteral("yes"));

  if (!request.userAgent.isEmpty())
    args << mpvArgument(QStringLiteral("user-agent"), request.userAgent);

  const QString aid = trackArgument(request.audioStream);
  if (!aid.isEmpty())
    args << mpvArgument(QStringLiteral("aid"), aid);

  // Only tracks inside the media itself are selectable. A "#,<url>" selection would put an
  // authenticated subtitle URL on the command line, so it is dropped rather than passed through.
  if (externalTrackUrl(request.subtitleStream).isEmpty())
  {
    const QString sid = trackArgument(request.subtitleStream);
    if (!sid.isEmpty())
      args << mpvArgument(QStringLiteral("sid"), sid);
  }

  // The media URL must be last: IINA treats every non-option argument as a file name.
  args << request.url;

  return args;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString IinaLaunch::socketDirectory(qint64 pid)
{
  // Intentionally not QDir::tempPath(): on macOS that expands to a long per-user /var/folders path
  // which would push the socket past the sun_path limit. The directory is created with owner-only
  // permissions by the caller.
  return QStringLiteral("/tmp/jfd-iina-%1").arg(pid);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QString IinaLaunch::socketPath(qint64 pid, quint64 sessionId)
{
  return QStringLiteral("%1/%2.sock").arg(socketDirectory(pid)).arg(sessionId);
}
