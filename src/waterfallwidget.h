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

    enum DisplayMode {
        Current       = 0,  // raw current spectrum each line
        LinearAverage = 1,  // running linear average of last N spectra
        Cumulative    = 2,  // peak-hold across all received spectra
    };

    // Feed a new FFT magnitude row.
    void addLine(const std::vector<float> &magnitudes, float sampleRateHz);

    // Set the TX frequency marker position (Hz)
    void setTxFreqHz(float hz)  { m_txFreqHz = hz; update(); }

    // Set display range in Hz (default: 0–4000 Hz)
    void setFreqRange(float lowHz, float highHz) {
        m_lowHz = lowHz; m_highHz = highHz; update();
    }

    // Gain offset in dB (0 = normal; positive = brighter; negative = dimmer).
    // Range: -30 to +30 dB.
    void setGain(float gainDb);
    float gain() const { return m_gain; }

    // Display mode (see enum above)
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_displayMode; }

    // Average window size for LinearAverage mode (number of input spectra)
    void setAverageCount(int n);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

signals:
    void frequencyClicked(float hz);

private:
    void rebuildImage(int w, int h);
    QRgb powerToColor(float power_db) const;  // uses m_gain
    void resetAverage();

    QImage   m_image;
    float    m_txFreqHz   = 1500.0f;
    float    m_lowHz      =    0.0f;
    float    m_highHz     = 4000.0f;
    float    m_range      =  50.0f;   // dB display range
    float    m_gain       =   0.0f;   // user gain offset (dB)
    int      m_scrollLine =     0;    // next write position in circular image
    float    m_lineAccum  =   0.0f;   // fractional line accumulator

    DisplayMode m_displayMode = Current;

    // LinearAverage accumulator
    std::vector<float> m_avgAccum;    // sum of last N spectra
    int                m_avgCount   = 0;    // how many spectra in accum
    int                m_avgWindow  = 4;    // window size

    // Cumulative peak-hold
    std::vector<float> m_cumPeak;     // per-bin peak magnitude seen so far
};
