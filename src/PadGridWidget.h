#pragma once
#include <QWidget>
#include <QTimer>
#include <array>
#include "MappingProfile.h"

// 4x4 打击垫网格：实时反映物理垫面的按下状态（辉光），点击选择垫进行编辑。
class PadGridWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PadGridWidget(QWidget *parent = nullptr);

    void setProfileVisuals(const MappingProfile &profile);
    int selectedPad() const { return m_selected; }

signals:
    void padClicked(int index);

public slots:
    void onPadActivity(int sourceNote, int velocity, bool isOn);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void layoutMetrics(qreal *cell, qreal *ox, qreal *oy) const;
    int padAt(const QPoint &pos) const;

    MappingProfile m_profile;
    std::array<qreal, MappingProfile::kPads> m_glow{};     // 当前辉光 0..1
    std::array<qreal, MappingProfile::kPads> m_heldGlow{}; // 按住时的辉光（随力度）
    std::array<bool, MappingProfile::kPads> m_held{};
    int m_selected = -1;
    QTimer *m_animTimer = nullptr;
};
