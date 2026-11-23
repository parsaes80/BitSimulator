// For the browser build of dot: a merge sort qsort, so equal elements keep their order as with glibc and layouts match the desktop dot
// Valid C and C++ (dot links with em++, which compiles it as C++); extern "C" keeps the names that replace libc's
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*cmp_r_t)(const void*, const void*, void*);

static void merge_sort(char* base, char* tmp, size_t n, size_t size, cmp_r_t cmp, void* arg)
{
    if (n < 2) return;
    size_t half = n / 2;
    merge_sort(base, tmp, half, size, cmp, arg);
    merge_sort(base + half * size, tmp, n - half, size, cmp, arg);
    char *a = base, *a_end = base + half * size, *b = a_end, *b_end = base + n * size, *out = tmp;
    while (a < a_end && b < b_end) {
        if (cmp(b, a, arg) < 0) { memcpy(out, b, size); b += size; }
        else { memcpy(out, a, size); a += size; }
        out += size;
    }
    memcpy(out, a, (size_t)(a_end - a)); out += a_end - a;
    memcpy(out, b, (size_t)(b_end - b));
    memcpy(base, tmp, n * size);
}

void qsort_r(void* base, size_t n, size_t size, cmp_r_t cmp, void* arg)
{
    if (n < 2) return;
    char* tmp = (char*)malloc(n * size);
    merge_sort((char*)base, tmp, n, size, cmp, arg);
    free(tmp);
}

static int call_plain(const void* a, const void* b, void* cmp) { return ((int (*)(const void*, const void*))cmp)(a, b); }

void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*))
{
    qsort_r(base, n, size, call_plain, (void*)cmp);
}

#ifdef __cplusplus
}
#endif
