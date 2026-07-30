/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/ton_explorer_url.h"

#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Core {
namespace {

constexpr auto kDefaultExplorerBase = "https://tonviewer.com"_cs;

} // namespace

QString TonExplorerUrl(const QString &base, const QString &route) {
	auto trimmed = base;
	while (trimmed.endsWith(u'/')) {
		trimmed.chop(1);
	}
	if (trimmed.isEmpty()) {
		trimmed = kDefaultExplorerBase.utf16();
	}
	auto path = route;
	while (path.startsWith(u'/')) {
		path = path.mid(1);
	}
	return trimmed + u'/' + path;
}

QString TonExplorerUrl(
		not_null<Main::Session*> session,
		const QString &route) {
	return TonExplorerUrl(
		session->appConfig().get<QString>(
			u"ton_blockchain_explorer_url"_q,
			QString()),
		route);
}

} // namespace Core
