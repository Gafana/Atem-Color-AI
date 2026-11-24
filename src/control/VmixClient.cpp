#include "VmixClient.h"

#include "atem/CameraControlProtocol.h"
#include "color/CorrectionParameterMask.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTcpSocket>

Q_LOGGING_CATEGORY(lcVmix, "vmix")

using namespace VmixProtocol;

namespace {

const QString kNoCamera = QStringLiteral("В vMix нет параметров камеры (только цветокоррекция входа)");

/** Decode HTTP/1.x response: status code + body (Content-Length, chunked or until close). */
int parseHttp(const QByteArray &raw, QByteArray *body, bool *complete)
{
    *complete = false;
    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return 0;
    const QList<QByteArray> lines = raw.left(headerEnd).split('\n');
    const QList<QByteArray> status = lines.value(0).trimmed().split(' ');
    const int code = status.value(1).toInt();
    qint64 contentLength = -1;
    bool chunked = false;
    for (const QByteArray &l : lines) {
        const QByteArray t = l.trimmed().toLower();
        if (t.startsWith("content-length:"))
            contentLength = t.mid(15).trimmed().toLongLong();
        else if (t.startsWith("transfer-encoding:") && t.contains("chunked"))
            chunked = true;
    }
    const QByteArray rest = raw.mid(headerEnd + 4);
    if (chunked) {
        QByteArray out;
        int pos = 0;
        while (true) {
            const int le = rest.indexOf("\r\n", pos);
            if (le < 0)
                return code;
            bool ok = false;
            const int n = rest.mid(pos, le - pos).trimmed().toInt(&ok, 16);
            if (!ok)
                return code;
            if (n == 0) {
                *complete = true;
                break;
            }
            if (rest.size() < le + 2 + n)
                return code;
            out += rest.mid(le + 2, n);
            pos = le + 2 + n + 2;
        }
        *body = out;
        return code;
    }
    *body = rest;
    if (contentLength >= 0 && rest.size() >= contentLength) {
        body->truncate(int(contentLength));
        *complete = true;
    }
    return code;
}

} // namespace

VmixClient::VmixClient(CacheLoad load, CacheSave save, int timeoutMs)
    : m_timeoutMs(timeoutMs)
    , m_load(std::move(load))
    , m_save(std::move(save))
{
    loadCache();
}

QString VmixClient::backendName() const
{
    return m_version.isEmpty() ? QStringLiteral("vMix") : QStringLiteral("vMix %1").arg(m_version);
}

QString VmixClient::httpGet(const QString &pathAndQuery, QByteArray *body)
{
    QTcpSocket s;
    // vMix Web Controller listens on IPv4; "localhost" may resolve to ::1 first and time out
    const QString host =
        m_host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 ? QStringLiteral("127.0.0.1") : m_host;
    s.connectToHost(host, quint16(m_port));
    if (!s.waitForConnected(m_timeoutMs))
        return QStringLiteral("vMix %1:%2 не отвечает (%3). Включите Web Controller в vMix: Settings → Web Controller")
            .arg(m_host).arg(m_port).arg(s.errorString());
    const QByteArray req = "GET " + pathAndQuery.toUtf8() + " HTTP/1.1\r\nHost: " + m_host.toUtf8() + ":"
                           + QByteArray::number(m_port) + "\r\nConnection: close\r\nUser-Agent: AtemColorAI\r\n\r\n";
    s.write(req);
    if (!s.waitForBytesWritten(m_timeoutMs))
        return QStringLiteral("vMix: запрос не отправлен (%1)").arg(s.errorString());
    QByteArray raw;
    QElapsedTimer t;
    t.start();
    int code = 0;
    bool complete = false;
    QByteArray b;
    while (t.elapsed() < m_timeoutMs * 2) {
        if (s.bytesAvailable() > 0 || s.waitForReadyRead(200))
            raw += s.readAll();
        code = parseHttp(raw, &b, &complete);
        if (complete)
            break;
        if (s.state() == QAbstractSocket::UnconnectedState) {
            raw += s.readAll();
            code = parseHttp(raw, &b, &complete);
            complete = code != 0;
            break;
        }
    }
    s.disconnectFromHost();
    if (code == 0)
        return QStringLiteral("vMix: нет ответа на %1").arg(pathAndQuery.left(80));
    if (body)
        *body = b;
    if (code != 200)
        return QStringLiteral("vMix ответил %1 на %2").arg(code).arg(pathAndQuery.left(120));
    return {};
}

State VmixClient::fetchState(QString *error)
{
    QByteArray body;
    const QString err = httpGet(QStringLiteral("/api"), &body);
    State st;
    if (!err.isEmpty()) {
        if (error)
            *error = err;
        return st;
    }
    st = parseState(body);
    if (!st.ok && error)
        *error = st.error.isEmpty() ? QStringLiteral("vMix: пустой ответ") : st.error;
    if (st.ok)
        m_state = st;
    return st;
}

QString VmixClient::callFunction(const QString &query)
{
    const QString err = httpGet(QStringLiteral("/api/?") + query, nullptr);
    qCInfo(lcVmix).noquote() << QStringLiteral("vmix %1 -> %2").arg(query, err.isEmpty() ? QStringLiteral("ok") : err);
    return err;
}

QString VmixClient::connectTo(const QString &address)
{
    const auto hp = parseAddress(address);
    m_host = hp.first;
    m_port = hp.second;
    m_connected = false;
    QString err;
    const State st = fetchState(&err);
    if (!err.isEmpty())
        return err;
    m_version = st.version;
    m_connected = true;
    m_routes.clear();
    return {};
}

void VmixClient::disconnect()
{
    m_connected = false;
}

QVector<AtemInputInfo> VmixClient::listInputs()
{
    QVector<AtemInputInfo> out;
    if (!m_connected)
        return out;
    QString err;
    fetchState(&err);
    for (const Input &in : m_state.inputs) {
        AtemInputInfo i;
        i.id = in.number;
        i.shortName = in.title.left(24);
        i.longName = in.title;
        i.portType = in.type;
        i.cameraControlSupported = true;
        out.push_back(i);
    }
    return out;
}

QVector<AtemAuxOutput> VmixClient::listAuxOutputs()
{
    QVector<AtemAuxOutput> out;
    for (const Output &o : outputs())
        out.push_back({o.id, o.name});
    return out;
}

QString VmixClient::auxSource(qint64 auxId, qint64 *sourceId)
{
    // vMix does not report output routing in the XML: we know what we set; untouched outputs are assumed
    // to show the programme («Output»), which is also what the restore sends back.
    if (sourceId)
        *sourceId = m_routes.value(auxId, kSourceProgram);
    return {};
}

QString VmixClient::setAuxSource(qint64 auxId, qint64 sourceId)
{
    if (!m_connected)
        return QStringLiteral("Нет подключения к vMix");
    QString err;
    const QString q = routeQuery(auxId, sourceId, &err);
    if (q.isEmpty())
        return err;
    err = callFunction(q);
    if (err.isEmpty())
        m_routes.insert(auxId, sourceId);
    return err;
}

QString VmixClient::programInput(qint64 *inputId)
{
    QString err;
    const State st = fetchState(&err);
    if (inputId)
        *inputId = st.active;
    return err;
}

QString VmixClient::setProgramInput(qint64 inputId)
{
    return callFunction(functionQuery(QStringLiteral("ActiveInput"), QString::number(inputId)));
}

QString VmixClient::inputKey(qint64 number)
{
    for (const Input &in : m_state.inputs)
        if (in.number == number && !in.key.isEmpty())
            return in.key;
    return QStringLiteral("#%1").arg(number);
}

QString VmixClient::readCorrectionBundle(qint64 destinationId, CameraCorrectionBundle &out)
{
    if (!m_connected)
        return QStringLiteral("Нет подключения к vMix");
    QString err;
    const State st = fetchState(&err);
    if (!err.isEmpty())
        return err;
    const Input *found = nullptr;
    for (const Input &in : st.inputs)
        if (in.number == destinationId)
            found = &in;
    if (!found)
        return QStringLiteral("В vMix нет входа %1").arg(destinationId);
    const QString key = inputKey(destinationId);
    CameraCorrectionBundle b = m_cc.value(key, CameraCorrectionBundle());
    if (found->hasCc) {
        b = keepUnsupported(found->cc, b);
        m_cc.insert(key, b);
    }
    m_ccFromXml.insert(destinationId, found->hasCc);
    out = b;
    return {};
}

QString VmixClient::applyCorrectionBundle(qint64 destinationId, const CameraCorrectionBundle &bundle,
                                          CameraCorrectionBundle *previousOut, const CorrectionParameterMask *mask,
                                          const QString &tuningMode)
{
    Q_UNUSED(tuningMode);
    return apply(destinationId, bundle, previousOut, mask, false);
}

QString VmixClient::apply(qint64 destinationId, const CameraCorrectionBundle &bundle,
                          CameraCorrectionBundle *previousOut, const CorrectionParameterMask *mask, bool sendAll)
{
    CameraCorrectionBundle cur;
    QString err = readCorrectionBundle(destinationId, cur);
    if (!err.isEmpty())
        return err;
    if (previousOut)
        *previousOut = cur;
    const CameraCorrectionBundle target = keepUnsupported(bundle, cur);
    const QString key = inputKey(destinationId);
    const bool all = sendAll || (m_ccUnknown.contains(key) && !m_ccFromXml.value(destinationId, false));
    const QVector<Command> cmds = ccCommands(cur, target, mask, all);
    const QString input = QString::number(destinationId);
    CameraCorrectionBundle sent = cur;
    QStringList errors;
    for (const Command &c : cmds) {
        const QString e = callFunction(functionQuery(c.function, input, c.value));
        if (!e.isEmpty()) {
            errors << e;
            break;
        }
    }
    if (errors.isEmpty())
        sent = mask ? mergeBundleWithMask(target, cur, *mask) : target;
    sent = keepUnsupported(sent, cur);
    m_cc.insert(key, sent);
    if (errors.isEmpty() && (!mask || (mask->lift && mask->gamma && mask->gain && mask->saturation && mask->hue)))
        m_ccUnknown.remove(key); // every vMix CC value was written: known again
    saveCache();
    qCInfo(lcVmix).noquote() << QStringLiteral("vmix input %1: %2 CC calls").arg(destinationId).arg(cmds.size());
    return errors.join(QStringLiteral("; "));
}

QString VmixClient::rollbackCorrection(qint64 destinationId, const CameraCorrectionBundle &previous)
{
    // explicit: every value is sent (vMix may have changed them itself, e.g. auto colour correction)
    return apply(destinationId, previous, nullptr, nullptr, true);
}

QString VmixClient::setCameraFloats(qint64, uint8_t, uint8_t, const QVector<float> &) { return kNoCamera; }
QString VmixClient::setCameraInt16Pair(qint64, uint8_t, uint8_t, int, int) { return kNoCamera; }
QString VmixClient::setCameraGainDb(qint64, int) { return kNoCamera; }

QString VmixClient::triggerCameraAction(qint64 destinationId, uint8_t category, uint8_t parameter)
{
    if (category == BmdCameraProtocol::kCategoryVideo && parameter == BmdCameraProtocol::kVideoAutoWhiteBalance) {
        // documented "Basic Auto Colour Correction" of the input
        const QString err =
            callFunction(functionQuery(QStringLiteral("ColourCorrectionAuto"), QString::number(destinationId)));
        if (err.isEmpty()) {
            m_cc.remove(inputKey(destinationId)); // values are now unknown to us
            m_ccUnknown.insert(inputKey(destinationId));
        }
        saveCache();
        return err;
    }
    return kNoCamera;
}

void VmixClient::loadCache()
{
    if (!m_load)
        return;
    const QJsonObject o = QJsonDocument::fromJson(m_load()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        m_cc.insert(it.key(), CameraCorrectionBundle::fromJson(it.value().toObject()));
}

void VmixClient::saveCache()
{
    if (!m_save)
        return;
    QJsonObject o;
    for (auto it = m_cc.cbegin(); it != m_cc.cend(); ++it)
        o.insert(it.key(), it.value().toJson());
    m_save(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

std::unique_ptr<AtemClient> createVmixClient(VmixClient::CacheLoad load, VmixClient::CacheSave save)
{
    return std::make_unique<VmixClient>(std::move(load), std::move(save));
}
