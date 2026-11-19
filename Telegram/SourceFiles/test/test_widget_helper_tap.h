/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "twidget/renderer.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

// The helper half of the validated-ops tap for widgets hosted in the widget
// helper (the whole widget there, tgw/tgw_hosted_glue.cpp, entered from
// _other/widget_helper_renderer.cpp). Qt-free: TelegramWidgetHelper's Debug
// build compiles it where the helper hosts renderers; its Release build
// takes the identity below. The client half lives in test_widget_ops.h,
// which reads the work records this half writes; their format is in
// test_widget_renderer_forward.h, which both targets compile.
// Reference: docs/helpers/test_widget_helper_tap.md.

namespace twidget {
class SandboxHostedPlatform;
} // namespace twidget

namespace Test {

using HelperRendererBuild = std::function<std::unique_ptr<twidget::Renderer>(
	std::shared_ptr<twidget::SandboxHostedPlatform> platform)>;

#ifdef _DEBUG

// The renderer the helper's hosted factory returns for one widget: in a
// Release build build(platform) itself. In Debug |build| gets a platform
// that forwards send() and request() to |platform| unchanged and times
// each request() (the waits for Telegram's answers), the renderer it
// returns is wrapped in a ForwardingRenderer (test_widget_renderer_forward.h)
// and |platform|'s onMessage is chained to the one |build| set: an exact
// Tgw::HostedWire Mirror or MirrorWork control is swallowed and arms the
// tap, every other message reaches the chained handler (through weak
// lifetimes, so a control after the wrapper is gone does nothing). Armed,
// the tap first sends one Mirrored record of way 'b' with |scheme|, the
// factory's colour scheme, then, when a setTheme() changed it before the
// arming, one of way 't' with the current scheme; from then on, through
// |platform|'s send(), way 'h' for every served-host -> renderer message but
// `ops` (whose validated batches reach Telegram through the host's
// onRenderOps alone), way 'r' for every renderer -> host message and way 't'
// for every setTheme(), each before it is forwarded. Armed by MirrorWork it
// also sends one way 'w' HelperWorkRecord after each outermost renderer
// call and each outermost platform message it chained (a call made inside
// another is part of it), right after the call returned, so the record's
// own send is outside every interval. A record past the platform limit is
// skipped. Every call runs on the served host's glue thread, the thread of
// all its renderer calls, of the platform's onMessage and of every
// request() the renderer makes.
[[nodiscard]] std::unique_ptr<twidget::Renderer> TapHelperRenderer(
	std::shared_ptr<twidget::SandboxHostedPlatform> platform,
	const std::string &scheme,
	const HelperRendererBuild &build);

#else // _DEBUG

[[nodiscard]] inline std::unique_ptr<twidget::Renderer> TapHelperRenderer(
		std::shared_ptr<twidget::SandboxHostedPlatform> platform,
		const std::string &,
		const HelperRendererBuild &build) {
	return build(std::move(platform));
}

#endif // _DEBUG

} // namespace Test
