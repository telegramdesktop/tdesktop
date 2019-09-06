/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_cloud_themes.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_preview.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "main/main_session.h"
#include "apiwrap.h"

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
			|| (cloud.documentId == object.cloud.documentId)) {
			return;
		}
		if (const auto updated = data.vdocument()) {
			updateFromDocument(
				cloud,
				_session->data().processDocument(*updated));
		}
	}, [&](const MTPDthemeDocumentNotModified &data) {
	});
	scheduleReload();
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
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPaccount_GetThemes(
		MTP_string(Format()),
		MTP_int(_hash)
	)).done([=](const MTPaccount_Themes &result) {
		result.match([&](const MTPDaccount_themes &data) {
			_hash = data.vhash().v;
			parseThemes(data.vthemes().v);
			_updates.fire({});
		}, [](const MTPDaccount_themesNotModified &) {
		});
	}).fail([=](const RPCError &error) {
		_requestId = 0;
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
