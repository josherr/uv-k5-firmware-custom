# FRS-Only Programmable Appliance Mode

**Quansheng UV-K5 / UV-K6 Firmware Fork**

---

## REGULATORY NOTICE — PLEASE READ

> **Firmware modifications alone do NOT make non-certified hardware an
> FCC-certified FRS radio.  This firmware enforces FRS operational
> constraints but does NOT grant FCC Part 95 Subpart B certification or
> authorization to operate as a certified FRS radio on any particular
> device.  Users are responsible for complying with all applicable
> regulations.**

---

## Overview

This branch adds a compile-time build target, `FRS_APPLIANCE_BUILD=1`,
that transforms the UV-K5 / UV-K6 into a hardened FRS-style appliance
radio:

- TX and RX **only** on the 22 FCC FRS channels (47 CFR §95.563)
- No arbitrary frequency transmit
- No VFO transmit
- No band unlock or hidden TX expansion
- Simple user programming similar to commercial FRS radios
- CTCSS / DCS tone squelch
- Busy-channel lockout (BCL)
- Channel scan and channel labels
- Strong defense-in-depth against accidental or malicious non-FRS TX

---

## FRS Channel Table (immutable)

| CH  | Frequency  | Group    | Max Power |
|-----|------------|----------|-----------|
| 01  | 462.5625 MHz | Main   | 2 W       |
| 02  | 462.5875 MHz | Main   | 2 W       |
| 03  | 462.6125 MHz | Main   | 2 W       |
| 04  | 462.6375 MHz | Main   | 2 W       |
| 05  | 462.6625 MHz | Main   | 2 W       |
| 06  | 462.6875 MHz | Main   | 2 W       |
| 07  | 462.7125 MHz | Main   | 2 W       |
| 08  | 467.5625 MHz | **Sub** | **0.5 W** |
| 09  | 467.5875 MHz | **Sub** | **0.5 W** |
| 10  | 467.6125 MHz | **Sub** | **0.5 W** |
| 11  | 467.6375 MHz | **Sub** | **0.5 W** |
| 12  | 467.6625 MHz | **Sub** | **0.5 W** |
| 13  | 467.6875 MHz | **Sub** | **0.5 W** |
| 14  | 467.7125 MHz | **Sub** | **0.5 W** |
| 15  | 462.5500 MHz | Shared | 2 W       |
| 16  | 462.5750 MHz | Shared | 2 W       |
| 17  | 462.6000 MHz | Shared | 2 W       |
| 18  | 462.6250 MHz | Shared | 2 W       |
| 19  | 462.6500 MHz | Shared | 2 W       |
| 20  | 462.6750 MHz | Shared | 2 W       |
| 21  | 462.7000 MHz | Shared | 2 W       |
| 22  | 462.7250 MHz | Shared | 2 W       |

Source: 47 CFR §95.563. Channels 8–14 power limit per §95.567(b).

---

## Build Instructions

### Standard (stock firmware, no change)

```bash
make
```

### FRS Appliance Build

```bash
make FRS_APPLIANCE_BUILD=1
```

Output file: `firmware_frs.bin`

This produces a `firmware_frs.packed.bin` suitable for flashing with
the standard Quansheng flashing tool.

### Verification — check for banned strings

After building, verify that the FRS release binary does not contain
TX-unlock patterns:

```bash
strings firmware_frs.bin | grep -i "UNLOCK"
# Expected: no output

strings firmware_frs.bin | grep "UNLOCK ALL"
# Expected: no output

strings firmware_frs.bin | grep "F_LOCK_NONE"
# Expected: no output
```

---

## Architecture

### Defense-in-Depth TX Blocking

Three independent layers block non-FRS TX:

```
Layer 1: TX_freq_check() in frequencies.c
  — Scans frs_channel_table[]; rejects any frequency not in
    the exact 22-entry FRS table.

Layer 2: FRS_IsTxAllowed() in RADIO_PrepareTX() in radio.c
  — Verifies channel index, exact frequency match, RX=TX
    (no split), no TX offset, FM modulation, narrow bandwidth,
    legal power, no scrambler.

Layer 3: FRS_IsTxAllowed() before PA ENABLE in RADIO_SetTxParameters()
  — Last check before BK4819 PA GPIO asserts.  Even if Layers 1
    and 2 were bypassed by a bug or code path, the PA stays
    disabled.
```

### EEPROM Sanitization

On every boot, `FRS_BootValidation()` calls `FRS_SanitizeChannelEEPROM()`
for all 22 channel slots:

- Forces RX frequency to exact FRS table value
- Zeros TX offset
- Forces modulation to FM
- Forces bandwidth to narrow (12.5 kHz)
- Caps power to channel-group legal maximum
- Clears scrambler and DTMF PTT-ID
- Validates CTCSS/DCS code indexes

Corrupt EEPROM **fails closed** — the radio enters a safe state
rather than enabling unrestricted TX.

### CHIRP / Serial Programming Sanitizer

`FRS_SanitizeSerialWrite()` intercepts every EEPROM write from the
serial programming interface (CHIRP / Quansheng vendor tool):

- Frequency bytes for channels 0–21 are replaced with legal FRS values
- TX offsets are zeroed
- Bandwidth is forced to narrow
- Power is capped to channel-group maximum
- Scrambler is cleared
- F_LOCK = UNLOCK_ALL writes are rejected

### Menu Restrictions

Dangerous menu items are **removed at compile time** in FRS mode,
not just hidden:

| Removed Item     | Why |
|------------------|-----|
| Step (MENU_STEP) | Frequency step — no VFO in FRS mode |
| TxODir, TxOffs   | TX offset — no repeater split |
| W/N (bandwidth)  | Forced to narrow by TX gate |
| Scramb           | Scrambler cleared; §95.589 prohibits it |
| Demodu (AM)      | FRS is FM-only |
| F Lock           | TX frequency unlock |
| Tx 200 / Tx 350 / Tx 500 | Band expansion |
| 350 En, ScraEn   | TX/scrambler expansion |
| RxMode (TDR)     | Dual-watch / cross-band TX paths |

### VFO Mode Blocked

`COMMON_SwitchVFOMode()` returns immediately in FRS mode.  The
radio stays in memory-channel mode at all times.  `gEeprom.VFO_OPEN`
is forced to `false` on boot.

---

## User Features (FRS Appliance Mode)

### What Users Can Change

**Per-channel (programmable via CHIRP or on-radio menu):**
- Channel label (up to 10 characters)
- RX CTCSS tone
- TX CTCSS tone
- RX DCS code
- TX DCS code
- Tone mode (off / CTCSS / DCS)
- Squelch level
- Busy-channel lockout (BCL)
- Scan include/exclude
- Legal power selection (LOW or HIGH; channels 8–14 only LOW)

**Global:**
- Volume
- Key lock / auto key lock
- Backlight
- Beep
- VOX (optional)
- Battery save
- Display preferences
- TX timeout (TOT)
- NOAA weather RX (receive-only)
- FM broadcast RX (receive-only)

### What Users Cannot Change

- Frequencies (immutable — enforced at boot, EEPROM load, and TX)
- Channel count (fixed at 22)
- Bandwidth (forced to 12.5 kHz narrowband)
- TX offset or repeater split (zeroed)
- Modulation (FM only)
- Scrambler (always off; prohibited by §95.589)
- F_LOCK / TX unlock settings

### Home Screen

The home screen shows `FRS nn` where `nn` is the FRS channel number
(01–22) instead of the raw memory-channel index.

When a TX attempt is rejected by the FRS gate, the display shows:
```
  FRS ONLY
```
and a double beep is played.

---

## CTCSS / DCS Tone Squelch

### RX Behavior

| RX Tone Mode | Behavior |
|-------------|----------|
| OFF         | Unmute on any valid carrier |
| CTCSS       | Unmute only when matching tone detected |
| DCS         | Unmute only when matching code + polarity detected |

Wrong or missing tone: audio remains muted.  Carrier/busy indicator
still activates (visible busy-channel lockout signal).

### TX Behavior

| TX Tone Mode | Behavior |
|-------------|----------|
| OFF         | Carrier + audio only (no subaudible tone) |
| CTCSS       | Transmit configured CTCSS tone |
| DCS         | Transmit configured DCS code |

No voice scrambling or encryption of any kind.  Scrambler function
is disabled and cleared by the EEPROM sanitizer.

---

## Busy-Channel Lockout (BCL)

Standard BCL: blocks PTT when carrier is detected on the RX channel.

The radio will still **display** a busy indicator even if the
received tone does not match the RX tone filter, giving the user
awareness of ongoing conversations that tone squelch would otherwise
conceal.

---

## CHIRP Programming

CHIRP programming is allowed for **metadata only**:

| Field | Allowed |
|-------|---------|
| Channel name | ✓ |
| CTCSS / DCS tone | ✓ |
| Squelch level | ✓ |
| Scan enable | ✓ |
| BCL | ✓ |
| Power (within legal limits) | ✓ |
| Frequency | Ignored — forced to FRS table value |
| TX offset | Ignored — forced to zero |
| Bandwidth | Ignored — forced to narrow |
| Scrambler | Ignored — forced to off |
| F_LOCK | Clamped — UNLOCK_ALL rejected |

A CHIRP image that specifies channel 1 as 146.520 MHz will be
accepted but the frequency will be silently corrected to 462.5625 MHz
on import.

---

## Threat Model and Risk Analysis

| Threat | Mitigation |
|--------|------------|
| User manually entering frequency | VFO mode disabled; keypad frequency entry blocked |
| Corrupt EEPROM enabling wide TX | Boot sanitization; fails closed |
| CHIRP importing non-FRS frequencies | FRS_SanitizeSerialWrite() corrects before EEPROM write |
| Menu navigation to F_LOCK | Item removed from menu at compile time |
| UNLOCK_ALL via hidden key combo | Hidden menu section removed; FIRST_HIDDEN_MENU_ITEM = 0xFF |
| Firmware bug bypasses Layer 1 check | Layer 2 (FRS_IsTxAllowed in RADIO_PrepareTX) catches it |
| Firmware bug bypasses Layer 2 | Layer 3 (PA enable gate in RADIO_SetTxParameters) catches it |
| Runtime memory corruption | Self-test at boot spins forever; radio cannot transmit |
| Cross-band TX path | CROSS_BAND_RX_TX forced to OFF at boot |
| Frequency reverse mode | FrequencyReverse checked in FRS_IsTxAllowed |
| TX offset set in EEPROM | Checked in FRS_IsTxAllowed and cleared by sanitizer |
| Wideband TX | CHANNEL_BANDWIDTH checked in FRS_IsTxAllowed |
| Scrambler enabled | SCRAMBLING_TYPE checked in FRS_IsTxAllowed; cleared by sanitizer |
| High power on ch 8–14 | FRS_GetMaxPower() checked in FRS_IsTxAllowed and menu handler |

---

## Known Limitations and Suggested Future Hardening

1. **No hardware-level TX interlock.**  This firmware relies on
   software controls only.  Hardware-certified FRS radios use RF
   hardware interlock circuits.

2. **Calibration accuracy.**  The UV-K5 TX carrier frequency accuracy
   depends on the BK4819 crystal calibration.  Verify frequency
   accuracy with a spectrum analyzer.

3. **Actual TX power.**  The UV-K5 TX power depends on factory
   calibration.  Verify actual radiated power meets §95.567 limits
   with a power meter.

4. **Read-only EEPROM.**  A future hardening step could set an
   EEPROM write-protection bit (if supported by hardware) to make
   FRS channel data physically read-only.

5. **Developer builds.**  The `FRS_APPLIANCE_BUILD` flag must be
   checked before any release.  Automate this with CI: reject
   builds where `FRS_APPLIANCE_BUILD=0` that contain the string
   "firmware_frs" in the output filename.

6. **FCC certification.**  For fully compliant FRS operation, use
   a hardware device that holds an FCC grant under Part 95 Subpart B.

---

## Files Changed

| File | Change |
|------|--------|
| `frs_appliance/frs.h` | Core FRS header — channel table, TX gate API |
| `frs_appliance/frs.c` | TX authorization gate, EEPROM sanitization, self-test |
| `frs_appliance/frs_chirp.c` | CHIRP / serial write sanitizer |
| `frs_appliance/frs_tests.c` | Compile-time assertions |
| `frequencies.c` | `TX_freq_check()` — FRS allow-list when `FRS_APPLIANCE_BUILD=1` |
| `radio.c` | `RADIO_PrepareTX()` — Layer 2 TX gate; `RADIO_SetTxParameters()` — Layer 3 PA gate |
| `main.c` | `FRS_RunSelfTest()` and `FRS_BootValidation()` called at boot |
| `settings.c` | `#include "frs_appliance/frs.h"` added |
| `app/menu.c` | `MENU_TXP` power clamp; `MENU_F_LOCK` / `MENU_200TX` etc. blocked |
| `app/uart.c` | `FRS_SanitizeSerialWrite()` called before CMD_051D EEPROM write |
| `app/common.c` | `COMMON_SwitchVFOMode()` is a no-op in FRS mode |
| `ui/menu.c` | FRS-safe `MenuList[]` replaces stock list when `FRS_APPLIANCE_BUILD=1` |
| `ui/main.c` | `VFO_STATE_TX_DISABLE` shows `"FRS ONLY"`; channel indicator shows `"FRS nn"` |
| `Makefile` | `FRS_APPLIANCE_BUILD ?= 0` option and OBJS/CFLAGS additions |

---

*This document is part of the UV-K5 FRS Appliance firmware fork.*
*For regulatory questions, consult an FCC communications attorney.*
