#include <clap/clap.h>
#include "bitcolor.h"
#include "digital_flux.h"
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the public ABI through dlopen, rather than linking plugin internals. */
static const void *CLAP_ABI host_extension(const clap_host_t *h, const char *id) {
    (void)h; (void)id; return NULL;
}
static void CLAP_ABI host_request(const clap_host_t *h) { (void)h; }
static const clap_host_t host = {
    .clap_version = CLAP_VERSION_INIT,
    .name = "Retro Study ABI test", .vendor = "Local", .url = "", .version = "0.1",
    .get_extension = host_extension,
    .request_restart = host_request, .request_process = host_request,
    .request_callback = host_request,
};

typedef struct {
    unsigned char data[8192];
    size_t size, cursor;
} memory_stream;
static int64_t CLAP_ABI write_stream(const clap_ostream_t *stream, const void *data, uint64_t size) {
    memory_stream *m = stream->ctx;
    /* Deliberate short writes: the plugin must loop until its state is saved. */
    size_t count = size > 7 ? 7 : (size_t)size;
    if (count > sizeof(m->data) - m->size) return -1;
    memcpy(m->data + m->size, data, count); m->size += count;
    return (int64_t)count;
}
static int64_t CLAP_ABI read_stream(const clap_istream_t *stream, void *data, uint64_t size) {
    memory_stream *m = stream->ctx;
    size_t count = size > 3 ? 3 : (size_t)size;
    if (count > m->size - m->cursor) count = m->size - m->cursor;
    memcpy(data, m->data + m->cursor, count); m->cursor += count;
    return (int64_t)count;
}
static uint32_t CLAP_ABI no_events(const clap_input_events_t *in) { (void)in; return 0; }
static const clap_event_header_t *CLAP_ABI no_event(const clap_input_events_t *in, uint32_t i) {
    (void)in; (void)i; return NULL;
}
static const clap_input_events_t empty_events = {.size = no_events, .get = no_event};

typedef struct { clap_event_param_value_t events[4]; } event_list;
static uint32_t CLAP_ABI event_count(const clap_input_events_t *in) { (void)in; return 4; }
static const clap_event_header_t *CLAP_ABI get_event(const clap_input_events_t *in, uint32_t i) {
    const event_list *events = in->ctx;
    return i < 4 ? &events->events[i].header : NULL;
}
static void check_automation(const clap_plugin_t *plugin) {
    assert(plugin->activate(plugin, 48000, 1, 512));
    assert(plugin->start_processing(plugin));
    float left[512], right[512], output[2][512], expected[2][512];
    for (unsigned i = 0; i < 512; ++i) {
        left[i] = (float)sin(i * 0.071) * 0.6f;
        right[i] = (float)cos(i * 0.093) * 0.4f;
    }
    const uint32_t times[] = {7, 64, 111, 190};
    const clap_id ids[] = {1003, 1000, 1011, 1011};
    const double values[] = {0.2, 2, 1, 0};
    event_list events = {0};
    for (unsigned i = 0; i < 4; ++i) {
        events.events[i] = (clap_event_param_value_t){
            .header = {.size = sizeof(clap_event_param_value_t), .time = times[i],
                .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = CLAP_EVENT_PARAM_VALUE},
            .param_id = ids[i], .note_id = -1, .port_index = -1, .channel = -1, .key = -1,
            .value = values[i]};
    }
    clap_input_events_t inputs = {.ctx = &events, .size = event_count, .get = get_event};
    float *ins[] = {left, right}, *outs[] = {output[0], output[1]};
    clap_audio_buffer_t audio_in = {.data32 = ins, .channel_count = 2};
    clap_audio_buffer_t audio_out = {.data32 = outs, .channel_count = 2};
    clap_process_t process = {.frames_count = 512, .audio_inputs = &audio_in, .audio_outputs = &audio_out,
        .audio_inputs_count = 1, .audio_outputs_count = 1, .in_events = &inputs};
    assert(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR);
    retro_digital dsp = {0}; retro_digital_params p = retro_digital_defaults();
    assert(retro_digital_set(&dsp, &p) && retro_digital_prepare(&dsp, 48000));
    retro_digital_process(&dsp, left, right, expected[0], expected[1], 7);
    p.mix = 0.2f; assert(retro_digital_set(&dsp, &p));
    retro_digital_process(&dsp, left + 7, right + 7, expected[0] + 7, expected[1] + 7, 57);
    p.bits = 2; assert(retro_digital_set(&dsp, &p));
    retro_digital_process(&dsp, left + 64, right + 64, expected[0] + 64, expected[1] + 64, 47);
    memcpy(expected[0] + 111, left + 111, 79 * sizeof(float));
    memcpy(expected[1] + 111, right + 111, 79 * sizeof(float));
    /* Bypass changes also invalidate adapter controls; mirror that at re-entry. */
    assert(retro_digital_set(&dsp, &p));
    retro_digital_process(&dsp, left + 190, right + 190, expected[0] + 190, expected[1] + 190, 322);
    assert(memcmp(output, expected, sizeof(output)) == 0);
    plugin->stop_processing(plugin); plugin->deactivate(plugin);
}


/* Same-offset macro events must initialize normalized smoothing from the final
   group of values, regardless of their order in the host event list. */
static void check_macro_group(const clap_plugin_t *a, const clap_plugin_t *b) {
    float left[512], right[512], output[2][2][512], expected[2][512];
    for (unsigned i = 0; i < 512; ++i) {
        left[i] = (float)sin(i * 0.071) * 0.6f;
        right[i] = (float)cos(i * 0.093) * 0.4f;
    }
    const clap_id ids[] = {1015, 1016, 1017, 1018};
    const double values[] = {1, 0.8, 0.3, 0.6};
    const clap_plugin_t *plugins[] = {a, b};
    for (unsigned n = 0; n < 2; ++n) {
        const clap_plugin_t *plugin = plugins[n];
        assert(plugin->activate(plugin, 48000, 1, 512));
        assert(plugin->start_processing(plugin));
        event_list events = {0};
        for (unsigned i = 0; i < 4; ++i) {
            unsigned k = n ? 3 - i : i;
            events.events[i] = (clap_event_param_value_t){
                .header = {.size = sizeof(clap_event_param_value_t), .time = 0,
                    .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = CLAP_EVENT_PARAM_VALUE},
                .param_id = ids[k], .note_id = -1, .port_index = -1, .channel = -1, .key = -1,
                .value = values[k]};
        }
        clap_input_events_t inputs = {.ctx = &events, .size = event_count, .get = get_event};
        float *ins[] = {left, right}, *outs[] = {output[n][0], output[n][1]};
        clap_audio_buffer_t audio_in = {.data32 = ins, .channel_count = 2};
        clap_audio_buffer_t audio_out = {.data32 = outs, .channel_count = 2};
        clap_process_t process = {.frames_count = 512, .audio_inputs = &audio_in, .audio_outputs = &audio_out,
            .audio_inputs_count = 1, .audio_outputs_count = 1, .in_events = &inputs};
        assert(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR);
        plugin->stop_processing(plugin); plugin->deactivate(plugin);
    }
    retro_digital dsp = {0}; retro_digital_params p = retro_digital_defaults();
    assert(retro_digital_set(&dsp, &p) && retro_digital_prepare(&dsp, 48000));
    float amount = 0.6f * 0.8f;
    p.normalized = 1; p.rate = amount; p.bits = (0.3f + 0.3f) * amount;
    assert(retro_digital_set(&dsp, &p));
    retro_digital_process(&dsp, left, right, expected[0], expected[1], 512);
    assert(memcmp(output[0], output[1], sizeof(expected)) == 0);
    assert(memcmp(output[0], expected, sizeof(expected)) == 0);
}


static void check_flux(const clap_plugin_t *plugin) {
    const clap_plugin_params_t *params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    const clap_plugin_state_t *state = plugin->get_extension(plugin, CLAP_EXT_STATE);
    const clap_id ids[2][4] = {{1015, 1016, 1017, 1018}, {1020, 1021, 1022, 1012}};
    const double values[2][4] = {{1, 0.8, 0.3, 0.6}, {0.9, 123, 456, 1}};
    for (unsigned group = 0; group < 2; ++group) {
        event_list events = {0};
        for (unsigned i = 0; i < 4; ++i) {
            events.events[i] = (clap_event_param_value_t){
                .header = {.size = sizeof(clap_event_param_value_t),
                    .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = CLAP_EVENT_PARAM_VALUE},
                .param_id = ids[group][i], .note_id = -1, .port_index = -1, .channel = -1, .key = -1,
                .value = values[group][i]};
        }
        clap_input_events_t inputs = {.ctx = &events, .size = event_count, .get = get_event};
        params->flush(plugin, &inputs, NULL);
    }
    memory_stream memory = {0};
    clap_ostream_t saved = {.ctx = &memory, .write = write_stream};
    clap_istream_t loaded = {.ctx = &memory, .read = read_stream};
    assert(state->save(plugin, &saved));
    assert(state->load(plugin, &loaded));
    double value;
    assert(params->get_value(plugin, 1020, &value) && value == (double)0.9f);
    assert(params->get_value(plugin, 1021, &value) && value == 123);
    assert(params->get_value(plugin, 1022, &value) && value == 456);
    assert(plugin->activate(plugin, 48000, 1, 512));
    assert(plugin->start_processing(plugin));
    enum { N = 4096 };
    float left[N], right[N], output[2][N], previous[2][N], expected[2][N];
    for (unsigned i = 0; i < N; ++i) {
        left[i] = (float)sin(i * 0.071) * 0.6f;
        right[i] = (float)cos(i * 0.093) * 0.4f;
    }
    for (unsigned pass = 0; pass < 2; ++pass) {
        plugin->reset(plugin);
        retro_digital dsp = {0}; retro_digital_params p = retro_digital_defaults();
        rs_digital_flux flux = {0}; rs_digital_flux_reset(&flux, 123, 456);
        p.normalized = 1; p.focus = 0;
        rs_digital_flux_targets(&flux, 0.6f, 0.8f, 0.3f, &p.bits, &p.rate);
        assert(retro_digital_set(&dsp, &p) && retro_digital_prepare(&dsp, 48000));
        for (unsigned offset = 0; offset < N; offset += 64) {
            rs_digital_flux_tick(&flux, 48000, 0.6f, 0.9f, 0.8f);
            rs_digital_flux_targets(&flux, 0.6f, 0.8f, 0.3f, &p.bits, &p.rate);
            assert(retro_digital_set(&dsp, &p));
            if (offset >= 256 && offset < 512) {
                memcpy(expected[0] + offset, left + offset, 64 * sizeof(float));
                memcpy(expected[1] + offset, right + offset, 64 * sizeof(float));
            } else {
                retro_digital_process(&dsp, left + offset, right + offset,
                                      expected[0] + offset, expected[1] + offset, 64);
            }
        }
        const unsigned lengths[] = {1, 7, 63, 65, 127, 511};
        unsigned offset = 0, block = 0;
        while (offset < N) {
            if (offset == 256 || offset == 512) {
                event_list events = {0};
                for (unsigned i = 0; i < 4; ++i) {
                    events.events[i] = (clap_event_param_value_t){
                        .header = {.size = sizeof(clap_event_param_value_t),
                            .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = CLAP_EVENT_PARAM_VALUE},
                        .param_id = 1019, .note_id = -1, .port_index = -1, .channel = -1, .key = -1,
                        .value = offset == 512};
                }
                clap_input_events_t inputs = {.ctx = &events, .size = event_count, .get = get_event};
                params->flush(plugin, &inputs, NULL);
            }
            unsigned size = pass ? 64 : lengths[block++ % 6];
            unsigned boundary = offset < 256 ? 256 : offset < 512 ? 512 : N;
            if (size > boundary - offset) size = boundary - offset;
            float *ins[] = {left + offset, right + offset};
            float *outs[] = {output[0] + offset, output[1] + offset};
            clap_audio_buffer_t audio_in = {.data32 = ins, .channel_count = 2};
            clap_audio_buffer_t audio_out = {.data32 = outs, .channel_count = 2};
            clap_process_t process = {.frames_count = size, .audio_inputs = &audio_in, .audio_outputs = &audio_out,
                .audio_inputs_count = 1, .audio_outputs_count = 1, .in_events = &empty_events};
            assert(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR);
            offset += size;
        }
        assert(memcmp(output, expected, sizeof(output)) == 0);
        if (pass) assert(memcmp(output, previous, sizeof(output)) == 0);
        else memcpy(previous, output, sizeof(output));
    }
    plugin->stop_processing(plugin); plugin->deactivate(plugin);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    const clap_plugin_entry_t *entry = dlsym(library, "clap_entry");
    assert(entry && entry->init(argv[1]));
    const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    assert(factory && factory->get_plugin_count(factory) == 1);
    assert(factory->get_plugin_descriptor(factory, 1) == NULL);
    const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, 0);
    assert(desc && desc->id && desc->name);
    assert(factory->create_plugin(factory, &host, "invalid.id") == NULL);
    const clap_plugin_t *plugin = factory->create_plugin(factory, &host, desc->id);
    const clap_plugin_t *other = factory->create_plugin(factory, &host, desc->id);
    assert(plugin && other && plugin != other && plugin->init(plugin) && other->init(other));
    assert(plugin->get_extension(plugin, "invalid.extension") == NULL);
    const clap_plugin_params_t *params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    const clap_plugin_audio_ports_t *ports = plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
    const clap_plugin_state_t *state = plugin->get_extension(plugin, CLAP_EXT_STATE);
    assert(params && ports && state);
    clap_audio_port_info_t port;
    assert(ports->count(plugin, true) == 1 && ports->count(plugin, false) == 1);
    assert(ports->get(plugin, 0, true, &port) && port.channel_count == 2);
    assert(!ports->get(plugin, 1, true, &port));
    uint32_t count = params->count(plugin);
    assert(count > 0 && count <= 64);
    double values[64];
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info_t info;
        assert(params->get_info(plugin, i, &info));
        assert(params->get_value(plugin, info.id, &values[i]));
        assert(values[i] == info.default_value);
        char text[128]; double parsed;
        assert(params->value_to_text(plugin, info.id, values[i], text, sizeof(text)));
        assert(params->text_to_value(plugin, info.id, text, &parsed));
        assert(fabs(parsed - values[i]) < 1e-4);
    }
    memory_stream memory = {0};
    clap_ostream_t output = {.ctx = &memory, .write = write_stream};
    clap_istream_t input = {.ctx = &memory, .read = read_stream};
    assert(state->save(plugin, &output) && memory.size > 0);
    assert(state->load(plugin, &input));
    /* Truncated state must fail without changing any parameter. */
    memory.cursor = 0; memory.size--;
    assert(!state->load(plugin, &input));
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info_t info; double value;
        assert(params->get_info(plugin, i, &info));
        assert(params->get_value(plugin, info.id, &value) && value == values[i]);
    }
    for (int cycle = 0; cycle < 3; ++cycle) {
        assert(plugin->activate(plugin, cycle == 1 ? 96000 : 48000, 1, 512));
        assert(plugin->start_processing(plugin));
        float left[512], right[512], out_left[512], out_right[512];
        float *ins[] = {left, right}, *outs[] = {out_left, out_right};
        clap_audio_buffer_t audio_in = {.data32 = ins, .channel_count = 2};
        clap_audio_buffer_t audio_out = {.data32 = outs, .channel_count = 2};
        clap_process_t process = {.audio_inputs = &audio_in, .audio_outputs = &audio_out,
            .audio_inputs_count = 1, .audio_outputs_count = 1, .in_events = &empty_events};
        for (uint32_t block = 1; block <= 512; block = block == 1 ? 7 : block == 7 ? 64 : block * 2) {
            for (uint32_t i = 0; i < block; ++i) {
                left[i] = (float)sin((double)i * 0.13) * 0.5f;
                right[i] = (float)cos((double)i * 0.19) * 0.4f;
            }
            process.frames_count = block;
            assert(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR);
            for (uint32_t i = 0; i < block; ++i) assert(isfinite(out_left[i]) && isfinite(out_right[i]));
            /* Exercise both distinct buffers and advertised in-place buffers. */
            audio_out.data32 = ins;
            assert(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR);
            audio_out.data32 = outs;
        }
        plugin->reset(plugin);
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
    }
    check_macro_group(plugin, other);
    /* Version 1 states retain the original 12 controls and default all newly
       introduced controls, even after macro mode has been used. */
    memory.data[4] = 1; memory.data[8] = 12;
    memory.size = 12 + 12 * 8; memory.cursor = 0;
    assert(state->load(plugin, &input));
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info_t info; double value;
        assert(params->get_info(plugin, i, &info));
        assert(params->get_value(plugin, info.id, &value) && value == values[i]);
    }
    check_automation(plugin);
    /* Version 2 accepts its original 20 records and supplies Flux defaults. */
    memory.data[4] = 2; memory.data[8] = 20;
    memory.size = 12 + 20 * 8; memory.cursor = 0;
    assert(state->load(plugin, &input));
    check_flux(plugin);
    other->destroy(other); plugin->destroy(plugin); entry->deinit();
    assert(dlclose(library) == 0);
    puts("CLAP ABI, lifecycle, state, stereo and sample-offset automation passed");
    return 0;
}
