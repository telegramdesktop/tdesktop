// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_url_handlers.h"

#include "lang_auto.h"
#include "mainwindow.h"
#include "ayu/ui/settings/settings_main.h"
#include "ayu/utils/official_resources.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/qthelp_url.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_controller.h"

#include <QDesktopServices>

namespace AyuUrlHandlers {

bool ResolveUser(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto userId = params.value(qsl("id")).toLongLong();
	if (!userId) {
		return false;
	}
	const auto peer = controller->session().data().peerLoaded(static_cast<PeerId>(userId));
	if (peer != nullptr) {
		controller->showPeerInfo(peer);
		return true;
	}

	searchUserById(
		userId,
		&controller->session(),
		[=](const QString &title, PeerData *data)
		{
			if (data) {
				controller->showPeerInfo(data);
				return;
			}

			Core::App().hideMediaView();
			Ui::show(Ui::MakeInformBox(tr::ayu_UserNotFoundMessage()));
		}
	);

	return true;
}

bool ResolveChat(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto chatId = params.value(qsl("id")).toLongLong();
	if (!chatId) {
		return false;
	}
	const auto peer = controller->session().data().peerLoaded(static_cast<PeerId>(chatId));
	if (peer != nullptr) {
		controller->showPeerHistory(peer);
		return true;
	}

	searchChatById(
		chatId,
		&controller->session(),
		[=](const QString &title, PeerData *data)
		{
			if (data) {
				controller->showPeerHistory(data);
				return;
			}

			Core::App().hideMediaView();
			Ui::show(Ui::MakeInformBox(tr::ayu_UserNotFoundMessage()));
		}
	);

	return true;
}

bool HandleAyu(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context) {
	if (!controller) {
		return false;
	}

	try {
		const auto section = match->captured(1).mid(1).toLower();
		const auto type = [&]() -> std::optional<::Settings::Type>
		{
			if (section == u"settings"_q || section == u"preferences"_q || section == u"prefs"_q) {
				return ::Settings::AyuMain::Id();
			}
			return std::nullopt;
		}();

		if (type.has_value()) {
			controller->showSettings(*type);
			controller->window().activate();
		} else {
			controller->showToast(QString(":3"), 500);
		}
	} catch (...) {
	}

	return true;
}

bool HandleSupport(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context) {
	if (!controller) {
		return false;
	}
	QDesktopServices::openUrl(QUrl(
		QString::fromLatin1(TeleForge::OfficialResources::kEntries.back().url)));
	return true;
}

struct ResolvedSetting {
	QString controlId;
	::Settings::Type section = ::Settings::AyuMain::Id();
};

[[nodiscard]] ResolvedSetting ResolveSetting(
		const QString &controlId,
		not_null<::Main::Session*> session) {
	const auto &registry = ::Settings::Builder::SearchRegistry::Instance();
	const auto entries = registry.collectAll(session);
	for (const auto &entry : entries) {
		if (!entry.section) {
			continue;
		}
		if (entry.id == controlId) {
			return {
				.controlId = entry.id,
				.section = entry.section,
			};
		}
		if (entry.altIds.contains(controlId)) {
			return {
				.controlId = entry.id,
				.section = entry.section,
			};
		}
	}
	return { .controlId = controlId };
}

bool HandleAyuSettings(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context) {
	if (!controller) {
		return false;
	}

	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto settingName = params.value(u"s"_q);

	if (settingName.isEmpty()) {
		controller->showSettings(::Settings::AyuMain::Id());
	} else {
		const auto resolved = ResolveSetting(
			u"ayu/"_q + settingName,
			&controller->session());
		controller->window().setHighlightControlId(resolved.controlId);
		controller->showSettings(resolved.section);
	}
	controller->window().activate();
	return true;
}

bool TryHandleSpotify(const QString &url) {
	if (!url.contains("spotify.com")) {
		return false;
	}

	// docs on their url scheme
	// https://www.iana.org/assignments/uri-schemes/prov/spotify

	using namespace qthelp;
	constexpr auto matchOptions = RegExOption::CaseInsensitive;
	// https://regex101.com/r/l4Ogzf/2
	const auto match = regex_match(
		u"^(https?:\\/\\/)?([a-zA-Z0-9_]+)\\.spotify\\.com\\/(?<type>track|album|artist|user|playlist)\\/(?<identifier>[a-zA-Z0-9_\\/]+?)((\\?si=.+)?)$"_q,
		url,
		matchOptions);
	if (match) {
		const auto type = match->captured("type").toLower();
		const auto identifier = match->captured("identifier").replace("/", ":");

		// '/' -> ':' for links like:
		// https://open.spotify.com/user/1185903410/playlist/6YAnJeVC7tgOiocOG23Dd
		// so it'll look like
		// spotify:user:1185903410:playlist:6YAnJeVC7tgOiocOG23Dd

		const auto res = QString("spotify:%1:%2").arg(type).arg(identifier);

		return QDesktopServices::openUrl(QUrl(res));
	}

	return false;
}

}
