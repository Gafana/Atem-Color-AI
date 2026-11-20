#include "AiTipsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QVBoxLayout>

using namespace FrameAdvisor;

namespace {

QString sevColor(Severity s)
{
    switch (s) {
    case Severity::Critical:
        return QStringLiteral("#ff5a5a");
    case Severity::Warning:
        return QStringLiteral("#ffb340");
    case Severity::Info:
        return QStringLiteral("#8fb8ff");
    case Severity::Ok:
        return QStringLiteral("#5ad28c");
    }
    return QStringLiteral("#cccccc");
}

QString issueHtml(const Issue &i)
{
    return QStringLiteral("<li><span style='color:%1'><b>[%2]</b> %3</span><br/>&rarr; %4</li>")
        .arg(sevColor(i.severity), severityLabel(i.severity), i.text.toHtmlEscaped(), i.advice.toHtmlEscaped());
}

} // namespace

AiTipsWidget::AiTipsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    auto *row = new QHBoxLayout();
    m_btn = new QPushButton(QStringLiteral("Анализ"), this);
    m_btn->setObjectName(QStringLiteral("aiTipsAnalyze"));
    m_btn->setToolTip(QStringLiteral("С ATEM: AUX по очереди переключается на отмеченные камеры, затем возвращается. "
                                     "Без ATEM: анализ текущего кадра с входа. Если выделена зона — анализ только зоны."));
    connect(m_btn, &QPushButton::clicked, this, &AiTipsWidget::analyzeRequested);
    row->addWidget(m_btn);
    m_chkMarks = new QCheckBox(QStringLiteral("Отметки на превью:"), this);
    m_chkMarks->setChecked(true);
    connect(m_chkMarks, &QCheckBox::toggled, this, &AiTipsWidget::marksChanged);
    row->addWidget(m_chkMarks);
    m_source = new QComboBox(this);
    m_source->setMinimumContentsLength(8);
    connect(m_source, &QComboBox::currentIndexChanged, this, &AiTipsWidget::marksChanged);
    row->addWidget(m_source, 1);
    lay->addLayout(row);
    m_status = new QLabel(QStringLiteral("Нажмите «Анализ»"), this);
    m_status->setStyleSheet(QStringLiteral("color:#9aa0aa;"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);
    m_view = new QTextBrowser(this);
    m_view->setOpenLinks(false);
    lay->addWidget(m_view, 1);
    setMinimumSize(220, 160);
}

void AiTipsWidget::setBusy(bool busy, const QString &status)
{
    m_btn->setEnabled(!busy);
    if (!status.isEmpty())
        m_status->setText(status);
}

int AiTipsWidget::markedSource() const { return m_source->currentIndex(); }

bool AiTipsWidget::marksEnabled() const { return m_chkMarks->isChecked(); }

QString AiTipsWidget::reportHtml(const QVector<Advice> &results, const QVector<QVector<Issue>> &cross,
                                 const QVector<double> &deltaE, const QString &header)
{
    QString h = QStringLiteral("<div style='font-size:12px'>");
    if (!header.isEmpty())
        h += QStringLiteral("<p style='color:#9aa0aa'>%1</p>").arg(header.toHtmlEscaped());
    for (int k = 0; k < results.size(); ++k) {
        const Advice &a = results.at(k);
        h += QStringLiteral("<h3 style='margin:6px 0 2px 0'>%1 <span style='font-weight:normal;color:#9aa0aa'>(%2, "
                            "медиана %3 IRE)</span></h3>")
                 .arg(a.label.toHtmlEscaped(), a.zone ? QStringLiteral("зона") : QStringLiteral("весь кадр"))
                 .arg(a.stats.medianIre, 0, 'f', 0);
        h += QStringLiteral("<ul style='margin-top:0'>");
        bool any = false;
        for (const Issue &i : a.issues) {
            h += issueHtml(i);
            any = true;
        }
        if (k < cross.size()) {
            for (const Issue &i : cross.at(k)) {
                h += issueHtml(i);
                any = true;
            }
        }
        if (!any)
            h += QStringLiteral("<li style='color:#5ad28c'>Замечаний нет</li>");
        h += QStringLiteral("</ul>");
        Q_UNUSED(deltaE);
    }
    h += QStringLiteral("</div>");
    return h;
}

void AiTipsWidget::setResults(const QVector<Advice> &results, const QVector<QVector<Issue>> &cross,
                              const QVector<double> &deltaE, const QString &header)
{
    m_results = results;
    {
        const QSignalBlocker block(m_source);
        m_source->clear();
        for (const Advice &a : results)
            m_source->addItem(a.label);
    }
    QStringList v;
    for (const Advice &a : results)
        v << QStringLiteral("%1: %2").arg(a.label, a.verdict());
    m_status->setText(v.join(QStringLiteral(" · ")));
    m_view->setHtml(reportHtml(results, cross, deltaE, header));
    Q_EMIT marksChanged();
}
