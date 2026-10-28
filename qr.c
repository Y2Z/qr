// SPDX-License-Identifier: 0BSD

/*
 * QR Code is a registered trademark of DENSO WAVE INCORPORATED in Japan
 * and other countries.
 *
 */

#include <qrencode.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

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
    int   format;      /* Format (animated QR codes) */
    int   data_type;   /* DataType (animated QR codes) */
    int   fps;
    int   frames;      /* Number of frames to output, 0 = default */
    bool  raw;
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
    "  -v  QR version    [1-40] (the default is 10 for -a)" EOL
    "  -e  QR EC level   [lmqh] or [1-4]" EOL
    "  -l  use two characters per block" EOL
    "  -c  compact mode" EOL
    "  -b  border width  [1-4] (the default is 1)" EOL
    "  -i  invert colors" EOL
    "  -p  force colorless output" EOL
    "  -u  ensure output has UTF-8 BOM" EOL
    "  -a  animate data  [bbqr|bbqr-hex|bbqr-base32|bbqr-zlib|ur|numbered|txqr]" EOL
    "  -t  data type     [auto|psbt|tx|json|cbor|text|binary] (for -a)" EOL
    "  -f  frame rate    [1-30] (frames per second, the default is 4)" EOL
    "  -n  number of frames to output (for -a)" EOL
    "  -r  print frame contents instead of QR codes (for -a)" EOL
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

/*
 * Read the whole stream into a buffer (caller frees it).  The buffer is also
 * NUL-terminated, but may contain NUL bytes: its length is stored in *size.
 */
char *read_stream(FILE *stream, size_t *size)
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
    *size = length;
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
/* Returns the number of text rows printed, or -1 on error */
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

    return ferror(out) ? -1 : rows;
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

/* Encode the whole input using the given mode */
QRcode *encode_as(const char *str, const size_t size, const int version,
                  const QRecLevel level, const QRencodeMode mode)
{
    QRinput *input;
    QRcode *code = NULL;

    if ((input = QRinput_new2(version, level)) == NULL) {
        return NULL;
    }
    if (QRinput_append(input, mode, (int)size,
                       (const unsigned char *)str) == 0) {
        code = QRcode_encodeInput(input);
    }
    QRinput_free(input);

    return code;
}

QRcode *encode(const char *str, const size_t size, const int version,
               const QRecLevel level, const QRencodeMode mode)
{
    /* Binary data (with NUL bytes) can only be stored in 8-bit mode */
    if (memchr(str, '\0', size) != NULL) {
        if (mode != QR_MODE_8) {
            return NULL;
        }
        return QRcode_encodeData((int)size, (const unsigned char *)str,
                                 version, level);
    }

    /* QRcode_encodeString() picks the optimal mix of segment modes,
       but only accepts 8-bit and Kanji as hints */
    if (mode == QR_MODE_8 || mode == QR_MODE_KANJI) {
        return QRcode_encodeString(str, version, level, mode, 1);
    }

    /* Force numeric or alphanumeric mode for the whole input */
    return encode_as(str, size, version, level, mode);
}

/*****************************************************************************/
/* Animated QR codes                                                         */
/*****************************************************************************/

/* Formats for splitting data into a series of QR codes (-a) */
typedef enum {
    FORMAT_NONE,
    FORMAT_BBQR,        /* zlib if it helps, else base32 */
    FORMAT_BBQR_HEX,
    FORMAT_BBQR_BASE32,
    FORMAT_BBQR_ZLIB,
    FORMAT_UR,
    FORMAT_NUMBERED,    /* "p1of3 ..." */
    FORMAT_TXQR,
} Format;

/* Kinds of data (-t) */
typedef enum {
    TYPE_AUTO,
    TYPE_PSBT,
    TYPE_TX,
    TYPE_JSON,
    TYPE_CBOR,
    TYPE_TEXT,
    TYPE_BINARY,
} DataType;

/* Default QR version of animation frames, and default frame rate */
#define ANIM_VERSION 10
#define ANIM_FPS     4

/* Growable byte buffer */
typedef struct {
    unsigned char *data;
    size_t size;
} Buffer;

/* A series of frames (QR code contents) */
typedef struct {
    Buffer *frames;
    size_t count;       /* Frames needed to transfer the data */
    QRencodeMode mode;  /* QR_MODE_AN or QR_MODE_8 */
    int version;        /* Version of every frame, 0 = smallest that fits */
    /* Generates frames past the first `count` ones (for fountain codes);
       without it, the frames repeat */
    bool (*more)(void *ctx, size_t index, Buffer *frame);
    void (*more_free)(void *ctx);
    void *ctx;
} Animation;

Format get_format(const char *name)
{
    if (strcmp(name, "bbqr") == 0)        return FORMAT_BBQR;
    if (strcmp(name, "bbqr-hex") == 0)    return FORMAT_BBQR_HEX;
    if (strcmp(name, "bbqr-base32") == 0) return FORMAT_BBQR_BASE32;
    if (strcmp(name, "bbqr-zlib") == 0)   return FORMAT_BBQR_ZLIB;
    if (strcmp(name, "ur") == 0)          return FORMAT_UR;
    if (strcmp(name, "numbered") == 0)    return FORMAT_NUMBERED;
    if (strcmp(name, "txqr") == 0)        return FORMAT_TXQR;
    return FORMAT_NONE;
}

/* Returns -1 for an unknown type */
int get_data_type(const char *name)
{
    static const char *const names[] = {
        "auto", "psbt", "tx", "json", "cbor", "text", "binary",
    };
    int i;

    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        if (strcmp(name, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

bool buffer_append(Buffer *buf, const void *data, const size_t size)
{
    unsigned char *tmp = realloc(buf->data, buf->size + size + 1);

    if (tmp == NULL) {
        return false;
    }
    buf->data = tmp;
    memcpy(buf->data + buf->size, data, size);
    buf->size += size;
    buf->data[buf->size] = '\0';
    return true;
}

void animation_free(Animation *anim)
{
    size_t i;

    for (i = 0; i < anim->count; i++) {
        free(anim->frames[i].data);
    }
    free(anim->frames);
    anim->frames = NULL;
    anim->count = 0;
    if (anim->more_free != NULL) {
        anim->more_free(anim->ctx);
    }
}

bool animation_add(Animation *anim, const void *data, const size_t size)
{
    Buffer *tmp = realloc(anim->frames, (anim->count + 1) * sizeof(Buffer));

    if (tmp == NULL) {
        return false;
    }
    anim->frames = tmp;
    anim->frames[anim->count] = (Buffer){ NULL, 0 };
    if (!buffer_append(&anim->frames[anim->count], data, size)) {
        return false;
    }
    anim->count++;
    return true;
}

/*****************************************************************************/
/* Binary-to-text encodings                                                  */
/*****************************************************************************/

bool hex_encode(Buffer *out, const unsigned char *data, const size_t size,
                const bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t i;

    for (i = 0; i < size; i++) {
        const char pair[2] = { digits[data[i] >> 4], digits[data[i] & 15] };
        if (!buffer_append(out, pair, 2)) {
            return false;
        }
    }
    return true;
}

static int hex_value(const int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Returns false if the text is not an even number of hex digits */
bool hex_decode(Buffer *out, const char *text, const size_t size)
{
    size_t i;

    if (size == 0 || size % 2 != 0) {
        return false;
    }
    for (i = 0; i < size; i += 2) {
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        unsigned char byte;

        if (hi < 0 || lo < 0) {
            return false;
        }
        byte = (unsigned char)(hi << 4 | lo);
        if (!buffer_append(out, &byte, 1)) {
            return false;
        }
    }
    return true;
}

/* RFC 4648 base32, without padding */
bool base32_encode(Buffer *out, const unsigned char *data, const size_t size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    unsigned int bits = 0;
    int nbits = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        bits = (bits << 8) | data[i];
        nbits += 8;
        while (nbits >= 5) {
            nbits -= 5;
            if (!buffer_append(out, &alphabet[(bits >> nbits) & 31], 1)) {
                return false;
            }
        }
    }
    if (nbits > 0) {
        if (!buffer_append(out, &alphabet[(bits << (5 - nbits)) & 31], 1)) {
            return false;
        }
    }
    return true;
}

/* RFC 4648 base64, with padding */
bool base64_encode(Buffer *out, const unsigned char *data, const size_t size)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;

    for (i = 0; i < size; i += 3) {
        const unsigned long n = (unsigned long)data[i] << 16 |
                                (i + 1 < size ? (unsigned long)data[i + 1] << 8 : 0) |
                                (i + 2 < size ? data[i + 2] : 0);
        const char quad[4] = {
            alphabet[n >> 18 & 63], alphabet[n >> 12 & 63],
            i + 1 < size ? alphabet[n >> 6 & 63] : '=',
            i + 2 < size ? alphabet[n & 63] : '=',
        };
        if (!buffer_append(out, quad, 4)) {
            return false;
        }
    }
    return true;
}

/* RFC 4648 base64; returns false if the text is not valid base64 */
bool base64_decode(Buffer *out, const char *text, const size_t size)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int bits = 0;
    int nbits = 0;
    size_t i, padding = 0;

    if (size == 0 || size % 4 != 0) {
        return false;
    }
    for (i = 0; i < size; i++) {
        const char *p;
        unsigned char byte;

        if (text[i] == '=' && i >= size - 2) {
            padding++;
            continue;
        }
        if (padding > 0 || text[i] == '\0' ||
            (p = strchr(alphabet, text[i])) == NULL) {
            return false;
        }
        bits = (bits << 6) | (unsigned int)(p - alphabet);
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            byte = (unsigned char)(bits >> nbits);
            if (!buffer_append(out, &byte, 1)) {
                return false;
            }
        }
    }
    return true;
}

/* Raw DEFLATE with a 1 KiB window, as required by BBQr */
bool deflate_raw(Buffer *out, const unsigned char *data, const size_t size)
{
    z_stream z;
    unsigned char chunk[4096];
    int status;

    memset(&z, 0, sizeof(z));
    if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, -10, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }
    z.next_in = (unsigned char *)data;
    z.avail_in = (uInt)size;
    do {
        z.next_out = chunk;
        z.avail_out = sizeof(chunk);
        status = deflate(&z, Z_FINISH);
        if (status == Z_STREAM_ERROR ||
            !buffer_append(out, chunk, sizeof(chunk) - z.avail_out)) {
            deflateEnd(&z);
            return false;
        }
    } while (status != Z_STREAM_END);
    deflateEnd(&z);
    return true;
}

/*****************************************************************************/
/* Input data                                                                */
/*****************************************************************************/

/* Valid UTF-8 without NUL bytes */
static bool is_utf8(const unsigned char *s, const size_t size)
{
    size_t i = 0;
    int n;

    while (i < size) {
        const unsigned char c = s[i++];

        if (c == 0)                  return false;
        else if (c < 0x80)           n = 0;
        else if ((c & 0xE0) == 0xC0) n = 1;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0) n = 3;
        else                         return false;

        for (; n > 0; n--, i++) {
            if (i >= size || (s[i] & 0xC0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

/* Copy text without any whitespace (e.g. line-wrapped base64) */
static bool strip_whitespace(Buffer *out, const char *text, const size_t size)
{
    size_t i;

    if (!buffer_append(out, "", 0)) {
        return false;
    }
    for (i = 0; i < size; i++) {
        if (strchr(" \t\r\n", text[i]) == NULL || text[i] == '\0') {
            if (!buffer_append(out, &text[i], 1)) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Turn the input into the bytes to transmit.  PSBTs are accepted as binary,
 * base64 (as printed by Bitcoin Core) or hex; transactions as binary or hex.
 * When the type is "auto", PSBTs are detected and anything else is sent as
 * text if it is valid UTF-8, or as binary data otherwise.
 */
static bool is_psbt(const Buffer *buf)
{
    return buf->size >= 5 && memcmp(buf->data, "psbt\xff", 5) == 0;
}

static void buffer_clear(Buffer *buf)
{
    free(buf->data);
    *buf = (Buffer){ NULL, 0 };
}

bool prepare_data(Buffer *out, DataType *type, const char *input,
                  const size_t size)
{
    Buffer compact = { NULL, 0 };
    bool ok = false;

    if (!strip_whitespace(&compact, input, size)) {
        return false;
    }

    if (*type == TYPE_AUTO || *type == TYPE_PSBT) {
        /* Binary, base64 or hex PSBT */
        if (!buffer_append(out, input, size)) {
            goto done;
        }
        if (!is_psbt(out)) {
            buffer_clear(out);
            if (!base64_decode(out, (char *)compact.data, compact.size) ||
                !is_psbt(out)) {
                buffer_clear(out);
                if (!hex_decode(out, (char *)compact.data, compact.size) ||
                    !is_psbt(out)) {
                    buffer_clear(out);
                }
            }
        }
        if (out->data != NULL) {
            *type = TYPE_PSBT;
            ok = true;
            goto done;
        }
        if (*type == TYPE_PSBT) {
            print_error("input is not a PSBT");
            goto done;
        }
        *type = is_utf8((const unsigned char *)input, size) ? TYPE_TEXT
                                                            : TYPE_BINARY;
    }

    /* Hex transaction */
    if (*type == TYPE_TX &&
        hex_decode(out, (char *)compact.data, compact.size)) {
        ok = true;
        goto done;
    }
    buffer_clear(out);

    ok = buffer_append(out, input, size);

done:
    free(compact.data);
    return ok;
}

/*****************************************************************************/
/* QR code capacity                                                          */
/*****************************************************************************/

static bool fits(const int version, const QRecLevel level,
                 const QRencodeMode mode, const size_t size)
{
    char *s = malloc(size + 1);
    QRinput *input = QRinput_new2(version, level);
    QRcode *code = NULL;
    bool ok = false;

    if (s != NULL && input != NULL) {
        memset(s, mode == QR_MODE_AN ? 'A' : 'a', size);
        if (QRinput_append(input, mode, (int)size, (unsigned char *)s) == 0 &&
            (code = QRcode_encodeInput(input)) != NULL) {
            ok = code->version == version;
        }
    }
    QRcode_free(code);
    QRinput_free(input);
    free(s);
    return ok;
}

/* How many characters (or bytes) fit into a QR code of the given version */
size_t qr_capacity(const int version, const QRecLevel level,
                   const QRencodeMode mode)
{
    size_t lo = 0, hi = 7089;  /* The most any QR code can hold */

    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (fits(version, level, mode, mid)) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

/*****************************************************************************/
/* BBQr (https://github.com/coinkite/BBQr)                                   */
/*****************************************************************************/

#define BBQR_HEADER_LEN 8
#define BBQR_MAX_PARTS  1295  /* "ZZ" in base 36 */

static void base36_pair(char *out, const size_t n)
{
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    out[0] = digits[n / 36];
    out[1] = digits[n % 36];
}

bool bbqr_split(Animation *anim, const Format format, const DataType type,
                const unsigned char *data, const size_t size,
                const QRecLevel level)
{
    static const char type_codes[] = "?PTJCUB";  /* Indexed by DataType */
    char encoding = format == FORMAT_BBQR_HEX ? 'H' : '2';
    Buffer compressed = { NULL, 0 };
    Buffer text = { NULL, 0 };
    size_t capacity, per_part, count, i;
    size_t align = 8;  /* Base32 parts must hold whole bytes */
    bool ok = false;

    if (format == FORMAT_BBQR || format == FORMAT_BBQR_ZLIB) {
        if (!deflate_raw(&compressed, data, size)) {
            goto done;
        }
        /* Compression doesn't always help (e.g. already random data) */
        if (compressed.size < size || format == FORMAT_BBQR_ZLIB) {
            encoding = 'Z';
        }
    }

    if (encoding == 'H') {
        align = 2;
        ok = hex_encode(&text, data, size, true);
    } else if (encoding == 'Z') {
        ok = base32_encode(&text, compressed.data, compressed.size);
    } else {
        ok = base32_encode(&text, data, size);
    }
    if (!ok) {
        goto done;
    }
    ok = false;

    capacity = qr_capacity(anim->version, level, QR_MODE_AN);
    if (capacity < BBQR_HEADER_LEN + align) {
        print_error("QR version too small for BBQr");
        goto done;
    }
    per_part = capacity - BBQR_HEADER_LEN;
    per_part -= per_part % align;
    count = (text.size + per_part - 1) / per_part;
    if (count > BBQR_MAX_PARTS) {
        print_error("data too large for BBQr, try a higher QR version (-v)");
        goto done;
    }
    /* Spread the data evenly rather than leaving a tiny last part */
    per_part = (text.size + count - 1) / count;
    per_part += (align - per_part % align) % align;

    for (i = 0; i < count; i++) {
        const size_t offset = i * per_part;
        const size_t len = (text.size - offset < per_part) ? text.size - offset
                                                           : per_part;
        char frame[BBQR_HEADER_LEN + 7089];

        frame[0] = 'B';
        frame[1] = '$';
        frame[2] = encoding;
        frame[3] = type_codes[type];
        base36_pair(frame + 4, count);
        base36_pair(frame + 6, i);
        memcpy(frame + BBQR_HEADER_LEN, text.data + offset, len);
        if (!animation_add(anim, frame, BBQR_HEADER_LEN + len)) {
            goto done;
        }
    }
    anim->mode = QR_MODE_AN;
    ok = true;

done:
    free(compressed.data);
    free(text.data);
    return ok;
}

/*****************************************************************************/
/* SHA-256 (FIPS 180-4), used to seed the fountain code generator of UR      */
/*****************************************************************************/

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        w[i] = w[i - 16] + w[i - 7] +
               (ROTR32(w[i - 15], 7) ^ ROTR32(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
               (ROTR32(w[i - 2], 17) ^ ROTR32(w[i - 2], 19) ^ (w[i - 2] >> 10));
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; k = h[7];
    for (i = 0; i < 64; i++) {
        t1 = k + (ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25)) +
             ((e & f) ^ (~e & g)) + sha256_k[i] + w[i];
        t2 = (ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22)) +
             ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void sha256(unsigned char digest[32], const unsigned char *data,
            const size_t size)
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char tail[128] = { 0 };
    const size_t full = size - size % 64;
    const size_t rest = size % 64;
    const size_t tail_len = rest < 56 ? 64 : 128;
    const uint64_t bits = (uint64_t)size * 8;
    size_t i;

    for (i = 0; i < full; i += 64) {
        sha256_block(h, data + i);
    }
    memcpy(tail, data + full, rest);
    tail[rest] = 0x80;
    for (i = 0; i < 8; i++) {
        tail[tail_len - 1 - i] = (unsigned char)(bits >> (8 * i));
    }
    for (i = 0; i < tail_len; i += 64) {
        sha256_block(h, tail + i);
    }
    for (i = 0; i < 32; i++) {
        digest[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

/*****************************************************************************/
/* UR: Uniform Resources (BCR-2020-005, BCR-2020-012, BCR-2024-001)          */
/*****************************************************************************/

/* xoshiro256** seeded with the SHA-256 digest of some bytes */
typedef struct {
    uint64_t s[4];
} Xoshiro256;

static uint64_t rotl64(const uint64_t x, const int k)
{
    return (x << k) | (x >> (64 - k));
}

static void xoshiro_seed(Xoshiro256 *rng, const unsigned char *data,
                         const size_t size)
{
    unsigned char digest[32];
    int i, n;

    sha256(digest, data, size);
    for (i = 0; i < 4; i++) {
        rng->s[i] = 0;
        for (n = 0; n < 8; n++) {
            rng->s[i] = rng->s[i] << 8 | digest[i * 8 + n];
        }
    }
}

static uint64_t xoshiro_next(Xoshiro256 *rng)
{
    uint64_t *s = rng->s;
    const uint64_t result = rotl64(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl64(s[3], 45);
    return result;
}

static double xoshiro_double(Xoshiro256 *rng)
{
    return (double)xoshiro_next(rng) / 18446744073709551616.0;  /* 2^64 */
}

static uint64_t xoshiro_int(Xoshiro256 *rng, const uint64_t low,
                            const uint64_t high)
{
    return (uint64_t)(xoshiro_double(rng) * (double)(high - low + 1)) + low;
}

/*
 * Picks the number of fragments to mix into a part (Walker's alias method),
 * with probabilities 1/1, 1/2, ... 1/n.  The floating point operations must
 * happen in exactly the same order as in the reference implementation.
 */
typedef struct {
    double *probs;
    size_t *aliases;
    size_t count;
} DegreeSampler;

static bool sampler_init(DegreeSampler *ds, const size_t n)
{
    double *p = malloc(n * sizeof(double));
    size_t *small = malloc(n * sizeof(size_t));
    size_t *large = malloc(n * sizeof(size_t));
    size_t ns = 0, nl = 0, i;
    double sum = 0;

    ds->probs = calloc(n, sizeof(double));
    ds->aliases = calloc(n, sizeof(size_t));
    ds->count = n;
    if (p == NULL || small == NULL || large == NULL ||
        ds->probs == NULL || ds->aliases == NULL) {
        free(p); free(small); free(large);
        return false;
    }

    for (i = 0; i < n; i++) {
        sum += 1.0 / (double)(i + 1);
    }
    for (i = 0; i < n; i++) {
        p[i] = (1.0 / (double)(i + 1)) * (double)n / sum;
    }
    for (i = n; i-- > 0;) {
        if (p[i] < 1) {
            small[ns++] = i;
        } else {
            large[nl++] = i;
        }
    }
    while (ns > 0 && nl > 0) {
        const size_t a = small[--ns];
        const size_t g = large[--nl];
        ds->probs[a] = p[a];
        ds->aliases[a] = g;
        p[g] += p[a] - 1;
        if (p[g] < 1) {
            small[ns++] = g;
        } else {
            large[nl++] = g;
        }
    }
    while (nl > 0) {
        ds->probs[large[--nl]] = 1;
    }
    while (ns > 0) {
        ds->probs[small[--ns]] = 1;
    }

    free(p); free(small); free(large);
    return true;
}

static void sampler_free(DegreeSampler *ds)
{
    free(ds->probs);
    free(ds->aliases);
}

static size_t sampler_next(const DegreeSampler *ds, Xoshiro256 *rng)
{
    const double r1 = xoshiro_double(rng);
    const double r2 = xoshiro_double(rng);
    const size_t i = (size_t)((double)ds->count * r1);
    return r2 < ds->probs[i] ? i : ds->aliases[i];
}

/* Minimal CBOR: unsigned integers, byte strings and array headers */
static bool cbor_head(Buffer *out, const unsigned char major, const uint64_t n)
{
    unsigned char head[9];
    int len, i;

    if (n < 24) {
        head[0] = (unsigned char)(major << 5 | n);
        len = 0;
    } else {
        len = n <= 0xff ? 1 : n <= 0xffff ? 2 : n <= 0xffffffffUL ? 4 : 8;
        head[0] = (unsigned char)(major << 5 | (len == 1 ? 24 : len == 2 ? 25
                                               : len == 4 ? 26 : 27));
        for (i = 0; i < len; i++) {
            head[1 + i] = (unsigned char)(n >> (8 * (len - 1 - i)));
        }
    }
    return buffer_append(out, head, (size_t)len + 1);
}

/* Bytewords, minimal style: first and last letters, with a CRC-32 suffix */
static bool bytewords_append(Buffer *out, const unsigned char *data,
                             const size_t size)
{
    static const char words[] =
        "ableacidalsoapexaquaarchatomauntawayaxisbackbaldbarnbeltbetabias"
        "bluebodybragbrewbulbbuzzcalmcashcatschefcityclawcodecolacookcost"
        "cruxcurlcuspcyandarkdatadaysdelidicedietdoordowndrawdropdrumdull"
        "dutyeacheasyechoedgeepicevenexamexiteyesfactfairfernfigsfilmfish"
        "fizzflapflewfluxfoxyfreefrogfuelfundgalagamegeargemsgiftgirlglow"
        "goodgraygrimgurugushgyrohalfhanghardhawkheathelphighhillholyhope"
        "hornhutsicedideaidleinchinkyintoirisironitemjadejazzjoinjoltjowl"
        "judojugsjumpjunkjurykeepkenokeptkeyskickkilnkingkitekiwiknoblamb"
        "lavalazyleaflegsliarlimplionlistlogoloudloveluaulucklungmainmany"
        "mathmazememomenumeowmildmintmissmonknailnavyneednewsnextnoonnote"
        "numbobeyoboeomitonyxopenovalowlspaidpartpeckplaypluspoempoolpose"
        "puffpumapurrquadquizraceramprealredorichroadrockroofrubyruinruns"
        "rustsafesagascarsetssilkskewslotsoapsolosongstubsurfswantacotask"
        "taxitenttiedtimetinytoiltombtoystriptunatwinuglyundouniturgeuser"
        "vastveryvetovialvibeviewvisavoidvowswallwandwarmwaspwavewaxywebs"
        "whatwhenwhizwolfworkyankyawnyellyogayurtzapszerozestzinczonezoom";
    const uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
    unsigned char checksum[4];
    size_t i;

    for (i = 0; i < 4; i++) {
        checksum[i] = (unsigned char)(crc >> (24 - 8 * i));
    }
    for (i = 0; i < size + 4; i++) {
        const unsigned char byte = i < size ? data[i] : checksum[i - size];
        const char pair[2] = { (char)(words[byte * 4] - 'a' + 'A'),
                               (char)(words[byte * 4 + 3] - 'a' + 'A') };
        if (!buffer_append(out, pair, 2)) {
            return false;
        }
    }
    return true;
}

/* Fountain encoder state, used to generate parts on demand */
typedef struct {
    char type[32];             /* UR type, e.g. "BYTES" */
    Buffer message;            /* CBOR-encoded data */
    uint32_t checksum;         /* CRC-32 of the message */
    size_t fragment_len;
    size_t seq_len;            /* Number of fragments */
    DegreeSampler sampler;
} UREncoder;

/* Indexes of the fragments mixed into part number seq_num (1-based) */
static size_t ur_choose_fragments(UREncoder *ur, const uint32_t seq_num,
                                  size_t *chosen)
{
    unsigned char seed[8];
    Xoshiro256 rng;
    size_t degree, i, n;

    /* The first seq_len parts are the plain fragments, in order */
    if (seq_num <= ur->seq_len) {
        chosen[0] = seq_num - 1;
        return 1;
    }

    for (i = 0; i < 4; i++) {
        seed[i] = (unsigned char)(seq_num >> (24 - 8 * i));
        seed[4 + i] = (unsigned char)(ur->checksum >> (24 - 8 * i));
    }
    xoshiro_seed(&rng, seed, sizeof(seed));
    degree = sampler_next(&ur->sampler, &rng) + 1;

    /* Fisher-Yates shuffle of all indexes; keep the first `degree` */
    for (i = 0; i < ur->seq_len; i++) {
        chosen[i] = i;
    }
    for (n = ur->seq_len, i = 0; i < degree; i++, n--) {
        const size_t pick = (size_t)xoshiro_int(&rng, 0, n - 1);
        const size_t value = chosen[i + pick];
        memmove(&chosen[i + 1], &chosen[i], pick * sizeof(size_t));
        chosen[i] = value;
    }
    return degree;
}

/* Generate the text of part number seq_num */
static bool ur_part(UREncoder *ur, const uint32_t seq_num, Buffer *out)
{
    size_t *chosen = malloc(ur->seq_len * sizeof(size_t));
    unsigned char *mixed = calloc(ur->fragment_len, 1);
    Buffer cbor = { NULL, 0 };
    char seq[32];
    size_t count, i, j;
    bool ok = false;

    if (chosen == NULL || mixed == NULL) {
        goto done;
    }

    count = ur_choose_fragments(ur, seq_num, chosen);
    for (i = 0; i < count; i++) {
        const size_t offset = chosen[i] * ur->fragment_len;
        for (j = 0; j < ur->fragment_len; j++) {
            /* The last fragment is padded with zeros */
            if (offset + j < ur->message.size) {
                mixed[j] ^= ur->message.data[offset + j];
            }
        }
    }

    snprintf(seq, sizeof(seq), "/%lu-%lu/", (unsigned long)seq_num,
             (unsigned long)ur->seq_len);
    ok = cbor_head(&cbor, 4, 5) &&
         cbor_head(&cbor, 0, seq_num) &&
         cbor_head(&cbor, 0, ur->seq_len) &&
         cbor_head(&cbor, 0, ur->message.size) &&
         cbor_head(&cbor, 0, ur->checksum) &&
         cbor_head(&cbor, 2, ur->fragment_len) &&
         buffer_append(&cbor, mixed, ur->fragment_len) &&
         buffer_append(out, "UR:", 3) &&
         buffer_append(out, ur->type, strlen(ur->type)) &&
         buffer_append(out, seq, strlen(seq)) &&
         bytewords_append(out, cbor.data, cbor.size);

done:
    free(chosen);
    free(mixed);
    free(cbor.data);
    return ok;
}

/* Sequence numbers start over after this, so that parts keep their size */
#define UR_MAX_SEQ_NUM 999999

/* Generates the fountain-coded parts past the first cycle */
static bool ur_more(void *ctx, size_t index, Buffer *out)
{
    return ur_part((UREncoder *)ctx, (uint32_t)(index % UR_MAX_SEQ_NUM + 1),
                   out);
}

static size_t cbor_len(const uint64_t n)
{
    return n < 24 ? 1 : n <= 0xff ? 2 : n <= 0xffff ? 3 : 5;
}

static size_t digits(size_t n)
{
    size_t d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

/* Length of the longest part: "UR:TYPE/999999-N/" + bytewords of CBOR */
static size_t ur_part_len(const UREncoder *ur, const size_t seq_len,
                          const size_t fragment_len)
{
    const size_t cbor = 1 + cbor_len(UR_MAX_SEQ_NUM) + cbor_len(seq_len) +
                        cbor_len(ur->message.size) + cbor_len(0xffffffffUL) +
                        cbor_len(fragment_len) + fragment_len;

    return 3 + strlen(ur->type) + 1 + digits(UR_MAX_SEQ_NUM) + 1 +
           digits(seq_len) + 1 + (cbor + 4) * 2;
}

static void ur_free(void *ctx)
{
    UREncoder *ur = ctx;

    free(ur->message.data);
    sampler_free(&ur->sampler);
    free(ur);
}

bool ur_split(Animation *anim, const DataType type, const unsigned char *data,
              const size_t size, const QRecLevel level)
{
    UREncoder *ur = calloc(1, sizeof(UREncoder));
    size_t capacity, i;
    Buffer text = { NULL, 0 };
    bool ok = false;

    if (ur == NULL) {
        return false;
    }
    if (size > 0xffffffffUL - 16) {
        print_error("data too large for UR");
        free(ur);
        return false;
    }

    /* Message is the data as a CBOR byte string; PSBTs have their own type */
    strcpy(ur->type, type == TYPE_PSBT ? "CRYPTO-PSBT" : "BYTES");
    if (!cbor_head(&ur->message, 2, size) ||
        !buffer_append(&ur->message, data, size)) {
        goto fail;
    }
    ur->checksum = (uint32_t)crc32(0, ur->message.data,
                                   (uInt)ur->message.size);

    /* Single part, if it fits */
    capacity = qr_capacity(anim->version, level, QR_MODE_AN);
    if (3 + strlen(ur->type) + 1 + (ur->message.size + 4) * 2 <= capacity) {
        ok = buffer_append(&text, "UR:", 3) &&
             buffer_append(&text, ur->type, strlen(ur->type)) &&
             buffer_append(&text, "/", 1) &&
             bytewords_append(&text, ur->message.data, ur->message.size) &&
             animation_add(anim, text.data, text.size);
        anim->mode = QR_MODE_AN;
        free(text.data);
        ur_free(ur);
        return ok;
    }

    /* Multiple parts: the smallest number of equally-sized fragments
       (of at least 10 bytes, like the reference) that fit */
    for (ur->seq_len = 2;; ur->seq_len++) {
        ur->fragment_len = (ur->message.size + ur->seq_len - 1) / ur->seq_len;
        if (ur->fragment_len < 10) {
            print_error("QR version too small for UR");
            goto fail;
        }
        if (ur_part_len(ur, ur->seq_len, ur->fragment_len) <= capacity) {
            break;
        }
    }
    /* Splitting into pieces of that length may need fewer fragments */
    ur->seq_len = (ur->message.size + ur->fragment_len - 1) / ur->fragment_len;
    if (!sampler_init(&ur->sampler, ur->seq_len)) {
        goto fail;
    }

    for (i = 0; i < ur->seq_len; i++) {
        text.size = 0;
        if (!ur_part(ur, (uint32_t)(i + 1), &text) ||
            !animation_add(anim, text.data, text.size)) {
            free(text.data);
            goto fail;
        }
    }
    free(text.data);

    anim->mode = QR_MODE_AN;
    anim->more = ur_more;
    anim->more_free = ur_free;
    anim->ctx = ur;
    return true;

fail:
    ur_free(ur);
    return false;
}

/*****************************************************************************/
/* Formats that carry text                                                   */
/*****************************************************************************/

/*
 * Turn the data into the text that wallets exchange: PSBTs in base64,
 * transactions in hex, text as is, and any other binary data in base64.
 */
bool to_text(Buffer *out, const DataType type, const unsigned char *data,
             const size_t size)
{
    if (!buffer_append(out, "", 0)) {
        return false;
    }
    if (type == TYPE_TX) {
        return hex_encode(out, data, size, false);
    }
    if (type != TYPE_PSBT && is_utf8(data, size)) {
        return buffer_append(out, data, size);
    }
    return base64_encode(out, data, size);
}

/*
 * Numbered parts, as used by Specter and Krux: "p1of3 ...", "p2of3 ...".
 * Parts never split a UTF-8 character, since receivers decode each one.
 */
bool numbered_split(Animation *anim, const Buffer *text,
                    const QRecLevel level)
{
    const size_t capacity = qr_capacity(anim->version, level, QR_MODE_8);
    char prefix[48];
    size_t count, target, start, end, n, i;

    anim->mode = QR_MODE_8;
    if (text->size <= capacity) {
        return animation_add(anim, text->data, text->size);
    }

    for (count = 2;; count++) {
        const size_t prefix_len = (size_t)snprintf(prefix, sizeof(prefix),
                                                   "p%luof%lu ",
                                                   (unsigned long)count,
                                                   (unsigned long)count);
        if (prefix_len + 1 > capacity || count > text->size) {
            print_error("QR version too small for numbered parts");
            return false;
        }
        target = (text->size + count - 1) / count;
        if (target + prefix_len > capacity) {
            continue;
        }
        /* Count the parts needed when splitting at character boundaries */
        for (n = 0, start = 0; start < text->size; n++, start = end) {
            end = start + target < text->size ? start + target : text->size;
            while (end < text->size && end > start + 1 &&
                   (text->data[end] & 0xC0) == 0x80) {
                end--;
            }
        }
        if (n <= count) {
            count = n;
            break;
        }
    }

    for (i = 0, start = 0; i < count; i++, start = end) {
        Buffer frame = { NULL, 0 };
        const int prefix_len = snprintf(prefix, sizeof(prefix), "p%luof%lu ",
                                        (unsigned long)(i + 1),
                                        (unsigned long)count);
        bool ok;

        end = start + target < text->size ? start + target : text->size;
        while (end < text->size && end > start + 1 &&
               (text->data[end] & 0xC0) == 0x80) {
            end--;
        }
        ok = buffer_append(&frame, prefix, (size_t)prefix_len) &&
             buffer_append(&frame, text->data + start, end - start) &&
             animation_add(anim, frame.data, frame.size);
        free(frame.data);
        if (!ok) {
            return false;
        }
    }
    return true;
}

/*****************************************************************************/
/* TXQR (https://github.com/divan/txqr)                                      */
/*****************************************************************************/

/*
 * TXQR uses the Luby Transform codec of github.com/google/gofountain, whose
 * choices come from Go's math/rand on top of gofountain's Mersenne Twister.
 * Both are reproduced exactly, so that TXQR decoders pick the same blocks.
 */
typedef struct {
    uint32_t mt[624];
    int index;
} MersenneTwister;

static void mt_seed(MersenneTwister *t, const int64_t seed)
{
    int i;

    t->mt[0] = (uint32_t)(((uint64_t)seed >> 32) ^ (uint64_t)seed);
    for (i = 1; i < 624; i++) {
        t->mt[i] = 1812433253u * (t->mt[i - 1] ^ (t->mt[i - 1] >> 30)) +
                   (uint32_t)i;
    }
    t->index = 0;
}

static uint32_t mt_next(MersenneTwister *t)
{
    uint32_t y;
    int i;

    if (t->index == 0) {
        for (i = 0; i < 624; i++) {
            y = (t->mt[i] & 0x80000000u) | (t->mt[(i + 1) % 624] & 0x7fffffffu);
            t->mt[i] = t->mt[(i + 397) % 624] ^ (y >> 1) ^
                       ((y & 1) ? 0x9908b0dfu : 0);
        }
    }
    y = t->mt[t->index];
    t->index = (t->index + 1) % 624;
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    return y;
}

/* rand.Rand.Int63() */
static int64_t go_int63(MersenneTwister *t)
{
    const uint32_t a = mt_next(t);
    const uint32_t b = mt_next(t);
    return ((int64_t)a << 31) ^ (int64_t)b;
}

/* rand.Rand.Float64() */
static double go_float64(MersenneTwister *t)
{
    double f;

    do {
        f = (double)go_int63(t) / 9223372036854775808.0;  /* 2^63 */
    } while (f == 1);
    return f;
}

/* rand.Rand.Intn(), for n < 2^31 */
static int32_t go_intn(MersenneTwister *t, const int32_t n)
{
    int32_t max, v;

    if ((n & (n - 1)) == 0) {
        return (int32_t)(go_int63(t) >> 32) & (n - 1);
    }
    max = (int32_t)(2147483647u - 2147483648u % (uint32_t)n);
    do {
        v = (int32_t)(go_int63(t) >> 32);
    } while (v > max);
    return v % n;
}

/* Block codes start over after this, so that frames keep their size */
#define TXQR_MAX_CODE 999999

typedef struct {
    Buffer text;
    size_t chunk_len;          /* Advertised in every frame */
    size_t blocks;             /* Number of source blocks */
    size_t long_len, short_len, long_count;
    double *cdf;               /* Soliton distribution, 1-based */
    size_t *picks;
    unsigned char *mixed;
} TXQREncoder;

/* Generate the frame for block code `code` */
static bool txqr_frame(TXQREncoder *tx, const size_t code, Buffer *out)
{
    MersenneTwister mt;
    char header[64];
    size_t degree, len = 0, i, j;
    double r;
    int header_len;

    /* Degree: the first index of the CDF that reaches r */
    mt_seed(&mt, (int64_t)code);
    r = go_float64(&mt);
    for (degree = 0; degree <= tx->blocks && tx->cdf[degree] < r; degree++) {
    }
    if (degree > tx->blocks) {
        degree = tx->blocks;  /* Only possible through rounding errors */
    } else if (tx->cdf[degree] == r && degree < tx->blocks) {
        degree++;
    }

    /* Which blocks to mix: all of them, or `degree` distinct random ones */
    if (degree >= tx->blocks) {
        degree = tx->blocks;
        for (i = 0; i < degree; i++) {
            tx->picks[i] = i;
        }
    } else {
        for (i = 0; i < degree; i++) {
            size_t pick;
            bool seen;
            do {
                pick = (size_t)go_intn(&mt, (int32_t)tx->blocks);
                for (seen = false, j = 0; j < i && !seen; j++) {
                    seen = tx->picks[j] == pick;
                }
            } while (seen);
            tx->picks[i] = pick;
        }
    }

    /* XOR them together; the result is as long as the longest one */
    memset(tx->mixed, 0, tx->long_len);
    for (i = 0; i < degree; i++) {
        const size_t b = tx->picks[i];
        const size_t size = b < tx->long_count ? tx->long_len : tx->short_len;
        const size_t offset = b < tx->long_count
                            ? b * tx->long_len
                            : tx->long_count * tx->long_len +
                              (b - tx->long_count) * tx->short_len;
        for (j = 0; j < size; j++) {
            tx->mixed[j] ^= tx->text.data[offset + j];
        }
        if (size > len) {
            len = size;
        }
    }

    header_len = snprintf(header, sizeof(header), "%lu/%lu/%lu|",
                          (unsigned long)code, (unsigned long)tx->chunk_len,
                          (unsigned long)tx->text.size);
    return buffer_append(out, header, (size_t)header_len) &&
           buffer_append(out, tx->mixed, len);
}

static bool txqr_more(void *ctx, size_t index, Buffer *out)
{
    return txqr_frame((TXQREncoder *)ctx, index % (TXQR_MAX_CODE + 1), out);
}

static void txqr_free(void *ctx)
{
    TXQREncoder *tx = ctx;

    free(tx->text.data);
    free(tx->cdf);
    free(tx->picks);
    free(tx->mixed);
    free(tx);
}

/*
 * The first round of frames are the ones TXQR's own encoder makes (twice
 * as many as there are blocks); later frames use new block codes, which
 * decoders accept just the same.
 */
bool txqr_split(Animation *anim, Buffer *text, const QRecLevel level)
{
    const size_t capacity = qr_capacity(anim->version, level, QR_MODE_8);
    TXQREncoder *tx = calloc(1, sizeof(TXQREncoder));
    char header[64];
    size_t header_max, i;
    Buffer frame = { NULL, 0 };

    if (tx == NULL) {
        return false;
    }
    tx->text = *text;
    *text = (Buffer){ NULL, 0 };
    anim->mode = QR_MODE_8;

    /* Leave room for the longest header: "999999/<chunk_len>/<total>|" */
    header_max = (size_t)snprintf(header, sizeof(header), "%d/%lu/%lu|",
                                  TXQR_MAX_CODE, (unsigned long)capacity,
                                  (unsigned long)tx->text.size);
    if (capacity <= header_max) {
        print_error("QR version too small for TXQR");
        goto fail;
    }
    tx->chunk_len = capacity - header_max;

    /* Fits into a single frame */
    if (tx->text.size < tx->chunk_len) {
        bool ok = buffer_append(&frame, header,
                                (size_t)snprintf(header, sizeof(header),
                                                 "0/%lu/%lu|",
                                                 (unsigned long)tx->chunk_len,
                                                 (unsigned long)tx->text.size)) &&
                  buffer_append(&frame, tx->text.data, tx->text.size) &&
                  animation_add(anim, frame.data, frame.size);
        free(frame.data);
        txqr_free(tx);
        return ok;
    }

    tx->blocks = (tx->text.size + tx->chunk_len - 1) / tx->chunk_len;
    if (tx->blocks * 2 > TXQR_MAX_CODE) {
        print_error("data too large for TXQR, try a higher QR version (-v)");
        goto fail;
    }
    /* Blocks of nearly equal size, the longer ones first (RFC 5053) */
    tx->short_len = tx->text.size / tx->blocks;
    tx->long_count = tx->text.size - tx->short_len * tx->blocks;
    tx->long_len = tx->long_count > 0 ? tx->short_len + 1 : tx->short_len;

    tx->cdf = calloc(tx->blocks + 1, sizeof(double));
    tx->picks = calloc(tx->blocks, sizeof(size_t));
    tx->mixed = calloc(tx->long_len, 1);
    if (tx->cdf == NULL || tx->picks == NULL || tx->mixed == NULL) {
        goto fail;
    }
    tx->cdf[1] = 1 / (double)tx->blocks;
    for (i = 2; i <= tx->blocks; i++) {
        tx->cdf[i] = tx->cdf[i - 1] + 1 / ((double)i * (double)(i - 1));
    }

    for (i = 0; i < tx->blocks * 2; i++) {
        frame.size = 0;
        if (!txqr_frame(tx, i, &frame) ||
            !animation_add(anim, frame.data, frame.size)) {
            free(frame.data);
            goto fail;
        }
    }
    free(frame.data);

    anim->more = txqr_more;
    anim->more_free = txqr_free;
    anim->ctx = tx;
    return true;

fail:
    txqr_free(tx);
    return false;
}

/*****************************************************************************/
/* Platform specifics                                                        */
/*****************************************************************************/

/* MinGW's getopt() reorders arguments, unless told not to with a '+' */
#ifdef _WIN32
#define GETOPT_PREFIX "+"
#else
#define GETOPT_PREFIX ""
#endif

#ifdef _WIN32
static UINT saved_output_cp = 0;
static DWORD saved_console_mode = 0;

static void restore_console(void)
{
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), saved_console_mode);
    SetConsoleOutputCP(saved_output_cp);
}
#endif

/*
 * Prepare standard streams, and tell whether STDOUT is a terminal that
 * understands UTF-8 and ANSI escape codes.
 */
static bool setup_terminal(void)
{
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;

    /* No newline conversions: data may be binary, output must be exact */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    /* Only a console has a mode (NUL passes isatty(), for example) */
    if (!GetConsoleMode(out, &mode)) {
        return false;
    }
    saved_console_mode = mode;
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        return false;  /* Older than Windows 10 */
    }
    saved_output_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    atexit(restore_console);
    return true;
#else
    return isatty(STDOUT_FILENO);
#endif
}

#ifdef _WIN32
/*
 * The C runtime converts arguments to the ANSI code page, which turns every
 * character missing from it into "?": take them from the UTF-16 command line
 * instead, converted to UTF-8.
 */
static char **utf8_arguments(int *argc)
{
    LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), argc);
    char **argv = NULL;
    int i, size;

    if (wide == NULL ||
        (argv = calloc((size_t)*argc + 1, sizeof(char *))) == NULL) {
        goto done;
    }
    for (i = 0; i < *argc; i++) {
        size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0, NULL, NULL);
        if (size <= 0 || (argv[i] = malloc((size_t)size)) == NULL) {
            while (i-- > 0) {
                free(argv[i]);
            }
            free(argv);
            argv = NULL;
            goto done;
        }
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, argv[i], size, NULL, NULL);
    }

done:
    LocalFree(wide);
    return argv;
}
#endif

static volatile sig_atomic_t stop_playback = 0;

static void on_signal(int signum)
{
    (void)signum;
    stop_playback = 1;
}

/* Stop playback on Ctrl+C instead of exiting, so the cursor gets restored */
static void catch_signals(void)
{
#ifdef _WIN32
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#else
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
#endif
}

/* Wait for the given number of nanoseconds, or until interrupted */
static void wait_ns(const long ns)
{
#ifdef _WIN32
    Sleep((DWORD)(ns / 1000000L));
#else
    struct timespec delay = { ns / 1000000000L, ns % 1000000000L };

    while (nanosleep(&delay, &delay) != 0 && !stop_playback) {
    }
#endif
}

/*****************************************************************************/
/* Playback                                                                  */
/*****************************************************************************/

/* Returns the number of text rows printed, or -1 on error */
int print_frame(const Animation *anim, const Buffer *frame,
                const Options *options)
{
    QRcode *code = encode_as((const char *)frame->data, frame->size,
                             anim->version, options->ec_level, anim->mode);
    int rows;

    if (code == NULL || (anim->version != 0 &&
                         code->version != anim->version)) {
        print_error("failed to generate QR code");
        QRcode_free(code);
        return -1;
    }
    rows = print_qr(stdout, code, options->border, options->invert,
                    !options->plain,
                    options->large ? (options->compact ? 2 : 4)
                                   : (options->compact ? 1 : 2),
                    options->large ? 2 : 1);
    QRcode_free(code);
    if (rows < 0) {
        print_error("unable to write output");
    }
    return rows;
}

/* Frame number `index`; `extra` holds frames generated on demand */
static const Buffer *get_frame(const Animation *anim, const size_t index,
                               Buffer *extra)
{
    if (index < anim->count || anim->more == NULL) {
        return &anim->frames[index % anim->count];
    }
    extra->size = 0;
    return anim->more(anim->ctx, index, extra) ? extra : NULL;
}

/*
 * Show the frames in a loop until interrupted when writing to a terminal;
 * otherwise print each frame once (as a QR code, or as text with -r).
 * `count` frames are shown if it isn't 0.
 */
int play(const Animation *anim, const Options *options, const size_t count,
         const bool terminal)
{
    const long frame_ns = 1000000000L / options->fps;
    const bool loop = !options->raw && terminal &&
                      (anim->count > 1 || count > 1);
    const size_t total = count > 0 ? count : loop ? 0 : anim->count;
    Buffer extra = { NULL, 0 };
    size_t i;
    int rows = 0;

    if (loop) {
        catch_signals();
        fputs("\x1b[?25l", stdout);  /* Hide cursor */
    }

    for (i = 0; (total == 0 || i < total) && !stop_playback; i++) {
        const Buffer *frame = get_frame(anim, i, &extra);

        if (frame == NULL) {
            print_error("out of memory");
            rows = -1;
            break;
        }

        if (options->raw) {
            fwrite(frame->data, 1, frame->size, stdout);
            fputs(EOL, stdout);
            continue;
        }

        if (loop && rows > 0) {
            printf("\x1b[%dA", rows);  /* Move back up to redraw */
        } else if (!loop && i > 0) {
            fputs(EOL, stdout);
        }
        if ((rows = print_frame(anim, frame, options)) < 0) {
            break;
        }

        if (loop) {
            fflush(stdout);
            wait_ns(frame_ns);
        }
    }
    free(extra.data);

    if (loop) {
        fputs("\x1b[?25h", stdout);  /* Show cursor */
    }
    fflush(stdout);

    return (rows < 0 || ferror(stdout)) ? 1 : 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    const char *str = NULL;  /* Input data */
    size_t str_size = 0;     /* Input data length (it may contain NUL bytes) */
    char *stdin_buf = NULL;  /* Owns the input when it comes from STDIN */
    int c = 0;
    const bool terminal = setup_terminal();

#ifdef _WIN32
    if ((argv = utf8_arguments(&argc)) == NULL) {
        print_error("unable to read arguments");
        return 1;
    }
#endif

    /* Default options */
    Options options = {
        .encode_mode = QR_MODE_8,
        .version = 0,
        .ec_level = QR_ECLEVEL_L,
        .large = false,
        .compact = false,
        .border = 1,
        .invert = false,
        .plain = false,
        .format = FORMAT_NONE,
        .data_type = TYPE_AUTO,
        .fps = ANIM_FPS,
        .frames = 0,
        .raw = false
    };
    bool type_set = false;
    bool fps_set = false;
    bool version_set = false;

    /* Parse CLI arguments (options may come before or after STRING) */
    while (optind < argc) {
        if ((c = getopt(argc, argv, GETOPT_PREFIX "m:v:e:lcb:ipua:t:f:n:rhV")) == -1) {
            if (str != NULL) {
                print_error("too many arguments");
                fprintf(stderr, EOL "%s" EOL, help_msg);
                ret = 1;
                goto exit;
            }
            str = argv[optind++];
            str_size = strlen(str);
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
                version_set = true;
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

            case 'a':
                if ((options.format = get_format(optarg)) == FORMAT_NONE) {
                    goto invalid_options;
                }
                break;

            case 't':
                if ((options.data_type = get_data_type(optarg)) < 0) {
                    goto invalid_options;
                }
                type_set = true;
                break;

            case 'f':
                if (!parse_int(optarg, 1, 30, &options.fps)) {
                    goto invalid_options;
                }
                fps_set = true;
                break;

            case 'n':
                if (!parse_int(optarg, 1, 1000000, &options.frames)) {
                    goto invalid_options;
                }
                break;

            case 'r':
                options.raw = true;
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

    /* Options for animated QR codes go together with -a only */
    if (options.format == FORMAT_NONE
        ? (type_set || fps_set || options.frames || options.raw)
        : (options.encode_mode != QR_MODE_8 || options.unicode)) {
        goto invalid_options;
    }

    /* Read input from STDIN only if no STRING was given */
    if (str == NULL && !isatty(STDIN_FILENO)) {
        if ((str = stdin_buf = read_stream(stdin, &str_size)) == NULL) {
            print_error("unable to read from STDIN");
            ret = 1;
            goto exit;
        }
    }

    /* Check input */
    if (str == NULL || str_size == 0) {
        print_error("no input specified");
        fprintf(stderr, EOL "%s" EOL, help_msg);
        ret = 1;
        goto exit;
    }

    /* Enforce colorless output mode for non-terminal environments */
    if (!terminal) {
        options.plain = true;
    }

    /*****************************************/
    /* Generate and output animated QR codes */
    /*****************************************/

    if (options.format != FORMAT_NONE) {
        Animation anim = {
            .mode = QR_MODE_8,
            .version = version_set ? options.version : ANIM_VERSION,
        };
        DataType type = (DataType)options.data_type;
        Buffer data = { NULL, 0 };
        bool ok = prepare_data(&data, &type, str, str_size);

        if (ok && options.format == FORMAT_UR) {
            ok = ur_split(&anim, type, data.data, data.size, options.ec_level);
        } else if (ok && (options.format == FORMAT_NUMBERED ||
                          options.format == FORMAT_TXQR)) {
            Buffer text = { NULL, 0 };
            ok = to_text(&text, type, data.data, data.size) &&
                 (options.format == FORMAT_TXQR
                  ? txqr_split(&anim, &text, options.ec_level)
                  : numbered_split(&anim, &text, options.ec_level));
            free(text.data);
        } else if (ok) {
            ok = bbqr_split(&anim, (Format)options.format, type,
                            data.data, data.size, options.ec_level);
        }
        if (ok) {
            /* No need for a large QR code if there's only one */
            if (anim.count == 1 && !version_set) {
                anim.version = 0;
            }
            ret = play(&anim, &options, (size_t)options.frames, terminal);
        } else {
            ret = 1;
        }
        animation_free(&anim);
        free(data.data);
        goto exit;
    }

    /*******************************/
    /* Generate and output QR code */
    /*******************************/

    char *str_bom = NULL;
    if (options.unicode && !str_has_utf8_bom(str)) {
        /* Prepend UTF-8 BOM to the input string */
        const size_t bom_size = strlen(utf8_bom);
        if ((str_bom = malloc(bom_size + str_size + 1)) == NULL) {
            print_error("out of memory");
            ret = 1;
            goto exit;
        }
        memcpy(str_bom, utf8_bom, bom_size);
        memcpy(str_bom + bom_size, str, str_size + 1);
        str = str_bom;
        str_size += bom_size;
    }

    QRcode *qr = encode(str, str_size, options.version, options.ec_level,
                        options.encode_mode);
    free(str_bom);

    /* Bail out if unable to encode the data */
    if (qr == NULL) {
        print_error("failed to generate QR code");
        ret = 1;
        goto exit;
    }

    /* Output QR code as text */
    if (print_qr(stdout, qr, options.border, options.invert, !options.plain,
                 options.large ? (options.compact ? 2 : 4)
                               : (options.compact ? 1 : 2),
                 options.large ? 2 : 1) < 0) {
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
