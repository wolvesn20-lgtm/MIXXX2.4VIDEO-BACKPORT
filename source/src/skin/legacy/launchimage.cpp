#include "skin/legacy/launchimage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QStyleOption>
#include <QVBoxLayout>

#include "moc_launchimage.cpp"

LaunchImage::LaunchImage(QWidget* pParent, const QString& styleSheet)
        : QWidget(pParent) {
    if (styleSheet.isEmpty()) {
        setStyleSheet(
                "LaunchImage { background-color: #202020; }"
                "QLabel { "
                "image: url(:/images/mixxx-icon-logo-symbolic.svg);"
                "padding:0;"
                "margin:0;"
                "border:none;"
                "min-width: 236px;"
                "min-height: 48px;"
                "max-width: 236px;"
                "max-height: 48px;"
                "}"
                "QProgressBar {"
                "background-color: #202020; "
                "border:none;"
                "min-width: 236px;"
                "min-height: 3px;"
                "max-width: 236px;"
                "max-height: 3px;"
                "}"
                "QProgressBar::chunk { background-color: #f3efed; }");
    } else {
        setStyleSheet(styleSheet);
    }

    QLabel *label = new QLabel(this);

    m_pProgressBar = new QProgressBar(this);
    m_pProgressBar->setTextVisible(false);

    QLabel* pTaglineLabel = new QLabel(
            QStringLiteral("BUILT BY DJS, TESTED BY DJS, FOR DJS\nPOWERED BY THE NIGHT WOLVES ENTERTAINMENT"),
            this);
    pTaglineLabel->setAlignment(Qt::AlignCenter);
    pTaglineLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  image: none;"
            "  color: #E0F7FA;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "  letter-spacing: 1px;"
            "  background: transparent;"
            "  border: none;"
            "  min-width: 0px;"
            "  max-width: 16777215px;"
            "  min-height: 0px;"
            "  max-height: 16777215px;"
            "  padding-top: 8px;"
            "  padding-bottom: 4px;"
            "}"));

    QHBoxLayout* hbox = new QHBoxLayout(this);
    QVBoxLayout* vbox = new QVBoxLayout();
    vbox->addStretch();
    vbox->addWidget(label);
    vbox->addWidget(m_pProgressBar);
    vbox->addWidget(pTaglineLabel);
    vbox->addStretch();
    hbox->addStretch();
    hbox->addLayout(vbox);
    hbox->addStretch();
}

void LaunchImage::progress(int value, const QString& serviceName) {
    m_pProgressBar->setValue(value);
    // TODO: show serviceName
    Q_UNUSED(serviceName);
}

void LaunchImage::paintEvent(QPaintEvent *)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
