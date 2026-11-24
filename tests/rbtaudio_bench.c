// Numbers for Rabbittus Audio's mixer (include/audio/rbtaudio.h), in Release: one
// step's rbtaudio_mixer_update() and one render of 512 frames, stereo at
// 48 kHz, with 32, 256 and 1,024 looping mono voices that move every step.
// The numbers go into RBTAUDIO_TODO.md (item 16), beside each machine's.
//
// Not a ctest: it times, and a time is no pass or fail. Run it by hand,
// Release, with nothing else running: rbtaudio_bench
//
// Each voice count is run three times: placed by angles
// (rbtaudio_voice_set_direction() for every voice each step, the caller doing
// the maths), placed in the world and still (rbtaudio_mixer_set_emitters()
// for every voice, one listener, the mixer's update doing the maths), and
// placed in the world and moving, so doppler resamples every voice.
//
// Then streamed sounds: 8 and 32 looping voices of
// tests/data/rbtaudio/stream.flac (2 s of stereo at 48 kHz), each decoding
// into its own ring, decoded inside the update (no scheduler). A round is one
// update and the four renders it feeds, timed two ways:
// - one thread: the update, then the four renders, in turn;
// - two threads: the four renders on a Rabbittus Jobs pool's worker, as an
//   audio callback runs, while the update runs on this thread, then waited
//   for. This is the case the stream records' padding is for: the render
//   writes a stream's `read` while the decoding writes its `written` and the
//   update reads both. The rounds whose renders ran on the worker are
//   counted, so a run that overlapped nothing says so.
// Each prints the render's and the update's median, minimum and maximum.
// The data folder is the first argument, else the source tree's.
//
// Written in C, as the tests of a C header are.

#include "audio/rbtaudio.h"
#include "jobs/rbtjobs.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FRAMES 512
#define STEPS 400
#define PI 3.14159265358979323846

static double now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static double median(double *values, int count)
{
    qsort(values, (size_t)count, sizeof(double), compare_doubles);
    return values[count / 2];
}

static rbtaudio_mixer make_mixer(uint32_t voices, bool world)
{
    static const CRAUDIO_CHANNEL_POSITION stereo[2] = {CRAUDIO_CHANNEL_POSITION_FL, CRAUDIO_CHANNEL_POSITION_FR};
    RBTAUDIO_SPATIAL_DESC spatial;
    memset(&spatial, 0, sizeof spatial);
    spatial.base.type = RBT_STRUCT_TYPE_AUDIO_SPATIAL_DESC;
    spatial.handedness = RBTAUDIO_HANDEDNESS_RIGHT;
    spatial.max_listeners = 1;
    spatial.max_attenuations = 1;
    spatial.speed_of_sound = 343.0f;

    RBTAUDIO_MIXER_DESC desc;
    memset(&desc, 0, sizeof desc);
    desc.base.type = RBT_STRUCT_TYPE_AUDIO_MIXER_DESC;
    desc.base.next = world ? &spatial.base : NULL;
    desc.format.sample_format = CRAUDIO_SAMPLE_FORMAT_F32;
    desc.format.sample_rate = 48000;
    desc.format.channel_count = 2;
    desc.format.interleaved = true;
    desc.format.positions = stereo;
    desc.max_frames = FRAMES;
    desc.max_voices = voices;
    desc.max_real_voices = voices;
    desc.max_fading_voices = 8;
    desc.transition_fade_us = 5000;
    desc.max_sounds = 1;
    desc.max_buses = 1;
    desc.queue_capacity = voices;
    desc.resampler = RBTAUDIO_RESAMPLER_LINEAR;
    desc.gain = 1.0f;
    desc.limiter = RBTAUDIO_LIMITER_OFF;
    rbtaudio_mixer mixer = NULL;
    if (rbtaudio_mixer_create(&desc, &mixer, NULL) != RBT_RESULT_OK)
        return NULL;
    return mixer;
}

static void run(uint32_t voices, bool world, bool moving)
{
    rbtaudio_mixer mixer = make_mixer(voices, world);
    if (!mixer)
    {
        printf("mixer_create failed\n");
        exit(1);
    }
    // A second of a 440 Hz sine, looped.
    static float samples[48000];
    for (int i = 0; i < 48000; i++)
        samples[i] = 0.1f * (float)sin(2.0 * PI * 440.0 * i / 48000.0);
    RBTAUDIO_SOUND_DESC sd;
    memset(&sd, 0, sizeof sd);
    sd.base.type = RBT_STRUCT_TYPE_AUDIO_SOUND_DESC;
    sd.samples = samples;
    sd.frame_count = 48000;
    sd.channel_count = 1;
    sd.sample_rate = 48000;
    sd.virtual_behavior = RBTAUDIO_VIRTUAL_CONTINUE;
    RBTAUDIO_SOUND sound = 0;
    rbtaudio_sound_create(mixer, &sd, &sound, NULL);

    RBTAUDIO_ATTENUATION attenuation = 0;
    if (world)
    {
        RBTAUDIO_ATTENUATION_DESC ad;
        memset(&ad, 0, sizeof ad);
        ad.base.type = RBT_STRUCT_TYPE_AUDIO_ATTENUATION_DESC;
        ad.rolloff = RBTAUDIO_ROLLOFF_INVERSE;
        ad.min_distance = 1.0f;
        ad.max_distance = 100.0f;
        ad.rolloff_factor = 1.0f;
        ad.cone_inner_angle = (float)PI;
        ad.cone_outer_angle = (float)(1.5 * PI);
        ad.cone_outer_gain = 0.25f;
        ad.doppler_factor = 1.0f;
        rbtaudio_attenuation_create(mixer, &ad, &attenuation, NULL);
        RBTAUDIO_LISTENER listener = {{0, 0, 0}, {0, 0, 0}, {0, 0, -1}, {0, 1, 0}};
        rbtaudio_mixer_set_listeners(mixer, &listener, 1, NULL);
    }

    RBTAUDIO_VOICE *ids = (RBTAUDIO_VOICE *)malloc(voices * sizeof(RBTAUDIO_VOICE));
    RBTAUDIO_EMITTER *emitters = (RBTAUDIO_EMITTER *)malloc(voices * sizeof(RBTAUDIO_EMITTER));
    for (uint32_t i = 0; i < voices; i++)
    {
        RBTAUDIO_EMITTER e = {{10.0 * cos(i), 0.0, 10.0 * sin(i)}, {moving ? 1.0f : 0.0f, 0, 0}, {0, 0, 1}};
        emitters[i] = e;
        RBTAUDIO_PLAY_DESC pd;
        memset(&pd, 0, sizeof pd);
        pd.base.type = RBT_STRUCT_TYPE_AUDIO_PLAY_DESC;
        pd.gain = 1.0f;
        pd.rate = 1.0f;
        pd.loop = true;
        pd.placement = world ? RBTAUDIO_PLACEMENT_WORLD : RBTAUDIO_PLACEMENT_ANGLES;
        pd.emitter = world ? &emitters[i] : NULL;
        pd.attenuation = attenuation;
        rbtaudio_play(mixer, sound, &pd, &ids[i]);
    }
    static float out[FRAMES * 2];
    rbtaudio_mixer_update(mixer, NULL);
    rbtaudio_mixer_render(mixer, out, FRAMES, NULL);
    rbtaudio_mixer_update(mixer, NULL);
    rbtaudio_mixer_render(mixer, out, FRAMES, NULL);

    static double update_times[STEPS], render_times[STEPS];
    for (int step = 0; step < STEPS; step++)
    {
        double t0 = now_seconds();
        double turn = step * 0.01;
        if (world)
        {
            for (uint32_t i = 0; i < voices; i++)
            {
                emitters[i].position.x = 10.0 * cos(i + turn);
                emitters[i].position.z = 10.0 * sin(i + turn);
            }
            rbtaudio_mixer_set_emitters(mixer, ids, emitters, voices, NULL);
        }
        else
        {
            for (uint32_t i = 0; i < voices; i++)
            {
                RBTAUDIO_DIRECTION d = {fmod(i + turn, 2.0 * PI) - PI, 0.0};
                rbtaudio_voice_set_direction(mixer, ids[i], &d);
            }
        }
        rbtaudio_mixer_update(mixer, NULL);
        double t1 = now_seconds();
        rbtaudio_mixer_render(mixer, out, FRAMES, NULL);
        double t2 = now_seconds();
        update_times[step] = (t1 - t0) * 1e6;
        render_times[step] = (t2 - t1) * 1e6;
    }
    printf("%-13s %5u voices: step (placing + update) %8.1f us, render of %d frames %8.1f us (medians of %d)\n",
           world ? (moving ? "world, moving" : "world, still") : "angles", voices, median(update_times, STEPS), FRAMES, median(render_times, STEPS), STEPS);
    free(ids);
    free(emitters);
    rbtaudio_mixer_destroy(mixer);
}

/* ---- Streamed sounds ---- */

#define ROUNDS 400
#define RENDERS_PER_ROUND 4

typedef struct stream_file
{
    unsigned char *bytes;
    uint64_t size;
} stream_file;

static bool stream_file_read(void *user, uint64_t offset, void *buffer, uint64_t count)
{
    const stream_file *file = (const stream_file *)user;
    if (offset > file->size || count > file->size - offset)
        return false;
    memcpy(buffer, file->bytes + offset, (size_t)count);
    return true;
}

static bool stream_file_load(stream_file *file, const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    file->bytes = (unsigned char *)malloc(size > 0 ? (size_t)size : 1);
    bool read = file->bytes && size > 0 && fread(file->bytes, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    file->size = read ? (uint64_t)size : 0;
    return read;
}

typedef struct render_round
{
    rbtaudio_mixer mixer;
    float *out;
    double *times; // RENDERS_PER_ROUND of them, this round's
    uint32_t worker_index;
} render_round;

static void render_task(uint32_t first_item, uint32_t end_item, uint32_t worker_index, void *task_data)
{
    (void)first_item;
    (void)end_item;
    render_round *round = (render_round *)task_data;
    round->worker_index = worker_index;
    for (int i = 0; i < RENDERS_PER_ROUND; i++)
    {
        double t0 = now_seconds();
        rbtaudio_mixer_render(round->mixer, round->out, FRAMES, NULL);
        round->times[i] = (now_seconds() - t0) * 1e6;
    }
}

static void print_spread(const char *what, double *values, int count)
{
    double low = values[0], high = values[0];
    for (int i = 1; i < count; i++)
    {
        low = values[i] < low ? values[i] : low;
        high = values[i] > high ? values[i] : high;
    }
    printf("  %-28s median %8.1f us, min %8.1f, max %8.1f (%d)\n", what, median(values, count), low, high, count);
}

static void run_streams(const stream_file *file, uint32_t voices, const RBTJOBS_SCHEDULER *pool)
{
    static const CRAUDIO_CHANNEL_POSITION stereo[2] = {CRAUDIO_CHANNEL_POSITION_FL, CRAUDIO_CHANNEL_POSITION_FR};
    RBTAUDIO_MIXER_DESC desc;
    memset(&desc, 0, sizeof desc);
    desc.base.type = RBT_STRUCT_TYPE_AUDIO_MIXER_DESC;
    desc.format.sample_format = CRAUDIO_SAMPLE_FORMAT_F32;
    desc.format.sample_rate = 48000;
    desc.format.channel_count = 2;
    desc.format.interleaved = true;
    desc.format.positions = stereo;
    desc.max_frames = FRAMES;
    desc.max_voices = voices;
    desc.max_real_voices = voices;
    desc.max_fading_voices = 8;
    desc.transition_fade_us = 5000;
    desc.max_sounds = 1;
    desc.max_buses = 1;
    desc.queue_capacity = voices;
    desc.resampler = RBTAUDIO_RESAMPLER_LINEAR;
    desc.gain = 1.0f;
    desc.limiter = RBTAUDIO_LIMITER_OFF;
    desc.max_streams = voices;
    rbtaudio_mixer mixer = NULL;
    if (rbtaudio_mixer_create(&desc, &mixer, NULL) != RBT_RESULT_OK)
    {
        printf("mixer_create failed\n");
        exit(1);
    }
    RBTAUDIO_SOUND_DESC sd;
    memset(&sd, 0, sizeof sd);
    sd.base.type = RBT_STRUCT_TYPE_AUDIO_SOUND_DESC;
    sd.virtual_behavior = RBTAUDIO_VIRTUAL_CONTINUE;
    sd.stream_codec = RBTAUDIO_STREAM_CODEC_FLAC;
    sd.stream_source.user = (void *)file;
    sd.stream_source.size = file->size;
    sd.stream_source.read = stream_file_read;
    sd.stream_ring_us = 500000;
    RBTAUDIO_SOUND sound = 0;
    if (rbtaudio_sound_create(mixer, &sd, &sound, NULL) != RBT_RESULT_OK)
    {
        printf("sound_create (streamed) failed\n");
        exit(1);
    }
    for (uint32_t i = 0; i < voices; i++)
    {
        RBTAUDIO_PLAY_DESC pd;
        memset(&pd, 0, sizeof pd);
        pd.base.type = RBT_STRUCT_TYPE_AUDIO_PLAY_DESC;
        pd.gain = 1.0f / (float)voices;
        pd.rate = 1.0f;
        pd.loop = true;
        pd.placement = RBTAUDIO_PLACEMENT_ANGLES;
        RBTAUDIO_VOICE voice = 0;
        rbtaudio_play(mixer, sound, &pd, &voice);
    }
    static float out[FRAMES * 2];
    // Every voice real and its stream open and full before the clock starts.
    for (int i = 0; i < 8; i++)
    {
        rbtaudio_mixer_update(mixer, NULL);
        rbtaudio_mixer_render(mixer, out, FRAMES, NULL);
        rbtaudio_mixer_collect(mixer);
    }

    static double update_times[ROUNDS], render_times[ROUNDS * RENDERS_PER_ROUND];
    int on_worker = 0;
    for (int r = 0; r < ROUNDS; r++)
    {
        render_round round = {mixer, out, &render_times[r * RENDERS_PER_ROUND], 0};
        void *task = NULL;
        if (pool)
            task = pool->enqueue(render_task, 1, 1, &round, pool->user);
        double t0 = now_seconds();
        rbtaudio_mixer_update(mixer, NULL);
        update_times[r] = (now_seconds() - t0) * 1e6;
        if (pool)
        {
            if (task)
                pool->wait(task, pool->user);
        }
        else
        {
            render_task(0, 1, 0, &round);
        }
        on_worker += round.worker_index != 0;
        rbtaudio_mixer_collect(mixer);
    }
    uint64_t underruns = rbtaudio_sound_get_underrun_frames(mixer, sound);
    if (pool)
        printf("streamed, two threads %4u voices (renders on the worker in %d of %d rounds; %llu frames underrun):\n", voices,
               on_worker, ROUNDS, (unsigned long long)underruns);
    else
        printf("streamed, one thread  %4u voices (%llu frames underrun):\n", voices, (unsigned long long)underruns);
    print_spread("render of 512 frames", render_times, ROUNDS * RENDERS_PER_ROUND);
    print_spread("update (with decoding)", update_times, ROUNDS);
    rbtaudio_mixer_destroy(mixer);
}

int main(int argc, char **argv)
{
    static const uint32_t counts[3] = {32, 256, 1024};
    for (int i = 0; i < 3; i++)
    {
        run(counts[i], false, false);
        run(counts[i], true, false);
        run(counts[i], true, true);
    }

    const char *dir = argc > 1 ? argv[1] : RBTAUDIO_BENCH_DATA;
    stream_file file = {NULL, 0};
    if (!stream_file_load(&file, dir, "stream.flac"))
    {
        printf("can't read %s/stream.flac: streamed cases skipped\n", dir);
        return 1;
    }
    RBTJOBS_POOL_DESC pd;
    memset(&pd, 0, sizeof pd);
    pd.base.type = RBT_STRUCT_TYPE_JOBS_POOL_DESC;
    pd.thread_count = 1;
    pd.max_tasks = 4;
    pd.debug_name = "rbtaudio_bench render";
    rbtjobs_pool pool = NULL;
    RBTJOBS_SCHEDULER scheduler;
    if (rbtjobs_pool_create(&pd, &pool, NULL) != RBT_RESULT_OK || rbtjobs_pool_get_scheduler(pool, &scheduler) != RBT_RESULT_OK)
    {
        printf("pool_create failed\n");
        return 1;
    }
    static const uint32_t stream_counts[2] = {8, 32};
    for (int i = 0; i < 2; i++)
    {
        run_streams(&file, stream_counts[i], NULL);
        run_streams(&file, stream_counts[i], &scheduler);
    }
    rbtjobs_pool_destroy(pool);
    free(file.bytes);
    return 0;
}
