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
	void savedFromEditor(const CloudTheme &data);
	void remove(uint64 cloudThemeId);

	void applyUpdate(const MTPTheme &theme);

	void resolve(const QString &slug, const FullMsgId &clickFromMessageId);
	void showPreview(const MTPTheme &data);
	void showPreview(const CloudTheme &cloud);

private:
	struct LoadingDocument {
		DocumentData *document = nullptr;
		rpl::lifetime subscription;
		Fn<void()> callback;
	};

	void parseThemes(const QVector<MTPTheme> &list);

	void install();
	void setupReload();
	[[nodiscard]] bool needReload() const;
	void scheduleReload();
	void reloadCurrent();
	void updateFromDocument(
		const CloudTheme &cloud,
		not_null<DocumentData*> document);
	void previewFromDocument(
		const CloudTheme &cloud,
		not_null<DocumentData*> document);
	void loadDocumentAndInvoke(
		LoadingDocument &value,
		not_null<DocumentData*> document,
		Fn<void()> callback);
	void invokeForLoaded(LoadingDocument &value);

	const not_null<Main::Session*> _session;
	int32 _hash = 0;
	mtpRequestId _refreshRquestId = 0;
	mtpRequestId _resolveRequestId = 0;
	std::vector<CloudTheme> _list;
	rpl::event_stream<> _updates;

	base::Timer _reloadCurrentTimer;
	LoadingDocument _updatingFrom;
	LoadingDocument _previewFrom;
	uint64 _installedDayThemeId = 0;
	uint64 _installedNightThemeId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Data
