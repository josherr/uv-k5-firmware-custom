/* frs_appliance/frs.h
 *
 * FRS-Only Programmable Appliance Mode
 * Quansheng UV-K5 / UV-K6 firmware fork
 *
 * REGULATORY NOTICE:
 *   Firmware modifications alone do NOT make non-certified hardware an
 *   FCC-certified FRS radio.  This firmware enforces FRS operational
 *   constraints but does NOT grant FCC certification or authorization
 *   to operate under Part 95 Subpart B on any particular device.
 *
 * Licensed under the Apache License, Version 2.0
 *
 * Priority order:
 *   1. Prevent illegal TX
 *   2. Prevent bypasses
 *   3. Preserve stable radio operation
 *   4. Maintain simple FRS usability
 *   5. Keep changes maintainable
 */

#ifndef FRS_APPLIANCE_H
#define FRS_APPLIANCE_H

#ifdef FRS_APPLIANCE_BUILD

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration — avoid circular includes */
struct VFO_Info_t;

/* ----------------------------------------------------------------
 * FRS Channel Count -- IMMUTABLE
 * 47 CFR §95.563: exactly 22 FRS channels.
 * Any attempt to change this constant is a regulatory violation.
 * ---------------------------------------------------------------- */
#define FRS_NUM_CHANNELS  22u

/* ----------------------------------------------------------------
 * FRS channel frequencies in the firmware's native 10 Hz units.
 *   actual_Hz = frs_channel_table[ch] * 10
 *
 * Table index 0 == FRS channel 1.
 * Source: 47 CFR §95.563
 * ---------------------------------------------------------------- */
extern const uint32_t frs_channel_table[FRS_NUM_CHANNELS];

/* Human-readable default labels shown on the home screen */
extern const char frs_default_channel_names[FRS_NUM_CHANNELS][10];

/* ----------------------------------------------------------------
 * Power-limit constants (OUTPUT_POWER_xxx enum values, settings.h)
 *
 * Ch  1– 7 (idx  0– 6):  ≤ OUTPUT_POWER_HIGH  (≤ 2 W)
 * Ch  8–14 (idx  7–13):  ≤ OUTPUT_POWER_LOW   (≤ 0.5 W) §95.567(b)
 * Ch 15–22 (idx 14–21):  ≤ OUTPUT_POWER_HIGH  (≤ 2 W)
 * ---------------------------------------------------------------- */
#define FRS_PWR_MAX_CH1_7    OUTPUT_POWER_HIGH
#define FRS_PWR_MAX_CH8_14   OUTPUT_POWER_LOW
#define FRS_PWR_MAX_CH15_22  OUTPUT_POWER_HIGH

/* FRS requires 12.5 kHz narrowband operation */
#define FRS_REQUIRED_BANDWIDTH  BK4819_FILTER_BW_NARROW

/* ----------------------------------------------------------------
 * Central TX Authorization Gate
 *
 * Returns true ONLY if ALL of the following hold:
 *  - channel_save is in [0, FRS_NUM_CHANNELS)
 *  - tx_freq exactly matches frs_channel_table[channel_save]
 *  - RX frequency equals TX frequency (simplex, no split)
 *  - TX_OFFSET_FREQUENCY_DIRECTION is OFF
 *  - TX_OFFSET_FREQUENCY is zero
 *  - FrequencyReverse is false
 *  - Modulation is FM
 *  - CHANNEL_BANDWIDTH is NARROW
 *  - OUTPUT_POWER ≤ FRS_GetMaxPower(channel_save)
 *  - SCRAMBLING_TYPE is 0
 *
 * Call this gate at every TX path.  Fail-closed: returns false on
 * any validation error, including a NULL vfo pointer.
 * ---------------------------------------------------------------- */
bool FRS_IsTxAllowed(uint32_t tx_freq, const struct VFO_Info_t *vfo);

/* Returns the maximum legal OUTPUT_POWER_xxx value for a channel index */
uint8_t FRS_GetMaxPower(uint8_t channel_idx);

/* Returns true if channel_idx is in [0, FRS_NUM_CHANNELS) */
bool FRS_IsValidChannel(uint8_t channel_idx);

/* Returns the exact FRS frequency (10 Hz units) for a channel index.
 * Returns 0 for out-of-range channel_idx. */
uint32_t FRS_GetChannelFreq(uint8_t channel_idx);

/* ----------------------------------------------------------------
 * EEPROM Sanitization
 *
 * Forces a single channel EEPROM block to legal FRS values:
 *  - RX/TX frequency set to exact FRS table value
 *  - TX offset zeroed (no split)
 *  - Modulation forced to FM
 *  - Bandwidth forced to narrow
 *  - Power capped to channel-group maximum
 *  - Scrambler cleared
 *  - DTMF PTT-ID cleared
 *  - Tone code indexes bounds-checked
 *
 * No-op for channel_idx >= FRS_NUM_CHANNELS.
 * Safe to call during boot, after CHIRP import, or after any
 * channel-data EEPROM write.
 * ---------------------------------------------------------------- */
void FRS_SanitizeChannelEEPROM(uint8_t channel_idx);

/* Sanitizes all 22 FRS channel EEPROM blocks plus global FRS
 * settings.  Called once at boot after SETTINGS_InitEEPROM(). */
void FRS_BootValidation(void);

/* Sanitizes an incoming serial (CHIRP/programming) write buffer
 * before EEPROM_WriteBuffer is called.  Modifies pData in-place.
 *  offset  — EEPROM start address of this write
 *  size    — byte count (multiple of 8)
 *  pData   — write buffer (modified in-place) */
void FRS_SanitizeSerialWrite(uint16_t offset, uint16_t size, uint8_t *pData);

/* Runtime self-test — spins forever on failure to prevent TX from a
 * corrupt build.  Call during boot before the main loop. */
void FRS_RunSelfTest(void);

/* ----------------------------------------------------------------
 * Compile-time assertions
 * These fire at build time if invariants are violated.
 * ---------------------------------------------------------------- */
static_assert(FRS_NUM_CHANNELS == 22u,
    "FRS_NUM_CHANNELS must be exactly 22 per FCC Part 95 Subpart B");

#endif /* FRS_APPLIANCE_BUILD */
#endif /* FRS_APPLIANCE_H */
