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
#include "ui/boxes/confirm_box.h"
#include "media/view/media_view_open_common.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kFirstReloadTimeout = 10 * crl::time(1000);
constexpr auto kReloadTimeout = 3600 * crl::time(1000);

bool IsTestingColors/* = false*/;

} // namespace

CloudTheme CloudTheme::Parse(
		not_null<Main::Session*> session,
		const MTPDtheme &data,
		bool parseSettings) {
	const auto document = data.vdocument();
	const auto paper = [&](const MTPThemeSettings &settings) {
		return settings.match([&](const MTPDthemeSettings &data) {
			return data.vwallpaper()
				? WallPaper::Create(session, *data.vwallpaper())
				: std::nullopt;
		});
	};
	const auto outgoingMessagesColors = [&](
			const MTPThemeSettings &settings) {
		auto result = std::vector<QColor>();
		settings.match([&](const MTPDthemeSettings &data) {
			if (const auto colors = data.vmessage_colors()) {
				for (const auto &color : colors->v) {
					result.push_back(Ui::ColorFromSerialized(color));
				}
			}
		});
		return result;
	};
	const auto accentColor = [&](const MTPThemeSettings &settings) {
		return settings.match([&](const MTPDthemeSettings &data) {
			return Ui::ColorFromSerialized(data.vaccent_color());
		});
	};
	const auto outgoingAccentColor = [&](const MTPThemeSettings &settings) {
		return settings.match([&](const MTPDthemeSettings &data) {
			return Ui::MaybeColorFromSerialized(data.voutbox_accent_color());
		});
	};
	const auto basedOnDark = [&](const MTPThemeSettings &settings) {
		return settings.match([&](const MTPDthemeSettings &data) {
			return data.vbase_theme().match([](
					const MTPDbaseThemeNight &) {
				return true;
			}, [](const MTPDbaseThemeTinted &) {
				return true;
			}, [](const auto &) {
				return false;
			});
		});
	};
	const auto settings = [&] {
		auto result = base::flat_map<Type, Settings>();
		const auto settings = data.vsettings();
		if (!settings) {
			return result;
		}
		for (const auto &fields : settings->v) {
			const auto type = basedOnDark(fields) ? Type::Dark : Type::Light;
			result.emplace(type, Settings{
				.paper = paper(fields),
				.accentColor = accentColor(fields),
				.outgoingAccentColor = outgoingAccentColor(fields),
				.outgoingMessagesColors = outgoingMessagesColors(fields),
			});
		}
		return result;
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
		.emoticon = qs(data.vemoticon().value_or_empty()),
		.settings = (parseSettings
			? settings()
			: base::flat_map<Type, Settings>()),
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
		MTP_inputTheme(MTP_long(cloudId), MTP_long(fields.accessHash)),
		MTP_string(Format()),
		MTPBaseTheme()
	)).send();
}

void CloudThemes::reloadCurrent() {
	if (!needReload()) {
		return;
	}
	const auto &fields = Window::Theme::Background()->themeObject().cloud;
	_session->api().request(MTPaccount_GetTheme(
		MTP_string(Format()),
		MTP_inputTheme(MTP_long(fields.id), MTP_long(fields.accessHash))
	)).done([=](const MTPTheme &result) {
		applyUpdate(result);
	}).fail([=] {
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
		MTP_inputThemeSlug(MTP_string(slug))
	)).done([=](const MTPTheme &result) {
		showPreview(controller, result);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"THEME_FORMAT_INVALID"_q) {
			controller->show(Ui::MakeInformBox(tr::lng_theme_no_desktop()));
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
		controller->show(Ui::MakeInformBox(tr::lng_theme_no_desktop()));
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
		MTP_long(_hash)
	)).done([=](const MTPaccount_Themes &result) {
		_refreshRequestId = 0;
		result.match([&](const MTPDaccount_themes &data) {
			_hash = data.vhash().v;
			parseThemes(data.vthemes().v);
			_updates.fire({});
		}, [](const MTPDaccount_themesNotModified &) {
		});
	}).fail([=] {
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
		MTP_long(_chatThemesHash)
	)).done([=](const MTPaccount_Themes &result) {
		_chatThemesRequestId = 0;
		result.match([&](const MTPDaccount_themes &data) {
			_chatThemesHash = data.vhash().v;
			parseChatThemes(data.vthemes().v);
			_chatThemesUpdates.fire({});
		}, [](const MTPDaccount_themesNotModified &) {
		});
	}).fail([=] {
		_chatThemesRequestId = 0;
	}).send();
}

const std::vector<CloudTheme> &CloudThemes::chatThemes() const {
	return _chatThemes;
}

rpl::producer<> CloudThemes::chatThemesUpdated() const {
	return _chatThemesUpdates.events();
}

std::optional<CloudTheme> CloudThemes::themeForEmoji(
		const QString &emoticon) const {
	const auto emoji = Ui::Emoji::Find(emoticon);
	if (!emoji) {
		return {};
	}
	const auto i = ranges::find(_chatThemes, emoji, [](const CloudTheme &v) {
		return Ui::Emoji::Find(v.emoticon);
	});
	return (i != end(_chatThemes)) ? std::make_optional(*i) : std::nullopt;
}

rpl::producer<std::optional<CloudTheme>> CloudThemes::themeForEmojiValue(
		const QString &emoticon) {
	const auto testing = TestingColors();
	if (!Ui::Emoji::Find(emoticon)) {
		return rpl::single<std::optional<CloudTheme>>(std::nullopt);
	} else if (auto result = themeForEmoji(emoticon)) {
		if (testing) {
			return rpl::single(
				std::move(result)
			) | rpl::then(chatThemesUpdated(
			) | rpl::map([=] {
				return themeForEmoji(emoticon);
			}) | rpl::filter([](const std::optional<CloudTheme> &theme) {
				return theme.has_value();
			}));
		}
		return rpl::single(std::move(result));
	}
	refreshChatThemes();
	const auto limit = testing ? (1 << 20) : 1;
	return rpl::single<std::optional<CloudTheme>>(
		std::nullopt
	) | rpl::then(chatThemesUpdated(
	) | rpl::map([=] {
		return themeForEmoji(emoticon);
	}) | rpl::filter([](const std::optional<CloudTheme> &theme) {
		return theme.has_value();
	}) | rpl::take(limit));
}

bool CloudThemes::TestingColors() {
	return IsTestingColors;
}

void CloudThemes::SetTestingColors(bool testing) {
	IsTestingColors = testing;
}

QString CloudThemes::prepareTestingLink(const CloudTheme &theme) const {
	const auto hex = [](int value) {
		return QChar((value < 10) ? ('0' + value) : ('a' + (value - 10)));
	};
	const auto hex2 = [&](int value) {
		return QString() + hex(value / 16) + hex(value % 16);
	};
	const auto color = [&](const QColor &color) {
		return hex2(color.red()) + hex2(color.green()) + hex2(color.blue());
	};
	const auto colors = [&](const std::vector<QColor> &colors) {
		auto list = QStringList();
		for (const auto &c : colors) {
			list.push_back(color(c));
		}
		return list.join(",");
	};
	auto arguments = QStringList();
	for (const auto &[type, settings] : theme.settings) {
		const auto add = [&, type = type](const QString &value) {
			const auto prefix = (type == CloudTheme::Type::Dark)
				? u"dark_"_q
				: u""_q;
			arguments.push_back(prefix + value);
		};
		add("accent=" + color(settings.accentColor));
		if (settings.paper && !settings.paper->backgroundColors().empty()) {
			add("bg=" + colors(settings.paper->backgroundColors()));
		}
		if (settings.paper/* && settings.paper->hasShareUrl()*/) {
			add("intensity="
				+ QString::number(settings.paper->patternIntensity()));
			//const auto url = settings.paper->shareUrl(_session);
			//const auto from = url.indexOf("bg/");
			//const auto till = url.indexOf("?");
			//if (from > 0 && till > from) {
			//	add("slug=" + url.mid(from + 3, till - from - 3));
			//}
		}
		if (settings.outgoingAccentColor) {
			add("out_accent" + color(*settings.outgoingAccentColor));
		}
		if (!settings.outgoingMessagesColors.empty()) {
			add("out_bg=" + colors(settings.outgoingMessagesColors));
		}
	}
	return arguments.isEmpty()
		? QString()
		: ("tg://test_chat_theme?" + arguments.join("&"));
}

std::optional<CloudTheme> CloudThemes::updateThemeFromLink(
		const QString &emoticon,
		const QMap<QString, QString> &params) {
	const auto emoji = Ui::Emoji::Find(emoticon);
	if (!TestingColors() || !emoji) {
		return std::nullopt;
	}
	const auto i = ranges::find(_chatThemes, emoji, [](const CloudTheme &v) {
		return Ui::Emoji::Find(v.emoticon);
	});
	if (i == end(_chatThemes)) {
		return std::nullopt;
	}
	const auto hex = [](const QString &value) {
		return (value.size() != 1)
			? std::nullopt
			: (value[0] >= 'a' && value[0] <= 'f')
			? std::make_optional(10 + int(value[0].unicode() - 'a'))
			: (value[0] >= 'A' && value[0] <= 'F')
			? std::make_optional(10 + int(value[0].unicode() - 'A'))
			: (value[0] >= '0' && value[0] <= '9')
			? std::make_optional(int(value[0].unicode() - '0'))
			: std::nullopt;
	};
	const auto hex2 = [&](const QString &value) {
		const auto first = hex(value.mid(0, 1));
		const auto second = hex(value.mid(1, 1));
		return (first && second)
			? std::make_optional((*first) * 16 + (*second))
			: std::nullopt;
	};
	const auto color = [&](const QString &value) {
		const auto red = hex2(value.mid(0, 2));
		const auto green = hex2(value.mid(2, 2));
		const auto blue = hex2(value.mid(4, 2));
		return (red && green && blue)
			? std::make_optional(QColor(*red, *green, *blue))
			: std::nullopt;
	};
	const auto colors = [&](const QString &value) {
		auto list = value.split(",");
		auto result = std::vector<QColor>();
		for (const auto &single : list) {
			if (const auto c = color(single)) {
				result.push_back(*c);
			} else {
				return std::vector<QColor>();
			}
		}
		return (result.size() > 4) ? std::vector<QColor>() : result;
	};

	const auto parse = [&](CloudThemeType type, const QString &prefix = {}) {
		const auto accent = color(params["accent"]);
		if (!accent) {
			return;
		}
		auto &settings = i->settings[type];
		settings.accentColor = *accent;
		const auto bg = colors(params["bg"]);
		settings.paper = (settings.paper && !bg.empty())
			? std::make_optional(settings.paper->withBackgroundColors(bg))
			: settings.paper;
		settings.paper = (settings.paper && params["intensity"].toInt())
			? std::make_optional(
				settings.paper->withPatternIntensity(
					params["intensity"].toInt()))
			: settings.paper;
		settings.outgoingAccentColor = color(params["out_accent"]);
		settings.outgoingMessagesColors = colors(params["out_bg"]);
	};
	if (params.contains("dark_accent")) {
		parse(CloudThemeType::Dark, "dark_");
	}
	if (params.contains("accent")) {
		parse(params["dark"].isEmpty()
			? CloudThemeType::Light
			: CloudThemeType::Dark);
	}
	_chatThemesUpdates.fire({});
	return *i;
}

void CloudThemes::parseChatThemes(const QVector<MTPTheme> &list) {
	_chatThemes.clear();
	_chatThemes.reserve(list.size());
	for (const auto &theme : list) {
		_chatThemes.push_back(CloudTheme::Parse(_session, theme, true));
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
