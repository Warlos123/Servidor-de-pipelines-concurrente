#ifndef CHECK_H
#define CHECK_H

/*
 * Framework mínimo de reporting compartido por todas las suites de pruebas.
 *
 * Objetivo: que la salida se autoexplique. Cada sección declara QUÉ verifica
 * y en el pie de página se declara QUÉ suite es, cuántos checks pasaron y si
 * hubo fallos. El exit code queda listo para make / CI.
 */

#include <stdio.h>
#include <stdlib.h>

static int g_checks   = 0;
static int g_failures = 0;

#define CHECK(cond) do {                                                          \
        g_checks++;                                                               \
        if (cond) {                                                               \
            printf("  ✔ %s\n", #cond);                                            \
        } else {                                                                  \
            g_failures++;                                                         \
            printf("  ✘ FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                         \
    } while (0)

/* Encabezado de sección: qué se prueba ahora y por qué importa.
 * __attribute__((unused)) evita ruido en suites con pocas secciones. */
static void check_section(const char *title, const char *explains)
    __attribute__((unused));
static void check_section(const char *title, const char *explains)
{
    printf("\n── %s · %s\n", title, explains);
}

/* Encabezado de la suite: primero, para que se sepa qué se está mirando. */
static void check_header(const char *suite, const char *explains)
    __attribute__((unused));
static void check_header(const char *suite, const char *explains)
{
    printf("═══ %s · %s\n", suite, explains);
}

/* Pie de página: resultado legible + exit code correcto. */
static int check_summary(const char *suite)
    __attribute__((unused));
static int check_summary(const char *suite)
{
    int passed = g_checks - g_failures;
    if (g_failures == 0) {
        printf("\n✔ %s: %d de %d checks pasaron — todo verde\n", suite, passed, g_checks);
        return EXIT_SUCCESS;
    }
    printf("\n✘ %s: %d de %d checks pasaron — %d FALLO(S)\n", suite, passed, g_checks, g_failures);
    return EXIT_FAILURE;
}

#endif /* CHECK_H */
