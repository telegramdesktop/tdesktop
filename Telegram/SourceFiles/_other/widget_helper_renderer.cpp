/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "test/test_widget_helper_tap.h"
#include "tgw/tgw_hosted_glue.h"
#include "twidget/remote_hosted.h"
#include "twidget/remote_stream.h"

#include <memory>
#include <utility>

namespace WidgetHelper {

std::string Serve(
		twidget::SandboxRemoteStream &stream,
		const std::string &build,
		const std::function<std::string()> &lockdown) {
	Tgw::PrepareHostedRenderers();
	return twidget::SandboxRemoteEngineEnd::serveHosting(
		stream,
		build,
		lockdown,
		[](
				std::shared_ptr<twidget::SandboxResources>,
				const std::string &scheme,
				double width,
				std::shared_ptr<twidget::SandboxHostedPlatform> platform) {
			return Test::TapHelperRenderer(
				std::move(platform),
				scheme,
				[&](std::shared_ptr<twidget::SandboxHostedPlatform> tapped) {
					return Tgw::MakeHostedRenderer(
						scheme,
						width,
						std::move(tapped));
				});
		});
}

} // namespace WidgetHelper
