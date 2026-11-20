#ifndef RETRO_STUDY_NXAC_TABLES_H
#define RETRO_STUDY_NXAC_TABLES_H
#include <stdint.h>
/* NXAC v1.1 coefficient decoding, recovered from RC-20 1.0.5 constructor.
   Stateless; call on the loading/decoding thread, not a realtime audio thread. */
float rs_nxac_gain(uint8_t index);
float rs_nxac_delta(uint8_t index);
#endif
