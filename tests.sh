#!/bin/sh
# SPDX-License-Identifier: 0BSD
#
# Test suite for qr.  Usage: sh tests.sh [NUMBER...]
#
# Runs every test, or only the ones numbered.  Settings come from the
# environment (make test passes them along):
#   QR       the program to test (default: ./qr)
#   VERSION  the version it should report (default: the one in config.mk)
#   CONVERT  ImageMagick's command (default: magick, or else convert)
#   FONT     font for ImageMagick to draw text QR codes with (default: FreeMono)
# Scanning tests also need zbarimg (zbar), and one needs python3.

###############################################################################
# Test framework (POSIX sh; works with any C program, not just this one)
###############################################################################

total=0 passed=0 failed=0
selected=" $* "
scratch=$(pwd)/tests.tmp
rm -rf "$scratch"

#
# check NAME [STATUS [STDOUT [STDERR]]] <<'EOF'
# commands
# EOF
#
# Runs the commands (read from STDIN) in a new empty directory, and checks
# that they exit with STATUS (default: 0) and write exactly STDOUT and STDERR
# (default: nothing).  Give "*" to accept any output.  The commands get
# /dev/null for STDIN, and run in tests.tmp/NUMBER, which is kept when the
# test fails, to look into.
#
check()
{
    total=$((total + 1))
    name=$1 status=${2-0} stdout=${3-} stderr=${4-}
    body=$(cat)

    case $selected in
        "  "|*" $total "*) ;;
        *) return 0 ;;
    esac

    dir=$scratch/$total
    mkdir -p "$dir" || exit 2
    (cd "$dir" && eval "$body") > "$dir/.stdout" 2> "$dir/.stderr" < /dev/null
    actual=$?
    printf '%s' "$stdout" > "$dir/.expected-stdout"
    printf '%s' "$stderr" > "$dir/.expected-stderr"

    problems=
    if [ "$actual" != "$status" ]; then
        problems="exited with $actual instead of $status"
    fi
    if differs stdout "$stdout"; then
        problems="$problems${problems:+, }unexpected stdout"
    fi
    if differs stderr "$stderr"; then
        problems="$problems${problems:+, }unexpected stderr"
    fi

    if [ -z "$problems" ]; then
        passed=$((passed + 1))
        printf '%3d  ok    %s\n' "$total" "$name"
        rm -rf "$dir"
    else
        failed=$((failed + 1))
        printf '%3d  FAIL  %s: %s (see tests.tmp/%d)\n' "$total" "$name" "$problems" "$total"
        for stream in stdout stderr; do
            if differs $stream "$(cat "$dir/.expected-$stream")"; then
                diff "$dir/.expected-$stream" "$dir/.$stream" 2>&1 |
                    head -n 20 | sed "s/^/      $stream: /"
            fi
        done
    fi
}

# differs STREAM EXPECTED: whether the test's STREAM isn't what's expected
differs()
{
    [ "$2" != '*' ] && ! cmp -s "$dir/.expected-$1" "$dir/.$1"
}

# Print the results; exit with 1 if anything failed
finish()
{
    echo
    echo "$passed of $((passed + failed)) tests passed"
    if [ "$failed" -eq 0 ]; then
        rm -rf "$scratch"
        exit 0
    fi
    exit 1
}

###############################################################################
# Settings and helpers
###############################################################################

QR=${QR:-./qr}
case $QR in
    /*) ;;
    *) QR=$(pwd)/$QR ;;
esac
VERSION=${VERSION:-$(sed -n 's/^VERSION *= *//p' "$(dirname "$0")/config.mk")}
CONVERT=${CONVERT:-$(command -v magick || echo convert)}
FONT=${FONT:-FreeMono}

qr()
{
    "$QR" "$@"
}

INPUT='Ünic0d3wörd 参 я'
# (The braces keep macOS's old bash from taking the text as part of the name)
EXTRA_LONG_INPUT=
i=0
while [ $i -lt 54 ]; do
    EXTRA_LONG_INPUT=${EXTRA_LONG_INPUT}参考文献に掲載されている文章等を抜粋し
    i=$((i + 1))
done

# Draw text (output of qr) with ImageMagick and scan it: BG FG POINTSIZE TEXT
scan_text()
{
    $CONVERT -background "$1" -fill "$2" -font "$FONT" -pointsize "$3" \
        -interline-spacing -1 label:"$4" png:- | zbarimg -q png:-
}

#
# scan DAMAGE [ZBARIMG-OPTIONS...] < output of qr -p -l -b 4
#
# Turns the symbol into a PBM image (a module is two characters wide and one
# line tall), scales it up and reads it with zbarimg.  DAMAGE is the side of a
# square of modules to flip, starting at module 16 (0 for none).
#
scan()
{
    damage=$1
    shift
    cat > scan.txt
    n=$(wc -l < scan.txt | tr -d ' ')
    { echo P1; echo "$n $n"; LC_ALL=C sed 's/██/0/g; s/  /1/g' scan.txt |
      awk -v k="$damage" 'NR > 16 && NR <= 16 + k {
        for (i = 17; i <= 16 + k; i++) {
            c = (substr($0, i, 1) == "0") ? "1" : "0"
            $0 = substr($0, 1, i - 1) c substr($0, i + 1)
        } } { print }'; } |
      $CONVERT pbm:- -scale 300% png:- | zbarimg -q "$@" png:-
}

# gen MODE LENGTH: print LENGTH characters of data for MODE n, a, 8 or k
gen()
{
    LC_ALL=C awk -v m="$1" -v n="$2" 'BEGIN {
        an = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:"
        for (i = 0; i < n; i++) {
            if (m == "n") printf "%d", (i * 7 + i * i + 3) % 10
            else if (m == "a") printf "%s", substr(an, (i * 11 + i * i) % 45 + 1, 1)
            else if (m == "8") printf "%c", 97 + (i * 5 + i * i) % 26
            else { s = 64 + (i * 13 + i * i) % 188; if (s >= 127) s++; printf "%c%c", 137 + i % 16, s }
        } }'
}

# Number of characters each version holds, for the n, a, 8 and k modes at
# EC levels L, M, Q and H (ISO/IEC 18004:2015, Table 7)
capacity()
{
    cat <<'EOF'
1 41 34 27 17 25 20 16 10 17 14 11 7 10 8 7 4
2 77 63 48 34 47 38 29 20 32 26 20 14 20 16 12 8
3 127 101 77 58 77 61 47 35 53 42 32 24 32 26 20 15
4 187 149 111 82 114 90 67 50 78 62 46 34 48 38 28 21
5 255 202 144 106 154 122 87 64 106 84 60 44 65 52 37 27
6 322 255 178 139 195 154 108 84 134 106 74 58 82 65 45 36
7 370 293 207 154 224 178 125 93 154 122 86 64 95 75 53 39
8 461 365 259 202 279 221 157 122 192 152 108 84 118 93 66 52
9 552 432 312 235 335 262 189 143 230 180 130 98 141 111 80 60
10 652 513 364 288 395 311 221 174 271 213 151 119 167 131 93 74
11 772 604 427 331 468 366 259 200 321 251 177 137 198 155 109 85
12 883 691 489 374 535 419 296 227 367 287 203 155 226 177 125 96
13 1022 796 580 427 619 483 352 259 425 331 241 177 262 204 149 109
14 1101 871 621 468 667 528 376 283 458 362 258 194 282 223 159 120
15 1250 991 703 530 758 600 426 321 520 412 292 220 320 254 180 136
16 1408 1082 775 602 854 656 470 365 586 450 322 250 361 277 198 154
17 1548 1212 876 674 938 734 531 408 644 504 364 280 397 310 224 173
18 1725 1346 948 746 1046 816 574 452 718 560 394 310 442 345 243 191
19 1903 1500 1063 813 1153 909 644 493 792 624 442 338 488 384 272 208
20 2061 1600 1159 919 1249 970 702 557 858 666 482 382 528 410 297 235
21 2232 1708 1224 969 1352 1035 742 587 929 711 509 403 572 438 314 248
22 2409 1872 1358 1056 1460 1134 823 640 1003 779 565 439 618 480 348 270
23 2620 2059 1468 1108 1588 1248 890 672 1091 857 611 461 672 528 376 284
24 2812 2188 1588 1228 1704 1326 963 744 1171 911 661 511 721 561 407 315
25 3057 2395 1718 1286 1853 1451 1041 779 1273 997 715 535 784 614 440 330
26 3283 2544 1804 1425 1990 1542 1094 864 1367 1059 751 593 842 652 462 365
27 3517 2701 1933 1501 2132 1637 1172 910 1465 1125 805 625 902 692 496 385
28 3669 2857 2085 1581 2223 1732 1263 958 1528 1190 868 658 940 732 534 405
29 3909 3035 2181 1677 2369 1839 1322 1016 1628 1264 908 698 1002 778 559 430
30 4158 3289 2358 1782 2520 1994 1429 1080 1732 1370 982 742 1066 843 604 457
31 4417 3486 2473 1897 2677 2113 1499 1150 1840 1452 1030 790 1132 894 634 486
32 4686 3693 2670 2022 2840 2238 1618 1226 1952 1538 1112 842 1201 947 684 518
33 4965 3909 2805 2157 3009 2369 1700 1307 2068 1628 1168 898 1273 1002 719 553
34 5253 4134 2949 2301 3183 2506 1787 1394 2188 1722 1228 958 1347 1060 756 590
35 5529 4343 3081 2361 3351 2632 1867 1431 2303 1809 1283 983 1417 1113 790 605
36 5836 4588 3244 2524 3537 2780 1966 1530 2431 1911 1351 1051 1496 1176 832 647
37 6153 4775 3417 2625 3729 2894 2071 1591 2563 1989 1423 1093 1577 1224 876 673
38 6479 5039 3599 2735 3927 3054 2181 1658 2699 2099 1499 1139 1661 1292 923 701
39 6743 5313 3791 2927 4087 3220 2298 1774 2809 2213 1579 1219 1729 1362 972 750
40 7089 5596 3993 3057 4296 3391 2420 1852 2953 2331 1663 1273 1817 1435 1024 784
EOF
}

# Symbols checked module by module against Project Nayuki's QR Code
# generator: mode, EC level, cksum of qr -p -l, data (printf format)
golden()
{
    cat <<'EOF'
8 l 1631119726 2293 Hello\054\040World\041
8 m 5586349 2269 Hello\054\040World\041
8 q 1519868773 3057 Hello\054\040World\041
8 h 2346809608 3129 Hello\054\040World\041
n l 3131544879 2285 0123456789
n h 2182554410 4105 31415926535897932384626433832795028841971693993751
a l 1791494306 2261 HELLO\040WORLD
a q 4044592551 3065 HTTPS\072\057\057EXAMPLE\056COM\057\044\045\052\053\055\056\057\072
8 l 1460265313 3065 https\072\057\057github\056com\057Y2Z\057qr
8 m 2137036348 3105 \303\234nic0d3w\303\266rd\040\345\217\202\040\321\217
8 h 2849240437 8941 bitcoin\072bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq\077amount\0750\056001
8 l 2920509932 5177 WIFI\072T\072WPA\073S\072my\040network\073P\072correct\040horse\040battery\040staple\073\073
8 q 461293336 22873 \001\002\003\004\005\006\007\010\011\013\014\015\016\017\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\0570123456789\072\073\074\075\076\077\100ABCDEFGHIJKLMNOPQRSTUVWXYZ\133\134\135\136\137\140abcdefghijklmnopqrstuvwxyz\173\174\175\176\177\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377
8 l 4030646647 43133 the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040the\040quick\040brown\040fox\040jumps\040over\040the\040lazy\040dog\040
n m 4124686090 28553 9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999
k h 2308645273 4105 \212\277\216\232\203\202\201\133\203h\202\314\223\372\226\173\214\352\203e\203L\203X\203g
k l 3301375820 3973 QR\203R\201\133\203h\202\3151994\224N\202\311\203f\203\223\203\134\201\133\203E\203F\201\133\203u\202\252\224\255\226\276\202\265\202\275\201B
8 m 284431741 4117 Order\040\04312345678901234567890\072\0403\040x\040WIDGETS\040\100\040\0449\05699
8 q 3909440146 3041 tel\072\0531\055555\0550123\0554567\0558901
8 l 3287937660 96993 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
8 h 2235751775 5125 MIXED\072\04012345678901234567890\040abc\040DEF\0400987654321
EOF
}

HELP='Usage: qr [OPTIONS] STRING
  or:  cat FILE | qr [OPTIONS]

Options:
  -m  QR mode       [na8k] (n = number, a = alphabet, 8 = 8-bit, k = Kanji)
  -v  QR version    [1-40] (the default is 10 for -a)
  -e  QR EC level   [lmqh] or [1-4]
  -l  use two characters per block
  -c  compact mode
  -b  border width  [1-4] (the default is 1)
  -i  invert colors
  -p  force colorless output
  -u  ensure output has UTF-8 BOM
  -a  animate data  [bbqr|bbqr-hex|bbqr-base32|bbqr-zlib|ur|numbered|txqr]
  -t  data type     [auto|psbt|tx|json|cbor|text|binary] (for -a)
  -f  frame rate    [1-30] (frames per second, the default is 4)
  -n  number of frames to output (for -a)
  -r  print frame contents instead of QR codes (for -a)
  -h  print help info and exit
  -V  print version info and exit'

###############################################################################
# Command line and rendering
###############################################################################

check 'generates proper QR Code' <<'EOF'
scan_text black white 9 "$(qr "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code using inverted colors' <<'EOF'
scan_text white black 9 "$(qr -i "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code using compact blocks' <<'EOF'
scan_text black white 9 "$(qr -c "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code using compact blocks and inverted colors' <<'EOF'
scan_text white black 9 "$(qr -ci "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code using large blocks' <<'EOF'
scan_text black white 4 "$(qr -l "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code using large blocks and inverted colors' <<'EOF'
scan_text white black 4 "$(qr -li "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code using large compact blocks' <<'EOF'
scan_text black white 4 "$(qr -lc "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code using large compact blocks and inverted colors' <<'EOF'
scan_text white black 9 "$(qr -lci "$INPUT")" | grep -q "QR-Code:$INPUT"
EOF

check 'generates proper QR Code with default settings using stdin' <<'EOF'
scan_text black white 9 "$(printf '%s' "$INPUT" | qr)" | grep -q "QR-Code:$INPUT"
EOF

check 'fails to generate an empty QR Code' 1 '' "Error: no input specified

$HELP

" <<'EOF'
qr ""
EOF

check 'fails and prints help information when no arguments provided' 1 '' "Error: no input specified

$HELP

" <<'EOF'
qr
EOF

check 'prints help information when help flag is set' 0 "$HELP

" <<'EOF'
qr -h
EOF

check 'prints version information when version flag is set' 0 "qr $VERSION
" <<'EOF'
qr -V
EOF

check 'fails if the input is too long' 1 '' 'Error: failed to generate QR code
' <<'EOF'
qr "$EXTRA_LONG_INPUT"
EOF

check 'fails when given more than one STRING' 0 'Error: too many arguments
' <<'EOF'
qr foo bar 2>&1 | head -n 1
EOF

check 'accepts options after STRING' <<'EOF'
test "$(qr -p foo -i -c)" = "$(qr -p -i -c foo)"
EOF

check 'reads all of STDIN' <<'EOF'
s=$(printf '%s' "$EXTRA_LONG_INPUT" | head -c 300)
test "$(printf '%s' "$s" | qr -p)" = "$(qr -p "$s")"
EOF

check 'prefers STRING over STDIN' <<'EOF'
test "$(echo bar | qr -p foo)" = "$(qr -p foo < /dev/null)"
EOF

check 'rejects invalid option values' <<'EOF'
for o in '-e z' '-e lol' '-v abc' '-v 41' '-b 0' '-b 2x' '-m x'; do
    qr $o foo 2> /dev/null && echo "accepted: $o"
done
true
EOF

check 'generates proper QR Code in numeric and alphanumeric modes' <<'EOF'
scan_text black white 9 "$(qr -m n 0123456789)" | grep -q 'QR-Code:0123456789' &&
scan_text black white 9 "$(qr -m a HELLO-WORLD)" | grep -q 'QR-Code:HELLO-WORLD'
EOF

check 'does not add a second UTF-8 BOM' <<'EOF'
test "$(printf '\357\273\277foo' | qr -p -u)" = "$(qr -p -u foo)"
EOF

check 'encodes binary data from STDIN' <<'EOF'
printf 'a\000b' | qr -p > with_nul && qr -p a > without_nul && ! cmp -s with_nul without_nul
EOF

###############################################################################
# Animated QR Codes
###############################################################################

# Parts are numbered in base 36, compressible data gets zlib-compressed, and
# parts are equally sized, except for the last one
check 'splits data into BBQr frames' 0 'B$HU010068656C6C6F
B$2B0100NBSWY3DP
B$2J0100PMRGCIR2EAYX2CQ
B$2B1400
B$2B1410
B$ZB
12
1
' <<'EOF'
printf 'hello' | qr -a bbqr-hex -r
printf 'hello' | qr -a bbqr-base32 -t binary -r
echo '{"a": 1}' | qr -a bbqr -t json -r
head -c 400 /dev/urandom | qr -a bbqr-base32 -v 1 -r | cut -c 1-8 | sed -n '1p;37p'
head -c 30000 /dev/zero | qr -a bbqr -r | cut -c 1-4 | uniq
head -c 3000 /dev/urandom | qr -a bbqr-base32 -v 10 -r |
    awk '{ print length($0) }' | uniq -c | awk '{ print $1 }'
EOF

check 'detects PSBTs in binary, base64 and hex form' 0 'B$HP010070736274FF0102
B$HP010070736274FF0102
B$HP010070736274FF0102
exit 1
B$HT01000200ABCD
' 'Error: input is not a PSBT
' <<'EOF'
printf 'psbt\377\001\002' > psbt.bin
qr -a bbqr-hex -r < psbt.bin
printf 'cHNidP8BAg==\n' | qr -a bbqr-hex -r
printf '70736274FF0102' | qr -a bbqr-hex -r
printf 'not a psbt' | qr -a bbqr -t psbt -r
echo "exit $?"
printf '0200aBcD' | qr -a bbqr-hex -t tx -r
EOF

check 'decodes and joins BBQr frames' <<'EOF'
head -c 5000 /dev/urandom > data
qr -a bbqr -v 8 -r < data > frames
python3 -c '
import sys, zlib, base64
parts = sorted(open("frames").read().split(), key=lambda p: int(p[6:8], 36))
assert all(p[:6] == parts[0][:6] for p in parts) and int(parts[0][4:6], 36) == len(parts)
enc, body = parts[0][2], "".join(p[8:] for p in parts)
data = bytes.fromhex(body) if enc == "H" else base64.b32decode(body + "=" * (-len(body) % 8))
if enc == "Z": data = zlib.decompress(data, wbits=-10)
sys.stdout.buffer.write(data)' | cmp - data
EOF

check 'generates scannable BBQr QR Codes' <<'EOF'
scan_text black white 9 "$(printf 'hello' | qr -a bbqr-hex)" | grep -q 'QR-Code:B\$HU010068656C6C6F'
EOF

check 'rejects options for animated QR Codes without -a' 0 'Error: invalid options
Error: invalid options
Error: invalid options
Error: invalid options
' <<'EOF'
qr -t psbt foo 2>&1 | head -n 1
qr -r foo 2>&1 | head -n 1
qr -n 2 foo 2>&1 | head -n 1
qr -a bbqr -m n 1 2>&1 | head -n 1
EOF

check 'outputs the requested number of frames' 0 'B$HU010068656C6C6F
B$HU010068656C6C6F
B$HU010068656C6C6F
B$2B0700
B$2B0701
B$2B0702
B$2B0703
B$2B0704
' <<'EOF'
printf 'hello' | qr -a bbqr-hex -r -n 3
head -c 600 /dev/urandom | qr -a bbqr-base32 -v 5 -r -n 5 | cut -c 1-8
test "$(qr -a bbqr-hex -n 3 hello | grep -c '^$')" = 2
EOF

# The first one is a test vector from the reference implementation (bc-ur)
check 'encodes data as single-part URs' 0 'UR:BYTES/HDEYMEJTSWHHYLKEPMYKHHTSYTSNOYOYAXAEDSUTTYDMMHHPKTPMSRJTGWDPFNSBOXGWLBAAWZUEFYWKDPLRSRJYNBVYGABWJLDAPFCSDWKBRKCH
UR:CRYPTO-PSBT/FLJOJKIDJYZMADAOINLNSFRS
' <<'EOF'
printf '\221\156\306\134\367\174\255\365\134\327\371\315\241\241\003\000\046\335\324\056\220\133\167\255\303\156\117\055\074\313\244\117\177\004\362\336\104\364\055\204\303\164\240\341\111\023\157\045\260\030' | qr -a ur -r
printf 'psbt\377\001\002' | qr -a ur -r
EOF

check 'splits data into multi-part URs with fountain codes' 0 '1 UR:BYTES/1-18
1 UR:BYTES/18-18
1 UR:BYTES/19-18
1 UR:BYTES/40-18
' <<'EOF'
head -c 3000 /dev/urandom | qr -a ur -r -n 40 | cut -d / -f 1-2 | uniq -c |
    awk '{ print $1, $2 }' | sed -n '1p;18p;19p;40p'
EOF

check 'generates scannable UR QR Codes' <<'EOF'
scan_text black white 9 "$(printf 'hello' | qr -a ur)" | grep -q 'QR-Code:UR:BYTES/FEISIHJZJZJLCKIHJEVD'
EOF

# Parts join back into the original text, none starts in the middle of a
# character (with a UTF-8 continuation byte), PSBTs are sent in base64 and
# transactions in hex
check 'splits text into numbered parts' 0 'hello
p1of3
p2of3
p3of3
cHNidP8BAg==
0200abcd
' <<'EOF'
printf 'hello' | qr -a numbered -r
printf '%0300d' 0 | qr -a numbered -v 5 -r | cut -c 1-5
printf 'Ünic0d3wörd 参 я %.0s' 1 2 3 4 5 6 7 8 9 10 > text
qr -a numbered -v 2 -r < text > parts
sed 's/^p[0-9]*of[0-9]* //' parts | tr -d '\n' | cmp - text
! LC_ALL=C grep -q "^p[0-9]*of[0-9]* $(printf '[\200-\277]')" parts || echo 'split a character'
printf 'psbt\377\001\002' | qr -a numbered -r
printf '0200aBcD' | qr -a numbered -t tx -r
EOF

# The checksums are of the frames made by the reference implementation (Go)
check 'encodes data as TXQR frames' 0 '0/258/5|hello
0/257/12|cHNidP8BAg==
338318238 120
3887260687 379
' <<'EOF'
printf 'hello' | qr -a txqr -r
printf 'psbt\377\001\002' | qr -a txqr -r
printf 'hello world, this is a txqr test message!' | qr -a txqr -v 3 -r | cksum
printf 'hello world, this is a txqr test message!' | qr -a txqr -v 1 -r -n 30 | cksum
EOF

###############################################################################
# QR Code encoder
###############################################################################

check 'fills every version to its capacity in every mode and EC level' <<'EOF'
capacity | while read v caps; do
    set -- $caps
    for m in n a 8 k; do for e in l m q h; do
        gen $m $1 > fits; gen $m $(($1 + 1)) > overflows; shift
        w=$(qr -p -l -m $m -e $e < fits | wc -l)
        test $w -eq $((4 * v + 19)) || echo "$m $e: version $v holds $(wc -c < fits) bytes, got width $w"
        if w=$(qr -p -l -m $m -e $e < overflows 2> /dev/null | wc -l) && test $w -gt 0; then
            test $v -lt 40 && test $w -eq $((4 * v + 23)) || echo "$m $e: overflowing version $v gave width $w"
        else
            test $v -eq 40 || echo "$m $e: overflowing version $v failed"
        fi
    done; done
done
EOF

# Every mode in every version, cycling through the EC levels
check 'decodes symbols of every version filled to capacity' <<'EOF'
capacity | while read v caps; do
    i=0
    for m in n a 8 k; do
        j=$(((v + i) % 4))
        e=$(echo lmqh | cut -c $((j + 1)))
        n=$(echo $caps | cut -d ' ' -f $((i * 4 + j + 1)))
        gen $m $n > data
        qr -p -l -b 4 -m $m -e $e < data | scan 0 -Sbinary > decoded
        cmp -s data decoded || echo "$m $e: version $v did not decode"
        i=$((i + 1))
    done
done
EOF

check 'matches reference symbols module by module' <<'EOF'
golden | while read -r m e sum size data; do
    set -- $(printf "$data" | qr -p -l -m $m -e $e | cksum)
    test "$1 $2" = "$sum $size" || printf '%s %s: %s\n' $m $e "$data"
done
EOF

check 'decodes reference symbols byte for byte' <<'EOF'
golden | while read -r m e sum size data; do
    printf "$data" > data
    qr -p -l -b 4 -m $m -e $e < data | scan 0 -Sbinary > decoded
    cmp -s data decoded || printf '%s %s: %s\n' $m $e "$data"
done
EOF

# Each of these needs a bigger symbol when encoded as 8-bit data alone
check 'mixes modes to make smaller symbols' <<'EOF'
for t in 'm 29 Order #12345678901234567890: 3 x WIDGETS @ $9.99' \
         'h 33 MIXED: 12345678901234567890 abc DEF 0987654321' \
         'q 25 tel:+1-555-0123-4567-8901' \
         'l 29 https://example.com/order/123456789012345678901234567890' \
         'q 33 ISBN 978-3-16-148410-0, PRICE 12.50 EUR, ref 00042'; do
    set -- $t; e=$1; w=$2; shift 2; s="$*"
    test $(qr -p -l -e $e "$s" | wc -l) -eq $((w + 2)) || echo "$e: wrong size for $s"
    qr -p -l -b 4 -e $e "$s" | scan 0 --raw | tr -d '\r' > decoded
    printf '%s\n' "$s" | cmp -s - decoded || echo "$e: $s did not decode"
done
EOF

# 13 characters fit version 3-H only in Kanji mode.  Then the first and last
# characters of both Kanji ranges (0x8140-0x9FFC and 0xE040-0xEBBF): ten of
# them fit version 1-L, but only as Kanji.  Byte pairs just outside the
# ranges are kept as 8-bit data, and text which is not all Kanji works too.
check 'encodes Shift JIS text in Kanji mode' 0 '31
35
漢字モードの日本語テキスト
QRコードは日本の発明です
23
27
Hello, World!
' <<'EOF'
printf '\212\277\216\232\203\202\201\133\203h\202\314\223\372\226\173\214\352\203e\203L\203X\203g' > kanji
qr -p -l -m k -e h < kanji | wc -l | tr -d ' '
qr -p -l -m 8 -e h < kanji | wc -l | tr -d ' '
qr -p -l -b 4 -m k -e h < kanji | scan 0 --raw | tr -d '\r'
printf 'QR\203R\201\133\203h\202\315\223\372\226\173\202\314\224\255\226\276\202\305\202\267' |
    qr -p -l -b 4 -m k | scan 0 --raw | tr -d '\r'
printf '\201\100\237\374\340\100\353\277\201\100\237\374\340\100\353\277\210\237\230\100' > edges
qr -p -l -m k < edges | wc -l | tr -d ' '
qr -p -l -b 4 -m k < edges | scan 0 -Sbinary | cmp - edges
printf '\201\077\201\177\237\375\240\100\337\374\340\077\353\300\354\100\201\375\377\377' > others
qr -p -l -m k < others | wc -l | tr -d ' '
qr -p -l -b 4 -m k < others | scan 0 -Sbinary | cmp - others
qr -p -l -b 4 -m k 'Hello, World!' | scan 0 --raw | tr -d '\r'
EOF

check 'fails to force a mode the data does not fit' 0 '1
1
1
1
' 'Error: failed to generate QR code
Error: failed to generate QR code
Error: failed to generate QR code
Error: failed to generate QR code
' <<'EOF'
qr -m n 12a; echo $?
qr -m n ' 123'; echo $?
qr -m a hello; echo $?
qr -m a 'HELLO#'; echo $?
EOF

check 'encodes forced numeric and alphanumeric modes' 0 '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:
00000000000000000000123
27
' <<'EOF'
qr -p -l -b 4 -m a '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:' | scan 0 --raw | tr -d '\r'
qr -p -l -b 4 -m n 00000000000000000000123 | scan 0 --raw | tr -d '\r'
qr -p -l -m a '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:' | wc -l | tr -d ' '
EOF

# Line feeds are left out: Windows builds of zbarimg would turn them into CRLF
check 'encodes every byte value' <<'EOF'
LC_ALL=C awk 'BEGIN { for (i = 0; i < 256; i++) if (i != 10) printf "%c", i }' > bytes
test $(wc -c < bytes) -eq 255 || echo 'awk made the wrong bytes'
for e in l m q h; do
    qr -p -l -b 4 -e $e < bytes | scan 0 -Sbinary | cmp - bytes || echo "$e failed"
done
printf '\000\000\000\377\377\377\000' > nul
qr -p -l -b 4 < nul | scan 0 -Sbinary | cmp - nul
EOF

# It is a minimum: data that does not fit gets a bigger symbol (version 3)
check 'honors the minimum version' 0 '31
' <<'EOF'
v=1
while test $v -le 40; do
    w=$(qr -p -l -v $v a | wc -l)
    test $w -eq $((4 * v + 19)) || echo "version $v gave width $w"
    v=$((v + 1))
done
qr -p -l -v 1 "$(printf '%0100d' 0)x" | wc -l | tr -d ' '
EOF

check 'draws borders of every width' 0 '23
139
25
151
27
163
29
175
' <<'EOF'
for b in 1 2 3 4; do
    qr -p -l -b $b a | wc -l | tr -d ' '
    qr -p -l -b $b a | head -n 1 | wc -c | tr -d ' '
done
EOF

# A square of flipped modules each EC level recovers from, which is too much
# for a lower level
check 'corrects errors in damaged symbols' 0 'Hello, World!
Hello, World!
Hello, World!
Hello, World!
exit 4
' <<'EOF'
for t in 'l 5' 'm 8' 'q 11' 'h 14'; do
    set -- $t
    qr -p -l -b 4 -v 5 -e $1 'Hello, World!' | scan $2 --raw | tr -d '\r'
done
qr -p -l -b 4 -v 5 -e l 'Hello, World!' | scan 14 --raw
echo "exit $?"
EOF

finish
