/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/bytes.h"
#include "mtproto/mtproto_proxy_data.h"
#include "webview/webview_interface.h"

namespace tr {
template <typename ...Tags>
struct phrase;
} // namespace tr

namespace Ui {

[[nodiscard]] QByteArray ComputeStyles(
	const base::flat_map<QByteArray, const style::color*> &colors,
	const base::flat_map<QByteArray, tr::phrase<>> &phrases,
	int zoom,
	bool nightTheme = false);
[[nodiscard]] QByteArray ComputeSemiTransparentOverStyle(
	const QByteArray &name,
	const style::color &over,
	const style::color &bg);

[[nodiscard]] QByteArray EscapeForAttribute(QByteArray value);
[[nodiscard]] QByteArray EscapeForScriptString(QByteArray value);

} // namespace Ui
