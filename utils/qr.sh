#!/bin/sh

DOCKER=docker

if command -v podman > /dev/null 2>&1; then
    DOCKER=podman
fi

$DOCKER run --rm -i y2z/qr qr "$@"
