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

std::map<int, const char*> BetaLogs() {
	return {
	{
		1004004,
		"- Interface scaling for large screens, up to 300% "
		"(up to 150% for macOS retina screens).\n"

		"- Updated emoji."
	},
	{
		1004005,
		"- Listen to voice and video messages in 2X mode "
		"if you're in a hurry.\n"

		"- Find video messages in the shared voice messages section.\n"

		"- Add a comment when you share posts from channels.\n"

		"- View all photos and videos "
		"in Twitter and Instagram link previews.\n"

		"- Bug fixes and other minor improvements."
	},
	{
		1004008,
		"- Add emoji to media captions.\n"

		"- Switch off the 'Count unread messages' option "
		"in Settings > Notifications if you want to see "
		"the unread chats count in the badge instead."
	},
	{
		1005005,
		"- Support for auto-download of files and music.\n"

		"- Improved auto-download settings.\n"

		"- Bug fixes and other minor improvements."
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
		crl::guard(this, callback));
}

void Changelogs::addLocalLogs() {
	if (AppBetaVersion || cAlphaVersion()) {
		addBetaLogs();
	}
	if (!_addedSomeLocal) {
		const auto text = lng_new_version_wrap(
			lt_version,
			QString::fromLatin1(AppVersionStr),
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
	_session->data().serviceNotification(
		textWithEntities,
		MTP_messageMediaEmpty());
	_addedSomeLocal = true;
};

void Changelogs::addBetaLogs() {
	for (const auto[version, changes] : BetaLogs()) {
		addBetaLog(version, changes);
	}
}

void Changelogs::addBetaLog(int changeVersion, const char *changes) {
	if (_oldVersion >= changeVersion) {
		return;
	}
	const auto version = FormatVersionDisplay(changeVersion);
	const auto text = qsl("New in version %1:\n\n").arg(version)
		+ QString::fromUtf8(changes).trimmed();
	addLocalLog(text);
}

} // namespace Core
