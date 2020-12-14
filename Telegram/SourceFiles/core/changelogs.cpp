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
		2001008,
		"- Add support for full group message history export.\n"

		"- Allow export of a single chat message history in JSON format."
	},
	{
		2001014,
		"- Support for multiple accounts."
	},
	{
		2001017,
		"- Fix messages editing in a non-active account.\n"

		"- Fix large animated emoji messages editing.\n"

		"- Fix high definition GIF animations opening in media viewer.\n"

		"- Multiple crash fixes."
	},
	{
		2001018,
		"- Fix a possible crash in Picture-in-Picture video player.\n"

		"- Fix copying links from message texts.\n"

		"- Raise file size limit to 2000 MB.\n"

		"- Allow using system window frame in Windows and Linux."
	},
	{
		2001019,
		"- File uploading in an inactive account correctly finishes.\n"

		"- Stickers panel works correctly after switching between accounts.\n"

		"- Large .webp files are not shown as stickers.\n"

		"- MacBook TouchBar support was fully rewritten with fixes for multiple accounts.\n"

		"- Custom window title bar works in all Linux versions.\n"

		"- Passcode doesn't auto-lock while you're active in other apps on Linux X11."
	},
	{
		2001021,
		"- Edit your scheduled messages.\n"

		"- See the unread messages indicator for your additional accounts on the main menu button.\n"

		"- Use Auto-Night Mode to make Telegram night mode match the system Dark Mode settings.\n"

		"- Enjoy dark native window frame for Telegram night mode on Windows.\n"
	},
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
			qsl("https://desktop.telegram.org/changelog"));
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
