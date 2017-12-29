/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "core/changelogs.h"

#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> AlphaLogs() {
	return {
	{
		1001024,
		"\xE2\x80\x94 Radically improved navigation. "
		"New side panel on the right with quick access to "
		"shared media and group members.\n"

		"\xE2\x80\x94 Pinned Messages. If you are a channel admin, "
		"pin messages to focus your subscribers\xE2\x80\x99 attention "
		"on important announcements.\n"

		"\xE2\x80\x94 Also supported clearing history in supergroups "
		"and added a host of minor improvements."
	},
	{
		1001026,
		"\xE2\x80\x94 Admin badges in supergroup messages.\n"
		"\xE2\x80\x94 Fix crashing on launch in OS X 10.6.\n"
		"\xE2\x80\x94 Bug fixes and other minor improvements."
	},
	{
		1001027,
		"\xE2\x80\x94 Saved Messages. Bookmark messages by forwarding them "
		"to \xE2\x80\x9C""Saved Messages\xE2\x80\x9D. "
		"Access them from the Chats list or from the side menu."
	},
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
