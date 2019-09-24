#ifndef DEBUG_PRINT_HPP
#define DEBUG_PRINT_HPP

#ifdef DEBUG
# include <cstdio>
# define DEBUG_PRINT(fmt, ...) \
    fprintf(stderr, "[tid:%d]" fmt "\n", my_tid, __VA_ARGS__)
#else
# define DEBUG_PRINT(fmt, ...) 
#endif

#endif