/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_cloud_themes.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_preview.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/window_controller.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "main/main_session.h"
#include "boxes/confirm_box.h"
#include "media/view/media_view_open_common.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kFirstReloadTimeout = 10 * crl::time(1000);
constexpr auto kReloadTimeout = 3600 * crl::time(1000);

} // namespace

CloudTheme CloudTheme::Parse(
		not_null<Main::Session*> session,
		const MTPDtheme &data,
		bool parseSettings) {
	const auto document = data.vdocument();
	const auto paper = [&]() -> std::optional<WallPaper> {
		if (const auto settings = data.vsettings()) {
			return settings->match([&](const MTPDthemeSettings &data) {
				return data.vwallpaper()
					? WallPaper::Create(session, *data.vwallpaper())
					: std::nullopt;
			});
		}
		return {};
	};
	const auto outgoingMessagesColors = [&] {
		auto result = std::vector<QColor>();
		if (const auto settings = data.vsettings()) {
			settings->match([&](const MTPDthemeSettings &data) {
				if (const auto colors = data.vmessage_colors()) {
					for (const auto color : colors->v) {
						result.push_back(ColorFromSerialized(color));
					}
				}
			});
		}
		return result;
	};
	const auto accentColor = [&]() -> std::optional<QColor> {
		if (const auto settings = data.vsettings()) {
			return settings->match([&](const MTPDthemeSettings &data) {
				return ColorFromSerialized(data.vaccent_color().v);
			});
		}
		return {};
	};
	const auto basedOnDark = [&] {
		if (const auto settings = data.vsettings()) {
			return settings->match([&](const MTPDthemeSettings &data) {
				return data.vbase_theme().match([](
						const MTPDbaseThemeNight &) {
					return true;
				}, [](const MTPDbaseThemeTinted &) {
					return true;
				}, [](const auto &) {
					return false;
				});
			});
		}
		return false;
	};
	return {
		.id = data.vid().v,
		.accessHash = data.vaccess_hash().v,
		.slug = qs(data.vslug()),
		.title = qs(data.vtitle()),
		.documentId = (document
			? session->data().processDocument(*document)->id
			: DocumentId(0)),
		.createdBy = data.is_creator() ? session->userId() : UserId(0),
		.usersCount = data.vinstalls_count().value_or_empty(),
		.paper = parseSettings ? paper() : std::nullopt,
		.accentColor = parseSettings ? accentColor() : std::nullopt,
		.outgoingMessagesColors = (parseSettings
			? outgoingMessagesColors()
			: std::vector<QColor>()),
		.basedOnDark = parseSettings && basedOnDark(),
	};
}

CloudTheme CloudTheme::Parse(
		not_null<Main::Session*> session,
		const MTPTheme &data,
		bool parseSettings) {
	return data.match([&](const MTPDtheme &data) {
		return CloudTheme::Parse(session, data, parseSettings);
	});
}

QString CloudThemes::Format() {
	static const auto kResult = QString::fromLatin1("tdesktop");
	return kResult;
}

CloudThemes::CloudThemes(not_null<Main::Session*> session)
: _session(session)
, _reloadCurrentTimer([=] { reloadCurrent(); }) {
	setupReload();
}

void CloudThemes::setupReload() {
	using namespace Window::Theme;

	if (needReload()) {
		_reloadCurrentTimer.callOnce(kFirstReloadTimeout);
	}
	Background()->updates(
	) | rpl::filter([](const BackgroundUpdate &update) {
		return (update.type == BackgroundUpdate::Type::ApplyingTheme);
	}) | rpl::map([=] {
		return needReload();
	}) | rpl::start_with_next([=](bool need) {
		install();
		if (need) {
			scheduleReload();
		} else {
			_reloadCurrentTimer.cancel();
		}
	}, _lifetime);
}

bool CloudThemes::needReload() const {
	const auto &fields = Window::Theme::Background()->themeObject().cloud;
	return fields.id && fields.documentId;
}

void CloudThemes::install() {
	using namespace Window::Theme;

	const auto &fields = Background()->themeObject().cloud;
	auto &themeId = IsNightMode()
		? _installedNightThemeId
		: _installedDayThemeId;
	const auto cloudId = fields.documentId ? fields.id : uint64(0);
	if (themeId == cloudId) {
		return;
	}
	themeId = cloudId;
	using Flag = MTPaccount_InstallTheme::Flag;
	const auto flags = (IsNightMode() ? Flag::f_dark : Flag(0))
		| Flag::f_format
		| (themeId ? Flag::f_theme : Flag(0));
	_session->api().request(MTPaccount_InstallTheme(
		MTP_flags(flags),
		MTP_string(Format()),
		MTP_inputTheme(MTP_long(cloudId), MTP_long(fields.accessHash))
	)).send();
}

void CloudThemes::reloadCurrent() {
	if (!needReload()) {
		return;
	}
	const auto &fields = Window::Theme::Background()->themeObject().cloud;
	_session->api().request(MTPaccount_GetTheme(
		MTP_string(Format()),
		MTP_inputTheme(MTP_long(fields.id), MTP_long(fields.accessHash)),
		MTP_long(fields.documentId)
	)).done([=](const MTPTheme &result) {
		applyUpdate(result);
	}).fail([=](const MTP::Error &error) {
		_reloadCurrentTimer.callOnce(kReloadTimeout);
	}).send();
}

void CloudThemes::applyUpdate(const MTPTheme &theme) {
	theme.match([&](const MTPDtheme &data) {
		const auto cloud = CloudTheme::Parse(_session, data);
		const auto &object = Window::Theme::Background()->themeObject();
		if ((cloud.id != object.cloud.id)
			|| (cloud.documentId == object.cloud.documentId)
			|| !cloud.documentId) {
			return;
		}
		applyFromDocument(cloud);
	});
	scheduleReload();
}

void CloudThemes::resolve(
		not_null<Window::Controller*> controller,
		const QString &slug,
		const FullMsgId &clickFromMessageId) {
	_session->api().request(_resolveRequestId).cancel();
	_resolveRequestId = _session->api().request(MTPaccount_GetTheme(
		MTP_string(Format()),
		MTP_inputThemeSlug(MTP_string(slug)),
		MTP_long(0)
	)).done([=](const MTPTheme &result) {
		showPreview(controller, result);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == qstr("THEME_FORMAT_INVALID")) {
			controller->show(Box<InformBox>(
				tr::lng_theme_no_desktop(tr::now)));
		}
	}).send();
}

void CloudThemes::showPreview(
		not_null<Window::Controller*> controller,
		const MTPTheme &data) {
	data.match([&](const MTPDtheme &data) {
		showPreview(controller, CloudTheme::Parse(_session, data));
	});
}

void CloudThemes::showPreview(
		not_null<Window::Controller*> controller,
		const CloudTheme &cloud) {
	if (const auto documentId = cloud.documentId) {
		previewFromDocument(controller, cloud);
	} else if (cloud.createdBy == _session->userId()) {
		controller->show(Box(
			Window::Theme::CreateForExistingBox,
			controller,
			cloud));
	} else {
		controller->show(Box<InformBox>(
			tr::lng_theme_no_desktop(tr::now)));
	}
}

void CloudThemes::applyFromDocument(const CloudTheme &cloud) {
	const auto document = _session->data().document(cloud.documentId);
	loadDocumentAndInvoke(_updatingFrom, cloud, document, [=](
			std::shared_ptr<Data::DocumentMedia> media) {
		const auto document = media->owner();
		auto preview = Window::Theme::PreviewFromFile(
			media->bytes(),
			document->location().name(),
			cloud);
		if (preview) {
			Window::Theme::Apply(std::move(preview));
			Window::Theme::KeepApplied();
		}
	});
}

void CloudThemes::previewFromDocument(
		not_null<Window::Controller*> controller,
		const CloudTheme &cloud) {
	const auto sessionController = controller->sessionController();
	if (!sessionController) {
		return;
	}
	const auto document = _session->data().document(cloud.documentId);
	loadDocumentAndInvoke(_previewFrom, cloud, document, [=](
			std::shared_ptr<Data::DocumentMedia> media) {
		const auto document = media->owner();
		using Open = Media::View::OpenRequest;
		controller->openInMediaView(Open(sessionController, document, cloud));
	});
}

void CloudThemes::loadDocumentAndInvoke(
		LoadingDocument &value,
		const CloudTheme &cloud,
		not_null<DocumentData*> document,
		Fn<void(std::shared_ptr<Data::DocumentMedia>)> callback) {
	const auto alreadyWaiting = (value.document != nullptr);
	if (alreadyWaiting) {
		value.document->cancel();
	}
	value.document = document;
	value.documentMedia = document->createMediaView();
	value.document->save(
		Data::FileOriginTheme(cloud.id, cloud.accessHash),
		QString());
	value.callback = std::move(callback);
	if (value.documentMedia->loaded()) {
		invokeForLoaded(value);
		return;
	}
	if (!alreadyWaiting) {
		_session->downloaderTaskFinished(
		) | rpl::filter([=, &value] {
			return value.documentMedia->loaded();
		}) | rpl::start_with_next([=, &value] {
			invokeForLoaded(value);
		}, value.subscription);
	}
}

void CloudThemes::invokeForLoaded(LoadingDocument &value) {
	const auto onstack = std::move(value.callback);
	auto media = std::move(value.documentMedia);
	value = LoadingDocument();
	onstack(std::move(media));
}

void CloudThemes::scheduleReload() {
	if (needReload()) {
		_reloadCurrentTimer.callOnce(kReloadTimeout);
	} else {
		_reloadCurrentTimer.cancel();
	}
}

void CloudThemes::refresh() {
	if (_refreshRequestId) {
		return;
	}
	_refreshRequestId = _session->api().request(MTPaccount_GetThemes(
		MTP_string(Format()),
		MTP_int(_hash)
	)).done([=](const MTPaccount_Themes &result) {
		_refreshRequestId = 0;
		result.match([&](const MTPDaccount_themes &data) {
			_hash = data.vhash().v;
			parseThemes(data.vthemes().v);
			_updates.fire({});
		}, [](const MTPDaccount_themesNotModified &) {
		});
	}).fail([=](const MTP::Error &error) {
		_refreshRequestId = 0;
	}).send();
}

void CloudThemes::parseThemes(const QVector<MTPTheme> &list) {
	_list.clear();
	_list.reserve(list.size());
	for (const auto &theme : list) {
		_list.push_back(CloudTheme::Parse(_session, theme));
	}
	checkCurrentTheme();
}

void CloudThemes::refreshChatThemes() {
	if (_chatThemesRequestId) {
		return;
	}
	_chatThemesRequestId = _session->api().request(MTPaccount_GetChatThemes(
		MTP_int(_chatThemesHash)
	)).done([=](const MTPaccount_ChatThemes &result) {
		_chatThemesRequestId = 0;
		result.match([&](const MTPDaccount_chatThemes &data) {
			_hash = data.vhash().v;
			parseChatThemes(data.vthemes().v);
			_chatThemesUpdates.fire({});
		}, [](const MTPDaccount_chatThemesNotModified &) {
		});
	}).fail([=](const MTP::Error &error) {
		_chatThemesRequestId = 0;
	}).send();
}

const std::vector<ChatTheme> &CloudThemes::chatThemes() const {
	return _chatThemes;
}

rpl::producer<> CloudThemes::chatThemesUpdated() const {
	return _chatThemesUpdates.events();
}

std::optional<ChatTheme> CloudThemes::themeForEmoji(
		const QString &emoji) const {
	if (emoji.isEmpty()) {
		return {};
	}
	const auto i = ranges::find(_chatThemes, emoji, &ChatTheme::emoji);
	return (i != end(_chatThemes)) ? std::make_optional(*i) : std::nullopt;
}

rpl::producer<std::optional<ChatTheme>> CloudThemes::themeForEmojiValue(
		const QString &emoji) {
	if (emoji.isEmpty()) {
		return rpl::single<std::optional<ChatTheme>>(std::nullopt);
	} else if (auto result = themeForEmoji(emoji)) {
		return rpl::single(std::move(result));
	}
	refreshChatThemes();
	return rpl::single<std::optional<ChatTheme>>(
		std::nullopt
	) | rpl::then(chatThemesUpdated(
	) | rpl::map([=] {
		return themeForEmoji(emoji);
	}) | rpl::filter([](const std::optional<ChatTheme> &theme) {
		return theme.has_value();
	}) | rpl::take(1));
}

void CloudThemes::parseChatThemes(const QVector<MTPChatTheme> &list) {
	_chatThemes.clear();
	_chatThemes.reserve(list.size());
	for (const auto &theme : list) {
		theme.match([&](const MTPDchatTheme &data) {
			_chatThemes.push_back({
				.emoji = qs(data.vemoticon()),
				.light = CloudTheme::Parse(_session, data.vtheme(), true),
				.dark = CloudTheme::Parse(_session, data.vdark_theme(), true),
			});
		});
	}
}

void CloudThemes::checkCurrentTheme() {
	const auto &object = Window::Theme::Background()->themeObject();
	if (!object.cloud.id || !object.cloud.documentId) {
		return;
	}
	const auto i = ranges::find(_list, object.cloud.id, &CloudTheme::id);
	if (i == end(_list)) {
		install();
	}
}

rpl::producer<> CloudThemes::updated() const {
	return _updates.events();
}

const std::vector<CloudTheme> &CloudThemes::list() const {
	return _list;
}

void CloudThemes::savedFromEditor(const CloudTheme &theme) {
	const auto i = ranges::find(_list, theme.id, &CloudTheme::id);
	if (i != end(_list)) {
		*i = theme;
		_updates.fire({});
	} else {
		_list.insert(begin(_list), theme);
		_updates.fire({});
	}
}

void CloudThemes::remove(uint64 cloudThemeId) {
	const auto i = ranges::find(_list, cloudThemeId, &CloudTheme::id);
	if (i == end(_list)) {
		return;
	}
	_session->api().request(MTPaccount_SaveTheme(
		MTP_inputTheme(
			MTP_long(i->id),
			MTP_long(i->accessHash)),
		MTP_bool(true)
	)).send();
	_list.erase(i);
	_updates.fire({});
}

} // namespace Data
