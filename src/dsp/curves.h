#ifndef RETRO_CURVES_H
#define RETRO_CURVES_H
/* Recovered Model::Scale_Fit3 and Model::Scale_Log, with float operation order. */
float retro_scale_fit3(float x, float low, float high, float middle);
float retro_scale_log(float x, float low, float high);
/* Adapter utility for the study's physical Hz controls. */
float retro_log_normalize(float value, float low, float high);
#endif
