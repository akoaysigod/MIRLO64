/* Portions derived from LiteX (BSD 2-Clause, (c) Enjoy-Digital & LiteX developers; see NOTICE) */
/* What picolibc wants from the system that a bare-metal program has not
 * got. */
#include <stddef.h>
#include <errno.h>

int getentropy(void *v, size_t s) { (void)v; (void)s; return -1; }
int getpid(void) { return 1; }
void _exit(int code) { (void)code; for (;;); }
int kill(int pid, int name) { (void)pid; (void)name; _exit(0); return 0; }
void *_impure_ptr;
