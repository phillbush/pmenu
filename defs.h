#define LEN(a)          (sizeof(a) / sizeof((a)[0]))
#define FLAG(f, b)      (((f) & (b)) == (b))
#ifndef RETURN_FAILURE
#define RETURN_FAILURE  (-1)
#endif
#ifndef RETURN_SUCCESS
#define RETURN_SUCCESS  0
#endif
#ifndef FALSE
#define FALSE           0
#endif
#ifndef TRUE
#define TRUE            1
#endif
