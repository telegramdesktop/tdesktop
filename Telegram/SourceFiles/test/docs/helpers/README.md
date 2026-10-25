# Harness helper index

[Harness guide](../../README.md) · [Documentation format](../adding-helpers.md)

Search this index by module or need, then read only the selected reference and
its source header. These are lookup pages, not a required reading bundle.
Self-tests exercise the harness; they are not routines every product overlay
should call.

| Module | Kind | Use |
| --- | --- | --- |
| [test_agent](test_agent.md) | API | Runtime gate, startup overrides and fixture access |
| [test_runner](test_runner.md) | API | Stages, gated groups, bounded readiness and exact-object actions |
| [test_gated_stage](test_gated_stage.md) | Self-test | Self-tests for explicit and inherited gated skips |
| [test_session_gate](test_session_gate.md) | API | Test-account gate, switching, restoration and explicit production legs |
| [test_log](test_log.md) | API | Evidence logs, assertions and telemetry formatting |
| [test_log_lines](test_log_lines.md) | Self-test | Self-test for one physical record per log call |
| [test_text_reads](test_text_reads.md) | API | Space-normalized text assertions |
| [test_lang_pack](test_lang_pack.md) | API | Temporary language overrides and restoration |
| [test_probe](test_probe.md) | API | Windowed observations and correlated requests |
| [test_expiry_window](test_expiry_window.md) | API | Served-expiry observations bound to a live owner, generation and link; frozen cutoff windows and boundary polls with named refusals |
| [test_widgets](test_widgets.md) | API | Typed discovery, a button's own disabled state, live publications and input events |
| [test_wheel](test_wheel.md) | Self-test | Self-test for wheel delivery |
| [test_queued_drag](test_queued_drag.md) | API | Deliver queued drag starts before the synthetic release |
| [test_text_drop](test_text_drop.md) | Self-test | Self-test for text-drop routing |
| [test_clipboard](test_clipboard.md) | API | Clipboard round-trip and classified read-back |
| [test_console_lock](test_console_lock.md) | API | Current console-lock classification |
| [test_capture](test_capture.md) | API | Prepared grabs, paint owners, mapped targets with typed coordinate owners, strict comparisons, checked full-owner crops and saved-image markers |
| [test_mapped_target](test_mapped_target.md) | Self-test | Self-test for full mapped-target visibility and typed coordinate owners |
| [test_post_paint](test_post_paint.md) | API | Samples after real product paints, shared capture scopes and paint receipts |
| [test_full_owner_capture](test_full_owner_capture.md) | Self-test | Self-test for coordinated samplers, strict renders, checked full-owner crops and panel sizing |
| [test_animation_clock](test_animation_clock.md) | API | Deterministic elapsed-time animation frames |
| [test_paint_stream](test_paint_stream.md) | API | Whole-owner QPicture streams and manifest for optimized replay timing |
| [test_corner_patch](test_corner_patch.md) | API | Rounded-corner comparisons and controls |
| [test_marking_read](test_marking_read.md) | API | Prevent primary-window read marking |
| [test_window_exposure](test_window_exposure.md) | API | Keep covered windows painting without activation |
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
| [test_menu](test_menu.md) | API | Same-turn menu readings, actions and captures |
| [test_chat_menu](test_chat_menu.md) | API | Attribute chat-list context menus to the exact message |
| [test_messages](test_messages.md) | API | Observe matching sent server messages |
| [test_history_fixtures](test_history_fixtures.md) | API | Lifetime-owned history service-message fixtures |
| [test_notify_override](test_notify_override.md) | API | Local notify-setting override and restoration |
| [test_custom_emoji](test_custom_emoji.md) | API | Explicitly registered ready emoji fixture |
| [test_launch_fuse](test_launch_fuse.md) | API | Assert intended OS launches while blocking them; report a credential-bearing one by label |
| [test_open_handoff](test_open_handoff.md) | API | Observe document-open handoff without OS launch |
| [test_transfer](test_transfer.md) | API | Observe document transfer success/failure |
| [test_web_cache](test_web_cache.md) | API | Clear the exact web-document cache entry |
| [test_rpc_retry](test_rpc_retry.md) | API | Observe server-error resends |
| [test_rpc_fixture](test_rpc_fixture.md) | API | Boxed controlled RPC replies |
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
