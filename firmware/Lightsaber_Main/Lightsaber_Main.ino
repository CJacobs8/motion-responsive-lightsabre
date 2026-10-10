// Motion-responsive lightsabre - main firmware (Raspberry Pi Pico 2)
// SmoothSwing audio, LED blade, OLED menu, clash detection and clash-location classifier.

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <I2S.h>
#include <math.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "ClashClassifier.h"

#define LIVE_TEST 0
#if LIVE_TEST
#include "LiveTest.h"
#endif

#define SDA_PIN 4
#define SCL_PIN 5

#define MPU_ADDRESS 0x68

#define I2S_BCLK 7
#define I2S_LRC  8
#define I2S_DIN  9

#define SD_CS   17
#define SD_MISO 16
#define SD_MOSI 19
#define SD_SCK  18

#define AUDIO_RATE 22050
#define IGNITE_FULL_BUFFER_SAMPLES  65536UL   // 2.97 s at 22.05 kHz
#define RETRACT_FULL_BUFFER_SAMPLES 45056UL   // 2.04 s at 22.05 kHz

#define POWER_FADE_IN_SAMPLES   96     // ~4 ms
#define POWER_FADE_OUT_SAMPLES  330    // ~15 ms

int16_t igniteFullBuffer[IGNITE_FULL_BUFFER_SAMPLES];
int16_t retractFullBuffer[RETRACT_FULL_BUFFER_SAMPLES];

struct PowerShotClip
{
    File file;
    int16_t *buffer;          // igniteFullBuffer or retractFullBuffer
    uint32_t capacity;        // samples the buffer can hold
    uint32_t dataStart;
    uint32_t sampleCount;
    uint32_t sampleRate;
    uint32_t bufferStart;     // first sample index held in buffer
    uint32_t bufferFilled;    // samples held in buffer
    float position;
    float step;               // file rate / AUDIO_RATE
    float gain;               // fade envelope 0..1
    bool fullyLoaded;         // whole file is in RAM
    bool stopping;            // fading out, then stops
    bool loaded;
    bool active;
};

PowerShotClip igniteShot;
PowerShotClip retractShot;

struct StreamClip;
extern bool isMoving;
extern float gyroSpeed;
extern float swingVolume;

#ifndef MAX_SWING_VOLUME
#define MAX_SWING_VOLUME 0.325f
#endif

#ifndef SWING_FULL_SPEED
#define SWING_FULL_SPEED 7.5f
#endif

float clampFloat(float value, float minimum, float maximum);
float getPowerShotPlaybackPosition(bool ignite);
void stopPowerShot(PowerShotClip &clip);

#define LED_PIN 0
#define NUM_LEDS 55

CRGB bladeColour = CRGB(255, 0, 0);
#define LED_TYPE WS2812B
#define LED_ORDER GRB
#define LED_BRIGHTNESS 65

CRGB leds[NUM_LEDS];

#define OLED_SDA 20
#define OLED_SCL 21
#define ENCODER_CLK 13
#define ENCODER_DT  14
#define ENCODER_SW  15
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDRESS 0x3C
#define OLED_RESET -1
#define LONG_PRESS_TIME 800UL

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

void selectOLEDI2C() {
    Wire.end();
    Wire.setSDA(OLED_SDA);
    Wire.setSCL(OLED_SCL);
    Wire.begin();
    Wire.setClock(400000);
}

void selectMPUI2C() {
    Wire.end();
    Wire.setSDA(SDA_PIN);
    Wire.setSCL(SCL_PIN);
    Wire.begin();
    Wire.setClock(400000);
}

enum UIState { DEFAULT_SCREEN, MAIN_MENU, SUB_MENU, SELECTION_FEEDBACK };
UIState currentUIState = DEFAULT_SCREEN;
struct MenuData { const char *title; const char *items[3]; int itemCount; };
MenuData menus[] = {
  {"Blade colour", {"Red", "Green", "Blue"}, 3},
  {"Brightness", {"0%", "5%", "10%"}, 21},
  {"Volume", {"0%", "5%", "10%"}, 21},
  {"Idle animation", {"Broken Crystal", "Default", "In-Between"}, 3},
  {"Gyro sensitivity", {"Medium", "High", "Low"}, 3},
  {"Accel sensitivity", {"Medium", "High", "Low"}, 3}
};
const int MENU_COUNT = sizeof(menus) / sizeof(menus[0]);
int selectedMenu=0, selectedOption=0;
int lastCLK=HIGH;
bool menuButtonPressed=false, longPressHandled=false;
unsigned long menuButtonPressStart=0, selectionFeedbackUntil=0, lastOLEDUpdate=0;
bool oledNeedsRedraw=true;
int idleAnimationMode=1; // 0 broken crystal, 1 default, 2 in-between
float menuVolumeGain=0.75f;
const float MAX_MENU_VOLUME_GAIN = 3.5f;
int menuBrightnessPercent = 100;

void updateEncoder();
void handleMenuButton();
void applyMenuSelection();
void updateMenuDisplay();
void drawMainMenuScreen();
void drawSubMenuScreen();
void drawSelectedScreen();
void drawSaberScreen(const char *status, int bladeLength);
void drawCentredText(const char *text, int y);
void updateOLEDPowerScreen();

uint8_t bladeColorRed = 255;
uint8_t bladeColorGreen = 0;
uint8_t bladeColorBlue = 0;

#define LED_FRAME_MS 5

#define TIP_BRIGHT_COUNT 4
#define TIP_BRIGHT_BOOST 60

#define SWING_BRIGHT_BOOST 105
#define SWING_SHADE_GREEN 10

#define TIP_DRAG_LENGTH 16

unsigned long lastLedFrame = 0;
uint16_t bladeEnergyPhase = 0;
uint16_t swingEnergyPhase = 0;
bool ledBladeOn = true;
bool ledTipDrag = false;

enum BladePowerState
{
    LED_POWER_OFF,
    LED_POWER_IGNITING,
    LED_POWER_ON,
    LED_POWER_RETRACTING
};

BladePowerState bladePowerState = LED_POWER_IGNITING;
unsigned long bladePowerStart = 0;
unsigned long retractAnimationStart = 0;

bool retractAudioPlaying = false;

#define LED_IGNITE_TIME_MS 2030
#define LED_RETRACT_TIME_MS 1800

#define OLED_POWER_ANIMATION_MS 40

#define RETRACT_SAFETY_TIMEOUT_MS 2500UL

#define IGNITE_AUDIO_START_SAMPLE 6259UL
#define IGNITE_AUDIO_END_SAMPLE   37627UL

#define RETRACT_AUDIO_START_SAMPLE 1787UL
#define RETRACT_AUDIO_END_SAMPLE   34209UL

void startIgniteAudio();
void startRetractAudio();

float ledSmoothStep(float x)
{
    x = constrain(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

CRGB makeAlivePixelDefault(int pixel, int baseBrightness, uint8_t waveOffset)
{
    float position =
        (float)pixel /
        (float)(NUM_LEDS - 1);

    uint8_t slowWave =
        sin8(
            waveOffset +
            pixel * 2
        );

    uint8_t mediumWave =
        sin8(
            (uint8_t)(waveOffset * 2) +
            pixel * 7
        );

    uint8_t fineWave =
        sin8(
            (uint8_t)(waveOffset * 3) +
            pixel * 15
        );

    float brightness =
        baseBrightness;

    brightness +=
        (((float)slowWave - 128.0f) / 127.0f) *
        22.0f;

    brightness +=
        (((float)mediumWave - 128.0f) / 127.0f) *
        14.0f;

    brightness +=
        (((float)fineWave - 128.0f) / 127.0f) *
        6.0f;

    brightness +=
        position *
        14.0f;

    float tipStart =
        (float)(NUM_LEDS - TIP_BRIGHT_COUNT - 1) /
        (float)(NUM_LEDS - 1);

    if (position > tipStart)
    {
        float tipAmount =
            ledSmoothStep(
                (position - tipStart) /
                (1.0f - tipStart)
            );

        brightness +=
            tipAmount *
            TIP_BRIGHT_BOOST;
    }

    brightness =
        constrain(
            (int)brightness,
            80,
            255
        );

    return CRGB(
        scale8(
            bladeColorRed,
            (uint8_t)brightness
        ),
        scale8(
            bladeColorGreen,
            (uint8_t)brightness
        ),
        scale8(
            bladeColorBlue,
            (uint8_t)brightness
        )
    );
}

bool clashVisualActive = false;
unsigned long clashVisualStart = 0;
int clashVisualZone = 1;                 // 0 bottom, 1 middle, 2 top

#define CLASH_VISUAL_DURATION_MS 480UL   // whole effect
#define CLASH_ATTACK_MS           25UL   // flare-up time
#define CLASH_HOLD_MS            110UL   // zone at full brightness until here
#define CLASH_FEATHER_LEDS       5.0f    // width of the soft zone edge
#define CLASH_DIM_DEPTH          0.55f   // how much the other zones dim (0-1)
#define CLASH_RING_TRAVEL_LEDS   22.0f   // how far the ring travels
#define CLASH_RING_WIDTH_LEDS    2.5f    // softness of the ring

void setClashVisualZone(int zone);
void triggerClashVisual();
void renderClashOverlay();

void setClashVisualZone(int zone)
{
    clashVisualZone = constrain(zone, 0, 2);
}

void triggerClashVisual()
{
    clashVisualActive = true;
    clashVisualStart = millis();
}

static inline float clashSmooth(float t)
{
    t = constrain(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void renderClashOverlay()
{
    if (!clashVisualActive)
        return;

    unsigned long elapsed = millis() - clashVisualStart;

    if (elapsed >= CLASH_VISUAL_DURATION_MS)
    {
        clashVisualActive = false;
        return;
    }

    const float zoneLen   = (float)NUM_LEDS / 3.0f;
    const float zoneStart = clashVisualZone * zoneLen;
    const float zoneEnd   = zoneStart + zoneLen;

    float envelope;
    if (elapsed < CLASH_ATTACK_MS)
        envelope = clashSmooth((float)elapsed / CLASH_ATTACK_MS);
    else if (elapsed < CLASH_HOLD_MS)
        envelope = 1.0f;
    else
        envelope = 1.0f - clashSmooth((float)(elapsed - CLASH_HOLD_MS) /
                                      (float)(CLASH_VISUAL_DURATION_MS - CLASH_HOLD_MS));

    float ringT = 0.0f;
    if (elapsed > CLASH_HOLD_MS - 40)
        ringT = (float)(elapsed - (CLASH_HOLD_MS - 40)) /
                (float)(CLASH_VISUAL_DURATION_MS - (CLASH_HOLD_MS - 40));
    float ringDist = clashSmooth(ringT) * CLASH_RING_TRAVEL_LEDS;
    float ringAmp  = (ringT > 0.0f) ? 0.65f * (1.0f - ringT) : 0.0f;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x = (float)i + 0.5f;

        float outside = 0.0f;
        if (x < zoneStart)    outside = zoneStart - x;
        else if (x > zoneEnd) outside = x - zoneEnd;

        float shape;
        if (outside <= 0.0f)
            shape = 1.0f;
        else if (outside < CLASH_FEATHER_LEDS)
            shape = 0.5f * (1.0f + cosf(3.14159f * outside / CLASH_FEATHER_LEDS));
        else
            shape = 0.0f;

        float centre = 0.5f * (zoneStart + zoneEnd);
        float swell = 0.85f + 0.15f * (1.0f - fabsf(x - centre) / (0.5f * zoneLen));
        if (swell < 0.85f) swell = 0.85f;

        float dim = CLASH_DIM_DEPTH * envelope * (1.0f - shape);
        if (dim > 0.0f)
            leds[i].nscale8_video((uint8_t)(255.0f * (1.0f - dim)));

        float ring = 0.0f;
        if (ringAmp > 0.0f && outside > 0.0f)
        {
            float d = (outside - ringDist) / CLASH_RING_WIDTH_LEDS;
            ring = ringAmp * expf(-d * d);
        }

        float intensity = shape * swell * envelope + ring;
        if (intensity > 1.0f) intensity = 1.0f;
        if (intensity <= 0.01f)
            continue;

        uint8_t white = (uint8_t)(intensity * 255.0f);

        leds[i] = blend(leds[i], CRGB::White, white);
    }
}

CRGB makeAlivePixelSet1(int pixel, int baseBrightness, uint8_t waveOffset)
{
    int brightness = baseBrightness;

    brightness -= random(0, 100);

    if (random(0, 100) < 18)
        brightness -= random(50, 130);

    if (random(0, 100) < 12)
        brightness += random(30, 100);

    uint8_t wave1 = sin8(pixel * 7 + waveOffset);
    uint8_t wave2 = sin8(pixel * 19 + waveOffset * 2);
    uint8_t wave3 = sin8(pixel * 31 + waveOffset * 3);

    brightness += map(wave1, 0, 255, -30, 30);
    brightness += map(wave2, 0, 255, -25, 25);
    brightness += map(wave3, 0, 255, -20, 20);

    brightness = constrain(brightness, 25, 255);

    float position = (float)pixel / (float)(NUM_LEDS - 1);
    float tipStart = (float)(NUM_LEDS - TIP_BRIGHT_COUNT) /
                     (float)(NUM_LEDS - 1);

    if (position > tipStart)
    {
        float tipAmount = ledSmoothStep(
            (position - tipStart) / (1.0f - tipStart)
        );

        brightness += (int)(tipAmount * TIP_BRIGHT_BOOST);
        brightness = constrain(brightness, 25, 255);
    }

    return CRGB(
        scale8(bladeColorRed, (uint8_t)brightness),
        scale8(bladeColorGreen, (uint8_t)brightness),
        scale8(bladeColorBlue, (uint8_t)brightness)
    );
}

CRGB makeAlivePixelSet3(int pixel, int baseBrightness, uint8_t waveOffset)
{
    float position =
        (float)pixel /
        (float)(NUM_LEDS - 1);

    uint8_t slowBand =
        sin8(
            waveOffset +
            pixel * 3
        );

    uint8_t mediumBand =
        sin8(
            (uint8_t)(waveOffset * 2) +
            pixel * 8
        );

    uint8_t fastBand =
        sin8(
            (uint8_t)(waveOffset * 4) +
            pixel * 19
        );

    float brightness =
        baseBrightness;

    brightness +=
        (((float)slowBand - 128.0f) / 127.0f) *
        42.0f;

    brightness +=
        (((float)mediumBand - 128.0f) / 127.0f) *
        25.0f;

    brightness +=
        (((float)fastBand - 128.0f) / 127.0f) *
        10.0f;

    uint8_t valley =
        sin8(
            (uint8_t)(waveOffset * 3) +
            pixel * 5
        );

    if (valley < 70)
    {
        brightness -=
            map(
                valley,
                0,
                69,
                70,
                145
            );
    }

    uint8_t burstField =
        sin8(
            (uint8_t)(waveOffset * 5) +
            pixel * 2
        );

    if (burstField > 225)
    {
        brightness +=
            90.0f;
    }

    if (
        random(0, 100) < 5
    )
    {
        brightness -=
            random(
                50,
                120
            );
    }

    brightness +=
        position *
        20.0f;

    float tipStart =
        (float)(NUM_LEDS - TIP_BRIGHT_COUNT - 1) /
        (float)(NUM_LEDS - 1);

    if (position > tipStart)
    {
        float tipAmount =
            ledSmoothStep(
                (position - tipStart) /
                (1.0f - tipStart)
            );

        brightness +=
            tipAmount *
            TIP_BRIGHT_BOOST;
    }

    brightness =
        constrain(
            (int)brightness,
            18,
            255
        );

    return CRGB(
        scale8(
            bladeColorRed,
            (uint8_t)brightness
        ),

        scale8(
            bladeColorGreen,
            (uint8_t)brightness
        ),

        scale8(
            bladeColorBlue,
            (uint8_t)brightness
        )
    );
}

CRGB makeAlivePixelSet1(int pixel, int baseBrightness, uint8_t waveOffset);
CRGB makeAlivePixelSet3(int pixel, int baseBrightness, uint8_t waveOffset);
void renderIdleSet1(int activeLength);
void renderIdleSet3(int activeLength);
void renderLivingBlade(int activeLength)
{
    if (idleAnimationMode == 0) renderIdleSet1(activeLength);
    else if (idleAnimationMode == 2) renderIdleSet3(activeLength);
    else renderDefaultLivingBlade(activeLength);
}

void renderDefaultLivingBlade(int activeLength)
{
    activeLength = constrain(activeLength, 0, NUM_LEDS);
    bladeEnergyPhase += 2;
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (i < activeLength) leds[i] = makeAlivePixelDefault(i, 225, bladeEnergyPhase / 2);
        else leds[i] = CRGB::Black;
    }
}

void renderIdleSet1(int activeLength)
{
    activeLength = constrain(activeLength, 0, NUM_LEDS);
    bladeEnergyPhase += 2;
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (i < activeLength) leds[i] = makeAlivePixelSet1(i, 225, bladeEnergyPhase / 2);
        else leds[i] = CRGB::Black;
    }
}

void renderIdleSet3(int activeLength)
{
    activeLength = constrain(activeLength, 0, NUM_LEDS);
    bladeEnergyPhase += 3;
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (i < activeLength) leds[i] = makeAlivePixelSet3(i, 220, bladeEnergyPhase / 2);
        else leds[i] = CRGB::Black;
    }
    for (int j = 0; j < 4; j++)
    {
        if (activeLength > 5 && random(0,100) < 20)
        {
            int centre = random(2, activeLength - 2);
            int clusterWidth = random(2,5);
            for (int k=-clusterWidth;k<=clusterWidth;k++)
            {
                int p=centre+k;
                if (p>=0 && p<activeLength)
                    leds[p].r = qadd8(leds[p].r, abs(k)<=1 ? 85 : 35);
            }
        }
    }
}

void renderSwingOverlay()
{

    float swing =
        swingVolume /
        MAX_SWING_VOLUME;

    swing =
        clampFloat(
            swing,
            0.0f,
            1.0f
        );

    float motion =
        gyroSpeed /
        SWING_FULL_SPEED;

    motion =
        clampFloat(
            motion,
            0.0f,
            1.0f
        );

    float visualSwing =
        swing * 1.18f +
        motion * 0.10f;

    visualSwing =
        clampFloat(
            visualSwing,
            0.0f,
            1.0f
        );

    if (isMoving)
    {
        uint16_t phaseStep =
            (uint16_t)(
                8.0f +
                gyroSpeed * 3.5f
            );

        if (phaseStep > 45)
            phaseStep = 45;

        swingEnergyPhase +=
            phaseStep;
    }
    else
    {
        swingEnergyPhase += 1;
    }

    if (visualSwing < 0.008f)
        return;

    for (
        int i = 0;
        i < NUM_LEDS;
        i++
    )
    {
        float position =
            (float)i /
            (float)(NUM_LEDS - 1);

        uint8_t mainWave =
            sin8(
                swingEnergyPhase +
                (uint8_t)(i * 3)
            );

        uint8_t secondWave =
            sin8(
                (uint16_t)(swingEnergyPhase * 2) +
                (uint8_t)(i * 9)
            );

        float mainEnergy =
            (float)mainWave /
            255.0f;

        float secondEnergy =
            (float)secondWave /
            255.0f;

        float energy =
            mainEnergy * 0.82f +
            secondEnergy * 0.18f;

        energy =
            energy *
            energy;

        float tipWeight =
            0.28f +
            position * 0.72f;

        float intensity =
            visualSwing *
            energy *
            tipWeight;

        intensity =
            clampFloat(
                intensity,
                0.0f,
                1.0f
            );

        float colourBoost =
            65.0f +
            visualSwing * 90.0f;

        uint8_t colourAmount =
            (uint8_t)constrain(
                (int)(
                    intensity *
                    colourBoost
                ),
                0,
                170
            );

        leds[i].r =
            qadd8(
                leds[i].r,
                scale8(
                    bladeColorRed,
                    colourAmount
                )
            );

        leds[i].g =
            qadd8(
                leds[i].g,
                scale8(
                    bladeColorGreen,
                    colourAmount
                )
            );

        leds[i].b =
            qadd8(
                leds[i].b,
                scale8(
                    bladeColorBlue,
                    colourAmount
                )
            );

        if (position > 0.65f)
        {
            float topWeight =
                (position - 0.65f) /
                0.35f;

            uint8_t topBoost =
                (uint8_t)constrain(
                    (int)(
                        intensity *
                        topWeight *
                        55.0f
                    ),
                    0,
                    65
                );

            leds[i].r =
                qadd8(
                    leds[i].r,
                    scale8(
                        bladeColorRed,
                        topBoost
                    )
                );

            leds[i].g =
                qadd8(
                    leds[i].g,
                    scale8(
                        bladeColorGreen,
                        topBoost
                    )
                );

            leds[i].b =
                qadd8(
                    leds[i].b,
                    scale8(
                        bladeColorBlue,
                        topBoost
                    )
                );
        }
    }
}

void renderTipDrag()
{
    if (!ledTipDrag)
        return;

    uint16_t dragPhase = millis() / 3;

    for (int i = 0; i < TIP_DRAG_LENGTH; i++)
    {
        int ledIndex = NUM_LEDS - 1 - i;
        if (ledIndex < 0) continue;

        float distance = (float)i / (float)(TIP_DRAG_LENGTH - 1);
        float heat = 1.0f - ledSmoothStep(distance);

        uint8_t wave = sin8(dragPhase + i * 17);
        float flicker = 0.88f + ((float)wave / 255.0f) * 0.12f;
        heat *= flicker;

        uint8_t tipAmount =
            (uint8_t)constrain(
                (int)(255.0f * heat * 0.50f),
                0,
                255
            );

        leds[ledIndex].r = qadd8(
            leds[ledIndex].r,
            scale8(
                bladeColorRed,
                tipAmount
            )
        );

        leds[ledIndex].g = qadd8(
            leds[ledIndex].g,
            scale8(
                bladeColorGreen,
                tipAmount
            )
        );

        leds[ledIndex].b = qadd8(
            leds[ledIndex].b,
            scale8(
                bladeColorBlue,
                tipAmount
            )
        );
    }
}

CRGB makeSelectedColourEnergy(float amount, uint8_t brightness = 255)
{
    amount = constrain(amount, 0.0f, 1.0f);

    float r = (float)bladeColorRed;
    float g = (float)bladeColorGreen;
    float b = (float)bladeColorBlue;

    r = r + (255.0f - r) * amount;
    g = g + (255.0f - g) * amount;
    b = b + (255.0f - b) * amount;

    CRGB result(
        (uint8_t)constrain((int)r, 0, 255),
        (uint8_t)constrain((int)g, 0, 255),
        (uint8_t)constrain((int)b, 0, 255)
    );

    result.nscale8_video(brightness);
    return result;
}

void updateBladeIgnition()
{

    unsigned long elapsed =
        millis() - bladePowerStart;

    float progress =
        (float)elapsed /
        (float)LED_IGNITE_TIME_MS;

    progress =
        constrain(progress, 0.0f, 1.0f);

    float shaped =
        progress * progress *
        (3.0f - 2.0f * progress);

    int activeLength =
        constrain(
            (int)(shaped * NUM_LEDS + 0.5f),
            0,
            NUM_LEDS
        );

    renderLivingBlade(activeLength);

    renderSwingOverlay();

    if (activeLength > 0)
    {
        leds[activeLength - 1] =
            CRGB(255, 255, 255);

        if (activeLength >= 2)
        {
            leds[activeLength - 2] =
                CRGB(
                    bladeColorRed,
                    bladeColorGreen,
                    bladeColorBlue
                );
        }

        if (activeLength >= 3)
        {
            leds[activeLength - 3] =
                CRGB(
                    bladeColorRed,
                    bladeColorGreen,
                    bladeColorBlue
                );
        }
    }

    FastLED.show();

    if (progress >= 1.0f)
    {
        ledTipDrag = false;

        renderLivingBlade(NUM_LEDS);

        for (int i = NUM_LEDS - TIP_BRIGHT_COUNT;
             i < NUM_LEDS;
             i++)
        {
            leds[i] =
                CRGB(
                    bladeColorRed,
                    bladeColorGreen,
                    bladeColorBlue
                );
        }

        FastLED.show();

        bladePowerState =
            LED_POWER_ON;

        Serial.println(
            "LED: BLADE ON - IGNITION COMPLETE"
        );
    }
}

void updateBladeRetraction()
{

    unsigned long elapsed =
        millis() - bladePowerStart;

    float progress =
        (float)elapsed /
        (float)LED_RETRACT_TIME_MS;

    progress =
        constrain(progress, 0.0f, 1.0f);

    float shaped =
        progress * progress *
        (3.0f - 2.0f * progress);

    int activeLength =
        NUM_LEDS -
        (int)(shaped * NUM_LEDS + 0.5f);

    activeLength =
        constrain(
            activeLength,
            0,
            NUM_LEDS
        );

    renderLivingBlade(activeLength);

    renderSwingOverlay();

    if (activeLength > 0)
    {
        leds[activeLength - 1] =
            CRGB(255, 255, 255);

        if (activeLength >= 2)
        {
            leds[activeLength - 2] =
                CRGB(
                    bladeColorRed,
                    bladeColorGreen,
                    bladeColorBlue
                );
        }
    }

    FastLED.show();

    if (progress >= 1.0f)
    {
        bladePowerState =
            LED_POWER_OFF;

        stopPowerShot(igniteShot);

        FastLED.clear();
        FastLED.show();

        Serial.println(
            "LED: BLADE OFF - RETRACTION COMPLETE"
        );
    }
}

void triggerBladeIgnite()
{
    bladePowerState = LED_POWER_IGNITING;
    bladePowerStart = millis();
    ledBladeOn = true;

    ledTipDrag = false;
    isMoving = false;
    swingVolume = 0.0f;

    startIgniteAudio();
}

void triggerBladeRetract()
{
    ledTipDrag = false;
    isMoving = false;
    swingVolume = 0.0f;

    stopPowerShot(igniteShot);

    bladePowerState = LED_POWER_RETRACTING;
    bladePowerStart = millis();
    retractAnimationStart = millis();

    startRetractAudio();
}

void updateBladeLEDs()
{
    unsigned long now = millis();

    if (now - lastLedFrame < LED_FRAME_MS)
        return;

    lastLedFrame = now;

    if (bladePowerState == LED_POWER_OFF)
    {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        return;
    }

    if (bladePowerState == LED_POWER_IGNITING)
    {
        updateBladeIgnition();
        return;
    }

    if (bladePowerState == LED_POWER_RETRACTING)
    {
        updateBladeRetraction();
        return;
    }

    renderLivingBlade(NUM_LEDS);

    renderSwingOverlay();

    renderTipDrag();

    renderClashOverlay();

    if (bladePowerState == LED_POWER_ON && !clashVisualActive)
    {
        for (int i = NUM_LEDS - TIP_BRIGHT_COUNT; i < NUM_LEDS; i++)
        {
            leds[i] = CRGB(
                bladeColorRed,
                bladeColorGreen,
                bladeColorBlue
            );
        }
    }

    FastLED.show();
}

void checkBladeSerialCommands()
{
    while (Serial.available())
    {
        char command = Serial.read();

#if LIVE_TEST
        if (liveTestHandleChar(command))
            continue;
#endif

        if (command == 'C' || command == 'c')
        {
            triggerClash();
        }
        else if (command == 'I' || command == 'i')
            triggerBladeIgnite();
        else if (command == 'R' || command == 'r')
            triggerBladeRetract();
        else if (command == 'D' || command == 'd')
            ledTipDrag = true;
        else if (command == 'S' || command == 's')
            ledTipDrag = false;
    }
}

#define HUM_GAIN 0.2f

#define MAX_SWING_VOLUME 0.325f

#define MASTER_GAIN 1.05f

#define LIMITER_CEILING 30000.0f
#define LIMITER_ATTACK_MS 1.0f
#define LIMITER_RELEASE_MS 120.0f

#define SWING_START_THRESHOLD_DEFAULT 1.8f

#define SWING_STOP_THRESHOLD_DEFAULT 1.15f

float swingStartThreshold = SWING_START_THRESHOLD_DEFAULT;
float swingStopThreshold = SWING_STOP_THRESHOLD_DEFAULT;

#define SWING_FULL_SPEED 7.5f

#define SWING_SHARPNESS 0.85f

#define VOLUME_SHARPNESS 1.8f

#define SLOW_PLAYBACK_RATE 0.72f

#define FAST_PLAYBACK_RATE 1.28f

#define MAX_HUM_DUCKING 0.65f

#define L_TO_H_DEGREES 55.0f

#define H_TO_L_DEGREES 145.0f

#define SWING_LOOP_CROSSFADE_MS 32

#define CLASH_FILE "/clash1.wav"
#define CLASH_START_SAMPLE 8200UL

float clashAccelThreshold = 1.20f;
float clashAccelJerkThreshold = 0.15f;

#define CLASH_COOLDOWN_MS 300

#define CLASH_VOLUME 0.65f

#define HUM_LOOP_CROSSFADE_MS 45

#define NUMBER_OF_SWING_PAIRS 4
#define FIXED_SWING_PAIR 4

#define LOOP_EDGE_CACHE_SAMPLES 1200

#define STREAM_CHUNK_SAMPLES 2048

#define STREAM_CHUNKS 2

#define STREAM_BUFFER_SAMPLES (STREAM_CHUNK_SAMPLES * STREAM_CHUNKS)

struct StreamClip
{
    File file;

    uint32_t dataStart;

    uint32_t sampleCount;

    uint32_t sampleRate;

    float position;

    bool loaded;

    int16_t headCache[LOOP_EDGE_CACHE_SAMPLES];

    int16_t tailCache[LOOP_EDGE_CACHE_SAMPLES];

    int16_t streamBuffer[STREAM_BUFFER_SAMPLES];

    uint32_t streamBufferStart;

    uint32_t streamBufferFilled;

    uint32_t fileReadPosition;
};

StreamClip hum;

StreamClip swingLow;

StreamClip swingHigh;

#define CLASH_MAX_SAMPLES 24000UL
#define CLASH_START_SAMPLE 8200UL

struct ClashClip
{
    File file;

    uint32_t dataStart;
    uint32_t sampleCount;
    uint32_t sampleRate;

    uint32_t loadedSamples;
    uint32_t startSample;

    float position;
    bool loaded;

    int16_t buffer[CLASH_MAX_SAMPLES];
};

ClashClip clash;

bool clashActive = false;
unsigned long clashStartMillis = 0;
unsigned long lastClashMillis = 0;

I2S i2s(OUTPUT);

float gyroX = 0.0f;
float gyroY = 0.0f;
float gyroZ = 0.0f;

float filteredX = 0.0f;
float filteredY = 0.0f;
float filteredZ = 0.0f;

float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;

float gyroSpeed = 0.0f;

float previousGyroSpeed = 0.0f;

float gyroAcceleration = 0.0f;

float accelX = 0.0f;
float accelY = 0.0f;
float accelZ = 0.0f;
float accelMagnitude = 1.0f;
float previousAccelMagnitude = 1.0f;
float accelJerk = 0.0f;

float rotationDegrees = 0.0f;

bool isMoving = false;

bool isSpinning = false;

unsigned long movementStartTime = 0;

unsigned long spinCandidateStart = 0;

float totalMovementDegrees = 0.0f;

int currentSwingPair = 0;

int previousSwingPair = -1;

float recentPeakSpeed = SWING_FULL_SPEED;

#define SUSTAIN_STRETCH_SECONDS 1.1f
#define SUSTAIN_STRETCH_AMOUNT 0.05f

#define SUSTAIN_VOLUME_SECONDS 0.9f
#define SUSTAIN_VOLUME_BOOST 0.15f

float swingVolume = 0.0f;

float targetSwingVolume = 0.0f;

float humVolume = 1.0f;

float targetHumVolume = 1.0f;

float playbackRate = 0.75f;

float targetPlaybackRate = 0.75f;

float lowMix = 1.0f;

float highMix = 0.0f;

float targetLowMix = 1.0f;

float targetHighMix = 0.0f;

unsigned long lastIMUUpdate = 0;

unsigned long lastDebugPrint = 0;

unsigned long lastMotionMicros = 0;

float limiterEnvelope = 0.0f;
float limiterAttackCoeff = 0.0f;
float limiterReleaseCoeff = 0.0f;

float clampFloat(
    float value,
    float minimum,
    float maximum
)
{
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}

float smoothStep(
    float value
)
{
    value =
        clampFloat(
            value,
            0.0f,
            1.0f
        );

    return
        value *
        value *
        (
            3.0f -
            2.0f *
            value
        );
}

void writeMPU(
    uint8_t reg,
    uint8_t value
)
{
    Wire.beginTransmission(
        MPU_ADDRESS
    );

    Wire.write(
        reg
    );

    Wire.write(
        value
    );

    Wire.endTransmission();
}

void readGyro()
{
    if (ccMpuReadFifo(Wire) == 0)
        return;

    const CCSample &latest = ccLatest();

    float x = latest.gx;
    float y = latest.gy;
    float z = latest.gz;

    x -= gyroBiasX;
    y -= gyroBiasY;
    z -= gyroBiasZ;

    filteredX += (x - filteredX) * 0.18f;
    filteredY += (y - filteredY) * 0.18f;
    filteredZ += (z - filteredZ) * 0.18f;

    gyroX = filteredX;
    gyroY = filteredY;
    gyroZ = filteredZ;

    accelX = latest.ax;
    accelY = latest.ay;
    accelZ = latest.az;

    float newMagnitude = sqrtf(
        accelX * accelX +
        accelY * accelY +
        accelZ * accelZ
    );

    accelJerk = fabsf(
        newMagnitude -
        previousAccelMagnitude
    );

    accelMagnitude +=
        (newMagnitude - accelMagnitude) * 0.25f;

    previousAccelMagnitude = newMagnitude;
}

void calibrateGyro()
{
    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "KEEP SABER STILL"
    );

    Serial.println(
        "CALIBRATING GYRO..."
    );

    Serial.println(
        "========================================"
    );

    delay(1000);

    const int samples =
        300;

    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumZ = 0.0f;

    for (
        int i = 0;
        i < samples;
        i++
    )
    {
        Wire.beginTransmission(
            MPU_ADDRESS
        );

        Wire.write(
            0x43
        );

        Wire.endTransmission(
            false
        );

        Wire.requestFrom(
            MPU_ADDRESS,
            6
        );

        if (
            Wire.available() >= 6
        )
        {
            int16_t x =
                (
                    (int16_t)Wire.read()
                    << 8
                )
                |
                Wire.read();

            int16_t y =
                (
                    (int16_t)Wire.read()
                    << 8
                )
                |
                Wire.read();

            int16_t z =
                (
                    (int16_t)Wire.read()
                    << 8
                )
                |
                Wire.read();

            sumX +=
                x / CC_GYRO_LSB_PER_DPS;   // +/-1000 dps range

            sumY +=
                y / CC_GYRO_LSB_PER_DPS;   // +/-1000 dps range

            sumZ +=
                z / CC_GYRO_LSB_PER_DPS;   // +/-1000 dps range
        }

        delay(3);
    }

    gyroBiasX =
        sumX /
        samples;

    gyroBiasY =
        sumY /
        samples;

    gyroBiasZ =
        sumZ /
        samples;

    Serial.println(
        "GYRO CALIBRATION COMPLETE"
    );
}

uint16_t read16(
    File &file
)
{
    uint8_t a =
        file.read();

    uint8_t b =
        file.read();

    return
        (
            uint16_t
        )a
        |
        (
            (
                uint16_t
            )b
            << 8
        );
}

uint32_t read32(
    File &file
)
{
    uint8_t a =
        file.read();

    uint8_t b =
        file.read();

    uint8_t c =
        file.read();

    uint8_t d =
        file.read();

    return
        (
            uint32_t
        )a
        |
        (
            (
                uint32_t
            )b
            << 8
        )
        |
        (
            (
                uint32_t
            )c
            << 16
        )
        |
        (
            (
                uint32_t
            )d
            << 24
        );
}

bool findWavData(
    File &file,
    uint32_t &dataStart,
    uint32_t &dataSize,
    uint32_t &sampleRate,
    uint16_t &channels,
    uint16_t &bits
)
{
    char riff[4];

    if (
        file.readBytes(
            riff,
            4
        ) != 4
    )
    {
        return false;
    }

    if (
        memcmp(
            riff,
            "RIFF",
            4
        ) != 0
    )
    {
        return false;
    }

    read32(
        file
    );

    char wave[4];

    if (
        file.readBytes(
            wave,
            4
        ) != 4
    )
    {
        return false;
    }

    if (
        memcmp(
            wave,
            "WAVE",
            4
        ) != 0
    )
    {
        return false;
    }

    bool fmtFound =
        false;

    bool dataFound =
        false;

    while (
        file.available()
    )
    {
        char chunk[4];

        if (
            file.readBytes(
                chunk,
                4
            ) != 4
        )
        {
            break;
        }

        uint32_t size =
            read32(
                file
            );

        if (
            memcmp(
                chunk,
                "fmt ",
                4
            ) == 0
        )
        {
            uint16_t format =
                read16(
                    file
                );

            channels =
                read16(
                    file
                );

            sampleRate =
                read32(
                    file
                );

            read32(
                file
            );

            read16(
                file
            );

            bits =
                read16(
                    file
                );

            if (
                format != 1
                ||
                channels != 1
                ||
                bits != 16
            )
            {
                Serial.println(
                    "ERROR: WAV must be 16-bit mono PCM"
                );

                return false;
            }

            if (
                size > 16
            )
            {
                file.seek(
                    file.position()
                    +
                    size -
                    16
                );
            }

            fmtFound =
                true;
        }
        else if (
            memcmp(
                chunk,
                "data",
                4
            ) == 0
        )
        {
            dataStart =
                file.position();

            dataSize =
                size;

            dataFound =
                true;

            break;
        }
        else
        {
            file.seek(
                file.position()
                +
                size
            );
        }

        if (
            size & 1
        )
        {
            file.seek(
                file.position()
                +
                1
            );
        }
    }

    return
        fmtFound &&
        dataFound;
}

bool readSamplesAt(
    File &file,
    uint32_t dataStart,
    uint32_t sampleIndex,
    int16_t *destination,
    uint32_t sampleCountToRead
)
{
    file.seek(
        dataStart +
        (
            (
                uint32_t
            )sampleIndex
            *
            2
        )
    );

    uint32_t bytesNeeded =
        sampleCountToRead *
        2;

    uint32_t bytesRead =
        0;

    while (
        bytesRead <
        bytesNeeded
    )
    {
        uint32_t remaining =
            bytesNeeded -
            bytesRead;

        uint32_t amount =
            remaining >
            4096
            ?
            4096
            :
            remaining;

        int result =
            file.read(
                (
                    uint8_t *
                )destination
                +
                bytesRead,
                amount
            );

        if (
            result <= 0
        )
        {

            memset(
                (
                    uint8_t *
                )destination
                +
                bytesRead,
                0,
                remaining
            );

            return
                bytesRead >
                0;
        }

        bytesRead +=
            result;
    }

    return true;
}

void closeStreamClip(
    StreamClip &clip
)
{
    if (clip.loaded)
    {
        clip.file.close();
    }

    clip.loaded =
        false;

    clip.position =
        0.0f;

    clip.streamBufferStart =
        0;

    clip.streamBufferFilled =
        0;
}

bool openStreamClip(
    const char *filename,
    StreamClip &clip
)
{
    closeStreamClip(
        clip
    );

    clip.file =
        SD.open(
            filename,
            FILE_READ
        );

    if (!clip.file)
    {
        Serial.print(
            "FAILED TO OPEN: "
        );

        Serial.println(
            filename
        );

        return false;
    }

    uint32_t dataStart;
    uint32_t dataSize;
    uint32_t sampleRate;

    uint16_t channels;
    uint16_t bits;

    if (
        !findWavData(
            clip.file,
            dataStart,
            dataSize,
            sampleRate,
            channels,
            bits
        )
    )
    {
        clip.file.close();

        Serial.print(
            "INVALID WAV: "
        );

        Serial.println(
            filename
        );

        return false;
    }

    if (
        sampleRate !=
        AUDIO_RATE
    )
    {
        Serial.print(
            "WARNING: "
        );

        Serial.print(
            filename
        );

        Serial.print(
            " = "
        );

        Serial.print(
            sampleRate
        );

        Serial.println(
            " Hz"
        );
    }

    uint32_t totalSamples =
        dataSize / 2;

    if (
        totalSamples <
        (
            LOOP_EDGE_CACHE_SAMPLES *
            2
        )
    )
    {
    }

    clip.dataStart =
        dataStart;

    clip.sampleCount =
        totalSamples;

    clip.sampleRate =
        sampleRate;

    clip.position =
        0.0f;

    uint32_t headCount =
        totalSamples <
        LOOP_EDGE_CACHE_SAMPLES
        ?
        totalSamples
        :
        LOOP_EDGE_CACHE_SAMPLES;

    readSamplesAt(
        clip.file,
        dataStart,
        0,
        clip.headCache,
        headCount
    );

    uint32_t tailCount =
        totalSamples <
        LOOP_EDGE_CACHE_SAMPLES
        ?
        totalSamples
        :
        LOOP_EDGE_CACHE_SAMPLES;

    uint32_t tailStartSample =
        totalSamples -
        tailCount;

    readSamplesAt(
        clip.file,
        dataStart,
        tailStartSample,
        clip.tailCache,
        tailCount
    );

    clip.streamBufferStart =
        headCount;

    uint32_t availableForStream =
        (
            tailStartSample >
            clip.streamBufferStart
        )
        ?
        (
            tailStartSample -
            clip.streamBufferStart
        )
        :
        0;

    uint32_t initialFill =
        availableForStream <
        STREAM_BUFFER_SAMPLES
        ?
        availableForStream
        :
        STREAM_BUFFER_SAMPLES;

    if (initialFill > 0)
    {
        readSamplesAt(
            clip.file,
            dataStart,
            clip.streamBufferStart,
            clip.streamBuffer,
            initialFill
        );
    }

    clip.streamBufferFilled =
        initialFill;

    clip.fileReadPosition =
        clip.streamBufferStart +
        initialFill;

    clip.loaded =
        true;

    Serial.print(
        "Streaming "
    );

    Serial.print(
        filename
    );

    Serial.print(
        " | "
    );

    Serial.print(
        totalSamples
    );

    Serial.println(
        " samples"
    );

    return true;
}

float getPowerShotPlaybackPosition(bool ignite)
{
    if (ignite)
        return igniteShot.position;
    return retractShot.position;
}

void closePowerShot(PowerShotClip &clip)
{
    if (clip.file)
        clip.file.close();

    clip.loaded = false;
    clip.active = false;
    clip.stopping = false;
    clip.fullyLoaded = false;
    clip.position = 0.0f;
    clip.gain = 0.0f;
    clip.bufferStart = 0;
    clip.bufferFilled = 0;
}

bool openPowerShot(const char *filename, PowerShotClip &clip)
{
    closePowerShot(clip);

    if (&clip == &igniteShot)
    {
        clip.buffer = igniteFullBuffer;
        clip.capacity = IGNITE_FULL_BUFFER_SAMPLES;
    }
    else
    {
        clip.buffer = retractFullBuffer;
        clip.capacity = RETRACT_FULL_BUFFER_SAMPLES;
    }

    clip.file = SD.open(filename, FILE_READ);

    if (!clip.file)
    {
        Serial.print("FAILED TO OPEN POWER SOUND: ");
        Serial.println(filename);
        return false;
    }

    uint32_t dataStart;
    uint32_t dataSize;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bits;

    if (!findWavData(
            clip.file,
            dataStart,
            dataSize,
            sampleRate,
            channels,
            bits))
    {
        clip.file.close();
        return false;
    }

    clip.dataStart = dataStart;
    clip.sampleCount = dataSize / 2;
    clip.sampleRate = sampleRate;
    clip.step = (float)sampleRate / (float)AUDIO_RATE;
    clip.bufferStart = 0;
    clip.position = 0.0f;
    clip.active = false;

    clip.fullyLoaded = clip.sampleCount <= clip.capacity;

    uint32_t initial =
        clip.fullyLoaded ? clip.sampleCount : clip.capacity;

    if (!readSamplesAt(
            clip.file,
            clip.dataStart,
            0,
            clip.buffer,
            initial))
    {
        Serial.print("POWER SOUND RAM LOAD FAILED: ");
        Serial.println(filename);
        closePowerShot(clip);
        return false;
    }

    clip.bufferFilled = initial;
    clip.loaded = true;

    Serial.print("POWER SOUND READY: ");
    Serial.print(filename);
    Serial.print(" | ");
    Serial.print(clip.sampleCount);
    Serial.print(" samples @ ");
    Serial.print(clip.sampleRate);
    Serial.print(" Hz");

    if (clip.fullyLoaded)
    {
        Serial.println(" | fully in RAM");
    }
    else
    {
        Serial.println(" | TOO LONG FOR RAM - streaming (may glitch).");
        Serial.println("   Shorten the file or convert it to 22050 Hz mono.");
    }

    if (channels != 1 || bits != 16)
    {
        Serial.println("   WARNING: power sounds must be 16-bit MONO WAV files.");
    }

    return true;
}

static void rewindStreamedPowerShot(PowerShotClip &clip)
{
    if (clip.fullyLoaded || clip.bufferStart == 0)
        return;

    uint32_t initial = clip.capacity;

    readSamplesAt(
        clip.file,
        clip.dataStart,
        0,
        clip.buffer,
        initial
    );

    clip.bufferStart = 0;
    clip.bufferFilled = initial;
}

void startPowerShot(PowerShotClip &clip, uint32_t startSample)
{
    if (!clip.loaded || clip.sampleCount == 0)
        return;

    rewindStreamedPowerShot(clip);

    clip.position =
        (float)constrain(startSample, 0UL, clip.sampleCount - 1);
    clip.gain = 0.0f;          // fades in over POWER_FADE_IN_SAMPLES
    clip.stopping = false;
    clip.active = true;
}

void stopPowerShot(PowerShotClip &clip)
{
    if (clip.active)
        clip.stopping = true;
}

void startIgniteAudio()
{
    if (!igniteShot.loaded)
        return;

    stopPowerShot(retractShot);

    startPowerShot(igniteShot, IGNITE_AUDIO_START_SAMPLE);
}

void startRetractAudio()
{
    if (!retractShot.loaded)
    {
        retractAudioPlaying = false;
        Serial.println("RETRACT AUDIO NOT READY");
        return;
    }

    startPowerShot(retractShot, 0);
    retractAudioPlaying = true;
}

void refillPowerShot(PowerShotClip &clip)
{
    if (!clip.loaded || !clip.active || clip.fullyLoaded)
        return;

    uint32_t positionInt = (uint32_t)clip.position;

    if (positionInt >= clip.sampleCount)
        return;

    uint32_t bufferEnd = clip.bufferStart + clip.bufferFilled;

    if (positionInt + clip.capacity / 2 < bufferEnd)
        return;

    if (bufferEnd >= clip.sampleCount)
        return;

    uint32_t newStart = positionInt > 4 ? positionInt - 4 : 0;
    uint32_t keep = bufferEnd - newStart;

    memmove(
        clip.buffer,
        clip.buffer + (newStart - clip.bufferStart),
        keep * sizeof(int16_t)
    );

    uint32_t amount = clip.capacity - keep;
    uint32_t remaining = clip.sampleCount - bufferEnd;
    if (amount > remaining)
        amount = remaining;

    if (amount > 0)
    {
        readSamplesAt(
            clip.file,
            clip.dataStart,
            bufferEnd,
            clip.buffer + keep,
            amount
        );
    }

    clip.bufferStart = newStart;
    clip.bufferFilled = keep + amount;
}

static inline float powerShotRawSample(const PowerShotClip &clip, uint32_t index)
{
    if (index < clip.bufferStart ||
        index >= clip.bufferStart + clip.bufferFilled)
        return 0.0f;

    return (float)clip.buffer[index - clip.bufferStart];
}

static void finishPowerShot(PowerShotClip &clip)
{
    clip.active = false;
    clip.stopping = false;
    clip.gain = 0.0f;

    if (&clip == &retractShot)
        retractAudioPlaying = false;
}

float getPowerShotSample(PowerShotClip &clip)
{
    if (!clip.loaded || !clip.active)
        return 0.0f;

    uint32_t index = (uint32_t)clip.position;

    if (index + 1 >= clip.sampleCount)
    {
        finishPowerShot(clip);
        return 0.0f;
    }

    float frac = clip.position - (float)index;
    float a = powerShotRawSample(clip, index);
    float b = powerShotRawSample(clip, index + 1);
    float sample = a + (b - a) * frac;

    if (clip.stopping)
    {
        clip.gain -= 1.0f / (float)POWER_FADE_OUT_SAMPLES;
        if (clip.gain <= 0.0f)
        {
            finishPowerShot(clip);
            return 0.0f;
        }
    }
    else if (clip.gain < 1.0f)
    {
        clip.gain += 1.0f / (float)POWER_FADE_IN_SAMPLES;
        if (clip.gain > 1.0f)
            clip.gain = 1.0f;
    }

    uint32_t left = clip.sampleCount - index;
    float endGain = 1.0f;
    if (left < POWER_FADE_OUT_SAMPLES)
        endGain = (float)left / (float)POWER_FADE_OUT_SAMPLES;

    clip.position += clip.step;

    return sample * clip.gain * endGain;
}

void refillStreamClip(
    StreamClip &clip
)
{
    if (!clip.loaded)
    {
        return;
    }

    uint32_t tailStartSample =
        clip.sampleCount >
        LOOP_EDGE_CACHE_SAMPLES
        ?
        (
            clip.sampleCount -
            LOOP_EDGE_CACHE_SAMPLES
        )
        :
        0;

    if (
        clip.fileReadPosition >=
        tailStartSample
    )
    {
        return;
    }

    uint32_t positionInt =
        (
            uint32_t
        )clip.position;

    uint32_t playedIn =
        positionInt >
        clip.streamBufferStart
        ?
        (
            positionInt -
            clip.streamBufferStart
        )
        :
        0;

    if (
        clip.streamBufferFilled <
        STREAM_BUFFER_SAMPLES
        ||
        playedIn <
        STREAM_CHUNK_SAMPLES
    )
    {
        return;
    }

    memmove(
        clip.streamBuffer,
        clip.streamBuffer +
        STREAM_CHUNK_SAMPLES,
        (
            STREAM_BUFFER_SAMPLES -
            STREAM_CHUNK_SAMPLES
        )
        *
        sizeof(
            int16_t
        )
    );

    clip.streamBufferStart +=
        STREAM_CHUNK_SAMPLES;

    uint32_t remainingToTail =
        tailStartSample -
        clip.fileReadPosition;

    uint32_t amount =
        remainingToTail <
        STREAM_CHUNK_SAMPLES
        ?
        remainingToTail
        :
        STREAM_CHUNK_SAMPLES;

    readSamplesAt(
        clip.file,
        clip.dataStart,
        clip.fileReadPosition,
        clip.streamBuffer +
        (
            STREAM_BUFFER_SAMPLES -
            STREAM_CHUNK_SAMPLES
        ),
        amount
    );

    if (
        amount <
        STREAM_CHUNK_SAMPLES
    )
    {
        memset(
            clip.streamBuffer +
            (
                STREAM_BUFFER_SAMPLES -
                STREAM_CHUNK_SAMPLES
            )
            +
            amount,
            0,
            (
                STREAM_CHUNK_SAMPLES -
                amount
            )
            *
            sizeof(
                int16_t
            )
        );
    }

    clip.fileReadPosition +=
        amount;

    clip.streamBufferFilled =
        STREAM_BUFFER_SAMPLES;
}

void resetStreamForLoop(
    StreamClip &clip
)
{
    if (!clip.loaded)
    {
        return;
    }

    uint32_t headCount =
        clip.sampleCount <
        LOOP_EDGE_CACHE_SAMPLES
        ?
        clip.sampleCount
        :
        LOOP_EDGE_CACHE_SAMPLES;

    clip.streamBufferStart =
        headCount;

    uint32_t tailStartSample =
        clip.sampleCount >
        LOOP_EDGE_CACHE_SAMPLES
        ?
        (
            clip.sampleCount -
            LOOP_EDGE_CACHE_SAMPLES
        )
        :
        0;

    uint32_t availableForStream =
        (
            tailStartSample >
            clip.streamBufferStart
        )
        ?
        (
            tailStartSample -
            clip.streamBufferStart
        )
        :
        0;

    uint32_t initialFill =
        availableForStream <
        STREAM_BUFFER_SAMPLES
        ?
        availableForStream
        :
        STREAM_BUFFER_SAMPLES;

    if (initialFill > 0)
    {
        readSamplesAt(
            clip.file,
            clip.dataStart,
            clip.streamBufferStart,
            clip.streamBuffer,
            initialFill
        );
    }

    clip.streamBufferFilled =
        initialFill;

    clip.fileReadPosition =
        clip.streamBufferStart +
        initialFill;
}

inline int16_t readClipSample(
    StreamClip &clip,
    uint32_t index
)
{
    uint32_t headCount =
        clip.sampleCount <
        LOOP_EDGE_CACHE_SAMPLES
        ?
        clip.sampleCount
        :
        LOOP_EDGE_CACHE_SAMPLES;

    if (index < headCount)
    {
        return
            clip.headCache[
                index
            ];
    }

    uint32_t tailCount =
        clip.sampleCount <
        LOOP_EDGE_CACHE_SAMPLES
        ?
        clip.sampleCount
        :
        LOOP_EDGE_CACHE_SAMPLES;

    uint32_t tailStartSample =
        clip.sampleCount -
        tailCount;

    if (
        index >=
        tailStartSample
    )
    {
        return
            clip.tailCache[
                index -
                tailStartSample
            ];
    }

    if (
        index >=
        clip.streamBufferStart
        &&
        index <
        (
            clip.streamBufferStart +
            clip.streamBufferFilled
        )
    )
    {
        return
            clip.streamBuffer[
                index -
                clip.streamBufferStart
            ];
    }

    return 0;
}

float getLoopedSample(
    StreamClip &clip,
    float rate,
    uint32_t crossfadeSamples
)
{
    if (
        !clip.loaded
        ||
        clip.sampleCount < 4
    )
    {
        return 0.0f;
    }

    bool wrapped =
        false;

    while (
        clip.position >=
        clip.sampleCount
    )
    {
        clip.position -=
            clip.sampleCount;

        wrapped =
            true;
    }

    while (
        clip.position < 0.0f
    )
    {
        clip.position +=
            clip.sampleCount;

        wrapped =
            true;
    }

    if (wrapped)
    {

        resetStreamForLoop(
            clip
        );
    }

    uint32_t index =
        (
            uint32_t
        )clip.position;

    uint32_t next =
        index + 1;

    if (
        next >=
        clip.sampleCount
    )
    {
        next =
            0;
    }

    float fraction =
        clip.position -
        index;

    float sampleA =
        readClipSample(
            clip,
            index
        );

    float sampleB =
        readClipSample(
            clip,
            next
        );

    float sample =
        sampleA
        +
        (
            sampleB -
            sampleA
        )
        *
        fraction;

    if (
        crossfadeSamples >
        2
        &&
        crossfadeSamples <
        clip.sampleCount / 2
        &&
        clip.position >
        (
            clip.sampleCount -
            crossfadeSamples
        )
    )
    {
        float fade =
            (
                clip.position -
                (
                    clip.sampleCount -
                    crossfadeSamples
                )
            )
            /
            crossfadeSamples;

        fade =
            clampFloat(
                fade,
                0.0f,
                1.0f
            );

        float beginningPosition =
            (
                clip.position -
                (
                    clip.sampleCount -
                    crossfadeSamples
                )
            );

        uint32_t beginIndex =
            (
                uint32_t
            )beginningPosition;

        if (
            beginIndex >=
            clip.sampleCount
        )
        {
            beginIndex =
                0;
        }

        uint32_t beginNext =
            beginIndex + 1;

        if (
            beginNext >=
            clip.sampleCount
        )
        {
            beginNext =
                0;
        }

        float beginFraction =
            beginningPosition -
            beginIndex;

        float beginA =
            readClipSample(
                clip,
                beginIndex
            );

        float beginB =
            readClipSample(
                clip,
                beginNext
            );

        float beginningSample =
            beginA
            +
            (
                beginB -
                beginA
            )
            *
            beginFraction;

        float fadeOut =
            cosf(
                fade *
                PI *
                0.5f
            );

        float fadeIn =
            sinf(
                fade *
                PI *
                0.5f
            );

        sample =
            sample *
            fadeOut
            +
            beginningSample *
            fadeIn;
    }

    clip.position +=
        rate;

    return sample;
}

bool openClashClip(
    const char *filename
)
{
    clash.loaded = false;
    clash.position = 0.0f;
    clash.loadedSamples = 0;

    clash.file =
        SD.open(
            filename,
            FILE_READ
        );

    if (!clash.file)
    {
        Serial.print(
            "FAILED TO OPEN CLASH: "
        );

        Serial.println(
            filename
        );

        return false;
    }

    uint32_t dataStart;
    uint32_t dataSize;
    uint32_t sampleRate;

    uint16_t channels;
    uint16_t bits;

    if (
        !findWavData(
            clash.file,
            dataStart,
            dataSize,
            sampleRate,
            channels,
            bits
        )
    )
    {
        clash.file.close();

        Serial.println(
            "INVALID CLASH WAV"
        );

        return false;
    }

    if (
        channels != 1 ||
        bits != 16
    )
    {
        Serial.print(
            "CLASH FORMAT ERROR: "
        );

        Serial.print(
            channels
        );

        Serial.print(
            " channels, "
        );

        Serial.print(
            bits
        );

        Serial.println(
            " bit. Need 16-bit mono."
        );

        clash.file.close();

        return false;
    }

    clash.dataStart =
        dataStart;

    clash.sampleCount =
        dataSize / 2;

    clash.sampleRate =
        sampleRate;

    uint32_t startSample =
        CLASH_START_SAMPLE;

    if (
        startSample >=
        clash.sampleCount
    )
    {
        startSample = 0;
    }

    uint32_t samplesToLoad =
        clash.sampleCount -
        startSample;

    if (
        samplesToLoad >
        CLASH_MAX_SAMPLES
    )
    {
        samplesToLoad =
            CLASH_MAX_SAMPLES;
    }

    if (
        !readSamplesAt(
            clash.file,
            clash.dataStart,
            startSample,
            clash.buffer,
            samplesToLoad
        )
    )
    {
        clash.file.close();

        Serial.println(
            "FAILED TO LOAD CLASH INTO RAM"
        );

        return false;
    }

    clash.startSample =
        startSample;

    clash.loadedSamples =
        samplesToLoad;

    clash.position =
        (float)samplesToLoad;

    clash.loaded =
        true;

    clash.file.close();

    Serial.print(
        "CLASH LOADED INTO RAM: "
    );

    Serial.print(
        clash.loadedSamples
    );

    Serial.print(
        " samples @ "
    );

    Serial.print(
        clash.sampleRate
    );

    Serial.println(
        " Hz"
    );

    Serial.print(
        "CLASH START SAMPLE: "
    );

    Serial.println(
        clash.startSample
    );

    return true;
}

void refillClashClip()
{
}

float getClashSample()
{
    if (
        !clash.loaded ||
        !clashActive
    )
    {
        return 0.0f;
    }

    if (
        clash.position >=
        clash.loadedSamples - 1
    )
    {
        clashActive =
            false;

        clash.position =
            (float)clash.loadedSamples;

        return 0.0f;
    }

    uint32_t index =
        (uint32_t)clash.position;

    float fraction =
        clash.position -
        index;

    float sampleA =
        clash.buffer[
            index
        ];

    float sampleB =
        clash.buffer[
            index + 1
        ];

    float sample =
        sampleA +
        (
            sampleB -
            sampleA
        )
        *
        fraction;

    clash.position +=
        1.0f;

    return sample;
}

void triggerClash()
{
    unsigned long now =
        millis();

    if (
        now -
        lastClashMillis <
        CLASH_COOLDOWN_MS
    )
    {
        return;
    }

    lastClashMillis =
        now;

    clash.position =
        0.0f;

    clashActive =
        true;

    Serial.println(
        ">>> CLASH"
    );
}

void selectNewSwingPair()
{
}

bool openSwingPair(int pairNumber)
{
    if (pairNumber < 1 || pairNumber > NUMBER_OF_SWING_PAIRS)
        return false;

    if (isMoving)
    {
        Serial.println("Cannot change pair while swinging.");
        Serial.println("Stop the saber first.");
        return false;
    }

    char lowFilename[24];
    char highFilename[24];

    snprintf(
        lowFilename,
        sizeof(lowFilename),
        "/swingL%d.wav",
        pairNumber
    );

    snprintf(
        highFilename,
        sizeof(highFilename),
        "/swingH%d.wav",
        pairNumber
    );

    closeStreamClip(swingLow);
    closeStreamClip(swingHigh);

    swingLow.position = 0.0f;
    swingHigh.position = 0.0f;

    if (!openStreamClip(lowFilename, swingLow))
    {
        Serial.print("FAILED: ");
        Serial.println(lowFilename);
        return false;
    }

    if (!openStreamClip(highFilename, swingHigh))
    {
        Serial.print("FAILED: ");
        Serial.println(highFilename);

        closeStreamClip(swingLow);
        return false;
    }

    currentSwingPair = pairNumber - 1;
    previousSwingPair = currentSwingPair;

    swingLow.position = 0.0f;
    swingHigh.position = 0.0f;

    lowMix = 1.0f;
    highMix = 0.0f;
    targetLowMix = 1.0f;
    targetHighMix = 0.0f;

    Serial.println();
    Serial.print("========================================");
    Serial.println();
    Serial.print("NOW TESTING SWING PAIR ");
    Serial.println(pairNumber);
    Serial.print("LOW : ");
    Serial.println(lowFilename);
    Serial.print("HIGH: ");
    Serial.println(highFilename);
    Serial.println("========================================");
    Serial.println();

    return true;
}

void checkPairSelection()
{
}

void startMovement()
{
    isMoving =
        true;

    isSpinning =
        false;

    movementStartTime =
        millis();

    totalMovementDegrees =
        0.0f;

    spinCandidateStart =
        0;

    Serial.println(
        ">>> SWING START"
    );
}

void stopMovement()
{
    isMoving =
        false;

    isSpinning =
        false;

    spinCandidateStart =
        0;

    Serial.println(
        ">>> SWING STOP"
    );
}

void updateMotion()
{
    readGyro();

    gyroSpeed =
        sqrtf(
            gyroX * gyroX
            +
            gyroY * gyroY
            +
            gyroZ * gyroZ
        );

    gyroAcceleration =
        fabsf(
            gyroSpeed -
            previousGyroSpeed
        );

    previousGyroSpeed =
        gyroSpeed;

    if (ccConsumeTrigger())
    {
        triggerClash();
#if LIVE_TEST
        liveTestOnTrigger();
#endif
    }

    unsigned long now =
        micros();

    if (
        lastMotionMicros ==
        0
    )
    {
        lastMotionMicros =
            now;

        return;
    }

    unsigned long delta =
        now -
        lastMotionMicros;

    if (
        delta >
        50000
    )
    {
        delta =
            4000;
    }

    lastMotionMicros =
        now;

    float dt =
        delta /
        1000000.0f;

    float signedRate;

    if (
        fabsf(gyroY)
        >
        fabsf(gyroZ)
    )
    {
        signedRate =
            gyroY;
    }
    else
    {
        signedRate =
            gyroZ;
    }

    rotationDegrees +=
        signedRate *
        dt;

    while (
        rotationDegrees >=
        360.0f
    )
    {
        rotationDegrees -=
            360.0f;
    }

    while (
        rotationDegrees <
        0.0f
    )
    {
        rotationDegrees +=
            360.0f;
    }

    if (
        isMoving
    )
    {
        totalMovementDegrees +=
            fabsf(
                signedRate
            )
            *
            dt;
    }

    if (
        !isMoving
        &&
        gyroSpeed >=
        swingStartThreshold
    )
    {
        startMovement();
    }

    if (
        isMoving
        &&
        millis() - movementStartTime > 90
        &&
        gyroSpeed <
        swingStopThreshold
    )
    {
        stopMovement();
    }

    if (
        isMoving
        &&
        gyroSpeed >
        5.0f
    )
    {
        if (
            spinCandidateStart ==
            0
        )
        {
            spinCandidateStart =
                millis();
        }

        if (
            millis() -
            spinCandidateStart >
            300
            &&
            totalMovementDegrees >
            180.0f
        )
        {
            isSpinning =
                true;
        }
    }
    else
    {
        spinCandidateStart =
            0;
    }
}

void updateAudioControl()
{

    if (
        isMoving
    )
    {

        if (
            gyroSpeed >
            recentPeakSpeed
        )
        {
            recentPeakSpeed =
                gyroSpeed;
        }
        else
        {
            recentPeakSpeed +=
                (
                    SWING_FULL_SPEED -
                    recentPeakSpeed
                )
                *
                0.0015f;
        }

        float effectiveFullSpeed =
            recentPeakSpeed;

        if (
            effectiveFullSpeed <
            swingStartThreshold *
            1.6f
        )
        {
            effectiveFullSpeed =
                swingStartThreshold *
                1.6f;
        }

        float speedAmount =
            (
                gyroSpeed -
                swingStartThreshold
            )
            /
            (
                effectiveFullSpeed -
                swingStartThreshold
            );

        speedAmount =
            clampFloat(
                speedAmount,
                0.0f,
                1.0f
            );

        float shapedSpeed =
            powf(
                speedAmount,
                SWING_SHARPNESS
            );

        float volumeShapedSpeed =
            powf(
                speedAmount,
                VOLUME_SHARPNESS
            );

        targetSwingVolume =
            volumeShapedSpeed *
            MAX_SWING_VOLUME;

        if (
            !isSpinning
        )
        {
            float heldSeconds =
                (
                    millis() -
                    movementStartTime
                )
                /
                1000.0f;

            float volumeSwell =
                clampFloat(
                    heldSeconds /
                    SUSTAIN_VOLUME_SECONDS,
                    0.0f,
                    1.0f
                );

            targetSwingVolume *=
                (
                    1.0f +
                    volumeSwell *
                    SUSTAIN_VOLUME_BOOST
                );
        }

        if (
            gyroSpeed >
            swingStartThreshold
            &&
            targetSwingVolume <
            0.055f
        )
        {
            targetSwingVolume =
                0.055f;
        }

        if (
            isSpinning
        )
        {
            targetSwingVolume *=
                1.08f;

            targetSwingVolume =
                clampFloat(
                    targetSwingVolume,
                    0.0f,
                    0.48f
                );
        }

        targetPlaybackRate =
            SLOW_PLAYBACK_RATE
            +
            (
                FAST_PLAYBACK_RATE -
                SLOW_PLAYBACK_RATE
            )
            *
            shapedSpeed;

        if (
            isSpinning
        )
        {
            targetPlaybackRate =
                1.12f;
        }

        if (
            !isSpinning
        )
        {
            float swingDurationSeconds =
                (
                    millis() -
                    movementStartTime
                )
                /
                1000.0f;

            float stretchAmount =
                clampFloat(
                    swingDurationSeconds /
                    SUSTAIN_STRETCH_SECONDS,
                    0.0f,
                    1.0f
                );

            targetPlaybackRate -=
                stretchAmount *
                SUSTAIN_STRETCH_AMOUNT;
        }

        targetHumVolume =
            1.0f
            -
            (
                shapedSpeed *
                MAX_HUM_DUCKING
            );

        targetHumVolume =
            clampFloat(
                targetHumVolume,
                0.35f,
                1.0f
            );
    }
    else
    {
        targetSwingVolume =
            0.0f;

        targetPlaybackRate =
            0.72f;

        targetHumVolume =
            1.0f;
    }

    float phase =
        rotationDegrees;

    if (
        phase <
        180.0f
    )
    {
        float amount =
            phase /
            180.0f;

        amount =
            smoothStep(
                amount
            );

        targetLowMix =
            1.0f -
            amount;

        targetHighMix =
            amount;
    }
    else
    {
        float amount =
            (
                phase -
                180.0f
            )
            /
            180.0f;

        amount =
            smoothStep(
                amount
            );

        targetLowMix =
            amount;

        targetHighMix =
            1.0f -
            amount;
    }

    if (
        targetSwingVolume >
        swingVolume
    )
    {
        swingVolume +=
            (
                targetSwingVolume -
                swingVolume
            )
            *
            0.11f;
    }
    else
    {
        swingVolume +=
            (
                targetSwingVolume -
                swingVolume
            )
            *
            0.16f;
    }

    humVolume +=
        (
            targetHumVolume -
            humVolume
        )
        *
        0.018f;

    playbackRate +=
        (
            targetPlaybackRate -
            playbackRate
        )
        *
        0.15f;

    lowMix +=
        (
            targetLowMix -
            lowMix
        )
        *
        0.035f;

    highMix +=
        (
            targetHighMix -
            highMix
        )
        *
        0.035f;
}

#define AMBIENT_FADE_IN_STEP  (1.0f / (AUDIO_RATE * 0.20f))  // 200 ms
#define AMBIENT_FADE_OUT_STEP (1.0f / (AUDIO_RATE * 0.03f))  // 30 ms
float ambientGain = 0.0f;

void outputAudio()
{

    uint32_t humCrossfadeSamples =
        (
            AUDIO_RATE *
            HUM_LOOP_CROSSFADE_MS
        )
        /
        1000;

    float humSample =
        getLoopedSample(
            hum,
            1.0f,
            humCrossfadeSamples
        );

    uint32_t swingCrossfadeSamples =
        (
            AUDIO_RATE *
            SWING_LOOP_CROSSFADE_MS
        )
        /
        1000;

    float lowSample =
        getLoopedSample(
            swingLow,
            playbackRate,
            swingCrossfadeSamples
        );

    float highSample =
        getLoopedSample(
            swingHigh,
            playbackRate,
            swingCrossfadeSamples
        );

    float swingSample =
        (
            lowSample *
            lowMix
        )
        +
        (
            highSample *
            highMix
        );

    float clashSample =
        0.0f;

    if (clashActive)
    {
        clashSample =
            getClashSample();
    }

    float powerSample =
        getPowerShotSample(igniteShot)
        +
        getPowerShotSample(retractShot);

    float humPart =
        humSample *
        HUM_GAIN *
        humVolume;

    float swingPart =
        swingSample *
        swingVolume;

    float clashPart =
        clashSample *
        CLASH_VOLUME;

    float powerPart =
        powerSample *
        0.65f;

    bool ambientOn =
        (bladePowerState == LED_POWER_ON ||
         bladePowerState == LED_POWER_IGNITING) &&
        !retractAudioPlaying;

    if (ambientOn)
    {
        ambientGain += AMBIENT_FADE_IN_STEP;
        if (ambientGain > 1.0f) ambientGain = 1.0f;
    }
    else
    {
        ambientGain -= AMBIENT_FADE_OUT_STEP;
        if (ambientGain < 0.0f) ambientGain = 0.0f;
    }

    float mixed =
        powerPart +
        ambientGain * (humPart + swingPart + clashPart);

    mixed *=
        MASTER_GAIN;

    mixed *= menuVolumeGain;

    float absMixed =
        fabsf(
            mixed
        );

    if (
        absMixed >
        limiterEnvelope
    )
    {
        limiterEnvelope =
            limiterEnvelope *
            limiterAttackCoeff
            +
            absMixed *
            (
                1.0f -
                limiterAttackCoeff
            );
    }
    else
    {
        limiterEnvelope =
            limiterEnvelope *
            limiterReleaseCoeff
            +
            absMixed *
            (
                1.0f -
                limiterReleaseCoeff
            );
    }

    float limiterGain =
        1.0f;

    if (
        limiterEnvelope >
        LIMITER_CEILING
    )
    {
        limiterGain =
            LIMITER_CEILING /
            limiterEnvelope;
    }

    mixed *=
        limiterGain;

    if (mixed > 32000.0f) mixed = 32000.0f;
    if (mixed < -32000.0f) mixed = -32000.0f;

    int16_t output =
        (
            int16_t
        )mixed;

    i2s.write16(
        output,
        output
    );
}

#define AUDIO_BATCH_MAX 256

void serviceAudio(bool atLeastOne)
{
    int room = i2s.availableForWrite() / 4;

    if (room > AUDIO_BATCH_MAX)
        room = AUDIO_BATCH_MAX;

    if (atLeastOne && room < 1)
        room = 1;

    for (int k = 0; k < room; k++)
        outputAudio();
}

void drawCentredText(const char *text, int y)
{
    int16_t x1,y1; uint16_t w,h;
    display.getTextBounds(text,0,y,&x1,&y1,&w,&h);
    int x=(SCREEN_WIDTH-(int)w)/2; if(x<0)x=0;
    display.setCursor(x,y); display.print(text);
}

void drawSaberScreen(const char *status, int bladeLength)
{
    if (!oledReady) return;
    selectOLEDI2C();
    bladeLength=constrain(bladeLength,0,74);
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1); display.setTextWrap(false);
    drawCentredText("LIGHTSABER",0); display.drawFastHLine(0,9,SCREEN_WIDTH,SSD1306_WHITE);
    if(bladeLength>0){display.drawRoundRect(24,13,bladeLength,7,2,SSD1306_WHITE); if(bladeLength>4)display.drawFastHLine(26,16,bladeLength-4,SSD1306_WHITE);}
    display.fillRect(21,14,5,5,SSD1306_WHITE); display.drawRect(2,12,20,9,SSD1306_WHITE); display.fillRect(5,15,14,3,SSD1306_WHITE);
    display.drawFastVLine(7,13,7,SSD1306_BLACK); display.drawFastVLine(12,13,7,SSD1306_BLACK); display.drawFastVLine(17,13,7,SSD1306_BLACK);
    drawCentredText(status,24); display.display(); selectMPUI2C();
}
void drawMainMenuScreen(){
    if (!oledReady) return;
    selectOLEDI2C();
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1); display.setTextWrap(false);
    char header[20]; snprintf(header,sizeof(header),"< MENU %d >",selectedMenu+1); drawCentredText(header,0); display.drawFastHLine(0,9,SCREEN_WIDTH,SSD1306_WHITE); drawCentredText(menus[selectedMenu].title,13); drawCentredText("< CW / CCW >",24); display.display(); selectMPUI2C();
}
void drawSubMenuScreen(){
    if (!oledReady) return;

    selectOLEDI2C();
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextWrap(false);

    if (selectedMenu == 1 || selectedMenu == 2)
    {
        int percent = selectedMenu == 1 ? selectedOption * 5 : selectedOption * 5;
        float angle = -135.0f + (270.0f * (float)percent / 100.0f);
        float radians = angle * 0.0174532925f;

        display.setCursor(2, 0);
        display.print(selectedMenu == 1 ? "BRIGHTNESS" : "VOLUME");

        display.drawCircle(28, 19, 10, SSD1306_WHITE);
        display.fillCircle(28, 19, 2, SSD1306_WHITE);

        int x2 = 28 + (int)(7.0f * cosf(radians));
        int y2 = 19 + (int)(7.0f * sinf(radians));
        display.drawLine(28, 19, x2, y2, SSD1306_WHITE);

        display.setTextSize(2);
        char valueText[8];
        snprintf(valueText, sizeof(valueText), "%d%%", percent);
        display.setCursor(48, 10);
        display.print(valueText);
        display.setTextSize(1);

        display.display();
        selectMPUI2C();
        return;
    }

    char header[20];
    snprintf(header,sizeof(header),"MENU %d",selectedMenu+1);
    drawCentredText(header,0);
    display.drawFastHLine(0,9,SCREEN_WIDTH,SSD1306_WHITE);
    drawCentredText(menus[selectedMenu].items[selectedOption],13);
    drawCentredText("< CW / CCW >",24);

    display.display();
    selectMPUI2C();
}
void drawSelectedScreen()
{
    if (!oledReady) return;

    selectOLEDI2C();
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextWrap(false);

    drawCentredText("SELECTED", 0);
    display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);

    if (selectedMenu == 1 || selectedMenu == 2)
    {
        int percent = constrain(selectedOption * 5, 0, 100);
        char valueText[8];
        snprintf(valueText, sizeof(valueText), "%d%%", percent);
        drawCentredText(valueText, 13);
    }
    else
    {
        drawCentredText(
            menus[selectedMenu].items[selectedOption],
            13
        );
    }

    drawCentredText("SAVED", 24);

    display.display();
    selectMPUI2C();
}
void updateOLEDPowerScreen()
{
    unsigned long elapsed =
        millis() - bladePowerStart;

    int bladeLength = 0;
    const char *status = "OFF";

    if (bladePowerState == LED_POWER_IGNITING)
    {
        float progress =
            (float)elapsed /
            (float)LED_IGNITE_TIME_MS;

        progress =
            constrain(
                progress,
                0.0f,
                1.0f
            );

        float shaped =
            progress *
            progress *
            (3.0f - 2.0f * progress);

        int ledLength =
            (int)(
                shaped *
                (float)NUM_LEDS +
                0.5f
            );

        ledLength =
            constrain(
                ledLength,
                0,
                NUM_LEDS
            );

        bladeLength =
            (int)(
                ((float)ledLength /
                 (float)NUM_LEDS) *
                74.0f +
                0.5f
            );

        bladeLength =
            constrain(
                bladeLength,
                0,
                74
            );

        status =
            progress >= 1.0f
            ? "READY"
            : "IGNITING";
    }
    else if (bladePowerState == LED_POWER_RETRACTING)
    {
        float progress =
            (float)elapsed /
            (float)LED_RETRACT_TIME_MS;

        progress =
            constrain(
                progress,
                0.0f,
                1.0f
            );

        float shaped =
            progress *
            progress *
            (3.0f - 2.0f * progress);

        int ledLength =
            NUM_LEDS -
            (int)(
                shaped *
                (float)NUM_LEDS +
                0.5f
            );

        ledLength =
            constrain(
                ledLength,
                0,
                NUM_LEDS
            );

        bladeLength =
            (int)(
                ((float)ledLength /
                 (float)NUM_LEDS) *
                74.0f +
                0.5f
            );

        bladeLength =
            constrain(
                bladeLength,
                0,
                74
            );

        status =
            progress >= 1.0f
            ? "OFF"
            : "RETRACTING";
    }
    else if (bladePowerState == LED_POWER_ON)
    {
        bladeLength = 74;
        status = "READY";
    }

    drawSaberScreen(
        status,
        bladeLength
    );
}
void applyMenuSelection(){
    switch(selectedMenu){
      case 0:
        if(selectedOption==0){bladeColorRed=255;bladeColorGreen=0;bladeColorBlue=0;}
        else if(selectedOption==1){bladeColorRed=0;bladeColorGreen=255;bladeColorBlue=0;}
        else {bladeColorRed=0;bladeColorGreen=0;bladeColorBlue=255;} break;
      case 1:
        menuBrightnessPercent = constrain(selectedOption * 5, 0, 100);
        FastLED.setBrightness((uint8_t)((255UL * (unsigned long)menuBrightnessPercent) / 100UL));
        break;
      case 2:
        {
          int percent = constrain(selectedOption * 5, 0, 100);
          menuVolumeGain =
              MAX_MENU_VOLUME_GAIN * ((float)percent / 100.0f);
        }
        break;
      case 3: idleAnimationMode=selectedOption; break;
      case 4:
        if(selectedOption==0){swingStartThreshold=1.8f;swingStopThreshold=1.15f;} else if(selectedOption==1){swingStartThreshold=1.5f;swingStopThreshold=0.95f;} else {swingStartThreshold=2.2f;swingStopThreshold=1.40f;} break;
      case 5:
        if(selectedOption==0){ccSetTrigger(3.0f,1.0f);} else if(selectedOption==1){ccSetTrigger(2.2f,0.7f);} else {ccSetTrigger(4.0f,1.5f);} break;
    }
    oledNeedsRedraw=true;
}
void updateEncoder()
{

    static uint8_t previousAB =
        ((uint8_t)digitalRead(ENCODER_CLK) << 1) |
        (uint8_t)digitalRead(ENCODER_DT);

    static int8_t encoderAccumulator = 0;
    static unsigned long lastEncoderTransition = 0;

    uint8_t currentAB =
        ((uint8_t)digitalRead(ENCODER_CLK) << 1) |
        (uint8_t)digitalRead(ENCODER_DT);

    if (currentAB != previousAB)
    {
        uint8_t transition =
            (previousAB << 2) | currentAB;

        static const int8_t transitionTable[16] =
        {
             0, -1,  1,  0,
             1,  0,  0, -1,
            -1,  0,  0,  1,
             0,  1, -1,  0
        };

        int8_t movement = transitionTable[transition];
        unsigned long now = millis();

        if (movement != 0 &&
            (now - lastEncoderTransition >= 1))
        {
            encoderAccumulator += movement;
            lastEncoderTransition = now;

            if (encoderAccumulator >= 4 ||
                encoderAccumulator <= -4)
            {
                int direction =
                    (encoderAccumulator > 0) ? 1 : -1;

                encoderAccumulator = 0;

                if (currentUIState == MAIN_MENU)
                {
                    selectedMenu += direction;

                    if (selectedMenu < 0)
                        selectedMenu = MENU_COUNT - 1;

                    if (selectedMenu >= MENU_COUNT)
                        selectedMenu = 0;
                }
                else if (currentUIState == SUB_MENU)
                {
                    int optionCount =
                        menus[selectedMenu].itemCount;

                    selectedOption += direction;

                    if (selectedOption < 0)
                        selectedOption = optionCount - 1;

                    if (selectedOption >= optionCount)
                        selectedOption = 0;

                    if (selectedMenu == 1)
                    {
                        menuBrightnessPercent = constrain(selectedOption * 5, 0, 100);
                        FastLED.setBrightness((uint8_t)((255UL * (unsigned long)menuBrightnessPercent) / 100UL));
                    }
                    else if (selectedMenu == 2)
                    {
                        int percent = constrain(selectedOption * 5, 0, 100);
                        menuVolumeGain = MAX_MENU_VOLUME_GAIN * ((float)percent / 100.0f);
                    }
                }

                oledNeedsRedraw = true;
            }
        }

        previousAB = currentAB;
    }

}
void handleMenuButton(){
    static bool lastButtonState = HIGH;
    bool currentButtonState = digitalRead(ENCODER_SW);
    unsigned long now = millis();

    if (lastButtonState == HIGH && currentButtonState == LOW)
    {
        menuButtonPressed = true;
        menuButtonPressStart = now;
        longPressHandled = false;
    }

    if (menuButtonPressed &&
        currentButtonState == LOW &&
        !longPressHandled &&
        now - menuButtonPressStart >= LONG_PRESS_TIME)
    {
        longPressHandled = true;

        if (currentUIState == DEFAULT_SCREEN ||
            currentUIState == SELECTION_FEEDBACK)
        {
            if (bladePowerState == LED_POWER_OFF)
            {
                triggerBladeIgnite();
            }
            else if (bladePowerState == LED_POWER_ON ||
                     bladePowerState == LED_POWER_IGNITING)
            {
                triggerBladeRetract();
            }
        }
        else if (currentUIState == MAIN_MENU)
        {
            currentUIState = DEFAULT_SCREEN;
        }
        else if (currentUIState == SUB_MENU)
        {
            currentUIState = MAIN_MENU;
        }

        oledNeedsRedraw = true;
    }

    if (lastButtonState == LOW && currentButtonState == HIGH)
    {
        unsigned long duration = now - menuButtonPressStart;

        if (duration < LONG_PRESS_TIME && !longPressHandled)
        {
            if (currentUIState == DEFAULT_SCREEN)
            {
                selectedMenu = 0;
                selectedOption = 0;
                currentUIState = MAIN_MENU;
            }
            else if (currentUIState == MAIN_MENU)
            {
                selectedOption = (selectedMenu == 2) ? 10 : 0;
                currentUIState = SUB_MENU;
            }
            else if (currentUIState == SUB_MENU)
            {
                applyMenuSelection();
                selectionFeedbackUntil = now + 700;
                currentUIState = SELECTION_FEEDBACK;
            }

            oledNeedsRedraw = true;
        }

        menuButtonPressed = false;
    }

    lastButtonState = currentButtonState;
}
void updateMenuDisplay(){
    if (!oledReady) return;

    unsigned long now = millis();

    if (currentUIState == SELECTION_FEEDBACK) {
        if ((long)(now - selectionFeedbackUntil) >= 0) {
            currentUIState = DEFAULT_SCREEN;
            oledNeedsRedraw = true;
        }
        else if (oledNeedsRedraw) {
            drawSelectedScreen();
            oledNeedsRedraw = false;
        }
        return;
    }

    if (currentUIState == MAIN_MENU || currentUIState == SUB_MENU) {
        if (oledNeedsRedraw) {
            if (currentUIState == MAIN_MENU)
                drawMainMenuScreen();
            else
                drawSubMenuScreen();
            oledNeedsRedraw = false;
            lastOLEDUpdate = now;
        }
        return;
    }

    if (currentUIState == DEFAULT_SCREEN) {
        bool changing =
            bladePowerState == LED_POWER_IGNITING ||
            bladePowerState == LED_POWER_RETRACTING;

        unsigned long period =
            changing
            ? OLED_POWER_ANIMATION_MS
            : 1000UL;

        if (oledNeedsRedraw ||
            now - lastOLEDUpdate >= period)
        {
            lastOLEDUpdate = now;
            updateOLEDPowerScreen();
            oledNeedsRedraw = false;
        }
    }
}

void setup()
{
    Serial.begin(
        115200
    );

    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);
    lastCLK = digitalRead(ENCODER_CLK);

    selectOLEDI2C();
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        oledReady = true;
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setTextWrap(false);

        drawSaberScreen("STARTING", 0);

        selectMPUI2C();
    } else {
        Serial.println("OLED FAILED - CONTINUING WITHOUT OLED");
        selectMPUI2C();
    }

    FastLED.addLeds<
        LED_TYPE,
        LED_PIN,
        LED_ORDER
    >(leds, NUM_LEDS);

    FastLED.setBrightness(LED_BRIGHTNESS);
    FastLED.clear();
    FastLED.show();

    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "       PICO 2 SMOOTHSWING ENGINE"
    );

    Serial.println(
        "========================================"
    );

    selectMPUI2C();

    delay(100);

    if (!ccMpuBegin(Wire))
    {
        Serial.println("MPU6050 NOT RESPONDING");
    }

    delay(100);

    Serial.println(
        "MPU READY"
    );

    gyroBiasX = 0.0f;
    gyroBiasY = 0.0f;
    gyroBiasZ = 0.0f;
    Serial.println("GYRO CALIBRATION SKIPPED - DIAGNOSTIC MODE");

    Serial.println();

    Serial.println(
        "Configuring SPI..."
    );

    SPI.setRX(
        SD_MISO
    );

    SPI.setTX(
        SD_MOSI
    );

    SPI.setSCK(
        SD_SCK
    );

    SPI.begin();

    Serial.println(
        "SPI started"
    );

    Serial.println();

    Serial.println(
        "Initializing SD card..."
    );

    if (
        !SD.begin(
            SD_CS
        )
    )
    {
        Serial.println();

        Serial.println(
            "!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        );

        Serial.println(
            "SD CARD INITIALIZATION FAILED"
        );

        Serial.println(
            "!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        );

        while (
            true
        )
        {
            delay(1000);
        }
    }

    Serial.println(
        "SD CARD INITIALIZED!"
    );

    Serial.println();

    Serial.println(
        "Checking required files..."
    );

    bool filesOK =
        true;

    if (
        !SD.exists(
            "/hum.wav"
        )
    )
    {
        Serial.println(
            "MISSING: /hum.wav"
        );

        filesOK =
            false;
    }
    else
    {
        Serial.println(
            "FOUND: /hum.wav"
        );
    }

    if (SD.exists("/swingL4.wav"))
        Serial.println("FOUND: /swingL4.wav");
    else
    {
        Serial.println("MISSING: /swingL4.wav");
        filesOK = false;
    }

    if (SD.exists("/swingH4.wav"))
        Serial.println("FOUND: /swingH4.wav");
    else
    {
        Serial.println("MISSING: /swingH4.wav");
        filesOK = false;
    }

    if (SD.exists("/ignite.wav"))
        Serial.println("FOUND: /ignite.wav");
    else
    {
        Serial.println("MISSING: /ignite.wav");
        filesOK = false;
    }

    if (SD.exists("/poweroff.wav"))
        Serial.println("FOUND: /poweroff.wav");
    else
    {
        Serial.println("MISSING: /poweroff.wav");
        filesOK = false;
    }

    if (!filesOK)
    {
        Serial.println();

        Serial.println(
            "REQUIRED AUDIO FILES ARE MISSING."
        );

        while (
            true
        )
        {
            delay(1000);
        }
    }

    Serial.println();
    Serial.println("Opening ignite/retract sounds...");

    if (!openPowerShot("/ignite.wav", igniteShot))
    {
        Serial.println("IGNITE SOUND OPEN FAILED");
        while (true) { delay(1000); }
    }

    if (!openPowerShot("/poweroff.wav", retractShot))
    {
        Serial.println("RETRACT SOUND OPEN FAILED");
        while (true) { delay(1000); }
    }

    Serial.println();

    Serial.println(
        "Opening hum for streaming..."
    );

    if (
        !openStreamClip(
            "/hum.wav",
            hum
        )
    )
    {
        Serial.println(
            "HUM OPEN FAILED"
        );

        while (
            true
        )
        {
            delay(1000);
        }
    }

    Serial.println();
    Serial.println("Opening swing pair 4 for streaming...");

    if (!openSwingPair(FIXED_SWING_PAIR))
    {
        Serial.println("INITIAL SWING PAIR OPEN FAILED");
        while (true) { delay(1000); }
    }

    Serial.println();
    Serial.println("Opening clash sound for streaming...");

    if (!openClashClip(CLASH_FILE))
    {
        Serial.println("CLASH OPEN FAILED");
        while (true) { delay(1000); }
    }

    clash.position = (float)clash.sampleCount;
    clashActive = false;
    lastClashMillis = 0;

    Serial.println();
    Serial.print("PAIR ");
    Serial.print(FIXED_SWING_PAIR);
    Serial.println(" LOCKED.");
    Serial.println("SWING AUDIO: /swingL4.wav + /swingH4.wav");
    Serial.println("ONE SWING PAIR ONLY - NO FILE SWITCHING");

    Serial.println();

    Serial.println(
        "Starting I2S..."
    );

    i2s.setBCLK(
        I2S_BCLK
    );

    i2s.setDATA(
        I2S_DIN
    );

    i2s.setBitsPerSample(
        16
    );

    i2s.setBuffers(
        8,
        256
    );

    if (
        !i2s.begin(
            AUDIO_RATE
        )
    )
    {
        Serial.println(
            "I2S FAILED"
        );

        while (
            true
        )
        {
            delay(1000);
        }
    }

    Serial.println(
        "I2S READY"
    );

    swingVolume =
        0.0f;

    targetSwingVolume =
        0.0f;

    humVolume =
        1.0f;

    targetHumVolume =
        1.0f;

    playbackRate =
        0.72f;

    targetPlaybackRate =
        0.72f;

    lowMix =
        1.0f;

    highMix =
        0.0f;

    targetLowMix =
        1.0f;

    targetHighMix =
        0.0f;

    hum.position =
        0.0f;

    isMoving =
        false;

    isSpinning =
        false;

    lastMotionMicros =
        micros();

    limiterAttackCoeff =
        expf(
            -1.0f /
            (
                (
                    LIMITER_ATTACK_MS /
                    1000.0f
                )
                *
                AUDIO_RATE
            )
        );

    limiterReleaseCoeff =
        expf(
            -1.0f /
            (
                (
                    LIMITER_RELEASE_MS /
                    1000.0f
                )
                *
                AUDIO_RATE
            )
        );

    limiterEnvelope =
        0.0f;

    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "             READY"
    );

    Serial.println(
        "========================================"
    );

    Serial.println(
        "Hum: ON"
    );

    Serial.println(
        "Swing L/H: ONE PAIR - CONTINUOUS"
    );

    Serial.println(
        "Swing threshold: 1.8"
    );

    Serial.println(
        "Slow swing: ENABLED"
    );

    Serial.println(
        "Fast swing: ENABLED"
    );

    Serial.println(
        "Long swing: CONTINUOUS"
    );

    Serial.println(
        "Spin: ENABLED"
    );

    Serial.println(
        "========================================"
    );

    triggerBladeIgnite();
    oledNeedsRedraw = true;
    updateMenuDisplay();
}

void loop()
{
    updateEncoder();
    handleMenuButton();
    checkBladeSerialCommands();
#if LIVE_TEST
    liveTestUpdate();
#endif

    if (
        millis() -
        lastIMUUpdate >=
        3
    )
    {
        lastIMUUpdate =
            millis();

        updateMotion();

        updateAudioControl();

        float clashProb[3];
        unsigned long classifyStart = micros();
        int clashZone = ccPollClassification(clashProb);

        if (clashZone >= 0)
        {
            unsigned long classifyMicros = micros() - classifyStart;

            setClashVisualZone(clashZone);
            triggerClashVisual();
#if LIVE_TEST
            liveTestOnZone(clashZone, clashProb, ccLastPeakG);
#endif

            Serial.print("CLASH ZONE: ");
            Serial.print(CC_ZONE_NAMES[clashZone]);
            Serial.print("  p=");
            Serial.print(clashProb[0], 2); Serial.print('/');
            Serial.print(clashProb[1], 2); Serial.print('/');
            Serial.print(clashProb[2], 2);
            Serial.print("  peak=");
            Serial.print(ccLastPeakG, 2);
            Serial.print("g  t_classify=");
            Serial.print(classifyMicros);
            Serial.print("us  t_since_trigger=");
            Serial.print((ccTotal - ccLastTrigger) * 1000UL / CC_FS_HZ);
            Serial.println("ms");
        }
    }

    refillPowerShot(igniteShot);
    refillPowerShot(retractShot);

    serviceAudio(true);

    updateBladeLEDs();
    serviceAudio(false);

    updateMenuDisplay();
    serviceAudio(false);

    refillStreamClip(
        hum
    );

    refillStreamClip(
        swingLow
    );

    refillStreamClip(
        swingHigh
    );

    refillClashClip();
    serviceAudio(false);

    if (
        millis() -
        lastDebugPrint >=
        500
    )
    {
        lastDebugPrint =
            millis();

        Serial.print(
            "Speed:"
        );

        Serial.print(
            gyroSpeed,
            2
        );

        Serial.print(
            "  Swing:"
        );

        Serial.print(
            swingVolume,
            3
        );

        Serial.print(
            "  Rate:"
        );

        Serial.print(
            playbackRate,
            2
        );

        Serial.print(
            "  Hum:"
        );

        Serial.print(
            humVolume,
            2
        );

        Serial.print(
            "  L:"
        );

        Serial.print(
            lowMix,
            2
        );

        Serial.print(
            "  H:"
        );

        Serial.print(
            highMix,
            2
        );

        Serial.print("  Pair:4");

        Serial.print(
            "  Move:"
        );

        Serial.print(
            isMoving
            ? "YES"
            : "NO"
        );

        Serial.print(
            "  Spin:"
        );

        Serial.println(
            isSpinning
            ? "YES"
            : "NO"
        );
    }
}
