// Optional live-test mode (LIVE_TEST 1 in Lightsaber_Main.ino): prompts and logs clashes.

#pragma once

#define LT_TOTAL        90
#define LT_TIMEOUT_MS   600UL   // trigger without a zone -> "NO ZONE"
#define LT_LOCKOUT_MS   1500UL  // after a logged clash, extra triggers are ignored

static const char *const LT_STRENGTHS[3] = {"LIGHT", "MEDIUM", "HARD"};

struct LTRow
{
    uint8_t trueZone;      // 0 bottom, 1 middle, 2 top
    uint8_t strength;      // 0 light, 1 medium, 2 hard
    int8_t  predZone;      // -1 = no zone (clash detected, not classified)
    float   p[3];
    float   peakG;
    float   delayMs;       // trigger detected -> ripple shown
};

static LTRow    ltRows[LT_TOTAL];
static uint8_t  ltOrder[LT_TOTAL];      // zone*3 + strength
static int      ltCount        = 0;     // logged clashes
static int      ltMissed       = 0;     // strikes that were not detected
static bool     ltStarted      = false;
static bool     ltDone         = false;
static bool     ltAwaiting     = false; // trigger seen, waiting for zone
static uint32_t ltTrigMicros   = 0;
static uint32_t ltLastLogMs    = 0;     // time of the last logged clash

/** ltBuildOrder(): Builds a random order of prompted clashes, balanced over zone and strength in blocks of nine. */
static void ltBuildOrder()
{
    randomSeed(micros() ^ analogRead(26) ^ (analogRead(27) << 8));
    for (int b = 0; b < LT_TOTAL / 9; b++)
    {
        uint8_t *blk = &ltOrder[b * 9];
        for (int i = 0; i < 9; i++) blk[i] = i;
        for (int i = 8; i > 0; i--)
        {
            int j = random(0, i + 1);
            uint8_t t = blk[i]; blk[i] = blk[j]; blk[j] = t;
        }
    }
}

/** ltPrompt(): Prints the next zone and strength to strike. */
static void ltPrompt()
{
    if (ltDone) return;
    uint8_t c = ltOrder[ltCount];
    Serial.println();
    Serial.print("NEXT  ->  ");
    Serial.print(CC_ZONE_NAMES[c / 3]);
    Serial.print("   ");
    Serial.print(LT_STRENGTHS[c % 3]);
    Serial.print("      (");
    Serial.print(ltCount + 1);
    Serial.print('/');
    Serial.print(LT_TOTAL);
    Serial.println(')');
}

/** ltPrintSummary(): Prints the live test confusion matrix, accuracy and glow delay statistics. */
static void ltPrintSummary()
{
    int conf[3][4] = {{0}};   // columns: bottom, middle, top, no zone
    int correct = 0, noZone = 0;
    float sumDelay = 0, maxDelay = 0;
    int nDelay = 0;

    for (int i = 0; i < ltCount; i++)
    {
        const LTRow &r = ltRows[i];
        int col = (r.predZone < 0) ? 3 : r.predZone;
        conf[r.trueZone][col]++;
        if (r.predZone == r.trueZone) correct++;
        if (r.predZone < 0) noZone++;
        else
        {
            sumDelay += r.delayMs; nDelay++;
            if (r.delayMs > maxDelay) maxDelay = r.delayMs;
        }
    }

    Serial.println();
    Serial.println("================ LIVE TEST SUMMARY ================");
    Serial.print("Clashes logged : "); Serial.println(ltCount);
    Serial.print("Correct        : "); Serial.print(correct);
    Serial.print("  ("); Serial.print(ltCount ? 100.0f * correct / ltCount : 0.0f, 1);
    Serial.println(" %)");
    Serial.print("No zone        : "); Serial.println(noZone);
    Serial.print("Not detected   : "); Serial.print(ltMissed);
    Serial.print("  (detection rate ");
    Serial.print((ltCount + ltMissed) ? 100.0f * ltCount / (ltCount + ltMissed) : 0.0f, 1);
    Serial.println(" %)");
    Serial.print("Ripple delay   : mean ");
    Serial.print(nDelay ? sumDelay / nDelay : 0.0f, 0);
    Serial.print(" ms, max ");
    Serial.print(maxDelay, 0);
    Serial.println(" ms after the clash sound");
    Serial.println();
    Serial.println("                 Shown on blade");
    Serial.println("Hit         Bottom  Middle     Top  NoZone  Correct");
    for (int z = 0; z < 3; z++)
    {
        char line[80];
        int n = conf[z][0] + conf[z][1] + conf[z][2] + conf[z][3];
        snprintf(line, sizeof(line), "%-8s %9d %7d %7d %7d  %5.1f %%",
                 CC_ZONE_NAMES[z], conf[z][0], conf[z][1], conf[z][2], conf[z][3],
                 n ? 100.0f * conf[z][z] / n : 0.0f);
        Serial.println(line);
    }
    Serial.println("===================================================");
}

/** ltPrintCsv(): Prints every logged clash as CSV for copying into a spreadsheet. */
static void ltPrintCsv()
{
    Serial.println();
    Serial.println("#CSV_START");
    Serial.println("n,true_zone,strength,pred_zone,correct,p_bottom,p_middle,p_top,peak_g,ripple_delay_ms");
    for (int i = 0; i < ltCount; i++)
    {
        const LTRow &r = ltRows[i];
        char line[128];
        snprintf(line, sizeof(line), "%d,%s,%s,%s,%d,%.3f,%.3f,%.3f,%.2f,%.1f",
                 i + 1, CC_ZONE_NAMES[r.trueZone], LT_STRENGTHS[r.strength],
                 r.predZone < 0 ? "NONE" : CC_ZONE_NAMES[r.predZone],
                 r.predZone == r.trueZone ? 1 : 0,
                 r.p[0], r.p[1], r.p[2], r.peakG, r.delayMs);
        Serial.println(line);
    }
    Serial.print("#MISSED,"); Serial.println(ltMissed);
    Serial.println("#CSV_END");
}

/** ltLog(): Records the result of one prompted clash (true zone, predicted zone, probabilities, peak and delay). */
static void ltLog(int predZone, const float *prob, float peakG, float delayMs)
{
    if (ltDone || !ltStarted) return;

    uint8_t c = ltOrder[ltCount];
    LTRow &r = ltRows[ltCount];
    r.trueZone = c / 3;
    r.strength = c % 3;
    r.predZone = predZone;
    for (int k = 0; k < 3; k++) r.p[k] = prob ? prob[k] : 0.0f;
    r.peakG = peakG;
    r.delayMs = delayMs;
    ltCount++;
    ltLastLogMs = millis();

    Serial.print("  #");
    Serial.print(ltCount);
    Serial.print("  hit ");
    Serial.print(CC_ZONE_NAMES[r.trueZone]);
    Serial.print("  ->  shown ");
    Serial.print(predZone < 0 ? "NO ZONE" : CC_ZONE_NAMES[predZone]);
    Serial.print(predZone == r.trueZone ? "   CORRECT" : "   WRONG");
    if (predZone >= 0)
    {
        Serial.print("   (p ");
        Serial.print(r.p[predZone], 2);
        Serial.print(", ");
        Serial.print(delayMs, 0);
        Serial.print(" ms)");
    }
    Serial.println();

    if (ltCount >= LT_TOTAL)
    {
        ltDone = true;
        Serial.println();
        Serial.println("LIVE TEST COMPLETE");
        ltPrintSummary();
        ltPrintCsv();
    }
    else
    {
        ltPrompt();
    }
}

/** liveTestUpdate(): Starts the live test after power-up and prints the first prompt. */
static void liveTestUpdate()
{
    if (!ltStarted && millis() > 3000)
    {
        ltBuildOrder();
        ltStarted = true;
        Serial.println();
        Serial.println("============ CLASH CLASSIFIER LIVE TEST ============");
        Serial.println("Ignite the blade and strike the prompted zone.");
        Serial.println("x = reject last   m = strike not detected");
        Serial.println("? = repeat prompt p = summary so far");
        ltPrompt();
    }

    if (ltAwaiting && (micros() - ltTrigMicros) / 1000UL > LT_TIMEOUT_MS)
    {
        ltAwaiting = false;
        ltLog(-1, nullptr, ccLastPeakG, 0.0f);
    }
}

/** liveTestOnTrigger(): Notes the time of a clash trigger (extra triggers from the same strike are ignored). */
static void liveTestOnTrigger()
{
    if (ltAwaiting || millis() - ltLastLogMs < LT_LOCKOUT_MS)
    {
        Serial.println("  (extra clash from the same strike ignored)");
        return;
    }
    ltTrigMicros = micros();
    ltAwaiting = true;
}

/** liveTestOnZone(): Logs the classified zone for the current prompt and moves to the next one. */
static void liveTestOnZone(int zone, const float *prob, float peakG)
{
    if (!ltAwaiting) return;            // zone of an ignored trigger
    float delayMs = (micros() - ltTrigMicros) / 1000.0f;
    ltAwaiting = false;
    ltLog(zone, prob, peakG, delayMs);
}

/** liveTestHandleChar(): Handles live test commands from the Serial Monitor: x rejects the last clash, m marks a missed (undetected) clash, ? repeats the prompt and p prints the summary and CSV. */
static bool liveTestHandleChar(char c)
{
    if (c == 'x' || c == 'X')
    {
        if (ltCount > 0 && !ltDone)
        {
            ltCount--;
            ltAwaiting = false;
            Serial.print("  REJECTED #");
            Serial.print(ltCount + 1);
            Serial.println(" - repeat the same strike");
            ltPrompt();
        }
        return true;
    }
    if (c == 'm' || c == 'M')
    {
        if (!ltDone)
        {
            ltMissed++;
            Serial.print("  NOT DETECTED (total ");
            Serial.print(ltMissed);
            Serial.println(") - repeat the same strike");
            ltPrompt();
        }
        return true;
    }
    if (c == '?')
    {
        ltPrompt();
        return true;
    }
    if (c == 'p' || c == 'P')
    {
        ltPrintSummary();
        ltPrintCsv();
        return true;
    }
    return false;
}
