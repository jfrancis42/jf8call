// SPDX-License-Identifier: GPL-3.0-or-later
#include "waterfallwidget.h"
#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <cmath>
#include <algorithm>

WaterfallWidget::WaterfallWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    rebuildImage(width(), height());
}

void WaterfallWidget::rebuildImage(int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    m_image = QImage(w, h, QImage::Format_RGB32);
    m_image.fill(Qt::black);
    m_scrollLine = 0;
}

void WaterfallWidget::resizeEvent(QResizeEvent *)
{
    rebuildImage(width(), height());
}

// Map power in dB to a colour.
// Range: -range dB (black) → 0 dB (red)
QRgb WaterfallWidget::powerToColor(float db)
{
    // Normalise to [0, 1]
    float t = std::clamp(db / 50.0f + 1.0f, 0.0f, 1.0f);
    // Colour map: black → navy → cyan → yellow → red
    if (t < 0.25f) {
        float u = t / 0.25f;
        return qRgb(0, 0, static_cast<int>(128 * u));
    } else if (t < 0.5f) {
        float u = (t - 0.25f) / 0.25f;
        return qRgb(0, static_cast<int>(192 * u), static_cast<int>(128 + 127 * u));
    } else if (t < 0.75f) {
        float u = (t - 0.5f) / 0.25f;
        return qRgb(static_cast<int>(255 * u), 192, static_cast<int>(255 * (1 - u)));
    } else {
        float u = (t - 0.75f) / 0.25f;
        return qRgb(255, static_cast<int>(192 * (1 - u)), 0);
    }
}

void WaterfallWidget::addLine(const std::vector<float> &magnitudes, float sampleRateHz)
{
    if (magnitudes.empty() || m_image.isNull()) return;

    const int w = m_image.width();
    const int h = m_image.height();
    const int N = static_cast<int>(magnitudes.size());

    // Target: full pane refreshes in 15 seconds at ~10 Hz input.
    // Accumulate fractional lines; write one or more per call as needed.
    const float linesPerCall = static_cast<float>(h) / (15.0f * 10.0f);
    m_lineAccum += linesPerCall;
    int linesToWrite = static_cast<int>(m_lineAccum);
    if (linesToWrite < 1) linesToWrite = 1;   // always advance at least one pixel
    m_lineAccum -= static_cast<float>(linesToWrite);

    // Pre-render this spectrum into a pixel row
    const float hzPerBin  = sampleRateHz / (2.0f * static_cast<float>(N));
    const float freqRange = m_highHz - m_lowHz;
    float peak = 1e-12f;
    for (float v : magnitudes) peak = std::max(peak, v);

    std::vector<QRgb> row(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) {
        const float hz  = m_lowHz + freqRange * static_cast<float>(x) / static_cast<float>(w);
        int bin = static_cast<int>(hz / hzPerBin);
        bin = std::clamp(bin, 0, N - 1);
        const float db = 20.0f * std::log10(magnitudes[bin] / peak + 1e-12f);
        row[static_cast<size_t>(x)] = powerToColor(db);
    }

    // Write linesToWrite copies of the rendered row into the circular buffer.
    // Decrement-first so m_scrollLine always points to the newest line (top of display).
    for (int l = 0; l < linesToWrite; ++l) {
        m_scrollLine = (m_scrollLine - 1 + h) % h;
        QRgb *dst = reinterpret_cast<QRgb *>(m_image.scanLine(m_scrollLine));
        std::copy(row.begin(), row.end(), dst);
    }

    update();
}

void WaterfallWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width();
    const int h = height();

    if (m_image.isNull() || m_image.width() != w || m_image.height() != h)
        rebuildImage(w, h);

    // Scroll downward: newest line at top, oldest at bottom.
    // m_scrollLine always points to the newest line.
    // Newest slot: m_scrollLine     → screen y=0
    // Oldest slot: m_scrollLine-1   → screen y=h-1
    const int sl = m_scrollLine;
    if (sl == 0) {
        p.drawImage(0, 0, m_image);
    } else {
        // Newest part: image[sl..h-1] → screen top
        const int newH = h - sl;
        p.drawImage(QRect(0, 0, w, newH), m_image, QRect(0, sl, w, newH));
        // Oldest part: image[0..sl-1] → screen bottom
        p.drawImage(QRect(0, newH, w, sl), m_image, QRect(0, 0, w, sl));
    }

    // Frequency axis along the top
    QFont tickFont = p.font();
    tickFont.setPixelSize(10);
    p.setFont(tickFont);
    const QFontMetrics fm(tickFont);
    const float freqRange = m_highHz - m_lowHz;
    for (float hz = 500.0f; hz < m_highHz; hz += 500.0f) {
        int x = static_cast<int>((hz - m_lowHz) / freqRange * w);
        const QString label = QStringLiteral("%1").arg(static_cast<int>(hz));
        const int textW = fm.horizontalAdvance(label);
        // Black background behind tick + label
        p.fillRect(x - 1, 0, textW + 6, fm.height() + 3, Qt::black);
        p.setPen(QColor(0xc9, 0xa8, 0x4c));  // amber
        p.drawLine(x, 0, x, 5);
        p.drawText(x + 2, 3 + fm.ascent(), label);
    }

    // TX frequency marker — white line with black flanking lines for contrast
    {
        float hz = m_txFreqHz;
        if (hz >= m_lowHz && hz <= m_highHz) {
            int x = static_cast<int>((hz - m_lowHz) / freqRange * w);
            p.setPen(QPen(Qt::black));
            p.drawLine(x - 2, 0, x - 2, h);
            p.drawLine(x + 2, 0, x + 2, h);
            QPen whitePen(Qt::white);
            whitePen.setWidth(2);
            p.setPen(whitePen);
            p.drawLine(x, 0, x, h);
        }
    }
}

void WaterfallWidget::mousePressEvent(QMouseEvent *e)
{
    const float freqRange = m_highHz - m_lowHz;
    float hz = m_lowHz + freqRange * e->pos().x() / width();
    emit frequencyClicked(hz);
}
