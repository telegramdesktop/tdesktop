#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
"""
Update the Hunspell dictionaries manifest consumed by Telegram Desktop.

One end-to-end run:
  1. Shallow-clone Chromium's hunspell_dictionaries repo into --cache-dir.
  2. If --manifest-post-id is not provided, send an empty placeholder
     message and use its id (printed so you can hardcode it in the client).
  3. For each language: read raw .dic and .aff, recode to UTF-8 if needed
     (rewriting the SET line), zip them under the Qt-side locale name,
     upload via Bot API `sendDocument` to --channel.
  4. Build a JSON manifest (each entry carries `location` =
     "<blobs-channel>#<message_id>") and write it into the manifest post
     via `editMessageText`.
  5. Delete --cache-dir unless --keep-cache is passed.

State file tracks sha256 of the UTF-8 dic/aff pair per language so
subsequent runs re-upload only changed dictionaries.

Usage:
  # Full one-shot run on a fresh channel: creates the manifest post,
  # uploads every dictionary to the same channel, edits the post, and
  # deletes the chromium clone.
  python3 update_dictionaries.py \\
      --bot-token $TG_BOT_TOKEN \\
      --channel @my_test_channel \\
      --state-file ./dict_state.json

  # Manifest and blobs in separate channels. Blobs go to @my_blobs_channel
  # (where clients will fetch them), the JSON manifest lives in the
  # private coordination channel -100...1438.
  python3 update_dictionaries.py \\
      --bot-token $TG_BOT_TOKEN \\
      --channel @my_test_channel \\
      --blobs-channel @my_blobs_channel \\
      --state-file ./dict_state.json

  # Re-run against an existing manifest post (incremental if state file
  # matches; unchanged languages are carried over without re-upload).
  python3 update_dictionaries.py \\
      --bot-token $TG_BOT_TOKEN \\
      --channel @my_test_channel \\
      --manifest-post-id 1234 \\
      --state-file ./dict_state.json

  # Subset / troubleshooting; keep the chromium clone between runs.
  python3 update_dictionaries.py \\
      --bot-token $TG_BOT_TOKEN \\
      --channel @my_test_channel \\
      --manifest-post-id 1234 \\
      --languages en_US,ru_RU \\
      --keep-cache

  # Preview: fetch, recode and zip locally; no network upload, no
  # manifest edit, no cache cleanup.
  python3 update_dictionaries.py \\
      --channel @anything \\
      --dry-run \\
      --languages en_US,sr

Flags:
  --channel <id|@name>         where the manifest post lives
                               (sendMessage / editMessageText target).
  --blobs-channel <@name>      where blob zips go (sendDocument target);
                               defaults to --channel. Must be @username
                               since the client resolves blob locations
                               by public username.
  --manifest-post-id <N>       reuse an existing manifest post; omit to
                               create a new placeholder automatically.
  --cache-dir <path>           where the shallow chromium clone lives
                               (default: .chromium_hunspell_cache).
  --keep-cache                 keep --cache-dir after completion
                               (default: delete it).
  --state-file <path>          sha256/post_id/size per language for
                               incremental uploads.

The bot must be admin (with post/edit rights) in both --channel and
--blobs-channel. When they coincide, one admin suffices.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path
from typing import Optional

import requests

CHROMIUM_REPO = (
    "https://chromium.googlesource.com/chromium/deps/hunspell_dictionaries"
)
BOT_API = "https://api.telegram.org/bot{token}/{method}"

# QLocale::Language values (stable across Qt 5/6, confirmed against
# qtbase/src/corelib/text/qlocale.h for Qt 6.2 and 6.11).
LANG = {
    "Afrikaans":        4,
    "Albanian":         9,
    "Armenian":        17,
    "Bulgarian":       45,
    "Catalan":         48,
    "Croatian":        66,
    "Czech":           67,
    "Danish":          68,
    "Dutch":           72,
    "English":         75,
    "Estonian":        78,
    "Faroese":         81,
    "French":          85,
    "Galician":        90,
    "German":          94,
    "Greek":           96,
    "Hebrew":         103,
    "Hindi":          105,
    "Hungarian":      107,
    "Indonesian":     112,
    "Italian":        119,
    "Korean":         142,
    "Latvian":        155,
    "Lithuanian":     160,
    "NorwegianBokmal":209,
    "Persian":        228,
    "Polish":         230,
    "Portuguese":     231,
    "Romanian":       235,
    "Russian":        239,
    "Serbian":        252,
    "Slovak":         262,
    "Slovenian":      263,
    "Spanish":        270,
    "Swedish":        275,
    "Tajik":          282,
    "Tamil":          283,
    "Turkish":        298,
    "Ukrainian":      303,
    "Vietnamese":     310,
    "Welsh":          316,
}

# QLocale::Country values.
COUNTRY = {
    "Australia":       15,
    "Brazil":          32,
    "Canada":          41,
    "Portugal":       188,
    "UnitedKingdom":  246,
    "UnitedStates":   248,
}

# Matches LWC() in spellchecker_common.cpp: default country collapses to
# the bare language id; otherwise language*1000 + country.
_DEFAULT_COUNTRIES = {COUNTRY["UnitedStates"], COUNTRY["Brazil"]}


def lwc(language: int, country: int) -> int:
    return language if country in _DEFAULT_COUNTRIES else language * 1000 + country


# Each entry: (chromium_stem, id, qt_name, display_name)
# - chromium_stem: the filename in chromium/deps/hunspell_dictionaries (without ext)
# - id: primary key used by settings / UI (QLocale::Language or LWC())
# - qt_name: must equal QLocale(id).name() at runtime — this is what the
#   client uses for both the unpack folder and the <qt_name>.dic/.aff
#   lookups. Double-check when adding new entries.
# - display_name: shown in "Manage dictionaries" UI.
LANGUAGES = [
    ("en_US", LANG["English"],                            "en_US",      "English"),
    ("bg_BG", LANG["Bulgarian"],                          "bg_BG",      "\u0411\u044a\u043b\u0433\u0430\u0440\u0441\u043a\u0438"),
    ("ca_ES", LANG["Catalan"],                            "ca_ES",      "Catal\u00e0"),
    ("cs_CZ", LANG["Czech"],                              "cs_CZ",      "\u010ce\u0161tina"),
    ("cy_GB", LANG["Welsh"],                              "cy_GB",      "Cymraeg"),
    ("da_DK", LANG["Danish"],                             "da_DK",      "Dansk"),
    ("de_DE", LANG["German"],                             "de_DE",      "Deutsch"),
    ("el_GR", LANG["Greek"],                              "el_GR",      "\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac"),
    ("en_AU", lwc(LANG["English"], COUNTRY["Australia"]),      "en_AU", "English (Australia)"),
    ("en_CA", lwc(LANG["English"], COUNTRY["Canada"]),         "en_CA", "English (Canada)"),
    ("en_GB", lwc(LANG["English"], COUNTRY["UnitedKingdom"]),  "en_GB", "English (United Kingdom)"),
    ("es_ES", LANG["Spanish"],                            "es_ES",      "Espa\u00f1ol"),
    ("et_EE", LANG["Estonian"],                           "et_EE",      "Eesti"),
    ("fa_IR", LANG["Persian"],                            "fa_IR",      "\u0641\u0627\u0631\u0633\u06cc"),
    ("fr_FR", LANG["French"],                             "fr_FR",      "Fran\u00e7ais"),
    ("he_IL", LANG["Hebrew"],                             "he_IL",      "\u05e2\u05d1\u05e8\u05d9\u05ea"),
    ("hi_IN", LANG["Hindi"],                              "hi_IN",      "\u0939\u093f\u0928\u094d\u0926\u0940"),
    ("hr_HR", LANG["Croatian"],                           "hr_HR",      "Hrvatski"),
    ("hu-HU", LANG["Hungarian"],                          "hu_HU",      "Magyar"),
    ("hy",    LANG["Armenian"],                           "hy_AM",      "\u0540\u0561\u0575\u0565\u0580\u0565\u0576"),
    ("id_ID", LANG["Indonesian"],                         "id_ID",      "Indonesia"),
    ("it_IT", LANG["Italian"],                            "it_IT",      "Italiano"),
    ("ko",    LANG["Korean"],                             "ko_KR",      "\ud55c\uad6d\uc5b4"),
    ("lt_LT", LANG["Lithuanian"],                         "lt_LT",      "Lietuvi\u0173"),
    ("lv_LV", LANG["Latvian"],                            "lv_LV",      "Latvie\u0161u"),
    ("nb_NO", LANG["NorwegianBokmal"],                    "nb_NO",      "Norsk"),
    ("nl_NL", LANG["Dutch"],                              "nl_NL",      "Nederlands"),
    ("pl_PL", LANG["Polish"],                             "pl_PL",      "Polski"),
    ("pt_BR", LANG["Portuguese"],                         "pt_BR",      "Portugu\u00eas (Brazil)"),
    ("pt_PT", lwc(LANG["Portuguese"], COUNTRY["Portugal"]),    "pt_PT", "Portugu\u00eas"),
    ("ro_RO", LANG["Romanian"],                           "ro_RO",      "Rom\u00e2n\u0103"),
    ("ru_RU", LANG["Russian"],                            "ru_RU",      "\u0420\u0443\u0441\u0441\u043a\u0438\u0439"),
    ("sk_SK", LANG["Slovak"],                             "sk_SK",      "Sloven\u010dina"),
    ("sl_SI", LANG["Slovenian"],                          "sl_SI",      "Sloven\u0161\u010dina"),
    ("sq",    LANG["Albanian"],                           "sq_AL",      "Shqip"),
    ("sv_SE", LANG["Swedish"],                            "sv_SE",      "Svenska"),
    ("ta_IN", LANG["Tamil"],                              "ta_IN",      "\u0ba4\u0bae\u0bbf\u0bb4\u0bcd"),
    ("tg_TG", LANG["Tajik"],                              "tg_TJ",      "\u0422\u043e\u04b7\u0438\u043a\u04e3"),
    ("tr",    LANG["Turkish"],                            "tr_TR",      "T\u00fcrk\u00e7e"),
    ("uk_UA", LANG["Ukrainian"],                          "uk_UA",      "\u0423\u043a\u0440\u0430\u0457\u043d\u0441\u044c\u043a\u0430"),
    ("vi_VN", LANG["Vietnamese"],                         "vi_VN",      "Ti\u1ebfng Vi\u1ec7t"),
    ("gl",    LANG["Galician"],                           "gl_ES",      "Galego"),
    ("sr",    LANG["Serbian"],                            "sr_Cyrl_RS", "\u0421\u0440\u043f\u0441\u043a\u0438"),
    # Afrikaans (af-ZA) and Faroese (fo-FO) are shipped by Chromium only
    # as compiled .bdic — raw .dic/.aff are not checked in. Add them when
    # an upstream Hunspell source is picked (LibreOffice, etc.).
]


def ensure_chromium_clone(cache_dir: Path) -> Path:
    """Return path to a fresh shallow clone of Chromium's hunspell repo."""
    clone = cache_dir / "hunspell_dictionaries"
    if clone.exists() and (clone / ".git").exists():
        print(f"  using existing clone at {clone}", flush=True)
        try:
            subprocess.run(
                ["git", "-C", str(clone), "fetch", "--depth=1", "origin", "main"],
                check=True, capture_output=True, text=True,
            )
            subprocess.run(
                ["git", "-C", str(clone), "reset", "--hard", "FETCH_HEAD"],
                check=True, capture_output=True, text=True,
            )
            return clone
        except subprocess.CalledProcessError as e:
            print(f"  refresh failed ({e.stderr.strip()}), recloning",
                  flush=True)
            shutil.rmtree(clone)
    cache_dir.mkdir(parents=True, exist_ok=True)
    print(f"  cloning {CHROMIUM_REPO} (shallow) → {clone}", flush=True)
    subprocess.run(
        ["git", "clone", "--depth=1", CHROMIUM_REPO, str(clone)],
        check=True,
    )
    return clone


def read_chromium_file(clone: Path, stem: str, ext: str) -> bytes:
    path = clone / f"{stem}.{ext}"
    if not path.exists():
        raise FileNotFoundError(f"{stem}.{ext} not found at chromium")
    return path.read_bytes()


# Chromium SET names → Python codec names when they differ.
_PY_CODEC_ALIAS = {
    "windows-1251": "cp1251",
    "windows-1252": "cp1252",
}


def _parse_aff_charset(aff: bytes) -> str:
    """Return the SET charset declared in an .aff file. Default per Hunspell
    docs is ISO-8859-1 when SET is absent."""
    for raw in aff.splitlines():
        line = raw.strip()
        if line.startswith(b"\xef\xbb\xbf"):  # BOM
            line = line[3:].strip()
        if line.startswith(b"SET "):
            return line[4:].strip().decode("ascii", errors="replace").strip()
    return "ISO-8859-1"


def _normalize_charset_name(name: str) -> str:
    return name.upper().replace("_", "-").replace(" ", "")


def normalize_to_utf8(dic: bytes, aff: bytes) -> tuple[bytes, bytes]:
    """Decode dic/aff using the .aff SET charset and re-emit both as UTF-8,
    rewriting (or inserting) the SET line so Hunspell reports utf-8 at runtime.
    Idempotent when input is already UTF-8."""
    charset = _parse_aff_charset(aff)
    normalized = _normalize_charset_name(charset)
    if normalized in ("UTF-8", "UTF8"):
        return dic, aff
    codec = _PY_CODEC_ALIAS.get(charset, charset)
    try:
        dic_text = dic.decode(codec)
        aff_text = aff.decode(codec)
    except (LookupError, UnicodeDecodeError) as e:
        raise RuntimeError(
            f"cannot decode dictionary as {charset!r}: {e}") from None

    pattern = re.compile(r"^SET\s+\S+\s*$", re.MULTILINE)
    if pattern.search(aff_text):
        aff_text = pattern.sub("SET UTF-8", aff_text, count=1)
    else:
        aff_text = "SET UTF-8\n" + aff_text
    return dic_text.encode("utf-8"), aff_text.encode("utf-8")


def make_zip(qt_name: str, dic: bytes, aff: bytes) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{qt_name}.dic", dic)
        z.writestr(f"{qt_name}.aff", aff)
    return buf.getvalue()


def sha256_pair(dic: bytes, aff: bytes) -> str:
    h = hashlib.sha256()
    h.update(b"dic:")
    h.update(dic)
    h.update(b"aff:")
    h.update(aff)
    return h.hexdigest()


def bot_call(token: str, method: str, *, data=None, files=None, json_body=None):
    url = BOT_API.format(token=token, method=method)
    for attempt in range(5):
        if json_body is not None:
            r = requests.post(url, json=json_body, timeout=120)
        else:
            r = requests.post(url, data=data, files=files, timeout=300)
        if r.status_code == 429:
            wait = r.json().get("parameters", {}).get("retry_after", 5)
            print(f"  rate-limited, sleeping {wait}s", flush=True)
            time.sleep(wait + 1)
            continue
        try:
            body = r.json()
        except ValueError:
            r.raise_for_status()
            raise
        if r.ok and body.get("ok"):
            return body["result"]
        raise RuntimeError(
            f"Bot API {method} failed ({r.status_code}): {body}"
        )
    raise RuntimeError(f"Bot API {method}: too many retries")


def bot_send_document(token, chat_id, filename, blob):
    result = bot_call(
        token,
        "sendDocument",
        data={"chat_id": chat_id, "disable_notification": "true"},
        files={"document": (filename, blob, "application/zip")},
    )
    return result["message_id"], result["document"]["file_size"]


def bot_edit_message_text(token, chat_id, message_id, text):
    try:
        bot_call(
            token,
            "editMessageText",
            json_body={
                "chat_id": chat_id,
                "message_id": message_id,
                "text": text,
            },
        )
    except RuntimeError as e:
        if "message is not modified" in str(e):
            print("manifest post unchanged, skip edit", flush=True)
            return
        raise


def bot_send_placeholder(token, chat_id):
    result = bot_call(
        token,
        "sendMessage",
        json_body={
            "chat_id": chat_id,
            "text": "{}",
            "disable_notification": True,
        },
    )
    return result["message_id"]


def load_state(path: Optional[Path]) -> dict:
    if path and path.exists():
        return json.loads(path.read_text())
    return {}


def save_state(path: Optional[Path], state: dict) -> None:
    if not path:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n")


_DEFAULT_CLIENT_SOURCE = (
    Path(__file__).resolve().parent.parent
    / "SourceFiles" / "chat_helpers" / "spellchecker_common.cpp"
)


def _client_channel_username(channel: str) -> Optional[str]:
    stripped = str(channel).lstrip("@").lstrip("+")
    if not stripped or stripped[0] == "-" or stripped.isdigit():
        return None
    return stripped


def patch_client_source(
    path: Path,
    channel_username: Optional[str],
    post_id: int,
) -> None:
    text = path.read_text(encoding="utf-8")
    original = text
    if channel_username is not None:
        text, n = re.subn(
            r'(constexpr auto kDictionariesManifestChannel\s*=\s*)'
            r'"[^"]*"(_cs\s*;)',
            lambda m: f'{m.group(1)}"{channel_username}"{m.group(2)}',
            text,
            count=1,
        )
        if n == 0:
            raise RuntimeError(
                f"patch: kDictionariesManifestChannel not found in {path}")
    text, n = re.subn(
        r'(constexpr auto kDictionariesManifestPostId\s*=\s*)\d+(\s*;)',
        lambda m: f'{m.group(1)}{post_id}{m.group(2)}',
        text,
        count=1,
    )
    if n == 0:
        raise RuntimeError(
            f"patch: kDictionariesManifestPostId not found in {path}")
    if text == original:
        print(f"  {path}: constants already up to date", flush=True)
        return
    path.write_text(text, encoding="utf-8")
    parts = [f"postId={post_id}"]
    if channel_username is not None:
        parts.append(f"channel={channel_username}")
    print(f"  patched {path}: {', '.join(parts)}", flush=True)


def format_manifest(entries: list[dict]) -> str:
    # One entry per line for readable diffs and to keep message size small
    # enough for editMessageText (4096-char limit).
    lines = ['{"version":1,"dictionaries":[']
    for i, e in enumerate(entries):
        sep = "" if i == len(entries) - 1 else ","
        lines.append(json.dumps(e, ensure_ascii=False, sort_keys=True) + sep)
    lines.append("]}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot-token", default=os.environ.get("TG_BOT_TOKEN"),
                    help="Bot API token (or via TG_BOT_TOKEN env)")
    ap.add_argument("--channel", required=True,
                    help="@username or numeric chat_id of the channel"
                         " that holds the manifest post (sendMessage /"
                         " editMessageText target)")
    ap.add_argument("--blobs-channel", default=None,
                    help="@username of the channel blobs are uploaded"
                         " into via sendDocument; defaults to --channel"
                         " with any leading @ stripped. Must be a public"
                         " username — clients resolve locations by it")
    ap.add_argument("--manifest-post-id", type=int, default=None,
                    help="reuse this message_id; if omitted, sends a new"
                         " placeholder first and uses its id (prints so"
                         " you can hardcode it in the client)")
    ap.add_argument("--state-file", type=Path, default=None,
                    help="path to persist sha/post_id/size per language"
                         " for incremental re-uploads")
    ap.add_argument("--languages", default="",
                    help="comma-separated chromium stems to restrict to")
    ap.add_argument("--dry-run", action="store_true",
                    help="fetch and zip but do not upload, edit, or clean")
    ap.add_argument("--cache-dir", type=Path,
                    default=Path(".chromium_hunspell_cache"),
                    help="directory for the shallow chromium clone")
    ap.add_argument("--keep-cache", action="store_true",
                    help="keep --cache-dir after completion (default:"
                         " delete the chromium clone when done)")
    ap.add_argument("--client-source", type=Path,
                    default=_DEFAULT_CLIENT_SOURCE,
                    help="path to spellchecker_common.cpp; after a"
                         " successful manifest edit the script rewrites"
                         " kDictionariesManifestChannel and"
                         " kDictionariesManifestPostId in place")
    ap.add_argument("--skip-client-patch", action="store_true",
                    help="do not rewrite manifest constants in"
                         " --client-source")
    args = ap.parse_args()

    if not args.dry_run and not args.bot_token:
        sys.exit("error: --bot-token or TG_BOT_TOKEN env required")

    blobs_target = args.blobs_channel or args.channel
    blobs_username = str(blobs_target).lstrip("@").lstrip("+")
    if not blobs_username or blobs_username.startswith("-"):
        sys.exit("error: blobs channel must be @username (clients resolve"
                 " locations by public username, not chat_id). Pass"
                 " --blobs-channel @name when --channel is numeric.")

    manifest_post_id = args.manifest_post_id
    if manifest_post_id is None and not args.dry_run:
        manifest_post_id = bot_send_placeholder(
            args.bot_token, args.channel)
        print(f"created manifest placeholder, message_id="
              f"{manifest_post_id}", flush=True)
        print(f"hardcode in client: kDictionariesManifestPostId = "
              f"{manifest_post_id}", flush=True)

    def location(post_id: int) -> str:
        return f"{blobs_username}#{post_id}"

    # Bot uploads go to --blobs-channel (the @username derived above),
    # which may or may not equal --channel. Use the args.blobs-channel
    # value if supplied, otherwise fall back to --channel as-is.
    blobs_chat = args.blobs_channel or args.channel

    filter_set = {s for s in args.languages.split(",") if s}
    state = load_state(args.state_file)
    manifest_entries = []

    clone = ensure_chromium_clone(args.cache_dir)

    for stem, lang_id, qt_name, display in LANGUAGES:
        if filter_set and stem not in filter_set:
            prev = state.get(stem)
            if prev:
                manifest_entries.append({
                    "id": lang_id,
                    "name": display,
                    "location": location(prev["post_id"]),
                    "size": prev["size"],
                })
            continue

        print(f"[{stem} → {qt_name}]", flush=True)
        try:
            dic_raw = read_chromium_file(clone, stem, "dic")
            aff_raw = read_chromium_file(clone, stem, "aff")
        except FileNotFoundError as e:
            print(f"  skip: {e}", flush=True)
            continue
        try:
            dic, aff = normalize_to_utf8(dic_raw, aff_raw)
        except RuntimeError as e:
            print(f"  skip: {e}", flush=True)
            continue
        if dic is not dic_raw:
            print(f"  recoded to UTF-8 from "
                  f"{_parse_aff_charset(aff_raw)}", flush=True)
        digest = sha256_pair(dic, aff)

        prev = state.get(stem)
        if (prev
                and prev.get("sha256") == digest
                and prev.get("qt_name") == qt_name
                and not args.dry_run):
            print(f"  unchanged (sha {digest[:8]}), carrying postId="
                  f"{prev['post_id']}", flush=True)
            manifest_entries.append({
                "id": lang_id,
                "name": display,
                "location": location(prev["post_id"]),
                "size": prev["size"],
            })
            continue

        blob = make_zip(qt_name, dic, aff)
        print(f"  zipped: dic={len(dic):,}  aff={len(aff):,}  "
              f"zip={len(blob):,}", flush=True)

        if args.dry_run:
            manifest_entries.append({
                "id": lang_id,
                "name": display,
                "location": location(prev["post_id"] if prev else 0),
                "size": len(blob),
            })
            continue

        post_id, size = bot_send_document(
            args.bot_token, blobs_chat, qt_name, blob)
        print(f"  uploaded: postId={post_id} size={size}", flush=True)

        state[stem] = {
            "sha256": digest,
            "post_id": post_id,
            "size": size,
            "qt_name": qt_name,
        }
        manifest_entries.append({
            "id": lang_id,
            "name": display,
            "location": location(post_id),
            "size": size,
        })

    manifest_text = format_manifest(manifest_entries)
    print(f"\nmanifest: {len(manifest_entries)} entries, "
          f"{len(manifest_text):,} chars", flush=True)

    if args.dry_run:
        print("--- manifest (dry-run) ---")
        print(manifest_text)
        return

    bot_edit_message_text(
        args.bot_token, args.channel,
        manifest_post_id, manifest_text)
    print(f"manifest post {manifest_post_id} updated", flush=True)

    if not args.skip_client_patch:
        channel_username = _client_channel_username(args.channel)
        if channel_username is None:
            print(f"  --channel {args.channel!r} is not a @username;"
                  f" updating only kDictionariesManifestPostId in"
                  f" {args.client_source}", flush=True)
        patch_client_source(
            args.client_source, channel_username, manifest_post_id)

    save_state(args.state_file, state)

    if not args.keep_cache and args.cache_dir.exists():
        print(f"removing {args.cache_dir}", flush=True)
        shutil.rmtree(args.cache_dir)


if __name__ == "__main__":
    main()
