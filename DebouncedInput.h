#pragma once

// logic in base class saves 142B from sketch for 3 buttons

class DebouncedInputBase {
  static constexpr uint8_t DebounceMs = 40;
public:

  DebouncedInputBase()
    : m_lastPressMs(0)
    , m_isSettling(0)
    , m_isDown(0)
    , m_justReleased(0)
    , m_isShift(0)
  { /**/ }
  
  bool isDown() const       { return m_isDown; }
  bool justReleased() const { return m_justReleased; }
  bool isShift() const      { return m_isShift; }
  void setShift()           { m_isShift = true; }

protected:
  // returns true if down state changed
  bool internalUpdate(uint16_t nowMillis16, bool isDownNow) {
    if (m_justReleased) {
      m_justReleased = false;
    }
    
    uint8_t nowMillis = uint8_t(nowMillis16);
    uint8_t dtMs = uint8_t(nowMillis - m_lastPressMs);

    if (m_isSettling) {
      if (dtMs < DebounceMs)
        return false;

      m_isSettling = false;
    }
    m_lastPressMs = nowMillis;
      
    bool wasDown = m_isDown;
    if (isDownNow != wasDown) {
      setState(isDownNow);
      return true;
    }
    
    return false;
  }
  
  void setState(bool isDownNow) {
    m_isSettling = true;

    if (m_isDown && !isDownNow) {
      m_justReleased = true;
    }
    else {
      m_justReleased = false;
    }

    if (!isDownNow && m_isShift) {
      m_isShift = false;
    }
    
    m_isDown = isDownNow;
  }

  uint8_t m_lastPressMs;
  uint8_t m_isSettling : 1;
  uint8_t m_isDown : 1;
  uint8_t m_justReleased : 1;
  uint8_t m_isShift : 1;
};


template<int Pin, uint8_t FlagBit=0>
class DebouncedInput : public DebouncedInputBase {
public:
  
  void init() {
    pinMode(Pin, INPUT_PULLUP);
    update(millis());
  }

  // returns true if down state changed
  bool update(uint16_t nowMillis16) {
    return internalUpdate(nowMillis16, !digitalRead(Pin));
  }

  constexpr uint8_t getId() const { return FlagBit; }

  static constexpr uint8_t Flag = 1 << FlagBit;
  constexpr uint8_t getFlag() const { return Flag; }

  uint8_t downFlag() const      { return m_isDown << FlagBit; }
  uint8_t shiftFlag() const     { return m_isShift << FlagBit; }
  uint8_t releasedFlag() const  { return m_justReleased << FlagBit; }
};
