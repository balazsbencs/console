#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <assert.h>
#include <clap/clap.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "noise_comp.h"
#include "noise_follow.h"
#include "noise_tone.h"

enum { FRAMES = 512, SOURCE_ID = 6009, TYPE_ID = 6010, AMOUNT_ID = 6011, TONE_ID = 6012, FOLLOW_ID = 6013 };
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
    return i == 0 ? events->ctx : NULL;
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

static void put_u32(unsigned char *data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[i] = (unsigned char)(value >> (i * 8));
}

static void texture_fixture(const char *directory, unsigned id, unsigned char code, char path[128]) {
    int length = snprintf(path, 128, "%s/990101_%03u_00_v700_Noise [ABC].nxac", directory, id);
    assert(length > 0 && length < 128);
    enum { TABLE = 0x128, DATA = TABLE + 16, BYTES_PER_BLOCK = 66 };
    unsigned char data[DATA + 32 * BYTES_PER_BLOCK] = "NXAC v1.1\n";
    put_u32(data + 0x100, 0x3a5920b3);
    put_u32(data + 0x104, 2);
    put_u32(data + 0x108, 65);
    put_u32(data + 0x10c, 2048);
    put_u32(data + 0x110, 64);
    put_u32(data + 0x114, 0x472c4400);
    put_u32(data + 0x118, TABLE);
    put_u32(data + 0x11c, 16);
    put_u32(data + 0x120, DATA);
    put_u32(data + 0x124, sizeof(data) - DATA);
    put_u32(data + TABLE, 0x472c4400);
    data[TABLE + 12] = 3;
    for (unsigned offset = DATA; offset < sizeof(data); offset += BYTES_PER_BLOCK) {
        data[offset] = 30;
        memset(data + offset + 2, code, 64);
    }
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, sizeof(data), file) == sizeof(data));
    assert(!fclose(file));
}

static void set_param(const clap_plugin_t *plugin, clap_id id, double value) {
    const clap_plugin_params_t *params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    clap_event_param_value_t event = {
        .header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0},
        .param_id = id, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = value,
    };
    clap_input_events_t events = {.ctx = &event, .size = event_count, .get = event_at};
    params->flush(plugin, &events, NULL);
}

static void render_samples(const clap_plugin_t *plugin) {
    char directory[] = "/tmp/retro-study-noise-XXXXXX";
    char white[128], vinyl[128];
    assert(mkdtemp(directory));
    texture_fixture(directory, 1, 192, white);
    texture_fixture(directory, 12, 96, vinyl);
    assert(!setenv("RETRO_STUDY_NOISE_DIR", directory, 1));

    set_param(plugin, SOURCE_ID, 1);
    set_param(plugin, AMOUNT_ID, 1);
    set_param(plugin, TONE_ID, .9);
    set_param(plugin, 6001, .9);
    set_param(plugin, 6007, 0);
    assert(plugin->activate(plugin, 44100, 1, FRAMES));
    assert(!unlink(white) && !unlink(vinyl) && !rmdir(directory));
    assert(plugin->start_processing(plugin));

    float data[2][FRAMES], expected[2][FRAMES], dry[2][FRAMES], key[2][FRAMES] = {0};
    const unsigned change = 77;
    for (unsigned i = 0; i < FRAMES; ++i) {
        dry[0][i] = .8f;
        dry[1][i] = -.2f;
        float texture = ((i < change ? 192.0f : 96.0f) - 127.5f) / 256.0f;
        expected[0][i] = expected[1][i] = texture;
    }
    memcpy(data, dry, sizeof(data));
    rs_noise_comp core = {0};
    rs_noise_comp_params config = rs_noise_comp_defaults();
    config.threshold = .9f;
    config.highpass = 0;
    assert(rs_noise_comp_set(&core, &config));
    assert(rs_noise_comp_prepare(&core, 44100));
    rs_noise_tone tone = {0};
    assert(rs_noise_tone_prepare(&tone, 44100));
    assert(rs_noise_tone_set(&tone, .9f));
    rs_noise_tone_process(&tone, expected[0], expected[1], FRAMES);
    rs_noise_comp_process(&core, expected[0], expected[1], dry[0], dry[1], FRAMES);

    float *in_place[] = {data[0], data[1]};
    float *side[] = {key[0], key[1]};
    clap_audio_buffer_t inputs[] = {
        {.data32 = in_place, .channel_count = 2}, {.data32 = side, .channel_count = 2},
    };
    clap_audio_buffer_t out = {.data32 = in_place, .channel_count = 2};
    clap_event_param_value_t type_change = {
        .header = {sizeof(type_change), change, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0},
        .param_id = TYPE_ID, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = 1,
    };
    clap_input_events_t events = {.ctx = &type_change, .size = event_count, .get = event_at};
    clap_process_t p = {.frames_count = FRAMES, .audio_inputs = inputs, .audio_outputs = &out,
        .audio_inputs_count = 2, .audio_outputs_count = 1, .in_events = &events};
    assert(plugin->process(plugin, &p) == CLAP_PROCESS_CONTINUE);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned i = 0; i < FRAMES; ++i)
            assert(fabsf(data[c][i] - (dry[c][i] + expected[c][i])) < 1e-7f);

    memcpy(data, dry, sizeof(data));
    for (unsigned i = 0; i < FRAMES; ++i)
        expected[0][i] = expected[1][i] = (96.0f - 127.5f) / 256.0f;
    rs_noise_tone_process(&tone, expected[0], expected[1], change);
    assert(rs_noise_tone_set(&tone, .1f));
    rs_noise_tone_process(&tone, expected[0] + change, expected[1] + change, FRAMES - change);
    rs_noise_comp_process(&core, expected[0], expected[1], dry[0], dry[1], FRAMES);
    type_change.param_id = TONE_ID;
    type_change.value = .1;
    assert(plugin->process(plugin, &p) == CLAP_PROCESS_CONTINUE);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned i = 0; i < FRAMES; ++i)
            assert(fabsf(data[c][i] - (dry[c][i] + expected[c][i])) < 1e-7f);

    memcpy(data, dry, sizeof(data));
    for (unsigned i = 0; i < FRAMES; ++i)
        expected[0][i] = expected[1][i] = (96.0f - 127.5f) / 256.0f;
    rs_noise_follow follow = {0};
    assert(rs_noise_follow_prepare(&follow, 44100));
    float gains[FRAMES];
    rs_noise_follow_process(&follow, dry[0], dry[1], gains, change);
    assert(rs_noise_follow_set(&follow, 1.0f));
    rs_noise_follow_process(&follow, dry[0] + change, dry[1] + change, gains + change, FRAMES - change);
    assert(gains[0] == 1.0f && gains[FRAMES - 1] != 1.0f);
    rs_noise_tone_process(&tone, expected[0], expected[1], FRAMES);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned i = 0; i < FRAMES; ++i)
            expected[c][i] *= gains[i];
    rs_noise_comp_process(&core, expected[0], expected[1], dry[0], dry[1], FRAMES);
    type_change.param_id = FOLLOW_ID;
    type_change.value = 1;
    assert(plugin->process(plugin, &p) == CLAP_PROCESS_CONTINUE);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned i = 0; i < FRAMES; ++i)
            assert(fabsf(data[c][i] - (dry[c][i] + expected[c][i])) < 1e-7f);

    memset(data, 0, sizeof(data));
    set_param(plugin, FOLLOW_ID, 0);
    plugin->reset(plugin);
    clap_event_transport_t stopped = {0};
    clap_event_transport_t playing = {
        .header = {sizeof(playing), 64, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0},
        .flags = CLAP_TRANSPORT_IS_PLAYING,
    };
    p.transport = &stopped;
    events.ctx = &playing;
    assert(plugin->process(plugin, &p) == CLAP_PROCESS_CONTINUE);
    for (unsigned c = 0; c < 2; ++c) {
        for (unsigned i = 0; i < 64; ++i)
            assert(data[c][i] == 0.0f);
        assert(data[c][64] != 0.0f);
    }

    p.transport = NULL;
    events.ctx = &type_change;
    memcpy(data, dry, sizeof(data));
    type_change.header.time = 0;
    type_change.param_id = TYPE_ID;
    type_change.value = 15;
    assert(plugin->process(plugin, &p) == CLAP_PROCESS_CONTINUE);
    assert(memcmp(data, dry, sizeof(data)) == 0);
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
    assert(state && params && params->count(plugin) == 14);
    state_buffer buffer = {0};
    clap_ostream_t writer = {.ctx = &buffer, .write = state_write};
    clap_istream_t reader = {.ctx = &buffer, .read = state_read};
    set_param(plugin, TONE_ID, .7);
    set_param(plugin, FOLLOW_ID, .8);
    assert(state->save(plugin, &writer) && buffer.size == 124);
    set_param(plugin, TONE_ID, .4);
    set_param(plugin, FOLLOW_ID, .4);
    assert(state->load(plugin, &reader));
    double value;
    assert(params->get_value(plugin, TONE_ID, &value) && (float)value == .7f);
    assert(params->get_value(plugin, FOLLOW_ID, &value) && (float)value == .8f);
    buffer.offset = 0;
    --buffer.size;
    assert(!state->load(plugin, &reader));
    assert(params->get_value(plugin, 6001, &value) && (float)value == .6f);
    buffer.data[4] = 3;
    buffer.data[8] = 13;
    buffer.size = 116;
    buffer.offset = 0;
    assert(state->load(plugin, &reader));
    assert(params->get_value(plugin, TONE_ID, &value) && (float)value == .7f);
    assert(params->get_value(plugin, FOLLOW_ID, &value) && value == 0);
    buffer.data[4] = 2;
    buffer.data[8] = 12;
    buffer.size = 108;
    buffer.offset = 0;
    set_param(plugin, TONE_ID, .1);
    assert(state->load(plugin, &reader));
    assert(params->get_value(plugin, TONE_ID, &value) && value == .5);
    buffer.data[4] = 1;
    buffer.data[8] = 9;
    buffer.size = 84;
    buffer.offset = 0;
    set_param(plugin, SOURCE_ID, 1);
    assert(state->load(plugin, &reader));
    assert(params->get_value(plugin, SOURCE_ID, &value) && value == 0);
    assert(params->get_value(plugin, TYPE_ID, &value) && value == 11);

    render_samples(plugin);

    plugin->destroy(plugin);
    entry->deinit();
    assert(dlclose(module) == 0);
    puts("Noise Ducking CLAP ports, state, external ducking and internal texture playback passed");
    return 0;
}
