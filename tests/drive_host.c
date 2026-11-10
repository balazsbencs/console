#include <clap/clap.h>
#include "drive.h"
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

enum { PARAM_COUNT = 11, FRAMES = 4096, MAX_BLOCK = 512 };
static const float defaults[PARAM_COUNT]={.5f,0,1,1,0,0,20,20000,0,0,20000};
static const float settings[PARAM_COUNT]={.83f,0,.72f,1,0,1,140,8700,-3,2,6500};
static const void *CLAP_ABI host_extension(const clap_host_t *host, const char *id) {
    (void)host; (void)id; return NULL;
}
static void CLAP_ABI request(const clap_host_t *host) { (void)host; }
static const clap_host_t host = {
    .clap_version = CLAP_VERSION_INIT, .name = "Drive Cores ABI test", .vendor = "Local", .url = "", .version = "1",
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
        .param_id = 4000 + index, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = value};
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
        double value; assert(params->get_value(p, 4000 + i, &value) && value == (double)values[i]);
    }
}
static retro_drive_params core_params(const float *v) {
    return (retro_drive_params){.amount=v[0],.character=v[1],.mix=v[2],.enable=v[3],
        .processing_mode=v[5],.low_hz=v[6],.high_hz=v[7],.dist_db=v[8],.pass_db=v[9],.output_hz=v[10]};
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
    assert(!memcmp(m.data, "RSDC", 4) && m.data[4]==2 && m.data[8]==PARAM_COUNT);
    set_values(p, defaults);
    --m.size; assert(!state->load(p, &in)); check_values(p, defaults);
    ++m.size; m.cursor = 0; assert(state->load(p, &in)); check_values(p, settings);
    /* A malformed record also rejects the complete state without partial publication. */
    set_values(p, defaults); m.cursor = 0; m.data[m.size - 8] ^= 1;
    assert(!state->load(p, &in)); check_values(p, defaults);
    m.data[m.size - 8] ^= 1; m.cursor = 0;
    assert(state->load(p, &in)); check_values(p, settings);
    /* Old five-control states must supply defaults for all processor controls,
       even after loading a nondefault processor state. */
    m.data[4]=1;m.data[8]=5;m.size=12+5*8;m.cursor=0;
    assert(state->load(p,&in));
    float migrated[PARAM_COUNT];memcpy(migrated,defaults,sizeof(migrated));
    memcpy(migrated,settings,5*sizeof(float));check_values(p,migrated);
}


static void core_range(retro_drive *core, const float *values, float input[2][FRAMES],
                       float output[2][FRAMES], unsigned first, unsigned last) {
    for (unsigned c=0;c<2;++c)
        memcpy(output[c]+first,input[c]+first,(last-first)*sizeof(float));
    if (values[4]==0) retro_drive_process(core,output[0]+first,output[1]+first,last-first);
}
static void check_render(const clap_plugin_t *p, float rate, unsigned character, unsigned mode) {
    float initial[PARAM_COUNT]; memcpy(initial, settings, sizeof(initial)); initial[1]=(float)character; initial[5]=(float)mode;
    float input[2][FRAMES], expected[2][FRAMES], output[2][FRAMES], previous[2][FRAMES];
    for (unsigned i = 0; i < FRAMES; ++i) {
        input[0][i] = .7f * (float)sin(i * .071); input[1][i] = .4f * (float)cos(i * .093);
    }
    /* Several controls change at one offset; enable and bypass exercise different histories. */
    const unsigned time[] = {7,7,64,127,127,129,237,333,389,511,513,
        650,650,769,769,769,769,1024,1025,1537,2048,2049,3073};
    const unsigned index[] = {0,2,1,6,7,3,3,4,4,1,5,
        6,7,0,8,9,10,1,5,5,1,5,10};
    const float value[] = {.24f,.86f,(float)((character+1)%3),12000,800,0,1,1,0,
        (float)((character+2)%3),(float)(1-mode),60,16000,.91f,4,-6,1700,
        (float)character,(float)mode,(float)(1-mode),(float)((character+1)%3),(float)mode,19000};
    enum { CHANGES = sizeof(time) / sizeof(time[0]) };
    for (unsigned pass = 0; pass < 2; ++pass) {
        set_values(p, initial);
        assert(p->activate(p, rate, 1, MAX_BLOCK)); assert(p->start_processing(p));
        /* Populate every selected numerical history so reset must rewind it. */
        float *warm_in[] = {input[0], input[1]}, *warm_out[] = {output[0], output[1]};
        clap_audio_buffer_t warm_input = {.data32 = warm_in, .channel_count = 2};
        clap_audio_buffer_t warm_output = {.data32 = warm_out, .channel_count = 2};
        clap_process_t warmup = {.frames_count = MAX_BLOCK, .audio_inputs = &warm_input,
            .audio_outputs = &warm_output, .audio_inputs_count = 1, .audio_outputs_count = 1};
        assert(p->process(p, &warmup) != CLAP_PROCESS_ERROR);
        p->reset(p);
        retro_drive *core = retro_drive_create(); assert(core);
        float current[PARAM_COUNT]; memcpy(current, initial, sizeof(current));
        retro_drive_params cp = core_params(current);
        assert(retro_drive_set(core, &cp) && retro_drive_prepare(core, rate));
        retro_drive_reset(core);
        unsigned first = 0, change = 0;
        while (change < CHANGES) {
            unsigned last = time[change]; core_range(core, current, input, expected, first, last);
            do { current[index[change]] = value[change]; ++change; }
            while (change < CHANGES && time[change] == last);
            cp = core_params(current); assert(retro_drive_set(core, &cp)); first = last;
        }
        core_range(core, current, input, expected, first, FRAMES);
        retro_drive_destroy(core);
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
            if (pass) for (unsigned first=0;first<e.count;) {
                unsigned last=first+1;
                while (last<e.count && e.events[last].header.time==e.events[first].header.time) ++last;
                for (unsigned i=first,j=last-1;i<j;++i,--j) {
                    clap_event_param_value_t swap=e.events[i];e.events[i]=e.events[j];e.events[j]=swap;
                }
                first=last;
            }
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
        for (unsigned c=0;c<2;++c) for (unsigned i=0;i<FRAMES;++i) assert(isfinite(output[c][i]));
        assert(memcmp(output, expected, sizeof(output)) == 0);
        assert(memcmp(output, input, sizeof(output)) != 0);
        if (pass) assert(memcmp(output, previous, sizeof(output)) == 0);
        else memcpy(previous, output, sizeof(output));
        p->stop_processing(p); p->deactivate(p);
    }
}

static void check_invalid_events(const clap_plugin_t *p) {
    set_values(p,defaults);
    events e={.count=15};
    const unsigned index[]={0,1,2,3,4,99,0,1,5,6,7,8,9,10,5};
    const double value[]={NAN,.5,INFINITY,.5,.5,0,-1,3,.5,19,20001,101,-101,NAN,2};
    for (unsigned i=0;i<e.count;++i) e.events[i]=make_event(index[i],value[i],0);
    clap_input_events_t in={.ctx=&e,.size=event_count,.get=event_get};
    const clap_plugin_params_t *params=p->get_extension(p,CLAP_EXT_PARAMS);
    params->flush(p,&in,NULL);check_values(p,defaults);
}
static void check_core_validation(unsigned mode) {
    retro_drive *a=retro_drive_create(),*b=retro_drive_create();assert(a && b);
    retro_drive_params initial=retro_drive_defaults();initial.amount=.73f;initial.character=2;initial.mix=.8f;initial.processing_mode=(float)mode;
    assert(retro_drive_set(a,&initial) && retro_drive_set(b,&initial));
    assert(retro_drive_prepare(a,48000) && retro_drive_prepare(b,48000));
    assert(!retro_drive_set(a,NULL) && !retro_drive_set(NULL,&initial));
    const float rates[]={NAN,INFINITY,-1,999,1536001};
    for (unsigned i=0;i<sizeof(rates)/sizeof(*rates);++i) assert(!retro_drive_prepare(a,rates[i]));
    for (unsigned field=0;field<4;++field) {
        const float invalid[]={NAN,INFINITY,-1,3,.5f};
        for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
            if (field!=1 && field!=3 && i==4) continue;
            retro_drive_params value=initial;
            if (field==0) value.amount=invalid[i];
            else if (field==1) value.character=invalid[i];
            else if (field==2) value.mix=invalid[i];
            else value.enable=invalid[i];
            assert(!retro_drive_set(a,&value));
        }
    }
    for (unsigned field=0;field<6;++field) {
        const float invalid[]={NAN,INFINITY,field==0?.5f:field<4?19.0f:-101.0f,
            field==0?2.0f:field<4?20001.0f:101.0f};
        for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
            retro_drive_params value=initial;
            if(field==0) value.processing_mode=invalid[i];
            else if(field==1) value.low_hz=invalid[i];
            else if(field==2) value.high_hz=invalid[i];
            else if(field==3) value.output_hz=invalid[i];
            else if(field==4) value.dist_db=invalid[i];
            else value.pass_db=invalid[i];
            assert(!retro_drive_set(a,&value));
        }
    }
    /* Invalid updates must leave both controls and populated histories intact. */
    for (unsigned block=0;block<3;++block) {
        float a_l[64],a_r[64],b_l[64],b_r[64];
        for (unsigned i=0;i<64;++i) {a_l[i]=(float)sin((block*64+i)*.1);a_r[i]=(float)cos((block*64+i)*.13);}
        memcpy(b_l,a_l,sizeof(a_l));memcpy(b_r,a_r,sizeof(a_r));
        retro_drive_process(a,a_l,a_r,64);retro_drive_process(b,b_l,b_r,64);
        assert(memcmp(a_l,b_l,sizeof(a_l))==0 && memcmp(a_r,b_r,sizeof(a_r))==0);
        retro_drive_params invalid=initial;invalid.character=.5f;assert(!retro_drive_set(a,&invalid));
    }
    retro_drive_destroy(a);retro_drive_destroy(b);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    const clap_plugin_entry_t *entry = dlsym(library, "clap_entry"); assert(entry && entry->init(argv[1]));
    const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    assert(factory && factory->get_plugin_count(factory) == 1);
    const clap_plugin_descriptor_t *descriptor = factory->get_plugin_descriptor(factory, 0);
    assert(descriptor && !strcmp(descriptor->id, "local.retro-study.drive-cores"));
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
        assert(params->get_info(p, i, &info) && info.id == 4000 + i);
        assert(info.default_value == (double)defaults[i]);
        assert(params->value_to_text(p, info.id, info.default_value, text, sizeof(text)));
        assert(params->text_to_value(p, info.id, text, &parsed));
        assert(fabs(parsed - info.default_value) < 1e-7);
    }
    check_values(p, defaults); check_invalid_events(p); check_state(p); check_core_validation(0); check_core_validation(1);
    assert(!p->activate(p, 999, 1, MAX_BLOCK));
    assert(!p->activate(p, 1536001, 1, MAX_BLOCK));
    const float rates[] = {48000, 96000, 1000, 384000, 44100};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
        for (unsigned character=0;character<3;++character)
            for (unsigned mode=0;mode<2;++mode) check_render(p, rates[i], character, mode);
    p->destroy(p); entry->deinit(); assert(dlclose(library) == 0);
    puts("Drive Cores CLAP ABI, state migration, reset, processor/core partitions and sample-offset automation passed");
    return 0;
}
