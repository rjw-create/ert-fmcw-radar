/*
 * ert_fmcw_sensor.ino
 * ====================
 * Firmware for a handheld subsurface imaging sensor that fuses
 * Electrical Resistivity Tomography (ERT) and FMCW Radar data.
 *
 * Hardware:
 *   - Arduino Uno R3
 *   - ADS1115 16-bit ADC (I²C) .............. ERT voltage acquisition
 *   - HLK-LD2410C 24 GHz FMCW radar ........ Subsurface density sensing
 *   - 4× stainless steel ground probes ...... Wenner array electrodes
 *
 * Data output (USB serial, 115200 baud):
 *   ADC,<scan>,<a0>,<a1>,<a2>,<a3>,<d01>
 *   RADAR,<scan>,<status>,<detectDist>,<score>,<ageMs>
 *
 * Author:  Jackson Raines
 * License: MIT
 */

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <SoftwareSerial.h>

// ─── Configuration ───────────────────────────────────────────────────────────

const long     PC_BAUD       = 115200;   // USB serial baud rate
const long     LD_BAUD       = 115200;   // LD2410C default baud rate
const int      NUM_AVG       = 2;        // ADC samples to average per reading
const unsigned long OUTPUT_MS = 250;     // Acquisition interval (4 Hz)

// ─── Peripheral Instances ────────────────────────────────────────────────────

Adafruit_ADS1115 ads;                    // 16-bit ADC on I²C bus
SoftwareSerial   ldSerial(3, 4);         // Radar RX=D3, TX=D4

// ─── LD2410C Frame Protocol Constants ────────────────────────────────────────
//
// The LD2410C streams binary frames with a 4-byte header, 2-byte length,
// variable payload, and 4-byte tail. We parse these with a state machine.

const byte HDR[4]  = { 0xF4, 0xF3, 0xF2, 0xF1 };
const byte TAIL[4] = { 0xF8, 0xF7, 0xF6, 0xF5 };
const int  MAX_PAYLOAD = 160;
byte       payload[MAX_PAYLOAD];

// ─── Frame Parser State Machine ──────────────────────────────────────────────

enum ParseState {
  FIND_HDR,       // Scanning for 4-byte header sequence
  READ_LEN_LO,    // Reading payload length (low byte)
  READ_LEN_HI,    // Reading payload length (high byte)
  READ_PAYLOAD,   // Buffering payload bytes
  READ_TAIL       // Validating 4-byte tail sequence
};

ParseState   state    = FIND_HDR;
byte         hdrIdx   = 0;
byte         tailIdx  = 0;
unsigned int frameLen = 0;
unsigned int payIdx   = 0;

// ─── Scan & Timing State ────────────────────────────────────────────────────

unsigned long scanIndex      = 0;
unsigned long lastOutputMs   = 0;
unsigned long lastGoodRadarMs = 0;
bool          radarSeen      = false;

// ─── Latest Radar Readings ──────────────────────────────────────────────────

byte         latestStatus       = 0;   // 0=no target, 1=moving, 2=stationary, 3=both
unsigned int latestMoveDistCm   = 0;   // Moving target distance (cm)
byte         latestMoveEnergy   = 0;   // Moving target energy (0-100)
unsigned int latestStatDistCm   = 0;   // Stationary target distance (cm)
byte         latestStatEnergy   = 0;   // Stationary target energy (0-100)
unsigned int latestDetectDistCm = 0;   // Detection distance (cm)
int          latestScore        = 0;   // Combined energy score

// ─── Signal Smoothing ───────────────────────────────────────────────────────
//
// Exponential moving average (EMA) with alpha=0.25 provides a balance
// between responsiveness and noise rejection for a handheld device
// being physically swept across terrain.

float smoothDist  = 0.0f;
float smoothScore = 0.0f;
bool  smoothInit  = false;

// ─── Baseline Calibration ───────────────────────────────────────────────────
//
// The first 20 radar frames establish an ambient energy baseline.
// Subsequent scores are reported as deltas above this baseline,
// so that only true anomalies produce nonzero readings.

bool baselineDone  = false;
int  baselineCount = 0;
long baselineSum   = 0;
int  baselineScore = 0;


// ═══════════════════════════════════════════════════════════════════════════════
//  UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════════

/** Parse a 16-bit unsigned integer from a little-endian byte pair. */
unsigned int u16le(const byte *p) {
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

/** Convert raw ADS1115 counts to volts (GAIN_ONE → ±4.096 V, 125 µV/bit). */
float countsToVolts(int16_t raw) {
  return raw * 0.000125f;
}

/** Read a single-ended ADC channel with averaging. */
float readAvgSingleEnded(uint8_t ch) {
  long sum = 0;
  for (int i = 0; i < NUM_AVG; i++) {
    sum += ads.readADC_SingleEnded(ch);
    delay(1);
  }
  return countsToVolts((int16_t)(sum / NUM_AVG));
}

/** Read differential ADC (A0–A1) with averaging. */
float readAvgDiff01() {
  long sum = 0;
  for (int i = 0; i < NUM_AVG; i++) {
    sum += ads.readADC_Differential_0_1();
    delay(1);
  }
  return countsToVolts((int16_t)(sum / NUM_AVG));
}


// ═══════════════════════════════════════════════════════════════════════════════
//  LD2410C RADAR PARSING
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Parse a "basic" LD2410C data frame (type 0x01 or 0x02).
 *
 * Payload layout (after data-type and 0xAA marker):
 *   [0]   target status
 *   [1-2] moving target distance (cm, uint16 LE)
 *   [3]   moving target energy (0-100)
 *   [4-5] stationary target distance (cm, uint16 LE)
 *   [6]   stationary target energy (0-100)
 *   [7-8] detection distance (cm, uint16 LE)
 *
 * Returns true if the frame was valid and parsed successfully.
 */
bool parseBasicPayload(const byte *p, unsigned int n) {
  if (n < 11) return false;

  byte dataType = p[0];
  if (!(dataType == 0x01 || dataType == 0x02)) return false;
  if (p[1] != 0xAA) return false;

  const byte *t = &p[2];
  latestStatus       = t[0];
  latestMoveDistCm   = u16le(&t[1]);
  latestMoveEnergy   = t[3];
  latestStatDistCm   = u16le(&t[4]);
  latestStatEnergy   = t[6];
  latestDetectDistCm = u16le(&t[7]);
  latestScore        = (int)latestMoveEnergy + (int)latestStatEnergy;

  radarSeen       = true;
  lastGoodRadarMs = millis();

  // ── Baseline calibration (first 20 frames) ──
  if (!baselineDone) {
    baselineSum += latestScore;
    baselineCount++;
    if (baselineCount >= 20) {
      baselineScore = baselineSum / baselineCount;
      baselineDone  = true;
    }
  }

  // ── Subtract baseline and clamp ──
  int usableScore       = latestScore;
  unsigned int usableDist = latestDetectDistCm;

  if (baselineDone) {
    usableScore -= baselineScore;
    if (usableScore < 0) usableScore = 0;
  }

  // Zero out when no target is detected
  if (latestStatus == 0 || usableDist == 0) {
    usableScore = 0;
    usableDist  = 0;
  }

  // ── Exponential moving average (α = 0.25) ──
  if (!smoothInit) {
    smoothDist  = (float)usableDist;
    smoothScore = (float)usableScore;
    smoothInit  = true;
  } else {
    smoothDist  = 0.75f * smoothDist  + 0.25f * (float)usableDist;
    smoothScore = 0.75f * smoothScore + 0.25f * (float)usableScore;
  }

  return true;
}

/**
 * Poll the LD2410C serial buffer and advance the frame parser state machine.
 * Should be called frequently (multiple times per loop iteration) to avoid
 * buffer overruns on SoftwareSerial.
 */
void pollLD2410() {
  while (ldSerial.available()) {
    byte b = (byte)ldSerial.read();

    switch (state) {

      case FIND_HDR:
        if (b == HDR[hdrIdx]) {
          hdrIdx++;
          if (hdrIdx >= 4) {
            hdrIdx = 0;
            state  = READ_LEN_LO;
          }
        } else {
          hdrIdx = (b == HDR[0]) ? 1 : 0;
        }
        break;

      case READ_LEN_LO:
        frameLen = b;
        state    = READ_LEN_HI;
        break;

      case READ_LEN_HI:
        frameLen |= ((unsigned int)b << 8);
        if (frameLen == 0 || frameLen > MAX_PAYLOAD) {
          state    = FIND_HDR;
          frameLen = 0;
          payIdx   = 0;
          tailIdx  = 0;
        } else {
          payIdx = 0;
          state  = READ_PAYLOAD;
        }
        break;

      case READ_PAYLOAD:
        payload[payIdx++] = b;
        if (payIdx >= frameLen) {
          tailIdx = 0;
          state   = READ_TAIL;
        }
        break;

      case READ_TAIL:
        if (b == TAIL[tailIdx]) {
          tailIdx++;
          if (tailIdx >= 4) {
            // ── Complete frame received — parse it ──
            parseBasicPayload(payload, frameLen);
            state    = FIND_HDR;
            frameLen = 0;
            payIdx   = 0;
            tailIdx  = 0;
          }
        } else {
          // Tail mismatch — discard and resync
          state    = FIND_HDR;
          frameLen = 0;
          payIdx   = 0;
          tailIdx  = 0;
        }
        break;
    }
  }
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SETUP & MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════════

void setup() {
  delay(1500);  // Allow peripherals to power up

  Serial.begin(PC_BAUD);
  delay(200);
  Serial.println("SYSTEM START");

  // ── Initialize ADS1115 ──
  if (!ads.begin()) {
    Serial.println("ERROR,ADS1115_NOT_FOUND");
    while (1);  // Halt — ADC is critical
  }
  ads.setGain(GAIN_ONE);  // ±4.096 V range, 125 µV/bit
  Serial.println("ADS1115_OK");

  // ── Initialize LD2410C ──
  ldSerial.begin(LD_BAUD);
  Serial.println("LD2410C_OK");

  // ── Print CSV column headers ──
  Serial.println("FORMAT_ADC,scan,a0,a1,a2,a3,d01");
  Serial.println("FORMAT_RADAR,scan,status,detectDist,score,ageMs");
}

void loop() {
  // Poll radar 3× per loop to keep up with its ~10 Hz output
  pollLD2410();
  pollLD2410();
  pollLD2410();

  if (millis() - lastOutputMs >= OUTPUT_MS) {
    lastOutputMs = millis();
    scanIndex++;

    // ── Read all ERT channels ──
    float a0  = readAvgSingleEnded(0);  // Probe A (current inject)
    float a1  = readAvgSingleEnded(1);  // Probe M (voltage sense +)
    float a2  = readAvgSingleEnded(2);  // Probe N (voltage sense −)
    float a3  = readAvgSingleEnded(3);  // Probe B (current return)
    float d01 = readAvgDiff01();        // Differential V across M-N

    // ── Output ERT data ──
    Serial.print("ADC,");
    Serial.print(scanIndex);    Serial.print(",");
    Serial.print(a0, 6);       Serial.print(",");
    Serial.print(a1, 6);       Serial.print(",");
    Serial.print(a2, 6);       Serial.print(",");
    Serial.print(a3, 6);       Serial.print(",");
    Serial.println(d01, 6);

    // ── Output radar data ──
    unsigned long ageMs = radarSeen ? (millis() - lastGoodRadarMs) : 999999UL;

    int outScore = (int)(smoothScore + 0.5f);
    int outDist  = (int)(smoothDist  + 0.5f);

    // Zero readings if radar data is stale (>1.2 s since last valid frame)
    if (ageMs > 1200) {
      outScore = 0;
      outDist  = 0;
    }

    Serial.print("RADAR,");
    Serial.print(scanIndex);   Serial.print(",");
    Serial.print(latestStatus); Serial.print(",");
    Serial.print(outDist);      Serial.print(",");
    Serial.print(outScore);     Serial.print(",");
    Serial.println(ageMs);
  }
}
