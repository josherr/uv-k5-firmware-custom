/* frs_appliance/frs_tests.c
 *
 * FRS-Only Appliance Mode — Compile-time Assertions & Test Plan
 *
 * These static_assert() declarations fire at build time if any
 * invariant is violated.  A build failure here means the firmware
 * has been configured in a way that could allow non-FRS TX.
 *
 * The runtime tests are in FRS_RunSelfTest() in frs.c.
 *
 * REGULATORY NOTICE:
 *   Firmware modifications alone do NOT make non-certified hardware an
 *   FCC-certified FRS radio.
 *
 * Licensed under the Apache License, Version 2.0
 */

#ifdef FRS_APPLIANCE_BUILD

#include <assert.h>
#include <stdint.h>

#include "driver/bk4819.h"
#include "frs_appliance/frs.h"
#include "settings.h"

/* ================================================================
 * 1. Channel count is exactly 22
 * ================================================================ */
static_assert(FRS_NUM_CHANNELS == 22u,
    "FAIL: FRS_NUM_CHANNELS must be exactly 22 per FCC Part 95 Subpart B");

/* ================================================================
 * 2. Power enumeration ordering must be correct
 *    (output power is compared with >, so LOW < MID < HIGH must hold)
 * ================================================================ */
static_assert(OUTPUT_POWER_LOW  < OUTPUT_POWER_MID,
    "FAIL: OUTPUT_POWER_LOW must be < OUTPUT_POWER_MID");
static_assert(OUTPUT_POWER_MID  < OUTPUT_POWER_HIGH,
    "FAIL: OUTPUT_POWER_MID must be < OUTPUT_POWER_HIGH");

/* ================================================================
 * 3. Bandwidth enum: NARROW must be non-zero (not equal to WIDE)
 * ================================================================ */
static_assert((int)BK4819_FILTER_BW_NARROW != (int)BK4819_FILTER_BW_WIDE,
    "FAIL: BK4819_FILTER_BW_NARROW must not equal BK4819_FILTER_BW_WIDE");

/* ================================================================
 * 4. The 22 FRS channels fit in the MR channel pool (0–199)
 * ================================================================ */
static_assert(FRS_NUM_CHANNELS <= 200u,
    "FAIL: FRS channels must fit within MR_CHANNEL_FIRST..MR_CHANNEL_LAST");

/* ================================================================
 * 5. Spot-check compile-time frequency values
 *    (Actual table correctness is verified at runtime in FRS_RunSelfTest)
 * ================================================================ */
#define _FRS_CH01_EXPECTED  46256250u   /* 462.5625 MHz */
#define _FRS_CH08_EXPECTED  46756250u   /* 467.5625 MHz */
#define _FRS_CH14_EXPECTED  46771250u   /* 467.7125 MHz */
#define _FRS_CH15_EXPECTED  46255000u   /* 462.5500 MHz */
#define _FRS_CH22_EXPECTED  46272500u   /* 462.7250 MHz */

/* These verify the named macros are internally consistent.
 * The actual table values in frs.c are verified by FRS_RunSelfTest(). */
static_assert(_FRS_CH01_EXPECTED == 46256250u, "CH01 frequency constant wrong");
static_assert(_FRS_CH08_EXPECTED == 46756250u, "CH08 frequency constant wrong");
static_assert(_FRS_CH14_EXPECTED == 46771250u, "CH14 frequency constant wrong");
static_assert(_FRS_CH15_EXPECTED == 46255000u, "CH15 frequency constant wrong");
static_assert(_FRS_CH22_EXPECTED == 46272500u, "CH22 frequency constant wrong");

/* The 467 MHz sub-band gap (ch 8–14) is separated from the main
 * 462 MHz band by ~5 MHz.  Verify the boundary constants. */
static_assert(_FRS_CH08_EXPECTED > _FRS_CH01_EXPECTED + 4500000u,
    "CH08 must be ~5 MHz above CH01 (467 vs 462 MHz)");

/* ================================================================
 * 6. Step size STEP_12_5kHz must exist in the step enum
 * ================================================================ */
static_assert((int)STEP_12_5kHz >= 0,
    "FAIL: STEP_12_5kHz must be a valid step enum value");

/* ================================================================
 * 7. F_LOCK_NONE (UNLOCK_ALL) must be the highest F_LOCK value so
 *    that our clamp (>= F_LOCK_NONE → F_LOCK_ALL) logic is correct
 * ================================================================ */
static_assert((int)F_LOCK_NONE > (int)F_LOCK_ALL,
    "FAIL: F_LOCK_NONE must be numerically greater than F_LOCK_ALL");
static_assert((int)F_LOCK_ALL >= 0,
    "FAIL: F_LOCK_ALL must be a non-negative enum value");

/* ================================================================
 * TEST PLAN (executed at runtime by FRS_RunSelfTest in frs.c)
 * ================================================================
 *
 * T01 – FRS_NUM_CHANNELS == 22
 * T02 – Every channel frequency is in the 462/467 MHz FRS sub-bands
 * T03 – FRS_GetMaxPower(ch 7..13) == OUTPUT_POWER_LOW
 * T04 – FRS_GetMaxPower(ch 0..6 and 14..21) == OUTPUT_POWER_HIGH
 * T05 – FRS_IsTxAllowed returns false for channel_save == 22 (out of range)
 * T06 – FRS_IsTxAllowed returns true  for valid channel 0 with correct settings
 * T07 – FRS_IsTxAllowed returns false for channels 8–14 with OUTPUT_POWER_HIGH
 * T08 – FRS_IsTxAllowed returns false for non-FRS frequency (433 MHz)
 * T09 – FRS_IsTxAllowed returns false for wideband BW on FRS channel
 * T10 – FRS_IsTxAllowed returns false when scrambler is enabled
 *
 * Additional integration checks (manual / bench verification):
 *
 * I01 – After boot, EEPROM channel 0 frequency == frs_channel_table[0]
 * I02 – After boot, EEPROM channel 13 frequency == frs_channel_table[13]
 * I03 – Pressing PTT on ch 8 with HIGH power → VFO_STATE_TX_DISABLE
 * I04 – CHIRP write of 146.520 MHz to slot 0 → read back shows 462.5625 MHz
 * I05 – CHIRP write of F_LOCK_NONE → read back shows F_LOCK_ALL
 * I06 – Scan halts on FRS ch with matching CTCSS; remains muted without match
 * I07 – Binary grep for "UNLOCK" string → not found in FRS release binary
 * I08 – Binary grep for "UNLOCK\nALL" → not found in FRS release binary
 * I09 – VFO mode toggle key → no effect (radio stays in channel mode)
 * I10 – F_LOCK menu item → not visible in FRS appliance menu
 */

#endif /* FRS_APPLIANCE_BUILD */
