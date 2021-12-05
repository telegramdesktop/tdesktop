/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_wall_paper.h"

class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class Controller;
} // namespace Window

namespace Data {

class DocumentMedia;

enum class CloudThemeType {
	Dark,
	Light,
};

struct CloudTheme {
	uint64 id = 0;
	uint64 accessHash = 0;
	QString slug;
	QString title;
	DocumentId documentId = 0;
	UserId createdBy = 0;
	int usersCount = 0;
	QString emoticon;

	using Type = CloudThemeType;
	struct Settings {
		std::optional<WallPaper> paper;
		QColor accentColor;
		std::optional<QColor> outgoingAccentColor;
		std::vector<QColor> outgoingMessagesColors;
	};
	base::flat_map<Type, Settings> settings;

	static CloudTheme Parse(
		not_null<Main::Session*> session,
		const MTPDtheme &data,
		bool parseSettings = false);
	static CloudTheme Parse(
		not_null<Main::Session*> session,
		const MTPTheme &data,
		bool parseSettings = false);
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

	void refreshChatThemes();
	[[nodiscard]] const std::vector<CloudTheme> &chatThemes() const;
	[[nodiscard]] rpl::producer<> chatThemesUpdated() const;
	[[nodiscard]] std::optional<CloudTheme> themeForEmoji(
		const QString &emoticon) const;
	[[nodiscard]] auto themeForEmojiValue(const QString &emoticon)
		-> rpl::producer<std::optional<CloudTheme>>;

	[[nodiscard]] static bool TestingColors();
	static void SetTestingColors(bool testing);
	[[nodiscard]] QString prepareTestingLink(const CloudTheme &theme) const;
	[[nodiscard]] std::optional<CloudTheme> updateThemeFromLink(
		const QString &emoticon,
		const QMap<QString, QString> &params);

	void applyUpdate(const MTPTheme &theme);

	void resolve(
		not_null<Window::Controller*> controller,
		const QString &slug,
		const FullMsgId &clickFromMessageId);
	void showPreview(
		not_null<Window::Controller*> controller,
		const MTPTheme &data);
	void showPreview(
		not_null<Window::Controller*> controller,
		const CloudTheme &cloud);
	void applyFromDocument(const CloudTheme &cloud);

private:
	struct LoadingDocument {
		CloudTheme theme;
		DocumentData *document = nullptr;
		std::shared_ptr<Data::DocumentMedia> documentMedia;
		rpl::lifetime subscription;
		Fn<void(std::shared_ptr<Data::DocumentMedia>)> callback;
	};

	void parseThemes(const QVector<MTPTheme> &list);
	void checkCurrentTheme();

	void install();
	void setupReload();
	[[nodiscard]] bool needReload() const;
	void scheduleReload();
	void reloadCurrent();
	void previewFromDocument(
		not_null<Window::Controller*> controller,
		const CloudTheme &cloud);
	void loadDocumentAndInvoke(
		LoadingDocument &value,
		const CloudTheme &cloud,
		not_null<DocumentData*> document,
		Fn<void(std::shared_ptr<Data::DocumentMedia>)> callback);
	void invokeForLoaded(LoadingDocument &value);

	void parseChatThemes(const QVector<MTPTheme> &list);

	const not_null<Main::Session*> _session;
	uint64 _hash = 0;
	mtpRequestId _refreshRequestId = 0;
	mtpRequestId _resolveRequestId = 0;
	std::vector<CloudTheme> _list;
	rpl::event_stream<> _updates;

	uint64 _chatThemesHash = 0;
	mtpRequestId _chatThemesRequestId = 0;
	std::vector<CloudTheme> _chatThemes;
	rpl::event_stream<> _chatThemesUpdates;

	base::Timer _reloadCurrentTimer;
	LoadingDocument _updatingFrom;
	LoadingDocument _previewFrom;
	uint64 _installedDayThemeId = 0;
	uint64 _installedNightThemeId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Data
