#include <EEPROM.h>

#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include "DebouncedInput.h"

#define PIN_UIPIXEL       9
#define PIN_BTN_THRU      10
#define PIN_BTN_CHANNEL   11
#define PIN_BTN_REC       12

MIDI_CREATE_DEFAULT_INSTANCE();

enum class EepAddr {
  Channel = 200,    // skip the first block of eeprom as it's likely the most tired
  Bpm,
  Scale,
};


byte bpm;
uint32_t usPerQn = 100;
void setBpm(byte newBpm) {
  bpm = newBpm;
  usPerQn = (((unsigned long)1000000 * 60) / (newBpm * 4));
}


Adafruit_NeoPixel uiPixel(1, PIN_UIPIXEL, NEO_GRB + NEO_KHZ800);
uint32_t uiOverrideCol = uiPixel.Color(0xff, 0xff, 0xff);
uint16_t uiOverrideMsRemaining = 0;
uint16_t uiLastMs = 0;

DebouncedInput<PIN_BTN_CHANNEL> channelButton;
DebouncedInput<PIN_BTN_THRU>    thruButton;
DebouncedInput<PIN_BTN_REC>     recButton;

byte sendChannel = 2;

struct MidiNote
{
  byte note;
  byte oct;
  byte vel;
};
static const byte MaxNotes = 16;
static const byte numNotes = MaxNotes;

MidiNote noteBuf[MaxNotes];
byte nextNote = 0;


#define NOTE_ON_MEMORY 10
byte playingNotes[NOTE_ON_MEMORY];
byte numPlayingNotes = 0;

void rememberNoteOn(byte note) {
  if (numPlayingNotes >= NOTE_ON_MEMORY)
    return;

  for (byte i = 0; i < numPlayingNotes; ++i) {
    if (playingNotes[i] == note)
      return;
  }

  playingNotes[numPlayingNotes] = note;
  ++numPlayingNotes;
}
void forgetNoteOn(byte note) {
  for (byte i = 0; i < numPlayingNotes; ++i) {
    if (playingNotes[i] == note) {
      playingNotes[i] = playingNotes[numPlayingNotes - 1];
      --numPlayingNotes;
      return;
    }
  }
}
void midiSend(const struct MidiNote* note, bool noteOn) {
  if (noteOn) {
    MIDI.sendNoteOn(note->note, note->vel, sendChannel);
    rememberNoteOn(note->note);
    digitalWrite(13, HIGH);
  }
  else {
    MIDI.sendNoteOff(note->note, 0, sendChannel);
    forgetNoteOn(note->note);
    digitalWrite(13, LOW);
  }
}
void midiClearAllNotes() {
  for (byte i = 0; i < numPlayingNotes; ++i) {
    MIDI.sendNoteOff(playingNotes[i], 0, sendChannel);
  }
  numPlayingNotes = 0;
  digitalWrite(13, LOW);
}


static byte scaleJ5[] = { 0, 1, 5, 7, 11 };
byte scaleJ5Num = 0xc;
void updateScaleJ5(byte num) {
  num &= 0xf;
  
  byte lo = num & 0x3;
  scaleJ5[1] = 1 + lo;

  byte hi = num >> 2;
  scaleJ5[4] = 8 + hi;

  scaleJ5Num = num;
}

static const byte ScaleSize = 5;

static const int RootNote = 36;
int numOctaves = 3;

unsigned long lastTimeUs = 0;
unsigned long tickProgressUs = 0;

void generateNewNote(MidiNote* note, bool init = false) {
  if (init) {
    note->oct = random(0, numOctaves);
  }
  
  byte newNote = RootNote;
  newNote += scaleJ5[random(ScaleSize)];

  if (random(2) > 0) {
    if ((random(100) > 50 || note->oct < 1) && (note->oct < (numOctaves-1))) {
      note->oct += 1;
    }
    else {
      note->oct -= 1;
    }
  }

  newNote += note->oct * 12;

  note->note = newNote;

  note->vel = random(100, 127);
}

void updatePlayback() {
  unsigned long now = micros();
  unsigned long deltaUs = now - lastTimeUs;
  lastTimeUs = now;
  tickProgressUs += deltaUs;
  while (tickProgressUs > usPerQn) {
    tickProgressUs -= usPerQn;

    midiClearAllNotes();
    midiSend(&noteBuf[nextNote], true);

    uint32_t colour = 0x00ff00;
    if (noteBuf[nextNote].note < RootNote) {
      colour = 0xffff00;
    }
    else if (noteBuf[nextNote].note >= RootNote + 36) {
      colour = 0x0000ff;
    }
    
    // generate a new note if we rolled enough dice
    int threshold = analogRead(0);
    if (random(0, 980) > threshold) {
      generateNewNote(&noteBuf[nextNote]);
    }
      
    ++nextNote;
    if (nextNote >= numNotes) {
      nextNote = 0;
    }

    uiPixel.setPixelColor(0, uiPixel.gamma32(colour));
    uiPixel.show();
  }
}





void overrideUiCol(uint32_t col) {
  uiOverrideMsRemaining = 175;
  uiOverrideCol = col;
}

void updateUI() {
  bool uiChanged = false;
  uint16_t nowMs = millis();

  // --- update playback channel ---
  if (channelButton.update(nowMs) && channelButton.isDown()) {
    sendChannel = (sendChannel + 1) & 0xf;
    EEPROM.write(int(EepAddr::Channel), sendChannel);
    overrideUiCol(uiPixel.Color(0xc0, 0xc0, 0xc0));
  }

  // --- update bpm ---
  if (thruButton.update(nowMs) && thruButton.isDown()) {
    byte newBpm = bpm + 20;
    if (newBpm > 120) {
      newBpm = 60;
    }
    setBpm(newBpm);
    EEPROM.write(int(EepAddr::Bpm), bpm);
    overrideUiCol(uiPixel.Color(0xff, 0xff, 0xc0));
  }

  // --- update scale ---
  if (recButton.update(nowMs) && recButton.isDown()) {
    updateScaleJ5(scaleJ5Num + 1);
    EEPROM.write(int(EepAddr::Scale), scaleJ5Num);
    overrideUiCol(uiPixel.Color(0xc0, 0xc0, 0xff));
  }
  
  if (uiOverrideMsRemaining > 0) {
    uint16_t deltaMs = nowMs - uiLastMs;
    if (deltaMs > uiOverrideMsRemaining)
      uiOverrideMsRemaining = 0;
    else
      uiOverrideMsRemaining -= deltaMs;

    uiChanged = true;
  }
  uiLastMs = nowMs;

  if (uiChanged) {
    uint32_t colour;
    if (uiOverrideMsRemaining > 0)
    {
      colour = uiOverrideCol;
      
      uiPixel.setPixelColor(0, uiPixel.gamma32(colour));
      uiPixel.show();
    }
  }
}




void loadSettings() {
  EEPROM.get(int(EepAddr::Channel), sendChannel);
  if (sendChannel > 15) {
    sendChannel = 0;
  }

  byte newBpm;
  EEPROM.get(int(EepAddr::Bpm), newBpm);
  if (newBpm != 60 && newBpm != 80 && newBpm != 100 && newBpm != 120) {
    newBpm = 80;
  }
  setBpm(newBpm);

  byte scaleNum;
  EEPROM.get(int(EepAddr::Scale), scaleNum);
  updateScaleJ5(scaleNum);
}


void setup() {
  MIDI.begin(MIDI_CHANNEL_OMNI);
  pinMode(13, OUTPUT);

  loadSettings();
  
  channelButton.init();
  thruButton.init();
  recButton.init();
  
  uiPixel.begin();
  uiPixel.show();
  uiPixel.setBrightness(25);

  lastTimeUs = micros();

  randomSeed(analogRead(5));

  for (byte i=0; i<MaxNotes; ++i)
  {
    generateNewNote(&noteBuf[i]);
  }
}


void loop() {
  random(100);
  
  updateUI();

  updatePlayback();
}
