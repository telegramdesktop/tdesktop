/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "twidget/remote_stream.h"

namespace WidgetHelper {

std::string Serve(
		twidget::SandboxRemoteStream &stream,
		const std::string &build,
		const std::function<std::string()> &lockdown) {
	return twidget::SandboxRemoteEngineEnd::serve(stream, build, lockdown);
}

} // namespace WidgetHelper
