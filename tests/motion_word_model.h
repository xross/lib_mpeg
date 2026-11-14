// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#ifndef MOTION_WORD_MODEL_H_
#define MOTION_WORD_MODEL_H_

/* Exercise the portable motion loop in big-endian word order on a little-endian
 * host. These bytewise hooks change representation only, not the prediction. */
#define PLM_WORD_ACCESS
#define PLM_WORD_LITTLE_ENDIAN 0
static inline uint32_t plm_load_word(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static inline void plm_store_word(uint8_t *p, uint32_t word)
{
    p[0] = (uint8_t)(word >> 24);
    p[1] = (uint8_t)(word >> 16);
    p[2] = (uint8_t)(word >> 8);
    p[3] = (uint8_t)word;
}
static inline uint32_t plm_load_stored_word(const uint8_t *p)
{
    return plm_load_word(p);
}
#endif
