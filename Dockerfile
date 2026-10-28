FROM alpine:3.22

RUN apk update && \
    apk add --no-cache \
        build-base \
        zlib-dev

COPY Makefile config.mk qr.c ./

RUN make clean && \
    make -j 16 && \
    make install

CMD ["qr"]
