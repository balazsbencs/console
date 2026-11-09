#include <clap/clap.h>
#include "wobble.h"
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

enum { PARAM_COUNT = 10, FRAMES = 4096, MAX_BLOCK = 512 };
static const float defaults[PARAM_COUNT] = {.5f, .5f, .2f, .294f, 0, 1, 0, 0, 1, 0};
static const float settings[PARAM_COUNT] = {.83f, .61f, .37f, .79f, .27f, .72f, -.31f, .48f, 1, 0};
static const void *CLAP_ABI host_extension(const clap_host_t *host, const char *id) {
    (void)host; (void)id; return NULL;
}
static void CLAP_ABI request(const clap_host_t *host) { (void)host; }
static const clap_host_t host = {
    .clap_version = CLAP_VERSION_INIT, .name = "Wobble ABI test", .vendor = "Local", .url = "", .version = "1",
    .get_extension = host_extension, .request_restart = request, .request_process = request, .request_callback = request,
};
typedef struct { clap_event_param_value_t events[16]; unsigned count; } events;
static uint32_t CLAP_ABI event_count(const clap_input_events_t *in) { return ((events *)in->ctx)->count; }
static const clap_event_header_t *CLAP_ABI event_get(const clap_input_events_t *in, uint32_t i) {
    const events *e = in->ctx; return i < e->count ? &e->events[i].header : NULL;
}
static clap_event_param_value_t make_event(unsigned index, double value, uint32_t time) {
    return (clap_event_param_value_t){
        .header = {.size = sizeof(clap_event_param_value_t), .time = time,
            .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = CLAP_EVENT_PARAM_VALUE},
        .param_id = 2000 + index, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = value};
}
static void set_values(const clap_plugin_t *p, const float values[PARAM_COUNT]) {
    events e = {.count = PARAM_COUNT};
    for (unsigned i = 0; i < PARAM_COUNT; ++i) e.events[i] = make_event(i, values[i], 0);
    clap_input_events_t in = {.ctx = &e, .size = event_count, .get = event_get};
    const clap_plugin_params_t *params = p->get_extension(p, CLAP_EXT_PARAMS);
    params->flush(p, &in, NULL);
}
static void check_values(const clap_plugin_t *p, const float values[PARAM_COUNT]) {
    const clap_plugin_params_t *params = p->get_extension(p, CLAP_EXT_PARAMS);
    for (unsigned i = 0; i < PARAM_COUNT; ++i) {
        double value; assert(params->get_value(p, 2000 + i, &value) && value == (double)values[i]);
    }
}
static retro_wobble_params core_params(const float *v) {
    return (retro_wobble_params){.wow_depth = v[0], .flutter_depth = v[1], .wow_rate = v[2],
        .flutter_rate = v[3], .stereo_phase = v[4], .mix = v[5], .lfo_noise = v[6], .noise_scale = v[7], .enable = v[8]};
}
typedef struct { unsigned char data[256]; size_t size, cursor; } memory;
static int64_t CLAP_ABI write_state(const clap_ostream_t *stream, const void *data, uint64_t size) {
    memory *m = stream->ctx; size_t count = size > 7 ? 7 : (size_t)size;
    if (count > sizeof(m->data) - m->size) return -1;
    memcpy(m->data + m->size, data, count); m->size += count; return (int64_t)count;
}
static int64_t CLAP_ABI read_state(const clap_istream_t *stream, void *data, uint64_t size) {
    memory *m = stream->ctx; size_t count = size > 3 ? 3 : (size_t)size;
    if (count > m->size - m->cursor) count = m->size - m->cursor;
    memcpy(data, m->data + m->cursor, count); m->cursor += count; return (int64_t)count;
}
static void check_state(const clap_plugin_t *p) {
    const clap_plugin_state_t *state = p->get_extension(p, CLAP_EXT_STATE); assert(state);
    set_values(p, settings);
    memory m = {0}; clap_ostream_t out = {.ctx = &m, .write = write_state};
    clap_istream_t in = {.ctx = &m, .read = read_state};
    assert(state->save(p, &out)); assert(m.size == 12 + PARAM_COUNT * 8);
    assert(!memcmp(m.data, "RSWB", 4));
    set_values(p, defaults);
    --m.size; assert(!state->load(p, &in)); check_values(p, defaults);
    ++m.size; m.cursor = 0; assert(state->load(p, &in)); check_values(p, settings);
    /* A malformed record also rejects the complete state without partial publication. */
    set_values(p, defaults); m.cursor = 0; m.data[m.size - 8] ^= 1;
    assert(!state->load(p, &in)); check_values(p, defaults);
    m.data[m.size - 8] ^= 1; m.cursor = 0;
    assert(state->load(p, &in)); check_values(p, settings);
}
static void core_range(retro_wobble *core, const float *values, float input[2][FRAMES],
                       float output[2][FRAMES], unsigned first, unsigned last) {
    if (values[9] != 0) {
        for (unsigned c = 0; c < 2; ++c)
            memcpy(output[c] + first, input[c] + first, (last - first) * sizeof(float));
    } else {
        retro_wobble_process(core, input[0] + first, input[1] + first,
            output[0] + first, output[1] + first, last - first);
    }
}
static void check_render(const clap_plugin_t *p, float rate) {
    float input[2][FRAMES], expected[2][FRAMES], output[2][FRAMES], previous[2][FRAMES];
    for (unsigned i = 0; i < FRAMES; ++i) {
        input[0][i] = .7f * (float)sin(i * .071); input[1][i] = .4f * (float)cos(i * .093);
    }
    /* Several controls change at one offset; enable and bypass exercise different histories. */
    const unsigned time[] = {7, 7, 7, 129, 237, 333, 389, 511};
    const unsigned index[] = {0, 2, 6, 8, 8, 9, 9, 5};
    const float value[] = {.24f, .86f, .63f, 0, 1, 1, 0, .19f};
    enum { CHANGES = sizeof(time) / sizeof(time[0]) };
    for (unsigned pass = 0; pass < 2; ++pass) {
        set_values(p, settings);
        assert(p->activate(p, rate, 1, MAX_BLOCK)); assert(p->start_processing(p));
        /* Populate delay and smoothing history so reset must clear real audio state. */
        float *warm_in[] = {input[0], input[1]}, *warm_out[] = {output[0], output[1]};
        clap_audio_buffer_t warm_input = {.data32 = warm_in, .channel_count = 2};
        clap_audio_buffer_t warm_output = {.data32 = warm_out, .channel_count = 2};
        clap_process_t warmup = {.frames_count = MAX_BLOCK, .audio_inputs = &warm_input,
            .audio_outputs = &warm_output, .audio_inputs_count = 1, .audio_outputs_count = 1};
        assert(p->process(p, &warmup) != CLAP_PROCESS_ERROR);
        p->reset(p);
        retro_wobble *core = retro_wobble_create(); assert(core);
        float current[PARAM_COUNT]; memcpy(current, settings, sizeof(current));
        retro_wobble_params cp = core_params(current);
        assert(retro_wobble_set(core, &cp) && retro_wobble_prepare(core, rate));
        retro_wobble_reset(core);
        unsigned first = 0, change = 0;
        while (change < CHANGES) {
            unsigned last = time[change]; core_range(core, current, input, expected, first, last);
            do { current[index[change]] = value[change]; ++change; }
            while (change < CHANGES && time[change] == last);
            cp = core_params(current); assert(retro_wobble_set(core, &cp)); first = last;
        }
        core_range(core, current, input, expected, first, FRAMES);
        retro_wobble_destroy(core);
        if (pass) memcpy(output, input, sizeof(output));
        const unsigned lengths[] = {1, 7, 63, 65, 127, 511};
        unsigned offset = 0, block = 0;
        while (offset < FRAMES) {
            unsigned size = pass ? lengths[block++ % 6] : MAX_BLOCK;
            if (size > FRAMES - offset) size = FRAMES - offset;
            events e = {0};
            for (unsigned i = 0; i < CHANGES; ++i)
                if (time[i] >= offset && time[i] < offset + size)
                    e.events[e.count++] = make_event(index[i], value[i], time[i] - offset);
            clap_input_events_t in_events = {.ctx = &e, .size = event_count, .get = event_get};
            float *ins[] = {pass ? output[0] + offset : input[0] + offset,
                           pass ? output[1] + offset : input[1] + offset};
            float *outs[] = {output[0] + offset, output[1] + offset};
            clap_audio_buffer_t audio_in = {.data32 = ins, .channel_count = 2};
            clap_audio_buffer_t audio_out = {.data32 = outs, .channel_count = 2};
            clap_process_t process = {.frames_count = size, .audio_inputs = &audio_in, .audio_outputs = &audio_out,
                .audio_inputs_count = 1, .audio_outputs_count = 1, .in_events = &in_events};
#if defined(__SSE__)
            unsigned mxcsr = _mm_getcsr();
#endif
            assert(p->process(p, &process) != CLAP_PROCESS_ERROR);
#if defined(__SSE__)
            assert(_mm_getcsr() == mxcsr);
#endif
            offset += size;
        }
        assert(memcmp(output, expected, sizeof(output)) == 0);
        assert(memcmp(output, input, sizeof(output)) != 0);
        if (pass) assert(memcmp(output, previous, sizeof(output)) == 0);
        else memcpy(previous, output, sizeof(output));
        p->stop_processing(p); p->deactivate(p);
    }
}
int main(int argc, char **argv) {
    assert(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    const clap_plugin_entry_t *entry = dlsym(library, "clap_entry"); assert(entry && entry->init(argv[1]));
    const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    assert(factory && factory->get_plugin_count(factory) == 1);
    const clap_plugin_descriptor_t *descriptor = factory->get_plugin_descriptor(factory, 0);
    assert(descriptor && !strcmp(descriptor->id, "local.retro-study.wobble"));
    assert(!factory->get_plugin_descriptor(factory, 1));
    assert(!factory->create_plugin(factory, &host, "invalid.id"));
    const clap_plugin_t *p = factory->create_plugin(factory, &host, descriptor->id);
    assert(p && p->init(p)); assert(!p->get_extension(p, "invalid.extension"));
    const clap_plugin_params_t *params = p->get_extension(p, CLAP_EXT_PARAMS);
    const clap_plugin_audio_ports_t *ports = p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    assert(params && params->count(p) == PARAM_COUNT && ports);
    for (unsigned direction = 0; direction < 2; ++direction) {
        clap_audio_port_info_t info;
        assert(ports->count(p, direction != 0) == 1);
        assert(ports->get(p, 0, direction != 0, &info));
        assert(info.channel_count == 2 && !strcmp(info.port_type, CLAP_PORT_STEREO));
        assert(info.in_place_pair == 0 && (info.flags & CLAP_AUDIO_PORT_IS_MAIN));
        assert(!ports->get(p, 1, direction != 0, &info));
    }
    for (unsigned i = 0; i < PARAM_COUNT; ++i) {
        clap_param_info_t info; char text[128]; double parsed;
        assert(params->get_info(p, i, &info) && info.id == 2000 + i);
        assert(info.default_value == (double)defaults[i]);
        assert(params->value_to_text(p, info.id, info.default_value, text, sizeof(text)));
        assert(params->text_to_value(p, info.id, text, &parsed));
        assert(fabs(parsed - info.default_value) < 1e-7);
    }
    check_values(p, defaults); check_state(p);
    assert(!p->activate(p, 999, 1, MAX_BLOCK));
    assert(!p->activate(p, 1536001, 1, MAX_BLOCK));
    const float rates[] = {48000, 96000, 1000, 384000, 44100};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) check_render(p, rates[i]);
    p->destroy(p); entry->deinit(); assert(dlclose(library) == 0);
    puts("Wobble CLAP ABI, state, reset, in-place partitions and sample-offset automation passed");
    return 0;
}
