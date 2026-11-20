#pragma once

#include "color/FrameAdvisor.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTextBrowser;

/** «ИИ-подсказки» panel: one «Анализ» button, the report, and which analysed source is marked on the preview. */
class AiTipsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AiTipsWidget(QWidget *parent = nullptr);

    void setBusy(bool busy, const QString &status = QString());
    /** @p results: one per analysed source (camera or the input); @p cross: per source vs the reference. */
    void setResults(const QVector<FrameAdvisor::Advice> &results, const QVector<QVector<FrameAdvisor::Issue>> &cross,
                    const QVector<double> &deltaE, const QString &header);
    int markedSource() const;
    bool marksEnabled() const;
    const QVector<FrameAdvisor::Advice> &results() const { return m_results; }

    static QString reportHtml(const QVector<FrameAdvisor::Advice> &results,
                              const QVector<QVector<FrameAdvisor::Issue>> &cross, const QVector<double> &deltaE,
                              const QString &header);

Q_SIGNALS:
    void analyzeRequested();
    void marksChanged();

private:
    QPushButton *m_btn = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *m_chkMarks = nullptr;
    QComboBox *m_source = nullptr;
    QTextBrowser *m_view = nullptr;
    QVector<FrameAdvisor::Advice> m_results;
};
