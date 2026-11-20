#pragma once
// Minimal fake vMix HTTP Web API (https://www.vmix.com/help28/DeveloperAPI.html) for tests and UI demos.
// GET /api            -> XML state in the documented shape (<vmix><version>…<inputs><input key number type
//                         title state …>title</input>…</inputs>…<preview>…<active>…)
// GET /api/?Function=… -> 200 for the documented functions below, 500 for anything else / bad values.
// Runs its own event loop in a thread so a blocking client in the test thread can talk to it.

#include <QHash>
#include <QHostAddress>
#include <QtNumeric>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <functional>

class FakeVmixServer final : public QThread
{
public:
    struct In
    {
        int number;
        QString key;
        QString type;
        QString title;
        QHash<QString, double> cc; // "LiftR" … "Saturation", "Hue"
    };

    explicit FakeVmixServer(quint16 port = 0, bool exposeCc = false)
        : m_port(port)
        , m_exposeCc(exposeCc)
    {
        m_inputs = {
            {1, QStringLiteral("26cae087-b7b6-4d45-98e4-de03ab4feb6b"), QStringLiteral("Capture"), QStringLiteral("CAM1"), {}},
            {2, QStringLiteral("55cbe357-a801-4d54-8ff2-08ee68766fae"), QStringLiteral("Capture"), QStringLiteral("CAM2"), {}},
            {3, QStringLiteral("877bb3e7-58bd-46a1-85ce-0d673aec6bf5"), QStringLiteral("Xaml"), QStringLiteral("NewsHD.xaml"), {}},
        };
        for (In &in : m_inputs)
            resetCc(in);
    }
    ~FakeVmixServer() override
    {
        quit();
        wait(3000);
    }

    /** Start and wait until listening; returns the port. */
    quint16 startAndWait()
    {
        start();
        for (int i = 0; i < 300 && boundPort() == 0; ++i)
            QThread::msleep(10);
        return boundPort();
    }
    quint16 boundPort() const
    {
        QMutexLocker l(&m_mutex);
        return m_bound;
    }
    QStringList calls() const
    {
        QMutexLocker l(&m_mutex);
        return m_calls;
    }
    void clearCalls()
    {
        QMutexLocker l(&m_mutex);
        m_calls.clear();
    }
    double cc(int input, const QString &name) const
    {
        QMutexLocker l(&m_mutex);
        for (const In &in : m_inputs)
            if (in.number == input)
                return in.cc.value(name, qQNaN());
        return qQNaN();
    }
    QString output(const QString &fn) const
    {
        QMutexLocker l(&m_mutex);
        return m_outputs.value(fn);
    }

protected:
    void run() override
    {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, m_port))
            return;
        {
            QMutexLocker l(&m_mutex);
            m_bound = server.serverPort();
        }
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this, &server] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    *buf += s->readAll();
                    if (!buf->contains("\r\n\r\n"))
                        return;
                    const QByteArray line = buf->left(buf->indexOf("\r\n"));
                    const QString path = QString::fromUtf8(line.split(' ').value(1));
                    int code = 200;
                    QByteArray body = handle(path, &code);
                    QByteArray resp = "HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error")
                                      + "\r\nContent-Type: text/xml\r\nContent-Length: " + QByteArray::number(body.size())
                                      + "\r\nConnection: close\r\n\r\n" + body;
                    s->write(resp);
                    s->disconnectFromHost();
                });
                QObject::connect(s, &QTcpSocket::disconnected, s, [s, buf] {
                    delete buf;
                    s->deleteLater();
                });
            }
        });
        exec();
    }

private:
    static void resetCc(In &in)
    {
        for (const char *n : {"LiftR", "LiftG", "LiftB", "LiftY", "GammaR", "GammaG", "GammaB", "GammaY"})
            in.cc.insert(QLatin1String(n), 0.0);
        for (const char *n : {"GainR", "GainG", "GainB", "GainY"})
            in.cc.insert(QLatin1String(n), 1.0);
        in.cc.insert(QStringLiteral("Saturation"), 0.0);
        in.cc.insert(QStringLiteral("Hue"), 0.0);
    }

    In *findInput(const QString &spec)
    {
        bool ok = false;
        const int n = spec.toInt(&ok);
        for (In &in : m_inputs)
            if ((ok && in.number == n) || in.key == spec || in.title == spec)
                return &in;
        if (ok && n == 0)
            for (In &in : m_inputs)
                if (in.number == m_preview)
                    return &in;
        if (ok && n == -1)
            for (In &in : m_inputs)
                if (in.number == m_active)
                    return &in;
        return nullptr;
    }

    QByteArray xml() const
    {
        QByteArray x = "<vmix><version>28.0.0.42</version><edition>4K</edition><inputs>";
        for (const In &in : m_inputs) {
            x += "<input key=\"" + in.key.toUtf8() + "\" number=\"" + QByteArray::number(in.number) + "\" type=\""
                 + in.type.toUtf8() + "\" title=\"" + in.title.toUtf8()
                 + "\" state=\"Running\" position=\"0\" duration=\"0\" loop=\"False\" muted=\"True\" volume=\"100\""
                   " balance=\"0\" solo=\"False\" audiobusses=\"M\" meterF1=\"0\" meterF2=\"0\">"
                 + in.title.toUtf8();
            if (m_exposeCc) {
                x += "<cc";
                for (auto it = in.cc.cbegin(); it != in.cc.cend(); ++it)
                    x += " " + it.key().toUtf8() + "=\"" + QByteArray::number(it.value()) + "\"";
                x += "/>";
            }
            x += "</input>";
        }
        x += "</inputs><overlays><overlay number=\"1\"/><overlay number=\"2\"/></overlays><preview>"
             + QByteArray::number(m_preview) + "</preview><active>" + QByteArray::number(m_active)
             + "</active><fadeToBlack>False</fadeToBlack><recording>False</recording><external>False</external>"
               "<streaming>False</streaming><playList>False</playList><multiCorder>False</multiCorder></vmix>";
        return x;
    }

    QByteArray handle(const QString &path, int *code)
    {
        QMutexLocker l(&m_mutex);
        const QUrl url(QStringLiteral("http://x") + path);
        if (!url.path().startsWith(QLatin1String("/api"), Qt::CaseInsensitive)) {
            *code = 404;
            return "not found";
        }
        const QUrlQuery q(url);
        const QString fn = q.queryItemValue(QStringLiteral("Function"), QUrl::FullyDecoded);
        if (fn.isEmpty())
            return xml();
        m_calls << url.query(QUrl::FullyDecoded);
        const QString inputSpec = q.queryItemValue(QStringLiteral("Input"), QUrl::FullyDecoded);
        const QString valueStr = q.queryItemValue(QStringLiteral("Value"), QUrl::FullyDecoded);
        bool numOk = false;
        const double value = valueStr.toDouble(&numOk);
        if (fn.startsWith(QLatin1String("SetCC"))) {
            const QString name = fn.mid(5);
            In *in = findInput(inputSpec);
            const bool gain = name.startsWith(QLatin1String("Gain"));
            const double lo = gain ? 0.0 : -1.0, hi = gain ? 2.0 : 1.0;
            const bool known = in && in->cc.contains(name);
            if (!known || !numOk || value < lo - 1e-9 || value > hi + 1e-9) {
                *code = 500;
                return "error";
            }
            in->cc[name] = value;
            return "Function completed successfully.";
        }
        if (fn == QLatin1String("ColourCorrectionAuto") || fn == QLatin1String("ColourCorrectionReset")) {
            In *in = findInput(inputSpec);
            if (!in) {
                *code = 500;
                return "error";
            }
            resetCc(*in);
            if (fn == QLatin1String("ColourCorrectionAuto"))
                in->cc[QStringLiteral("GainB")] = 0.95; // "something automatic happened"
            return "Function completed successfully.";
        }
        if (fn == QLatin1String("SetOutput2") || fn == QLatin1String("SetOutput3") || fn == QLatin1String("SetOutput4")
            || fn == QLatin1String("SetOutputExternal2") || fn == QLatin1String("SetOutputFullscreen")
            || fn == QLatin1String("SetOutputFullscreen2")) {
            static const QStringList allowed{QStringLiteral("Output"), QStringLiteral("Preview"),
                                             QStringLiteral("MultiView"), QStringLiteral("Replay"),
                                             QStringLiteral("Mix"), QStringLiteral("Input")};
            if (!allowed.contains(valueStr) || (valueStr == QLatin1String("Input") && !findInput(inputSpec))) {
                *code = 500;
                return "error";
            }
            m_outputs[fn] = valueStr == QLatin1String("Input") ? QStringLiteral("Input:%1").arg(inputSpec) : valueStr;
            return "Function completed successfully.";
        }
        if (fn == QLatin1String("PreviewInput") || fn == QLatin1String("ActiveInput")) {
            In *in = findInput(inputSpec);
            if (!in) {
                *code = 500;
                return "error";
            }
            (fn == QLatin1String("PreviewInput") ? m_preview : m_active) = in->number;
            return "Function completed successfully.";
        }
        *code = 500; // not a function this fake knows -> catches typos in function names
        return "error";
    }

    mutable QMutex m_mutex;
    quint16 m_port = 0;
    quint16 m_bound = 0;
    bool m_exposeCc = false;
    QVector<In> m_inputs;
    QHash<QString, QString> m_outputs;
    QStringList m_calls;
    int m_preview = 2;
    int m_active = 1;
};
