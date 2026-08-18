# Telegram Desktop WEB proxy: refined client design

The hosted half is specified in `../tproxy-server/PLAN.md`. Its multiplexing frame
format and `MessageChannel` contract are authoritative. This document records the
reviewed Telegram Desktop design and the implementation now present in this tree.
The server-dependent execution procedure is intentionally separate in
`docs/web-proxy-test-plan.md`.

## 1. Scope and invariant

WEB is an MTProxy whose primary carrier is one process-wide hidden native WebView:

```text
MTProto session threads
  -> TcpConnection (existing MTProxy obfuscation and AES-CTR)
  -> WebProxySocket (one logical stream)
  -> process-wide WebProxy::Transport (one worker thread)
  -> one hidden platform WebView
  -> injected exact-origin TelegramWebProxy bridge
  -> https://relay.example/?bridge=<derived-capability>#android=<nonce>
  -> HTTPS carrier
  -> hosted relay
  -> stock MTProxy
  -> Telegram
```

The central invariant is that Telegram Desktop opens no external MTProto socket
while WEB is active. The hidden WebView's platform web engine makes the external
HTTPS connection. The hosted relay sees only bytes already transformed by the
existing MTProxy protocol layer; the MTProxy secret is never placed in HTML or
JavaScript.

The previous local-page/system-browser path remains a fallback. Telegram offers it
only after the hidden WebView is unavailable or has failed its ten-second startup or
health deadline. It never opens a browser automatically.

## 2. Design decisions

The initial draft left several architectural choices open. They are now fixed:

1. There is one transport and one hidden WebView per process, not per account. Proxy
   selection is already process-wide, so all accounts using the selected WEB proxy
   share one multiplexed carrier.
2. The transport owns a dedicated `QThread`. `QTcpServer`, accepted fallback
   sockets, WebSocket framing, mux state, and queues live there. The native WebView
   stays on the application thread and exchanges bounded messages with the worker.
3. `WebProxySocket` and the transport are compiled in the main `Telegram` target.
   `connection_tcp.cpp`, the only factory site retaining the full `ProxyData`, is
   already in that target. No reverse dependency from `td_mtproto` is introduced.
4. The serialized `host` field stores only the canonical lowercase ASCII/IDNA
   A-label hostname. Scheme, port, path, query, fragment, user info, IP addresses
   (including WHATWG "ends in a number" shorthands such as `127.1` or `0x7f.1`),
   and single-label names are rejected. `port` is fixed to `443`; `password` stores
   the MTProxy secret. Operators should publish WEB hostnames in ACE (`xn--…`)
   form: ACE input round-trips unchanged on every platform, while a hand-typed
   Unicode host is mapped by the Qt version the build ships (IDNA2003/nameprep
   on the Qt 5.15 Windows builds, UTS #46 nontransitional on Qt 6), so hosts
   containing deviation characters (`ß`, `ς`, ZWJ, ZWNJ) or characters newer
   than Unicode 3.2 can normalise to different strings — and therefore different
   capabilities — per platform.
5. WEB entries are entered manually or imported from `tg://webproxy` /
   `https://t.me/webproxy` links (see §11); there is no `tg://proxy?…` form.
6. Inactive WEB entries are not checked and are removed from proxy rotation's
   candidate order in v1. Checking would require activating a WebView carrier and
   must not create background carriers for every saved proxy.
7. The loopback parent is one inline, dependency-free HTML response. A qrc asset
   adds no value for this small page and would create another generated-resource
   dependency.
8. While an explicitly opened fallback page's authenticated loopback WebSocket is
   open, the local parent maintains
   an empty `RTCDataChannel` between two same-page `RTCPeerConnection`s. This is a
   best-effort Chrome background-lifecycle guard: it uses no media, STUN, TURN, or
   remote signaling, and failure to establish it never fails the carrier.

## 3. Data model and persistence

`MTP::ProxyData::Type::Web` is appended to the enum and serialized as type code `4`.
The existing five-field proxy blob remains unchanged:

```text
type | host | port | user | password
```

WEB maps those fields as follows:

| Field | WEB meaning |
|---|---|
| `host` | canonical lowercase ASCII/IDNA A-label hostname |
| `port` | fixed value `443` |
| `user` | empty |
| `password` | existing MTProxy secret syntax |

Validation requires both a valid DNS hostname and a supported MTProxy secret. Plain
16-byte and `dd` random-padding secrets are accepted; `ee` TLS-emulation secrets are
rejected because the stock MTProxy would expect an inner TLS-emulation record that
this raw relay deliberately does not add. Unknown future serialized type codes
deserialize to `None` instead of reaching
`Unexpected`, so downgrades skip an unsupported proxy rather than crashing.

WEB behaves like MTProxy throughout the existing model:

- `secretFromMtprotoPassword()` accepts WEB.
- Qt's application proxy is `NoProxy`; WEB does not affect update or generic HTTP
  traffic.
- custom DC/proxy DNS resolution is disabled because the WebView engine, or the
  explicitly selected browser fallback, resolves the relay hostname.
- calls remain unsupported.
- TCP MTProto is enabled and the plain MTProto HTTP connection is disabled.
- DC endpoints are ignored; the hosted relay chooses its fixed stock-MTProxy target.
- `initConnection` reports the relay hostname and port 443 as client proxy metadata.

## 4. `WebProxySocket`

`mtproto/details/mtproto_web_proxy_socket.*` implements `AbstractSocket` as a
logical byte stream over the shared transport.

On `connectToHost`, it registers a new 24-bit stream id. The address and port
arguments are intentionally ignored. It emits `connected` after the active carrier
has completed the relay `WELCOME` handshake and the transport has sent `OPEN` for
that stream.

Writes concatenate the one-time MTProxy connection prefix and body before queuing a
`DATA` frame. Incoming `DATA` is buffered and exposed through partial `read()` calls.
Every successful read replenishes exactly that many bytes of receive credit with a
`WINDOW` frame. Transport loss emits `disconnected`; protocol violations, queue
overflow, and explicit transport failures emit `error`.

The existing `TcpConnection` continues to own all MTProxy protocol work. For WEB it
uses `secretFromMtprotoPassword()` and `Protocol::Create(secret)` exactly as for
MTProxy, then selects `WebProxySocket` at the one socket-factory call site.

## 5. Process-wide transport and threading

`mtproto/web_proxy/web_proxy_transport.*` provides a main-thread lifecycle facade and
runs all I/O state on its worker thread.

Main-thread lifecycle:

- `Activate(proxy)` creates the worker on first use, synchronously installs the
  selected valid proxy and creates one hidden WebView when the selected WEB proxy
  changes; the fallback listener stays unbound.
- `OpenBrowser(proxy)` binds the loopback listener if needed, mints a fresh
  one-shot capability and opens a new tab on explicit user request after fallback
  has been offered.
- `Deactivate()` closes streams, accepted clients, and the listener when the app
  changes away from WEB, and destroys the hidden WebView.
- `Shutdown()` runs after MTP accounts have stopped and joins the worker thread.

The WebView candidate performs its own `HELLO` / `WELCOME` handshake before the
worker adopts it. Startup, bridge initialization, write acknowledgement, and health
are each bounded. A failed candidate is destroyed and another candidate is tried
after an exponentially growing delay (2 s to 30 s); fallback is offered only once
the failure is terminal (see §8). If a retry succeeds while the browser fallback
is connected, logical streams reconnect through the WebView and the fallback socket
is closed; no relay session is migrated across carriers.

Session-thread interaction uses queued calls into the worker. Each stream stores its
socket context, and worker-to-socket delivery is queued to that socket's owning
thread. `WebProxySocket` destruction unregisters synchronously on the worker before
the QObject base destructor can invalidate the context. This creates a strict
ordering boundary: notifications already posted remain owned by Qt and are removed
with the QObject, while the worker cannot inspect or post through the context after
unregistration returns. The global transport pointer is atomic and remains alive
until all MTP sessions have been destroyed.

The principal state transitions surfaced to settings are:

```text
Idle
  -> Connecting
  -> Connected
  -> WaitingForBrowser  (WebView unavailable, unhealthy, or failed)
  -> Connecting         (user confirmed the browser fallback)
  -> Connected

WaitingForBrowser
  -> Connected          (a 30-second WebView retry succeeds)
```

## 6. Shared relay frames

All integers are big-endian. The implementation mirrors server plan section 7:

```text
type:u8 | stream_id:u24 | length:u32 | payload:length
```

Each carrier message must contain one or more complete frames. The parser
accepts concatenated frames and rejects an empty message or trailing partial frame.
A payload is capped at 1 MiB. Known types are:

| Value | Name | Stream | Client behavior |
|---:|---|---:|---|
| `0x01` | `OPEN` | >0 | sent once after `WELCOME` |
| `0x02` | `DATA` | >0 | opaque MTProxy bytes |
| `0x03` | `CLOSE` | >0 | empty payload; closes one logical socket |
| `0x04` | `WINDOW` | >0 | four-byte credit delta |
| `0x05` | `PING` | 0 | relay-to-client keepalive; answered with `PONG` |
| `0x06` | `PONG` | 0 | sent only as the exact `PING` response |
| `0x10` | `HELLO` | 0 | client sends payload `01` for protocol v1 |
| `0x11` | `WELCOME` | 0 | empty payload; must be the first relay frame |
| `0x12` | `AUTH_CHAL` | 0 | reserved for relay-auth v2, rejected in v1 |
| `0x13` | `AUTH_RESP` | 0 | reserved for relay-auth v2 |
| `0x1f` | `BYE` | 0 | fails current logical streams and closes the carrier |

An incoming `OPEN`, a stream frame on stream zero, a session frame on a nonzero
stream, malformed `WINDOW`, data beyond granted credit, an unknown live stream, or
an unknown type is a protocol error for v1. The client retains up to 4096 recently
closed stream ids. Well-formed `DATA`, `WINDOW`, and `CLOSE` already in flight for a
retained id are discarded; this prevents an ordinary cross-direction close race from
failing unrelated multiplexed streams.

## 7. Flow control and memory bounds

Both directions start with an implicit 4 MiB per-stream window.

Downlink flow control is exact: relay `DATA` consumes client receive credit, and
Telegram Desktop grants it back only when `WebProxySocket::read()` drains bytes into
the MTProto engine. This naturally bounds each socket's unread data.

Uplink has a constraint the initial draft missed: `AbstractSocket::write()` returns
`void` and provides no writable/backpressure event, so it cannot stop the MTProto
caller and resume later. The client therefore:

- spends relay-granted send credit before emitting each `DATA` frame;
- splits outgoing data into at most 64 KiB frames;
- queues excess data per stream;
- coalesces adjacent writes up to 64 KiB and avoids front-removal copies;
- fails the stream if its pending uplink exceeds 8 MiB or 1024 queued items;
- caps all queued cross-thread uplink data at 64 MiB and 8192 items;
- pauses stream flushing when the process-wide loopback socket write queue reaches
  4 MiB and resumes it as bytes drain;
- reserves 64 KiB of that socket budget for control traffic and bounds a separate
  64 KiB / 1024-frame control queue; and
- schedules ready streams round-robin, with at most 256 frames per worker turn.

If the active carrier makes no write progress for 30 seconds, the carrier fails and
normal MTProto reconnect logic replaces it. Exhausting a stream or transport budget
also fails promptly rather than allowing unbounded queued worker events. If
measurements show sustained multi-megabyte uploads can exhaust these bounds, a
future change must add writable backpressure to the `AbstractSocket` contract rather
than silently growing memory.

### 7.1 Performance envelope and built-in HTTP comparison

The hosted bridge batches up to 2 MiB and runs uplink and downlink concurrently.
Each direction is sequenced stop-and-wait in v1, giving an RTT-only busy-direction
bound of 40, 20, 10, and 4 MiB/s at 50, 100, 200, and 500 ms web-engine-to-relay RTT,
respectively. Actual results include transfer time, the relay-to-MTProxy leg, and
web-engine scheduling. The 4 MiB stream window is two carrier batches so returned
credit does not reproduce the former 256 KiB bottleneck.

The built-in MTProto HTTP transport also copies request/response bodies and uses an
HTTP wait request, but `QNetworkAccessManager` may keep several POSTs active. WEB is
therefore more RTT-sensitive today. That serialization, fixed batch size, and most
buffer copies are implementation choices; a bounded ordered pipeline or compatible
streaming carrier can narrow them. Inherent WEB cost remains one platform web
engine, an extra relay/TLS path, a native JavaScript boundary, and shared-carrier
head-of-line exposure. The explicit browser fallback adds MessageChannel and
loopback crossings. With a well-placed relay, ordinary messaging and moderate media
should be in the same practical class as the built-in HTTP transport, while direct
TCP/MTProxy remains the latency and peak-throughput reference.

## 8. Hidden WebView boundary

`lib_webview` exposes `WindowMode::Hidden`, `HiddenSupported()`, and `Window::valid()`.
Hidden mode creates the platform web engine without a Telegram window or embedded
Qt widget:

- macOS retains a native `WKWebView` without wrapping it in a `QWindow` or widget;
- Windows uses modern WebView2 with an invisible controller and no widget container;
- all-other platforms keep WebKitGTK in the existing helper process and attach it to
  an unmapped native GTK toplevel, without creating an embed/compositor widget.

Hidden mode does not install the normal WebView dialog UI. New-window navigation is
rejected. The transport allows only the exact canonical HTTPS bridge navigation.
The bridge is injected only into the top-level document, and native messages are
accepted only from the configured HTTPS origin (compared in ACE form, so IDN hosts
on Qt-whitelisted TLDs work); a message without a source URL is rejected. The
scrubbed `https://host/` history URL is accepted for messages but not as a fresh
navigation.

The carrier opens the WebView with `restrictedOrigin` set, which puts `lib_webview`
into its restricted profile: an ephemeral, per-carrier storage area; cookies never
accepted; downloads, new windows, subframe navigations, permission prompts,
authentication dialogs and non-`https`/`wss` requests to any other host refused;
and a document-start lock script, injected into every frame (so a fresh
`about:blank` realm cannot bypass it), that installs a `<meta>` CSP allowing only
inline script and connections to the exact origin, and defines `undefined` over
storage, workers, audio, speech, WebRTC (`RTCPeerConnection` and friends),
`WebTransport`, `WebAssembly`, notifications, payment, presentation, media capture,
file pickers, `navigator.credentials/mediaDevices/getUserMedia/wakeLock/share/xr/
getGamepads/storage/locks/sendBeacon/permissions` and similar. The operator's page
therefore only ever gets inline script plus `fetch`/`WebSocket` to its own origin.

Engine-level enforcement backs the script per platform:

- Windows (WebView2): the restricted profile owns its own user-data folder and
  browser process; it runs with `--disable-features=msSmartScreenProtection`
  and `IsReputationCheckingRequired = FALSE` (so the capability URL is never
  reported to SmartScreen) and
  `--force-webrtc-ip-handling-policy=disable_non_proxied_udp` (no direct UDP);
  requests to other origins are answered 403 from `WebResourceRequested`; page
  messages are not echoed back and new-window requests never reach the system
  browser.
- macOS (WKWebView): `peerConnectionEnabled` and `mediaDevicesEnabled` are turned
  off through the same KVC path used for `developerExtrasEnabled`, and on macOS 13+
  the configuration enables Lockdown Mode (no JIT, WebAssembly or WebGL). The
  lock script is a `WKUserScript` with `forMainFrameOnly:NO`.
- all-other platforms (WebKitGTK): `enable-webrtc` and `enable-media-stream` are
  set to false when the installed WebKitGTK exposes them, the 4.0/4.1 API path
  enables the web-process sandbox (`webkit_web_context_set_sandbox_enabled`),
  the engine already runs in the separate `-webviewhelper` process, and the lock
  script is injected with `WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES`. The helper's
  script-message cap is 2 MiB, enough for a full 1 MiB relay frame in base64.

Debug builds evaluate a probe after the first bridge message and log a line if
`RTCPeerConnection`, `WebTransport` or `WebAssembly` is still reachable in the
top frame or in a freshly created `about:blank` iframe.

The page receives an exact-origin `TelegramWebProxy` object at document start. This
uses the same deployed bridge contract as Android: a fresh 32-byte URL-safe nonce in
`#android=`, `tproxy-android-init` version 1, and raw relay frames. Since the common
desktop WebView API carries strings, binary frames cross the native boundary as
strict base64 and are acknowledged by monotonically increasing write sequence. The
native queue is bounded to 8 MiB / 1024 items.

The candidate must receive exactly one valid `WELCOME`, and only after the nonce'd
`tproxy-android-init`. The handshake deadline is ten seconds, extended by another
ten seconds each time the bridge reports `status: connecting|reconnecting` (the
bridge's own retry budget is ~23–28 s), up to 45 seconds from navigation start.
Once adopted, the main thread probes JavaScript every three seconds; ten seconds
without a valid bridge message, or ten seconds without the acknowledgement for a
native write, fails the carrier.

The transport keeps at most 512 frames outstanding towards the WebView (control
frames may use the last 64 of them) and coalesces `WINDOW` grants per stream,
emitting them once 256 KiB accumulate or after 20 ms, so the carrier's hard
1024-item cap is unreachable in normal operation.

When a carrier is dropped after being adopted, the close control is evaluated in
the page and the WebView is kept alive for a further 200 ms so the bridge can send
its `DELETE /api/v1/session`.

WebView failures are retried with exponential backoff from 2 to 30 seconds (reset
on every successful adoption). The row shows `connecting` throughout; the
`WaitingForBrowser` state, and the confirmation box offering the system-browser
fallback, appear only for a terminal failure: hidden WebViews unsupported on the
platform (checked once per activation; nothing is retried), WebView creation
failed, or three consecutive failures without a single adoption. Retries continue
in the background at the maximum interval after that.

## 9. Explicit system-browser fallback boundary

The worker binds `QHostAddress::LocalHost` on an ephemeral port only when the user
asks for the fallback (`Open browser`), and advertises the numeric origin
`http://127.0.0.1:<port>`. The listener is closed again once a browser tab has
authenticated, when the minted capability expires unused, when the hidden WebView
takes over, and on deactivation, so there is no listening port unless the fallback
was requested. A page reload after that needs a fresh `Open browser`.

`GET /` serves the inline parent with `no-store`, `nosniff`, `no-referrer`, and a
fresh per-response script nonce. Its strict CSP permits only that nonce-bound
bootstrap, the configured HTTPS iframe origin, and its exact local WebSocket
endpoint.

`GET /transport` upgrades to RFC 6455 only when all of the following hold:

- peer address is loopback;
- method/path are exactly `GET /` or the `/transport` upgrade;
- `Host` is the exact numeric loopback host and current port;
- `Origin` is the exact loopback page origin;
- `Upgrade`, `Connection`, version 13, and a valid 16-byte key are present;
- duplicate HTTP header names are rejected;
- request bodies and transfer encodings are rejected on the local GET boundary;
- the HTTP header block is at most 16 KiB.

Client WebSocket frames must be masked. The parser supports 7/16/64-bit lengths,
text, binary, continuation, ping, pong, and close, with a 2 MiB message cap. Server
frames are unmasked as required by RFC 6455.

An accepted local client must complete capability authentication within ten seconds.
This bounds silent HTTP connections and unauthenticated WebSockets so they cannot
hold all 32 local client slots indefinitely.

The first complete WebSocket message must be UTF-8 JSON:

```json
{"t":"auth","token":"<capability>","browser":"<user agent summary>"}
```

The capability is 256 random bits, URL-safe base64, carried only in the fragment of
the browser URL. The page removes it from the visible URL immediately. It is
one-shot, expires after five minutes, and is replaced when another tab is opened. A
newly authenticated tab replaces the previous authenticated tab and causes MTProto
streams to reconnect rather than attempting unsupported cross-tab resume.

After authentication:

- the client first sends the text message
  `{"t":"bridge","url":"https://<host>/?bridge=<capability>"}`; the derived
  capability is therefore only ever handed to a tab that proved possession of the
  one-shot fragment token, never embedded in the unauthenticated `GET /` page;
- binary WebSocket messages carry one or more shared relay frames;
- text messages from the page may only report bridge state as
  `{"t":"status","state":"connecting|connected|reconnecting|failed"}`;
- pings from unauthenticated clients are ignored.

If an authenticated browser does not return the required `WELCOME` within 30
seconds, the client fails that carrier and closes its local WebSocket. This turns a
wrong bridge capability, iframe load failure, or ordinary public response into a
recoverable `unavailable` state instead of leaving the settings row connecting
forever.

## 10. Parent page and hosted iframe contract

The local parent reads and scrubs its independent one-shot loopback capability,
connects the local WebSocket, waits for the `bridge` text message, and only then
creates an iframe (with limited `sandbox` flags and `referrerPolicy` set before
`src`) for that URL, which must begin with `relayOrigin + '/?bridge='`, and
establishes a `MessageChannel`.

The parent also creates two same-page `RTCPeerConnection`s with an empty ICE-server
list, exchanges their descriptions only in local JavaScript, rewrites exchanged host
candidates to `127.0.0.1`, and retains an open, otherwise idle `RTCDataChannel`.
This avoids mDNS/interface-dependent self-connect behavior and keeps RTC packets on
loopback. No RTC state is exposed to the hosted iframe. The guard starts with the
authenticated loopback WebSocket, closes with it or on `pagehide`, and is recreated
on `pageshow` or with bounded backoff if the local RTC connection fails. Browsers
without usable WebRTC continue with the ordinary carrier. The guard reduces Chrome
background freezing, intensive timer throttling, and normal automatic discard risk,
but it is not a correctness dependency: manual tab closure, browser or OS
termination, and urgent discard remain ordinary transport loss.

For a canonical hostname `H` and decoded WEB secret bytes `S`, including the leading
`dd` byte when present, it computes:

```text
context = UTF-8("tdesktop-web-proxy-bridge-v1\n" + H)
bridge = base64url-no-padding(HMAC-SHA256(key=S, message=context))
bridgeUrl = "https://" + H + "/?bridge=" + bridge
```

Normative vectors:

| Hostname | Decoded secret hex | `bridge` |
|---|---|---|
| `proxy.example.com` | `000102030405060708090a0b0c0d0e0f` | `MHLEY5PmW1GWqJkSrlmJpvJUiLhBH_QKy6yKg8a0JPk` |
| `proxy.example.com` | `dd000102030405060708090a0b0c0d0e0f` | `IpJrt3e7sKtzPyoXy6w-Zj6GGEvsvclN66JzQEfPYLA` |

The derived capability is constructed in memory in tdesktop and is neither stored
nor shown in proxy settings. On iframe load the parent sends exactly:

```javascript
iframe.contentWindow.postMessage(
  { t: 'tproxy-init', v: 1 },
  relayOrigin,
  [channel.port2]);
```

The target origin is exact and never `*`. Binary messages are transferred as
`ArrayBuffer`s in both directions. Frames received locally before iframe
initialization are queued briefly and transferred after initialization. The parent
does not parse shared relay frames and never receives the MTProxy secret. Both the
hosted uplink queue and the parent's local-WebSocket queue are capped at 32 MiB; the
hosted queue also caps retained buffer objects at 16384. Exceeding either bound
closes the carrier instead of growing browser memory without limit.

The iframe's status objects update the visible tab and are forwarded to tdesktop.
When the local WebSocket closes, the parent sends `{t:'close'}` so the bridge can
delete its relay session. Closing the fallback tab drops the local WebSocket and
disconnects its logical sockets. Telegram Desktop does not reopen a tab
automatically. It keeps trying the hidden WebView with the backoff from §8.
Reloading cannot reuse the scrubbed, one-shot loopback capability (and the
listener is closed once a tab authenticated); after another failure, the
confirmation or row menu can mint a fresh capability and open a new tab.

## 11. Settings and app integration

Proxy settings expose a fourth `WEB` radio option. The editor shows:

- one proxy hostname field;
- one MTProxy secret field;
- no socket host/port pair and no username/password controls.

Rows display only the hostname. Inactive WEB rows show `not tested` without creating
a checker, WebView, or browser tab. Only the exact active WEB row shows the live
transport lifecycle. `Open browser` is offered only after the built-in carrier has
failed. WEB remains unsupported for calls. Because the backend is still MTProxy,
WEB keeps the existing sponsored-proxy disclosure (in the editor and in the link
confirmation) and promotion refresh behavior. Like MTProxy, the WEB hostname is
what `initConnection` reports as the proxy address; QNetworkAccessManager traffic
outside MTProto is not routed through the WEB carrier.

WEB links use `webproxy`, a canonical hostname, and the MTProxy secret. Port 443 is
implicit and is neither accepted from the link nor displayed in its confirmation:

```text
https://t.me/webproxy?server=<hostname>&secret=<secret>
tg://webproxy?server=<hostname>&secret=<secret>
```

The parser also accepts `host` when `server` is absent for compatibility with the
Android fork. Generated public links always use `server`. Following either link
shows the hostname and secret with one connect action. It does not check status or
enable the proxy until that action is invoked. Saved WEB entries can be shared as a
public link or a direct-scheme QR link.

Application proxy changes configure/deconfigure the web transport before MTP
sessions restart. WEB follows the MTProxy path in `Session`, `SessionPrivate`, and
`TcpConnection`; the global Qt proxy remains disabled for it. Proxy rotation and the
settings availability checker deliberately skip inactive WEB entries instead of
opening a WebView or browser.

## 12. Constraints and boundaries

- The listener is IPv4 loopback-only and validates peer, host, and origin.
- Local authentication requires the minted fragment capability.
- The local protocol has no arbitrary destination command. `OPEN` originates only
  from tdesktop and the relay is expected to dial one configured stock MTProxy.
- The configured value is a canonical DNS hostname; HTTPS and port 443 are fixed.
- The bridge URL contains only the domain-separated derived capability, never the raw
  MTProxy secret.
- Frame, WebSocket, HTTP-header, local-client-count, receive-window, and
  pending-uplink bounds prevent unbounded buffering.
- The parent iframe uses only `sandbox="allow-scripts allow-same-origin"`.
- Payloads and secrets are never logged by this client code.
- WEB socket failures do not invoke tdesktop's direct HTTP time-sync fallback.
- `CLOSE` is an abort in both directions: undelivered `DATA` on a closed stream is
  dropped, exactly like tdesktop's existing TCP path, which never half-closes.
- Trust model: the operator is semi-trusted. Their page runs JavaScript inside the
  restricted profile (§8) and never sees the MTProxy secret; the derived
  capability is a stable bearer for (host, secret). A local process cannot obtain
  the capability from the loopback listener (§9); it needs `tdata`.
- Relay authentication (`AUTH_CHAL` / `AUTH_RESP`) is not implemented in v1. Adding
  it requires a fully specified challenge context and server test vectors; it must be
  computed in tdesktop without passing the secret to JavaScript.

## 13. Hosted-server requirements before execution testing

The server must provide all of these before the separate test plan can pass:

1. `https://<hostname>/?bridge=<derived-capability>` implements the exact derivation,
   ordinary-site fallback, `MessageChannel`, close, and status contracts above.
2. Its CSP allows framing by random numeric loopback origins. A suitable source is
   `http://127.0.0.1:*`; `X-Frame-Options` must not block the embed.
3. The bridge accepts the v1 `HELLO` frame, establishes a reliable ordered carrier,
   and returns `WELCOME` before stream traffic.
4. The relay implements all v1 stream frames, the implicit 4 MiB windows, and
   deduplicated/cursor-based reliability for polling carriers.
5. Every `OPEN` dials only the configured stock MTProxy endpoint.
6. The hosted code never logs frame payloads.
7. The v1 HTTPS long-poll carrier is operational; the deployed bridge does not
   require a public WebSocket or another carrier.

## 14. Implementation inventory

Core transport:

- `Telegram/SourceFiles/mtproto/web_proxy/web_proxy_frame.{h,cpp}`
- `Telegram/SourceFiles/mtproto/web_proxy/web_proxy_transport.{h,cpp}`
- `Telegram/SourceFiles/mtproto/web_proxy/web_proxy_webview.{h,cpp}`
- `Telegram/SourceFiles/mtproto/details/mtproto_web_proxy_socket.{h,cpp}`

Native WebView support:

- `Telegram/lib_webview/webview/webview_common.h`
- `Telegram/lib_webview/webview/webview_embed.{h,cpp}`
- the macOS, Windows WebView2, and WebKitGTK platform backends

Integration:

- `mtproto_proxy_data.*`, `core_settings_proxy.cpp`
- `connection_tcp.cpp`, `session.cpp`, `session_private.cpp`, `proxy_check.cpp`
- `application.cpp`, `main_account.cpp`
- `boxes/connection_box.{h,cpp}`, `lang.strings`
- `Telegram/CMakeLists.txt`

The client-side implementation is complete without the hosted server. Remaining
verification is the hosted protocol, native-WebView/platform matrix, and explicit
browser-fallback matrix in `docs/web-proxy-test-plan.md`.

## 15. Explicitly deferred

- checking inactive WEB proxies and auto-rotation into them;
- cross-tab or cross-process relay-session resume;
- relay-auth v2;
- alternate bridge paths, ports, or non-HTTPS relay origins;
- expanding `AbstractSocket` with true uplink writable backpressure.
