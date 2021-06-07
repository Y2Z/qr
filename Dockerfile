FROM alpine:3.13.5

RUN apk update && \
    apk add --no-cache \
        build-base \
        libqrencode-dev

COPY Makefile qr.c .

RUN make clean && \
    make -j 16 && \
    make install

CMD ["qr"]
