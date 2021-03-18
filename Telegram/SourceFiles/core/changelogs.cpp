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
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> BetaLogs() {
	return {
	{
		2004006,
		"- Fix image compression option when sending files with drag-n-drop.\n"

		"- Fix caption text selection in media albums.\n"

		"- Fix drafts display in personal chats in the chats list.\n"

		"- Bug fixes and other minor improvements.\n"
	},
	{
		2004008,
		"- Upgrade several third party libraries to latest versions.\n"
	},
	{
		2004010,
		"- Use inline bots and sticker by emoji suggestions in channel comments.\n"

		"- Lock voice message recording, listen to your voice message before sending.\n"
	},
	{
		2004011,
		"- Improve locked voice message recording.\n"

		"- Fix main window closing to tray on Windows.\n"

		"- Fix crash in bot command sending.\n"

		"- Fix adding additional photos when sending an album to a group with enabled slow mode.\n"
	},
	{
		2004012,
		"- Voice chats in groups. (alpha version)\n"
	},
	{
		2004014,
		"- Create voice chats in legacy groups.\n"

		"- Fix sticker pack opening.\n"

		"- Fix group status display.\n"

		"- Fix group members display.\n"
	},
	{
		2004015,
		"- Improve design of voice chats.\n"

		"- Fix sending of voice messages as replies.\n"

		"- Fix 'Open With' menu position in macOS.\n"

		"- Fix freeze on secondary screen disconnect.\n"
	},
	{
		2005002,
		"- Fix possible crash in video calls.\n"

		"- Fix possible crash in connecting to voice chats.\n"

		"- Use different audio module code on Windows in calls.\n"
	},
	{
		2005003,
		"- Allow using mouse buttons in Push-to-Talk shortcut.\n"

		"- Fix blurred thumbnails in Shared Links section.\n"
	},
	{
		2005004,
		"- Implement new audio module code for calls and voice chats.\n"

		"- Allow retracting votes from polls in comments to channel posts.\n"

		"- Show small voice chat button for empty voice chats.\n"

		"- Fix media viewer updating when screen resolution is changed.\n"
	},
	{
		2005005,
		"- Fix recording of audio in voice chats.\n"

		"- Fix media viewer zoom and crashing.\n"
	},
	{
		2005006,
		"- Press Up arrow to edit your last sent comment.\n"

		"- Add more information to date tooltips "
		"in Recent Actions and channel comments.\n"

		"- Bug and crash fixes.\n"
	},
	{
		2006002,
		"- Fix text disappearing because of cloud drafts sync.\n"
	},
	{
		2006003,
		"- Fix audio device selection in voice chats.\n"

		"- Fix blinking self profile photo "
		"in case the profile photo privacy is used.\n"

		"- Fix voice chat admin menu on macOS.\n"
	},
	{
		2006004,
		"- Fix freeze in voice chats.\n"

		"- Make default interface scale 110% on macOS Retina screens.\n"
	},
	{
		2006005,
		"- Improvements and fixes in new voice chat features.\n"
	},
	{
		2006007,
		"- Improve voice chat participants list updating.\n"
	},
	{
		2006008,
		"- Fix connecting and getting allowed to speak on voice chats.\n"

		"- MPRIS support on Linux.\n"
	},
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
	for (const auto [version, changes] : BetaLogs()) {
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
		if (result.startsWith(simple.midRef(1))) {
			result = separator.mid(1) + result.mid(simple.size() - 1);
		}
		return result.replace(simple, separator);
	}();
	const auto version = FormatVersionDisplay(changeVersion);
	const auto log = qsl("New in version %1:\n\n").arg(version) + text;
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
