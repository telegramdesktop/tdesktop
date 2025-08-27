/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_app_config_values.h"

#include "main/main_app_config.h"
#include "main/main_session.h"

namespace AppConfig {

std::optional<QString> FragmentLink(not_null<Main::Session*> session) {
	using Strings = std::vector<QString>;
	const auto domains = session->appConfig().get<Strings>(
		u"whitelisted_domains"_q,
		std::vector<QString>());
	const auto proj = [&, domain = u"fragment"_q](const QString &p) {
		return p.contains(domain);
	};
	const auto it = ranges::find_if(domains, proj);
	return (it == end(domains))
		? std::nullopt
		: std::make_optional<QString>(*it);
}

} // namespace AppConfig
