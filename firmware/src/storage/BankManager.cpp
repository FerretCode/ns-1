#include "BankManager.h"
#include "../audio/WavFile.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t kPsramPoolSizeSamples = NS1_SAMPLE_RATE * 110; // 110s (1.8 minutes) total audio across pads (~9.7MB PSRAM)
EXTMEM static int16_t gPsramPool[kPsramPoolSizeSamples];
static int16_t *gPadPsramData[NS1_NUM_PADS] = {nullptr};
static uint32_t gPadPsramCount[NS1_NUM_PADS] = {0};

void resetPadDefaults(BankManager::PadConfig *pads) {
    for (int i = 0; i < NS1_NUM_PADS; i++) {
        pads[i] = BankManager::PadConfig{};
        snprintf(pads[i].filename, sizeof(pads[i].filename), "PAD%02d.WAV", i);
    }
}

// Remembers which bank was last loaded/created across power cycles
const char *kLastBankPath = "/BANKS/LAST.CFG";

void rememberLastBank(const char *name) {
    if (SD.exists(kLastBankPath))
        SD.remove(kLastBankPath);

    File f = SD.open(kLastBankPath, FILE_WRITE);

    if (!f)
        return;

    f.print(name);
    f.close();
}

bool readLastBank(char *out, size_t outSize) {
    File f = SD.open(kLastBankPath, FILE_READ);

    if (!f)
        return false;

    String line = f.readStringUntil('\n');

    line.trim();
    f.close();

    if (line.length() == 0)
        return false;

    snprintf(out, outSize, "%s", line.c_str());

    return true;
}
} // namespace

void BankManager::begin() {
    ensureSdReady();
    SD.mkdir(NS1_DIR_BANKS);
    SD.mkdir(NS1_DIR_REC);
    SD.mkdir(NS1_DIR_SAMPLES);

    char lastName[32];
    if (readLastBank(lastName, sizeof(lastName))) {
        snprintf(bankName, sizeof(bankName), "%s", lastName);
    }

    if (!loadBank(bankName)) {
        createBank(bankName);
    }
}

void BankManager::bankDirPath(char *out, size_t outSize) const {
    snprintf(out, outSize, "%s/%s", NS1_DIR_BANKS, bankName);
}

void BankManager::bankCfgPath(char *out, size_t outSize) const {
    char dir[64];
    bankDirPath(dir, sizeof(dir));
    snprintf(out, outSize, "%s/%s", dir, NS1_BANK_CFG_NAME);
}

bool BankManager::loadBank(const char *name) {
    if (!ensureSdReady())
        return false;

    char tmpName[32];
    snprintf(tmpName, sizeof(tmpName), "%s", name);

    char dir[64];
    snprintf(dir, sizeof(dir), "%s/%s", NS1_DIR_BANKS, tmpName);

    if (!SD.exists(dir))
        return false;

    char cfg[80];
    snprintf(cfg, sizeof(cfg), "%s/%s", dir, NS1_BANK_CFG_NAME);
    File f = SD.open(cfg, FILE_READ);

    if (!f)
        return false;

    snprintf(bankName, sizeof(bankName), "%s", tmpName);
    resetPadDefaults(pads);

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();

        if (line.length() == 0)
            continue;

        int c1 = line.indexOf(',');
        int c2 = line.indexOf(',', c1 + 1);
        int c3 = line.indexOf(',', c2 + 1);
        int c4 = line.indexOf(',', c3 + 1);

        if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0)
            continue;

        int c5 = line.indexOf(',', c4 + 1);
        int c6 = line.indexOf(',', c5 + 1);
        int c7 = line.indexOf(',', c6 + 1);
        int c8 = line.indexOf(',', c7 + 1);
        int c9 = line.indexOf(',', c8 + 1);

        int idx = line.substring(0, c1).toInt();

        if (idx < 0 || idx >= NS1_NUM_PADS)
            continue;

        String fname = line.substring(c1 + 1, c2);
        int loopVal = line.substring(c2 + 1, c3).toInt();
        int chokeVal = line.substring(c3 + 1, c4).toInt();
        int pitchVal = (c5 < 0) ? line.substring(c4 + 1).toInt()
                                : line.substring(c4 + 1, c5).toInt();

        PadConfig &p = pads[idx];

        snprintf(p.filename, sizeof(p.filename), "%s", fname.c_str());

        p.loop = loopVal != 0;
        p.chokeGroup = (int8_t)chokeVal;
        p.pitchCents = (int16_t)pitchVal;
        p.startTrim = 0;
        p.endTrim = 10000;
        p.volume = 100;
        p.gate = false;

        if (c5 >= 0) {
            String nm = (c6 < 0) ? line.substring(c5 + 1) : line.substring(c5 + 1, c6);
            snprintf(p.name, sizeof(p.name), "%s", nm.c_str());
        } else {
            p.name[0] = '\0';
        }

        if (c6 >= 0) {
            uint16_t v = (uint16_t)line.substring(c6 + 1, c7 < 0 ? line.length() : c7).toInt();
            p.startTrim = v <= 100 ? v * 100 : v;
        }
        if (c7 >= 0) {
            uint16_t v = (uint16_t)line.substring(c7 + 1, c8 < 0 ? line.length() : c8).toInt();
            p.endTrim = v <= 100 ? v * 100 : v;
        }
        if (c8 >= 0) {
            p.volume = (uint8_t)(c9 < 0 ? line.substring(c8 + 1).toInt() : line.substring(c8 + 1, c9).toInt());
        }
        if (c9 >= 0) {
            p.gate = line.substring(c9 + 1).toInt() != 0;
        }
        if (p.endTrim <= p.startTrim || p.endTrim > 10000) p.endTrim = 10000;
        if (p.volume > 100) p.volume = 100;

        char fullPath[96];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, p.filename);

        p.assigned = SD.exists(fullPath);
    }

    f.close();
    rememberLastBank(bankName);
    reloadAllPsramSamples();

    return true;
}

bool BankManager::createBank(const char *name) {
    snprintf(bankName, sizeof(bankName), "%s", name);
    resetPadDefaults(pads);

    char dir[64];
    bankDirPath(dir, sizeof(dir));
    SD.mkdir(dir);

    bool ok = saveBank();
    if (ok)
        rememberLastBank(bankName);

    return ok;
}

bool BankManager::saveBank() const {
    char dir[64];
    bankDirPath(dir, sizeof(dir));
    SD.mkdir(dir);

    char cfg[80];
    bankCfgPath(cfg, sizeof(cfg));
    if (SD.exists(cfg))
        SD.remove(cfg);

    File f = SD.open(cfg, FILE_WRITE);
    if (!f)
        return false;

    char line[128];
    for (int i = 0; i < NS1_NUM_PADS; i++) {
        const PadConfig &p = pads[i];

        int n =
            snprintf(line, sizeof(line), "%d,%s,%d,%d,%d,%s,%d,%d,%d,%d\n", i, p.filename,
                     p.loop ? 1 : 0, p.chokeGroup, p.pitchCents, p.name,
                     p.startTrim, p.endTrim, p.volume, p.gate ? 1 : 0);

        f.write((const uint8_t *)line, n);
    }

    f.close();
    return true;
}

void BankManager::padFilePath(uint8_t index, char *out, size_t outSize) const {
    char dir[64];
    bankDirPath(dir, sizeof(dir));
    snprintf(out, outSize, "%s/%s", dir, pads[index].filename);
}

void BankManager::padOrigFilePath(uint8_t index, char *out,
                                  size_t outSize) const {
    char dir[64];
    bankDirPath(dir, sizeof(dir));

    snprintf(out, outSize, "%s/P%02dORIG.WAV", dir, index);
}

void BankManager::nextTakePath(char *out, size_t outSize) const {
    for (uint32_t n = takeCounter; n < takeCounter + 10000; n++) {
        snprintf(out, outSize, "%s/TAKE%04lu.WAV", NS1_DIR_REC,
                 (unsigned long)n);

        if (!SD.exists(out)) {
            const_cast<BankManager *>(this)->takeCounter = n + 1;

            return;
        }
    }
}

bool BankManager::setPitchCents(uint8_t index, int16_t cents) {
    return applyPadProcess(index, cents, pads[index].startTrim, pads[index].endTrim);
}

bool BankManager::applyPadProcess(uint8_t index, int16_t cents, uint16_t startTrim, uint16_t endTrim) {
    char path[96], origPath[96];
    padFilePath(index, path, sizeof(path));
    padOrigFilePath(index, origPath, sizeof(origPath));

    if (!SD.exists(path))
        return false;

    if (!SD.exists(origPath)) {
        if (!WavFile::resample(path, origPath, 1.0f, NS1_SAMPLE_RATE))
            return false;
    }

    float speedRatio = powf(2.0f, cents / 1200.0f);
    if (!WavFile::cropAndResample(origPath, path, speedRatio, NS1_SAMPLE_RATE, startTrim, endTrim))
        return false;

    pads[index].pitchCents = cents;
    pads[index].startTrim = startTrim;
    pads[index].endTrim = endTrim;

    bool ok = saveBank();
    if (ok) loadPadSampleToPsram(index);
    return ok;
}

static File openDirRobust(const char *targetDir) {
    if (!targetDir || targetDir[0] == '\0') return File();

    const char *relName = targetDir[0] == '/' ? targetDir + 1 : targetDir;
    char path[64];
    snprintf(path, sizeof(path), "/%s", relName);
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';

    File dir = SD.open(path);
    if (dir) return dir;

    char pathSlash[64];
    snprintf(pathSlash, sizeof(pathSlash), "%s/", path);
    dir = SD.open(pathSlash);
    if (dir) return dir;

    dir = SD.open(relName);
    if (dir) return dir;

    // Fallback: case-insensitive scan of root directory
    File root = SD.open("/");
    if (root) {
        root.rewindDirectory();
        while (true) {
            File entry = root.openNextFile();
            if (!entry) break;

            const char *rawName = entry.name();
            if (rawName && rawName[0] != '\0') {
                const char *slash = strrchr(rawName, '/');
                const char *name = slash ? slash + 1 : rawName;

                if (strcasecmp(name, relName) == 0) {
                    char matchedPath[64];
                    snprintf(matchedPath, sizeof(matchedPath), "/%s", name);
                    File matched = SD.open(matchedPath);
                    if (!matched) matched = SD.open(name);
                    root.close();
                    return matched;
                }
            }
        }
        root.close();
    }

    return File();
}

static bool checkDirExistsRobust(const char *targetDir) {
    File dir = openDirRobust(targetDir);
    if (dir) {
        dir.close();
        return true;
    }
    return false;
}

bool BankManager::ensureSdReady() const {
    File root = SD.open("/");
    if (root) {
        root.close();
        return true;
    }

    static uint32_t lastTryMillis = 0;
    uint32_t now = millis();
    if (now - lastTryMillis < 5000) {
        return false;
    }
    lastTryMillis = now;

    pinMode(46, INPUT_PULLUP);
    bool cardOk = false;
    for (int retry = 0; retry < 2; retry++) {
        cardOk = SD.sdfs.cardBegin(SdioConfig(FIFO_SDIO));
        if (cardOk) break;
        delay(100);
    }
    if (!cardOk) {
        for (int retry = 0; retry < 2; retry++) {
            cardOk = SD.sdfs.cardBegin(SdioConfig(DMA_SDIO));
            if (cardOk) break;
            delay(100);
        }
    }

    if (cardOk && SD.sdfs.card() && SD.sdfs.sdErrorCode() == 0) {
        uint8_t parts[] = {1, 0, 2, 3, 4};
        for (uint8_t p : parts) {
            if (SD.sdfs.FsVolume::begin((FsBlockDevice*)SD.sdfs.card(), true, p)) {
                File r = SD.open("/");
                if (r) {
                    r.close();
                    return true;
                }
            }
        }
    }

    if (SD.begin(BUILTIN_SDCARD)) {
        File r = SD.open("/");
        if (r) {
            r.close();
            return true;
        }
    }

    return false;
}

int BankManager::listBanks(String *outNames, int maxCount) const {
    if (!ensureSdReady())
        return 0;

    File dir = openDirRobust("BANKS");
    if (!dir)
        return 0;

    dir.rewindDirectory();

    int count = 0;
    while (count < maxCount) {
        File entry = dir.openNextFile();

        if (!entry)
            break;

        const char *rawName = entry.name();
        if (rawName && rawName[0] != '\0') {
            const char *slash = strrchr(rawName, '/');
            const char *name = slash ? slash + 1 : rawName;

            char cleanName[64];
            snprintf(cleanName, sizeof(cleanName), "%s", name);
            size_t len = strlen(cleanName);
            if (len > 0 && cleanName[len - 1] == '/') {
                cleanName[len - 1] = '\0';
            }

            // Filter out hidden, system, macOS, recovery, and config files
            if (cleanName[0] != '\0' &&
                cleanName[0] != '.' &&
                cleanName[0] != '$' &&
                strncmp(cleanName, "System", 6) != 0 &&
                strncmp(cleanName, "FOUND.", 6) != 0 &&
                strcmp(cleanName, "lost+found") != 0 &&
                strcmp(cleanName, "LAST.CFG") != 0 &&
                !String(cleanName).equalsIgnoreCase("LAST.CFG") &&
                !String(cleanName).endsWith(".CFG") &&
                !String(cleanName).endsWith(".cfg") &&
                !String(cleanName).endsWith(".TMP") &&
                !String(cleanName).endsWith(".tmp")) {

                outNames[count++] = String(cleanName);
            }
        }
    }

    dir.close();

    return count;
}

static void scanDirectoryRecursive(File dir, const char *baseSdPath, String *outNames, int &count, int maxCount) {
    while (count < maxCount) {
        File entry = dir.openNextFile();
        if (!entry) break;

        const char *rawName = entry.name();
        if (!rawName || rawName[0] == '\0') {
            continue;
        }

        const char *slash = strrchr(rawName, '/');
        const char *baseName = slash ? slash + 1 : rawName;

        char cleanName[128];
        snprintf(cleanName, sizeof(cleanName), "%s", baseName);
        size_t len = strlen(cleanName);
        if (len > 0 && cleanName[len - 1] == '/') {
            cleanName[len - 1] = '\0';
        }

        if (cleanName[0] == '\0' || cleanName[0] == '.' || cleanName[0] == '$' ||
            strncmp(cleanName, "System", 6) == 0 ||
            strncmp(cleanName, "FOUND.", 6) == 0 ||
            strcmp(cleanName, "lost+found") == 0) {
            continue;
        }

        char currentPath[128];
        if (baseSdPath && baseSdPath[0] != '\0') {
            snprintf(currentPath, sizeof(currentPath), "%s/%s", baseSdPath, cleanName);
        } else {
            snprintf(currentPath, sizeof(currentPath), "%s", cleanName);
        }

        String nameStr = String(cleanName);
        String upper = nameStr;
        upper.toUpperCase();

        if (upper.endsWith(".WAV")) {
            outNames[count++] = String(currentPath);
        } else {
            File subDir = SD.open(currentPath);
            if (subDir) {
                if (subDir.isDirectory()) {
                    subDir.rewindDirectory();
                    scanDirectoryRecursive(subDir, currentPath, outNames, count, maxCount);
                }
                subDir.close();
            }
        }
    }
}

int BankManager::listSampleFiles(String *outNames, int maxCount) const {
    if (!ensureSdReady())
        return 0;

    int count = 0;

    File samplesDir = openDirRobust("SAMPLES");
    if (samplesDir) {
        samplesDir.rewindDirectory();
        scanDirectoryRecursive(samplesDir, NS1_DIR_SAMPLES, outNames, count, maxCount);
        samplesDir.close();
    }

    if (count < maxCount) {
        File recDir = openDirRobust("REC");
        if (recDir) {
            recDir.rewindDirectory();
            scanDirectoryRecursive(recDir, NS1_DIR_REC, outNames, count, maxCount);
            recDir.close();
        }
    }

    return count;
}

bool BankManager::importSample(uint8_t index, const char *srcPath) {
    File src = SD.open(srcPath, FILE_READ);
    if (!src) {
        error = "Could not open file";

        Serial.printf("Could not open file at %s\n", srcPath);

        return false;
    }

    WavFile::Info info;
    bool okInfo = WavFile::readInfo(src, info) && info.numChannels > 0 && info.dataBytes > 0;

    if (!okInfo) {
        src.close();

        error = "Invalid WAV format";

        Serial.printf("%s is not a valid WAV file\n", srcPath);

        return false;
    }

    char dir[64];
    bankDirPath(dir, sizeof(dir));
    char tmpPath[96];

    snprintf(tmpPath, sizeof(tmpPath), "%s/IMPORT.TMP", dir);

    if (SD.exists(tmpPath))
        SD.remove(tmpPath);

    // Pass 1: fold to mono if the source has multiple channels and convert any
    // sample format (8/16/24/32-bit PCM or 32-bit Float) into 16-bit PCM at
    // source's own sample rate, into a scratch file
    uint32_t bytesPerSample = info.bitsPerSample / 8;
    if (bytesPerSample == 0) bytesPerSample = 2;
    uint32_t bytesPerFrame = info.numChannels * bytesPerSample;
    uint32_t frameCount = info.dataBytes / bytesPerFrame;

    {
        File tmp = SD.open(tmpPath, FILE_WRITE);
        if (!tmp) {
            src.close();

            error = "Could not open scratch file";

            Serial.printf("Could not open scratch file at %s\n", tmpPath);

            return false;
        }

        WavFile::writeHeader(tmp, info.sampleRate, 1, 16, frameCount * 2);

        src.seek(info.dataOffset);

        const int kFrames = 2048;
        static uint8_t rawBuf[kFrames * 32]; // room for up to 8 channels * 4 bytes/sample
        static int16_t outBuf[kFrames];
        uint32_t framesLeft = frameCount;

        while (framesLeft > 0) {
            int chunk =
                framesLeft < (uint32_t)kFrames ? (int)framesLeft : kFrames;

            int got = src.read(rawBuf, chunk * bytesPerFrame);
            int gotFrames = got / (int)bytesPerFrame;

            if (gotFrames <= 0)
                break;

            for (int f = 0; f < gotFrames; f++) {
                const uint8_t *framePtr = rawBuf + f * bytesPerFrame;
                int32_t sum = 0;
                for (uint16_t ch = 0; ch < info.numChannels; ch++) {
                    const uint8_t *samplePtr = framePtr + ch * bytesPerSample;
                    sum += WavFile::decodeSampleTo16(samplePtr, info.audioFormat, info.bitsPerSample);
                }
                int16_t sample = (int16_t)(sum / (int32_t)info.numChannels);

                uint32_t currentFrame = frameCount - framesLeft + f;
                if (currentFrame < 64) {
                    sample = (int16_t)(sample * (int32_t)currentFrame / 64);
                }
                if (frameCount > 128 && currentFrame >= frameCount - 128) {
                    uint32_t remain = frameCount - 1 - currentFrame;
                    sample = (int16_t)(sample * (int32_t)remain / 128);
                }

                outBuf[f] = sample;
            }

            tmp.write((const uint8_t *)outBuf, gotFrames * 2);
            framesLeft -= (uint32_t)gotFrames;
        }

        tmp.close();
    }

    src.close();

    // Pass 2: resample the mono scratch file so the pad file always ends
    char padPath[96];
    padFilePath(index, padPath, sizeof(padPath));

    float speedRatio = (float)info.sampleRate / (float)NS1_SAMPLE_RATE;

    bool ok = WavFile::resample(tmpPath, padPath, speedRatio, NS1_SAMPLE_RATE);

    SD.remove(tmpPath);
    if (!ok) {
        error = "Could not open scratch file";

        Serial.printf("Could not open scratch file %s into mono\n", tmpPath);

        return false;
    }

    // invalidate any pitch-shift backup left over from whatever was
    // previously on this pad slot
    char origPath[96];
    padOrigFilePath(index, origPath, sizeof(origPath));

    if (SD.exists(origPath))
        SD.remove(origPath);

    // derive a display name from the source filename: strip any directory
    // and extension, truncate to fit PadConfig::name[13].
    String base = String(srcPath);
    int slash = base.lastIndexOf('/');

    if (slash >= 0)
        base = base.substring(slash + 1);

    int dot = base.lastIndexOf('.');
    if (dot > 0)
        base = base.substring(0, dot);

    PadConfig &p = pads[index];

    p.assigned = true;
    p.pitchCents = 0;
    snprintf(p.name, sizeof(p.name), "%s", base.c_str());

    bool savedOk = saveBank();
    if (savedOk) loadPadSampleToPsram(index);
    return savedOk;
}

void BankManager::loadPadSampleToPsram(uint8_t index) {
    reloadAllPsramSamples();
}

void BankManager::reloadAllPsramSamples() {
    uint32_t poolOffset = 0;

    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        gPadPsramData[i] = nullptr;
        gPadPsramCount[i] = 0;

        if (!pads[i].assigned) continue;

        char path[96];
        padFilePath(i, path, sizeof(path));
        if (!SD.exists(path)) continue;

        File src = SD.open(path, FILE_READ);
        if (!src) continue;

        WavFile::Info info;
        if (!WavFile::readInfo(src, info) || info.numChannels == 0 || info.dataBytes == 0) {
            src.close();
            continue;
        }

        const uint32_t bytesPerSample = info.bitsPerSample / 8;
        const uint32_t bytesPerFrame = info.numChannels * (bytesPerSample ? bytesPerSample : 2);
        uint32_t totalFrames = info.dataBytes / bytesPerFrame;

        uint32_t avail = kPsramPoolSizeSamples - poolOffset;
        if (totalFrames > avail) totalFrames = avail;
        if (totalFrames == 0) {
            src.close();
            continue;
        }

        src.seek(info.dataOffset);
        constexpr int kChunkFrames = 1024;
        static uint8_t rawBuf[kChunkFrames * 32];

        uint32_t loaded = 0;
        while (loaded < totalFrames) {
            int framesThisChunk = (totalFrames - loaded) < (uint32_t)kChunkFrames ? (int)(totalFrames - loaded) : kChunkFrames;
            int got = src.read(rawBuf, framesThisChunk * bytesPerFrame);
            int gotFrames = got / (int)bytesPerFrame;
            if (gotFrames <= 0) break;

            for (int f = 0; f < gotFrames; f++) {
                const uint8_t *framePtr = rawBuf + f * bytesPerFrame;
                int32_t sum = 0;
                for (uint16_t ch = 0; ch < info.numChannels; ch++) {
                    const uint8_t *samplePtr = framePtr + ch * bytesPerSample;
                    sum += WavFile::decodeSampleTo16(samplePtr, info.audioFormat, info.bitsPerSample);
                }
                gPsramPool[poolOffset + loaded++] = (int16_t)(sum / (int32_t)info.numChannels);
            }
        }

        src.close();
        if (loaded > 0) {
            gPadPsramData[i] = &gPsramPool[poolOffset];
            gPadPsramCount[i] = loaded;
            poolOffset += loaded;
        } else {
            gPadPsramData[i] = nullptr;
            gPadPsramCount[i] = 0;
        }
    }
}

const int16_t *BankManager::getPadSampleData(uint8_t index) const {
    if (index >= NS1_NUM_PADS) return nullptr;
    return gPadPsramData[index];
}

uint32_t BankManager::getPadSampleLength(uint8_t index) const {
    if (index >= NS1_NUM_PADS) return 0;
    return gPadPsramCount[index];
}

String *BankManager::getError() { return &error; }

namespace {
EXTMEM uint8_t gFastCopyBuf[32768];

bool copyFileFast(File &src, File &dst) {
    if (!src || !dst) return false;
    while (src.available()) {
        int n = src.read(gFastCopyBuf, sizeof(gFastCopyBuf));
        if (n <= 0) break;
        dst.write(gFastCopyBuf, n);
        yield();
    }
    return true;
}
} // namespace

bool BankManager::copyPad(uint8_t srcIndex, uint8_t dstIndex) {
    if (srcIndex >= NS1_NUM_PADS || dstIndex >= NS1_NUM_PADS || srcIndex == dstIndex) return false;
    if (!pads[srcIndex].assigned) return false;

    char srcPath[96], dstPath[96];
    padFilePath(srcIndex, srcPath, sizeof(srcPath));
    padFilePath(dstIndex, dstPath, sizeof(dstPath));

    if (SD.exists(dstPath)) SD.remove(dstPath);

    File srcFile = SD.open(srcPath, FILE_READ);
    if (!srcFile) return false;

    File dstFile = SD.open(dstPath, FILE_WRITE);
    if (!dstFile) { srcFile.close(); return false; }

    copyFileFast(srcFile, dstFile);
    srcFile.close();
    dstFile.close();

    char srcOrig[96], dstOrig[96];
    padOrigFilePath(srcIndex, srcOrig, sizeof(srcOrig));
    padOrigFilePath(dstIndex, dstOrig, sizeof(dstOrig));
    if (SD.exists(srcOrig)) {
        if (SD.exists(dstOrig)) SD.remove(dstOrig);
        File sOrig = SD.open(srcOrig, FILE_READ);
        File dOrig = SD.open(dstOrig, FILE_WRITE);
        if (sOrig && dOrig) {
            copyFileFast(sOrig, dOrig);
        }
        if (sOrig) sOrig.close();
        if (dOrig) dOrig.close();
    } else if (SD.exists(dstOrig)) {
        SD.remove(dstOrig);
    }

    pads[dstIndex] = pads[srcIndex];
    snprintf(pads[dstIndex].filename, sizeof(pads[dstIndex].filename), "PAD%02d.WAV", dstIndex);

    bool ok = saveBank();
    if (ok) reloadAllPsramSamples();
    return ok;
}

static bool loadBankConfigFromSD(const char *bName, BankManager::PadConfig outPads[NS1_NUM_PADS]) {
    for (int i = 0; i < NS1_NUM_PADS; i++) {
        outPads[i].assigned = false;
        outPads[i].loop = false;
        outPads[i].chokeGroup = -1;
        outPads[i].pitchCents = 0;
        outPads[i].startTrim = 0;
        outPads[i].endTrim = 10000;
        outPads[i].volume = 100;
        outPads[i].gate = false;
        snprintf(outPads[i].filename, sizeof(outPads[i].filename), "PAD%02d.WAV", i);
        outPads[i].name[0] = '\0';
    }

    char cfgPath[96], bankDir[96];
    snprintf(bankDir, sizeof(bankDir), "%s/%s", NS1_DIR_BANKS, bName);
    snprintf(cfgPath, sizeof(cfgPath), "%s/%s/BANK.CFG", NS1_DIR_BANKS, bName);

    File f = SD.open(cfgPath, FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int c1 = line.indexOf(',');
        int c2 = line.indexOf(',', c1 + 1);
        int c3 = line.indexOf(',', c2 + 1);
        int c4 = line.indexOf(',', c3 + 1);
        if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) continue;

        int c5 = line.indexOf(',', c4 + 1);
        int c6 = line.indexOf(',', c5 + 1);
        int c7 = line.indexOf(',', c6 + 1);
        int c8 = line.indexOf(',', c7 + 1);
        int c9 = line.indexOf(',', c8 + 1);

        int idx = line.substring(0, c1).toInt();
        if (idx < 0 || idx >= NS1_NUM_PADS) continue;

        BankManager::PadConfig &p = outPads[idx];
        String fname = line.substring(c1 + 1, c2);
        snprintf(p.filename, sizeof(p.filename), "%s", fname.c_str());
        p.loop = line.substring(c2 + 1, c3).toInt() != 0;
        p.chokeGroup = (int8_t)line.substring(c3 + 1, c4).toInt();
        p.pitchCents = (int16_t)((c5 < 0) ? line.substring(c4 + 1).toInt() : line.substring(c4 + 1, c5).toInt());

        if (c5 >= 0) {
            String nm = (c6 < 0) ? line.substring(c5 + 1) : line.substring(c5 + 1, c6);
            snprintf(p.name, sizeof(p.name), "%s", nm.c_str());
        }
        if (c6 >= 0) {
            uint16_t v = (uint16_t)line.substring(c6 + 1, c7 < 0 ? line.length() : c7).toInt();
            p.startTrim = v <= 100 ? v * 100 : v;
        }
        if (c7 >= 0) {
            uint16_t v = (uint16_t)line.substring(c7 + 1, c8 < 0 ? line.length() : c8).toInt();
            p.endTrim = v <= 100 ? v * 100 : v;
        }
        if (c8 >= 0) {
            p.volume = (uint8_t)(c9 < 0 ? line.substring(c8 + 1).toInt() : line.substring(c8 + 1, c9).toInt());
        }
        if (c9 >= 0) {
            p.gate = line.substring(c9 + 1).toInt() != 0;
        }

        char fullPath[96];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", bankDir, p.filename);
        p.assigned = SD.exists(fullPath);
    }
    f.close();
    return true;
}

static bool saveBankConfigToSD(const char *bName, const BankManager::PadConfig inPads[NS1_NUM_PADS]) {
    char bankDir[96], cfgPath[96];
    snprintf(bankDir, sizeof(bankDir), "%s/%s", NS1_DIR_BANKS, bName);
    snprintf(cfgPath, sizeof(cfgPath), "%s/%s/BANK.CFG", NS1_DIR_BANKS, bName);

    SD.mkdir(bankDir);
    if (SD.exists(cfgPath)) SD.remove(cfgPath);

    File f = SD.open(cfgPath, FILE_WRITE);
    if (!f) return false;

    char line[128];
    for (int i = 0; i < NS1_NUM_PADS; i++) {
        const BankManager::PadConfig &p = inPads[i];
        int n = snprintf(line, sizeof(line), "%d,%s,%d,%d,%d,%s,%d,%d,%d,%d\n", i, p.filename,
                         p.loop ? 1 : 0, p.chokeGroup, p.pitchCents, p.name,
                         p.startTrim, p.endTrim, p.volume, p.gate ? 1 : 0);
        f.write((const uint8_t *)line, n);
    }
    f.close();
    return true;
}

bool BankManager::copyPadToBank(uint8_t srcIndex, const char *destBankName, uint8_t dstIndex) {
    if (!destBankName || destBankName[0] == '\0') return false;
    if (strcmp(destBankName, bankName) == 0) {
        return copyPad(srcIndex, dstIndex);
    }
    if (srcIndex >= NS1_NUM_PADS || dstIndex >= NS1_NUM_PADS) return false;
    if (!pads[srcIndex].assigned) return false;

    char srcPath[96], destPath[96];
    padFilePath(srcIndex, srcPath, sizeof(srcPath));
    snprintf(destPath, sizeof(destPath), "%s/%s/PAD%02d.WAV", NS1_DIR_BANKS, destBankName, dstIndex);

    if (SD.exists(destPath)) SD.remove(destPath);

    File srcFile = SD.open(srcPath, FILE_READ);
    if (!srcFile) return false;

    File dstFile = SD.open(destPath, FILE_WRITE);
    if (!dstFile) { srcFile.close(); return false; }

    copyFileFast(srcFile, dstFile);
    srcFile.close();
    dstFile.close();

    char srcOrig[96], destOrig[96];
    padOrigFilePath(srcIndex, srcOrig, sizeof(srcOrig));
    snprintf(destOrig, sizeof(destOrig), "%s/%s/P%02dORIG.WAV", NS1_DIR_BANKS, destBankName, dstIndex);
    if (SD.exists(srcOrig)) {
        if (SD.exists(destOrig)) SD.remove(destOrig);
        File sOrig = SD.open(srcOrig, FILE_READ);
        File dOrig = SD.open(destOrig, FILE_WRITE);
        if (sOrig && dOrig) {
            copyFileFast(sOrig, dOrig);
        }
        if (sOrig) sOrig.close();
        if (dOrig) dOrig.close();
    } else if (SD.exists(destOrig)) {
        SD.remove(destOrig);
    }

    PadConfig targetPads[NS1_NUM_PADS];
    loadBankConfigFromSD(destBankName, targetPads);

    targetPads[dstIndex] = pads[srcIndex];
    snprintf(targetPads[dstIndex].filename, sizeof(targetPads[dstIndex].filename), "PAD%02d.WAV", dstIndex);
    targetPads[dstIndex].assigned = true;

    saveBankConfigToSD(destBankName, targetPads);

    return true;
}

bool BankManager::deletePad(uint8_t index) {
    if (index >= NS1_NUM_PADS) return false;

    gPadPsramData[index] = nullptr;
    gPadPsramCount[index] = 0;

    char path[96], origPath[96];
    padFilePath(index, path, sizeof(path));
    padOrigFilePath(index, origPath, sizeof(origPath));

    if (SD.exists(path)) {
        SD.remove(path);
    }
    if (SD.exists(origPath)) {
        SD.remove(origPath);
    }

    if (SD.exists(path)) {
        File f = SD.open(path, FILE_WRITE);
        if (f) {
            f.close();
        }
        SD.remove(path);
    }

    resetPadDefaults(&pads[index]);
    snprintf(pads[index].filename, sizeof(pads[index].filename), "PAD%02d.WAV", index);
    pads[index].assigned = false;
    pads[index].name[0] = '\0';

    saveBank();
    reloadAllPsramSamples();

    return true;
}

bool BankManager::exportBank(const char *targetBankName) {
    const char *bName = (targetBankName && targetBankName[0] != '\0') ? targetBankName : bankName;

    char exportDir[96];
    snprintf(exportDir, sizeof(exportDir), "/EXPORTS/%s", bName);
    if (!SD.exists("/EXPORTS")) SD.mkdir("/EXPORTS");
    if (!SD.exists(exportDir)) SD.mkdir(exportDir);

    char srcBankDir[96];
    snprintf(srcBankDir, sizeof(srcBankDir), "%s/%s", NS1_DIR_BANKS, bName);

    PadConfig targetPads[NS1_NUM_PADS];
    if (!loadBankConfigFromSD(bName, targetPads)) {
        return false;
    }

    int exportedCount = 0;
    for (int i = 0; i < NS1_NUM_PADS; i++) {
        if (!targetPads[i].assigned) continue;

        char srcWav[96];
        snprintf(srcWav, sizeof(srcWav), "%s/%s", srcBankDir, targetPads[i].filename);
        if (!SD.exists(srcWav)) continue;

        const char *pName = (targetPads[i].name[0] != '\0') ? targetPads[i].name : "SAMPLE";
        char destWav[96];
        snprintf(destWav, sizeof(destWav), "%s/P%02d_%s.WAV", exportDir, i + 1, pName);

        if (SD.exists(destWav)) SD.remove(destWav);

        File s = SD.open(srcWav, FILE_READ);
        File d = SD.open(destWav, FILE_WRITE);
        if (s && d) {
            copyFileFast(s, d);
            exportedCount++;
        }
        if (s) s.close();
        if (d) d.close();
    }

    return exportedCount > 0;
}

bool BankManager::exportPad(uint8_t index) {
    if (index >= NS1_NUM_PADS || !pads[index].assigned) return false;

    if (!SD.exists("/EXPORTS")) SD.mkdir("/EXPORTS");

    char srcWav[96];
    padFilePath(index, srcWav, sizeof(srcWav));
    if (!SD.exists(srcWav)) return false;

    const char *pName = (pads[index].name[0] != '\0') ? pads[index].name : "SAMPLE";
    char destWav[96];
    snprintf(destWav, sizeof(destWav), "/EXPORTS/%s_P%02d_%s.WAV", bankName, index + 1, pName);

    if (SD.exists(destWav)) SD.remove(destWav);

    File s = SD.open(srcWav, FILE_READ);
    File d = SD.open(destWav, FILE_WRITE);
    bool ok = false;
    if (s && d) {
        ok = copyFileFast(s, d);
    }
    if (s) s.close();
    if (d) d.close();
    return ok;
}
