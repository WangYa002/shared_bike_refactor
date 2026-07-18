#!/usr/bin/env bash
# Build the bike-server docker image locally, ship it to the Tencent Cloud
# host, and restart docker-compose there.
set -euo pipefail

REMOTE="${REMOTE:-ubuntu@124.220.92.243}"
REMOTE_DIR="${REMOTE_DIR:-~/bike}"
GIT_SHA="$(git rev-parse --short HEAD)"
IMG="bike-server:${GIT_SHA}"

echo "==> Building image ${IMG}"
docker build -t "${IMG}" -f docker/Dockerfile.server .

echo "==> Saving image ($(docker image inspect "${IMG}" --format '{{.Size}}') bytes)"
docker save "${IMG}" | gzip > /tmp/bike-server.tgz

echo "==> Copying to ${REMOTE}"
ssh "${REMOTE}" "mkdir -p ${REMOTE_DIR}"
scp /tmp/bike-server.tgz "${REMOTE}:${REMOTE_DIR}/"
scp docker/docker-compose.yml docker/server.toml "${REMOTE}:${REMOTE_DIR}/"
scp -r docker/mysql-init "${REMOTE}:${REMOTE_DIR}/"

echo "==> Loading + restarting on remote"
ssh "${REMOTE}" "cd ${REMOTE_DIR} && \
    docker load < bike-server.tgz && \
    docker tag ${IMG} bike-server:latest && \
    docker compose down && \
    docker compose up -d --build"

echo "==> Waiting 30s for healthy"
sleep 30
ssh "${REMOTE}" "cd ${REMOTE_DIR} && docker compose ps"

echo "==> Cleaning up local tarball"
rm -f /tmp/bike-server.tgz

echo "Deployed ${IMG} to ${REMOTE}"
echo "Smoke test from local:  nc -zv 124.220.92.243 8888"
