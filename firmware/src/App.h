#pragma once

#include "audio/AudioEngine.h"
#include "input/AnalogControls.h"
#include "input/Encoders.h"
#include "input/PadMatrix.h"
#include "storage/BankManager.h"
#include "ui/Display.h"
#include "ui/UI.h"

class App {
  public:
    void begin();
    void update();

  private:
    AudioEngine engine;
    BankManager bank;
    PadMatrix pads;
    Encoders encoders;
    AnalogControls analog;
    Display display;
    UI ui;
};
