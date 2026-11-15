// unpack_bench.c: pack v2's codec on another machine (15 Nov, menu-stage session). Times the game's own decoders
// (libdeflate 1.26's raw inflate, as lib/MCInflate calls it, and zstd 1.5.7's decompressor, as lib/MCZstd calls it)
// on one thread over the samples codec_bench export wrote, so the laptop's i5 unpacks the same bytes as the M4 Pro.
// Built with the game's own sources: build_unpack.sh here, the fork's codec-bench workflow on Windows.
//
//   unpack_bench <sample file>...      (prints a table a file, as codec_bench bench does)
//
// A sample file, little-endian: "MCCB1\0\0\0"; the kind (u32 length, its bytes); u32 entries, each u32 packet,
// u32 real, u32 stored and its stored bytes (raw deflate; the bytes themselves when stored equals real); u32
// candidates, each a 16-byte name, u32 packets, each u32 real, u32 stored and a zstd frame carrying its checksum.
// Every candidate's result is checked against today's, byte for byte, before it's timed; each zstd candidate is timed
// again with its checksums ignored, for the checksum's share.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdeflate.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

enum { NAME_FIELD = 16, BENCH_MIN_NS = 400000000, BENCH_BEST_OF = 3, MOST_UNITS = 10000000 };
static const char Magic[8] = {'M', 'C', 'C', 'B', '1', 0, 0, 0};

static void Fail(const char* what, const char* on)
{
    fprintf(stderr, "unpack_bench: %s: %s (%s)\n", what, on, errno ? strerror(errno) : "no system error");
    exit(1);
}

static void* Grab(size_t bytes, const char* forWhat)
{
    void* p = malloc(bytes ? bytes : 1);
    if (!p)
        Fail("out of memory", forWhat);
    return p;
}

static uint64_t NowNs(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (uint64_t)((double)now.QuadPart * 1e9 / (double)frequency.QuadPart);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
#endif
}

typedef struct
{
    const unsigned char* at;
    const unsigned char* end;
    const char* path;
} Reader;

static uint32_t TakeU32(Reader* r)
{
    if (r->end - r->at < 4)
        Fail("the sample ends early", r->path);
    const unsigned char* p = r->at;
    r->at += 4;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const unsigned char* TakeBytes(Reader* r, size_t n)
{
    if ((size_t)(r->end - r->at) < n)
        Fail("the sample ends early", r->path);
    const unsigned char* p = r->at;
    r->at += n;
    return p;
}

// One candidate's units (entries or packets): each one's stored bytes and real size.
typedef struct
{
    char name[NAME_FIELD + 8];
    int packets;  // a packet at a time, or today's entries a frame at a time
    uint32_t count;
    const unsigned char** stored;
    uint32_t* storedSizes;
    uint32_t* realSizes;
    uint64_t storedTotal;
} Units;

static Units TakeUnits(Reader* r, const char* name, int packets, int today)
{
    Units u = {{0}, packets, TakeU32(r), NULL, NULL, NULL, 0};
    snprintf(u.name, sizeof u.name, "%s", name);
    if (u.count > MOST_UNITS)
        Fail("a bad unit count", r->path);
    u.stored = Grab(sizeof(*u.stored) * u.count, name);
    u.storedSizes = Grab(sizeof(uint32_t) * u.count, name);
    u.realSizes = Grab(sizeof(uint32_t) * u.count, name);
    for (uint32_t i = 0; i < u.count; i++)
    {
        if (today)
            TakeU32(r);  // its packet: today's entries are timed a frame at a time
        u.realSizes[i] = TakeU32(r);
        u.storedSizes[i] = TakeU32(r);
        u.stored[i] = TakeBytes(r, u.storedSizes[i]);
        u.storedTotal += u.storedSizes[i];
    }
    return u;
}

typedef struct
{
    struct libdeflate_decompressor* inflater;
    ZSTD_DCtx* zstd;
} Decoders;

// One unit back into <out>: today's as MCHdArt does it (inflated only when its sizes differ), zstd's as MCZstd does
// (one whole frame, its checksum checked as it unpacks). False when it comes out wrong.
static int UnpackUnit(Decoders* d, const Units* u, uint32_t i, unsigned char* out)
{
    const unsigned char* in = u->stored[i];
    size_t n = u->storedSizes[i], real = u->realSizes[i];
    if (!u->packets)
    {
        if (n == real)
        {
            memcpy(out, in, real);
            return 1;
        }
        size_t got = 0;
        return libdeflate_deflate_decompress(d->inflater, in, n, out, real, &got) == LIBDEFLATE_SUCCESS &&
               got == real;
    }
    size_t frame = ZSTD_findFrameCompressedSize(in, n);
    if (ZSTD_isError(frame) || frame != n)
        return 0;
    size_t got = ZSTD_decompressDCtx(d->zstd, out, real, in, n);
    return !ZSTD_isError(got) && got == real;
}

static void UnpackAll(Decoders* d, const Units* u, unsigned char* out, uint64_t total)
{
    uint64_t at = 0;
    for (uint32_t i = 0; i < u->count; i++)
    {
        if (at + u->realSizes[i] > total || !UnpackUnit(d, u, i, out + at))
            Fail("came back wrong", u->name);
        at += u->realSizes[i];
    }
    if (at != total)
        Fail("unpacks to another size", u->name);
}

static uint64_t RealTotal(const Units* u)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < u->count; i++)
        total += u->realSizes[i];
    return total;
}

// The best of a few rounds, each at least BENCH_MIN_NS of whole passes over the sample.
static void Time(Decoders* d, const char* kind, const Units* u, unsigned char* out, uint64_t total)
{
    uint64_t best = UINT64_MAX;
    for (int round = 0; round < BENCH_BEST_OF; round++)
    {
        uint64_t start = NowNs(), took = 0;
        uint64_t repeats = 0;
        do
        {
            UnpackAll(d, u, out, total);
            repeats++;
            took = NowNs() - start;
        } while (took < BENCH_MIN_NS);
        if (took / repeats < best)
            best = took / repeats;
    }
    printf("%s\t%s\t%s\t%u\t%.2f\t%.0f\t%.1f\n", kind, u->name, u->packets ? "packet" : "frame", u->count,
           u->storedTotal / 1e6, total / (best / 1e9) / 1e6, best / 1e3 / u->count);
    fflush(stdout);
}

static unsigned char* ReadWhole(const char* path, size_t* size)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        Fail("can't open", path);
    if (fseek(f, 0, SEEK_END))
        Fail("can't seek", path);
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET))
        Fail("can't size", path);
    unsigned char* bytes = Grab((size_t)length, path);
    if (fread(bytes, 1, (size_t)length, f) != (size_t)length)
        Fail("can't read", path);
    fclose(f);
    *size = (size_t)length;
    return bytes;
}

static void BenchFile(Decoders* d, const char* path)
{
    size_t size = 0;
    unsigned char* bytes = ReadWhole(path, &size);
    Reader r = {bytes, bytes + size, path};
    if (memcmp(TakeBytes(&r, sizeof Magic), Magic, sizeof Magic))
        Fail("not a sample file", path);
    uint32_t kindLength = TakeU32(&r);
    if (kindLength > 64)
        Fail("a bad kind", path);
    char kind[65] = {0};
    memcpy(kind, TakeBytes(&r, kindLength), kindLength);
    Units today = TakeUnits(&r, "today", 0, 1);
    uint64_t total = RealTotal(&today);
    uint32_t candidates = TakeU32(&r);
    if (candidates > 16)
        Fail("a bad candidate count", path);
    Units* list = Grab(sizeof(Units) * (candidates + 1), path);
    list[0] = today;
    for (uint32_t k = 0; k < candidates; k++)
    {
        char name[NAME_FIELD + 1] = {0};
        memcpy(name, TakeBytes(&r, NAME_FIELD), NAME_FIELD);
        list[k + 1] = TakeUnits(&r, name, 1, 0);
    }
    if (r.at != r.end)
        Fail("bytes past the sample's end", path);
    printf("%s\t%u entries\t%u packets\t%.1f MB unpacked\n", kind, today.count, candidates ? list[1].count : 0,
           total / 1e6);
    printf("kind\tcandidate\tgrain\tunits\tstored MB\tunpack MB/s\tus a unit\n");
    // Today's unpacked bytes are the reference every candidate must give back exactly.
    unsigned char* reference = Grab((size_t)total, "reference");
    unsigned char* out = Grab((size_t)total, "unpacked");
    UnpackAll(d, &today, reference, total);
    for (uint32_t k = 0; k <= candidates; k++)
    {
        if (RealTotal(&list[k]) != total)
            Fail("a candidate's size isn't today's", list[k].name);
        UnpackAll(d, &list[k], out, total);
        if (memcmp(out, reference, (size_t)total))
            Fail("doesn't give back today's bytes", list[k].name);
        Time(d, kind, &list[k], out, total);
        if (!list[k].packets)
            continue;
        // The same frames with their checksums not checked (zstd's experimental parameter): the checksum's share.
        Units unchecked = list[k];
        snprintf(unchecked.name, sizeof unchecked.name, "%.*s-nocheck", (int)(strcspn(list[k].name, "+")),
                 list[k].name);
        if (ZSTD_isError(ZSTD_DCtx_setParameter(d->zstd, ZSTD_d_forceIgnoreChecksum, ZSTD_d_ignoreChecksum)))
            Fail("zstd won't ignore its checksums", unchecked.name);
        Time(d, kind, &unchecked, out, total);
        if (ZSTD_isError(ZSTD_DCtx_setParameter(d->zstd, ZSTD_d_forceIgnoreChecksum, ZSTD_d_validateChecksum)))
            Fail("zstd won't check its checksums again", unchecked.name);
    }
    for (uint32_t k = 0; k <= candidates; k++)
    {
        free((void*)list[k].stored);
        free(list[k].storedSizes);
        free(list[k].realSizes);
    }
    free(list);
    free(reference);
    free(out);
    free(bytes);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: unpack_bench <sample file>... (see the file's head)\n");
        return 2;
    }
#if defined(__clang__)
    printf("built by clang %d.%d", __clang_major__, __clang_minor__);
#elif defined(_MSC_VER)
    printf("built by MSVC %d", _MSC_VER);
#else
    printf("built by an unnamed compiler");
#endif
#if defined(__AVX2__)
    printf(", AVX2 on");
#endif
    printf(", zstd %s, libdeflate %s\n", ZSTD_versionString(), LIBDEFLATE_VERSION_STRING);
    Decoders d = {libdeflate_alloc_decompressor(), ZSTD_createDCtx()};
    if (!d.inflater || !d.zstd)
        Fail("can't make the decoders", argv[0]);
    for (int i = 1; i < argc; i++)
        BenchFile(&d, argv[i]);
    libdeflate_free_decompressor(d.inflater);
    ZSTD_freeDCtx(d.zstd);
    return 0;
}
