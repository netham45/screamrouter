#include <alsa/asoundlib.h>
#include <alsa/pcm_ioplug.h>
#include <alsa/pcm_external.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SOUND_GROUP_NAME
#define SOUND_GROUP_NAME "audio"
#endif

#define DEFAULT_RATE 48000U
#define DEFAULT_CHANNELS 2U
#define DEFAULT_BUFFER_FRAMES 4096U
#define DEFAULT_FORMAT SND_PCM_FORMAT_S16_LE

struct sr_runtime {
    snd_pcm_ioplug_t io;
    char name[64];
    char fifo_path[PATH_MAX];
    unsigned int channels;
    unsigned int rate;
    snd_pcm_format_t format;
    snd_pcm_uframes_t buffer_frames;
    int fifo_fd;
    int poll_fd;
    snd_pcm_uframes_t hw_ptr;
};

struct snd_dlsym_link *snd_dlsym_start __attribute__((visibility("default"))) = NULL;

static const char *device_dir_path(void)
{
    static char path[PATH_MAX];
    static bool initialized = false;
    if (!initialized) {
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        if (xdg && *xdg) {
            size_t len = strlen(xdg);
            if (len >= PATH_MAX)
                len = PATH_MAX - 1;
            while (len > 0 && xdg[len - 1] == '/')
                --len;
            if (len > 0) {
                snprintf(path, sizeof(path), "%.*s/screamrouter", (int)len, xdg);
            }
        }

        if (!path[0]) {
            unsigned int uid = (unsigned int)getuid();
            if (snprintf(path, sizeof(path), "/run/user/%u/screamrouter", uid) >= (int)sizeof(path))
                path[0] = '\0';
        }

        if (!path[0]) {
            snprintf(path, sizeof(path), "/var/run/screamrouter");
        }

        initialized = true;
    }
    return path;
}

static void ensure_device_dir(void)
{
    const char *device_dir = device_dir_path();
    struct stat st;
    if (stat(device_dir, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            goto cleanup;
        return;
    }

    if (mkdir(device_dir, 02770) < 0 && errno != EEXIST)
        return;

    struct group *grp = getgrnam(SOUND_GROUP_NAME);
    if (grp)
        chown(device_dir, -1, grp->gr_gid);

cleanup:
    static bool cleaned_once = false;
    if (!cleaned_once) {
        DIR *dir = opendir(device_dir);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir))) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                    continue;
                char path[PATH_MAX];
                if (snprintf(path, sizeof(path), "%s/%s", device_dir, de->d_name) >= (int)sizeof(path))
                    continue;
                unlink(path);
            }
            closedir(dir);
        }
        cleaned_once = true;
    }
}

static void maybe_assign_group(const char *path)
{
    struct group *grp = getgrnam(SOUND_GROUP_NAME);
    if (grp)
        chown(path, -1, grp->gr_gid);
}

static int ensure_fifo(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISFIFO(st.st_mode))
            return 0;
        errno = EEXIST;
        return -1;
    }
    if (mkfifo(path, 0660) < 0)
        return -1;
    maybe_assign_group(path);
    return 0;
}

static snd_pcm_format_t parse_format(const char *fmt)
{
    if (!fmt || !*fmt)
        return DEFAULT_FORMAT;
    snd_pcm_format_t format = snd_pcm_format_value(fmt);
    if (format == SND_PCM_FORMAT_UNKNOWN)
        return DEFAULT_FORMAT;
    return format;
}

static unsigned int parse_uint(const char *str, unsigned int fallback)
{
    if (!str || !*str)
        return fallback;
    unsigned long val = strtoul(str, NULL, 10);
    if (!val)
        return fallback;
    return (unsigned int)val;
}

static snd_pcm_uframes_t parse_frames(const char *str, snd_pcm_uframes_t fallback)
{
    if (!str || !*str)
        return fallback;
    unsigned long val = strtoul(str, NULL, 10);
    if (!val)
        return fallback;
    return (snd_pcm_uframes_t)val;
}

static bool extract_arg_string(snd_config_t *conf, const char *key, const char **val)
{
    snd_config_t *node;
    if (!conf)
        return false;
    if (snd_config_search(conf, key, &node) < 0)
        return false;
    if (snd_config_get_string(node, val) < 0)
        return false;
    return true;
}

static bool extract_device_name(const char *pcm_name,
                                snd_config_t *conf,
                                char *out,
                                size_t out_size)
{
    const char *conf_value = NULL;
    if (extract_arg_string(conf, "device", &conf_value) && conf_value && *conf_value) {
        snprintf(out, out_size, "%s", conf_value);
        return true;
    }

    if (pcm_name && *pcm_name) {
        const char *colon = strchr(pcm_name, ':');
        if (colon && *(colon + 1)) {
            snprintf(out, out_size, "%s", colon + 1);
            return true;
        }
        snprintf(out, out_size, "%s", pcm_name);
        return true;
    }

    return false;
}

static void sanitize_label(const char *src, char *dst, size_t dst_size)
{
    if (dst_size == 0)
        return;
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dst_size; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '_' || c == '-') {
            dst[di++] = tolower(c);
        } else {
            dst[di++] = '_';
        }
    }
    dst[di] = '\0';
}

static void format_name_lower(snd_pcm_format_t fmt, char *dst, size_t dst_size)
{
    const char *name = snd_pcm_format_name(fmt);
    if (!name)
        name = "UNKNOWN";
    sanitize_label(name, dst, dst_size);
}

static int sr_fifo_open(struct sr_runtime *rt, snd_pcm_stream_t stream)
{
    if (rt->fifo_fd >= 0) {
        close(rt->fifo_fd);
        rt->fifo_fd = -1;
    }

    if (stream == SND_PCM_STREAM_PLAYBACK) {
        int fd = open(rt->fifo_path, O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno == ENXIO)
                return 0; /* no consumer yet, drop silently */
            return -errno;
        }
        rt->fifo_fd = fd;
    } else {
        int fd = open(rt->fifo_path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            return -errno;
        rt->fifo_fd = fd;
    }
    return 0;
}

static int sr_runtime_close(snd_pcm_ioplug_t *io)
{
    struct sr_runtime *rt = io->private_data;
    if (!rt)
        return 0;

    if (rt->fifo_fd >= 0)
        close(rt->fifo_fd);
    if (rt->poll_fd >= 0 && rt->poll_fd != rt->fifo_fd)
        close(rt->poll_fd);
    unlink(rt->fifo_path);

    free(rt);
    return 0;
}

static int sr_runtime_start(snd_pcm_ioplug_t *io)
{
    (void)io;
    return 0;
}

static int sr_runtime_stop(snd_pcm_ioplug_t *io)
{
    (void)io;
    return 0;
}

static snd_pcm_sframes_t sr_runtime_pointer(snd_pcm_ioplug_t *io)
{
    struct sr_runtime *rt = io->private_data;
    return rt->hw_ptr % io->buffer_size;
}

static snd_pcm_sframes_t sr_runtime_transfer(snd_pcm_ioplug_t *io,
                                             const snd_pcm_channel_area_t *areas,
                                             snd_pcm_uframes_t offset,
                                             snd_pcm_uframes_t frames)
{
    struct sr_runtime *rt = io->private_data;
    const size_t frame_bytes = (snd_pcm_format_physical_width(rt->format) / 8) * rt->channels;

    if (frame_bytes == 0)
        return -EINVAL;

    if (io->stream == SND_PCM_STREAM_PLAYBACK) {
        for (snd_pcm_uframes_t f = 0; f < frames; ++f) {
            if (rt->fifo_fd < 0)
                sr_fifo_open(rt, io->stream);
            const uint8_t *src = (const uint8_t *)areas[0].addr + (offset + f) * frame_bytes;
            ssize_t written = (rt->fifo_fd >= 0) ? write(rt->fifo_fd, src, frame_bytes)
                                               : (ssize_t)frame_bytes;
            if (written < 0) {
                if (errno == EPIPE || errno == ENXIO)
                    sr_fifo_open(rt, io->stream);
                continue;
            }
        }
        rt->hw_ptr = (rt->hw_ptr + frames) % io->buffer_size;
        return frames;
    }

    uint8_t *tmp = alloca(frame_bytes);
    for (snd_pcm_uframes_t f = 0; f < frames; ++f) {
        if (rt->fifo_fd < 0) {
            if (sr_fifo_open(rt, io->stream) < 0)
                rt->fifo_fd = -1;
        }
        ssize_t r = rt->fifo_fd >= 0 ? read(rt->fifo_fd, tmp, frame_bytes) : 0;
        uint8_t *dst = (uint8_t *)areas[0].addr + (offset + f) * frame_bytes;
        if (r <= 0) {
            memset(dst, 0, frame_bytes);
            if (r < 0 && (errno == EPIPE || errno == ENXIO))
                sr_fifo_open(rt, io->stream);
        } else if ((size_t)r < frame_bytes) {
            memset(dst, 0, frame_bytes);
        } else {
            memcpy(dst, tmp, frame_bytes);
        }
    }
    rt->hw_ptr = (rt->hw_ptr + frames) % io->buffer_size;
    return frames;
}

static const snd_pcm_ioplug_callback_t sr_callbacks = {
    .close = sr_runtime_close,
    .start = sr_runtime_start,
    .stop = sr_runtime_stop,
    .pointer = sr_runtime_pointer,
    .transfer = sr_runtime_transfer,
};

static int screamrouter_pcm_open(snd_pcm_t **pcmp, const char *name,
                                 snd_config_t *root, snd_config_t *conf,
                                 snd_pcm_stream_t stream, int mode)
{
    (void)root;
    ensure_device_dir();

    char device_name[64] = {0};
    if (!extract_device_name(name, conf, device_name, sizeof(device_name)))
        return -EINVAL;

    struct sr_runtime *rt = calloc(1, sizeof(*rt));
    if (!rt)
        return -ENOMEM;

    snprintf(rt->name, sizeof(rt->name), "%s", device_name);
    rt->channels = DEFAULT_CHANNELS;
    rt->rate = DEFAULT_RATE;
    rt->buffer_frames = DEFAULT_BUFFER_FRAMES;
    rt->format = DEFAULT_FORMAT;
    rt->fifo_fd = -1;
    rt->poll_fd = -1;

    const char *value = NULL;
    if (extract_arg_string(conf, "channels", &value))
        rt->channels = parse_uint(value, DEFAULT_CHANNELS);
    if (extract_arg_string(conf, "rate", &value))
        rt->rate = parse_uint(value, DEFAULT_RATE);
    if (extract_arg_string(conf, "buffer", &value))
        rt->buffer_frames = parse_frames(value, DEFAULT_BUFFER_FRAMES);
    if (extract_arg_string(conf, "format", &value))
        rt->format = parse_format(value);

    const char *fifo_value = NULL;
    if (extract_arg_string(conf, "fifo", &fifo_value) && fifo_value && *fifo_value) {
        snprintf(rt->fifo_path, sizeof(rt->fifo_path), "%s", fifo_value);
    } else {
        char label[sizeof(rt->name)];
        sanitize_label(rt->name, label, sizeof(label));
        char fmt_label[32];
        format_name_lower(rt->format, fmt_label, sizeof(fmt_label));
        unsigned int width_bits = snd_pcm_format_physical_width(rt->format);
        if (!width_bits)
            width_bits = snd_pcm_format_width(rt->format);
        if (!width_bits)
            width_bits = 0;
        snprintf(rt->fifo_path, sizeof(rt->fifo_path), "%s/%s.%s.%uHz.%uch.%ubit.%s",
                 device_dir_path(),
                 stream == SND_PCM_STREAM_PLAYBACK ? "out" : "in",
                 label,
                 rt->rate,
                 rt->channels,
                 width_bits,
                 fmt_label);
    }

    if (ensure_fifo(rt->fifo_path) < 0) {
        free(rt);
        return -errno;
    }

    if (stream == SND_PCM_STREAM_PLAYBACK) {
        int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (efd < 0) {
            free(rt);
            return -errno;
        }
        rt->poll_fd = efd;
    }
    int fifo_status = sr_fifo_open(rt, stream);
    if (fifo_status < 0) {
        if (rt->poll_fd >= 0)
            close(rt->poll_fd);
        free(rt);
        return fifo_status;
    }

    snd_pcm_ioplug_t *io = &rt->io;
    memset(io, 0, sizeof(*io));
    io->version = SND_PCM_IOPLUG_VERSION;
    io->name = "Screamrouter";
    io->callback = &sr_callbacks;
    io->private_data = rt;
    io->stream = stream;
    io->poll_fd = (stream == SND_PCM_STREAM_PLAYBACK) ? rt->poll_fd : rt->fifo_fd;
    io->poll_events = (stream == SND_PCM_STREAM_PLAYBACK) ? POLLOUT : POLLIN;

    int err = snd_pcm_ioplug_create(io, name, stream, mode);
    if (err < 0) {
        sr_runtime_close(io);
        return err;
    }

    io->buffer_size = rt->buffer_frames;
    io->period_size = rt->buffer_frames ? rt->buffer_frames / 4 : DEFAULT_BUFFER_FRAMES / 4;
    if (!io->period_size)
        io->period_size = io->buffer_size;

    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS,
                                    rt->channels, rt->channels);
    unsigned int formats[1] = { (unsigned int)rt->format };
    snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
                                  1, formats);
    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
                                    rt->rate, rt->rate);

    unsigned int frame_bytes = (snd_pcm_format_physical_width(rt->format) / 8) * rt->channels;
    unsigned int buffer_bytes = rt->buffer_frames * frame_bytes;
    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES,
                                    buffer_bytes, buffer_bytes);
    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
                                    frame_bytes, buffer_bytes);
    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS, 2, 4);

    *pcmp = io->pcm;
    return 0;
}

int _snd_pcm_screamrouter_open(snd_pcm_t **pcmp, const char *name,
                               snd_config_t *root, snd_config_t *conf,
                               snd_pcm_stream_t stream, int mode)
{
    return screamrouter_pcm_open(pcmp, name, root, conf, stream, mode);
}

SND_DLSYM_BUILD_VERSION(_snd_pcm_screamrouter_open, SND_PCM_DLSYM_VERSION);
