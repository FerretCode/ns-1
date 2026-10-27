#include "App.h"
#include <SD.h>

void App::begin() {
    Serial.begin(115200);

    // Give physical SD card 250ms for internal 3.3V regulator to stabilize
    delay(250);
    pinMode(46, INPUT_PULLUP); // Teensy 4.1 _SD_DAT3 line

    bool sdOk = false;
    if (SD.open("/")) {
        sdOk = true;
    } else {
        bool cardOk = false;
        for (int retry = 0; retry < 3; retry++) {
            cardOk = SD.sdfs.cardBegin(SdioConfig(FIFO_SDIO));
            if (cardOk) break;
            delay(150);
        }
        if (!cardOk) {
            for (int retry = 0; retry < 3; retry++) {
                cardOk = SD.sdfs.cardBegin(SdioConfig(DMA_SDIO));
                if (cardOk) break;
                delay(150);
            }
        }

        if (cardOk && SD.sdfs.card() && SD.sdfs.sdErrorCode() == 0) {
            uint8_t parts[] = {1, 0, 2, 3, 4};
            for (uint8_t p : parts) {
                if (SD.sdfs.FsVolume::begin((FsBlockDevice*)SD.sdfs.card(), true, p)) {
                    File r = SD.open("/");
                    if (r) {
                        r.close();
                        sdOk = true;
                        break;
                    }
                }
            }
        }

        if (!sdOk) {
            sdOk = SD.begin(BUILTIN_SDCARD);
        }
    }

    if (!sdOk) {
        Serial.printf("NS-1: SD card init failed (err: 0x%02X, data: 0x%02X)\n",
                      SD.sdfs.sdErrorCode(), SD.sdfs.sdErrorData());
    }

    engine.begin();
    bank.begin();
    pads.begin();
    encoders.begin();
    analog.begin();
    display.begin();

    ui.begin(engine, bank, pads, encoders, analog, display);

    int assignedCount = 0;
    for (int i = 0; i < NS1_NUM_PADS; i++) {
        if (bank.pad(i).assigned) assignedCount++;
    }
    Serial.printf("NS-1: ready. bank=\"%s\" pads assigned=%d/%d\n", bank.currentBankName(),
                  assignedCount, NS1_NUM_PADS);
}

void App::update() {
    engine.update();
    ui.update();
}
