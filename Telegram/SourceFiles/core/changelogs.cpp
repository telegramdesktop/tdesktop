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
		1002024,
		"\xE2\x80\x94 Add links with custom text from context menu "
		"or by Ctrl/Cmd + K keyboard shortcut."
	},
	{
		1002025,
		"\xE2\x80\x94 Apply markdown formatting (```, `, **, __) "
		"only when sending the message.\n"

		"\xE2\x80\x94 Display connection quality bars in calls.\n"

		"\xE2\x80\x94 Telegram Desktop can update itself through MTProto.\n"

		"\xE2\x80\x94 Bug fixes and other minor improvements."
	},
	{
		1003011,
		"\xE2\x80\x94 Added a new night theme.\n"

		"\xE2\x80\x94 You can now assign custom themes "
		"as night and day themes to quickly switch between them."
	},
	{
		1003015,
		"\xE2\x80\x94 Improved local caching "
		"for images and GIF animations.\n"

		"\xE2\x80\x94 Control how much disk space is used by the cache "
		"and for how long the cached files are stored."
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
