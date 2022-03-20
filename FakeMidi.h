#pragma once

#define MIDI_CHANNEL_OMNI 16

class FakeMidi
{
public:
  void begin([[maybe_unused]] int channel) {
    Serial.begin(115200);
    Serial.println("FakeMIDI engage....");
  }
  
  void sendNoteOn(byte note, [[maybe_unused]] byte velocity, byte channel) {
    Serial.print("ON  ");
    Serial.print((int)note);
    Serial.print(" @ c");
    Serial.println((int)channel);
  }
  void sendNoteOff(byte note, [[maybe_unused]] byte velocity, byte channel) {
    Serial.print("OFF ");
    Serial.print((int)note);
    Serial.print(" @ c");
    Serial.println((int)channel);
  }

  char cmd;
  char note;
  
  bool read() {
    if (Serial.available() < 2) {
      return false;
    }
    cmd = Serial.read();
    note = Serial.read();
    Serial.print("read ");
    Serial.print(cmd);
    Serial.print(note);
    Serial.println("<");
    return true;
  }

  byte getData1() {
    return 0x3a + (note - 'a');
  }
  byte getData2() {
    return 100;
  }  
 
  void turnThruOff() {}
  void sendClock() {}
  void sendStart() {}
  
};



#define MIDI_CREATE_DEFAULT_INSTANCE()  FakeMidi MIDI
