#include <cstdio>
int main() {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
    fprintf(stderr, "pyser debug dummy\n");
#endif
    return 0;
}

