#pragma once

#include "scopes/ScopeTypes.h"

#include <QLabel>
#include <QRect>
#include <QWidget>
class QComboBox;

class ScopePlotLabel final : public QLabel
{
    Q_OBJECT

public:
    explicit ScopePlotLabel(QWidget *parent = nullptr);

    void setWipeLine(float pos01);
    void clearWipeLine();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_wipePos = -1.0f;
};

class ScopeCellView final : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeCellView(int index, QWidget *parent = nullptr);

    int index() const { return m_index; }
    void setTitle(const QString &title);
    void setScopeImage(const QImage &image);
    void setKind(ScopeKind kind);
    ScopeKind kind() const { return m_kind; }
    void setActive(bool active);
    ScopeDisplaySettings &settings() { return m_settings; }
    const ScopeDisplaySettings &settings() const { return m_settings; }

    QComboBox *kindCombo() const { return m_kindCombo; }

Q_SIGNALS:
    void kindChanged(int index, ScopeKind kind);
    void detachRequested(int index);
    void settingsRequested(int index);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QRect plotRect() const;
    void refreshPlotPixmap();
    void updateWipeOverlay();

    int m_index = 0;
    ScopeKind m_kind = ScopeKind::None;
    ScopeDisplaySettings m_settings;
    QLabel *m_title = nullptr;
    ScopePlotLabel *m_plot = nullptr;
    QComboBox *m_kindCombo = nullptr;
    QImage m_image;
    bool m_active = false;
    bool m_dragWipe = false;
    bool m_loadingKind = false;
    float m_wipePos = 0.5f;

public:
    float wipePosition() const { return m_wipePos; }
    void setWipePosition(float p);
};