#include <acutest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *pointer;
    size_t size;
} allocation_t;

static allocation_t allocations[32];

/**
 * @brief Move every resize to expose stale pointers into path buffers.
 */
static void *moving_realloc (void *pointer, size_t size) {
    size_t slot = 0;
    while (slot < sizeof(allocations) / sizeof(allocations[0]) && allocations[slot].pointer != pointer) {
        slot++;
    }
    TEST_ASSERT(slot < sizeof(allocations) / sizeof(allocations[0]));
    void *replacement = malloc(size);
    TEST_ASSERT(replacement != NULL);
    if (pointer) {
        size_t copied = allocations[slot].size < size ? allocations[slot].size : size;
        memcpy(replacement, pointer, copied);
        free(pointer);
    }
    allocations[slot] = (allocation_t){.pointer = replacement, .size = size};
    return replacement;
}

/**
 * @brief Remove an allocation from tracking before freeing it.
 */
static void tracked_free (void *pointer) {
    for (size_t i = 0; i < sizeof(allocations) / sizeof(allocations[0]); i++) {
        if (allocations[i].pointer == pointer) {
            allocations[i] = (allocation_t){0};
            break;
        }
    }
    free(pointer);
}

#define realloc moving_realloc
#define free tracked_free
#include "../src/menu/path.c"
#undef free
#undef realloc

/**
 * @brief Verify all tracked path allocations were freed.
 */
static void check_allocations_freed (void) {
    for (size_t i = 0; i < sizeof(allocations) / sizeof(allocations[0]); i++) {
        TEST_CHECK(allocations[i].pointer == NULL);
    }
}

/**
 * @brief Build a directory path long enough to require buffer growth.
 */
static void make_directory (char *output, size_t capacity) {
    size_t offset = 0;
    output[0] = '\0';
    for (unsigned i = 0; i < 5; i++) {
        int written = snprintf(
            output + offset,
            capacity - offset,
            "/segment-%u-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
            i
        );
        TEST_ASSERT(written > 0 && (size_t)written < capacity - offset);
        offset += written;
    }
}

/**
 * @brief Verify long UTF-8 paths retain root and filename state through clone and pop.
 */
static void test_long_initial_path (void) {
    char directory[512];
    make_directory(directory, sizeof(directory));
    char relative[600];
    snprintf(relative, sizeof(relative), "%s/日本語-é.z64", directory);
    char expected[620];
    snprintf(expected, sizeof(expected), "sd:%s", relative);

    path_t *path = path_init("sd:/", relative);
    TEST_CHECK(strcmp(path_get(path), expected) == 0);
    TEST_CHECK(strcmp(path_last_get(path), "日本語-é.z64") == 0);
    TEST_CHECK(!path_is_root(path));

    path_t *clone = path_clone(path);
    TEST_CHECK(path_are_match(path, clone));
    path_pop(clone);
    snprintf(expected, sizeof(expected), "sd:%s", directory);
    TEST_CHECK(strcmp(path_get(clone), expected) == 0);
    TEST_CHECK(strcmp(path_last_get(path), "日本語-é.z64") == 0);
    TEST_CHECK(!path_are_match(path, clone));
    for (unsigned i = 0; i < 5; i++) {
        path_pop(clone);
    }
    TEST_CHECK(path_is_root(clone) && strcmp(path_get(clone), "sd:/") == 0);
    path_pop(clone);
    TEST_CHECK(strcmp(path_get(clone), "sd:/") == 0);

    path_free(clone);
    path_free(path);
    check_allocations_freed();
}

/**
 * @brief Verify long inserted directories preserve filename and extension operations.
 */
static void test_long_subdirectory_and_extension (void) {
    char directory[512];
    make_directory(directory, sizeof(directory));
    path_t *path = path_init("sd:", "/games/original.z64");
    path_push_subdir(path, directory);
    char expected[620];
    snprintf(expected, sizeof(expected), "sd:/games%s/original.z64", directory);
    TEST_CHECK(strcmp(path_get(path), expected) == 0);
    TEST_CHECK(strcmp(path_last_get(path), "original.z64") == 0);
    TEST_CHECK(strcmp(path_ext_get(path), "z64") == 0);
    path_ext_replace(path, "sav");
    snprintf(expected, sizeof(expected), "sd:/games%s/original.sav", directory);
    TEST_CHECK(strcmp(path_get(path), expected) == 0);
    TEST_CHECK(strcmp(path_ext_get(path), "sav") == 0);
    path_ext_remove(path);
    TEST_CHECK(strcmp(path_last_get(path), "original") == 0 && path_ext_get(path) == NULL);
    path_pop(path);
    TEST_CHECK(
        strcmp(path_last_get(path), "segment-4-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") == 0
    );
    path_free(path);
    check_allocations_freed();
}

/**
 * @brief Verify cloned paths grow independently and preserve root state.
 */
static void test_clone_push_grows_independently (void) {
    char directory[512];
    make_directory(directory, sizeof(directory));
    path_t *path = path_init("rom:/", directory);
    path_t *clone = path_clone_push(path, directory);
    char expected[1100];
    snprintf(expected, sizeof(expected), "rom:%s%s", directory, directory);
    TEST_CHECK(strcmp(path_get(clone), expected) == 0);
    snprintf(expected, sizeof(expected), "rom:%s", directory);
    TEST_CHECK(strcmp(path_get(path), expected) == 0);
    for (unsigned i = 0; i < 10; i++) {
        path_pop(clone);
    }
    TEST_CHECK(path_is_root(clone) && strcmp(path_get(clone), "rom:/") == 0);
    TEST_CHECK(!path_is_root(path));
    path_free(clone);
    path_free(path);

    path = path_create(NULL);
    TEST_CHECK(strcmp(path_get(path), "") == 0 && !path_has_value(path));
    path_free(path);
    check_allocations_freed();
}

TEST_LIST = {
    {"long initial paths retain root through clone and pop", test_long_initial_path},
    {"long subdirectories preserve filename and extension operations", test_long_subdirectory_and_extension},
    {"clone-push grows independently and preserves root", test_clone_push_grows_independently},
    {NULL, NULL},
};
