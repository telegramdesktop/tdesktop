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
#include "base/qt/qt_common_adapters.h"
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> BetaLogs() {
	return {
	{
		4005004,
		"- Allow wide range of interface scale options.\n"

		"- Show opened chat name in the window title.\n"

		"- Bug fixes and other minor improvements.\n"

		"- Fix updating on macOS older than 10.14.\n"
	},
	{
		4005006,
		"- Try enabling non-fractional scale "
		"High DPI support on Windows and Linux.\n"

		"- Experimental setting for fractional scale "
		"High DPI support on Windows and Linux.\n"

		"- Fix navigation to bottom problems in groups you didn't join.\n"

		"- Fix a crash in chat export settings changes.\n"

		"- Fix a crash in sending some of JPEG images.\n"

		"- Fix CJK fonts on Windows.\n"
	},
	{
		4005007,
		"- Fix glitches after moving window to another screen.\n",
	},
	{
		4005008,
		"- Allow opening another account in a new window "
		"(see Settings > Advanced > Experimental Settings).\n"

		"- A lot of bugfixes for working with more than one window.\n"
	},
	{
		4006004,
		"- Allow media viewer to exit fullscreen and become a normal window."
	},
	{
		4006006,
		"- Confirmation window before starting a call.\n"

		"- New \"Battery and Animations\" settings section.\n"

		"- \"Save Power on Low Battery\" option for laptops.\n"

		"- Improved windowed mode support for media viewer.\n"

		"- Hardware accelerated video playback fix on macOS.\n"

		"- New application icon on macOS following the system guidelines.\n"
	},
	{
		4006007,
		"- Fix crash when accepting incoming calls.\n"

		"- Remove sound when cancelling an unconfirmed call.\n"
	},
	{
		4006008,
		"- Improve quality of voice messages with changed playback speed.\n"

		"- Show when your message was read in small groups.\n"

		"- Fix pasting images from Firefox on Windows.\n"

		"- Improve memory usage for custom emoji.\n"
	},
	{
		4006010,
		"- Suggest sending an invite link if user forbids "
		"inviting him to groups.\n"

		"- Show when a reaction was left on your message in small groups.\n"

		"- Fix a crash in video chats on Windows.\n"

		"- Fix a crash in audio speed change.\n"
	},
	{
		4006011,
		"- Allow larger interface scale values on high-dpi screens.\n"

		"- Implement new voice and video speed change interface (up to 2.5x).\n"

		"- Support global Fn+F shortcut to toggle fullscreen on macOS.\n"

		"- Silent notification sound in Focus Mode on macOS.\n"

		"- Fix media viewer on macOS with several screens.\n"
		
		"- Fix a crash in connection type box.\n"

		"- Fix possible crash on quit.\n"
	},
	{
		4006012,
		"- Fix several possible crashes.\n"

		"- Deprecate macOS 10.12, Ubuntu 18.04 and CentOS 7 in July.\n"
	},
	{
		4008011,
		"- Fix initial video playback speed.\n"

		"- Use native window resize on Windows 11.\n"

		"- Fix memory leak in Direct3D 11 media viewer on Windows.\n"
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
	const auto log = u"New in version %1 beta:\n\n"_q.arg(version) + text;
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
