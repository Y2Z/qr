// SPDX-License-Identifier: CC0-1.0

/*
 * QR Code is a registered trademark of DENSO WAVE INCORPORATED in Japan
 * and other countries.
 *
 */

#include <getopt.h>
#include <locale.h>
#include <math.h>
#include <qrencode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* STDIN read buffer chunk size in bytes */
#define STDIN_CHUNKSIZE 64

/* Single-module blocks (large size) */
#define BLOCK_0 "  "
#define BLOCK_1 "██"
/* Single-module blocks (large size, compact mode) */
#define BLOCK_0_C " "
#define BLOCK_1_C "█"
/* Double-module blocks (default size, normal mode) */
/*
    Module bit order: [ bottom | top ]
*/
#define DBL_BLOCK_00 " "
#define DBL_BLOCK_01 "▀"
#define DBL_BLOCK_10 "▄"
#define DBL_BLOCK_11 "█"
/* Quad-module blocks (default size, compact mode) */
/*
    Module bit order: [ bottom_right | top_right | bottom_left | top_left ]
*/
#define QUAD_BLOCK_0000 " "
#define QUAD_BLOCK_0001 "▘"
#define QUAD_BLOCK_0010 "▖"
#define QUAD_BLOCK_0011 "▌"
#define QUAD_BLOCK_0100 "▝"
#define QUAD_BLOCK_1000 "▗"
#define QUAD_BLOCK_1001 "▚"
#define QUAD_BLOCK_1010 "▄"
#define QUAD_BLOCK_0101 "▀"
#define QUAD_BLOCK_O110 "▞"
#define QUAD_BLOCK_0111 "▛"
#define QUAD_BLOCK_1011 "▙"
#define QUAD_BLOCK_1100 "▐"
#define QUAD_BLOCK_1101 "▜"
#define QUAD_BLOCK_1110 "▟"
#define QUAD_BLOCK_1111 "█"

/* ANSI terminal colors */
#define FG_WH     "\x1b[37m"
#define FG_DF     "\x1b[39m"
#define BG_BK     "\x1b[40m"
#define BG_DF     "\x1b[49m"
#define BGBK_FGWH BG_BK FG_WH
#define BGDF_FGDF BG_DF FG_DF

/* Characters used in terminal output */
#define EOL "\n"
#define CLR "\033[A\33[2KT\r"

typedef unsigned char bool;
#define true          1
#define false         0

/* Use binary code to represent modules when referring to blocks */
#define B_0    0
#define B_1    1
#define B_00   B_0
#define B_01   B_1
#define B_10   2
#define B_11   3
#define B_0000 B_0
#define B_0001 B_1
#define B_0010 B_10
#define B_0011 B_11
#define B_0100 4
#define B_0101 5
#define B_0110 6
#define B_0111 7
#define B_1000 8
#define B_1001 9
#define B_1010 10
#define B_1011 11
#define B_1100 12
#define B_1101 13
#define B_1110 14
#define B_1111 15

typedef struct {
    bool  anim;
    char  encode_mode;
    int   version;
    char  ec_level;
    bool  large;
    bool  compact;
    short border;
    bool  invert;
    bool  plain;
    bool  unicode;
} Options;

/* Unicode BOM */
const char *utf8_bom = "\xEF\xBB\xBF";

/* Help message */
const char *help_msg =
    "Usage: " PROG " [OPTIONS] [STRING]" EOL
    "  or:  cat FILE | " PROG " [OPTIONS]" EOL
    EOL
    "Options:" EOL
    "  -a  produce animated QR code" EOL
    "  -m  QR mode       [na8k] (n = number, a = alphabet, 8 = 8-bit, "
                                "k = Kanji)" EOL
    "  -v  QR version    [1-40]" EOL
    "  -e  QR EC level   [lmqh] or [1-4]" EOL
    "  -l  large mode" EOL
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
    printf(PROG " %s" EOL, VERSION);
}

void print_error(const char *message)
{
    fprintf(stderr, "Error: %s" EOL, message);
}

void msleep(const int ms)
{
    struct timeval tv;
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    select(0, NULL, NULL, NULL, &tv);
}

static inline bool is_utf8_string(const char *string)
{
    return (strcmp(string, utf8_bom) == 0);
}

char *qr_data_to_text(const QRcode *code, const char border_width,
                       const bool invert_colors, const bool paint,
                       const bool large_size, const bool compact_mode)
{
    int ih = 0; // Horizontal index counter
    int iv = 0; // Vertical index counter

    const unsigned char *data = code->data;

    if (data == NULL) {
        print_error("empty QR code data");
        return NULL;
    }

    const int resolution = code->width;
    const int l = resolution + border_width * 2;

    char *text;

    if (large_size) {
        /*********************************************************************/
        /*                                                                   */
        /*  One module per block (large size and large size + compact mode)  */
        /*                                                                   */
        /*********************************************************************/

        const char *blocks[2] = {
            // 0
            (invert_colors) ?
                (compact_mode) ? BLOCK_0_C : BLOCK_0 :
                (compact_mode) ? BLOCK_1_C : BLOCK_1,
            // 1
            (invert_colors) ?
                (compact_mode) ? BLOCK_1_C : BLOCK_1 :
                (compact_mode) ? BLOCK_0_C : BLOCK_0,
        };
        const char modules_per_block_v = 1;
        const char modules_per_block_h = 1;

        const int byte_len =
            (strlen((compact_mode) ? BLOCK_1_C : BLOCK_1) * l + strlen(EOL)) * l +
            ((paint) ? strlen(BGBK_FGWH) + strlen(BGDF_FGDF) : 0) * l;
        text = malloc(byte_len + 1);
        /* Move cursor to the beginning of *text */
        text[0] = '\0';

        /* Top border */
        for (iv = 0; iv < border_width; iv += modules_per_block_v) {
            /* Set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }

            /* Append top border blocks */
            for (ih = 0; ih < l; ih += modules_per_block_h) {
                strcat(text, blocks[B_0]);
            }

            /* Reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }

            /* Put newline */
            strcat(text, EOL);
        }

        /* Left border, data, right border */
        for (iv = 0; iv < resolution; iv += modules_per_block_v) {
            /* Set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }

            /* Append left border blocks */
            for (ih = 0; ih < border_width; ih += modules_per_block_h) {
                strcat(text, blocks[B_0]);
            }

            /* Append data blocks */
            for (ih = 0; ih < resolution; ih++) {
                strcat(text, blocks[data[iv * resolution + ih] & B_1]);
            }

            /* Append right border blocks */
            for (ih = 0; ih < border_width; ih += modules_per_block_h) {
                strcat(text, blocks[B_0]);
            }

            /* Reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }

            /* Put newline */
            strcat(text, EOL);
        }

        /* Bottom border */
        for (iv = 0; iv < border_width; iv += modules_per_block_v) {
            /* Set palette */
            if (paint) {
                strcat(text, BGBK_FGWH);
            }

            /* Append bottom border blocks */
            for (ih = 0; ih < l; ih += modules_per_block_h) {
                strcat(text, blocks[B_0]);
            }

            /* Reset palette */
            if (paint) {
                strcat(text, BGDF_FGDF);
            }

            /* Put newline */
            strcat(text, EOL);
        }
    } else {
        /******************************************************************/
        /*                                                                */
        /*  Two or four modules per block (normal mode and compact mode)  */
        /*                                                                */
        /******************************************************************/

        if (compact_mode) {
            /*******************************************/
            /*                                         */
            /*  Four modules per block (compact mode)  */
            /*                                         */
            /*******************************************/

            const char *blocks[16] = {
                (invert_colors) ? QUAD_BLOCK_0000 : QUAD_BLOCK_1111,
                (invert_colors) ? QUAD_BLOCK_0001 : QUAD_BLOCK_1110,
                (invert_colors) ? QUAD_BLOCK_0010 : QUAD_BLOCK_1101,
                (invert_colors) ? QUAD_BLOCK_0011 : QUAD_BLOCK_1100,
                (invert_colors) ? QUAD_BLOCK_0100 : QUAD_BLOCK_1011,
                (invert_colors) ? QUAD_BLOCK_0101 : QUAD_BLOCK_1010,
                (invert_colors) ? QUAD_BLOCK_O110 : QUAD_BLOCK_1001,
                (invert_colors) ? QUAD_BLOCK_0111 : QUAD_BLOCK_1000,
                (invert_colors) ? QUAD_BLOCK_1000 : QUAD_BLOCK_0111,
                (invert_colors) ? QUAD_BLOCK_1001 : QUAD_BLOCK_O110,
                (invert_colors) ? QUAD_BLOCK_1010 : QUAD_BLOCK_0101,
                (invert_colors) ? QUAD_BLOCK_1011 : QUAD_BLOCK_0100,
                (invert_colors) ? QUAD_BLOCK_1100 : QUAD_BLOCK_0011,
                (invert_colors) ? QUAD_BLOCK_1101 : QUAD_BLOCK_0010,
                (invert_colors) ? QUAD_BLOCK_1110 : QUAD_BLOCK_0001,
                (invert_colors) ? QUAD_BLOCK_1111 : QUAD_BLOCK_0000,
            };
            const char modules_per_block_v = 2;
            const char modules_per_block_h = 2;
            const char border_leftover_v = (border_width % modules_per_block_v);
            const char border_leftover_h = (border_width % modules_per_block_h);

            const int byte_len =
                (strlen(QUAD_BLOCK_1111) * l + strlen(EOL)) * l +
                ((paint) ? strlen(BGBK_FGWH) + strlen(BGDF_FGDF) : 0) * l;
            text = malloc(byte_len + 1);
            /* Move cursor to the beginning of *text */
            text[0] = '\0';

            /* Top border */
            for (iv = 0; iv < border_width - border_leftover_v; iv += modules_per_block_v) {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);
                }

                /* Append top border quad-blocks */
                for (ih = 0; ih < l - border_leftover_h; ih += modules_per_block_h) {
                    strcat(text, blocks[B_0000]);
                }

                /* Trailing quad-module blocks for right border */
                if (border_leftover_h % modules_per_block_h != 0) {
                    /* Avoid coloring rightmost (transparent) quad-module line */
                    if (paint && !invert_colors) {
                        strcat(text, BG_DF);
                    }

                    /* Append quad-module block */
                    strcat(text, blocks[(invert_colors) ? B_0000 : B_1100]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }

            /* Left border, data, right border */
            for (iv = -border_leftover_v; iv < resolution; iv += modules_per_block_v) {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);
                }

                /* Append left border quad-module blocks */
                for (ih = 0; ih < border_width - border_leftover_h; ih += modules_per_block_h) {
                    strcat(text, blocks[B_0000]);
                }

                /* Append data quad-module blocks */
                bool is_top_half_border_iter = (iv == -1) ? true : false;
                bool is_last_data_row_iter = (iv == resolution - 1) ? true : false;
                for (ih = -border_leftover_h; ih < resolution; ih += modules_per_block_h) {
                    bool is_left_half_border_iter = (ih == -1) ? true : false;
                    bool is_last_data_col_iter = (ih == resolution - 1) ? true : false;

                    unsigned char block_mask = B_0000;

                    if (is_top_half_border_iter) {
                        if (is_left_half_border_iter) {
                            // Able to determine bottom-right module
                            if (data[iv * resolution + ih + resolution + 1] & 1) {
                                block_mask += B_1000;
                            }
                        } else {
                            // Can determine both of bottom modules
                            if (data[iv * resolution + ih + resolution] & 1) {
                                block_mask += B_0010;
                            }
                            if (!is_last_data_col_iter && data[iv * resolution + ih + resolution + 1] & 1) {
                                block_mask += B_1000;
                            }
                        }
                    } else {
                        if (is_left_half_border_iter) {
                            // Can determine both of right modules
                            if (data[iv * resolution + ih + 1] & 1) {
                                block_mask += B_0100;
                            }
                            if (!is_last_data_row_iter && data[iv * resolution + ih + resolution + 1] & 1) {
                                block_mask += B_1000;
                            }
                        } else {
                            // Can determine all modules
                            if (data[iv * resolution + ih] & 1) {
                                block_mask += B_0001;
                            }
                            if (!is_last_data_col_iter && data[iv * resolution + ih + 1] & 1) {
                                block_mask += B_0100;
                            }
                            if (!is_last_data_row_iter) {
                                if (data[iv * resolution + ih + resolution] & 1) {
                                    block_mask += B_0010;
                                }
                                if (!is_last_data_col_iter && data[iv * resolution + ih + resolution + 1] & 1) {
                                    block_mask += B_1000;
                                }
                            }
                        }
                    }

                    strcat(text, blocks[block_mask]);
                }

                /* Append right border quad-module blocks */
                for (ih = 0; ih < border_width - border_leftover_h; ih += modules_per_block_h) {
                    strcat(text, blocks[B_0000]);
                }

                /* Trailing quad-module blocks for right border */
                if (border_leftover_h % modules_per_block_h != 0) {
                    /* Avoid coloring rightmost (transparent) quad-module line */
                    if (paint && !invert_colors) {
                        strcat(text, BG_DF);
                    }

                    /* Append quad-module block */
                    strcat(text, blocks[(invert_colors) ? B_0000 : B_1100]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }

            /* Bottom border */
            for (iv = modules_per_block_v; iv < border_width; iv += modules_per_block_v) {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);
                }

                /* Append quad-module blocks */
                for (ih = 0; ih < l - border_leftover_h; ih += modules_per_block_h) {
                    strcat(text, blocks[B_0000]);
                }

                /* Trailing quad-module blocks for right border */
                if (border_leftover_h % modules_per_block_h != 0) {
                    /* Avoid coloring rightmost (transparent) quad-module line */
                    if (paint && !invert_colors) {
                        strcat(text, BG_DF);
                    }

                    /* Append quad-module block */
                    strcat(text, blocks[(invert_colors) ? B_0000 : B_1100]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }

            /* Trailing quad-module blocks for bottom border */
            if (border_leftover_v == 0 || border_leftover_v % modules_per_block_v != 0) {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);

                    /* Avoid coloring last (transparent) quad-module line */
                    if (!invert_colors) {
                        strcat(text, BG_DF);
                    }
                }

                /* Append quad-module blocks */
                for (ih = 0; ih < l - border_leftover_h; ih += modules_per_block_h) {
                    strcat(text, blocks[(invert_colors) ? B_0000 : B_1010]);
                }

                /* Trailing quad-module blocks for right border */
                if (border_leftover_h % modules_per_block_h != 0) {
                    /* Avoid coloring rightmost (transparent) quad-module line */
                    if (paint && !invert_colors) {
                        strcat(text, BG_DF);
                    }

                    /* Append quad-module block */
                    strcat(text, blocks[(invert_colors) ? B_0000 : B_1110]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }
        } else {
            /*****************************************/
            /*                                       */
            /*  Two modules per block (normal mode)  */
            /*                                       */
            /*****************************************/

            const char *blocks[4] = {
                (invert_colors) ? DBL_BLOCK_00 : DBL_BLOCK_11,
                (invert_colors) ? DBL_BLOCK_10 : DBL_BLOCK_01,
                (invert_colors) ? DBL_BLOCK_01 : DBL_BLOCK_10,
                (invert_colors) ? DBL_BLOCK_11 : DBL_BLOCK_00,
            };
            const char modules_per_block_v = 2;
            const char modules_per_block_h = 1;
            const char border_leftover_v = (border_width % modules_per_block_v);

            const int byte_len =
                (strlen(DBL_BLOCK_11) * l + strlen(EOL)) * l +
                ((paint) ? strlen(BGBK_FGWH) + strlen(BGDF_FGDF) : 0) * l;
            text = malloc(byte_len + 1);
            /* Move cursor to the beginning of *text */
            text[0] = '\0';

            /* Top border */
            for (iv = 0; iv < border_width - border_leftover_v; iv += modules_per_block_v) {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);
                }

                /* Append top border double-module blocks */
                for (ih = 0; ih < l; ih++) {
                    strcat(text, blocks[B_00]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }

            /* Left border, data, right border */
            for (iv = -border_leftover_v; iv < resolution; iv += modules_per_block_v) {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);
                }

                /* Append left border double-module blocks */
                for (ih = 0; ih < border_width; ih += modules_per_block_h) {
                    strcat(text, blocks[B_00]);
                }

                /* Append data double-module blocks */
                bool is_top_half_border_iter = (iv == -1) ? true : false;
                bool is_last_data_row_iter = (iv == resolution - 1) ? true : false;
                for (ih = 0; ih < resolution; ih += modules_per_block_h) {
                    unsigned char block_mask = B_00;

                    if (is_top_half_border_iter) {
                        if (data[iv * resolution + ih + resolution] & 1) {
                            block_mask += B_01;
                        }
                    } else {
                        if (data[iv * resolution + ih] & 1) {
                            block_mask += B_10;
                        }

                        if (!is_last_data_row_iter && data[iv * resolution + ih + resolution] & 1) {
                            block_mask += B_01;
                        }
                    }

                    strcat(text, blocks[block_mask]);
                }

                /* Append right border double-module blocks */
                for (ih = 0; ih < border_width; ih += modules_per_block_h) {
                    strcat(text, blocks[B_00]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }

            /* Bottom border */
            for (iv = modules_per_block_v; iv < border_width; iv += modules_per_block_v) {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);
                }

                /* Append double-module blocks */
                for (ih = 0; ih < l; ih += modules_per_block_h) {
                    strcat(text, blocks[B_00]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }

            /* Trailing double-module blocks for bottom border */
            {
                /* Set palette */
                if (paint) {
                    strcat(text, BGBK_FGWH);
                    /* Avoid coloring last (transparent) double-line */
                    if (!invert_colors) {
                        strcat(text, BG_DF);
                    }
                }

                /* Append double-module blocks */
                for (ih = 0; ih < l; ih += modules_per_block_h) {
                    strcat(text, blocks[(invert_colors) ? B_00 : B_01]);
                }

                /* Reset palette */
                if (paint) {
                    strcat(text, BGDF_FGDF);
                }

                /* Put newline */
                strcat(text, EOL);
            }
        }
    }

    return text;
}

QRencodeMode get_qr_encode_mode(const char encode_mode)
{
    switch (encode_mode) {
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

QRecLevel get_qr_ec_level(const char ec_level)
{
    switch (ec_level) {
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

int main(int argc, char *argv[])
{
    int ret = 0, c = 0;
    char *str = NULL;
    // bool str_is_dynmically_allocated = false;
    const bool is_stdin_input = !isatty(STDIN_FILENO);
    const bool is_term_output = isatty(STDOUT_FILENO);

    // Enable wide-character support
    char *p = setlocale(LC_ALL, "");
    if (!p) {
        print_error("unable to unset locale");
    }

    /* Default options */
    Options options = {
        .anim = false,
        .encode_mode = '8',
        .version = 0,
        .ec_level = '1',
        .large = false,
        .compact = false,
        .border = 1,
        .invert = false,
        .plain = false,
        .unicode = false,
    };

    /* Parse CLI flags and options */
    while ((c = getopt(argc, argv, "am:v:e:lcb:ipuhV")) != -1) {
        switch (c) {
                break;
            case 'a':
                options.anim = true;
                break;

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

            case 'c':
                options.compact = true;
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

    /* Validate and process CLI arguments */
    while (optind < argc) {
        if (argc - optind > 1) {
            print_error("too many arguments");
            fprintf(stderr, "%s" EOL, help_msg);
            ret = 1;
            goto exit;
        }

        // Ignore input provided via CLI when STDIN is available
        if (!is_stdin_input) {
            str = realloc(str, strlen(argv[optind]) + 1);
            strcpy(str, argv[optind]);
        }

        optind++;
    }

    /* Validate given options */
    if (
        options.version < 0 || options.version > QRSPEC_VERSION_MAX ||
        get_qr_ec_level(options.ec_level) < 0 ||
        get_qr_encode_mode(options.encode_mode) == QR_MODE_NUL ||
        options.border < 1 || options.border > 4
    ) {
        print_error("invalid options");
        fprintf(stderr, "%s" EOL, help_msg);
        ret = 1;
        goto exit;
    }

    /* Process STDIN (if any) */
    if (is_stdin_input) {
        size_t buf_size = STDIN_CHUNKSIZE;
        char buf[buf_size];
        if (str != NULL) {
            free(str);
        }
        str = NULL;
        ssize_t stdin_read_size = 0;
        size_t total_bytes = 0;

        while ((stdin_read_size = read(STDIN_FILENO, buf, STDIN_CHUNKSIZE)) > 0) {
            str = realloc(str, total_bytes + stdin_read_size + 1);
            if (str == NULL) {
                print_error("out of memory");
                ret = 1;
                goto exit;
            }
            memcpy(&str[total_bytes], buf, stdin_read_size);
            total_bytes += stdin_read_size;
        }
        str[total_bytes] = '\0';
    }

    /* Check input */
    if (str == NULL || strlen(str) == 0) {
        print_error("no input specified");
        fprintf(stderr, EOL "%s" EOL, help_msg);
        ret = 1;
        goto exit;
    }

    /* Enforce colorless output mode for non-terminal environments */
    if (!is_term_output) {
        options.plain = true;
    }

    /* Ensure that input string contains UTF-8 BOM */
    if (options.unicode && !is_utf8_string(str)) {
        /* Prepend UTF-8 BOM */
        str = realloc(str, strlen(utf8_bom) + strlen(str) + 1);
        memmove(str + strlen(utf8_bom), str, strlen(str) + 1);
        memcpy(str, utf8_bom, strlen(utf8_bom));
    }

    /*********************************/
    /*                               */
    /*  Generate and output QR code  */
    /*                               */
    /*********************************/

    if (options.anim) {
        // Determine amount of chunks (frames)
        const int data_chunk_size = 100; // Max chunk size in bytes
        size_t data_chunk_count = strlen(str) / data_chunk_size;
        // Account for leftover error (if any)
        if (data_chunk_count * data_chunk_size < strlen(str)) {
            data_chunk_count++;
        }
        unsigned char data_chunk[data_chunk_size + 1];

        while (true) {
            for (size_t str_chunk_i = 0; str_chunk_i < data_chunk_count; str_chunk_i++) {
                strncpy((char*)data_chunk, str + data_chunk_size * str_chunk_i, data_chunk_size + 1);
                if (str_chunk_i == data_chunk_count - 1) {
                    /* Since the whole chunk gets converted into our QR code, let's wipe its tail */
                    // TODO
                }
                QRcode *qr = QRcode_encodeData(data_chunk_size, data_chunk,
                                                 options.version,
                                                 get_qr_ec_level(options.ec_level));
                                                 // get_qr_encode_mode(options.encode_mode),
                                                 // true);

                int line_num_to_clear = qr->width + options.border * 2;
                if (!options.large) {
                    // Small (default) and compact (-c) modes both use 2 blocks per line
                    line_num_to_clear /= 2;
                    // Account for trailing half-block line
                    line_num_to_clear += 1;
                }
                const int clear_str_len = strlen(CLR) * line_num_to_clear + 1;
                char clear_str[clear_str_len + 1];
                clear_str[0] = clear_str[clear_str_len] = '\0';
                for (int j = 0, jlen = line_num_to_clear; j < jlen; j++) {
                    strcat(clear_str, CLR);
                }

                /* Bail out if unable to successfully execute QRcode_encodeString() */
                if (qr == NULL) {
                    print_error("failed to generate QR code");
                    ret = 1;
                    goto exit;
                }

                /* Convert QR code data into text */
                char *qr_code_text = qr_data_to_text(qr,
                                                     options.border,
                                                     options.invert,
                                                     !options.plain,
                                                     options.large,
                                                     options.compact);

                /* Output QR code as text */
                if (qr_code_text) {
                    printf("%s", qr_code_text);
                } else {
                    print_error("failed to convert QR code data into text");
                    ret = 1;
                }

                QRcode_free(qr);

                /* Clean up */
                free(qr_code_text);

                // Delay between frames (in mc)
                if (is_term_output) {
                    // Hang it here if it's just one frame
                    while(data_chunk_count == 1);

                    // Small delay before wiping the current and showing next frame
                    msleep(100);

                    // Wipe previously printed QR code
                    printf("%s", clear_str);
                }
            }

            // Only one iteration if outputting into a file
            if (!is_term_output) {
                break;
            }
        }
    } else {
        QRcode *qr = QRcode_encodeString(str,
                                         options.version,
                                         get_qr_ec_level(options.ec_level),
                                         get_qr_encode_mode(options.encode_mode),
                                         true);

        /* Bail out if unable to successfully execute QRcode_encodeString() */
        if (qr == NULL) {
            print_error("failed to generate QR code");
            ret = 1;
            goto exit;
        }

        /* Convert QR code data into text */
        char *qr_code_text = qr_data_to_text(qr,
                                             options.border,
                                             options.invert,
                                             !options.plain,
                                             options.large,
                                             options.compact);

        /* Output QR code as text */
        if (qr_code_text) {
            printf("%s", qr_code_text);
        } else {
            print_error("failed to convert QR code data into text");
            ret = 1;
        }

        QRcode_free(qr);

        /* Clean up */
        free(qr_code_text);
    }

exit:
    return ret;
}
