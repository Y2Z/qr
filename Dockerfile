FROM alpine:latest

RUN apk update
RUN apk add build-base libqrencode-dev

RUN mkdir -p /src/qr
WORKDIR /src/qr

COPY qr.c Makefile .

RUN make
RUN make install

CMD ["qr"]
