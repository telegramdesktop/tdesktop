/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/changelogs.h"

#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> AlphaLogs() {
	return {
	{
		1002002,
		"\xE2\x80\x94 Grouped photos and videos are displayed as albums."
	},
	{
		1002004,
		"\xE2\x80\x94 Group media into an album "
		"when sharing multiple photos and videos.\n"

		"\xE2\x80\x94 Bug fixes and other minor improvements."
	},
	{
		1002005,
		"\xE2\x80\x94 When viewing a photo from an album, "
		"you'll see other pictures from the same group "
		"as thumbnails in the lower part of the screen.\n"

		"\xE2\x80\x94 When composing an album paste "
		"additional media from the clipboard.\n"

		"\xE2\x80\x94 Bug fixes and other minor improvements."
	},
	{
		1002007,
		"\xE2\x80\x94 Use fast reply button in group chats.\n"

		"\xE2\x80\x94 Select a message you want to reply to by "
		"pressing Ctrl+Up and Ctrl+Down."
	},
	{
		1002009,
		"\xE2\x80\x94 Quick Reply. "
		"Double click next to any message for a quick reply.\n"

		"\xE2\x80\x94 Search for Stickers. "
		"Click on the new search icon to access "
		"your sticker sets or find new ones."
	},
	{
		1002019,
		"\xE2\x80\x94 Enable proxy for calls in Settings.\n"

		"\xE2\x80\x94 Bug fixes and other minor improvements."
	},
	{
		1002020,
		"\xE2\x80\x94 Emoji and text replacements are done "
		"while you type the message.\n"

		"\xE2\x80\x94 Revert emoji and text replacements "
		"by pressing backspace.\n"

		"\xE2\x80\x94 Disable emoji replacements or suggestions "
		"in Settings.\n"

		"\xE2\x80\x94 Some critical bug fixes."
	},
	{
		1002022,
		"\xE2\x80\x94 Use markdown in media captions "
		"(**bold**, __italic__, `tag` and ```code```).\n"

		"\xE2\x80\x94 Use emoji replacement in media captions, "
		"group and channel titles and descriptions (:like: etc.)\n"

		"\xE2\x80\x94 Markdown replacement now happens immediately "
		"after typing (instead of after sending) and can be "
		"rolled back using Backspace or Ctrl/Cmd + Z. "
		"Replacement no longer happens when pasting text."
	},
	{
		1002023,
		"\xE2\x80\x94 Apply formatting from input field context menu.\n"

		"\xE2\x80\x94 Apply formatting by hotkeys.\n"

		"\xE2\x80\x94 Bug fixes and other minor improvements."
	}
	};
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

} // namespace

Changelogs::Changelogs(not_null<AuthSession*> session, int oldVersion)
: _session(session)
, _oldVersion(oldVersion) {
	_chatsSubscription = subscribe(
		_session->data().moreChatsLoaded(),
		[this] { requestCloudLogs(); });
}

std::unique_ptr<Changelogs> Changelogs::Create(
		not_null<AuthSession*> session) {
	const auto oldVersion = Local::oldMapVersion();
	return (oldVersion > 0 && oldVersion < AppVersion)
		? std::make_unique<Changelogs>(session, oldVersion)
		: nullptr;
}

void Changelogs::requestCloudLogs() {
	unsubscribe(base::take(_chatsSubscription));

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
			resultEmpty = result.c_updatesCombined().vupdates.v.isEmpty();
			break;
		case mtpc_updates:
			resultEmpty = result.c_updates().vupdates.v.isEmpty();
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
		base::lambda_guarded(this, callback));
}

void Changelogs::addLocalLogs() {
	if (cAlphaVersion() || cBetaVersion()) {
		addAlphaLogs();
	}
	if (!_addedSomeLocal) {
		const auto text = lng_new_version_wrap(
			lt_version,
			str_const_toString(AppVersionStr),
			lt_changes,
			lang(lng_new_version_minor),
			lt_link,
			qsl("https://desktop.telegram.org/changelog"));
		addLocalLog(text.trimmed());
	}
}

void Changelogs::addLocalLog(const QString &text) {
	auto textWithEntities = TextWithEntities{ text };
	TextUtilities::ParseEntities(textWithEntities, TextParseLinks);
	App::wnd()->serviceNotification(
		textWithEntities,
		MTP_messageMediaEmpty(),
		unixtime());
	_addedSomeLocal = true;
};

void Changelogs::addAlphaLogs() {
	for (const auto[version, changes] : AlphaLogs()) {
		addAlphaLog(version, changes);
	}
}

void Changelogs::addAlphaLog(int changeVersion, const char *changes) {
	if (_oldVersion >= changeVersion) {
		return;
	}
	const auto version = FormatVersionDisplay(changeVersion);
	const auto text = qsl("New in version %1:\n\n").arg(version)
		+ QString::fromUtf8(changes).trimmed();
	addLocalLog(text);
}

} // namespace Core
