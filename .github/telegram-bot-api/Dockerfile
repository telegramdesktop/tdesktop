# Bot API server image for the canary publish job, built from a pinned
# tdlib/telegram-bot-api ref by canary-bot-api.yml. The publish job hands
# this container the bot token and the update files, so it must never be
# replaced by a third-party image.

FROM ubuntu:22.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ git gperf libssl-dev make zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

ARG TELEGRAM_BOT_API_REF
RUN test -n "$TELEGRAM_BOT_API_REF" \
 && git clone https://github.com/tdlib/telegram-bot-api.git /src \
 && git -C /src checkout "$TELEGRAM_BOT_API_REF" \
 && git -C /src submodule update --init --recursive

RUN cmake -S /src -B /build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/out \
 && cmake --build /build --target install --parallel

FROM ubuntu:22.04

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
