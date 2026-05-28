/* frs_appliance/frs_chirp.c
 *
 * FRS-Only Appliance Mode — CHIRP / Serial Programming Sanitizer
 *
 * Called from CMD_051D in app/uart.c to intercept every EEPROM write
 * that arrives via the serial programming interface (CHIRP or Quansheng
 * vendor tool).
 *
 * Goals:
 *  - Prevent CHIRP from writing non-FRS frequencies to channel slots 0–21
 *  - Prevent CHIRP from setting TX offsets, wideband BW, or scrambler
 *  - Prevent CHIRP from writing UNLOCK_ALL / F_LOCK_NONE
 *  - Allow CHIRP to write channel labels, tone codes, squelch, BCL, power
 *
 * REGULATORY NOTICE:
 *   Firmware modifications alone do NOT make non-certified hardware an
 *   FCC-certified FRS radio.
 *
 * Licensed under the Apache License, Version 2.0
 */

#ifdef FRS_APPLIANCE_BUILD

#include <string.h>

#include "dcs.h"
#include "driver/bk4819.h"
#include "frequencies.h"
#include "frs_appliance/frs.h"
#include "radio.h"
#include "settings.h"

/* EEPROM layout: each MR channel occupies 16 bytes at ch*16.
 * The first FRS_NUM_CHANNELS (22) channels are the FRS channel bank.
 * Total FRS channel block: 22 * 16 = 352 = 0x160 bytes (0x0000–0x015F). */
#define FRS_EEPROM_CHANNEL_END  ((uint16_t)(FRS_NUM_CHANNELS * 16u))

/* The F_LOCK setting byte is stored at EEPROM offset 0x0F30.
 * We clamp it to F_LOCK_ALL (disables all TX) at most.
 * F_LOCK_NONE (= UNLOCK_ALL) must never be accepted via serial. */
#define EEPROM_FLOCK_OFFSET  0x0F30u

void FRS_SanitizeSerialWrite(uint16_t offset, uint16_t size, uint8_t *pData)
{
    /* Process each 8-byte chunk independently */
    for (uint16_t i = 0u; i < (size / 8u); i++) {
        const uint16_t addr  = offset + (i * 8u);
        uint8_t       *block = pData  + (i * 8u);

        /* --------------------------------------------------------
         * FRS channel data block (EEPROM 0x0000 – 0x015F)
         * -------------------------------------------------------- */
        if (addr < FRS_EEPROM_CHANNEL_END) {
            const uint8_t ch      = (uint8_t)(addr / 16u);
            const uint8_t blk_off = (uint8_t)(addr % 16u);  /* 0 or 8 */

            if (blk_off == 0u) {
                /* Bytes 0–7: frequency (4 B) + TX offset (4 B).
                 * CHIRP-provided values are IGNORED; we force the correct
                 * FRS frequency and zero offset so that even a malformed
                 * or malicious CHIRP image cannot set an illegal frequency. */
                const uint32_t legal_freq  = frs_channel_table[ch];
                const uint32_t zero_offset = 0u;
                memcpy(block + 0u, &legal_freq,   4u);
                memcpy(block + 4u, &zero_offset,  4u);

            } else {
                /* Bytes 8–15: channel settings
                 *  block[0] = RX code value
                 *  block[1] = TX code value
                 *  block[2] = code types  [3:0]=RX type, [7:4]=TX type
                 *  block[3] = [3:0]=TX offset direction, [7:4]=Modulation
                 *  block[4] = [0]=FreqRev, [1]=BW, [3:2]=Power, [4]=BCL
                 *  block[5] = DTMF settings
                 *  block[6] = Step
                 *  block[7] = Scrambler
                 */

                /* Validate code types */
                uint8_t rx_type = block[2] & 0x0Fu;
                uint8_t tx_type = (block[2] >> 4) & 0x0Fu;
                if (rx_type > CODE_TYPE_REVERSE_DIGITAL) rx_type = CODE_TYPE_OFF;
                if (tx_type > CODE_TYPE_REVERSE_DIGITAL) tx_type = CODE_TYPE_OFF;

                /* Bounds-check CTCSS tone indexes */
                if (rx_type == CODE_TYPE_CONTINUOUS_TONE && block[0] >= 50u)
                    block[0] = 0u;
                if (tx_type == CODE_TYPE_CONTINUOUS_TONE && block[1] >= 50u)
                    block[1] = 0u;

                /* Bounds-check DCS code indexes */
                if ((rx_type == CODE_TYPE_DIGITAL || rx_type == CODE_TYPE_REVERSE_DIGITAL)
                    && block[0] >= 104u)
                    block[0] = 0u;
                if ((tx_type == CODE_TYPE_DIGITAL || tx_type == CODE_TYPE_REVERSE_DIGITAL)
                    && block[1] >= 104u)
                    block[1] = 0u;

                block[2] = (uint8_t)(rx_type | (tx_type << 4));

                /* Force TX offset direction = OFF, modulation = FM */
                block[3] = (uint8_t)(TX_OFFSET_FREQUENCY_DIRECTION_OFF
                                   | ((uint8_t)MODULATION_FM << 4));

                /* Cap power, force narrow bandwidth, clear freq-reverse,
                 * preserve BCL (user-programmable) */
                {
                    uint8_t power = (block[4] >> 2) & 3u;
                    uint8_t bcl   = (block[4] >> 4) & 1u;
                    if (power > FRS_GetMaxPower(ch))
                        power = OUTPUT_POWER_LOW;
                    block[4] = (uint8_t)((0u << 0)
                                        | ((uint8_t)BK4819_FILTER_BW_NARROW << 1)
                                        | (power << 2)
                                        | (bcl   << 4));
                }

                /* Clear DTMF PTT-ID, force step, clear scrambler */
                block[5] = 0u;
                block[6] = (uint8_t)STEP_12_5kHz;
                block[7] = 0u;
            }
        }

        /* --------------------------------------------------------
         * Channel name block (EEPROM 0x0800 – 0x08FF for ch 0–127,
         * and 0x0B00 – 0x0BFF for ch 128+).
         * Channel names for ch > 21 are irrelevant in FRS mode but
         * we still allow them to be written (they affect only RX).
         * No sanitization needed for name bytes.
         * -------------------------------------------------------- */

        /* --------------------------------------------------------
         * Global settings area: block any F_LOCK = UNLOCK_ALL write.
         * EEPROM 0x0F30 contains the F_LOCK byte in bits [2:0].
         * -------------------------------------------------------- */
        if (addr == EEPROM_FLOCK_OFFSET) {
            /* Accept F_LOCK_ALL (block all TX) but reject F_LOCK_NONE
             * (UNLOCK_ALL).  Any value ≥ F_LOCK_NONE gets clamped to
             * F_LOCK_ALL.  Values < F_LOCK_ALL are accepted because
             * they are regionally restricted, not unlocked. */
            if (block[0] >= (uint8_t)F_LOCK_NONE)
                block[0] = (uint8_t)F_LOCK_ALL;
        }

        /* --------------------------------------------------------
         * Settings page 0x0F30 also carries the 200TX / 350TX / 500TX
         * enable bytes.  Force them off unconditionally — FRS appliance
         * never enables extended TX bands.
         * (Byte layout from misc.c: 200TX at +2, 350TX at +3, 500TX at +4)
         * -------------------------------------------------------- */
        if (addr == EEPROM_FLOCK_OFFSET) {
            block[2] = 0u;  /* gSetting_200TX = false */
            block[3] = 0u;  /* gSetting_350TX = false */
            block[4] = 0u;  /* gSetting_500TX = false */
        }
    }
}

#endif /* FRS_APPLIANCE_BUILD */
