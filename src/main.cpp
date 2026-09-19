#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QStyle>
#include "MainWindow.h"

namespace {

// 与 resources/app.ico（scripts/make_icon.py 生成）同款设计：
// 深色圆角底板 + 4x4 彩色垫面
QIcon makeAppIcon()
{
    const int S = 256;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor plate(0x1a, 0x1c, 0x23);
    const QColor palette[4] = {
        QColor(0x4c, 0xc2, 0xff), QColor(0x35, 0xd0, 0xba),
        QColor(0x8b, 0x7c, 0xf6), QColor(0xf2, 0xb2, 0x4c),
    };

    p.setPen(Qt::NoPen);
    p.setBrush(plate);
    p.drawRoundedRect(4, 4, S - 8, S - 8, 56, 56);

    const qreal pad = S * 0.115, gap = S * 0.035;
    const qreal cell = (S - 2 * pad - 3 * gap) / 4.0;
    int k = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            p.setBrush(palette[(k + r) % 4]);
            const qreal x = pad + c * (cell + gap);
            const qreal y = pad + r * (cell + gap);
            p.drawRoundedRect(QRectF(x, y, cell, cell), cell * 0.28, cell * 0.28);
            ++k;
        }
    }
    p.end();
    return QIcon(pm);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SuperMidiMap"));
    app.setOrganizationName(QStringLiteral("SuperMidiMap"));
    app.setStyle(QStringLiteral("Fusion"));
    app.setWindowIcon(makeAppIcon());

    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    MainWindow window;
    window.show();
    return app.exec();
}
