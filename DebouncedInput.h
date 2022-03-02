#pragma once

#define DEBOUNCE_MS 80

template<int Pin>
class DebouncedInput {
public:
  void init() {
    pinMode(Pin, INPUT_PULLUP);
    m_lastPressMs = 0;
    m_isDown = false;
    m_isShift = false;
    m_justReleased = false;
    update(millis());
  }
  
  bool isDown() const       { return m_isDown; }
  bool justReleased() const { return m_justReleased; }
  bool isShift() const      { return m_isShift; }

  // returns true if down state changed
  bool update(uint16_t nowMillis) {
    // lose shift status one tick after we were released so the caller can tell w
    if (!m_isDown && m_isShift) {
      m_isShift = false;
    }
    if (m_justReleased) {
      m_justReleased = true;
    }
    
    bool isDownNow = !digitalRead(Pin);
    bool wasDown = m_isDown;
    if (isDownNow != wasDown) {
      if ((nowMillis - m_lastPressMs) > DEBOUNCE_MS) {
        setState(isDownNow, nowMillis);
        return true;
      }
    }
    return false;
  }
  
private:
  void setState(bool isDownNow, uint16_t nowMillis) {
    m_lastPressMs = uint8_t(nowMillis);

    if (m_isDown && !isDownNow) {
      m_justReleased = true;
    }
    
    m_isDown = isDownNow;
  }

  uint8_t m_lastPressMs;
  uint8_t m_isDown : 1;
  uint8_t m_justReleased : 1;
  uint8_t m_isShift : 1;
};
