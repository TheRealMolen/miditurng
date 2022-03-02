#pragma once

class ModalPot {
public:
  static constexpr uint8_t Pin = 0;
  static constexpr uint8_t MaxModes = 8;
  static constexpr int     Threshold = 10;

  ModalPot() {
    m_vals[0] = analogRead(Pin);
  }

  int getVal(uint8_t mode) const { return m_vals[mode]; }
  int getLastMode() const { return m_lastMode; }

  bool hasMoved() const {
    int currVal = analogRead(Pin);
    int prevVal = m_vals[m_lastMode];
    int delta = abs(currVal - prevVal);
    return (delta > Threshold);
  }

  bool update(uint8_t mode) {
    int currVal = analogRead(Pin);

    if (mode == m_lastMode) {
      bool changed = (currVal != m_vals[mode]);
      m_vals[mode] = currVal;
      return changed;
    }

    int prevVal = m_vals[m_lastMode];
    int delta = abs(currVal - prevVal);
    if (delta < Threshold) {
      return false;
    }

    m_vals[mode] = currVal;
    m_lastMode = mode;
    return true;
  }
  

private:
  int m_vals[MaxModes] = {};
  uint8_t m_lastMode = 0;
};
