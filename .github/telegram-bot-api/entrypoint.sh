#!/bin/sh
# TELEGRAM_API_ID and TELEGRAM_API_HASH are read by the binary itself,
# TELEGRAM_LOCAL=1 enables --local (the same environment interface the
# canary publish job uses).
set -e
exec /usr/local/bin/telegram-bot-api \
	--http-port=8081 \
	--dir=/data \
	--temp-dir=/data/temp \
	${TELEGRAM_LOCAL:+--local} \
	"$@"
