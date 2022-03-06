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

  UIMode_Pot_Variant = UIMode_Play,
};


MIDI_CREATE_DEFAULT_INSTANCE();

enum class ScaleFamily : byte {
  Japanese,
  Penta,
  Western,
  Discordant,

  Count,
};
inline ScaleFamily nextFamily(ScaleFamily fam) {
  fam = (byte(fam) < byte(ScaleFamily::Count)) ? ScaleFamily(byte(fam) + 1) : ScaleFamily(0);
  return fam;
}

enum class EepAddr {
  Channel = 200,    // skip the first block of eeprom as it's likely the most tired
  BpmChoice,
  Key,              // 0-11 C-B
  ScaleFamily,      // enum ScaleFamily
  ScaleVariantJP,
  ScaleVariantPent,
  ScaleVariantEU,
  ScaleVariantDisc,
  NumOctaves,
  BaseOctave,
  Chance,
  RatchetChance,
  RatchetIntensity,
};


static constexpr uint32_t Colours[] = {
  0xdd0000,
  0xe89000,
  0xddd000,
  0x50ee50,
  0x5050ee,
  0x9900dd,
  0xdd99ff, // "ultraviolet"
  0xffffff, // "white"
};
static constexpr byte NumColours = sizeof(Colours) / sizeof(Colours[0]);


byte pot2Range(int potVar, byte maxVal) {   // returns [0,maxVal] inclusive
  long n = potVar;
  n *= (maxVal + 1);
  n /= 1024;
  return n;
}


static constexpr uint8_t BpmChoices[] = { 4, 8, 15, 25, 40, 60, 80, 100, 120, 140, 160, 200 };
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

static constexpr uint16_t uiOverrideTimeMs = 300;
static constexpr uint16_t uiFlashOverrideTimeMs = 500;
static constexpr uint16_t uiFlashOffTimeStartMs = 400;

uint16_t uiLastMs = 0;


void overrideUiCol(uint32_t col, bool flash, bool force) {
  if (uiOverrideMsRemaining == 0 || force) {
    uiOverrideMsRemaining = flash ? uiFlashOverrideTimeMs : uiOverrideTimeMs;
    uiOverrideCol = col;
  }
}

void feedbackValue(byte val) {
  uint32_t col;
  bool flash = false;
  if (val >= NumColours) {
    flash = true;
    val -= NumColours;
  }

  col = (val < NumColours) ? Colours[val] : 0xff00ff;

  overrideUiCol(col, flash, true);
}



//               _       _   _ 
//   _ __ ___   (_)   __| | (_)
//  | '_ ` _ \  | |  / _` | | |
//  | | | | | | | | | (_| | | |
//  |_| |_| |_| |_|  \__,_| |_|
//                             
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
bool ledState = false;
void midiSend(const struct MidiNote* note, bool noteOn) {
  if (noteOn) {
    MIDI.sendNoteOn(note->note, note->vel, sendChannel);
    rememberNoteOn(note->note);
    ledState = !ledState;
    digitalWrite(13, ledState);
  }
  else {
    MIDI.sendNoteOff(note->note, 0, sendChannel);
    forgetNoteOn(note->note);
  }
}
void midiClearAllNotes() {
  for (byte i = 0; i < numPlayingNotes; ++i) {
    MIDI.sendNoteOff(playingNotes[i], 0, sendChannel);
  }
  numPlayingNotes = 0;
}



//                 _           
//   ___  ___ __ _| | ___  ___ 
//  / __|/ __/ _` | |/ _ \/ __|
//  \__ \ (_| (_| | |  __/\__ \      .  
//  |___/\___\__,_|_|\___||___/  
//                             

static const byte WesternSpaces[] = { 2, 2, 1, 2, 2, 2, 1, 2, 2, 1, 2, 2, 2 };
byte scaleWestern[7]{};
byte scaleWesternMode = 0;
void updateScaleWestern(byte mode) {
  byte note = 0;
  for (byte i = 0; i < 7; ++i) {
    scaleWestern[i] = note;
    note += WesternSpaces[i + mode];
  }
  
  scaleWesternMode = mode;
}

byte scalePentaMaj[] = { 0, 2, 4, 7, 9 };
byte scalePentaMin[] = { 0, 3, 5, 7, 10 };
byte scaleBlues[]    = { 0, 3, 5, 6, 7, 10 };
byte scalePentaVar = 0;

byte scaleJ5[] = { 0, 1, 5, 7, 11 };
byte scaleJ5Num = 0xc;
void updateScaleJ5(byte num) {
  num &= 0xf;
  
  byte lo = num & 0x3;
  scaleJ5[1] = 1 + lo;

  byte hi = num >> 2;
  scaleJ5[4] = 8 + hi;

  scaleJ5Num = num;
}

byte scaleWhole[] = { 0, 2, 4, 6, 8, 10 };
byte scaleChromatic[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };   // hmm
byte scaleDiscordVar = 0;

ScaleFamily scaleFamily = ScaleFamily::Japanese;
byte scaleSize = 5;
byte scale[12] = {};

void copyScale(const byte* src, byte len) {
  for (byte i = 0; i < len; ++i) {
    scale[i] = src[i];
  }
  scaleSize = len;
}

void updateScale(ScaleFamily fam) {
  switch (fam) {
    case ScaleFamily::Japanese:
      copyScale(scaleJ5, 5);
      break;
      
    case ScaleFamily::Penta:
      switch (scalePentaVar) {
        case 0:   copyScale(scalePentaMaj, 5);  break;
        case 1:   copyScale(scalePentaMin, 5);  break;
        case 2:   copyScale(scaleBlues, 6);  break;
      }
      break;
      
    case ScaleFamily::Western:
      copyScale(scaleWestern, 7);
      break;
      
    case ScaleFamily::Discordant:
      switch (scaleDiscordVar) {
        case 0:   copyScale(scaleWhole, 6);  break;
        case 1:   copyScale(scaleChromatic, 12);  break;        
      }
      break;

    case ScaleFamily::Count: break;
  }

  if (fam != scaleFamily) {
    scaleFamily = fam;
    EEPROM.write(int(EepAddr::ScaleFamily), byte(fam));
  }
}

void updateScaleVariant() {
  int potVal = pot.getVal(UIMode_Pot_Variant);
  
  switch (scaleFamily) {
    case ScaleFamily::Japanese:
      updateScaleJ5(pot2Range(potVal, 15));
      EEPROM.write(int(EepAddr::ScaleVariantJP), scaleJ5Num);
      feedbackValue(scaleJ5Num);
      break;
      
    case ScaleFamily::Penta:
      scalePentaVar = pot2Range(potVal, 2);
      EEPROM.write(int(EepAddr::ScaleVariantPent), scalePentaVar);
      feedbackValue(scalePentaVar);
      break;
      
    case ScaleFamily::Western:
      updateScaleWestern(pot2Range(potVal, 6));
      EEPROM.write(int(EepAddr::ScaleVariantEU), scaleWesternMode);
      feedbackValue(scaleWesternMode);
      break;
      
    case ScaleFamily::Discordant:
      scaleDiscordVar = pot2Range(potVal, 1);
      EEPROM.write(int(EepAddr::ScaleVariantDisc), scaleDiscordVar);
      feedbackValue(scaleDiscordVar);
      break;

    case ScaleFamily::Count: break;
  }

  // this copies the scale into the scale buffer
  updateScale(scaleFamily);
}



//                                   _   _             
//    __ _  ___ _ __   ___ _ __ __ _| |_(_) ___  _ __  
//   / _` |/ _ \ '_ \ / _ \ '__/ _` | __| |/ _ \| '_ \                 .
//  | (_| |  __/ | | |  __/ | | (_| | |_| | (_) | | | |
//   \__, |\___|_| |_|\___|_|  \__,_|\__|_|\___/|_| |_|
//   |___/                                             
//  
static constexpr byte NumNotesInOctave = 12;
static constexpr byte MaxOctaves = 4;
static constexpr byte MinBaseOctave = 1;
static constexpr byte MaxBaseOctave = 7;

constexpr byte calcMinNote(byte k, byte o) {
  return (k + (o * NumNotesInOctave));
}
constexpr byte calcMaxNote(byte _minNote, byte nO) {
  return (_minNote + ((nO + 1) * NumNotesInOctave));
}

byte key = 0;
byte baseOctave = 3;
byte numOctaves = 2;

byte minNote = calcMinNote(key, baseOctave);
byte maxNote = calcMaxNote(minNote, numOctaves);

unsigned long lastTimeUs = 0;
unsigned long tickProgressUs = 0;

bool rand50() {
  return (byte(random()) < 0x80u);
}

void setKey(byte newKey) {
  if (newKey == key)
    return;

  key = newKey;
  minNote = calcMinNote(key, baseOctave);
  maxNote = calcMaxNote(minNote, numOctaves);
}
void setBaseOctave(byte newOctave) {
  if (newOctave == baseOctave)
    return;

  baseOctave = newOctave;
  minNote = calcMinNote(key, baseOctave);
  maxNote = calcMaxNote(minNote, numOctaves);
}

void generateNewNote(MidiNote* note, bool init = false) {
  
  byte newNote = minNote;
  newNote += scale[random(scaleSize)];

  if (init) {
    note->oct = random(0, numOctaves);
  }
  else if (rand50()) {
    if ((rand50() || note->oct == 0) && ((note->oct + 1) < numOctaves)) {
      note->oct += 1;
    }
    else if (note->oct > 0) {
      note->oct -= 1;
    }
  }

  newNote += note->oct * 12;

  note->note = newNote;

  note->vel = random(100, 127);
}

void regenPattern() {
  for (byte i=0; i<MaxNotes; ++i)
  {
    generateNewNote(&noteBuf[i], true);
  }
}


//         _             _                _    
//   _ __ | | __ _ _   _| |__   __ _  ___| | __
//  | '_ \| |/ _` | | | | '_ \ / _` |/ __| |/ /
//  | |_) | | (_| | |_| | |_) | (_| | (__|   < 
//  | .__/|_|\__,_|\__, |_.__/ \__,_|\___|_|\_\                      .
//  |_|            |___/                       

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
    if (random(0, 1000) > threshold) {
      overrideUiCol(0xddaaaa, false, false);
      generateNewNote(&noteBuf[nextNote]);
    }
  }
}



//           _ 
//   _   _  (_)
//  | | | | | |
//  | |_| | | |
//   \__,_| |_|
//             

void uiDefaultModeAction(uint8_t action) {
  switch (action) {
    case Btn_Thru:
      ++bpmChoice;
      if (bpmChoice >= NumBpmChoices)
        bpmChoice = 0;
        
      setBpm(BpmChoices[bpmChoice]);
      EEPROM.write(int(EepAddr::BpmChoice), bpmChoice);
      static_assert(NumBpmChoices <= 12);
      feedbackValue(bpmChoice);
      break;

    case Btn_Chnl:
      ++numOctaves;
      if (numOctaves >= MaxOctaves)
        numOctaves = 0;        
      EEPROM.write(int(EepAddr::NumOctaves), numOctaves);
      feedbackValue(numOctaves);
      regenPattern();
      break;

    case Btn_Play:
      ScaleFamily newFam = nextFamily(scaleFamily);
      updateScale(newFam);
      feedbackValue(byte(newFam));
      regenPattern();
      break;
  }
}

void uiPlayModeAction(uint8_t action) {
  switch (action) {
    case Btn_Chnl:
      byte newBaseOctave = (baseOctave < (MaxBaseOctave - 1)) ? baseOctave + 1 : MinBaseOctave;
      setBaseOctave(newBaseOctave);
      EEPROM.write(int(EepAddr::BaseOctave), baseOctave);
      feedbackValue(baseOctave);
      regenPattern();
      break;
  }
}

void uiAction(uint8_t action, uint8_t mode) {
  switch (mode) {
    case UIMode_Default:      uiDefaultModeAction(action); break;
    case UIMode_Thru:         break;
    case UIMode_Chnl:         break;
    case UIMode_Play:         uiPlayModeAction(action); ;
    case UIMode_ThruChnl:     break;
    case UIMode_ThruPlay:     break;
    case UIMode_ChnlPlay:     break;
    case UIMode_ThruChnlPlay: break;
  }
}

void applyPotValue(int val, uint8_t mode) {
  switch (mode) {
    case UIMode_Pot_Variant:
      updateScaleVariant();
      regenPattern();
      break;
    
    case UIMode_ThruChnlPlay:
      sendChannel = map(val, 0, 1023, 0, 15);
      EEPROM.write(int(EepAddr::Channel), sendChannel);
      feedbackValue(sendChannel);
      break;
  }
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


void updateUI(bool force = false) {
  bool uiChanged = force;
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
      if (uiOverrideMsRemaining < uiOverrideTimeMs || uiOverrideMsRemaining > uiFlashOffTimeStartMs) {
        colour = uiOverrideCol; 
      }
      else {
        colour = 0x000000;
      }
    }
    
    uiPixel.setPixelColor(0, uiPixel.gamma32(colour));
    uiPixel.show();
  }
}



//   _                 _      _                   
//  | |__   ___   ___ | |_   | | ___   ___  _ __  
//  | '_ \ / _ \ / _ \| __|  | |/ _ \ / _ \| '_ \                .
//  | |_) | (_) | (_) | |_   | | (_) | (_) | |_) |
//  |_.__/ \___/ \___/ \__|  |_|\___/ \___/| .__/ 
//                                         |_|    

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

  byte scaleFam;
  EEPROM.get(int(EepAddr::ScaleFamily), scaleFam);
  if (scaleFam >= byte(ScaleFamily::Count)) {
    scaleFam = 0;
  }
  scaleFamily = ScaleFamily(scaleFam);

  byte val;
  EEPROM.get(int(EepAddr::ScaleVariantJP), val);
  updateScaleJ5((val < 16) ? val : 0);
  EEPROM.get(int(EepAddr::ScaleVariantPent), val);
  scalePentaVar = (val < 3) ? val : 0;
  EEPROM.get(int(EepAddr::ScaleVariantEU), val);
  updateScaleWestern((val < 7) ? val : 0);
  EEPROM.get(int(EepAddr::ScaleVariantDisc), val);
  scaleDiscordVar = (val < 2) ? val : 0;

  updateScale(scaleFamily);

  EEPROM.get(int(EepAddr::NumOctaves), numOctaves);
  numOctaves = (numOctaves < 5) ? numOctaves : 2;
  EEPROM.get(int(EepAddr::BaseOctave), val);
  val = (val < MaxBaseOctave) ? val : 3;
  setBaseOctave(baseOctave);

  //TODO:
//  Chance,
//  RatchetChance,
//  RatchetIntensity,
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

  regenPattern();

  // ensure we instantly play the first note
  tickProgressUs = usPerQn + 1;

  updateUI(true);
}


void loop() {
  random(100);
  
  updateUI();

  updatePlayback();
}
