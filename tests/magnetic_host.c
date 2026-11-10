#include <clap/clap.h>
#include "magnetic.h"
#include "magnetic_macro.h"
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

enum { PARAM_COUNT = 24, FRAMES = 4096, MAX_BLOCK = 512 };
static const float defaults[PARAM_COUNT]={1,.5f,.5f,0,0,0,0,1,0,0,.1f,.25f,1,0,
    0,0,.5f,1,0,0,0,1,2,3};
static const float settings[PARAM_COUNT]={.83f,.31f,.37f,.79f,-.61f,-.43f,1,.71f,.66f,1,.18f,.47f,1,0,
    0,.73f,.31f,.68f,.91f,.82f,123,456,789,1011};
static const void *CLAP_ABI host_extension(const clap_host_t *host, const char *id) {
    (void)host; (void)id; return NULL;
}
static void CLAP_ABI request(const clap_host_t *host) { (void)host; }
static const clap_host_t host = {
    .clap_version = CLAP_VERSION_INIT, .name = "Magnetic ABI test", .vendor = "Local", .url = "", .version = "1",
    .get_extension = host_extension, .request_restart = request, .request_process = request, .request_callback = request,
};
typedef struct { clap_event_param_value_t events[32]; unsigned count; } events;
static uint32_t CLAP_ABI event_count(const clap_input_events_t *in) { return ((events *)in->ctx)->count; }
static const clap_event_header_t *CLAP_ABI event_get(const clap_input_events_t *in, uint32_t i) {
    const events *e = in->ctx; return i < e->count ? &e->events[i].header : NULL;
}
static clap_event_param_value_t make_event(unsigned index, double value, uint32_t time) {
    return (clap_event_param_value_t){
        .header = {.size = sizeof(clap_event_param_value_t), .time = time,
            .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = CLAP_EVENT_PARAM_VALUE},
        .param_id = 3000 + index, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = value};
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
        double value; assert(params->get_value(p, 3000 + i, &value) && value == (double)values[i]);
    }
}
static retro_magnetic_params core_params(const float *v) {
    return (retro_magnetic_params){.depth=v[0],.depth_steer=v[1],.rate=v[2],
        .stereo_phase=v[3],.wear_gain=v[4],.wear_pan=v[5],.wear_stereo=v[6],
        .wear_depth=v[7],.dropout_steer=v[8],.dropout_amount=v[9],
        .dropout_min=v[10],.dropout_max=v[11],.enable=v[12]};
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
    assert(!memcmp(m.data, "RSMG", 4));
    set_values(p, defaults);
    --m.size; assert(!state->load(p, &in)); check_values(p, defaults);
    ++m.size; m.cursor = 0; assert(state->load(p, &in)); check_values(p, settings);
    /* A malformed record also rejects the complete state without partial publication. */
    set_values(p, defaults); m.cursor = 0; m.data[m.size - 8] ^= 1;
    assert(!state->load(p, &in)); check_values(p, defaults);
    m.data[m.size - 8] ^= 1; m.cursor = 0;
    assert(state->load(p, &in)); check_values(p, settings);
    /* Version 1 has exactly the original 14 records; new controls default even
       after nondefault macro controls and seeds have been used. */
    m.data[4] = 1; m.data[8] = 14; m.size = 12 + 14 * 8; m.cursor = 0;
    assert(state->load(p, &in));
    float migrated[PARAM_COUNT]; memcpy(migrated, defaults, sizeof(migrated));
    memcpy(migrated, settings, 14 * sizeof(float)); check_values(p, migrated);
}

static void core_range(retro_magnetic *core, const float *values, float input[2][FRAMES],
                       float output[2][FRAMES], unsigned first, unsigned last) {
    for (unsigned c=0;c<2;++c)
        memcpy(output[c]+first,input[c]+first,(last-first)*sizeof(float));
    if (values[13]==0) retro_magnetic_process(core,output[0]+first,output[1]+first,last-first);
}
static void check_render(const clap_plugin_t *p, float rate) {
    float input[2][FRAMES], expected[2][FRAMES], output[2][FRAMES], previous[2][FRAMES];
    for (unsigned i = 0; i < FRAMES; ++i) {
        input[0][i] = .7f * (float)sin(i * .071); input[1][i] = .4f * (float)cos(i * .093);
    }
    /* Several controls change at one offset; enable and bypass exercise different histories. */
    const unsigned time[] = {7, 7, 7, 129, 237, 333, 389, 511};
    const unsigned index[] = {0, 2, 4, 12, 12, 13, 13, 5};
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
        retro_magnetic *core = retro_magnetic_create(); assert(core);
        float current[PARAM_COUNT]; memcpy(current, settings, sizeof(current));
        retro_magnetic_params cp = core_params(current);
        assert(retro_magnetic_set(core, &cp) && retro_magnetic_prepare(core, rate));
        retro_magnetic_reset(core);
        unsigned first = 0, change = 0;
        while (change < CHANGES) {
            unsigned last = time[change]; core_range(core, current, input, expected, first, last);
            do { current[index[change]] = value[change]; ++change; }
            while (change < CHANGES && time[change] == last);
            cp = core_params(current); assert(retro_magnetic_set(core, &cp)); first = last;
        }
        core_range(core, current, input, expected, first, FRAMES);
        retro_magnetic_destroy(core);
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

/* Compose the kernel and four generators with a separate 64-frame control
   clock. Host buffer sizes and event grouping do not drive this reference. */
static void macro_params(retro_magnetic *core, const rs_magnetic_macro *macro, const float *v) {
    retro_magnetic_params cp = core_params(v);
    if (v[14] != 0) rs_magnetic_macro_apply(macro, v[17], v[15], v[16], v[19], &cp);
    assert(retro_magnetic_set(core, &cp));
}
static void reset_generators(rs_magnetic_macro *macro, const float *v) {
    rs_magnetic_macro_reset(macro, (int32_t)v[20], (int32_t)v[21], (int32_t)v[22], (int32_t)v[23]);
}
static void check_macro_render(const clap_plugin_t *plugin, float rate) {
    enum { N = 8192 };
    float input[2][N], expected[2][N], output[2][N], previous[2][N];
    float initial[PARAM_COUNT]; memcpy(initial, settings, sizeof(initial)); initial[14] = 1;
    for (unsigned i = 0; i < N; ++i) {
        input[0][i] = .7f * (float)sin(i * .071); input[1][i] = .4f * (float)cos(i * .093);
    }
    const unsigned times[] = {7,7,269,1151,1539,1797,2049,2561,
        3333,3333,3333,3333,4096,4096,4096,4096,6144};
    const unsigned indices[] = {15,16,12,12,13,13,14,14,
        20,21,22,23,15,16,17,18,19};
    const float changes[] = {.91f,.2f,0,1,1,0,0,1,
        327,119,4095,65535,.6f,.8f,.9f,.3f,.15f};
    enum { CHANGES = sizeof(times) / sizeof(times[0]) };
    for (unsigned pass = 0; pass < 2; ++pass) {
        set_values(plugin, initial);
        assert(plugin->activate(plugin, rate, 1, MAX_BLOCK));
        assert(plugin->start_processing(plugin));
        /* Reset must rewind all four generators as well as audio histories. */
        float *warm_in[] = {input[0], input[1]}, *warm_out[] = {output[0], output[1]};
        clap_audio_buffer_t warm_input = {.data32 = warm_in, .channel_count = 2};
        clap_audio_buffer_t warm_output = {.data32 = warm_out, .channel_count = 2};
        clap_process_t warmup = {.frames_count = MAX_BLOCK, .audio_inputs = &warm_input,
            .audio_outputs = &warm_output, .audio_inputs_count = 1, .audio_outputs_count = 1};
        assert(plugin->process(plugin, &warmup) != CLAP_PROCESS_ERROR); plugin->reset(plugin);
        retro_magnetic *core = retro_magnetic_create(); assert(core);
        rs_magnetic_macro macro = {0}; reset_generators(&macro, initial);
        float v[PARAM_COUNT]; memcpy(v, initial, sizeof(v));
        macro_params(core, &macro, v); assert(retro_magnetic_prepare(core, rate));
        memcpy(expected, input, sizeof(expected));
        unsigned offset = 0, change = 0, remaining = 0, ticks_while_disabled = 0;
        while (offset < N) {
            bool changed = false, seed_changed = false;
            while (change < CHANGES && times[change] == offset) {
                unsigned i = indices[change]; seed_changed |= i >= 20 && v[i] != changes[change];
                v[i] = changes[change++]; changed = true;
            }
            if (seed_changed) { reset_generators(&macro, v); remaining = 0; }
            if (changed) macro_params(core, &macro, v);
            unsigned size = (change < CHANGES ? times[change] : N) - offset;
            if (v[13] == 0) {
                if (v[14] != 0) {
                    if (!remaining) {
                        rs_magnetic_macro_tick(&macro, rate, v[17], v[18], v[15], v[19]);
                        macro_params(core, &macro, v); remaining = 64;
                        if (v[12] == 0) ++ticks_while_disabled;
                    }
                    if (size > remaining) size = remaining;
                }
                retro_magnetic_process(core, expected[0] + offset, expected[1] + offset, size);
                if (v[14] != 0) remaining -= size;
            }
            offset += size;
        }
        assert(ticks_while_disabled > 0);
        retro_magnetic_destroy(core);
        if (pass) memcpy(output, input, sizeof(output));
        const unsigned lengths[] = {1,7,63,65,127,511}; offset = 0; unsigned block = 0;
        while (offset < N) {
            unsigned size = pass ? lengths[block++ % 6] : MAX_BLOCK;
            if (size > N - offset) size = N - offset;
            events e = {0};
            for (unsigned i = 0; i < CHANGES; ++i)
                if (times[i] >= offset && times[i] < offset + size)
                    e.events[e.count++] = make_event(indices[i], changes[i], times[i] - offset);
            /* Reverse each equal-offset group on the second pass. Seed and
               macro changes must be applied as one settled event group. */
            if (pass) for (unsigned first = 0; first < e.count;) {
                unsigned last = first + 1;
                while (last < e.count && e.events[last].header.time == e.events[first].header.time) ++last;
                for (unsigned i = first, j = last - 1; i < j; ++i, --j) {
                    clap_event_param_value_t swap = e.events[i]; e.events[i] = e.events[j]; e.events[j] = swap;
                }
                first = last;
            }
            clap_input_events_t in_events = {.ctx = &e, .size = event_count, .get = event_get};
            float *ins[] = {pass ? output[0] + offset : input[0] + offset,
                           pass ? output[1] + offset : input[1] + offset};
            float *outs[] = {output[0] + offset, output[1] + offset};
            clap_audio_buffer_t audio_in = {.data32 = ins, .channel_count = 2};
            clap_audio_buffer_t audio_out = {.data32 = outs, .channel_count = 2};
            clap_process_t process = {.frames_count = size, .audio_inputs = &audio_in, .audio_outputs = &audio_out,
                .audio_inputs_count = 1, .audio_outputs_count = 1, .in_events = &in_events};
            assert(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR); offset += size;
        }
        assert(memcmp(output, expected, sizeof(output)) == 0);
        if (pass) assert(memcmp(output, previous, sizeof(output)) == 0);
        else memcpy(previous, output, sizeof(output));
        plugin->stop_processing(plugin); plugin->deactivate(plugin);
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
    assert(descriptor && !strcmp(descriptor->id, "local.retro-study.magnetic"));
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
        assert(params->get_info(p, i, &info) && info.id == 3000 + i);
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
    check_macro_render(p, 44100); check_macro_render(p, 48000); check_macro_render(p, 96000);
    p->destroy(p); entry->deinit(); assert(dlclose(library) == 0);
    puts("Magnetic CLAP ABI, state migration, reset, direct/macro partitions and sample-offset automation passed");
    return 0;
}
