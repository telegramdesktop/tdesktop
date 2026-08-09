# Telegram Desktop WEB proxy: server-ready execution test plan

Execute this document only after a hosted relay matching
`../tproxy-server/PLAN.md` is deployed. Record every requested value and artifact in
one run log. Do not put the proxy secret, Telegram auth keys, message contents, or
the loopback fragment capability in logs or screenshots.

## 1. Release gate

WEB is ready for wider testing only when all P0 and P1 cases below pass on at least
one Chromium browser and the supported browser/platform matrix has no unexplained
P0 failure.

Severity:

- P0: transport boundary, corruption, crash, login, basic send/receive, or total
  reconnect failure.
- P1: large transfer, concurrency, bounded memory, lifecycle recovery, or carrier
  fallback failure.
- P2: status text, browser-specific lifecycle annoyance, or non-blocking polish.

## 2. Inputs to obtain from the server operator

Record these before starting:

| Input | Value |
|---|---|
| Relay hostname | `________________` |
| Bridge deployment/version | `________________` |
| Relay binary commit/version | `________________` |
| Stock MTProxy version | `________________` |
| MTProxy secret | store outside this document |
| Long-poll enabled | yes / no |
| Server log location | `________________` |
| Server metrics endpoint/dashboard | `________________` |
| Test start/end in UTC | `________________` |
| Tester network and country | `________________` |

The operator must confirm:

- the bridge root response can be framed by `http://127.0.0.1:*` and is not prevented
  by `X-Frame-Options`;
- `HELLO` payload `01`, `WELCOME`, implicit 4 MiB windows, and all v1 frame types
  match `docs/web-proxy-plan.md`;
- the polling carrier provides ordered retry/deduplication;
- `OPEN` can dial only the configured local stock MTProxy;
- payload logging is disabled.

Stop if any item is false. A client run cannot produce a meaningful result against
an incompatible bridge.

## 3. Client build and isolation

1. Build the exact candidate commit in Debug:

   ```bash
   cmake --build out --config Debug --target Telegram
   ```

2. Record the commit, build timestamp, OS version, browser name/version, and whether
   the browser is managed by enterprise policy.
3. Use a disposable Telegram test account and a separate portable/test profile. Do
   not overwrite an existing personal portable profile.
4. Preserve Debug logs for the run, but verify they contain no proxy secret or frame
   payload.
5. Start with all browser developer tools closed; opening DevTools changes background
   throttling and would invalidate lifecycle observations.
6. Disable unrelated VPNs/proxies for baseline. Record DNS-over-HTTPS, browser proxy,
   and system proxy state.

## 4. Hosted endpoint preflight

Run from the client machine:

```bash
curl -fsS -D /tmp/tproxy-site.headers -o /tmp/tproxy-site.body \
  https://RELAY_HOSTNAME/
curl -fsS -D /tmp/tproxy-invalid.headers -o /tmp/tproxy-invalid.body \
  'https://RELAY_HOSTNAME/?bridge=invalid'
```

Verify:

- TLS certificate and hostname are valid with no warning or redirect to HTTP;
- `/` looks like the intended ordinary site and contains no transport error details;
- an invalid or missing `bridge` query returns the same ordinary site without
  transport-branded errors;
- server unit tests pass both capability vectors from `docs/web-proxy-plan.md`;
- the valid bridge response has a compatible `frame-ancestors` CSP and no
  incompatible `X-Frame-Options`, verified without recording its query;
- transport APIs reject unsupported methods and malformed session identifiers;
- server health shows the stock MTProxy backend reachable.

Delete the captured bodies after inspection if the deployment embeds any
configuration. Keep sanitized headers with the run artifacts.

## 5. Configuration and first connection (P0)

1. Launch the candidate.
2. Open Settings -> Advanced -> Connection type -> Proxy settings -> Add proxy.
3. Select `WEB`.
4. Enter the relay hostname only and the MTProxy secret.
5. Confirm that these invalid inputs are rejected without saving:

   - a value containing `http://` or `https://`;
   - a hostname with an explicit port;
   - a value with username, path, query, or fragment;
   - an IPv4 or IPv6 address;
   - a single-label name such as `localhost`;
   - an invalid IDNA name, empty label, overlong label, or trailing dot;
   - invalid or unsupported MTProxy secret;
   - an `ee` TLS-emulation MTProxy secret;
   - empty hostname or secret.

6. Save the valid entry and enable it.
7. Confirm exactly one default-browser tab opens at a numeric
   `http://127.0.0.1:<ephemeral>/#<capability>` URL.
8. Confirm the fragment disappears immediately and the page says it is connecting.
9. Confirm the settings row moves through `waiting for browser…` / `connecting…` to
   `online` and the tab says it is connected.
10. Confirm Telegram loads dialogs and receives updates.

Pass conditions:

- no crash, assertion, TLS warning, CORS/LNA prompt, mixed-content error, or iframe
  refusal;
- server sees one relay session and the expected MTProxy stream connections;
- the iframe requests exactly `https://<canonical-host>/?bridge=<43-char-capability>`;
- the browser page, DOM, network request headers, and console never contain the
  MTProxy secret;
- closing proxy settings does not affect the connection.

Repeat once with a profile secret that is valid MTProxy syntax but is not configured
on staging. The ordinary-site fallback must match other root responses, and the
client must leave `connecting…` for `not available` within 30 seconds rather than
waiting forever.

## 6. Network-origin invariant (P0)

While WEB is the only enabled Telegram proxy, collect connection ownership with an
OS tool. On macOS, for example:

```bash
lsof -nP -iTCP -sTCP:ESTABLISHED | egrep 'Telegram|Chrome|Chromium|Edge|Safari|Firefox'
```

Also capture a short packet trace or firewall connection log if permitted.

Verify:

- Telegram Desktop connects to `127.0.0.1:<ephemeral>` only for this transport;
- Telegram Desktop has no external connection to the relay origin, stock MTProxy,
  or Telegram DCs for MTProto traffic;
- a WEB connection failure does not trigger tdesktop's HTTP time-sync fallback;
- the browser process owns the external HTTPS connection to the relay;
- DNS resolution of the relay is attributable to the browser/system resolver, not a
  custom proxy/DC resolver in tdesktop;
- external carrier traffic remains inside the browser-owned TLS connection.

Account for unrelated Telegram HTTP traffic such as update checks before declaring
a failure. The invariant applies to the MTProto transport, not every auxiliary HTTP
request made by the application.

## 7. Functional traffic (P0/P1)

Run in order, checking both client behavior and relay/MTProxy stream metrics:

1. Fresh-account login, including code entry and 2FA if available. P0.
2. Dialog/history load and live incoming updates. P0.
3. Send and receive plain messages in private and group chats. P0.
4. Send and receive stickers, reactions, edits, deletes, and read receipts. P1.
5. Download thumbnails and several small media files. P0.
6. Download one file larger than 1 GiB. P1.
7. Upload one file large enough to exceed the 4 MiB window many times. P1.
8. Stream a video while downloading another file. P1.
9. Open media from CDN-backed storage and confirm shifted/CDN DC streams work. P1.
10. Leave the client idle for 30 minutes, then send and receive immediately. P1.

For large transfers record:

- bytes and final content hash;
- average and minimum throughput;
- tdesktop, browser, and relay peak memory;
- number of logical streams and stock-MTProxy sockets;
- reconnect/retry count;
- pending-uplink overflow or flow-control errors.

Run the same payload once through direct MTProxy and once through tdesktop's built-in
HTTP transport under the same controlled capacity/RTT when those controls are
reachable. On a controlled link with at least 100 Mbit/s capacity, WEB should
sustain at least 40 Mbit/s at 200 ms browser-to-relay RTT and 20 Mbit/s at 500 ms,
in both a foreground and an ordinarily hidden tab on each supported browser.
Also report WEB/direct-HTTP ratios for message round-trip p50/p95 and bulk transfer;
route differences must be reported separately from transport overhead.

No transfer may corrupt, silently truncate, duplicate an upload, or grow client
memory without returning toward baseline after completion.

## 8. Multiplexing and concurrency (P1)

1. Start at least 16 simultaneous media downloads across chats.
2. Send messages continuously during the downloads.
3. Start a large upload at the same time.
4. If multi-account is available, sign in to two disposable accounts and generate
   traffic on both.
5. Confirm all logical streams use the same authenticated local WebSocket and browser
   relay session.
6. Confirm one slow or window-exhausted stream does not block unrelated streams.
7. Cancel half the transfers and verify the corresponding `CLOSE`s release relay and
   stock-MTProxy resources.
8. Let the remainder finish and compare hashes.

Monitor thread sanitizer output if a TSan build is practical. Otherwise run for at
least two hours while watching for cross-thread QObject warnings, stale stream
delivery, stream-id mixups, growing queues, and use-after-free crashes.

Add a focused destruction race run that repeatedly opens and destroys logical WEB
sockets while the worker concurrently delivers `connected`, `DATA`, `CLOSE`, and
failure notifications. Run it under ASan and TSan where supported. No notification
may begin after synchronous stream unregistration returns, and no callback may run
against a socket whose destructor has started.

## 9. Browser and transport lifecycle (P0/P1)

Execute each case from a connected baseline:

| Case | Expected result |
|---|---|
| Close the sidecar tab | all logical sockets disconnect; row says waiting; no tab auto-reopens |
| Row menu -> Open browser | fresh fragment capability; new tab authenticates; Telegram reconnects |
| Refresh the tab | consumed capability is not reusable; row says waiting; `Open browser` restores the connection with a fresh capability |
| Open browser twice | newest authenticated tab replaces the old one; no stream crosses sessions |
| Quit browser | same as tab loss; tdesktop remains responsive |
| Restart browser and use Open browser | clean reconnection |
| Restart tdesktop with WEB saved/enabled | new loopback port/token and one new tab; stale old tab cannot attach |
| Disable WEB | listener closes, browser loses local WS, normal connection policy resumes |
| Switch WEB A -> non-WEB -> WEB A | clean teardown and reactivation |
| Edit WEB hostname or secret | old transport closes; new settings take effect; no old relay traffic remains |
| System sleep 5 minutes | reconnect after wake without corruption or permanent spinner |
| Network down/up | browser carrier and MTProto recover within normal retry bounds |

Record whether the browser freezes/discards the active tab under battery/energy
saving. This is evidence for or against a future keepalive feature, not a v1 pass
condition if explicit `Open browser` recovers correctly.

## 10. Carrier reliability and server faults (P1)

Coordinate these with the server operator:

1. Drop an empty long-poll request or response. Verify bounded retry and continued
   Telegram usability.
2. Drop one nonempty downlink response after the relay has assigned a cursor. Verify the next
   request replays it once and tdesktop receives bytes once.
3. Drop an uplink response after the relay has processed the sequence. Verify retry
   deduplication prevents a second write to stock MTProxy.
4. Add 1%, then 5%, packet loss and 200-500 ms latency. Verify ordered recovery.
5. Restart the web relay while preserving or intentionally discarding session state;
   record expected bridge status and MTProto reconnection.
6. Restart stock MTProxy only. Affected logical streams must close/reconnect without
   breaking the browser carrier.
7. Return malformed frame length, unknown frame type, invalid stream zero usage,
   zero/invalid `WINDOW`, and data beyond granted credit in a controlled staging
   environment. The client must close the carrier/streams cleanly, remain responsive,
   and show no memory error.
8. Send `BYE`. Current streams must fail and reconnect according to the bridge/server
   recovery policy.
9. Race client-side close against backend EOF and delayed `DATA`/`WINDOW`/`CLOSE`.
   The closed stream may discard late frames, but unrelated streams must remain live.
10. Stall downlink reads and send highly fragmented one-byte/empty-control patterns.
    Verify the relay enforces byte and item budgets, returns at most 4096 frames in
    one body, coalesces adjacent credit, and returns near baseline heap use afterward.
11. Issue bridge requests past the per-IP burst/rate and unused-token limits. Other
    source IPs must retain bounded access, expired tokens must release their slots,
    and the public fallback must not expose why a request was limited.

## 11. Loopback validation tests (P0)

Use a purpose-built local test client; do not paste the real capability into shell
history.

Verify rejection of:

- a connection to a non-loopback interface (the port must not be listening there);
- wrong `Host`;
- absent, wrong, or cross-origin `Origin`;
- duplicate HTTP header names, including `Host` and `Origin`;
- invalid WebSocket key/version/upgrade headers;
- an unmasked client frame;
- invalid fragmentation or control-frame fragmentation;
- a WebSocket message larger than 2 MiB;
- an HTTP header block larger than 16 KiB;
- a GET with `Content-Length` or `Transfer-Encoding`;
- a silent connection or unauthenticated WebSocket held past ten seconds;
- first message not being an auth object;
- wrong, reused, expired, or empty capability;
- binary data before authentication;
- non-status text after authentication.

Then verify:

- a consumed capability cannot authenticate a second socket;
- minting via `Open browser` invalidates any unconsumed earlier capability;
- a newly authenticated browser replaces the old one and forces logical reconnect;
- `GET /` never includes the capability or MTProxy secret;
- the local protocol cannot request an arbitrary host or port;
- malformed input causes bounded close/failure, not a crash or growing buffer.
- the loopback parent CSP contains a fresh nonce and does not permit arbitrary
  inline script.

## 12. Persistence and compatibility (P1)

1. Save WEB, quit cleanly, relaunch, and verify hostname/secret/type survive with
   port fixed to 443.
2. Switch among System, Disabled, SOCKS5/HTTP/MTProxy, and WEB; verify Qt's global
   application proxy is never set to the WEB hostname.
3. Corrupt a copy of the serialized proxy type to an unknown future value and verify
   the candidate skips it instead of crashing. Never modify the only real settings
   file.
4. Launch an older binary against a disposable copy of settings containing WEB and
   document its behavior. The new binary handles unknown types; old binary
   behavior may still require a release-note warning.
5. Confirm WEB cannot be shared/copied as a `tg://proxy` link and inactive WEB rows do
   not auto-open a browser during availability checks or proxy rotation.
6. Confirm a saved and enabled WEB proxy intentionally opens one replacement tab at
   application startup, while closing that tab does not cause an automatic reopen
   loop.

## 13. Browser/platform matrix

At minimum execute sections 5, 6, 7 (small traffic), 9, and 11 on each supported
combination available:

| OS | Browser | Version | Result |
|---|---|---|---|
| Windows | Chrome | | |
| Windows | Edge | | |
| Windows | Firefox | | |
| macOS | Chrome | | |
| macOS | Safari | | |
| macOS | Firefox | | |
| all-other desktop | Chrome/Chromium | | |
| all-other desktop | Firefox | | |

Pay special attention to iframe CSP, loopback WebSocket Origin, mixed-content rules,
background-tab throttling, default-browser launch, and managed-browser policies.

## 14. Unreliable-MTProto network field test (P0)

This is the product hypothesis test and cannot be replaced by a lab run.

1. Use a network where direct Telegram and ordinary MTProxy are demonstrably
   unreliable.
2. Record those comparison results immediately before WEB testing.
3. Confirm the relay's ordinary site is reachable in the chosen browser.
4. Enable WEB and repeat login/history/message/media cases.
5. Capture sanitized connection ownership and traffic metadata.
6. Confirm the observable external client is the browser and all carrier requests are
   ordinary same-origin HTTPS; no public WebSocket is required.
7. Repeat at two times of day and, if possible, through two access providers.

Pass means WEB works while both direct Telegram and ordinary MTProxy controls fail,
without requiring a browser certificate exception or a nonstandard network setting.

## 15. Deployment-boundary checks (P1)

1. Run the Go unit suite and race detector, including concurrent carrier, bootstrap,
   queue-fragmentation, downlink-frame-count, and goroutine-shutdown cases.
2. Validate the shipped Caddyfile with the pinned Caddy build and exercise public
   root, bridge root, API, static asset, and error routes through Caddy.
3. Confirm `/debug/pprof/` is 404 on the admin listener by default and appears only
   when `enable_pprof` is explicitly enabled.
4. Confirm the MTProxy source archive matches the pinned commit checksum and its
   Makefile executes as `mtproxy`, not root.
5. From shells running as `caddy` and `tproxy`, verify the MTProxy command line is
   hidden by the supplied `/proc` restrictions. Record that root remains able to
   inspect the stock upstream `-S` argument.
6. Start sessions with active backend reads and writes, then stop the relay. Shutdown
   must complete inside its configured deadline with no backend goroutine left.

## 16. Exit criteria and run report

Attach or link:

- client/server/bridge/MTProxy versions;
- sanitized server headers and logs;
- Debug build result;
- functional and lifecycle checklist;
- transfer hashes and performance/memory table;
- connection-owner evidence;
- long-poll retry/replay evidence;
- loopback and protocol validation results;
- browser/platform matrix;
- unreliable-MTProto network comparisons and outcome;
- every defect with severity, exact reproduction, expected/actual result, timestamps,
  and relevant sanitized logs.

Final decision:

| Gate | Result |
|---|---|
| All P0 passed | |
| All P1 passed or explicitly waived | |
| Logs contain no secrets or payloads | |
| Memory and queues bounded | |
| Long-poll reliability proven | |
| Unreliable-MTProto network result confirmed | |
| Ready for wider testing | yes / no |
