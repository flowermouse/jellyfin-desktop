#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../src/player/MpvIpcClient.h"

class TestMpvIpc : public QObject
{
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // Framing (pure)
  void testExtractsSingleMessage();
  void testExtractsBatchedMessages();
  void testKeepsPartialMessageBuffered();
  void testSkipsMalformedAndBlankLines();
  void testEncodeCommandCarriesRequestId();

  // Transport, against a real local socket server (no IINA needed)
  void testConnectsAfterSocketAppears();
  void testFailsWhenSocketNeverAppears();
  void testDispatchesEventsPropertiesAndResponses();
  void testInterleavedResponsesAndEvents();
  void testReportsDisconnect();
  void testRequestIdsIncrement();

private:
  QTemporaryDir* m_dir = nullptr;
  QString m_path;
};

void TestMpvIpc::init()
{
  m_dir = new QTemporaryDir();
  QVERIFY(m_dir->isValid());
  m_path = m_dir->filePath(QStringLiteral("s.sock"));
}

void TestMpvIpc::cleanup()
{
  delete m_dir;
  m_dir = nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// Framing
///////////////////////////////////////////////////////////////////////////////

void TestMpvIpc::testExtractsSingleMessage()
{
  QByteArray buffer = "{\"event\":\"file-loaded\"}\n";
  const QVector<QJsonObject> messages = MpvIpcProtocol::extractMessages(buffer);

  QCOMPARE(messages.size(), 1);
  QCOMPARE(messages.at(0).value("event").toString(), QStringLiteral("file-loaded"));
  QVERIFY(buffer.isEmpty());
}

void TestMpvIpc::testExtractsBatchedMessages()
{
  // mpv happily packs several messages into one read.
  QByteArray buffer = "{\"event\":\"start-file\"}\n"
                      "{\"event\":\"property-change\",\"name\":\"time-pos\",\"data\":1.5}\n"
                      "{\"event\":\"file-loaded\"}\n";

  const QVector<QJsonObject> messages = MpvIpcProtocol::extractMessages(buffer);

  QCOMPARE(messages.size(), 3);
  QCOMPARE(messages.at(1).value("name").toString(), QStringLiteral("time-pos"));
  QCOMPARE(messages.at(1).value("data").toDouble(), 1.5);
  QVERIFY(buffer.isEmpty());
}

void TestMpvIpc::testKeepsPartialMessageBuffered()
{
  QByteArray buffer = "{\"event\":\"file-loaded\"}\n{\"event\":\"pla";
  QVector<QJsonObject> messages = MpvIpcProtocol::extractMessages(buffer);

  QCOMPARE(messages.size(), 1);
  QCOMPARE(buffer, QByteArray("{\"event\":\"pla"));

  // The rest of the message arrives in a later read.
  buffer.append("yback-restart\"}\n");
  messages = MpvIpcProtocol::extractMessages(buffer);

  QCOMPARE(messages.size(), 1);
  QCOMPARE(messages.at(0).value("event").toString(), QStringLiteral("playback-restart"));
  QVERIFY(buffer.isEmpty());
}

void TestMpvIpc::testSkipsMalformedAndBlankLines()
{
  QByteArray buffer = "\n"
                      "not json\n"
                      "[1,2,3]\n"
                      "{\"event\":\"idle\"}\n";

  const QVector<QJsonObject> messages = MpvIpcProtocol::extractMessages(buffer);

  QCOMPARE(messages.size(), 1);
  QCOMPARE(messages.at(0).value("event").toString(), QStringLiteral("idle"));
  QVERIFY(buffer.isEmpty());
}

void TestMpvIpc::testEncodeCommandCarriesRequestId()
{
  const QByteArray encoded =
      MpvIpcProtocol::encodeCommand(QJsonArray{QStringLiteral("set_property"),
                                               QStringLiteral("pause"), true},
                                    7);

  QVERIFY(encoded.endsWith('\n'));

  const QJsonObject object = QJsonDocument::fromJson(encoded).object();
  QCOMPARE(object.value("request_id").toInt(), 7);
  QCOMPARE(object.value("command").toArray().at(1).toString(), QStringLiteral("pause"));
}

///////////////////////////////////////////////////////////////////////////////
// Transport
///////////////////////////////////////////////////////////////////////////////

void TestMpvIpc::testConnectsAfterSocketAppears()
{
  MpvIpcClient client;
  QSignalSpy connectedSpy(&client, &MpvIpcClient::connected);
  QSignalSpy failedSpy(&client, &MpvIpcClient::connectionFailed);

  // The socket does not exist yet, exactly as when IINA has not finished launching.
  client.connectToSocket(m_path, 5000);
  QTest::qWait(250);
  QCOMPARE(connectedSpy.count(), 0);

  QLocalServer server;
  QVERIFY(server.listen(m_path));

  QVERIFY(connectedSpy.wait(3000));
  QCOMPARE(failedSpy.count(), 0);
  QVERIFY(client.isConnected());
}

void TestMpvIpc::testFailsWhenSocketNeverAppears()
{
  MpvIpcClient client;
  QSignalSpy failedSpy(&client, &MpvIpcClient::connectionFailed);

  client.connectToSocket(m_path, 300);

  QVERIFY(failedSpy.wait(3000));
  QCOMPARE(failedSpy.count(), 1);
  QVERIFY(!client.isConnected());
}

void TestMpvIpc::testDispatchesEventsPropertiesAndResponses()
{
  QLocalServer server;
  QVERIFY(server.listen(m_path));

  MpvIpcClient client;
  QSignalSpy connectedSpy(&client, &MpvIpcClient::connected);
  QSignalSpy eventSpy(&client, &MpvIpcClient::eventReceived);
  QSignalSpy propertySpy(&client, &MpvIpcClient::propertyChanged);
  QSignalSpy responseSpy(&client, &MpvIpcClient::commandResponse);

  client.connectToSocket(m_path, 3000);
  // Connecting to an already-listening socket can complete synchronously, so poll rather than
  // waiting for an emission that may already have happened.
  QTRY_VERIFY(client.isConnected());
  QCOMPARE(connectedSpy.count(), 1);
  QVERIFY(server.waitForNewConnection(3000));

  QLocalSocket* peer = server.nextPendingConnection();
  QVERIFY(peer);

  peer->write("{\"event\":\"end-file\",\"reason\":\"eof\"}\n"
              "{\"event\":\"property-change\",\"name\":\"duration\",\"data\":3600.5}\n"
              "{\"error\":\"success\",\"data\":null,\"request_id\":4}\n");
  QVERIFY(peer->waitForBytesWritten(3000));

  QTRY_COMPARE(eventSpy.count(), 1);
  QTRY_COMPARE(propertySpy.count(), 1);
  QTRY_COMPARE(responseSpy.count(), 1);

  QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("end-file"));
  QCOMPARE(eventSpy.at(0).at(1).toJsonObject().value("reason").toString(), QStringLiteral("eof"));
  QCOMPARE(propertySpy.at(0).at(0).toString(), QStringLiteral("duration"));
  QCOMPARE(responseSpy.at(0).at(0).toLongLong(), qint64(4));
  QCOMPARE(responseSpy.at(0).at(1).toBool(), true);
}

void TestMpvIpc::testInterleavedResponsesAndEvents()
{
  QLocalServer server;
  QVERIFY(server.listen(m_path));

  MpvIpcClient client;
  QSignalSpy connectedSpy(&client, &MpvIpcClient::connected);
  QSignalSpy eventSpy(&client, &MpvIpcClient::eventReceived);
  QSignalSpy responseSpy(&client, &MpvIpcClient::commandResponse);

  client.connectToSocket(m_path, 3000);
  // Connecting to an already-listening socket can complete synchronously, so poll rather than
  // waiting for an emission that may already have happened.
  QTRY_VERIFY(client.isConnected());
  QCOMPARE(connectedSpy.count(), 1);
  QVERIFY(server.waitForNewConnection(3000));
  QLocalSocket* peer = server.nextPendingConnection();
  QVERIFY(peer);

  // Two commands in flight; their answers come back out of order and with an event wedged in
  // between, split across two writes so the second response also crosses a read boundary.
  QCOMPARE(client.sendCommand(QJsonArray{QStringLiteral("get_property"),
                                         QStringLiteral("time-pos")}),
           qint64(1));
  QCOMPARE(client.sendCommand(QJsonArray{QStringLiteral("get_property"),
                                         QStringLiteral("duration")}),
           qint64(2));

  peer->write("{\"error\":\"success\",\"data\":12.5,\"request_id\":2}\n"
              "{\"event\":\"seek\"}\n"
              "{\"error\":\"property unavailable\",\"data\":null,\"reques");
  QVERIFY(peer->waitForBytesWritten(3000));
  QTRY_COMPARE(responseSpy.count(), 1);

  peer->write("t_id\":1}\n");
  QVERIFY(peer->waitForBytesWritten(3000));

  QTRY_COMPARE(responseSpy.count(), 2);
  QCOMPARE(eventSpy.count(), 1);

  QCOMPARE(responseSpy.at(0).at(0).toLongLong(), qint64(2));
  QCOMPARE(responseSpy.at(0).at(1).toBool(), true);
  QCOMPARE(responseSpy.at(1).at(0).toLongLong(), qint64(1));
  QCOMPARE(responseSpy.at(1).at(1).toBool(), false);
}

void TestMpvIpc::testReportsDisconnect()
{
  QLocalServer server;
  QVERIFY(server.listen(m_path));

  MpvIpcClient client;
  QSignalSpy connectedSpy(&client, &MpvIpcClient::connected);
  QSignalSpy disconnectedSpy(&client, &MpvIpcClient::disconnected);

  client.connectToSocket(m_path, 3000);
  // Connecting to an already-listening socket can complete synchronously, so poll rather than
  // waiting for an emission that may already have happened.
  QTRY_VERIFY(client.isConnected());
  QCOMPARE(connectedSpy.count(), 1);
  QVERIFY(server.waitForNewConnection(3000));
  QLocalSocket* peer = server.nextPendingConnection();
  QVERIFY(peer);

  // IINA quitting looks like this from our side.
  peer->disconnectFromServer();
  server.close();

  QVERIFY(disconnectedSpy.wait(3000));
  QCOMPARE(disconnectedSpy.count(), 1);
  QVERIFY(!client.isConnected());
}

void TestMpvIpc::testRequestIdsIncrement()
{
  QLocalServer server;
  QVERIFY(server.listen(m_path));

  MpvIpcClient client;
  QSignalSpy connectedSpy(&client, &MpvIpcClient::connected);

  // Not connected yet: commands are rejected rather than silently dropped.
  QCOMPARE(client.sendCommand(QJsonArray{QStringLiteral("stop")}), qint64(-1));

  client.connectToSocket(m_path, 3000);
  // Connecting to an already-listening socket can complete synchronously, so poll rather than
  // waiting for an emission that may already have happened.
  QTRY_VERIFY(client.isConnected());
  QCOMPARE(connectedSpy.count(), 1);

  QCOMPARE(client.sendCommand(QJsonArray{QStringLiteral("stop")}), qint64(1));
  QCOMPARE(client.setProperty(QStringLiteral("pause"), true), qint64(2));
  QCOMPARE(client.observeProperty(QStringLiteral("time-pos")), qint64(3));
}

QTEST_GUILESS_MAIN(TestMpvIpc)
#include "test_mpvipc.moc"
