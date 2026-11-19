#include <assert.h>
#include <clap/clap.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "noise_comp.h"

enum { FRAMES = 512 };
typedef struct {
    unsigned char data[128];
    size_t size, offset;
} state_buffer;

static const void *CLAP_ABI host_extension(const clap_host_t *host, const char *id) {
    (void)host;
    (void)id;
    return NULL;
}

static void CLAP_ABI host_request(const clap_host_t *host) { (void)host; }

static uint32_t CLAP_ABI event_count(const clap_input_events_t *events) {
    return events->ctx ? 1u : 0u;
}

static const clap_event_header_t *CLAP_ABI event_at(const clap_input_events_t *events, uint32_t i) {
    const clap_event_param_value_t *event = events->ctx;
    return i == 0 && event ? &event->header : NULL;
}

static int64_t CLAP_ABI state_write(const clap_ostream_t *stream, const void *data, uint64_t size) {
    state_buffer *buffer = stream->ctx;
    size_t n = size > 7 ? 7 : (size_t)size;
    if (n > sizeof(buffer->data) - buffer->size) return -1;
    memcpy(buffer->data + buffer->size, data, n);
    buffer->size += n;
    return (int64_t)n;
}

static int64_t CLAP_ABI state_read(const clap_istream_t *stream, void *data, uint64_t size) {
    state_buffer *buffer = stream->ctx;
    size_t n = size > 5 ? 5 : (size_t)size;
    if (n > buffer->size - buffer->offset) n = buffer->size - buffer->offset;
    memcpy(data, buffer->data + buffer->offset, n);
    buffer->offset += n;
    return (int64_t)n;
}

static void render(const clap_plugin_t *plugin, float rate, bool connected, bool in_place) {
    const clap_plugin_params_t *params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    clap_event_param_value_t threshold = {
        .header = {sizeof(threshold), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0},
        .param_id = 6001, .note_id = -1, .port_index = -1, .channel = -1, .key = -1,
        .value = .9,
    };
    clap_input_events_t events = {.ctx = &threshold, .size = event_count, .get = event_at};
    params->flush(plugin, &events, NULL);
    assert(plugin->activate(plugin, rate, 1, FRAMES));
    assert(plugin->start_processing(plugin));

    float input[2][FRAMES], side[2][FRAMES], expected[2][FRAMES], output[2][FRAMES];
    for (unsigned i = 0; i < FRAMES; ++i) {
        input[0][i] = .3f * sinf(.27f * (float)i);
        input[1][i] = .2f * cosf(.19f * (float)i);
        side[0][i] = 0;
        side[1][i] = .8f * sinf(.11f * (float)i);
    }
    memcpy(expected, input, sizeof(expected));
    memcpy(output, input, sizeof(output));
    rs_noise_comp core = {0};
    rs_noise_comp_params config = rs_noise_comp_defaults();
    config.threshold = .9f;
    assert(rs_noise_comp_set(&core, &config));
    assert(rs_noise_comp_prepare(&core, rate));
    const float *side_l = connected ? side[0] : NULL;
    const float *side_r = connected ? side[1] : NULL;
    const unsigned change_at = 79;
    rs_noise_comp_process(&core, expected[0], expected[1], side_l, side_r, change_at);
    config.threshold = .6f;
    assert(rs_noise_comp_set(&core, &config));
    rs_noise_comp_process(&core, expected[0] + change_at, expected[1] + change_at,
                          connected ? side_l + change_at : NULL,
                          connected ? side_r + change_at : NULL, FRAMES - change_at);

    for (unsigned offset = 0; offset < FRAMES;) {
        unsigned n = in_place ? 37 : FRAMES;
        if (n > FRAMES - offset) n = FRAMES - offset;
        float *in[] = {input[0] + offset, input[1] + offset};
        float *out[] = {output[0] + offset, output[1] + offset};
        float *key[] = {side[0] + offset, side[1] + offset};
        clap_audio_buffer_t inputs[] = {
            {.data32 = in_place ? out : in, .channel_count = 2},
            {.data32 = key, .channel_count = 2},
        };
        clap_audio_buffer_t result = {.data32 = out, .channel_count = 2};
        events.ctx = NULL;
        if (change_at >= offset && change_at < offset + n) {
            threshold.header.time = change_at - offset;
            threshold.value = .6;
            events.ctx = &threshold;
        }
        clap_process_t process = {.frames_count = n, .audio_inputs = inputs, .audio_outputs = &result,
            .audio_inputs_count = connected ? 2 : 1, .audio_outputs_count = 1, .in_events = &events};
        assert(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE);
        offset += n;
    }
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned i = 0; i < FRAMES; ++i)
            assert(fabsf(expected[c][i] - output[c][i]) < 1e-7f);
    if (connected) assert(fabsf(output[0][400]) < fabsf(input[0][400]) * .85f);
    plugin->reset(plugin);
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(module);
    const clap_plugin_entry_t *entry = dlsym(module, "clap_entry");
    assert(entry && entry->init(argv[1]));
    const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    assert(factory && factory->get_plugin_count(factory) == 1);
    const clap_host_t host = {.clap_version = CLAP_VERSION_INIT, .name = "Noise Duck Host Test",
        .vendor = "Retro Study", .url = "", .version = "1", .get_extension = host_extension,
        .request_restart = host_request, .request_process = host_request, .request_callback = host_request};
    const clap_plugin_t *plugin = factory->create_plugin(factory, &host, "local.retro-study.noise-duck");
    assert(plugin && plugin->init(plugin));
    const clap_plugin_audio_ports_t *ports = plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
    assert(ports && ports->count(plugin, true) == 2 && ports->count(plugin, false) == 1);
    clap_audio_port_info_t side;
    assert(ports->get(plugin, 1, true, &side) && side.channel_count == 2 &&
           side.flags == 0 && side.in_place_pair == CLAP_INVALID_ID);
    assert(!ports->get(plugin, 1, false, &side));

    render(plugin, 44100, true, false);
    render(plugin, 44100, true, true);
    render(plugin, 96000, false, true);

    const clap_plugin_state_t *state = plugin->get_extension(plugin, CLAP_EXT_STATE);
    const clap_plugin_params_t *params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    assert(state && params && params->count(plugin) == 9);
    state_buffer buffer = {0};
    clap_ostream_t writer = {.ctx = &buffer, .write = state_write};
    clap_istream_t reader = {.ctx = &buffer, .read = state_read};
    assert(state->save(plugin, &writer) && buffer.size == 84);
    assert(state->load(plugin, &reader));
    buffer.offset = 0;
    --buffer.size;
    assert(!state->load(plugin, &reader));
    double value;
    assert(params->get_value(plugin, 6001, &value) && (float)value == .6f);

    plugin->destroy(plugin);
    entry->deinit();
    assert(dlclose(module) == 0);
    puts("Noise Ducking CLAP ports, state and stereo-linked processing passed");
    return 0;
}
