/* e_entry for in-game inject. Spin until the plugin has detached, then
 * run Johns CRT. Do not use libc here (CRT is not up).
 *
 * 0.61 used pt_call(scePthreadCreate), which single-stepped this thread
 * and restored the game's registers onto it → CE-108255-1.
 */

int __crt_start(void *args);

__attribute__((used, visibility("default"), no_stack_protector))
int overlay_gate(void *args)
{
    unsigned int lo;
    unsigned int hi;
    unsigned long long start;
    unsigned long long now;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    start = ((unsigned long long)hi << 32) | lo;
    do {
        __asm__ volatile("pause");
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        now = ((unsigned long long)hi << 32) | lo;
    } while (now - start < 6000000000ULL);

    return __crt_start(args);
}
