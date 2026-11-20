# Harness helper index

[Harness guide](../../README.md) · [Documentation format](../adding-helpers.md)

Search this index by module or need, then read only the selected reference and
its source header. These are lookup pages, not a required reading bundle.
Self-tests exercise the harness; they are not routines every product overlay
should call.

| Module | Kind | Use |
| --- | --- | --- |
| [test_agent](test_agent.md) | API | Runtime gate, startup overrides with `TDESKTOP_TEST_*` escapes (scale, a fresh run download folder or a startup refusal, update checks suppressed per process, no automatic URL-handler registration, macOS App Nap hold), their product queries and fixture access |
| [test_runner](test_runner.md) | API | Stages, gated groups, bounded readiness and exact-object actions |
| [test_gated_stage](test_gated_stage.md) | Self-test | Self-tests for explicit and inherited gated skips |
| [test_session_gate](test_session_gate.md) | API | Test-account gate, switching, restoration and explicit production legs |
| [test_controller_ready](test_controller_ready.md) | API | Exact account/session window controllers from the window registry, observation-only readiness with named refusals and one guarded action |
| [test_log](test_log.md) | API | Evidence logs, assertions and telemetry formatting |
| [test_log_lines](test_log_lines.md) | Self-test | Self-test for one physical record per log call |
| [test_text_reads](test_text_reads.md) | API | Space-normalized text assertions |
| [test_lang_pack](test_lang_pack.md) | API | Temporary language overrides and restoration |
| [test_l10n_seams](test_l10n_seams.md) | Internal dependency | Phrase-read observer, app-config overrides and the request holder for the localization capture |
| [test_probe](test_probe.md) | API | Windowed observations and correlated requests |
| [test_expiry_window](test_expiry_window.md) | API | Served-expiry observations bound to a live owner, generation and link; frozen cutoff windows and boundary polls with named refusals |
| [test_widgets](test_widgets.md) | API | Typed discovery, a button's own disabled state, live publications and input events; key presses leave Qt's cached keyboard modifiers released |
| [test_wheel](test_wheel.md) | Self-test | Self-test for wheel delivery |
| [test_keyboard_modifiers](test_keyboard_modifiers.md) | Self-test | Self-test for released keyboard modifiers after `PressKey` and a QPA sequence's `end()`, beside modifier delivery during the press and a widget shortcut |
| [test_field_submit](test_field_submit.md) | API | Submit a `Ui::InputField` with the Return chord its submit setting requires - the app's `sendSubmitWay()` by default - and report whether `submits()` fired, refusing `None` by name; with its self-test |
| [test_queued_drag](test_queued_drag.md) | API | Deliver queued drag starts before the synthetic release |
| [test_qpa_mouse](test_qpa_mouse.md) | API | Mouse press, move and release through Qt's platform layer and window routing, for widgets that grab the mouse; a held press keeps the pressed widget at the sequence's point through lib_ui's moves built from the OS pointer; a drag stage that waits for queued work before its release and leaves no button, grab, hover or keyboard modifier |
| [test_qpa_mouse_self_test](test_qpa_mouse_self_test.md) | Self-test | Self-test for QPA grab routing, the drag stage's wait and refusals, coordinates, double clicks, clean end state and a paced gesture passing its own armed console shield, with the OS cursor covered on Windows |
| [test_qpa_mouse_held_self_test](test_qpa_mouse_held_self_test.md) | Self-test | Self-test for a held QPA press clicking once through a scroll that sends lib_ui's move from the OS pointer, beside a deliberate move outside that does not |
| [test_touch](test_touch.md) | API | Registered touchscreen and touchpad input through Qt's platform layer |
| [test_text_drop](test_text_drop.md) | Self-test | Self-test for text-drop routing |
| [test_clipboard](test_clipboard.md) | API | Clipboard round-trip and classified read-back |
| [test_console_lock](test_console_lock.md) | API | Current console-lock classification |
| [test_console_shield](test_console_shield.md) | API | Opt-in shield that vetoes and counts console input outside harness delivery, passes product closes and quits, and releases on finish |
| [test_capture](test_capture.md) | API | Prepared grabs, paint owners, mapped targets with typed coordinate owners, strict comparisons, checked full-owner crops and saved-image markers |
| [test_mapped_target](test_mapped_target.md) | Self-test | Self-test for full mapped-target visibility and typed coordinate owners |
| [test_post_paint](test_post_paint.md) | API | Samples after real product paints, shared capture scopes and paint receipts |
| [test_full_owner_capture](test_full_owner_capture.md) | Self-test | Self-test for coordinated samplers, strict renders, checked full-owner crops and panel sizing |
| [test_animation_clock](test_animation_clock.md) | API | Deterministic elapsed-time animation frames |
| [test_paint_stream](test_paint_stream.md) | API | Whole-owner QPicture streams and manifest for optimized replay timing |
| [test_corner_patch](test_corner_patch.md) | API | Rounded-corner comparisons and controls |
| [test_marking_read](test_marking_read.md) | API | Prevent primary-window read marking |
| [test_window_exposure](test_window_exposure.md) | API | Keep covered windows painting without activation |
| [test_application_state](test_application_state.md) | API | Inject the application's inactive and active state that `Core::App().appDeactivatedValue()` follows, read back, with named refusals and without touching the focus window; with its self-test |
| [test_layer_root](test_layer_root.md) | Self-test | Self-test for the painting-layer resolver |
| [test_via_window](test_via_window.md) | Self-test | Self-test for window-mapped captures |
| [test_toast_capture](test_toast_capture.md) | API | Isolated toast-subtree captures, shown/hiding/gone toast state with a bounded gone wait, and painted-content captures |
| [test_hover](test_hover.md) | Self-test | Self-test for click/drag hover cleanup |
| [test_press_origin](test_press_origin.md) | Self-test | Self-test for press-point ripple origins |
| [test_activation](test_activation.md) | Self-test | Self-test for injected window activation |
| [test_box_button](test_box_button.md) | API | Resolve and click shell footer buttons |
| [test_ink](test_ink.md) | API | Painted bands, ink, colour and contrast, glyph cores on flat and gradient grounds, isolated glyph bounds |
| [test_style](test_style.md) | API | Settled palette samples and baseline checks |
| [test_palette](test_palette.md) | API | Switch and restore palettes while respecting system following |
| [test_panel](test_panel.md) | API | Separate-panel discovery, show-cache readiness and typed fixture sizing |
| [test_menu](test_menu.md) | API | Same-turn menu readings and their later-turn retake, actions, keyboard activation of accessible custom rows, and captures; the capture self-test decides its opening-turn grab row on the menu's own show state |
| [test_popup_hold](test_popup_hold.md) | API | Opt-in hold that keeps widget popups open when a Windows application deactivates, counts what it stopped, and releases on finish |
| [test_chat_menu](test_chat_menu.md) | API | Attribute chat-list context menus to the exact message |
| [test_chat_show](test_chat_show.md) | API | Show a chat-list message wholly in view, painted, ready and hovered before input |
| [test_chat_show_self_test](test_chat_show_self_test.md) | Self-test | Self-test for chat-list show acceptance, re-correction and named refusals |
| [test_chat_show_place](test_chat_show_place.md) | API | Place several chat-list messages wholly in or out of view together, gated by geometry and a fresh visibility reading, and prove they stayed for a window |
| [test_widget_ops](test_widget_ops.md) | API | Tap a widget message's hosted hosts for their validated ops, results, themes and press steps, and read each hosted work's final charge and death |
| [test_widget_host](test_widget_host.md) | API | Override a widget message's host measurement options and record what its host got and reported: applied options, stats, nodes, load, verbatim death with its time, diag counts and error logs, one record per host; and its renderer core's work, timed by a disposable overlay's scopes |
| [test_widget_attacks](test_widget_attacks.md) | API | Audit widget-sandbox's attack corpus in the chat list: each file's leak scan and death verdict, one click per eligible card, and optional loopback controls around all files |
| [test_widget_bot](test_widget_bot.md) | API | Answer an armed widget message's bot requests from a table at the runtime's request seam, with a record per message |
| [test_widget_conformance](test_widget_conformance.md) | API | Run widget-sandbox's conformance sets through the chat list with real input and 2x crops into the run file `report.mjs` reads, deaths read from final charges |
| [test_widget_stress](test_widget_stress.md) | API | Run widget-sandbox's stress kit through the chat list, one widget alive at a time in a one-column window on the fastest screen, with frame, renderer, host and widget readings per level (of a hosted widget its own theme reading and, in Debug, the helper's renderer work), into the run file of one pass that `report.mjs` reads |
| [test_widget_fixture](test_widget_fixture.md) | Internal dependency | Shared live-chat fixture and removal barrier of the widget packs; the reference for where widget engines run and how a helper's end is charged |
| [test_widget_helper_probe](test_widget_helper_probe.md) | Internal dependency | Debug probe of the widget helper's lockdown, inside `TelegramWidgetHelper`'s Debug build on macOS, Linux and Windows: its argv tail; on macOS the full descriptor and Mach listing after the real lockdown, the denied attempts with their unsandboxed control, and a refusal summary instead of serving, or, with `rendering:<ms>`, the same listing taken later while the helper serves widgets; on Linux the probe lines on stderr from forked copies and the locked helper, then serving hosted widgets; on Windows, in `modules\<arch>\TelegramWidgetHelper.exe`, the native probe report on stderr before the helper serves hosted widgets |
| [test_widget_helper_tap](test_widget_helper_tap.md) | Internal dependency | The helper's half of the validated-ops tap, twidget's Rust tap in `TelegramWidgetHelper`'s Debug build (macOS, Linux and Windows): the hosted renderer's wrapper that, armed by the client's `Mirror` or `MirrorWork` control, mirrors the served host's and renderer's messages and the scheme to the client's tap and, when asked, times the helper's renderer work |
| [test_widget_renderer_forward](test_widget_renderer_forward.md) | Internal dependency | The format and decode bounds of the helper work record that the helper's Rust tap writes and Telegram's tap reads |
| [test_messages](test_messages.md) | API | Observe matching sent server messages |
| [test_history_fixtures](test_history_fixtures.md) | API | Lifetime-owned history service-message fixtures |
| [test_notify_override](test_notify_override.md) | API | Local notify-setting override and restoration |
| [test_custom_emoji](test_custom_emoji.md) | API | Explicitly registered ready emoji fixture |
| [test_launch_fuse](test_launch_fuse.md) | API | Assert intended OS launches while blocking them; report a credential-bearing one by label |
| [test_open_handoff](test_open_handoff.md) | API | Observe document-open handoff and launch classification without OS launch |
| [test_transfer](test_transfer.md) | API | Observe document transfer success/failure |
| [test_web_cache](test_web_cache.md) | API | Clear the exact web-document cache entry |
| [test_rpc_retry](test_rpc_retry.md) | API | Observe server-error resends |
| [test_rpc_fixture](test_rpc_fixture.md) | API | Boxed controlled RPC replies |
| [test_fixture_fence](test_fixture_fence.md) | API | Refuse and record requests naming registered local fixture message or synthetic peer ids |
| [test_fixture_transaction](test_fixture_transaction.md) | API | Read-before-write server fixture changes with rate deadlines and a resumable restoration record |
| [test_wallet_ready](test_wallet_ready.md) | API | Judge served wallet freshness |
| [test_wallet_intercept](test_wallet_intercept.md) | API | Controlled wallet request interception |
| [test_gram_reconcile](test_gram_reconcile.md) | API | Reconcile golden-wallet custody and words |
| [test_secrecy_scan](test_secrecy_scan.md) | API | Certify fixture-secret absence from logs |
| [test_publication_identity_scan](test_publication_identity_scan.md) | API | Audit supplied public identities and launch-log coverage |
| [test_publication_identity_scan_self_test](test_publication_identity_scan_self_test.md) | Self-test | Self-test for identity, coverage and claim separation |
| [test_log_scan_internal](test_log_scan_internal.md) | Internal dependency | Shared reader for secrecy, identity and coverage scans |
| [test_scenario](test_scenario.md) | Scenario slot | Disposable task scenario entry point |

Read the selected module's header before using it; the contracts there are
more precise than the summary above. Search this directory before writing a
new local helper: an overlay that reimplements a shared facility is a test
flaw like any other, because the local copy carries none of the refusals the
shared one accumulated. One overlay rebuilt grab-check-save by hand after
`CaptureWidget` had already refused blank and hidden targets for months.

For new or changed modules, follow [Adding a harness helper](../adding-helpers.md).
