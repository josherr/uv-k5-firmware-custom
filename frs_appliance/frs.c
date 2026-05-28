/* frs_appliance/frs.c
 *
 * FRS-Only Appliance Mode — Core TX Authorization Gate & EEPROM Sanitization
 *
 * REGULATORY NOTICE:
 *   Firmware modifications alone do NOT make non-certified hardware an
 *   FCC-certified FRS radio.
 *
 * Licensed under the Apache License, Version 2.0
 */

#ifdef FRS_APPLIANCE_BUILD

#include <assert.h>
#include <string.h>

#include "dcs.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "frequencies.h"
#include "frs_appliance/frs.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"

/* ================================================================
 * FRS Channel Frequency Table (const — stored in ROM)
 *
 * Units: 10 Hz  (firmware-native: freq_Hz = table_value * 10)
 * Source: 47 CFR §95.563
 *
 * This table is the single source of truth for every TX and EEPROM
 * frequency check.  It must not be modified at runtime.
 * ================================================================ */
const uint32_t frs_channel_table[FRS_NUM_CHANNELS] = {
    /* idx 0  = FRS Ch 01 */ 46256250u,  /* 462.5625 MHz */
    /* idx 1  = FRS Ch 02 */ 46258750u,  /* 462.5875 MHz */
    /* idx 2  = FRS Ch 03 */ 46261250u,  /* 462.6125 MHz */
    /* idx 3  = FRS Ch 04 */ 46263750u,  /* 462.6375 MHz */
    /* idx 4  = FRS Ch 05 */ 46266250u,  /* 462.6625 MHz */
    /* idx 5  = FRS Ch 06 */ 46268750u,  /* 462.6875 MHz */
    /* idx 6  = FRS Ch 07 */ 46271250u,  /* 462.7125 MHz */
    /* idx 7  = FRS Ch 08 */ 46756250u,  /* 467.5625 MHz */
    /* idx 8  = FRS Ch 09 */ 46758750u,  /* 467.5875 MHz */
    /* idx 9  = FRS Ch 10 */ 46761250u,  /* 467.6125 MHz */
    /* idx 10 = FRS Ch 11 */ 46763750u,  /* 467.6375 MHz */
    /* idx 11 = FRS Ch 12 */ 46766250u,  /* 467.6625 MHz */
    /* idx 12 = FRS Ch 13 */ 46768750u,  /* 467.6875 MHz */
    /* idx 13 = FRS Ch 14 */ 46771250u,  /* 467.7125 MHz */
    /* idx 14 = FRS Ch 15 */ 46255000u,  /* 462.5500 MHz */
    /* idx 15 = FRS Ch 16 */ 46257500u,  /* 462.5750 MHz */
    /* idx 16 = FRS Ch 17 */ 46260000u,  /* 462.6000 MHz */
    /* idx 17 = FRS Ch 18 */ 46262500u,  /* 462.6250 MHz */
    /* idx 18 = FRS Ch 19 */ 46265000u,  /* 462.6500 MHz */
    /* idx 19 = FRS Ch 20 */ 46267500u,  /* 462.6750 MHz */
    /* idx 20 = FRS Ch 21 */ 46270000u,  /* 462.7000 MHz */
    /* idx 21 = FRS Ch 22 */ 46272500u,  /* 462.7250 MHz */
};

static_assert(ARRAY_SIZE(frs_channel_table) == 22u,
    "FRS channel table must contain exactly 22 entries");

/* Default channel names shown on the home screen */
const char frs_default_channel_names[FRS_NUM_CHANNELS][10] = {
    "FRS  1",  "FRS  2",  "FRS  3",  "FRS  4",
    "FRS  5",  "FRS  6",  "FRS  7",  "FRS  8",
    "FRS  9",  "FRS 10",  "FRS 11",  "FRS 12",
    "FRS 13",  "FRS 14",  "FRS 15",  "FRS 16",
    "FRS 17",  "FRS 18",  "FRS 19",  "FRS 20",
    "FRS 21",  "FRS 22",
};

/* ================================================================
 * Channel helpers
 * ================================================================ */

bool FRS_IsValidChannel(uint8_t ch)
{
    return ch < FRS_NUM_CHANNELS;
}

uint32_t FRS_GetChannelFreq(uint8_t ch)
{
    if (ch >= FRS_NUM_CHANNELS)
        return 0u;
    return frs_channel_table[ch];
}

uint8_t FRS_GetMaxPower(uint8_t ch)
{
    /* 47 CFR §95.567(b): channels 8–14 ≤ 0.5 W */
    if (ch >= 7u && ch <= 13u)
        return OUTPUT_POWER_LOW;
    return OUTPUT_POWER_HIGH;
}

/* ================================================================
 * Central TX Authorization Gate
 *
 * This is the critical path.  Every TX attempt calls this function.
 * Returns false (= TX blocked) unless every condition is met.
 *
 * Defense principle: fail-closed.  When in doubt, return false.
 * ================================================================ */
bool FRS_IsTxAllowed(uint32_t tx_freq, const VFO_Info_t *vfo)
{
    /* Null pointer → block TX */
    if (!vfo)
        return false;

    const uint8_t ch = vfo->CHANNEL_SAVE;

    /* 1. Channel must be a valid FRS memory channel (0–21) */
    if (!FRS_IsValidChannel(ch))
        return false;

    /* 2. TX frequency must EXACTLY match the FRS channel table.
     *    Even a 1-unit (10 Hz) deviation is rejected. */
    if (tx_freq != frs_channel_table[ch])
        return false;

    /* 3. RX frequency must equal TX frequency — no simplex split,
     *    no cross-band, no repeater input/output pairing. */
    if (vfo->freq_config_RX.Frequency != tx_freq)
        return false;

    /* 4. No TX offset of any kind */
    if (vfo->TX_OFFSET_FREQUENCY_DIRECTION != TX_OFFSET_FREQUENCY_DIRECTION_OFF)
        return false;

    /* 5. TX offset value must also be zero (belt-and-suspenders) */
    if (vfo->TX_OFFSET_FREQUENCY != 0u)
        return false;

    /* 6. No frequency-reverse mode (swaps TX/RX pointers) */
    if (vfo->FrequencyReverse)
        return false;

    /* 7. Modulation must be FM — FRS is analog narrowband FM only */
    if (vfo->Modulation != MODULATION_FM)
        return false;

    /* 8. Bandwidth must be narrow (12.5 kHz) */
    if (vfo->CHANNEL_BANDWIDTH != FRS_REQUIRED_BANDWIDTH)
        return false;

    /* 9. Power must not exceed the legal maximum for this channel group */
    if (vfo->OUTPUT_POWER > FRS_GetMaxPower(ch))
        return false;

    /* 10. Scrambler (voice inversion) must be off — §95.589 prohibits
     *     encryption or devices that conceal the meaning of messages */
    if (vfo->SCRAMBLING_TYPE != 0u)
        return false;

    return true;
}

/* ================================================================
 * EEPROM Channel Sanitization
 *
 * Per-channel EEPROM layout (16 bytes at base = ch * 16):
 *
 *  EEPROM_ReadBuffer(base, &info, 8):
 *    bytes 0–3 : RX Frequency   (uint32_t, 10 Hz units)
 *    bytes 4–7 : TX Offset      (uint32_t, 10 Hz units)
 *
 *  EEPROM_ReadBuffer(base+8, data, 8):
 *    [0] = RX code value
 *    [1] = TX code value
 *    [2] = code types [bits 3:0]=RX type, [bits 7:4]=TX type
 *    [3] = [bits 3:0]=TX_OFFSET_DIRECTION, [bits 7:4]=Modulation
 *    [4] = [bit 0]=FreqReverse, [bit 1]=Bandwidth, [bits 3:2]=Power, [bit 4]=BCL
 *    [5] = DTMF settings
 *    [6] = Step setting
 *    [7] = Scrambler type
 * ================================================================ */
void FRS_SanitizeChannelEEPROM(uint8_t ch)
{
    if (!FRS_IsValidChannel(ch))
        return;

    const uint16_t base = (uint16_t)ch * 16u;

    /* --- Block 1: frequency + TX offset (EEPROM bytes 0–7) ------- */
    struct {
        uint32_t frequency;
        uint32_t tx_offset;
    } __attribute__((packed)) freq_block;

    freq_block.frequency = frs_channel_table[ch];
    freq_block.tx_offset = 0u;
    EEPROM_WriteBuffer(base, &freq_block);

    /* --- Block 2: channel settings (EEPROM bytes 8–15) ----------- */
    uint8_t s[8];
    EEPROM_ReadBuffer(base + 8u, s, sizeof(s));

    /* Validate code type nibbles */
    uint8_t rx_type = s[2] & 0x0Fu;
    uint8_t tx_type = (s[2] >> 4) & 0x0Fu;
    if (rx_type > CODE_TYPE_REVERSE_DIGITAL)  rx_type = CODE_TYPE_OFF;
    if (tx_type > CODE_TYPE_REVERSE_DIGITAL)  tx_type = CODE_TYPE_OFF;

    /* Bounds-check CTCSS/DCS code indexes */
    if (rx_type == CODE_TYPE_CONTINUOUS_TONE && s[0] >= 50u)
        s[0] = 0u;
    else if ((rx_type == CODE_TYPE_DIGITAL || rx_type == CODE_TYPE_REVERSE_DIGITAL)
             && s[0] >= 104u)
        s[0] = 0u;

    if (tx_type == CODE_TYPE_CONTINUOUS_TONE && s[1] >= 50u)
        s[1] = 0u;
    else if ((tx_type == CODE_TYPE_DIGITAL || tx_type == CODE_TYPE_REVERSE_DIGITAL)
             && s[1] >= 104u)
        s[1] = 0u;

    s[2] = (uint8_t)(rx_type | (tx_type << 4));

    /* Force TX offset direction = OFF, modulation = FM */
    s[3] = (uint8_t)(TX_OFFSET_FREQUENCY_DIRECTION_OFF | ((uint8_t)MODULATION_FM << 4));

    /* Sanitize flags byte: clear freq-reverse, force narrow BW,
     * cap power, preserve BCL */
    {
        uint8_t power = (s[4] >> 2) & 3u;
        uint8_t bcl   = (s[4] >> 4) & 1u;
        if (power > FRS_GetMaxPower(ch))
            power = OUTPUT_POWER_LOW;
        s[4] = (uint8_t)((0u << 0)                              /* FreqReverse = off  */
                       | ((uint8_t)BK4819_FILTER_BW_NARROW << 1) /* Bandwidth = narrow */
                       | (power << 2)                            /* legal power        */
                       | (bcl   << 4));                          /* BCL preserved      */
    }

    s[5] = 0u;                      /* DTMF PTT-ID = off  */
    s[6] = (uint8_t)STEP_12_5kHz;  /* step = 12.5 kHz    */
    s[7] = 0u;                      /* scrambler = off    */

    EEPROM_WriteBuffer(base + 8u, s);
}

/* ================================================================
 * Boot Validation
 *
 * Called once from SETTINGS_InitEEPROM() before the main loop.
 * Ensures every FRS channel is in a legal state regardless of what
 * is stored in EEPROM.  Corrupt EEPROM fails closed: the radio
 * remains operational but TX-safe.
 * ================================================================ */
void FRS_BootValidation(void)
{
    /* Repair every FRS channel EEPROM block */
    for (uint8_t ch = 0u; ch < FRS_NUM_CHANNELS; ch++)
        FRS_SanitizeChannelEEPROM(ch);

    /* FRS appliance is always in memory-channel mode — no VFO */
    gEeprom.VFO_OPEN = false;

    /* FRS is simplex only: no cross-band */
    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;

    /* Clamp screen/channel pointers to the valid FRS range */
    if (gEeprom.ScreenChannel[0] >= FRS_NUM_CHANNELS)
        gEeprom.ScreenChannel[0] = 0u;
    if (gEeprom.ScreenChannel[1] >= FRS_NUM_CHANNELS)
        gEeprom.ScreenChannel[1] = 0u;
    if (gEeprom.MrChannel[0] >= FRS_NUM_CHANNELS)
        gEeprom.MrChannel[0] = 0u;
    if (gEeprom.MrChannel[1] >= FRS_NUM_CHANNELS)
        gEeprom.MrChannel[1] = 0u;

    /* Persist the corrected global settings */
    SETTINGS_SaveSettings();
}

/* ================================================================
 * Runtime Self-Test
 *
 * Called from main.c during boot before the radio enters service.
 * Spins forever (radio unusable) if any invariant fails — this is
 * intentional: a failed self-test indicates a corrupted build or
 * memory fault that could allow non-FRS TX.
 * ================================================================ */
void FRS_RunSelfTest(void)
{
    /* 1. Channel count */
    while (FRS_NUM_CHANNELS != 22u) { /* spin */ }

    /* 2. Every channel frequency is in the 462/467 MHz FRS bands */
    for (uint8_t i = 0u; i < FRS_NUM_CHANNELS; i++) {
        const uint32_t f = frs_channel_table[i];
        /* Ch 1–7 and 15–22: 462.500–462.750 MHz (46250000–46275000) */
        /* Ch 8–14:          467.500–467.750 MHz (46750000–46775000) */
        while (!((f >= 46250000u && f <= 46275000u) ||
                 (f >= 46750000u && f <= 46775000u))) { /* spin */ }
    }

    /* 3. Power limits: ch 8–14 must be LOW only */
    for (uint8_t i = 7u; i <= 13u; i++)
        while (FRS_GetMaxPower(i) != OUTPUT_POWER_LOW) { /* spin */ }

    /* 4. Power limits: ch 1–7 and 15–22 may use HIGH */
    for (uint8_t i = 0u; i < 7u; i++)
        while (FRS_GetMaxPower(i) != OUTPUT_POWER_HIGH) { /* spin */ }
    for (uint8_t i = 14u; i < FRS_NUM_CHANNELS; i++)
        while (FRS_GetMaxPower(i) != OUTPUT_POWER_HIGH) { /* spin */ }

    /* 5. TX gate rejects a channel index out of range */
    {
        VFO_Info_t bad;
        memset(&bad, 0, sizeof(bad));
        bad.CHANNEL_SAVE              = FRS_NUM_CHANNELS;   /* out of range */
        bad.freq_config_RX.Frequency  = frs_channel_table[0];
        bad.freq_config_TX.Frequency  = frs_channel_table[0];
        bad.pRX                       = &bad.freq_config_RX;
        bad.pTX                       = &bad.freq_config_TX;
        bad.CHANNEL_BANDWIDTH         = BK4819_FILTER_BW_NARROW;
        bad.Modulation                = MODULATION_FM;
        bad.OUTPUT_POWER              = OUTPUT_POWER_LOW;
        while (FRS_IsTxAllowed(frs_channel_table[0], &bad)) { /* spin — must be false */ }
    }

    /* 6. TX gate accepts a fully valid FRS channel 0 */
    {
        VFO_Info_t good;
        memset(&good, 0, sizeof(good));
        good.CHANNEL_SAVE             = 0u;
        good.freq_config_RX.Frequency = frs_channel_table[0];
        good.freq_config_TX.Frequency = frs_channel_table[0];
        good.pRX                      = &good.freq_config_RX;
        good.pTX                      = &good.freq_config_TX;
        good.CHANNEL_BANDWIDTH        = BK4819_FILTER_BW_NARROW;
        good.Modulation               = MODULATION_FM;
        good.OUTPUT_POWER             = OUTPUT_POWER_LOW;
        while (!FRS_IsTxAllowed(frs_channel_table[0], &good)) { /* spin — must be true */ }
    }

    /* 7. TX gate rejects channels 8–14 at OUTPUT_POWER_HIGH */
    for (uint8_t i = 7u; i <= 13u; i++) {
        VFO_Info_t bad;
        memset(&bad, 0, sizeof(bad));
        bad.CHANNEL_SAVE              = i;
        bad.freq_config_RX.Frequency  = frs_channel_table[i];
        bad.freq_config_TX.Frequency  = frs_channel_table[i];
        bad.pRX                       = &bad.freq_config_RX;
        bad.pTX                       = &bad.freq_config_TX;
        bad.CHANNEL_BANDWIDTH         = BK4819_FILTER_BW_NARROW;
        bad.Modulation                = MODULATION_FM;
        bad.OUTPUT_POWER              = OUTPUT_POWER_HIGH;  /* illegal for ch 8–14 */
        while (FRS_IsTxAllowed(frs_channel_table[i], &bad)) { /* spin — must be false */ }
    }

    /* 8. TX gate rejects a non-FRS frequency even on a valid channel slot */
    {
        VFO_Info_t bad;
        memset(&bad, 0, sizeof(bad));
        bad.CHANNEL_SAVE              = 0u;
        bad.freq_config_RX.Frequency  = 43300000u;  /* 433 MHz — not FRS */
        bad.freq_config_TX.Frequency  = 43300000u;
        bad.pRX                       = &bad.freq_config_RX;
        bad.pTX                       = &bad.freq_config_TX;
        bad.CHANNEL_BANDWIDTH         = BK4819_FILTER_BW_NARROW;
        bad.Modulation                = MODULATION_FM;
        bad.OUTPUT_POWER              = OUTPUT_POWER_LOW;
        while (FRS_IsTxAllowed(43300000u, &bad)) { /* spin — must be false */ }
    }

    /* 9. TX gate rejects wideband on any valid FRS channel */
    {
        VFO_Info_t bad;
        memset(&bad, 0, sizeof(bad));
        bad.CHANNEL_SAVE              = 0u;
        bad.freq_config_RX.Frequency  = frs_channel_table[0];
        bad.freq_config_TX.Frequency  = frs_channel_table[0];
        bad.pRX                       = &bad.freq_config_RX;
        bad.pTX                       = &bad.freq_config_TX;
        bad.CHANNEL_BANDWIDTH         = BK4819_FILTER_BW_WIDE;  /* illegal */
        bad.Modulation                = MODULATION_FM;
        bad.OUTPUT_POWER              = OUTPUT_POWER_LOW;
        while (FRS_IsTxAllowed(frs_channel_table[0], &bad)) { /* spin — must be false */ }
    }

    /* 10. TX gate rejects scrambler enabled */
    {
        VFO_Info_t bad;
        memset(&bad, 0, sizeof(bad));
        bad.CHANNEL_SAVE              = 0u;
        bad.freq_config_RX.Frequency  = frs_channel_table[0];
        bad.freq_config_TX.Frequency  = frs_channel_table[0];
        bad.pRX                       = &bad.freq_config_RX;
        bad.pTX                       = &bad.freq_config_TX;
        bad.CHANNEL_BANDWIDTH         = BK4819_FILTER_BW_NARROW;
        bad.Modulation                = MODULATION_FM;
        bad.OUTPUT_POWER              = OUTPUT_POWER_LOW;
        bad.SCRAMBLING_TYPE           = 1u;  /* scrambler on — illegal */
        while (FRS_IsTxAllowed(frs_channel_table[0], &bad)) { /* spin — must be false */ }
    }
}

#endif /* FRS_APPLIANCE_BUILD */
