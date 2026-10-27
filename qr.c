// SPDX-License-Identifier: CC0-1.0

/*
 * QR Code is a registered trademark of DENSO WAVE INCORPORATED in Japan
 * and other countries.
 *
 */

#include <qrencode.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Initial size of the buffer used for reading STDIN */
#define STDIN_BUFSIZE 256

/*
 * Every character cell is split into 2x2 quadrants; a glyph is picked by
 * which quadrants are filled.  Mask bits:  1 = top left,    2 = top right,
 *                                          4 = bottom left, 8 = bottom right
 */
static const char *const glyphs[16] = {
    " ", "▘", "▝", "▀", "▖", "▌", "▞", "▛",
    "▗", "▚", "▐", "▜", "▄", "▙", "▟", "█",
};

/* ANSI terminal colors */
#define FG_WH     "\x1b[37m"
#define FG_DF     "\x1b[39m"
#define BG_BK     "\x1b[40m"
#define BG_DF     "\x1b[49m"
#define BGBK_FGWH BG_BK FG_WH
#define BGDF_FGDF BG_DF FG_DF

/* Newline character(s) */
#define EOL "\n"

typedef struct {
    QRencodeMode encode_mode;
    int   version;
    QRecLevel ec_level;
    bool  large;
    bool  compact;
    int   border;
    bool  invert;
    bool  plain;
    bool  unicode;
} Options;

/* Unicode BOM */
const char *utf8_bom = "\xEF\xBB\xBF";

/* Help message */
const char *help_msg =
    "Usage: qr [OPTIONS] STRING" EOL
    "  or:  cat FILE | qr [OPTIONS]" EOL
    EOL
    "Options:" EOL
    "  -m  QR mode       [na8k] (n = number, a = alphabet, 8 = 8-bit, "
                                "k = Kanji)" EOL
    "  -v  QR version    [1-40]" EOL
    "  -e  QR EC level   [lmqh] or [1-4]" EOL
    "  -l  use two characters per block" EOL
    "  -c  compact mode" EOL
    "  -b  border width  [1-4] (the default is 1)" EOL
    "  -i  invert colors" EOL
    "  -p  force colorless output" EOL
    "  -u  ensure output has UTF-8 BOM" EOL
    "  -h  print help info and exit" EOL
    "  -V  print version info and exit" EOL
;

void print_help_msg(void)
{
    printf("%s" EOL, help_msg);
}

void print_version(void)
{
    printf("qr %s" EOL, VERSION);
}

void print_error(const char *message)
{
    fprintf(stderr, "Error: %s" EOL, message);
}

/* Read the whole stream into a NUL-terminated buffer (caller frees it) */
char *read_stream(FILE *stream)
{
    size_t capacity = STDIN_BUFSIZE;
    size_t length = 0;
    size_t n;
    char *buffer = malloc(capacity);
    char *tmp;

    if (buffer == NULL) {
        return NULL;
    }

    while ((n = fread(buffer + length, 1, capacity - length - 1, stream)) > 0) {
        length += n;

        if (length == capacity - 1) {
            capacity *= 2;
            if ((tmp = realloc(buffer, capacity)) == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = tmp;
        }
    }

    if (ferror(stream)) {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

static inline bool str_has_utf8_bom(const char *string)
{
    return (strncmp(string, utf8_bom, strlen(utf8_bom)) == 0);
}

/* Parse a whole decimal number within [min, max] */
bool parse_int(const char *string, int min, int max, int *result)
{
    char *end;
    long value = strtol(string, &end, 10);

    if (end == string || *end != '\0' || value < min || value > max) {
        return false;
    }

    *result = (int)value;
    return true;
}

/*
 * Tell whether the quadrant-sized module at (x, y) of the bordered symbol is
 * filled.  Modules beyond the symbol (the unused half of the last row or
 * column of character cells) are never filled.
 */
static inline bool is_filled(const QRcode *code, const int border,
                             const bool invert, const int x, const int y)
{
    const int width = code->width;
    const int size = width + border * 2;
    const int dx = x - border;
    const int dy = y - border;
    bool dark;

    if (x >= size || y >= size) {
        return false;
    }

    dark = dx >= 0 && dy >= 0 && dx < width && dy < width &&
           (code->data[dy * width + dx] & 1);

    /* Light modules are drawn by default (meant for dark backgrounds) */
    return invert ? dark : !dark;
}

/*
 * Print the QR code as text.  One module spans module_w x module_h
 * quadrants: 2x1 (normal), 1x1 (compact), 4x2 (large), 2x2 (large compact).
 */
int print_qr(FILE *out, const QRcode *code, const int border,
             const bool invert, const bool paint,
             const int module_w, const int module_h)
{
    const int size = code->width + border * 2;
    const int cols = (size * module_w + 1) / 2;
    const int rows = (size * module_h + 1) / 2;
    int cx, cy, q;

    for (cy = 0; cy < rows; cy++) {
        bool reset_bg = false;

        if (paint) {
            fputs(BGBK_FGWH, out);
        }

        for (cx = 0; cx < cols; cx++) {
            unsigned char mask = 0;
            bool partial = false;

            for (q = 0; q < 4; q++) {
                const int qx = cx * 2 + (q & 1);
                const int qy = cy * 2 + (q >> 1);

                if (qx >= size * module_w || qy >= size * module_h) {
                    partial = true;
                } else if (is_filled(code, border, invert,
                                     qx / module_w, qy / module_h)) {
                    mask |= 1 << q;
                }
            }

            /* Keep the part of the cell beyond the symbol transparent */
            if (paint && !invert && partial && !reset_bg) {
                fputs(BG_DF, out);
                reset_bg = true;
            }

            fputs(glyphs[mask], out);
        }

        if (paint) {
            fputs(BGDF_FGDF, out);
        }
        fputs(EOL, out);
    }

    return ferror(out) ? -1 : 0;
}

QRencodeMode get_qr_encode_mode(const char *encode_mode)
{
    if (encode_mode[0] == '\0' || encode_mode[1] != '\0') {
        return QR_MODE_NUL;
    }

    switch (encode_mode[0]) {
        case 'n':
        case 'N':
            return QR_MODE_NUM;

        case 'a':
        case 'A':
            return QR_MODE_AN;

        case '8':
            return QR_MODE_8;

        case 'k':
        case 'K':
            return QR_MODE_KANJI;

        default:
            return QR_MODE_NUL;
    }
}

/* Returns -1 for an invalid level */
int get_qr_ec_level(const char *ec_level)
{
    if (ec_level[0] == '\0' || ec_level[1] != '\0') {
        return -1;
    }

    switch (ec_level[0]) {
        case '1':
        case 'l':
        case 'L':
            return QR_ECLEVEL_L;

        case '2':
        case 'm':
        case 'M':
            return QR_ECLEVEL_M;

        case '3':
        case 'q':
        case 'Q':
            return QR_ECLEVEL_Q;

        case '4':
        case 'h':
        case 'H':
            return QR_ECLEVEL_H;

        default:
            return -1;
    }
}

QRcode *encode(const char *str, const int version, const QRecLevel level,
               const QRencodeMode mode)
{
    QRinput *input;
    QRcode *code = NULL;

    /* QRcode_encodeString() picks the optimal mix of segment modes,
       but only accepts 8-bit and Kanji as hints */
    if (mode == QR_MODE_8 || mode == QR_MODE_KANJI) {
        return QRcode_encodeString(str, version, level, mode, 1);
    }

    /* Force numeric or alphanumeric mode for the whole input */
    if ((input = QRinput_new2(version, level)) == NULL) {
        return NULL;
    }
    if (QRinput_append(input, mode, (int)strlen(str),
                       (const unsigned char *)str) == 0) {
        code = QRcode_encodeInput(input);
    }
    QRinput_free(input);

    return code;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    const char *str = NULL;  /* Input string */
    char *stdin_buf = NULL;  /* Owns the input when it comes from STDIN */
    int c = 0;

    /* Default options */
    Options options = {
        .encode_mode = QR_MODE_8,
        .version = 0,
        .ec_level = QR_ECLEVEL_L,
        .large = false,
        .compact = false,
        .border = 1,
        .invert = false,
        .plain = false
    };

    /* Parse CLI arguments (options may come before or after STRING) */
    while (optind < argc) {
        if ((c = getopt(argc, argv, "m:v:e:lcb:ipuhV")) == -1) {
            if (str != NULL) {
                print_error("too many arguments");
                fprintf(stderr, EOL "%s" EOL, help_msg);
                ret = 1;
                goto exit;
            }
            str = argv[optind++];
            continue;
        }

        switch (c) {
            case 'm':
                options.encode_mode = get_qr_encode_mode(optarg);
                if (options.encode_mode == QR_MODE_NUL) {
                    goto invalid_options;
                }
                break;

            case 'v':
                if (!parse_int(optarg, 0, QRSPEC_VERSION_MAX, &options.version)) {
                    goto invalid_options;
                }
                break;

            case 'e':
                if ((c = get_qr_ec_level(optarg)) < 0) {
                    goto invalid_options;
                }
                options.ec_level = (QRecLevel)c;
                break;

            case 'l':
                options.large = true;
                break;

            case 'c':
                options.compact = true;
                break;

            case 'b':
                if (!parse_int(optarg, 1, 4, &options.border)) {
                    goto invalid_options;
                }
                break;

            case 'i':
                options.invert = true;
                break;

            case 'p':
                options.plain = true;
                break;

            case 'u':
                options.unicode = true;
                break;

            case '?':
                ret = 1;
                goto exit;

            case 'h':
                print_help_msg();
                goto exit;

            case 'V':
                print_version();
                goto exit;
        }
    }

    /* Read input from STDIN only if no STRING was given */
    if (str == NULL && !isatty(STDIN_FILENO)) {
        if ((str = stdin_buf = read_stream(stdin)) == NULL) {
            print_error("unable to read from STDIN");
            ret = 1;
            goto exit;
        }
    }

    /* Check input */
    if (str == NULL || strlen(str) == 0) {
        print_error("no input specified");
        fprintf(stderr, EOL "%s" EOL, help_msg);
        ret = 1;
        goto exit;
    }

    /*******************************/
    /* Generate and output QR code */
    /*******************************/

    char *str_bom = NULL;
    if (options.unicode && !str_has_utf8_bom(str)) {
        /* Prepend UTF-8 BOM to the input string */
        if ((str_bom = malloc(strlen(utf8_bom) + strlen(str) + 1)) == NULL) {
            print_error("out of memory");
            ret = 1;
            goto exit;
        }
        strcpy(str_bom, utf8_bom);
        strcat(str_bom, str);
        str = str_bom;
    }

    QRcode *qr = encode(str, options.version, options.ec_level,
                        options.encode_mode);
    free(str_bom);

    /* Bail out if unable to successfully execute QRcode_encodeString() */
    if (qr == NULL) {
        print_error("failed to generate QR code");
        ret = 1;
        goto exit;
    }

    /* Enforce colorless output mode for non-terminal environments */
    if (!isatty(STDOUT_FILENO)) {
        options.plain = true;
    }

    /* Output QR code as text */
    if (print_qr(stdout, qr, options.border, options.invert, !options.plain,
                 options.large ? (options.compact ? 2 : 4)
                               : (options.compact ? 1 : 2),
                 options.large ? 2 : 1) != 0) {
        print_error("unable to write output");
        ret = 1;
    }

    QRcode_free(qr);

exit:
    free(stdin_buf);
    return ret;

invalid_options:
    print_error("invalid options");
    fprintf(stderr, EOL "%s" EOL, help_msg);
    ret = 1;
    goto exit;
}
