#include <EEPROM.h>

#include <Adafruit_NeoPixel.h>
#include <MIDI.h>

#include "DebouncedInput.h"
#include "ModalPot.h"

#define PIN_UIPIXEL       9
#define PIN_BTN_THRU      10
#define PIN_BTN_CHANNEL   11
#define PIN_BTN_REC       12

enum Btn {
  Btn_Thru,
  Btn_Chnl,
  Btn_Play,
};

using ThruBtn = DebouncedInput<PIN_BTN_THRU, Btn_Thru>;
using ChnlBtn = DebouncedInput<PIN_BTN_CHANNEL, Btn_Chnl>;
using PlayBtn = DebouncedInput<PIN_BTN_REC, Btn_Play>;

ThruBtn thruButton;
ChnlBtn channelButton;
PlayBtn recButton;
ModalPot pot;

enum UIMode {
  UIMode_Default = 0,
  
  UIMode_Thru = ThruBtn::Flag,
  UIMode_Chnl = ChnlBtn::Flag,
  UIMode_Play = PlayBtn::Flag,
  
  UIMode_ThruChnl = ThruBtn::Flag | ChnlBtn::Flag,
  UIMode_ThruPlay = ThruBtn::Flag | PlayBtn::Flag,
  UIMode_ChnlPlay = ChnlBtn::Flag | PlayBtn::Flag,
  
  UIMode_ThruChnlPlay = ThruBtn::Flag | ChnlBtn::Flag | PlayBtn::Flag,
};


MIDI_CREATE_DEFAULT_INSTANCE();

enum class EepAddr {
  Channel = 200,    // skip the first block of eeprom as it's likely the most tired
  BpmChoice,
  Scale,
};


static const uint32_t Colours12[] = {
  0x404060,
  0x884422,
  0xff0000,
  0xee9900,
  0xdddd00,
  0x00ff00,
  0x0000ff,
  0xdd00dd,
  0x9999aa,
  0x99ffbb,
  0xff9988,
  0xffffff,
};


static constexpr uint8_t BpmChoices[] = { 8, 15, 25, 40, 60, 80, 100, 120, 140, 160, 180, 200 };
static constexpr uint8_t NumBpmChoices = (sizeof(BpmChoices) / sizeof(BpmChoices[0]));

byte bpmChoice = 8;
byte bpm;
uint32_t usPerQn = 100;
void setBpm(byte newBpm) {
  bpm = newBpm;
  usPerQn = (((unsigned long)1000000 * 60) / newBpm);
}


Adafruit_NeoPixel uiPixel(1, PIN_UIPIXEL, NEO_GRB + NEO_KHZ800);
uint32_t uiOverrideCol = uiPixel.Color(0xff, 0xff, 0xff);
uint16_t uiOverrideMsRemaining = 0;
uint16_t uiLastMs = 0;


void overrideUiCol(uint32_t col, bool force=true) {
  if (uiOverrideMsRemaining == 0 || force) {
    uiOverrideMsRemaining = 300;
    uiOverrideCol = col;
  }
}


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
      
    ++nextNote;
    if (nextNote >= numNotes) {
      nextNote = 0;
    }
    
    // generate a new note if we rolled enough dice
    int threshold = pot.getVal(UIMode_Default);
    if (random(0, 980) > threshold) {
      overrideUiCol(0xddaaaa, false);
      generateNewNote(&noteBuf[nextNote]);
    }
  }
}



void uiDefaultModeAction(uint8_t action) {
  switch (action) {
    case Btn_Thru:
      ++bpmChoice;
      if (bpmChoice >= NumBpmChoices)
        bpmChoice = 0;
        
      setBpm(BpmChoices[bpmChoice]);
      EEPROM.write(int(EepAddr::BpmChoice), bpmChoice);
      static_assert(NumBpmChoices <= 12);
      overrideUiCol(Colours12[bpmChoice]);
      break;

    case Btn_Chnl:
      sendChannel = (sendChannel + 1) & 0xf;
      EEPROM.write(int(EepAddr::Channel), sendChannel);
      overrideUiCol(Colours12[sendChannel & 7]);
      break;

    case Btn_Play:
      updateScaleJ5(scaleJ5Num + 1);
      EEPROM.write(int(EepAddr::Scale), scaleJ5Num);
      overrideUiCol(uiPixel.Color(0xc0, 0xc0, 0xff));
      break;
  }
}

void uiAction(uint8_t action, uint8_t mode) {
  switch (mode) {
    case UIMode_Default:      uiDefaultModeAction(action); break;
    case UIMode_Thru:         break;
    case UIMode_Chnl:         break;
    case UIMode_Play:         break;
    case UIMode_ThruChnl:     break;
    case UIMode_ThruPlay:     break;
    case UIMode_ChnlPlay:     break;
    case UIMode_ThruChnlPlay: break;
  }
}

void applyPotValue(int val, uint8_t mode) {
  (void) val;
  (void) mode;
}


uint8_t updateButtons(int nowMs) {
  uint8_t changes = 0;
  if (thruButton.update(nowMs))
    changes |= ThruBtn::Flag;
  if (channelButton.update(nowMs))
    changes |= ChnlBtn::Flag;
  if (recButton.update(nowMs))
    changes |= PlayBtn::Flag;

  return changes;
}

void handleInputs(int nowMs) {
  uint8_t oldDown = thruButton.downFlag() | channelButton.downFlag() | recButton.downFlag();
  uint8_t oldShift = thruButton.shiftFlag() | channelButton.shiftFlag() | recButton.shiftFlag();

  uint8_t changes = updateButtons(nowMs);

  uint8_t pressed = ~oldDown & changes;

  uint8_t released = thruButton.releasedFlag() | channelButton.releasedFlag() | recButton.releasedFlag();

  uint8_t unshiftedButtons = oldDown & ~oldShift;
  if (pot.hasMoved() && unshiftedButtons) {
    changes |= 0x10;
    pressed |= 0x10;
  }
  
  uint8_t mode = oldShift;

  if (changes) {    
    // first: figure out if any buttons have just become shift
    if (pressed) {
      uint8_t addedShift = oldDown & ~released & ~oldShift;
      if (addedShift) {
        if (addedShift & ThruBtn::Flag)
          thruButton.setShift();
        if (addedShift & ChnlBtn::Flag)
          channelButton.setShift();
        if (addedShift & PlayBtn::Flag)
          recButton.setShift();
      }
    }
    
    mode = thruButton.shiftFlag() | channelButton.shiftFlag() | recButton.shiftFlag();
    uint8_t action = released & ~(mode | oldShift);

    // FIXME: the following sequence results in a surprising action:
    //    - hold A
    //    - hold B  (A is now SHIFT)
    //    - release A
    //    - release B   --> ACTION B/default    

    // then figure out what mode/button combo just got released
    if (action) {
      if (action & thruButton.getFlag())
        uiAction(thruButton.getId(), mode);
      else if (action & channelButton.getFlag())
        uiAction(channelButton.getId(), mode);
      else if (action & recButton.getFlag())
        uiAction(recButton.getId(), mode);
    }
  }

  // note: if we have any buttons down but haven't yet marked them as shifted,
  //       we don't want to update the existing pot, or we might miss the edge
  //       where it should latch and shift the button
  if (!unshiftedButtons) {
    if (pot.update(mode))
      applyPotValue(pot.getVal(mode), mode);
  }
}


void updateUI() {
  bool uiChanged = false;
  uint16_t nowMs = millis();

  handleInputs(nowMs);
  
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
    uint32_t colour = 0xeeddaa;
    if (uiOverrideMsRemaining > 0){
      colour = uiOverrideCol; 
    }
    
    uiPixel.setPixelColor(0, uiPixel.gamma32(colour));
    uiPixel.show();
  }
}




void loadSettings() {
  EEPROM.get(int(EepAddr::Channel), sendChannel);
  if (sendChannel > 15) {
    sendChannel = 0;
  }

  EEPROM.get(int(EepAddr::BpmChoice), bpmChoice);
  if (bpmChoice >= NumBpmChoices) {
    bpmChoice = 8;
  }
  setBpm(BpmChoices[bpmChoice]);

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

  randomSeed(analogRead(3) + analogRead(4) + analogRead(5));

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
