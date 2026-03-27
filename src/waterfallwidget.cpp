// SPDX-License-Identifier: GPL-3.0-or-later
#include "waterfallwidget.h"
#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <cmath>
#include <algorithm>
#include <numeric>

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

void WaterfallWidget::setGain(float gainDb)
{
    m_gain = std::clamp(gainDb, -30.0f, 30.0f);
    update();
}

void WaterfallWidget::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode != mode) {
        m_displayMode = mode;
        resetAverage();
    }
}

void WaterfallWidget::setAverageCount(int n)
{
    m_avgWindow = std::max(1, n);
    resetAverage();
}

void WaterfallWidget::resetAverage()
{
    m_avgAccum.clear();
    m_avgCount = 0;
    m_cumPeak.clear();
}

// Map power in dB (relative to peak, so ≤0) to a colour.
// Range controlled by m_range and m_gain.
QRgb WaterfallWidget::powerToColor(float db) const
{
    // Apply gain offset: positive gain makes small signals more visible
    const float boosted = db + m_gain;
    // Normalise: boosted=0 → t=1.0, boosted=-range → t=0.0
    float t = std::clamp(boosted / m_range + 1.0f, 0.0f, 1.0f);
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

    // Build the spectrum to display based on the current mode
    const std::vector<float> *display = &magnitudes;
    std::vector<float> processed;

    if (m_displayMode == LinearAverage) {
        // Maintain running sum of last m_avgWindow spectra
        if (m_avgAccum.size() != magnitudes.size()) {
            m_avgAccum.assign(magnitudes.size(), 0.0f);
            m_avgCount = 0;
        }
        // Rolling average: just accumulate and track count up to window
        for (size_t i = 0; i < magnitudes.size(); ++i)
            m_avgAccum[i] += magnitudes[i];
        ++m_avgCount;
        if (m_avgCount > m_avgWindow) {
            // Subtract oldest estimate by subtracting the fractional excess
            // (simplistic: just divide by count to normalize)
        }
        processed.resize(magnitudes.size());
        const float denom = static_cast<float>(std::min(m_avgCount, m_avgWindow));
        for (size_t i = 0; i < magnitudes.size(); ++i)
            processed[i] = m_avgAccum[i] / denom;
        // If we've exceeded the window, pull back the accumulator
        if (m_avgCount >= m_avgWindow) {
            const float scale = static_cast<float>(m_avgWindow - 1) / static_cast<float>(m_avgWindow);
            for (float &v : m_avgAccum) v *= scale;
            m_avgCount = m_avgWindow;
        }
        display = &processed;

    } else if (m_displayMode == Cumulative) {
        if (m_cumPeak.size() != magnitudes.size())
            m_cumPeak.assign(magnitudes.size(), 0.0f);
        for (size_t i = 0; i < magnitudes.size(); ++i)
            m_cumPeak[i] = std::max(m_cumPeak[i], magnitudes[i]);
        display = &m_cumPeak;
    }
    // else: Current mode — display = &magnitudes (set above)

    // Rate control: target full-pane refresh in 15 s at ~10 Hz input
    const float linesPerCall = static_cast<float>(h) / (15.0f * 10.0f);
    m_lineAccum += linesPerCall;
    int linesToWrite = static_cast<int>(m_lineAccum);
    if (linesToWrite < 1) linesToWrite = 1;
    m_lineAccum -= static_cast<float>(linesToWrite);

    // Find peak for normalisation
    const float hzPerBin  = sampleRateHz / (2.0f * static_cast<float>(N));
    const float freqRange = m_highHz - m_lowHz;
    float peak = 1e-12f;
    for (float v : *display) peak = std::max(peak, v);

    // Render pixel row
    std::vector<QRgb> row(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) {
        const float hz  = m_lowHz + freqRange * static_cast<float>(x) / static_cast<float>(w);
        int bin = static_cast<int>(hz / hzPerBin);
        bin = std::clamp(bin, 0, N - 1);
        const float db = 20.0f * std::log10((*display)[static_cast<size_t>(bin)] / peak + 1e-12f);
        row[static_cast<size_t>(x)] = powerToColor(db);
    }

    // Write into circular image buffer
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

    const int sl = m_scrollLine;
    if (sl == 0) {
        p.drawImage(0, 0, m_image);
    } else {
        const int newH = h - sl;
        p.drawImage(QRect(0, 0, w, newH), m_image, QRect(0, sl, w, newH));
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
        p.fillRect(x - 1, 0, textW + 6, fm.height() + 3, Qt::black);
        p.setPen(QColor(0xc9, 0xa8, 0x4c));  // amber
        p.drawLine(x, 0, x, 5);
        p.drawText(x + 2, 3 + fm.ascent(), label);
    }

    // Gain indicator (shown when gain != 0)
    if (std::abs(m_gain) > 0.5f) {
        const QString gainStr = (m_gain > 0 ? QStringLiteral("+") : QString())
                                + QString::number(static_cast<int>(m_gain)) + QStringLiteral("dB");
        const int textW = fm.horizontalAdvance(gainStr) + 6;
        p.fillRect(w - textW - 4, 0, textW + 4, fm.height() + 3, QColor(0x1a, 0x1a, 0x2e));
        p.setPen(QColor(0xc9, 0xa8, 0x4c));
        p.drawText(w - textW, 3 + fm.ascent(), gainStr);
    }

    // Mode indicator
    if (m_displayMode != Current) {
        const QString modeStr = (m_displayMode == LinearAverage)
                                ? QStringLiteral("AVG") : QStringLiteral("CUM");
        const int textW = fm.horizontalAdvance(modeStr) + 6;
        p.fillRect(4, 0, textW, fm.height() + 3, QColor(0x1a, 0x1a, 0x2e));
        p.setPen(QColor(0x7f, 0xbf, 0x7f));
        p.drawText(7, 3 + fm.ascent(), modeStr);
    }

    // TX frequency marker
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
