#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QWidget>
#include <QImage>
#include <vector>

// Scrolling spectrogram waterfall widget.
// Call addLine() with the latest magnitude spectrum (linear power, 0 Hz to nyquist).
// The widget maps the spectrum to a colour gradient and scrolls it down.
class WaterfallWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaterfallWidget(QWidget *parent = nullptr);

    // Feed a new FFT magnitude row.
    // magnitudes: magnitude values (linear power) for bins 0..N/2
    // sampleRateHz: sample rate the FFT was computed at (to compute Hz/bin)
    void addLine(const std::vector<float> &magnitudes, float sampleRateHz);

    // Set the TX frequency marker position (Hz)
    void setTxFreqHz(float hz)  { m_txFreqHz = hz; update(); }

    // Set display range in Hz (default: 0–4000 Hz)
    void setFreqRange(float lowHz, float highHz) {
        m_lowHz = lowHz; m_highHz = highHz; update();
    }

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

signals:
    // Emitted when user clicks on the waterfall (audio freq in Hz)
    void frequencyClicked(float hz);

private:
    void rebuildImage(int w, int h);
    static QRgb powerToColor(float power_db);

    QImage   m_image;
    float    m_txFreqHz  = 1500.0f;
    float    m_lowHz     =    0.0f;
    float    m_highHz    = 4000.0f;
    float    m_noiseFloor = -35.0f;  // dB re peak
    float    m_range      =  50.0f;  // dB display range
    int      m_scrollLine = 0;       // next write position in circular image
    float    m_lineAccum  = 0.0f;    // fractional line accumulator for rate control
};
