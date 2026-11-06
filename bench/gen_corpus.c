/*
 * gen_corpus: deterministic generator of large text files for the benchmarks.
 *
 * usage: ./gen_corpus <output> <size_bytes | Nm> [seed] [top_words]
 *
 * It writes ASCII lowercase words selected with a linear congruential
 * generator, so the output is byte-identical for the same arguments no matter
 * the machine or load. The vocabulary is small on purpose: WordCount keeps a
 * small hash table and the run measures the streaming cost, not the table.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORDS_COUNT 256
#define LINE_WORDS  12

typedef struct {
    uint64_t state;
} rng_t;

static void rng_seed(rng_t *r, uint64_t seed)
{
    r->state = seed + 0x9E3779B97F4A7C15ULL;
}

static uint64_t rng_next(rng_t *r)
{
    r->state = r->state * 6364136223846793005ULL + 1442695040888963407ULL;
    return r->state >> 33;
}

/* The word list itself also comes from the LCG, keeping it reproducible. */
static void make_vocabulary(char words[WORDS_COUNT][16], uint64_t seed)
{
    static const char *syllables[] = {
        "ba", "be", "bi", "bo", "bu", "ca", "ce", "ci", "co", "cu",
        "da", "de", "di", "do", "du", "fa", "fe", "fi", "fo", "fu",
        "ga", "ge", "gi", "go", "gu", "ha", "he", "hi", "ho", "hu",
        "la", "le", "li", "lo", "lu", "ma", "me", "mi", "mo", "mu",
        "na", "ne", "ni", "no", "nu", "pa", "pe", "pi", "po", "pu",
        "ra", "re", "ri", "ro", "ru", "sa", "se", "si", "so", "su",
        "ta", "te", "ti", "to", "tu", "va", "ve", "vi", "vo", "vu",
    };
    size_t n_syllables = sizeof syllables / sizeof syllables[0];
    rng_t rng;
    size_t i;
    unsigned syl;

    rng_seed(&rng, seed);
    for (i = 0; i < WORDS_COUNT; i++) {
        words[i][0] = '\0';
        for (syl = 0; syl < 2 + (unsigned)(rng_next(&rng) % 2); syl++) {
            const char *piece = syllables[rng_next(&rng) % n_syllables];
            if (strlen(words[i]) + strlen(piece) >= 15) {
                break;
            }
            strcat(words[i], piece);
        }
    }
    /* The numeric suffix at the end makes the words distinct. */
    for (i = 0; i < WORDS_COUNT; i++) {
        snprintf(words[i] + strlen(words[i]),
                 16 - strlen(words[i]), "%02u",
                 (unsigned)(rng_next(&rng) % WORDS_COUNT));
    }
}

int main(int argc, char **argv)
{
    char words[WORDS_COUNT][16];
    unsigned char chunk[65536];
    size_t chunk_used;
    uint64_t seed = 12345;
    unsigned long long target;
    unsigned long long written = 0;
    FILE *out;
    rng_t rng;
    int top_words = WORDS_COUNT;
    unsigned i;

    if (argc < 3 || argc > 5) {
        fprintf(stderr, "usage: %s <output> <size like 256M> [seed] [vocabulary]\n", argv[0]);
        return 1;
    }

    {
        const char *size_text = argv[2];
        char *end = NULL;
        double value = strtod(size_text, &end);
        if (end == size_text || *end == '\0' || value <= 0) {
            fprintf(stderr, "invalid size '%s'\n", size_text);
            return 1;
        }
        if (*end == 'm' || *end == 'M') {
            value *= 1024.0 * 1024.0;
        }
        else if (*end == 'g' || *end == 'G') {
            value *= 1024.0 * 1024.0 * 1024.0;
        }
        else if (!(*end == 'k' || *end == 'K' || *end == 'b' || *end == 'B' || *end == '\0')) {
            fprintf(stderr, "invalid size '%s'\n", size_text);
            return 1;
        }
        target = (unsigned long long)value;
    }

    if (argc >= 4) {
        seed = strtoull(argv[3], NULL, 10);
    }
    if (argc == 5) {
        top_words = (int)strtol(argv[4], NULL, 10);
        if (top_words < 2 || top_words > WORDS_COUNT) {
            top_words = WORDS_COUNT;
        }
    }

    make_vocabulary(words, seed);

    out = fopen(argv[1], "wb");
    if (out == NULL) {
        perror(argv[1]);
        return 1;
    }

    rng_seed(&rng, seed);
    chunk_used = 0;

    while (written < target) {
        size_t need = (size_t)(target - written);
        if (need > sizeof chunk - 32) {
            need = sizeof chunk - 32;
        }

        while (chunk_used < need) {
            for (i = 0; i < LINE_WORDS && chunk_used < need - 2; i++) {
                int n = snprintf((char *)chunk + chunk_used,
                                 sizeof chunk - chunk_used, "%s ",
                                 words[rng_next(&rng) % (unsigned)top_words]);
                if (n < 0 || (size_t)n > need - chunk_used) {
                    break;
                }
                chunk_used += (size_t)n;
            }
            chunk[chunk_used++] = '\n';
        }

        if (fwrite(chunk, 1, chunk_used, out) != chunk_used) {
            perror("fwrite");
            fclose(out);
            return 1;
        }
        written += chunk_used;
        chunk_used = 0;
    }

    fclose(out);

    printf("%llu bytes\n", (unsigned long long)written);
    return 0;
}
