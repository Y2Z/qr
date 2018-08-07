# qr

Print Unicode-friendly QR codes straight in your terminal.

![qr-snapshot](https://user-images.githubusercontent.com/1392048/38160038-0a03cbf2-3484-11e8-916d-2428085cb0f8.png)

## Build

    $ make clean all


## Install

    # make install
or

    $ sudo PREFIX=/usr/local make install

### Usage

    $ qr "Hello"
or

    $ echo -n "Hello" | qr


## Options

    Usage: qr [OPTIONS] STRING
      or:  cat FILE | qr [OPTIONS]

    Options:
      -m  QR mode       [na8k] (n = number, a = alphabet, 8 = 8-bit, k = Kanji)
      -v  QR version    [1-40]
      -e  QR EC level   [lmqh] or [1-4]
      -l  use two characters per block
      -b  border width  [1-10] (the default is 2)
      -i  invert colors
      -p  force colorless output
      -h  print help (this message)


## Uninstall

    # make uninstall


## Dependencies

 - [libqrencode](https://github.com/fukuchi/libqrencode)

### Resolve on Ubuntu or Debian

    $ apt-get install libqrencode-dev

### Resolve on macOS

    $ brew install qrencode

### Build from source

    $ git clone https://github.com/fukuchi/libqrencode.git && cd libqrencode
    $ ./configure --prefix=/usr/local
    $ make
    # make install


## Acknowledgements

**QR Code** is a registered trademark of DENSO WAVE INCORPORATED in Japan
and other countries.


## License

The Unlicense
