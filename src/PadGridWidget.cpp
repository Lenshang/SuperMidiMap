#include "PadGridWidget.h"
#include "NoteNames.h"

#include <QMouseEvent>
#include <QPainter>

PadGridWidget::PadGridWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(380, 380);
    setFocusPolicy(Qt::ClickFocus);  // 点击垫面后 Tab 可进入右侧编辑器
    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(33);
    connect(m_animTimer, &QTimer::timeout, this, [this] {
        bool active = false;
        for (int i = 0; i < MappingProfile::kPads; ++i) {
            if (m_held[i]) {
                m_glow[i] = m_heldGlow[i];
                active = true;
            } else if (m_glow[i] > 0.02) {
                m_glow[i] *= 0.78;
                active = true;
            } else {
                m_glow[i] = 0.0;
            }
        }
        if (active)
            update();
        else
            m_animTimer->stop();
    });
}

void PadGridWidget::setProfileVisuals(const MappingProfile &profile)
{
    m_profile = profile;
    update();
}

void PadGridWidget::onPadActivity(int sourceNote, int velocity, bool isOn)
{
    for (int i = 0; i < MappingProfile::kPads; ++i) {
        if (m_profile.pads[i].source != sourceNote)
            continue;
        if (isOn) {
            m_held[i] = true;
            m_heldGlow[i] = qMax(0.3, qMin(1.0, velocity / 127.0));
            m_glow[i] = m_heldGlow[i];
        } else {
            m_held[i] = false;
        }
    }
    if (!m_animTimer->isActive())
        m_animTimer->start();
    update();
}

void PadGridWidget::layoutMetrics(qreal *cell, qreal *ox, qreal *oy) const
{
    const qreal margin = 6.0, gap = 10.0;
    const qreal cw = (qreal(width()) - 2 * margin - 3 * gap) / 4.0;
    const qreal chh = (qreal(height()) - 2 * margin - 3 * gap) / 4.0;
    *cell = qMin(cw, chh);
    *ox = (width() - (4 * (*cell) + 3 * gap)) / 2.0;
    *oy = (height() - (4 * (*cell) + 3 * gap)) / 2.0;
}

int PadGridWidget::padAt(const QPoint &pos) const
{
    qreal cell = 0, ox = 0, oy = 0;
    layoutMetrics(&cell, &ox, &oy);
    const qreal gap = 10.0;
    for (int i = 0; i < MappingProfile::kPads; ++i) {
        const int row = i / 4, col = i % 4;
        const QRectF r(ox + col * (cell + gap), oy + row * (cell + gap), cell, cell);
        if (r.contains(pos))
            return i;
    }
    return -1;
}

void PadGridWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    qreal cell = 0, ox = 0, oy = 0;
    layoutMetrics(&cell, &ox, &oy);
    const qreal gap = 10.0;

    QFont srcFont = font();
    srcFont.setPointSizeF(qMax(8.0, cell * 0.095));
    QFont tgtFont = font();
    tgtFont.setPointSizeF(qMax(11.0, cell * 0.20));
    tgtFont.setBold(true);

    for (int i = 0; i < MappingProfile::kPads; ++i) {
        const int row = i / 4, col = i % 4;
        const QRectF r(ox + col * (cell + gap), oy + row * (cell + gap), cell, cell);

        const quint8 src = m_profile.pads[i].source;
        const PadMapping &m = m_profile.pads[i];
        const bool muted = m.enabled && m.muted;
        const bool identity = !muted && (!m.enabled || (m.target == src && m.channel == 0));

        // 底色
        QColor base(0x23, 0x25, 0x2d);
        if (i == m_selected)
            base = QColor(0x2b, 0x32, 0x40);
        p.setPen(Qt::NoPen);
        p.setBrush(base);
        p.drawRoundedRect(r, 10, 10);

        // 按下辉光（静音垫用红色提示）
        const qreal g = qMin(1.0, m_glow[i]);
        if (g > 0.01) {
            QColor glow = muted ? QColor(0xe5, 0x53, 0x4b) : QColor(0x4c, 0xc2, 0xff);
            glow.setAlphaF(g * 0.55);
            p.setBrush(glow);
            p.drawRoundedRect(r, 10, 10);
        }

        // 边框
        QColor border(0x3a, 0x3b, 0x45);
        qreal borderWidth = 1.0;
        if (i == m_selected) {
            border = QColor(0x4c, 0xc2, 0xff);
            borderWidth = 2.0;
        }
        p.setPen(QPen(border, borderWidth));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(borderWidth * 0.5, borderWidth * 0.5,
                                     -borderWidth * 0.5, -borderWidth * 0.5), 10, 10);

        // 左上：源音符（设备固定键位）
        p.setPen(QColor(0x8f, 0x95, 0xa1));
        p.setFont(srcFont);
        p.drawText(r.adjusted(8, 6, -8, -8), Qt::AlignTop | Qt::AlignLeft,
                   QStringLiteral("%1 · %2").arg(NoteNames::name(src)).arg(src));

        // 底部中央：当前映射目标
        p.setPen(muted ? QColor(0xff, 0x7b, 0x72) : QColor(0xd8, 0xdb, 0xe2));
        p.setFont(tgtFont);
        const QString tgt = muted ? QStringLiteral("静音")
                                  : NoteNames::name(identity ? src : int(m.target));
        p.drawText(r.adjusted(8, 0, -8, -6), Qt::AlignBottom | Qt::AlignHCenter, tgt);

        // 已重映射的垫在左下角画箭头提示
        if (!identity && !muted) {
            p.setPen(QColor(0x53, 0xd7, 0x6f));
            p.setFont(srcFont);
            p.drawText(r.adjusted(8, 0, -8, -6), Qt::AlignBottom | Qt::AlignLeft,
                       QStringLiteral("\u2192"));
        }
    }
}

void PadGridWidget::mousePressEvent(QMouseEvent *event)
{
    const int index = padAt(event->pos());
    if (index >= 0) {
        m_selected = index;
        update();
        emit padClicked(index);
    }
}
