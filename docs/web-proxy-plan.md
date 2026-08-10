# Telegram Desktop WEB proxy: refined client design

The hosted half is specified in `../tproxy-server/PLAN.md`. Its multiplexing frame
format and `MessageChannel` contract are authoritative. This document records the
reviewed Telegram Desktop design and the implementation now present in this tree.
The server-dependent execution procedure is intentionally separate in
`docs/web-proxy-test-plan.md`.

## 1. Scope and invariant

WEB is an MTProxy whose carrier is the user's real browser:

```text
MTProto session threads
  -> TcpConnection (existing MTProxy obfuscation and AES-CTR)
  -> WebProxySocket (one logical stream)
  -> process-wide WebProxy::Transport (one worker thread)
  -> authenticated ws://127.0.0.1:<random>/transport
  -> loopback parent page
  -> MessageChannel
  -> https://relay.example/?bridge=<derived-capability> iframe
  -> HTTPS carrier
  -> hosted relay
  -> stock MTProxy
  -> Telegram
```

The central invariant is that Telegram Desktop opens no external MTProto socket
while WEB is active. The only external connection in this path is made by the real
browser. The browser and hosted relay see only bytes already transformed by the
existing MTProxy protocol layer; the MTProxy secret is never placed in HTML or
JavaScript.

## 2. Design decisions

The initial draft left several architectural choices open. They are now fixed:

1. There is one transport per process, not per account. Proxy selection is already
   process-wide, so all accounts using the selected WEB proxy share one browser tab
   and one multiplexed browser carrier.
2. The transport owns a dedicated `QThread`. `QTcpServer`, accepted loopback
   sockets, WebSocket framing, mux state, queues, and windows live only there.
3. `WebProxySocket` and the transport are compiled in the main `Telegram` target.
   `connection_tcp.cpp`, the only factory site retaining the full `ProxyData`, is
   already in that target. No reverse dependency from `td_mtproto` is introduced.
4. The serialized `host` field stores only the canonical lowercase ASCII/IDNA
   A-label hostname. Scheme, port, path, query, fragment, user info, IP addresses,
   and single-label names are rejected. `port` is fixed to `443`; `password` stores
   the MTProxy secret.
5. WEB is manual-entry-only in v1. It has no `tg://proxy` share/import format.
6. Inactive WEB entries are not checked and are removed from proxy rotation's
   candidate order in v1. Checking would require activating a browser sidecar and
   must never open tabs for every saved proxy.
7. The loopback parent is one inline, dependency-free HTML response. A qrc asset
   adds no value for this small page and would create another generated-resource
   dependency.
8. While the authenticated loopback WebSocket is open, the local parent maintains
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
- custom DC/proxy DNS resolution is disabled because the browser resolves the relay
  hostname.
- calls remain unsupported.
- TCP MTProto is enabled and the plain MTProto HTTP connection is disabled.
- DC endpoints are ignored; the hosted relay chooses its fixed stock-MTProxy target.
- `initConnection` reports the relay hostname and port 443 as client proxy metadata.

## 4. `WebProxySocket`

`mtproto/details/mtproto_web_proxy_socket.*` implements `AbstractSocket` as a
logical byte stream over the shared transport.

On `connectToHost`, it registers a new 24-bit stream id. The address and port
arguments are intentionally ignored. It emits `connected` after the browser carrier
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
  selected valid proxy, binds the loopback listener, and auto-opens one browser tab
  when the selected WEB proxy changes.
- `OpenBrowser(proxy)` mints a fresh one-shot capability and opens a new tab on
  explicit user request.
- `Deactivate()` closes streams, accepted clients, and the listener when the app
  changes away from WEB.
- `Shutdown()` runs after MTP accounts have stopped and joins the worker thread.

Session-thread interaction uses queued calls into the worker. Each stream stores its
socket context, and worker-to-socket delivery is queued to that socket's owning
thread. `WebProxySocket` destruction unregisters synchronously on the worker before
the QObject base destructor can invalidate the context. This creates a strict
ordering boundary: notifications already posted remain owned by Qt and are removed
with the QObject, while the worker cannot inspect or post through the context after
unregistration returns. The global transport pointer is atomic and remains alive
until all MTP sessions have been destroyed.

The state surfaced to settings is:

```text
Idle
  -> WaitingForBrowser
  -> Connecting
  -> Connected
  -> WaitingForBrowser  (tab/local WS lost)
  -> Failed             (protocol/relay failure)
```

## 6. Shared relay frames

All integers are big-endian. The implementation mirrors server plan section 7:

```text
type:u8 | stream_id:u24 | length:u32 | payload:length
```

Each browser carrier message must contain one or more complete frames. The parser
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

If the browser socket makes no write progress for 30 seconds, the carrier fails and
normal MTProto reconnect logic replaces it. Exhausting a stream or transport budget
also fails promptly rather than allowing unbounded queued worker events. If
measurements show sustained multi-megabyte uploads can exhaust these bounds, a
future change must add writable backpressure to the `AbstractSocket` contract rather
than silently growing memory.

### 7.1 Performance envelope and built-in HTTP comparison

The hosted bridge batches up to 2 MiB and runs uplink and downlink concurrently.
Each direction is sequenced stop-and-wait in v1, giving an RTT-only busy-direction
bound of 40, 20, 10, and 4 MiB/s at 50, 100, 200, and 500 ms browser-to-relay RTT,
respectively. Actual results include transfer time, the relay-to-MTProxy leg, and
browser scheduling. The 4 MiB stream window is two carrier batches so returned
credit does not reproduce the former 256 KiB bottleneck.

The built-in MTProto HTTP transport also copies request/response bodies and uses an
HTTP wait request, but `QNetworkAccessManager` may keep several POSTs active. WEB is
therefore more RTT-sensitive today. That serialization, fixed batch size, and most
buffer copies are implementation choices; a bounded ordered pipeline or compatible
streaming carrier can narrow them. Inherent WEB cost remains one browser process,
an extra relay/TLS path, MessageChannel/loopback crossings, and shared-carrier
head-of-line exposure. With a well-placed relay, ordinary messaging and moderate
media should be in the same practical class as the built-in HTTP transport, while
direct TCP/MTProxy remains the latency and peak-throughput reference.

## 8. Loopback HTTP and WebSocket boundary

The worker binds `QHostAddress::LocalHost` on an ephemeral port and advertises the
numeric origin `http://127.0.0.1:<port>`.

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

- binary WebSocket messages carry one or more shared relay frames;
- text messages may only report bridge state as
  `{"t":"status","state":"connecting|connected|reconnecting|failed"}`.

If an authenticated browser does not return the required `WELCOME` within 30
seconds, the client fails that carrier and closes its local WebSocket. This turns a
wrong bridge capability, iframe load failure, or ordinary public response into a
recoverable `unavailable` state instead of leaving the settings row connecting
forever.

## 9. Parent page and hosted iframe contract

The local parent reads and scrubs its independent one-shot loopback capability,
connects the local WebSocket, derives the bridge URL, creates an iframe with limited
`sandbox` flags, and establishes a `MessageChannel`.

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

The derived capability is constructed in memory and is neither stored nor shown in
proxy settings. On iframe load the parent sends exactly:

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
delete its relay session. Closing the tab drops the local WebSocket, disconnects all
logical sockets, and leaves the settings row in `waiting for browser…`. Telegram
Desktop does not reopen a tab automatically after a user closes it. Reloading cannot
reuse the scrubbed, one-shot loopback capability either. In both cases the row menu
provides `Open browser`, which mints a fresh loopback capability and opens a new tab.

## 10. Settings and app integration

Proxy settings expose a fourth `WEB` radio option. The editor shows:

- one proxy hostname field;
- one MTProxy secret field;
- no socket host/port pair and no username/password controls.

Rows display only the hostname. The selected row
shows the transport lifecycle, and its menu has `Open browser`. WEB remains
non-shareable and unsupported for calls. Because the backend is still MTProxy, WEB
keeps the existing sponsored-proxy disclosure and promotion refresh behavior.

Application proxy changes configure/deconfigure the browser transport before MTP
sessions restart. WEB follows the MTProxy path in `Session`, `SessionPrivate`, and
`TcpConnection`; the global Qt proxy remains disabled for it. Proxy rotation and the
settings availability checker deliberately treat inactive WEB entries as unavailable
instead of opening a browser.

## 11. Constraints and boundaries

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
- Relay authentication (`AUTH_CHAL` / `AUTH_RESP`) is not implemented in v1. Adding
  it requires a fully specified challenge context and server test vectors; it must be
  computed in tdesktop without passing the secret to JavaScript.

## 12. Hosted-server requirements before execution testing

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

## 13. Implementation inventory

Core transport:

- `Telegram/SourceFiles/mtproto/web_proxy/web_proxy_frame.{h,cpp}`
- `Telegram/SourceFiles/mtproto/web_proxy/web_proxy_transport.{h,cpp}`
- `Telegram/SourceFiles/mtproto/details/mtproto_web_proxy_socket.{h,cpp}`

Integration:

- `mtproto_proxy_data.*`, `core_settings_proxy.cpp`
- `connection_tcp.cpp`, `session.cpp`, `session_private.cpp`, `proxy_check.cpp`
- `application.cpp`, `main_account.cpp`
- `boxes/connection_box.{h,cpp}`, `lang.strings`
- `Telegram/CMakeLists.txt`

The client-side implementation is complete without the hosted server. The arm64
Debug build passes; remaining verification is the hosted protocol/loopback and
browser matrix in `docs/web-proxy-test-plan.md`, followed by other platform builds.

## 14. Explicitly deferred

- public deep-link/share format;
- checking inactive WEB proxies and auto-rotation into them;
- cross-tab or cross-process relay-session resume;
- relay-auth v2;
- alternate bridge paths, ports, or non-HTTPS relay origins;
- expanding `AbstractSocket` with true uplink writable backpressure.
