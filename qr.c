/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 *
 */

/*
 * QR Code is a registered trademark of DENSO WAVE INCORPORATED in Japan
 * and other countries.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <math.h>
#include <qrencode.h>


/* stdin read buffer size */
#define BUFFER_SIZE 64
/* block elements */
#define BLOCK0      "██"
#define BLOCK1      "  "
/* double-block elements */
#define DBLOCK0     "█"
#define DBLOCK1     " "
#define DBLOCK2     "▀"
#define DBLOCK3     "▄"
/* ANSI terminal colors */
#define FG_WH       "\x1b[37m"
#define FG_DF       "\x1b[39m"
#define BG_BK       "\x1b[40m"
#define BG_DF       "\x1b[49m"
#define BGBK_FGWH   BG_BK FG_WH
#define BGDF_FGDF   BG_DF FG_DF
/* newline character(s) */
#define EOL         "\n"


typedef struct {
    char encode_mode;
    int version;
    char ec_level;
    bool large;
    short border;
    bool invert;
    bool plain;
} Options;


/* help screen */
const char *help =
    "Usage: qr [OPTIONS] STRING" EOL
    "  or:  cat FILE | qr [OPTIONS]" EOL
    EOL
    "Options:" EOL
    "  -m  QR mode       [na8k] (n = number, a = alphabet, 8 = 8-bit, "
                                "k = Kanji)" EOL
    "  -v  QR version    [1-40]" EOL
    "  -e  QR EC level   [lmqh] or [1-4]" EOL
    "  -l  use two characters per block" EOL
    "  -b  border width  [1-10] (the default is 2)" EOL
    "  -i  invert colors" EOL
    "  -p  force colorless output" EOL
    "  -h  print help (this message)" EOL
;

/* Unicode BOMs */
const char *bom_utf8 = "\xEF\xBB\xBF";


void bzero(void *s, size_t n);

void print_help (void)
{
    printf("%s" EOL, help);
}

void print_error (const char *message)
{
    fprintf(stderr, EOL "Error: %s" EOL EOL, message);
}

static inline bool has_bom (const char *string)
{
    return (strcmp(string, bom_utf8) == 0);
}

char *qr_data_to_text (const unsigned char *data, const char border_width,
                       const bool invert, const bool paint, const bool large)
{
    int i = 0;
    int j = 0;

    if (data == NULL) {
        return NULL;
    }

    const char *blocks[2] = {
        (invert) ? BLOCK1 : BLOCK0,
        (invert) ? BLOCK0 : BLOCK1
    };
    const char *double_blocks[4] = {
        (invert) ? DBLOCK1 : DBLOCK0,
        (invert) ? DBLOCK0 : DBLOCK1,
        (invert) ? DBLOCK3 : DBLOCK2,
        (invert) ? DBLOCK2 : DBLOCK3
    };
    const int r = sqrt(strlen((char *)data));
    const int l = r + border_width * 2;
    const int bytelen = (strlen(BLOCK0) * l + strlen(EOL)) * l +
        ((paint) ? strlen(BGBK_FGWH) + strlen(BGDF_FGDF) : 0) * l;
    char *text = malloc(bytelen);

    /* put the string cursor in the beginning of *text */
    text[0] = '\0';

    if (large) { /* 2 characters per block */
        /* top border */
        for (i = 0; i < border_width; i++) {
            /* set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }
            /* add top border blocks */
            for (j = 0; j < l; j++) {
                strcat(text, blocks[0]);
            }
            /* reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }
            /* put newline */
            strcat(text, EOL);
        }

        /* left border, data, right border */
        for (i = 0; i < r; i++) {
            /* set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }
            /* add left border blocks */
            for (j = 0; j < border_width; j++) {
                strcat(text, blocks[0]);
            }
            /* add data blocks */
            for (j = 0; j < r; j++) {
                strcat(text, blocks[data[i * r + j] & 0x01]);
            }
            /* add right border blocks */
            for (j = 0; j < border_width; j++) {
                strcat(text, blocks[0]);
            }
            /* reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }
            /* put newline */
            strcat(text, EOL);
        }

        /* bottom border */
        for (i = 0; i < border_width; i++) {
            /* set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }
            /* add bottom border blocks */
            for (j = 0; j < l; j++) {
                strcat(text, blocks[0]);
            }
            /* reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }
            /* put newline */
            strcat(text, EOL);
        }
    } else { /* 1/2 char per block (default size) */
        int border_leftover = (border_width % 2);

        /* top border */
        for (i = 0; i < border_width - border_leftover; i += 2) {
            /* set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }
            /* add top border double-blocks */
            for (j = 0; j < l; j++) {
                strcat(text, double_blocks[0]);
            }
            /* reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }
            /* put newline */
            strcat(text, EOL);
        }

        /* left border, data, right border */
        for (i = 0 - border_leftover; i < r; i += 2) {
            /* set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }
            /* add left border double-blocks */
            for (j = 0; j < border_width; j++) {
                strcat(text, double_blocks[0]);
            }
            /* add data double-blocks */
            for (j = 0; j < r; j++) {
                int first_row = (i < 0);
                int not_last_row = (i < r-1);

                if (!first_row && data[i * r + j] & 0x01) {
                    if (not_last_row && data[i * r + j + r] & 0x01) {
                        strcat(text, double_blocks[1]);
                    } else {
                        strcat(text, double_blocks[3]);
                    }
                } else {
                    if (not_last_row && data[i * r + j + r] & 0x01) {
                        strcat(text, double_blocks[2]);
                    } else {
                        strcat(text, double_blocks[0]);
                    }
                }
            }
            /* add right border double-blocks */
            for (j = 0; j < border_width; j++) {
                strcat(text, double_blocks[0]);
            }
            /* reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }
            /* put newline */
            strcat(text, EOL);
        }

        /* bottom border */
        for (i = 2; i < border_width; i += 2) {
            /* set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }
            /* add double-blocks */
            for (j = 0; j < l; j++) {
                strcat(text, double_blocks[0]);
            }
            /* reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }
            /* put newline */
            strcat(text, EOL);
        }
        /* trailing double-block for bottom border */
        {
            /* set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
                /* avoid coloring last (transparent) double-line */
                if (!invert) {
                    strcat(text, BG_DF);
                }
            }
            /* add double-blocks */
            for (j = 0; j < l; j++) {
                strcat(text, double_blocks[(invert) ? 0 : 2]);
            }
            /* reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }
            /* put newline */
            strcat(text, EOL);
        }
    }

    return text;
}

char get_qr_encode_mode (const char encode_mode)
{
    char ret = -1;

    switch (encode_mode)
    {
        case 'n':
        case 'N':
            ret = QR_MODE_NUM;
            break;

        case 'a':
        case 'A':
            ret = QR_MODE_AN;
            break;

        case '8':
            ret = QR_MODE_8;
            break;

        case 'k':
        case 'K':
            ret = QR_MODE_KANJI;
    }

    return ret;
}

char get_qr_ec_level (const char ec_level)
{
    char ret = -1;

    switch (ec_level)
    {
        case '1':
        case 'l':
        case 'L':
            ret = QR_ECLEVEL_L;
            break;

        case '2':
        case 'm':
        case 'M':
            ret = QR_ECLEVEL_M;
            break;

        case '3':
        case 'q':
        case 'Q':
            ret = QR_ECLEVEL_Q;
            break;

        case '4':
        case 'h':
        case 'H':
            ret = QR_ECLEVEL_H;
    }

    return ret;
}

int main (int argc, char *argv[])
{
    int ret = 0;
    char *str = "";
    char buffer[BUFFER_SIZE];
    int c = 0;

    /* default options */
    Options options = {
        .encode_mode = '8',
        .version = 0,
        .ec_level = '1',
        .large = false,
        .border = 2,
        .invert = false,
        .plain = false
    };

    /* process stdin (if any) */
    if (!isatty(STDIN_FILENO)) {
        ssize_t stdin_read_size = 0;

        size_t buffer_size = 0;
        while ((stdin_read_size = read(0, buffer, BUFFER_SIZE)) > 0) {
            size_t new_buffer_size = buffer_size + stdin_read_size;
            char *new_buffer = malloc(new_buffer_size);

            if (new_buffer == NULL) {
                print_error("out of memory");
                ret = 1;
                goto cleanup;
            }

            memcpy(new_buffer, str, buffer_size);
            memcpy(&new_buffer[buffer_size], buffer, stdin_read_size);
            str = new_buffer;
            buffer_size = new_buffer_size;
        }
    }

    /* parse cli arguments */
    while (optind < argc)
    {
        if ((c = getopt(argc, argv, "m:v:e:lb:iph")) == -1) {
            str = argv[optind++];
            continue;
        }

        switch (c)
        {
            case 'm':
                options.encode_mode = optarg[0];
                break;

            case 'v':
                options.version = atoi(optarg);
                break;

            case 'e':
                options.ec_level = optarg[0];
                break;

            case 'l':
                options.large = true;
                break;

            case 'b':
                options.border = atoi(optarg);
                break;

            case 'i':
                options.invert = true;
                break;

            case 'p':
                options.plain = true;
                break;

            case 'h':
                print_help();
                ret = 1;
                goto cleanup;
        }
    }

    if (options.version < 0 || options.version > 40 ||
        get_qr_ec_level(options.ec_level) < 0 ||
        get_qr_encode_mode(options.encode_mode) < 0 ||
        options.border < 1 || options.border > 10)
    {
        print_error("invalid options");
        fprintf(stderr, "%s" EOL, help);
        ret = 1;
        goto cleanup;
    }

    if (optind != argc) {
        if (str != NULL) {
            print_error("too many arguments");
            fprintf(stderr, "%s" EOL, help);
            ret = 1;
            goto cleanup;
        }

        str = argv[optind];
    }

    if (strlen(str) == 0) {
        print_error("no input specified");
        fprintf(stderr, "%s" EOL, help);
        ret = 1;
        goto cleanup;
    }

    /* generate QR code */
    QRcode *qr;

    /* ensure QR code contains UTF-8 BOM */
    if (has_bom(str)) {
        qr = QRcode_encodeString(str, options.version,
                                 get_qr_ec_level(options.ec_level),
                                 get_qr_encode_mode(options.encode_mode), true);
    } else {
        char str_utf8[strlen(bom_utf8) + strlen(str) + 1];
        strcpy(str_utf8, bom_utf8);
        strcat(str_utf8, str);
        qr = QRcode_encodeString(str_utf8, options.version,
                                 get_qr_ec_level(options.ec_level),
                                 get_qr_encode_mode(options.encode_mode), true);
        bzero(str_utf8, strlen(str_utf8));
    }

    /* bail out if unable to successfully execute QRcode_encodeString() */
    if (qr == NULL) {
        print_error("failed to generate QR code");
        ret = 1;
        goto cleanup;
    }

    /* enforce colorless output mode for non-terminal environments */
    if (!isatty(STDIN_FILENO)) {
        options.plain = true;
    }

    /* convert QR code data into text */
    char *text = qr_data_to_text(qr->data, options.border,
                                 options.invert, !options.plain, options.large);

    /* output QR code as text */
    if (text) {
        printf("%s", text);
        bzero(text, strlen(text));
        free(text);
    } else {
        print_error("failed to convert QR code into text");
        ret = 1;
    }

    /* wipe data from memory for security reasons */
    bzero(qr->data, strlen((char *)qr->data));
    bzero(qr, sizeof(QRcode));
    QRcode_free(qr);
cleanup:
    bzero(str, strlen(str));

    return ret;
}
