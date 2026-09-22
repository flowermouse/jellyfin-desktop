#include <QtTest/QtTest>

#include "../src/player/IinaLaunchArgs.h"
#include "../src/player/IinaSessionState.h"

// A realistic Jellyfin playback URL: it carries an access token, query delimiters and a
// pre-existing percent escape, none of which may be altered on the way to argv.
static const char* const kMediaUrl =
    "https://media.example.com:8096/Videos/abc/stream.mkv"
    "?api_key=0123456789abcdef0123456789abcdef&static=true&name=a+b%20c";

class TestIina : public QObject
{
  Q_OBJECT

private slots:
  void testSecondsArgument_data();
  void testSecondsArgument();
  void testSecondsToMilliseconds_data();
  void testSecondsToMilliseconds();
  void testTrackArgument_data();
  void testTrackArgument();
  void testExternalTrackUrl();
  void testCliArgumentsAreSeparateElements();
  void testCliArgumentsOmitUnsetOptions();
  void testCliArgumentsKeepExternalSubtitleOffCommandLine();
  void testCliArgumentsCarryTheDisplayTitle();
  void testSocketPathIsShortAndUnique();

  void testSessionEmitsPlayingOnlyAfterFileLoaded();
  void testSessionEndFileReasonMapping_data();
  void testSessionEndFileReasonMapping();
  void testSessionEmitsTerminalSignalOnce();
  void testSessionIgnoresLateEventsAfterTermination();
  void testSessionPauseToggling();
  void testSessionRedirectIsNotTerminal();
};

///////////////////////////////////////////////////////////////////////////////
// Millisecond <-> second boundary
///////////////////////////////////////////////////////////////////////////////

void TestIina::testSecondsArgument_data()
{
  QTest::addColumn<qint64>("milliseconds");
  QTest::addColumn<QString>("expected");

  QTest::newRow("zero")          << qint64(0)       << QString();
  QTest::newRow("negative")      << qint64(-5000)   << QString();
  QTest::newRow("whole second")  << qint64(42000)   << QStringLiteral("42");
  QTest::newRow("sub second")    << qint64(1500)    << QStringLiteral("1.5");
  QTest::newRow("millisecond")   << qint64(1001)    << QStringLiteral("1.001");
  QTest::newRow("long resume")   << qint64(3723456) << QStringLiteral("3723.456");
}

void TestIina::testSecondsArgument()
{
  QFETCH(qint64, milliseconds);
  QFETCH(QString, expected);

  QCOMPARE(IinaLaunch::secondsArgument(milliseconds), expected);
}

void TestIina::testSecondsToMilliseconds_data()
{
  QTest::addColumn<double>("seconds");
  QTest::addColumn<qint64>("expected");

  QTest::newRow("zero")      << 0.0      << qint64(0);
  QTest::newRow("negative")  << -1.0     << qint64(0);
  QTest::newRow("whole")     << 42.0     << qint64(42000);
  QTest::newRow("rounding")  << 1.0004   << qint64(1000);
  QTest::newRow("round up")  << 1.0006   << qint64(1001);
  QTest::newRow("nan")       << qQNaN()  << qint64(0);
  QTest::newRow("infinity")  << qInf()   << qint64(0);
}

void TestIina::testSecondsToMilliseconds()
{
  QFETCH(double, seconds);
  QFETCH(qint64, expected);

  QCOMPARE(IinaLaunch::secondsToMilliseconds(seconds), expected);
}

///////////////////////////////////////////////////////////////////////////////
// Track selection
///////////////////////////////////////////////////////////////////////////////

void TestIina::testTrackArgument_data()
{
  QTest::addColumn<QVariant>("stream");
  QTest::addColumn<QString>("expected");

  QTest::newRow("unset")            << QVariant()                        << QString();
  QTest::newRow("relative index")   << QVariant(2)                       << QStringLiteral("2");
  QTest::newRow("disabled")         << QVariant(-1)                      << QStringLiteral("no");
  QTest::newRow("numeric string")   << QVariant(QStringLiteral("3"))     << QStringLiteral("3");
  QTest::newRow("embedded hash")    << QVariant(QStringLiteral("#4"))    << QStringLiteral("4");
  QTest::newRow("external only")    << QVariant(QStringLiteral("#,https://x/s.srt")) << QString();
  QTest::newRow("external with id") << QVariant(QStringLiteral("#2,https://x/s.srt"))
                                    << QStringLiteral("2");
  QTest::newRow("empty string")     << QVariant(QStringLiteral(""))      << QStringLiteral("no");
}

void TestIina::testTrackArgument()
{
  QFETCH(QVariant, stream);
  QFETCH(QString, expected);

  QCOMPARE(IinaLaunch::trackArgument(stream), expected);
}

void TestIina::testExternalTrackUrl()
{
  QCOMPARE(IinaLaunch::externalTrackUrl(QVariant(QStringLiteral("#,https://x/s.srt"))),
           QStringLiteral("https://x/s.srt"));
  QCOMPARE(IinaLaunch::externalTrackUrl(QVariant(QStringLiteral("#2"))), QString());
  QCOMPARE(IinaLaunch::externalTrackUrl(QVariant(2)), QString());
  QCOMPARE(IinaLaunch::externalTrackUrl(QVariant()), QString());
}

///////////////////////////////////////////////////////////////////////////////
// iina-cli arguments
///////////////////////////////////////////////////////////////////////////////

void TestIina::testCliArgumentsAreSeparateElements()
{
  IinaLaunch::MediaRequest request;
  request.url = QString::fromLatin1(kMediaUrl);
  request.ipcSocketPath = QStringLiteral("/tmp/jfd-iina-1234/7.sock");
  request.startMilliseconds = 90500;
  request.userAgent = QStringLiteral("Jellyfin Desktop/1.0 (macOS)");
  request.audioStream = QVariant(2);
  request.subtitleStream = QVariant(1);

  const QStringList args = IinaLaunch::buildCliArguments(request);

  QCOMPARE(args, QStringList({
                     QStringLiteral("--no-stdin"),
                     QStringLiteral("--separate-windows"),
                     QStringLiteral("--mpv-input-ipc-server=/tmp/jfd-iina-1234/7.sock"),
                     QStringLiteral("--mpv-start=90.5"),
                     QStringLiteral("--mpv-user-agent=Jellyfin Desktop/1.0 (macOS)"),
                     QStringLiteral("--mpv-aid=2"),
                     QStringLiteral("--mpv-sid=1"),
                     QString::fromLatin1(kMediaUrl),
                 }));

  // The URL must be a single argv element with no quoting applied: it never passes through a
  // shell, so any quoting would become part of the URL.
  QCOMPARE(args.last(), QString::fromLatin1(kMediaUrl));
  // A user agent containing spaces must not have been split.
  QVERIFY(args.contains(QStringLiteral("--mpv-user-agent=Jellyfin Desktop/1.0 (macOS)")));
}

void TestIina::testCliArgumentsOmitUnsetOptions()
{
  IinaLaunch::MediaRequest request;
  request.url = QStringLiteral("https://example.com/a.mkv");

  const QStringList args = IinaLaunch::buildCliArguments(request);

  QCOMPARE(args, QStringList({
                     QStringLiteral("--no-stdin"),
                     QStringLiteral("--separate-windows"),
                     QStringLiteral("https://example.com/a.mkv"),
                 }));

  request.autoplay = false;
  QVERIFY(IinaLaunch::buildCliArguments(request).contains(QStringLiteral("--mpv-pause=yes")));
}

void TestIina::testCliArgumentsKeepExternalSubtitleOffCommandLine()
{
  IinaLaunch::MediaRequest request;
  request.url = QStringLiteral("https://example.com/a.mkv");
  request.subtitleStream =
      QVariant(QStringLiteral("#,https://media.example.com/Subtitles/1?api_key=secrettoken"));

  const QStringList args = IinaLaunch::buildCliArguments(request);

  for (const QString& arg : args)
    QVERIFY2(!arg.contains(QStringLiteral("secrettoken")),
             "an external subtitle URL must never reach the command line");
}

void TestIina::testCliArgumentsCarryTheDisplayTitle()
{
  IinaLaunch::MediaRequest request;
  request.url = QString::fromLatin1(kMediaUrl);
  // Spaces, a dash and non-ASCII all have to survive as one argv element.
  request.title = QStringLiteral("Le Samoura\u00EF - S01E02 - L'\u00E9v\u00E9nement");

  const QStringList args = IinaLaunch::buildCliArguments(request);

  QVERIFY(args.contains(
      QStringLiteral("--mpv-force-media-title=Le Samoura\u00EF - S01E02 - L'\u00E9v\u00E9nement")));

  // An untitled item leaves IINA's own naming alone rather than blanking the window title.
  request.title.clear();
  for (const QString& arg : IinaLaunch::buildCliArguments(request))
    QVERIFY(!arg.startsWith(QStringLiteral("--mpv-force-media-title")));
}

void TestIina::testSocketPathIsShortAndUnique()
{
  const QString first = IinaLaunch::socketPath(12345, 1);
  const QString second = IinaLaunch::socketPath(12345, 2);

  QVERIFY(first != second);
  QVERIFY(first.startsWith(IinaLaunch::socketDirectory(12345) + QLatin1Char('/')));
  // macOS caps sun_path at 104 bytes; stay well clear of it.
  QVERIFY2(first.toUtf8().size() < 100, qPrintable(QString::number(first.toUtf8().size())));
}

///////////////////////////////////////////////////////////////////////////////
// Session state machine
///////////////////////////////////////////////////////////////////////////////

void TestIina::testSessionEmitsPlayingOnlyAfterFileLoaded()
{
  IinaSessionState session;
  session.begin();
  session.onConnecting();

  // Connecting alone is not playback.
  QCOMPARE(session.onConnected(), IinaSessionState::Signal::None);

  QCOMPARE(session.onFileLoaded(), IinaSessionState::Signal::Playing);
  QCOMPARE(session.state(), IinaSessionState::State::Playing);

  // playback-restart after a seek must not re-announce playback.
  QCOMPARE(session.onFileLoaded(), IinaSessionState::Signal::None);
}

void TestIina::testSessionEndFileReasonMapping_data()
{
  QTest::addColumn<QString>("reason");
  QTest::addColumn<int>("expected");

  QTest::newRow("eof")     << QStringLiteral("eof")
                           << int(IinaSessionState::Signal::Finished);
  QTest::newRow("stop")    << QStringLiteral("stop")
                           << int(IinaSessionState::Signal::Canceled);
  QTest::newRow("quit")    << QStringLiteral("quit")
                           << int(IinaSessionState::Signal::Canceled);
  QTest::newRow("error")   << QStringLiteral("error")
                           << int(IinaSessionState::Signal::Error);
  QTest::newRow("unknown") << QStringLiteral("something-new")
                           << int(IinaSessionState::Signal::Canceled);
  QTest::newRow("empty")   << QString()
                           << int(IinaSessionState::Signal::Canceled);
}

void TestIina::testSessionEndFileReasonMapping()
{
  QFETCH(QString, reason);
  QFETCH(int, expected);

  IinaSessionState session;
  session.begin();
  session.onConnecting();
  session.onConnected();
  session.onFileLoaded();

  QCOMPARE(int(session.onEndFile(reason)), expected);
  QVERIFY(session.isTerminal());
}

void TestIina::testSessionEmitsTerminalSignalOnce()
{
  IinaSessionState session;
  session.begin();
  session.onConnecting();
  session.onConnected();
  session.onFileLoaded();

  QCOMPARE(session.onEndFile(QStringLiteral("eof")), IinaSessionState::Signal::Finished);

  // mpv follows end-file with idle and then, on quit, shutdown; the socket drops afterwards.
  // None of that may produce a second terminal signal.
  QCOMPARE(session.onEndFile(QStringLiteral("eof")), IinaSessionState::Signal::None);
  QCOMPARE(session.onDisconnected(), IinaSessionState::Signal::None);
  QCOMPARE(session.requestStop(), IinaSessionState::Signal::None);
  QCOMPARE(session.onFailed(), IinaSessionState::Signal::None);
}

void TestIina::testSessionIgnoresLateEventsAfterTermination()
{
  IinaSessionState session;
  session.begin();
  session.onConnecting();
  session.onConnected();
  session.onFileLoaded();
  QCOMPARE(session.requestStop(), IinaSessionState::Signal::Canceled);

  // Events still queued from the superseded session's socket must not resurrect it.
  QCOMPARE(session.onFileLoaded(), IinaSessionState::Signal::None);
  QCOMPARE(session.onPauseChanged(true), IinaSessionState::Signal::None);
  QCOMPARE(session.onPauseChanged(false), IinaSessionState::Signal::None);
  QCOMPARE(session.state(), IinaSessionState::State::Idle);

  // A fresh session starts clean.
  session.begin();
  session.onConnecting();
  session.onConnected();
  QCOMPARE(session.onFileLoaded(), IinaSessionState::Signal::Playing);
}

void TestIina::testSessionPauseToggling()
{
  IinaSessionState session;
  session.begin();
  session.onConnecting();
  session.onConnected();
  session.onFileLoaded();

  QCOMPARE(session.onPauseChanged(true), IinaSessionState::Signal::Paused);
  // mpv re-reports the property on seek; a repeated value must not emit again.
  QCOMPARE(session.onPauseChanged(true), IinaSessionState::Signal::None);
  QCOMPARE(session.onPauseChanged(false), IinaSessionState::Signal::Playing);
  QCOMPARE(session.onPauseChanged(false), IinaSessionState::Signal::None);
}

void TestIina::testSessionRedirectIsNotTerminal()
{
  IinaSessionState session;
  session.begin();
  session.onConnecting();
  session.onConnected();
  session.onFileLoaded();

  QCOMPARE(session.onEndFile(QStringLiteral("redirect")), IinaSessionState::Signal::None);
  QVERIFY(!session.isTerminal());
  QCOMPARE(session.state(), IinaSessionState::State::Playing);
}

QTEST_APPLESS_MAIN(TestIina)
#include "test_iina.moc"
