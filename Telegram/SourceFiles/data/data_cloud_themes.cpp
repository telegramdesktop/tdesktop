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
#include "main/main_session.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "mainwindow.h"

namespace Data {
namespace {

constexpr auto kFirstReloadTimeout = 60 * crl::time(1000);
constexpr auto kReloadTimeout = 3600 * crl::time(1000);

} // namespace

CloudTheme CloudTheme::Parse(
		not_null<Main::Session*> session,
		const MTPDtheme &data) {
	const auto document = data.vdocument();
	return {
		data.vid().v,
		data.vaccess_hash().v,
		qs(data.vslug()),
		qs(data.vtitle()),
		(document
			? session->data().processDocument(*document)->id
			: DocumentId(0)),
		data.is_creator() ? session->userId() : UserId(0)
	};
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
	base::ObservableViewer(
		*Background()
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
	}).fail([=](const RPCError &error) {
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
		updateFromDocument(
			cloud,
			_session->data().document(cloud.documentId));
	}, [&](const MTPDthemeDocumentNotModified &data) {
	});
	scheduleReload();
}

void CloudThemes::resolve(
		const QString &slug,
		const FullMsgId &clickFromMessageId) {
	_session->api().request(_resolveRequestId).cancel();
	_resolveRequestId = _session->api().request(MTPaccount_GetTheme(
		MTP_string(Format()),
		MTP_inputThemeSlug(MTP_string(slug)),
		MTP_long(0)
	)).done([=](const MTPTheme &result) {
		result.match([&](const MTPDtheme &data) {
			const auto cloud = CloudTheme::Parse(_session, data);
			if (cloud.documentId) {
				const auto document = _session->data().document(
					cloud.documentId);
				DocumentOpenClickHandler::Open(
					Data::FileOrigin(),
					document,
					_session->data().message(clickFromMessageId));
			} else if (cloud.createdBy == _session->userId()) {
				Ui::show(Box(
					Window::Theme::CreateForExistingBox,
					&App::wnd()->controller(),
					cloud));
			} else {
				Ui::show(Box<InformBox>(
					tr::lng_theme_no_desktop(tr::now)));
			}
		}, [&](const MTPDthemeDocumentNotModified &data) {
		});
	}).fail([=](const RPCError &error) {
		if (error.type() == qstr("THEME_FORMAT_INVALID")) {
			Ui::show(Box<InformBox>(
				tr::lng_theme_no_desktop(tr::now)));
		}
	}).send();
}

void CloudThemes::updateFromDocument(
		const CloudTheme &cloud,
		not_null<DocumentData*> document) {
	if (_updatingFrom) {
		_updatingFrom->cancel();
	} else {
		base::ObservableViewer(
			_session->downloaderTaskFinished()
		) | rpl::filter([=] {
			return _updatingFrom->loaded();
		}) | rpl::start_with_next([=] {
			_updatingFromLifetime.destroy();
			auto preview = Window::Theme::PreviewFromFile(
				document->data(),
				document->location().name(),
				cloud);
			if (preview) {
				Window::Theme::Apply(std::move(preview));
			}
		}, _updatingFromLifetime);
	}

	_updatingFrom = document;
	_updatingFrom->save(Data::FileOrigin(), QString()); // #TODO themes
}

void CloudThemes::scheduleReload() {
	if (needReload()) {
		_reloadCurrentTimer.callOnce(kReloadTimeout);
	} else {
		_reloadCurrentTimer.cancel();
	}
}

void CloudThemes::refresh() {
	if (_refreshRquestId) {
		return;
	}
	_refreshRquestId = _session->api().request(MTPaccount_GetThemes(
		MTP_string(Format()),
		MTP_int(_hash)
	)).done([=](const MTPaccount_Themes &result) {
		_refreshRquestId = 0;
		result.match([&](const MTPDaccount_themes &data) {
			_hash = data.vhash().v;
			parseThemes(data.vthemes().v);
			_updates.fire({});
		}, [](const MTPDaccount_themesNotModified &) {
		});
	}).fail([=](const RPCError &error) {
		_refreshRquestId = 0;
	}).send();
}

void CloudThemes::parseThemes(const QVector<MTPTheme> &list) {
	_list.clear();
	_list.reserve(list.size());
	for (const auto &theme : list) {
		theme.match([&](const MTPDtheme &data) {
			_list.push_back(CloudTheme::Parse(_session, data));
		}, [&](const MTPDthemeDocumentNotModified &data) {
			LOG(("API Error: Unexpected themeDocumentNotModified."));
		});
	}
}

rpl::producer<> CloudThemes::updated() const {
	return _updates.events();
}

const std::vector<CloudTheme> &CloudThemes::list() const {
	return _list;
}

void CloudThemes::apply(const CloudTheme &theme) {
	const auto i = ranges::find(_list, theme.id, &CloudTheme::id);
	if (i != end(_list)) {
		*i = theme;
		_updates.fire({});
	}
}

} // namespace Data
