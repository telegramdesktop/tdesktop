/* tinyasr: on-device speech recognition (streaming with pause / end-of-turn detection, or a whole file).
 * Audio: mono float, 16 kHz, [-1, 1]. Not thread-safe per object; models can serve several sessions (each call runs
 * on the caller's thread, or on more: see tinyasr_set_threads).
 * Returned strings are UTF-8 JSON owned by the object, valid until its next call.
 * Model files are checked when loaded: a corrupted file, or one for another version, makes tinyasr_load return NULL
 * with the reason; it never crashes. Every function accepts NULL for its object and then does nothing: it returns
 * NULL (pointers), 0 (tinyasr_chunk_ms) or -1 (tinyasr_file_progress). So a failed load needs no special path. */
#ifndef TINYASR_H
#define TINYASR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct tinyasr_models tinyasr_models;
typedef struct tinyasr_session tinyasr_session;
typedef struct tinyasr_file tinyasr_file;

/* Version of the library (its API and the model files it reads): 3. Reads TZF3 model files, which name their
 * languages, and the TZF1 / TZF2 files of version 2. */
uint32_t tinyasr_version(void);

/* Model files from memory (copied): the recognizer (.tzf, int4, uncompressed; with its end-of-turn head, else a
 * sentence ends after 1.6 s of silence) and silero_vad.onnx. NULL on failure, with the reason in err. */
tinyasr_models *tinyasr_load(const uint8_t *asr, size_t asr_len, const uint8_t *vad, size_t vad_len, char *err,
                             size_t err_len);
void tinyasr_free(tinyasr_models *m);
uint32_t tinyasr_chunk_ms(const tinyasr_models *m); /* 0 for NULL */
/* The model's languages: codes its file names ("en", "uz_cyrillic", ...), in its order; the library knows none of its
 * own. Strings valid while the models are loaded; 0 / NULL for NULL or i out of range. */
uint32_t tinyasr_lang_count(const tinyasr_models *m);
const char *tinyasr_lang(const tinyasr_models *m, uint32_t i);

/* Threads. By default every call runs on the thread that makes it. The bigger steps of a pass (matrix products,
 * attention heads, row blocks of a layer) can be split over more threads instead, with the same results to the bit for
 * any number of threads. Two ways, set on the models while no session or file of them is running (simplest: right
 * after tinyasr_load):
 *
 * tinyasr_set_threads(m, n): the library's own n - 1 extra threads (n = 0, 1: none). They start on the first parallel
 *   step, from the thread that makes that call, and inherit its CPU affinity (Linux, Android): pin that thread to the
 *   cores you want and the extra threads follow. Between steps they spin briefly (~0.1 ms), then sleep; tinyasr_free
 *   ends them.
 * tinyasr_set_parallel_for(m, parallel_for, user, n): your own pool runs the tasks; the library starts no thread.
 *   parallel_for(user, count, task, ctx) must call task(ctx, i) for every i in [0, count) - from any threads, in any
 *   order, possibly at the same time - and return when all have finished. It may be called from any thread, also from
 *   several at once (sessions of the same models on different threads). task must not call back into tinyasr. n: how
 *   many tasks run at once (steps too small to be worth splitting stay on the calling thread). NULL: back to one thread.
 *
 * How many: the cores of the fastest cluster. Steps wait for their slowest task, so mixing big and little cores (or two
 * clusters) is often slower than the big ones alone. Measured: Snapdragon 8 Elite, 2 threads on its 2 prime cores:
 * ~1.5x (more threads: no faster); Apple M5 Pro, 4 threads: ~2x on files, ~1.7x streaming. Builds without std: one
 * thread, both functions do nothing.
 *
 * Android: pin the thread that calls tinyasr to the big cores (cpu_capacity close to the largest), then give that many
 * threads. Call this on that thread before the first session / file call:
 *
 *     #define _GNU_SOURCE
 *     #include <sched.h>
 *     #include <stdio.h>
 *     static int pin_to_big_cores(void) {           // returns the number of big cores (0: unknown, nothing pinned)
 *         int cap[CPU_SETSIZE], n = 0, max = 0, big = 0;
 *         for (; n < CPU_SETSIZE; n++) {
 *             char path[64];
 *             snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpu_capacity", n);
 *             FILE *f = fopen(path, "r");
 *             if (f == NULL) break;
 *             if (fscanf(f, "%d", &cap[n]) != 1) cap[n] = 0;
 *             fclose(f);
 *             if (cap[n] > max) max = cap[n];
 *         }
 *         cpu_set_t set;
 *         CPU_ZERO(&set);
 *         for (int i = 0; i < n; i++) if (max > 0 && cap[i] * 10 >= max * 9) { CPU_SET(i, &set); big++; }
 *         if (big > 0) sched_setaffinity(0, sizeof set, &set);
 *         return big;
 *     }
 *     ...
 *     int big = pin_to_big_cores();               // on the thread that will call tinyasr
 *     tinyasr_set_threads(m, big > 0 ? big : 1);  // then sessions / files as usual, from this thread
 *
 * Apple (iOS, macOS): run the calls on a thread or queue of high QoS (user-initiated or user-interactive), and either
 * tinyasr_set_threads(m, P) with P = sysctlbyname("hw.perflevel0.physicalcpu") (the performance cores), or GCD, whose
 * dispatch_apply_f has exactly the task signature:
 *
 *     #include <dispatch/dispatch.h>
 *     static void gcd_parallel_for(void *user, size_t n, tinyasr_task task, void *ctx) {
 *         dispatch_apply_f(n, DISPATCH_APPLY_AUTO, ctx, task);   // returns when all n have run
 *     }
 *     ...
 *     tinyasr_set_parallel_for(m, gcd_parallel_for, NULL, P);
 *
 * With the built-in threads, one parallel step of the models runs at a time: a session on another thread that reaches
 * a step meanwhile runs that step on its own thread (correct, just not split). */
typedef void (*tinyasr_task)(void *ctx, size_t i);
typedef void (*tinyasr_parallel_for)(void *user, size_t n, tinyasr_task task, void *ctx);
void tinyasr_set_threads(tinyasr_models *m, uint32_t threads);
void tinyasr_set_parallel_for(tinyasr_models *m, tinyasr_parallel_for parallel_for, void *user, uint32_t threads);

/* Live audio. langs: the codes of the languages to recognize in, comma-separated ("en", "en,ru"; NULL or "": all of
 * the model's; codes the model lacks are ignored), e.g. from a language ID the host runs. One fixes the language; with several every full-context pass runs in each of them and
 * the surest wins (that many times the cost). Starts from start_lang (may be NULL: the first).
 * clock: monotonic seconds, or NULL. beam: beam width of the full-context passes (0: the default, 4; 1: greedy,
 * fastest; streaming text is always greedy). push / finish return a JSON array of events:
 *   {"type":"commit","words":[{"text":"Hello,","start":0.52,"end":0.9}, ...]}   final words, appended
 *   {"type":"partial","open":[...],"tail":[...]}   words not final yet (replace the previous partial)
 *   {"type":"language","lang":"uk"}      several languages: the recognizer switched to this one
 *   {"type":"eou","at":3.1,"p":0.93}   the end-of-turn head at a pause (> 0.5: the sentence ends)
 */
/* NULL if m is NULL. */
tinyasr_session *tinyasr_session_new(const tinyasr_models *m, const char *langs, const char *start_lang,
                                     double (*clock)(void), uint32_t beam);
const char *tinyasr_session_push(tinyasr_session *s, const float *pcm, size_t n);
const char *tinyasr_session_finish(tinyasr_session *s);
const char *tinyasr_session_lang(const tinyasr_session *s); /* the current language's code (valid while the models are loaded) */
void tinyasr_session_free(tinyasr_session *s);

/* A recorded file (copied; langs and beam as for sessions): only the full-context pass, piece by piece. next returns the next piece's final
 * words as a JSON array of {"text","start","end"} (seconds), or NULL at the end. NULL if m is NULL. */
tinyasr_file *tinyasr_file_new(const tinyasr_models *m, const float *pcm, size_t n, const char *langs, uint32_t beam);
const char *tinyasr_file_lang(const tinyasr_file *f);
const char *tinyasr_file_next(tinyasr_file *f);
float tinyasr_file_progress(const tinyasr_file *f); /* 0..1; -1 for NULL */
void tinyasr_file_free(tinyasr_file *f);

#ifdef __cplusplus
}
#endif
#endif
