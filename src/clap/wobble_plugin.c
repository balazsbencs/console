#include <clap/clap.h>
#include "wobble.h"
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Audio parameter publication requires lock-free integers");
_Static_assert(sizeof(float) == 4, "State format requires 32-bit floats");

enum { WOW_DEPTH, FLUTTER_DEPTH, WOW_RATE, FLUTTER_RATE, STEREO_PHASE, MIX,
       LFO_NOISE, NOISE_SCALE, ENABLE, BYPASS, PARAM_COUNT };
typedef struct { const char *name; float low, high, initial; } parameter;
static const parameter parameters[PARAM_COUNT] = {
    {"Wow Depth", 0, 1, 0.5f}, {"Flutter Depth", 0, 1, 0.5f},
    {"Wow Rate", 0, 1, 0.2f}, {"Flutter Rate", 0, 1, 0.294f},
    {"Stereo Phase", 0, 0.4f, 0}, {"Mix", 0, 1, 1},
    {"LFO Noise", -1, 1, 0}, {"Noise Scale", 0, 1, 0},
    {"Enable", 0, 1, 1}, {"Bypass", 0, 1, 0},
};
static const char *const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_CHORUS, CLAP_PLUGIN_FEATURE_STEREO, NULL};
static const clap_plugin_descriptor_t descriptor = {
    .clap_version = CLAP_VERSION_INIT, .id = "local.retro-study.wobble",
    .name = "Retro Study Wobble", .vendor = "Retro Study", .url = "",
    .manual_url = "", .support_url = "", .version = "0.1.0",
    .description = "Recovered Wobble kernel with direct controls. Original macro and Flux mapping pending.",
    .features = features,
};
typedef struct {
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_params_t *host_params;
    atomic_uint values[PARAM_COUNT];
    float applied[PARAM_COUNT];
    retro_wobble *dsp;
    bool active, processing;
    uint32_t max_frames;
} instance;

static instance *self(const clap_plugin_t *plugin) { return plugin->plugin_data; }
static unsigned float_bits(float value) { unsigned bits; memcpy(&bits, &value, 4); return bits; }
static float bits_float(unsigned bits) { float value; memcpy(&value, &bits, 4); return value; }
static float value_at(const instance *s, unsigned index) {
    return bits_float(atomic_load_explicit(&s->values[index], memory_order_relaxed));
}
static bool stepped(unsigned i) { return i == BYPASS || i == ENABLE; }
static bool valid_value(unsigned i, double value) {
    return i < PARAM_COUNT && isfinite(value) && value >= parameters[i].low && value <= parameters[i].high
        && (!stepped(i) || value == floor(value));
}
static int index_for(clap_id id) { return id >= 2000 && id < 2000 + PARAM_COUNT ? (int)(id - 2000) : -1; }
static void apply_dsp_values(instance *s) {
    const float *v = s->applied;
    retro_wobble_params p = {.wow_depth = v[WOW_DEPTH], .flutter_depth = v[FLUTTER_DEPTH],
        .wow_rate = v[WOW_RATE], .flutter_rate = v[FLUTTER_RATE], .stereo_phase = v[STEREO_PHASE],
        .mix = v[MIX], .lfo_noise = v[LFO_NOISE], .noise_scale = v[NOISE_SCALE], .enable = v[ENABLE]};
    (void)retro_wobble_set(s->dsp, &p);
}
static void apply_values(instance *s) {
    float v[PARAM_COUNT];
    for (unsigned i = 0; i < PARAM_COUNT; ++i) v[i] = value_at(s, i);
    if (!memcmp(v, s->applied, sizeof(v))) return;
    memcpy(s->applied, v, sizeof(v));
    apply_dsp_values(s);
}
static bool CLAP_ABI plugin_init(const clap_plugin_t *plugin) {
    instance *s = self(plugin);
    s->host_params = s->host->get_extension(s->host, CLAP_EXT_PARAMS);
    return true;
}
static void CLAP_ABI destroy(const clap_plugin_t *plugin) {
    instance *s = self(plugin); retro_wobble_destroy(s->dsp); free(s);
}
static bool CLAP_ABI activate(const clap_plugin_t *plugin, double rate, uint32_t min_frames, uint32_t max_frames) {
    instance *s = self(plugin);
    if (s->active || !isfinite(rate) || rate < 1000 || rate > 1536000 ||
        min_frames < 1 || min_frames > max_frames || max_frames > INT32_MAX) return false;
    apply_values(s); apply_dsp_values(s);
    if (!retro_wobble_prepare(s->dsp, (float)rate)) return false;
    s->active = true; s->max_frames = max_frames;
    return true;
}
static void CLAP_ABI deactivate(const clap_plugin_t *plugin) { self(plugin)->active = false; }
static bool CLAP_ABI start(const clap_plugin_t *plugin) {
    instance *s = self(plugin);
    if (!s->active || s->processing) return false;
    s->processing = true; return true;
}
static void CLAP_ABI stop(const clap_plugin_t *plugin) { self(plugin)->processing = false; }
static void CLAP_ABI reset(const clap_plugin_t *plugin) {
    instance *s = self(plugin); apply_values(s); apply_dsp_values(s); retro_wobble_reset(s->dsp);
}
static void accept_event(instance *s, const clap_event_header_t *header) {
    if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE ||
        header->size < sizeof(clap_event_param_value_t)) return;
    const clap_event_param_value_t *event = (const clap_event_param_value_t *)header;
    int i = index_for(event->param_id);
    if (i < 0 || event->note_id != -1 || event->port_index != -1 || event->channel != -1 || event->key != -1 ||
        !valid_value((unsigned)i, event->value)) return;
    atomic_store_explicit(&s->values[i], float_bits((float)event->value), memory_order_relaxed);
}
static void process_range(instance *s, const clap_process_t *p, uint32_t first, uint32_t last) {
    if (first == last) return;
    float **in = p->audio_inputs[0].data32, **out = p->audio_outputs[0].data32;
    if (s->applied[BYPASS] >= 0.5f) {
        for (unsigned c = 0; c < 2; ++c)
            if (in[c] != out[c]) memmove(out[c] + first, in[c] + first, (last - first) * sizeof(float));
        return;
    }
    retro_wobble_process(s->dsp, in[0] + first, in[1] + first,
                         out[0] + first, out[1] + first, last - first);
}
static clap_process_status CLAP_ABI process(const clap_plugin_t *plugin, const clap_process_t *p) {
    instance *s = self(plugin);
    if (!s->processing || !p || p->frames_count > s->max_frames || p->audio_inputs_count != 1 ||
        p->audio_outputs_count != 1 || !p->audio_inputs || !p->audio_outputs ||
        p->audio_inputs[0].channel_count != 2 || p->audio_outputs[0].channel_count != 2 ||
        !p->audio_inputs[0].data32 || !p->audio_outputs[0].data32 ||
        !p->audio_inputs[0].data32[0] || !p->audio_inputs[0].data32[1] ||
        !p->audio_outputs[0].data32[0] || !p->audio_outputs[0].data32[1]) return CLAP_PROCESS_ERROR;
    /* Keep subnormal buffers from causing realtime CPU spikes. Restore the
       host's floating-point control state before returning. */
#if defined(__SSE__)
    unsigned previous_mxcsr = _mm_getcsr();
    _mm_setcsr(previous_mxcsr | 0x8040u);
#endif
    apply_values(s);
    uint32_t cursor = 0, count = p->in_events ? p->in_events->size(p->in_events) : 0;
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t *e = p->in_events->get(p->in_events, i);
        if (!e || e->time < cursor || e->time > p->frames_count) continue;
        if (e->time > cursor) {
            apply_values(s); process_range(s, p, cursor, e->time); cursor = e->time;
        }
        accept_event(s, e);
    }
    apply_values(s);
    process_range(s, p, cursor, p->frames_count);
    p->audio_outputs[0].constant_mask = 0;
#if defined(__SSE__)
    _mm_setcsr(previous_mxcsr);
#endif
    return CLAP_PROCESS_CONTINUE;
}
static uint32_t CLAP_ABI port_count(const clap_plugin_t *p, bool input) { (void)p; (void)input; return 1; }
static bool CLAP_ABI port_info(const clap_plugin_t *p, uint32_t index, bool input, clap_audio_port_info_t *info) {
    (void)p;
    if (index != 0 || !info) return false;
    memset(info, 0, sizeof(*info));
    info->id = 0; info->channel_count = 2; info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO; info->in_place_pair = 0;
    snprintf(info->name, sizeof(info->name), "%s", input ? "Stereo In" : "Stereo Out");
    return true;
}
static uint32_t CLAP_ABI param_count(const clap_plugin_t *p) { (void)p; return PARAM_COUNT; }
static bool CLAP_ABI param_info(const clap_plugin_t *p, uint32_t index, clap_param_info_t *info) {
    (void)p;
    if (index >= PARAM_COUNT || !info) return false;
    memset(info, 0, sizeof(*info)); info->id = 2000 + index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (stepped(index)) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (index == BYPASS) info->flags |= CLAP_PARAM_IS_BYPASS;
    info->min_value = parameters[index].low; info->max_value = parameters[index].high;
    info->default_value = parameters[index].initial;
    snprintf(info->name, sizeof(info->name), "%s", parameters[index].name);
    snprintf(info->module, sizeof(info->module), "%s", "Wobble");
    return true;
}
static bool CLAP_ABI param_value(const clap_plugin_t *p, clap_id id, double *value) {
    int i = index_for(id); if (i < 0 || !value) return false;
    *value = value_at(self(p), (unsigned)i); return true;
}
static bool CLAP_ABI param_text(const clap_plugin_t *p, clap_id id, double value, char *text, uint32_t capacity) {
    (void)p; int i = index_for(id);
    if (i < 0 || !text || capacity == 0 || !isfinite(value) ||
        value < parameters[i].low || value > parameters[i].high) return false;
    int n = snprintf(text, capacity, "%.9g", value);
    return n >= 0 && (uint32_t)n < capacity;
}
static bool CLAP_ABI param_parse(const clap_plugin_t *p, clap_id id, const char *text, double *value) {
    (void)p; int i = index_for(id);
    if (i < 0 || !text || !value) return false;
    char *end; double parsed = strtod(text, &end);
    float rounded = (float)parsed;
    if (end == text || *end || !isfinite(parsed) || !isfinite(rounded) ||
        rounded < parameters[i].low || rounded > parameters[i].high) return false;
    *value = parsed < parameters[i].low ? parameters[i].low :
        parsed > parameters[i].high ? parameters[i].high : parsed;
    return true;
}
static void CLAP_ABI param_flush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out) {
    (void)out; instance *s = self(p);
    if (in) for (uint32_t i = 0, n = in->size(in); i < n; ++i) accept_event(s, in->get(in, i));
    apply_values(s);
}
static void encode32(unsigned char *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = (unsigned char)(value >> (i * 8));
}
static uint32_t decode32(const unsigned char *in) {
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}
enum { STATE_SIZE = 12 + PARAM_COUNT * 8 };
static bool CLAP_ABI save_state(const clap_plugin_t *p, const clap_ostream_t *stream) {
    unsigned char data[STATE_SIZE]; memcpy(data, "RSWB", 4);
    encode32(data + 4, 1); encode32(data + 8, PARAM_COUNT);
    for (unsigned i = 0; i < PARAM_COUNT; ++i) {
        encode32(data + 12 + i * 8, 2000 + i);
        encode32(data + 16 + i * 8, float_bits(value_at(self(p), i)));
    }
    size_t offset = 0;
    while (offset < sizeof(data)) {
        int64_t n = stream->write(stream, data + offset, sizeof(data) - offset);
        if (n <= 0 || (uint64_t)n > sizeof(data) - offset) return false;
        offset += (size_t)n;
    }
    return true;
}
static bool CLAP_ABI load_state(const clap_plugin_t *p, const clap_istream_t *stream) {
    unsigned char data[STATE_SIZE]; size_t offset = 0;
    while (offset < 12) {
        int64_t n = stream->read(stream, data + offset, 12 - offset);
        if (n <= 0 || (uint64_t)n > 12 - offset) return false;
        offset += (size_t)n;
    }
    uint32_t version = decode32(data + 4), count = decode32(data + 8);
    if (memcmp(data, "RSWB", 4) || !(version == 1 && count == PARAM_COUNT)) return false;
    size_t size = 12 + count * 8;
    while (offset < size) {
        int64_t n = stream->read(stream, data + offset, size - offset);
        if (n <= 0 || (uint64_t)n > size - offset) return false;
        offset += (size_t)n;
    }
    float values[PARAM_COUNT];
    for (unsigned i = 0; i < PARAM_COUNT; ++i) values[i] = parameters[i].initial;
    for (unsigned i = 0; i < count; ++i) {
        if (decode32(data + 12 + i * 8) != 2000 + i) return false;
        values[i] = bits_float(decode32(data + 16 + i * 8));
        if (!valid_value(i, values[i])) return false;
    }
    instance *s = self(p);
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
        atomic_store_explicit(&s->values[i], float_bits(values[i]), memory_order_relaxed);
    /* Audio state is touched only by process/flush/activate/reset. */
    if (s->host_params) s->host_params->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}
static uint32_t CLAP_ABI latency(const clap_plugin_t *p) { (void)p; return 0; }
static const clap_plugin_params_t params_ext = {param_count, param_info, param_value, param_text, param_parse, param_flush};
static const clap_plugin_audio_ports_t ports_ext = {port_count, port_info};
static const clap_plugin_state_t state_ext = {save_state, load_state};
static const clap_plugin_latency_t latency_ext = {latency};
static const void *CLAP_ABI extension(const clap_plugin_t *p, const char *id) {
    (void)p;
    if (!strcmp(id, CLAP_EXT_PARAMS)) return &params_ext;
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports_ext;
    if (!strcmp(id, CLAP_EXT_STATE)) return &state_ext;
    if (!strcmp(id, CLAP_EXT_LATENCY)) return &latency_ext;
    return NULL;
}
static void CLAP_ABI on_main(const clap_plugin_t *p) { (void)p; }
static uint32_t CLAP_ABI factory_count(const clap_plugin_factory_t *f) { (void)f; return 1; }
static const clap_plugin_descriptor_t *CLAP_ABI factory_descriptor(const clap_plugin_factory_t *f, uint32_t index) {
    (void)f; return index == 0 ? &descriptor : NULL;
}
static const clap_plugin_t *CLAP_ABI factory_create(const clap_plugin_factory_t *f, const clap_host_t *host, const char *id) {
    (void)f;
    if (!host || !clap_version_is_compatible(host->clap_version) || !id || strcmp(id, descriptor.id)) return NULL;
    instance *s = calloc(1, sizeof(*s)); if (!s) return NULL;
    s->host = host; s->dsp = retro_wobble_create();
    if (!s->dsp) { free(s); return NULL; }
    for (unsigned i = 0; i < PARAM_COUNT; ++i) {
        atomic_init(&s->values[i], float_bits(parameters[i].initial)); s->applied[i] = parameters[i].initial;
    }
    apply_dsp_values(s);
    s->plugin = (clap_plugin_t){.desc = &descriptor, .plugin_data = s, .init = plugin_init,
        .destroy = destroy, .activate = activate, .deactivate = deactivate, .start_processing = start,
        .stop_processing = stop, .reset = reset, .process = process, .get_extension = extension, .on_main_thread = on_main};
    return &s->plugin;
}
static const clap_plugin_factory_t factory = {factory_count, factory_descriptor, factory_create};
static bool CLAP_ABI entry_init(const char *path) { (void)path; return true; }
static void CLAP_ABI entry_deinit(void) {}
static const void *CLAP_ABI get_factory(const char *id) { return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : NULL; }
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entry_init, entry_deinit, get_factory};
