#include <QtTest>

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "mcp/MCPServer.h"

using SnapTray::MCP::MCPServer;
using SnapTray::MCP::ToolCallContext;

class tst_ServerProtocol : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void initialize_returnsCapabilities();
    void toolsList_containsMinimalTools();
    void toolsCall_unknownTool_returnsToolExecutionError();
    void methodNotFound_returns32601();
    void toolsCall_invalidParams_returns32602();
    void ping_returnsEmptyResult();
    void notificationsInitialized_returns202WithoutBody();
    void getEndpoint_returns405();
    void listener_bindsToLoopbackOnly();
    void secondServer_samePortCannotStart();
    void stop_releasesPort();

private:
    struct HttpResult {
        int statusCode = -1;
        QByteArray body;
    };

    HttpResult postJson(const QJsonObject& request) const;
    HttpResult getRequest() const;

    QUrl m_endpoint;
    std::unique_ptr<MCPServer> m_server;
};

void tst_ServerProtocol::initTestCase()
{
    ToolCallContext context;
    m_server = std::make_unique<MCPServer>(context);

    QString error;
    if (!m_server->start(0, &error)) {
        const QString detail = error.isEmpty() ? QStringLiteral("unknown startup error") : error;
        QSKIP(qPrintable(QString("Cannot start localhost MCP test server in this environment: %1").arg(detail)));
    }
    QVERIFY(m_server->port() > 0);
    m_endpoint = QUrl(QString("http://127.0.0.1:%1/mcp").arg(m_server->port()));
}

void tst_ServerProtocol::cleanupTestCase()
{
    if (m_server) {
        m_server->stop();
    }
}

void tst_ServerProtocol::initialize_returnsCapabilities()
{
    const HttpResult result = postJson(QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", QJsonObject{}},
    });

    QCOMPARE(result.statusCode, 200);
    const QJsonObject response = QJsonDocument::fromJson(result.body).object();
    QCOMPARE(response.value("jsonrpc").toString(), QString("2.0"));
    QCOMPARE(response.value("id").toInt(), 1);

    const QJsonObject responseResult = response.value("result").toObject();
    QVERIFY(responseResult.contains("capabilities"));
    QVERIFY(responseResult.value("capabilities").toObject().contains("tools"));
}

void tst_ServerProtocol::toolsList_containsMinimalTools()
{
    const HttpResult result = postJson(QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"},
    });

    QCOMPARE(result.statusCode, 200);
    const QJsonObject response = QJsonDocument::fromJson(result.body).object();
    const QJsonArray tools = response.value("result").toObject().value("tools").toArray();

    QStringList names;
    for (const QJsonValue& toolValue : tools) {
        names.append(toolValue.toObject().value("name").toString());
    }

    QVERIFY(names.contains("capture_screenshot"));
    QVERIFY(names.contains("pin_image"));
    QVERIFY(names.contains("share_upload"));
    QCOMPARE(names.size(), 3);
}

void tst_ServerProtocol::toolsCall_unknownTool_returnsToolExecutionError()
{
    const HttpResult result = postJson(QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "tools/call"},
        {"params",
            QJsonObject{
                {"name", "unknown_tool"},
                {"arguments", QJsonObject{}},
            }},
    });

    QCOMPARE(result.statusCode, 200);
    const QJsonObject response = QJsonDocument::fromJson(result.body).object();
    const QJsonObject error = response.value("error").toObject();
    QCOMPARE(error.value("code").toInt(), -32000);
    QVERIFY(error.value("message").toString().contains("Unknown tool"));
}

void tst_ServerProtocol::methodNotFound_returns32601()
{
    const HttpResult result = postJson(QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 31},
        {"method", "not/a/real/method"},
    });

    QCOMPARE(result.statusCode, 200);
    const QJsonObject response = QJsonDocument::fromJson(result.body).object();
    const QJsonObject error = response.value("error").toObject();
    QCOMPARE(error.value("code").toInt(), -32601);
}

void tst_ServerProtocol::toolsCall_invalidParams_returns32602()
{
    const HttpResult result = postJson(QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 32},
        {"method", "tools/call"},
        {"params",
            QJsonObject{
                {"name", "capture_screenshot"},
                {"arguments", "invalid-arguments-object"},
            }},
    });

    QCOMPARE(result.statusCode, 200);
    const QJsonObject response = QJsonDocument::fromJson(result.body).object();
    const QJsonObject error = response.value("error").toObject();
    QCOMPARE(error.value("code").toInt(), -32602);
}

void tst_ServerProtocol::ping_returnsEmptyResult()
{
    const HttpResult result = postJson(QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 4},
        {"method", "ping"},
    });

    QCOMPARE(result.statusCode, 200);
    const QJsonObject response = QJsonDocument::fromJson(result.body).object();
    QVERIFY(response.value("result").toObject().isEmpty());
}

void tst_ServerProtocol::notificationsInitialized_returns202WithoutBody()
{
    const HttpResult result = postJson(QJsonObject{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"},
    });

    QCOMPARE(result.statusCode, 202);
    QVERIFY(result.body.isEmpty());
}

void tst_ServerProtocol::getEndpoint_returns405()
{
    const HttpResult result = getRequest();
    QCOMPARE(result.statusCode, 405);
}

void tst_ServerProtocol::listener_bindsToLoopbackOnly()
{
    QCOMPARE(m_server->listenAddress(), QHostAddress(QHostAddress::LocalHost));
}

void tst_ServerProtocol::secondServer_samePortCannotStart()
{
    ToolCallContext context;
    MCPServer anotherServer(context);

    QString error;
    QVERIFY(!anotherServer.start(m_server->port(), &error));
}

void tst_ServerProtocol::stop_releasesPort()
{
    const quint16 port = m_server->port();
    QVERIFY(port > 0);

    m_server->stop();
    QVERIFY(!m_server->isListening());

    QString error;
    QVERIFY2(m_server->start(port, &error), qPrintable(error));
}

tst_ServerProtocol::HttpResult tst_ServerProtocol::postJson(const QJsonObject& request) const
{
    QNetworkAccessManager manager;
    QNetworkRequest networkRequest(m_endpoint);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = manager.post(networkRequest, payload);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    HttpResult result;
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    reply->deleteLater();
    return result;
}

tst_ServerProtocol::HttpResult tst_ServerProtocol::getRequest() const
{
    QNetworkAccessManager manager;
    QNetworkRequest networkRequest(m_endpoint);
    QNetworkReply* reply = manager.get(networkRequest);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    HttpResult result;
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    reply->deleteLater();
    return result;
}

QTEST_MAIN(tst_ServerProtocol)
#include "tst_ServerProtocol.moc"
