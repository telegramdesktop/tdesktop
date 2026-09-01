#!/usr/bin/env bash
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
#
# Publishes ONE platform build of a canary lane: posts the update file(s)
# and the portable archive to the lane's channel through the local Bot
# API server, then rewrites that platform's metadata message wholesale.
# Every platform owns its own metadata message (the id is compiled into
# that platform's builds), so there is exactly one writer per message and
# no read-modify-write anywhere: a bot cannot read back a message that is
# not pinned, and concurrent merges into one shared message would race.
#
# Run from the repository root (the changelog walks git history). All
# inputs come from the environment:
#
#   BOT_API, BOT_TOKEN      local Bot API server and the lane's bot
#   CHAT_ID                 channel id in Bot API form (-100...)
#   MSG_ID                  this platform's metadata message id
#   CHANNEL                 public | private
#   PLATFORMS               feed keys published here: "win64", "mac armac"
#                           or "linux" (mac and armac come from the one
#                           universal build and share a message)
#   BASE, COUNTER, COMMIT   the version being published
#   VERSION_STR             the display version (7.0.9) of the archives
#   PREVIOUS                commit of the previous run, for the changelog
#   SIGNED                  "true" when the platform binaries carry their
#                           Authenticode signature / notarization (Linux
#                           has none to carry and always passes "true")
#   KEYS_LOC                directory with manifest.min.json + manifest.sig
#   UPDATE_DIR, PORTABLE_DIR
#                           downloaded artifacts of this platform

set -eo pipefail

for NAME in BOT_API BOT_TOKEN CHAT_ID MSG_ID CHANNEL PLATFORMS BASE \
    COUNTER COMMIT VERSION_STR KEYS_LOC UPDATE_DIR PORTABLE_DIR; do
  if [ -z "${!NAME}" ]; then
    echo "::error::$NAME is required."
    exit 1
  fi
done

# The same suffix the packer puts after the version of the update file:
# -canary-{counter} on the public lane, -canary-{counter}-private on the
# private one.
SUFFIX="-canary-$COUNTER"
if [ "$CHANNEL" = "private" ]; then SUFFIX="$SUFFIX-private"; fi

update_file() {
  case "$1" in
    win64) echo "td-update-win-x64-$BASE$SUFFIX" ;;
    mac) echo "td-update-mac-x64-$BASE$SUFFIX" ;;
    armac) echo "td-update-mac-arm-$BASE$SUFFIX" ;;
    linux) echo "td-update-linux-x64-$BASE$SUFFIX" ;;
    *) echo "::error::Unknown platform '$1'." >&2; return 1 ;;
  esac
}

portable_file() {
  case "$1" in
    win64) echo "td-portable-win-x64-$VERSION_STR$SUFFIX.zip" ;;
    mac|armac) echo "td-portable-mac-$VERSION_STR$SUFFIX.zip" ;;
    linux) echo "td-portable-linux-x64-$VERSION_STR$SUFFIX.tar.xz" ;;
  esac
}

call() {
  local METHOD="$1"
  shift
  local RESPONSE
  # Deliberately not curl -f: the Bot API reports failures as 4xx with a
  # JSON body, and -f would discard that body and abort the script
  # through set -e before anything could be printed. Diagnostics go to
  # stderr because callers capture stdout.
  RESPONSE=$(curl -s "$BOT_API/bot$BOT_TOKEN/$METHOD" "$@" || true)
  if ! echo "$RESPONSE" | jq -e '.ok == true' > /dev/null 2>&1; then
    local WHY
    WHY=$(echo "$RESPONSE" | jq -r '.description // empty' 2>/dev/null) || WHY=""
    echo "::error::$METHOD failed: ${WHY:-no response}" >&2
    return 1
  fi
  echo "$RESPONSE"
}

# GitHub only waits for the service container process, not for tdlib to
# accept requests.
READY=false
for i in $(seq 1 30); do
  if curl -sf "$BOT_API/bot$BOT_TOKEN/getMe" | jq -e '.ok == true' > /dev/null; then
    READY=true
    break
  fi
  sleep 2
done
if [ "$READY" != "true" ]; then
  echo "::error::The local Bot API server did not come up."
  exit 1
fi

# Exact names only: the build jobs delete the .unsigned intermediates, a
# publish run never guesses from a glob.
set -- $PLATFORMS
FIRST="$1"
PORTABLE="$PORTABLE_DIR/$(portable_file "$FIRST")"
for PLATFORM in $PLATFORMS; do
  FILE="$UPDATE_DIR/$(update_file "$PLATFORM")"
  if [ ! -f "$FILE" ]; then
    echo "::error::$FILE is missing, refusing to publish."
    exit 1
  fi
done
if [ ! -f "$PORTABLE" ]; then
  echo "::error::$PORTABLE is missing, refusing to publish."
  exit 1
fi

NOTE=""
if [ "$SIGNED" != "true" ]; then
  case "$FIRST" in
    win64) NOTE="UNSIGNED test build: no Authenticode signature." ;;
    mac|armac) NOTE="UNSIGNED test build: not signed or notarized." ;;
  esac
fi
CAPTION=$({
  echo "Canary #$COUNTER · $COMMIT"
  if [ -n "$NOTE" ]; then
    echo "$NOTE"
  fi
  echo ""
  if [ -n "$PREVIOUS" ] && git cat-file -e "$PREVIOUS^{commit}" 2>/dev/null \
    && [ "$(git rev-parse "$PREVIOUS")" != "$(git rev-parse HEAD)" ]; then
    git log --no-merges --pretty=format:'• %s' "$PREVIOUS..HEAD" | head -20
  else
    git log --no-merges --pretty=format:'• %s' -10
  fi
} | head -c 1000)

POSTS_JSON="{}"
for PLATFORM in $PLATFORMS; do
  RESPONSE=$(call sendDocument \
    -F chat_id="$CHAT_ID" \
    -F document=@"$UPDATE_DIR/$(update_file "$PLATFORM")" \
    -F caption="$CAPTION")
  POST=$(echo "$RESPONSE" | jq -e -r '.result.message_id | numbers')
  POSTS_JSON=$(echo "$POSTS_JSON" | jq --arg p "$PLATFORM" --argjson id "$POST" '. + {($p): $id}')
  echo "$PLATFORM -> post $POST"
done

# The portable archive is for first installs, posted as a plain document
# and not referenced from the metadata.
call sendDocument \
  -F chat_id="$CHAT_ID" \
  -F document=@"$PORTABLE" \
  -F caption="Portable, $CAPTION" > /dev/null
echo "$FIRST portable posted."

MANIFEST_B64=$(base64 < "$KEYS_LOC/manifest.min.json" | tr -d '\n')
MANIFEST_SIG_B64=$(base64 < "$KEYS_LOC/manifest.sig" | tr -d '\n')

NEW=$(jq -n \
  --arg manifest "$MANIFEST_B64" \
  --arg manifest_sig "$MANIFEST_SIG_B64" \
  --arg commit "$COMMIT" \
  --argjson base "$BASE" \
  --argjson counter "$COUNTER" \
  --argjson posts "$POSTS_JSON" \
  "{
    format: 1,
    manifest: \$manifest,
    manifest_sig: \$manifest_sig,
    channels: {
      \"canary-$CHANNEL\": {
        base: \$base,
        counter: \$counter,
        commit: \$commit,
        posts: \$posts
      }
    }
  }")
if [ "${#NEW}" -gt 4096 ]; then
  echo "::error::The metadata message would exceed 4096 characters, prune the manifest."
  exit 1
fi
call editMessageText \
  -F chat_id="$CHAT_ID" \
  -F message_id="$MSG_ID" \
  --form-string text="$NEW" > /dev/null
echo "Metadata message $MSG_ID updated: canary-$CHANNEL $BASE #$COUNTER for $PLATFORMS."
