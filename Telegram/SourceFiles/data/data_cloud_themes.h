/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct CloudTheme {
	uint64 id = 0;
	uint64 accessHash = 0;
	QString slug;
	QString title;
	DocumentId documentId = 0;
	UserId createdBy = 0;

	static CloudTheme Parse(
		not_null<Main::Session*> session,
		const MTPDtheme &data);
};

class CloudThemes final {
public:
	explicit CloudThemes(not_null<Main::Session*> session);

	[[nodiscard]] static QString Format();

	void refresh();
	[[nodiscard]] rpl::producer<> updated() const;
	[[nodiscard]] const std::vector<CloudTheme> &list() const;
	void apply(const CloudTheme &data);

	void applyUpdate(const MTPTheme &theme);

private:
	void parseThemes(const QVector<MTPTheme> &list);

	void install();
	void setupReload();
	[[nodiscard]] bool needReload() const;
	void scheduleReload();
	void reloadCurrent();
	void updateFromDocument(
		const CloudTheme &cloud,
		not_null<DocumentData*> document);

	const not_null<Main::Session*> _session;
	int32 _hash = 0;
	mtpRequestId _requestId = 0;
	std::vector<CloudTheme> _list;
	rpl::event_stream<> _updates;

	base::Timer _reloadCurrentTimer;
	DocumentData *_updatingFrom = nullptr;
	rpl::lifetime _updatingFromLifetime;
	uint64 _installedDayThemeId = 0;
	uint64 _installedNightThemeId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Data
