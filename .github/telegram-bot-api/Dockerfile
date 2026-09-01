# Bot API server image for the canary publish job, built from a pinned
# tdlib/telegram-bot-api ref by canary-bot-api.yml. The publish job hands
# this container the bot token and the update files, so it must never be
# replaced by a third-party image.

# The base image is pinned by digest (ubuntu:22.04 as of 2026-08-20) so
# a rebuild of "the same" ref cannot silently pick up another base OS.
FROM ubuntu:22.04@sha256:2edbbc5dc405e9612ba3584ce95480277e3eb374407b5505fe26f17df77c7dbc AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ git gperf libssl-dev make zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

ARG TELEGRAM_BOT_API_REF
RUN test -n "$TELEGRAM_BOT_API_REF" \
 && git clone https://github.com/tdlib/telegram-bot-api.git /src \
 && git -C /src checkout "$TELEGRAM_BOT_API_REF" \
 && git -C /src submodule update --init --recursive \
 && echo "telegram-bot-api at $(git -C /src rev-parse HEAD)" \
 && git -C /src submodule status --recursive

RUN cmake -S /src -B /build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/out \
 && cmake --build /build --target install --parallel

FROM ubuntu:22.04@sha256:2edbbc5dc405e9612ba3584ce95480277e3eb374407b5505fe26f17df77c7dbc

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libssl3 zlib1g \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 999 --create-home botapi \
 && mkdir /data && chown botapi:botapi /data

COPY --from=build /out/bin/telegram-bot-api /usr/local/bin/telegram-bot-api
COPY entrypoint.sh /usr/local/bin/entrypoint.sh

USER botapi
WORKDIR /data
EXPOSE 8081
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
