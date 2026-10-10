// Clash data logger: prompts zone/strength/face and streams each clash window to MATLAB.

#include <Arduino.h>
#include <Wire.h>
#include <FastLED.h>
#include <math.h>

#define SDA_PIN     4
#define SCL_PIN     5
#define LED_PIN     0
#define NUM_LEDS    55
#define ENCODER_SW  15

uint8_t MPU_ADDR = 0x68;          // 0x69 is tried automatically
#define FS_HZ             500
#define ACCEL_LSB_PER_G   2048.0f   // +/-16 g
#define GYRO_LSB_PER_DPS  32.8f     // +/-1000 dps

float triggerG    = 3.0f;           // |a| threshold (g)
float triggerJerk = 1.0f;           // jump in |a| between samples (g)
#define PEAK_SEARCH   8             // samples searched for the |a| peak
#define REFRACTORY    150           // samples (300 ms)

#define LOG_PRE   64                // samples before the peak
#define LOG_POST  192               // samples after the peak
#define LOG_LEN   (LOG_PRE + LOG_POST)
#define RING      512

#define SETTLE_MS        2000       // ignore impacts after a clash
#define REJECT_WINDOW_MS 3000
#define ZONE_LED_LEVEL   40

const char *ZONE_NAMES[3]     = {"BOTTOM", "MIDDLE", "TOP"};
const char *STRENGTH_NAMES[3] = {"LIGHT", "MEDIUM", "HARD"};
const char *FACE_NAMES[4]     = {"FRONT", "BACK", "LEFT", "RIGHT"};

struct Prompt { uint8_t zone, strength, face; };
#define BLOCK_SIZE 36
Prompt block[BLOCK_SIZE];
int    blockIndex = 0;
int    blockNumber = 0;
Prompt current, lastSaved;
bool   redoActive = false;

struct Sample { float ax, ay, az, gx, gy, gz; };
Sample   ring[RING];
uint32_t total = 0;

float    prevMag = 1.0f;
uint32_t lastTrigger = 0;
bool     everTriggered = false;
bool     pending = false;
uint32_t pendingTrig = 0;
float    lastJerk = 0;
uint32_t overflows = 0;

CRGB leds[NUM_LEDS];
Sample logWin[LOG_LEN];

uint32_t clashId = 0;
bool paused = false;
unsigned long lastClashMs = 0;
bool lastRejectable = false;

unsigned long rateStartMs = 0;
uint32_t rateStartTotal = 0;
float measuredFs = 0;

void mpuWrite(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t mpuRead8(uint8_t reg)
{
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

void fifoReset()
{
    mpuWrite(0x6A, 0x04);   // FIFO reset
    delayMicroseconds(50);
    mpuWrite(0x6A, 0x40);   // FIFO enable
}

bool i2cPresent(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void i2cScan()
{
    Serial.print("#INFO,I2C devices on GP4/GP5:");
    bool any = false;
    for (uint8_t a = 1; a < 127; a++)
        if (i2cPresent(a)) { Serial.print(" 0x"); Serial.print(a, HEX); any = true; }
    Serial.println(any ? "" : " none - check wiring/power");
}

bool mpuBegin()
{
    if (i2cPresent(0x68)) MPU_ADDR = 0x68;
    else if (i2cPresent(0x69)) MPU_ADDR = 0x69;
    else { i2cScan(); return false; }

    mpuWrite(0x6B, 0x80);   // reset
    delay(100);
    mpuWrite(0x6B, 0x01);   // wake, gyro PLL clock
    delay(10);
    mpuWrite(0x1A, 0x01);   // DLPF 184 Hz, gyro rate 1 kHz
    mpuWrite(0x19, 0x01);   // 1 kHz / 2 = 500 Hz
    mpuWrite(0x1B, 0x10);   // +/-1000 dps
    mpuWrite(0x1C, 0x18);   // +/-16 g
    mpuWrite(0x23, 0x78);   // FIFO: accel + gyro
    fifoReset();

    uint8_t who = mpuRead8(0x75);
    Serial.print("#INFO,MPU at 0x");  Serial.print(MPU_ADDR, HEX);
    Serial.print(", WHO_AM_I = 0x");  Serial.println(who, HEX);

    delay(60);
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x72);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2);
    uint16_t count = Wire.available() >= 2 ? (((uint16_t)Wire.read() << 8) | Wire.read()) : 0;
    Serial.print("#INFO,FIFO bytes after 60 ms = ");
    Serial.print(count);
    Serial.println(" (expected ~360)");
    fifoReset();
    return count > 0;
}

inline Sample &at(uint32_t i) { return ring[i % RING]; }
inline float accelMag(const Sample &s) { return sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az); }
inline float gyroMag(const Sample &s)  { return sqrtf(s.gx*s.gx + s.gy*s.gy + s.gz*s.gz); }

void pushSample(const Sample &s)
{
    ring[total % RING] = s;
    float mag  = accelMag(s);
    float jerk = fabsf(mag - prevMag);
    prevMag = mag;

    bool free = !everTriggered || (total - lastTrigger) >= REFRACTORY;
    if (free && mag >= triggerG && jerk >= triggerJerk)
    {
        everTriggered = true;
        lastTrigger = total;
        lastJerk = jerk;
        if (!pending) { pending = true; pendingTrig = total; }
    }
    total++;
}

void readFifo()
{
    if (mpuRead8(0x3A) & 0x10)      // overflow
    {
        fifoReset();
        overflows++;
        pending = false;            // window would contain a gap
        return;
    }

    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x72);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2);
    if (Wire.available() < 2) return;
    uint16_t count = ((uint16_t)Wire.read() << 8) | Wire.read();
    int samples = count / 12;

    while (samples > 0)
    {
        int chunk = samples > 20 ? 20 : samples;
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(0x74);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)(chunk * 12));
        if (Wire.available() < chunk * 12) return;

        for (int i = 0; i < chunk; i++)
        {
            int16_t r[6];
            for (int j = 0; j < 6; j++)
                r[j] = (int16_t)(((uint16_t)Wire.read() << 8) | Wire.read());
            Sample s;
            s.ax = r[0] / ACCEL_LSB_PER_G;
            s.ay = r[1] / ACCEL_LSB_PER_G;
            s.az = r[2] / ACCEL_LSB_PER_G;
            s.gx = r[3] / GYRO_LSB_PER_DPS;
            s.gy = r[4] / GYRO_LSB_PER_DPS;
            s.gz = r[5] / GYRO_LSB_PER_DPS;
            pushSample(s);
        }
        samples -= chunk;
    }
}

void updateRate()
{
    unsigned long now = millis();
    if (now - rateStartMs >= 2000)
    {
        measuredFs = (total - rateStartTotal) * 1000.0f / (now - rateStartMs);
        rateStartMs = now;
        rateStartTotal = total;
    }
}

void newBlock()
{
    int n = 0;
    for (int z = 0; z < 3; z++)
        for (int st = 0; st < 3; st++)
            for (int f = 0; f < 4; f++)
                block[n++] = {(uint8_t)z, (uint8_t)st, (uint8_t)f};
    for (int i = BLOCK_SIZE - 1; i > 0; i--)
    {
        int j = random(0, i + 1);
        Prompt t = block[i]; block[i] = block[j]; block[j] = t;
    }
    blockIndex = 0;
    blockNumber++;
    Serial.print("#BLOCK,");
    Serial.println(blockNumber);
}

void showZone(int zone)
{
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    int third = NUM_LEDS / 3;
    int start = zone * third;
    int end = (zone == 2) ? NUM_LEDS : start + third;
    for (int i = start; i < end; i++) leds[i] = CRGB(0, 0, ZONE_LED_LEVEL);
    FastLED.show();
}

void flash(CRGB c)
{
    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(100);
}

void announce()
{
    showZone(current.zone);
    Serial.print("#NEXT,");
    Serial.print(ZONE_NAMES[current.zone]);     Serial.print(',');
    Serial.print(STRENGTH_NAMES[current.strength]); Serial.print(',');
    Serial.print(FACE_NAMES[current.face]);     Serial.print(',');
    Serial.print(blockIndex);                   Serial.print('/');
    Serial.println(BLOCK_SIZE);
}

void nextPrompt()
{
    redoActive = false;
    if (blockIndex >= BLOCK_SIZE) newBlock();
    current = block[blockIndex++];
    announce();
}

void deferCurrent()
{
    if (redoActive) { Serial.println("#INFO,finish the repeated strike first"); return; }
    blockIndex--;
    Prompt p = block[blockIndex];
    for (int i = blockIndex; i < BLOCK_SIZE - 1; i++) block[i] = block[i + 1];
    block[BLOCK_SIZE - 1] = p;
    current = block[blockIndex++];
    announce();
}

void rejectLast()
{
    if (!lastRejectable) return;
    lastRejectable = false;
    Serial.print("#REJECT,");
    Serial.println(clashId);
    flash(CRGB(50, 0, 0));
    blockIndex--;
    current = lastSaved;
    redoActive = true;
    announce();
}

uint32_t findAnchor(uint32_t trig, float &peak)
{
    uint32_t best = trig;
    peak = -1;
    for (uint32_t i = trig; i < trig + PEAK_SEARCH; i++)
    {
        float m = accelMag(at(i));
        if (m > peak) { peak = m; best = i; }
    }
    return best;
}

void printClash(float peak)
{
    float preGyro = 0;
    for (int i = 0; i < LOG_PRE - 10; i++) preGyro += gyroMag(logWin[i]);
    preGyro /= (LOG_PRE - 10);

    Serial.print("#CLASH,");
    Serial.print(clashId);                         Serial.print(',');
    Serial.print(ZONE_NAMES[current.zone]);        Serial.print(',');
    Serial.print(STRENGTH_NAMES[current.strength]); Serial.print(',');
    Serial.print(FACE_NAMES[current.face]);        Serial.print(',');
    Serial.print(peak, 3);                         Serial.print(',');
    Serial.print(lastJerk, 3);                     Serial.print(',');
    Serial.print(preGyro, 1);                      Serial.print(',');
    Serial.print(measuredFs, 1);                   Serial.print(',');
    Serial.println(overflows);

    char line[96];
    for (int i = 0; i < LOG_LEN; i++)
    {
        const Sample &s = logWin[i];
        snprintf(line, sizeof(line), "%d,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f",
                 i - LOG_PRE, s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
        Serial.println(line);
    }
    Serial.println("#END");
}

void printStatus()
{
    Serial.print("#STATUS,trigger_g=");  Serial.print(triggerG, 2);
    Serial.print(",jerk_g=");            Serial.print(triggerJerk, 2);
    Serial.print(",fs_hz=");             Serial.print(measuredFs, 1);
    Serial.print(",clashes=");           Serial.print(clashId);
    Serial.print(",block=");             Serial.print(blockNumber);
    Serial.print(",overflows=");         Serial.println(overflows);
}

void handleSerial()
{
    while (Serial.available())
    {
        char c = Serial.read();
        if (c == 'x' || c == 'X') rejectLast();
        else if (c == 'n' || c == 'N') deferCurrent();
        else if (c == 'p' || c == 'P') { paused = !paused; Serial.println(paused ? "#PAUSED" : "#RESUMED"); }
        else if (c == 's' || c == 'S') printStatus();
        else if (c == '?') announce();          // repeat current prompt
        else if (c == 't' || c == 'T') { triggerG = Serial.parseFloat(); printStatus(); }
        else if (c == 'j' || c == 'J') { triggerJerk = Serial.parseFloat(); printStatus(); }
    }
}

void setup()
{
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 4000) {}

    pinMode(ENCODER_SW, INPUT_PULLUP);
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(255);
    FastLED.clear();
    FastLED.show();

    Wire.setSDA(SDA_PIN);
    Wire.setSCL(SCL_PIN);
    Wire.begin();
    Wire.setClock(400000);

    if (!mpuBegin())
    {
        while (true)
        {
            Serial.println("#ERROR,MPU6050 not responding or FIFO not filling - see #INFO lines above");
            i2cScan();
            flash(CRGB(60, 0, 0));
            delay(2000);
        }
    }

    randomSeed(micros() ^ analogRead(26) ^ (analogRead(27) << 8));
    rateStartMs = millis();

    Serial.println("#LOGGER,ClashLogger v3");
    Serial.println("#CONFIG,fs=500,accel=16g,gyro=1000dps,dlpf=184Hz,pre=64,post=192");
    newBlock();
    nextPrompt();
}

void loop()
{
    handleSerial();

    static bool lastButton = HIGH;
    bool button = digitalRead(ENCODER_SW);
    if (lastButton == HIGH && button == LOW) rejectLast();
    lastButton = button;

    if (lastRejectable && millis() - lastClashMs > REJECT_WINDOW_MS)
        lastRejectable = false;

    readFifo();
    updateRate();

    if (paused || millis() - lastClashMs < SETTLE_MS)
    {
        pending = false;
        delay(2);
        return;
    }

    if (pending && total >= pendingTrig + PEAK_SEARCH + LOG_POST)
    {
        pending = false;
        float peak;
        uint32_t anchor = findAnchor(pendingTrig, peak);

        if (anchor >= LOG_PRE && total - (anchor - LOG_PRE) <= RING)
        {
            for (int i = 0; i < LOG_LEN; i++) logWin[i] = at(anchor - LOG_PRE + i);
            clashId++;
            printClash(peak);
            lastSaved = current;
            flash(CRGB(0, 50, 0));
            lastClashMs = millis();
            lastRejectable = true;
            nextPrompt();
        }
        else
        {
            Serial.println("#SKIP,window incomplete");
            lastClashMs = millis();
        }
    }
    delay(2);
}
