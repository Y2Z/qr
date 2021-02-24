#!/bin/sh

DOCKER=docker

if which podman 2>&1 > /dev/null; then
    DOCKER=podman
fi

$DOCKER run --rm y2z/qr qr "$@"
