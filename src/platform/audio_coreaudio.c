/*
 * macOS audio output: Core Audio's AudioQueue (AudioToolbox framework).
 *
 * AudioQueue pulls audio from its own real-time thread through a callback,
 * while Bronto mixes on the game thread. The two meet in a lock-free
 * single-producer/single-consumer ring (br_audio_ring): the game thread
 * keeps the ring topped up to the target latency, the callback drains it
 * and plays silence on underrun - it never blocks or allocates.
 *
 * Included by platform_macos.c. Link: -framework AudioToolbox.
 */
#include <AudioToolbox/AudioQueue.h>

#define CA_BUFFERS 3
#define CA_BUFFER_FRAMES 512u   /* ~11.6 ms each at 44.1 kHz */

static struct {
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[CA_BUFFERS];
    br_audio_ring ring;
    uint32_t target;            /* frames to keep in the ring */
    bool open;
} CA;

/* Runs on Core Audio's thread: only touches the ring and the buffer. */
static void ca_callback(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    uint32_t frames = buffer->mAudioDataBytesCapacity / 4u;
    int16_t *out = (int16_t *)buffer->mAudioData;
    uint32_t got = br_audio_ring_read(&CA.ring, out, frames);
    BR_UNUSED(user);
    if (got < frames) memset(out + (size_t)got * 2u, 0, (size_t)(frames - got) * 4u);
    buffer->mAudioDataByteSize = frames * 4u;
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static bool ca_open(int rate, int latency_ms) {
    AudioStreamBasicDescription fmt;
    OSStatus st;
    int i;
    uint32_t latency_frames;
    memset(&CA, 0, sizeof(CA));
    if (rate <= 0) return false;
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate = (Float64)rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    fmt.mBytesPerPacket = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;

    latency_frames = (uint32_t)((double)rate * (double)(latency_ms > 0 ? latency_ms : 75) / 1000.0);
    /* The queue's own buffers already hold CA_BUFFERS blocks of audio. */
    CA.target = latency_frames > CA_BUFFERS * CA_BUFFER_FRAMES + 1024u
                    ? latency_frames - CA_BUFFERS * CA_BUFFER_FRAMES : 1024u;
    if (!br_audio_ring_init(&CA.ring, CA.target * 2u)) return false;

    st = AudioQueueNewOutput(&fmt, ca_callback, NULL, NULL, NULL, 0, &CA.queue);
    if (st != noErr) {
        BR_WARN("audio: AudioQueueNewOutput failed (%d) - running silent", (int)st);
        br_audio_ring_destroy(&CA.ring);
        return false;
    }
    for (i = 0; i < CA_BUFFERS; ++i) {
        st = AudioQueueAllocateBuffer(CA.queue, CA_BUFFER_FRAMES * 4u, &CA.buffers[i]);
        if (st != noErr) break;
        ca_callback(NULL, CA.queue, CA.buffers[i]);   /* prime with silence */
    }
    if (st == noErr) st = AudioQueueStart(CA.queue, NULL);
    if (st != noErr) {
        BR_WARN("audio: could not start AudioQueue (%d) - running silent", (int)st);
        AudioQueueDispose(CA.queue, true);
        br_audio_ring_destroy(&CA.ring);
        return false;
    }
    CA.open = true;
    BR_INFO("audio: Core Audio, %d Hz, ~%u ms buffering", rate,
            (unsigned)((CA.target + CA_BUFFERS * CA_BUFFER_FRAMES) * 1000u / (uint32_t)rate));
    return true;
}

static void ca_close(void) {
    if (!CA.open) return;
    AudioQueueStop(CA.queue, true);      /* waits for the callback thread */
    AudioQueueDispose(CA.queue, true);
    br_audio_ring_destroy(&CA.ring);
    memset(&CA, 0, sizeof(CA));
}

static uint32_t ca_wanted(void) {
    uint32_t avail;
    if (!CA.open) return 0;
    avail = br_audio_ring_available(&CA.ring);
    if (avail >= CA.target) return 0;
    return BR_MIN(CA.target - avail, br_audio_ring_space(&CA.ring));
}

static void ca_submit(const int16_t *frames, uint32_t count) {
    if (CA.open) br_audio_ring_write(&CA.ring, frames, count);
}
