/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_cloud_themes.h"

#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Data {

QString CloudThemes::Format() {
	static const auto kResult = QString::fromLatin1("tdesktop");
	return kResult;
}

CloudThemes::CloudThemes(not_null<Main::Session*> session)
: _session(session) {
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
			const auto document = data.vdocument();
			_list.push_back({
				data.vid().v,
				data.vaccess_hash().v,
				qs(data.vslug()),
				qs(data.vtitle()),
				(document
					? _session->data().processDocument(*document).get()
					: nullptr),
				data.is_creator()
			});
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

} // namespace Data
