#include "VmixProtocol.h"

#include "color/CorrectionParameterMask.h"

#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <cmath>

namespace VmixProtocol {

namespace {

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

QString num(double v)
{
    // locale independent, short
    QString s = QString::number(v, 'f', 4);
    while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0')) || s.endsWith(QLatin1Char('.'))))
        s.chop(1);
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s;
}

/** Normalised key for CC names: "LiftR", "liftr", "lift_r", "CCLiftR" -> "liftr". */
QString ccKey(QString n)
{
    n = n.toLower();
    n.remove(QLatin1Char('_'));
    n.remove(QLatin1Char('-'));
    if (n.startsWith(QLatin1String("cc")))
        n.remove(0, 2);
    if (n.startsWith(QLatin1String("setcc")))
        n.remove(0, 5);
    return n;
}

bool applyCcValue(CameraCorrectionBundle &b, const QString &rawName, const QString &text)
{
    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);
    if (!ok)
        return false;
    const QString k = ccKey(rawName);
    const auto f = static_cast<float>(v);
    if (k == QLatin1String("liftr")) b.liftR = f;
    else if (k == QLatin1String("liftg")) b.liftG = f;
    else if (k == QLatin1String("liftb")) b.liftB = f;
    else if (k == QLatin1String("lifty")) b.liftY = f;
    else if (k == QLatin1String("gammar")) b.gammaR = f;
    else if (k == QLatin1String("gammag")) b.gammaG = f;
    else if (k == QLatin1String("gammab")) b.gammaB = f;
    else if (k == QLatin1String("gammay")) b.gammaY = f;
    else if (k == QLatin1String("gainr")) b.gainR = f;
    else if (k == QLatin1String("gaing")) b.gainG = f;
    else if (k == QLatin1String("gainb")) b.gainB = f;
    else if (k == QLatin1String("gainy")) b.gainY = f;
    else if (k == QLatin1String("saturation")) b.saturation = fromVmixSaturation(v);
    else if (k == QLatin1String("hue")) b.hue = f;
    else
        return false;
    return true;
}

bool changed(float a, float b) { return std::abs(a - b) > 1e-4f; }

} // namespace

double toVmixLift(float v) { return clampd(v, -1.0, 1.0); }
double toVmixGamma(float v) { return clampd(v, -1.0, 1.0); }
double toVmixGain(float v) { return clampd(v, 0.0, 2.0); }
double toVmixSaturation(float s) { return clampd(double(s) - 1.0, -1.0, 1.0); }
double toVmixHue(float v) { return clampd(v, -1.0, 1.0); }
float fromVmixSaturation(double v) { return static_cast<float>(clampd(v, -1.0, 1.0) + 1.0); }

State parseState(const QByteArray &xml)
{
    State st;
    QXmlStreamReader r(xml);
    Input *cur = nullptr;
    int depthInInput = 0;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QString n = r.name().toString();
            if (n == QLatin1String("vmix")) {
                st.ok = true;
            } else if (n == QLatin1String("version") && !cur) {
                st.version = r.readElementText();
            } else if (n == QLatin1String("edition") && !cur) {
                st.edition = r.readElementText();
            } else if (n == QLatin1String("preview") && !cur) {
                st.preview = r.readElementText().toInt();
            } else if (n == QLatin1String("active") && !cur) {
                st.active = r.readElementText().toInt();
            } else if (n == QLatin1String("input") && !cur && r.attributes().hasAttribute(QLatin1String("number"))) {
                Input in;
                const QXmlStreamAttributes a = r.attributes();
                in.number = a.value(QLatin1String("number")).toInt();
                in.key = a.value(QLatin1String("key")).toString();
                in.type = a.value(QLatin1String("type")).toString();
                in.title = a.value(QLatin1String("title")).toString();
                in.state = a.value(QLatin1String("state")).toString();
                for (const QXmlStreamAttribute &at : a)
                    in.hasCc |= applyCcValue(in.cc, at.name().toString(), at.value().toString());
                st.inputs.push_back(in);
                cur = &st.inputs.last();
                depthInInput = 0;
            } else if (cur) {
                ++depthInInput;
                // <cc liftR=".." .../> or <colourCorrection><gainR>1.1</gainR>...</colourCorrection>
                for (const QXmlStreamAttribute &at : r.attributes())
                    cur->hasCc |= applyCcValue(cur->cc, at.name().toString(), at.value().toString());
                const QString lower = ccKey(n);
                if (lower.startsWith(QLatin1String("lift")) || lower.startsWith(QLatin1String("gamma"))
                    || lower.startsWith(QLatin1String("gain")) || lower == QLatin1String("saturation")
                    || lower == QLatin1String("hue")) {
                    const QString text = r.readElementText(QXmlStreamReader::SkipChildElements);
                    cur->hasCc |= applyCcValue(cur->cc, n, text);
                    --depthInInput;
                }
            }
        } else if (r.isEndElement()) {
            if (cur) {
                if (depthInInput == 0 && r.name() == QLatin1String("input"))
                    cur = nullptr;
                else
                    --depthInInput;
            }
        }
    }
    if (r.hasError() && !st.ok) {
        st.error = QStringLiteral("XML vMix не разобран: %1").arg(r.errorString());
    } else if (!st.ok) {
        st.error = QStringLiteral("Ответ не похож на XML vMix");
    } else if (r.hasError()) {
        st.error = r.errorString();
    }
    return st;
}

QString functionQuery(const QString &function, const QString &input, const QString &value)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("Function"), function);
    if (!input.isEmpty())
        q.addQueryItem(QStringLiteral("Input"), input);
    if (!value.isEmpty())
        q.addQueryItem(QStringLiteral("Value"), value);
    return q.toString(QUrl::FullyEncoded);
}

QVector<Command> ccCommands(const CameraCorrectionBundle &from, const CameraCorrectionBundle &to,
                            const CorrectionParameterMask *mask, bool all)
{
    QVector<Command> out;
    const bool lift = !mask || mask->lift;
    const bool gamma = !mask || mask->gamma;
    const bool gain = !mask || mask->gain;
    const bool levelY = !mask || mask->gain || mask->level; // Gain Y = level in vMix (no sensor dB)
    const bool sat = !mask || mask->saturation;
    const bool hue = !mask || mask->hue;
    const auto add = [&](bool on, float a, float b, const char *fn, double v) {
        if (on && (all || changed(a, b)))
            out.push_back({QLatin1String(fn), num(v)});
    };
    add(lift, from.liftR, to.liftR, "SetCCLiftR", toVmixLift(to.liftR));
    add(lift, from.liftG, to.liftG, "SetCCLiftG", toVmixLift(to.liftG));
    add(lift, from.liftB, to.liftB, "SetCCLiftB", toVmixLift(to.liftB));
    add(lift, from.liftY, to.liftY, "SetCCLiftY", toVmixLift(to.liftY));
    add(gamma, from.gammaR, to.gammaR, "SetCCGammaR", toVmixGamma(to.gammaR));
    add(gamma, from.gammaG, to.gammaG, "SetCCGammaG", toVmixGamma(to.gammaG));
    add(gamma, from.gammaB, to.gammaB, "SetCCGammaB", toVmixGamma(to.gammaB));
    add(gamma, from.gammaY, to.gammaY, "SetCCGammaY", toVmixGamma(to.gammaY));
    add(gain, from.gainR, to.gainR, "SetCCGainR", toVmixGain(to.gainR));
    add(gain, from.gainG, to.gainG, "SetCCGainG", toVmixGain(to.gainG));
    add(gain, from.gainB, to.gainB, "SetCCGainB", toVmixGain(to.gainB));
    add(levelY, from.gainY, to.gainY, "SetCCGainY", toVmixGain(to.gainY));
    add(sat, from.saturation, to.saturation, "SetCCSaturation", toVmixSaturation(to.saturation));
    add(hue, from.hue, to.hue, "SetCCHue", toVmixHue(to.hue));
    return out;
}

CameraCorrectionBundle keepUnsupported(const CameraCorrectionBundle &wanted, const CameraCorrectionBundle &current)
{
    CameraCorrectionBundle b = wanted;
    b.whiteBalanceKelvin = current.whiteBalanceKelvin;
    b.whiteBalanceTint = current.whiteBalanceTint;
    b.gainDb = current.gainDb;
    b.irisFStopAv = current.irisFStopAv;
    b.offsetR = current.offsetR;
    b.offsetG = current.offsetG;
    b.offsetB = current.offsetB;
    b.offsetY = current.offsetY;
    b.contrastPivot = current.contrastPivot;
    b.contrastAdj = current.contrastAdj;
    b.lumaMix = current.lumaMix;
    // vMix ranges
    const auto c = [](float &v, double lo, double hi) { v = static_cast<float>(clampd(v, lo, hi)); };
    for (float *v : {&b.liftR, &b.liftG, &b.liftB, &b.liftY, &b.gammaR, &b.gammaG, &b.gammaB, &b.gammaY})
        c(*v, -1, 1);
    for (float *v : {&b.gainR, &b.gainG, &b.gainB, &b.gainY})
        c(*v, 0, 2);
    c(b.saturation, 0, 2);
    c(b.hue, -1, 1);
    return b;
}

QVector<Output> outputs()
{
    return {
        {2, QStringLiteral("vMix Output 2"), QStringLiteral("SetOutput2")},
        {3, QStringLiteral("vMix Output 3"), QStringLiteral("SetOutput3")},
        {4, QStringLiteral("vMix Output 4"), QStringLiteral("SetOutput4")},
        {102, QStringLiteral("vMix External 2"), QStringLiteral("SetOutputExternal2")},
        {201, QStringLiteral("vMix Fullscreen"), QStringLiteral("SetOutputFullscreen")},
        {202, QStringLiteral("vMix Fullscreen 2"), QStringLiteral("SetOutputFullscreen2")},
    };
}

QString routeQuery(qint64 outputId, qint64 sourceId, QString *error)
{
    for (const Output &o : outputs()) {
        if (o.id != outputId)
            continue;
        if (sourceId == kSourceProgram)
            return functionQuery(o.function, QString(), QStringLiteral("Output"));
        if (sourceId == kSourcePreview)
            return functionQuery(o.function, QString(), QStringLiteral("Preview"));
        if (sourceId <= 0) {
            if (error)
                *error = QStringLiteral("Неверный вход vMix %1").arg(sourceId);
            return {};
        }
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("Function"), o.function);
        q.addQueryItem(QStringLiteral("Value"), QStringLiteral("Input"));
        q.addQueryItem(QStringLiteral("Input"), QString::number(sourceId));
        return q.toString(QUrl::FullyEncoded);
    }
    if (error)
        *error = QStringLiteral("Выход vMix id %1 неизвестен").arg(outputId);
    return {};
}

QPair<QString, int> parseAddress(const QString &address)
{
    QString a = address.trimmed();
    if (a.isEmpty())
        return {QStringLiteral("localhost"), kDefaultHttpPort};
    if (!a.contains(QLatin1String("://")))
        a.prepend(QStringLiteral("http://"));
    const QUrl u(a);
    const QString host = u.host().isEmpty() ? QStringLiteral("localhost") : u.host();
    return {host, u.port(kDefaultHttpPort)};
}

} // namespace VmixProtocol
