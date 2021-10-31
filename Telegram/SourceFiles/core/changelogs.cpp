/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/changelogs.h"

#include "lang/lang_keys.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "data/data_session.h"
#include "base/qt_adapters.h"
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> BetaLogs() {
	return {
	{
		2009004,
		"- Choose one from dozens of new gorgeous animated backgrounds"
		" in Chat Settings > Chat background.\n"
	},
	{
		2009005,
		"- Tile chat background patterns horizontally.\n"

		"- Fix a rare crash in spellchecker on Windows.\n"

		"- Fix animated chat backgrounds in Saved Messages.\n"

		"- Fix \"Sorry, group is inaccessible\" message "
		"in scheduled voice chats.\n",
	},
	{
		2009013,
		"- See unread comments count when scrolling discussions in channels."
	},
	{
		3000002,
		"- Check who've seen your message in small groups "
		"from the context menu.\n"

		"- Enable recording with video in live streams and video chats."
	},
	{
		3000004,
		"- Fix a crash when joining video chat or live broadcast.\n"

		"- Add a \"Close to Taskbar\" option when tray icon is disabled "
		"(Windows and Linux)."
	},
	{
		3000005,
		"- Add support for Emoji 13.1."
	},
	{
		3001002,
		"- Control video in fullscreen mode using arrows and numbers.\n"

		"- Open locations in browser if default Bing Maps is not installed.\n"

		"- Reconnect without timeout when network availability changes.\n"

		"- Crash fixes."
	},
	{
		3001005,
		"- Choose one of 8 new preset themes for any individual private chat.\n"

		"- Click on '...' menu > 'Change Colors' to pick a theme.\n"

		"- Both chat participants will see the same theme in that chat "
		"– on all their devices.\n"

		"- Each new theme features colorful gradient message bubbles, "
		"beautifully animated backgrounds and unique background patterns.\n"

		"- All chat themes have day and night versions and will follow "
		"your overall dark mode settings.\n"

		"- Implement main window rounded corners on Windows 11.\n"

		"- Fix audio capture from AirPods on macOS.\n"
	},
	{
		3001006,
		"- Show small media previews in chats list.\n"

		"- Show media album previews and caption text in chats list.\n"

		"- Add \"Quick Reply\" and \"Mark as Read\" "
		"to native Windows notifications.\n"
	},
	{
		3001012,
		"- Create special invite links that require admins "
		"to approve users before they become members.\n"

		"- Admins can view the applicants' profiles and bios "
		"by tapping the Join Requests bar at the top of the chat.\n"

		"- Add internal labels to your chat's Invite Links "
		"to keep them organized.\n"

		"- Run natively on Apple Silicon (macOS only).\n"
	}
	};
};

} // namespace

Changelogs::Changelogs(not_null<Main::Session*> session, int oldVersion)
: _session(session)
, _oldVersion(oldVersion) {
	_session->data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return !folder;
	}) | rpl::start_with_next([=] {
		requestCloudLogs();
	}, _chatsSubscription);
}

std::unique_ptr<Changelogs> Changelogs::Create(
		not_null<Main::Session*> session) {
	auto &local = Core::App().domain().local();
	const auto oldVersion = local.oldVersion();
	local.clearOldVersion();
	return (oldVersion > 0 && oldVersion < AppVersion)
		? std::make_unique<Changelogs>(session, oldVersion)
		: nullptr;
}

void Changelogs::requestCloudLogs() {
	_chatsSubscription.destroy();

	const auto callback = [this](const MTPUpdates &result) {
		_session->api().applyUpdates(result);

		auto resultEmpty = true;
		switch (result.type()) {
		case mtpc_updateShortMessage:
		case mtpc_updateShortChatMessage:
		case mtpc_updateShort:
			resultEmpty = false;
			break;
		case mtpc_updatesCombined:
			resultEmpty = result.c_updatesCombined().vupdates().v.isEmpty();
			break;
		case mtpc_updates:
			resultEmpty = result.c_updates().vupdates().v.isEmpty();
			break;
		case mtpc_updatesTooLong:
		case mtpc_updateShortSentMessage:
			LOG(("API Error: Bad updates type in app changelog."));
			break;
		}
		if (resultEmpty) {
			addLocalLogs();
		}
	};
	_session->api().requestChangelog(
		FormatVersionPrecise(_oldVersion),
		crl::guard(this, callback));
}

void Changelogs::addLocalLogs() {
	if (AppBetaVersion || cAlphaVersion()) {
		addBetaLogs();
	}
	if (!_addedSomeLocal) {
		const auto text = tr::lng_new_version_wrap(
			tr::now,
			lt_version,
			QString::fromLatin1(AppVersionStr),
			lt_changes,
			tr::lng_new_version_minor(tr::now),
			lt_link,
			Core::App().changelogLink());
		addLocalLog(text.trimmed());
	}
}

void Changelogs::addLocalLog(const QString &text) {
	auto textWithEntities = TextWithEntities{ text };
	TextUtilities::ParseEntities(textWithEntities, TextParseLinks);
	_session->data().serviceNotification(textWithEntities);
	_addedSomeLocal = true;
};

void Changelogs::addBetaLogs() {
	for (const auto &[version, changes] : BetaLogs()) {
		addBetaLog(version, changes);
	}
}

void Changelogs::addBetaLog(int changeVersion, const char *changes) {
	if (_oldVersion >= changeVersion) {
		return;
	}
	const auto text = [&] {
		static const auto simple = u"\n- "_q;
		static const auto separator = QString::fromUtf8("\n\xE2\x80\xA2 ");
		auto result = QString::fromUtf8(changes).trimmed();
		if (result.startsWith(base::StringViewMid(simple, 1))) {
			result = separator.mid(1) + result.mid(simple.size() - 1);
		}
		return result.replace(simple, separator);
	}();
	const auto version = FormatVersionDisplay(changeVersion);
	const auto log = qsl("New in version %1 beta:\n\n").arg(version) + text;
	addLocalLog(log);
}

QString FormatVersionDisplay(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ ((version % 1000)
			? ('.' + QString::number(version % 1000))
			: QString());
}

QString FormatVersionPrecise(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ '.' + QString::number(version % 1000);
}

} // namespace Core
