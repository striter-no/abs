#ifndef ABS_COLORS

struct {
    const char *normal;
    const char *reset;
    const char *black;
    const char *white;
    const char *green;
    const char *yellow;
    const char *blue;
    const char *cyan;
    const char *magenta;
    const char *red;
    const char *gray;
} abs_fore = {
    .gray    = "\033[61m",
    .normal  = "\033[39m",
    .reset   = "\033[0m",
    .black   = "\033[30m",
    .red     = "\033[31m",
    .green   = "\033[32m",
    .yellow  = "\033[33m",
    .blue    = "\033[34m",
    .magenta = "\033[35m",
    .cyan    = "\033[36m",
    .white   = "\033[37m",
};

#endif
#define ABS_COLORS