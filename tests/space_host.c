#include <clap/clap.h>
#include "space.h"
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

enum { PARAM_COUNT = 15, FRAMES = 4096, MAX_BLOCK = 512 };
static const float defaults[PARAM_COUNT]={1,.8f,0,22050,.15f,0,0,0,1,1,1,.01f,0,0,0};
static const float settings[PARAM_COUNT]={1,.73f,7300,11500,.41f,.36f,1,0,1,1,1,17,.31f,.22f,0};
static const void *CLAP_ABI host_extension(const clap_host_t *host, const char *id) {
    (void)host; (void)id; return NULL;
}
static void CLAP_ABI request(const clap_host_t *host) { (void)host; }
static const clap_host_t host = {
    .clap_version = CLAP_VERSION_INIT, .name = "Space ABI test", .vendor = "Local", .url = "", .version = "1",
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
        .param_id = 5000 + index, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = value};
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
        double value; assert(params->get_value(p, 5000 + i, &value) && value == (double)values[i]);
    }
}
static retro_space_params core_params(const float *v) {
    return (retro_space_params){.enable=v[0],.decay=v[1],.dampen_hz=v[2],.soften_hz=v[3],
        .level=v[4],.stereo_detune=v[5],.stereo_mode=v[6],.dual_mono=v[7],
        .feedbacks=v[8],.allpass=v[9],.reverb_mode=v[10],.pre_delay_ms=v[11],
        .pre_wobble_noise=v[12],.pre_wobble_amount=v[13]};
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
    assert(!memcmp(m.data, "RSSP", 4) && m.data[4]==1 && m.data[8]==PARAM_COUNT);
    set_values(p, defaults);
    --m.size; assert(!state->load(p, &in)); check_values(p, defaults);
    ++m.size; m.cursor = 0; assert(state->load(p, &in)); check_values(p, settings);
    /* A malformed record also rejects the complete state without partial publication. */
    set_values(p, defaults); m.cursor = 0; m.data[m.size - 8] ^= 1;
    assert(!state->load(p, &in)); check_values(p, defaults);
    m.data[m.size - 8] ^= 1; m.cursor = 0;
    assert(state->load(p, &in)); check_values(p, settings);

}

static void core_range(retro_space *core, const float *values, float input[2][FRAMES],
                       float output[2][FRAMES], unsigned first, unsigned last) {
    for (unsigned c=0;c<2;++c)
        memcpy(output[c]+first,input[c]+first,(last-first)*sizeof(float));
    if (values[14]==0) retro_space_process(core,output[0]+first,output[1]+first,last-first);
}
static void check_render(const clap_plugin_t *p, float rate) {
    float input[2][FRAMES], expected[2][FRAMES], output[2][FRAMES], previous[2][FRAMES];
    for (unsigned i = 0; i < FRAMES; ++i) {
        input[0][i] = .7f * (float)sin(i * .071); input[1][i] = .4f * (float)cos(i * .093);
    }
    /* Several controls change at one offset; enable and bypass exercise different histories. */
    const unsigned time[] = {7,7,7,63,64,65,129,237,333,389,511,513,769,1025,1537,2049};
    const unsigned index[] = {1,2,4,5,11,13,0,0,14,14,6,7,8,9,10,12};
    const float value[] = {.24f,4300,.63f,.8f,31,.41f,0,1,1,0,0,1,0,0,0,-.53f};
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
        for (unsigned warm=0;warm<16;++warm)
            assert(p->process(p, &warmup) != CLAP_PROCESS_ERROR);
        p->reset(p);
        retro_space *core = retro_space_create(); assert(core);
        float current[PARAM_COUNT]; memcpy(current, settings, sizeof(current));
        retro_space_params cp = core_params(current);
        assert(retro_space_set(core, &cp) && retro_space_prepare(core, rate));
        retro_space_reset(core);
        unsigned first = 0, change = 0;
        while (change < CHANGES) {
            unsigned last = time[change]; core_range(core, current, input, expected, first, last);
            do { current[index[change]] = value[change]; ++change; }
            while (change < CHANGES && time[change] == last);
            cp = core_params(current); assert(retro_space_set(core, &cp)); first = last;
        }
        core_range(core, current, input, expected, first, FRAMES);
        retro_space_destroy(core);
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
            /* Equal-offset edits represent one settled parameter state. */
            if(pass) for(unsigned first=0;first<e.count;) {
                unsigned last=first+1;
                while(last<e.count && e.events[last].header.time==e.events[first].header.time) ++last;
                for(unsigned i=first,j=last-1;i<j;++i,--j) {
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
        assert(memcmp(output, expected, sizeof(output)) == 0);
        assert(memcmp(output, input, sizeof(output)) != 0);
        if (pass) assert(memcmp(output, previous, sizeof(output)) == 0);
        else memcpy(previous, output, sizeof(output));
        p->stop_processing(p); p->deactivate(p);
    }
}

static void check_invalid_events(const clap_plugin_t *p) {
    const clap_plugin_params_t *params=p->get_extension(p,CLAP_EXT_PARAMS);
    set_values(p,defaults);
    for(unsigned i=0;i<PARAM_COUNT;++i) {
        clap_param_info_t info;assert(params->get_info(p,i,&info));
        double invalid[]={NAN,INFINITY,info.min_value-1,info.max_value+1};
        for(unsigned j=0;j<4;++j) {
            events e={.count=1};e.events[0]=make_event(i,invalid[j],0);
            clap_input_events_t in={.ctx=&e,.size=event_count,.get=event_get};
            params->flush(p,&in,NULL);check_values(p,defaults);
            double parsed;char text[64];
            assert(!params->value_to_text(p,info.id,invalid[j],text,sizeof(text)));
            assert(!params->text_to_value(p,info.id,j==0?"nan":j==1?"inf":j==2?"-999":"99999",&parsed));
        }
        if(info.flags&CLAP_PARAM_IS_STEPPED) {
            events e={.count=1};e.events[0]=make_event(i,.5,0);
            clap_input_events_t in={.ctx=&e,.size=event_count,.get=event_get};
            params->flush(p,&in,NULL);check_values(p,defaults);
        }
    }
    events e={.count=1};e.events[0]=make_event(99,0,0);
    clap_input_events_t in={.ctx=&e,.size=event_count,.get=event_get};
    params->flush(p,&in,NULL);check_values(p,defaults);
}
static void invalidate_field(retro_space_params *p,unsigned index,float value) {
    float *fields[]={&p->enable,&p->decay,&p->dampen_hz,&p->soften_hz,&p->level,&p->stereo_detune,
        &p->stereo_mode,&p->dual_mono,&p->feedbacks,&p->allpass,&p->reverb_mode,
        &p->pre_delay_ms,&p->pre_wobble_noise,&p->pre_wobble_amount};
    assert(index<14);*fields[index]=value;
}
static void check_core_validation(void) {
    retro_space *a=retro_space_create(),*b=retro_space_create();assert(a&&b);
    retro_space_params initial=core_params(settings);
    assert(retro_space_set(a,&initial)&&retro_space_set(b,&initial));
    assert(retro_space_prepare(a,48000)&&retro_space_prepare(b,48000));
    assert(!retro_space_set(a,NULL)&&!retro_space_set(NULL,&initial));
    assert(!retro_space_prepare(NULL,48000));
    /* Warm populated delay state, then verify rejected updates preserve it. */
    for(unsigned block=0;block<8;++block) {
        float al[64],ar[64],bl[64],br[64];
        for(unsigned i=0;i<64;++i) {al[i]=(float)sin((block*64+i)*.1);ar[i]=(float)cos((block*64+i)*.13);}
        memcpy(bl,al,sizeof(al));memcpy(br,ar,sizeof(ar));
        retro_space_process(a,al,ar,64);retro_space_process(b,bl,br,64);
        assert(!memcmp(al,bl,sizeof(al))&&!memcmp(ar,br,sizeof(ar)));
        const float rates[]={NAN,INFINITY,-1,999,1536001};
        for(unsigned i=0;i<5;++i) assert(!retro_space_prepare(a,rates[i]));
        for(unsigned field=0;field<14;++field) {
            const float lower[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-2,-1};
            const float upper[]={2,2,22051,22051,2,2,2,2,2,2,2,101,2,2};
            const float bad[]={NAN,INFINITY,lower[field],upper[field]};
            for(unsigned j=0;j<4;++j) {
                retro_space_params invalid=initial;invalidate_field(&invalid,field,bad[j]);
                assert(!retro_space_set(a,&invalid));
            }
            if(field==0||(field>=6&&field<=10)) {
                retro_space_params invalid=initial;invalidate_field(&invalid,field,.5f);
                assert(!retro_space_set(a,&invalid));
            }
        }
    }
    retro_space_destroy(a);retro_space_destroy(b);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    const clap_plugin_entry_t *entry = dlsym(library, "clap_entry"); assert(entry && entry->init(argv[1]));
    const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    assert(factory && factory->get_plugin_count(factory) == 1);
    const clap_plugin_descriptor_t *descriptor = factory->get_plugin_descriptor(factory, 0);
    assert(descriptor && !strcmp(descriptor->id, "local.retro-study.space"));
    assert(!strcmp(descriptor->version,"0.1.0"));
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
        assert(params->get_info(p, i, &info) && info.id == 5000 + i);
        assert(info.default_value == (double)defaults[i]);
        assert(params->value_to_text(p, info.id, info.default_value, text, sizeof(text)));
        assert(params->text_to_value(p, info.id, text, &parsed));
        assert(fabs(parsed - info.default_value) < 1e-7);
        /* Formatting/parsing also accepts fractional preview values for stepped
           controls; publication still enforces their integer domain. */
        double preview=info.min_value+(info.max_value-info.min_value)*.0101010101;
        assert(params->value_to_text(p,info.id,preview,text,sizeof(text)));
        assert(params->text_to_value(p,info.id,text,&parsed));
        assert(fabs(parsed-preview)<1e-6*(1+fabs(preview)));
    }
    const clap_plugin_t *other=factory->create_plugin(factory,&host,descriptor->id);
    assert(other && other->init(other));set_values(other,settings);
    check_values(p,defaults);check_values(other,settings);other->destroy(other);
    check_values(p, defaults); check_invalid_events(p); check_state(p); check_core_validation();
    assert(!p->activate(p, 999, 1, MAX_BLOCK));
    assert(!p->activate(p, 1536001, 1, MAX_BLOCK));
    const float rates[] = {48000, 96000, 1000, 384000, 44100, 1536000};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) check_render(p, rates[i]);
    p->destroy(p); entry->deinit(); assert(dlclose(library) == 0);
    puts("Space CLAP ABI, state validation, reset, in-place partitions and sample-offset automation passed");
    return 0;
}
