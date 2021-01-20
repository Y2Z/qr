[![Travis CI Build Status](https://travis-ci.org/Y2Z/qr.svg?branch=master)](https://travis-ci.org/Y2Z/qr)

# qr

Print Unicode-friendly QR codes straight in your terminal.

![qr-screencast](https://user-images.githubusercontent.com/1392048/47276020-47b04800-d582-11e8-9da0-b09d0c949720.gif)


## How to build

    $ make

#### Build dependencies
 - [libqrencode](https://github.com/fukuchi/libqrencode)
   ###### Resolve on Ubuntu or Debian
       $ apt-get install libqrencode-dev
   ###### Resolve on macOS
       $ brew install qrencode
   ###### Build from source
       $ git clone https://github.com/fukuchi/libqrencode.git && cd libqrencode
       $ ./configure --prefix=/usr/local
       $ make
       # make install


## How to install

    # make install
or

    $ sudo PREFIX=/usr/local make install


## Usage

    $ qr "Hello"
or

    $ echo -n "Hello" | qr

#### Options

    Usage: qr [OPTIONS] STRING
    or:  cat FILE | qr [OPTIONS]

    Options:
    -m  QR mode       [na8k] (n = number, a = alphabet, 8 = 8-bit, k = Kanji)
    -v  QR version    [1-40]
    -e  QR EC level   [lmqh] or [1-4]
    -l  use two characters per block
    -c  compact mode
    -b  border width  [1-4] (the default is 1)
    -i  invert colors
    -p  force colorless output
    -h  print help (this message)


## How to remove

    # make uninstall


## Running tests

    $ make test

#### Test dependencies
 - [autoconf](https://www.gnu.org/software/autoconf/autoconf.html)
   ###### Resolve on Ubuntu or Debian
       $ apt-get install autoconf
   ###### Resolve on macOS
       $ brew install autoconf
 - [zbar](http://zbar.sourceforge.net)
   ###### Resolve on Ubuntu or Debian
       $ apt-get install zbar-tools
   ###### Resolve on macOS
       $ brew install zbar
 - [imagemagick](https://www.imagemagick.org/script/index.php)
   ###### Resolve on Ubuntu or Debian
       $ apt-get install imagemagick
   ###### Resolve on macOS
       $ brew install imagemagick
 - [FreeMono font](https://en.wikipedia.org/wiki/GNU_FreeFont)
   ###### Resolve on Ubuntu or Debian
       $ apt-get install fonts-freefont-ttf
   ###### Resolve on macOS
       $ brew tap homebrew/cask-fonts
       $ brew cask install font-freesans


## Acknowledgements

**QR Code** is a registered trademark of DENSO WAVE INCORPORATED in Japan
and other countries.


## License

<a href="http://creativecommons.org/publicdomain/zero/1.0/">
    <img src="http://i.creativecommons.org/p/zero/1.0/88x31.png" alt="CC0-1.0" />
</a>
<br />
To the extent possible under law, the author(s) have dedicated all copyright related and neighboring rights to this software to the public domain worldwide.
This software is distributed without any warranty.
