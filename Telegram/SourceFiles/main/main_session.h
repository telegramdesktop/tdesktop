/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/filter.h>
#include <rpl/variable.h>
#include "base/timer.h"

class ApiWrap;

namespace Api {
class Updates;
class SendProgressManager;
} // namespace Api

namespace MTP {
class Instance;
struct ConfigFields;
} // namespace MTP

namespace Support {
class Helper;
class Templates;
class FastButtonsBots;
} // namespace Support

namespace Data {
class Session;
class Changes;
class RecentPeers;
class RecentSharedMediaGifts;
class ScheduledMessages;
class SponsoredMessages;
class TopPeers;
class Factchecks;
class LocationPickers;
class Credits;
class PromoSuggestions;
} // namespace Data

namespace HistoryView::Reactions {
class CachedIconFactory;
} // namespace HistoryView::Reactions

namespace Storage {
class DownloadManagerMtproto;
class Uploader;
class Facade;
class Account;
class Domain;
} // namespace Storage

namespace Window {
class SessionController;
struct TermsLock;
} // namespace Window

namespace Stickers {
class EmojiPack;
class DicePacks;
class GiftBoxPack;
} // namespace Stickers;

namespace InlineBots {
class AttachWebView;
} // namespace InlineBots

namespace Ui {
struct ColorIndicesCompressed;
} // namespace Ui

namespace Main {

class Account;
class AppConfig;
class Domain;
class SessionSettings;
class SendAsPeers;

struct FreezeInfo {
	TimeId since = 0;
	TimeId until = 0;
	QString appealUrl;

	explicit operator bool() const {
		return since != 0;
	}
	friend inline bool operator==(
		const FreezeInfo &,
		const FreezeInfo &) = default;
};

class Session final : public base::has_weak_ptr {
public:
	Session(
		not_null<Account*> account,
		const MTPUser &user,
		std::unique_ptr<SessionSettings> settings);
	~Session();

	Session(const Session &other) = delete;
	Session &operator=(const Session &other) = delete;

	[[nodiscard]] Account &account() const;
	[[nodiscard]] Storage::Account &local() const;
	[[nodiscard]] Domain &domain() const;
	[[nodiscard]] Storage::Domain &domainLocal() const;

	[[nodiscard]] AppConfig &appConfig() const;

	[[nodiscard]] bool premium() const;
	[[nodiscard]] bool premiumPossible() const;
	[[nodiscard]] rpl::producer<bool> premiumPossibleValue() const;
	[[nodiscard]] bool premiumBadgesShown() const;
	[[nodiscard]] bool premiumCanBuy() const;

	[[nodiscard]] bool isTestMode() const;
	[[nodiscard]] uint64 uniqueId() const; // userId() with TestDC shift.
	[[nodiscard]] UserId userId() const;
	[[nodiscard]] PeerId userPeerId() const;
	[[nodiscard]] not_null<UserData*> user() const {
		return _user;
	}
	bool validateSelf(UserId id);

	[[nodiscard]] Data::Changes &changes() const {
		return *_changes;
	}
	[[nodiscard]] Data::RecentPeers &recentPeers() const {
		return *_recentPeers;
	}
	[[nodiscard]] Data::RecentSharedMediaGifts &recentSharedGifts() const {
		return *_recentSharedGifts;
	}
	[[nodiscard]] Data::SponsoredMessages &sponsoredMessages() const {
		return *_sponsoredMessages;
	}
	[[nodiscard]] Data::ScheduledMessages &scheduledMessages() const {
		return *_scheduledMessages;
	}
	[[nodiscard]] Data::TopPeers &topPeers() const {
		return *_topPeers;
	}
	[[nodiscard]] Data::TopPeers &topBotApps() const {
		return *_topBotApps;
	}
	[[nodiscard]] Data::Factchecks &factchecks() const {
		return *_factchecks;
	}
	[[nodiscard]] Data::LocationPickers &locationPickers() const {
		return *_locationPickers;
	}
	[[nodiscard]] Data::Credits &credits() const {
		return *_credits;
	}
	[[nodiscard]] Api::Updates &updates() const {
		return *_updates;
	}
	[[nodiscard]] Api::SendProgressManager &sendProgressManager() const {
		return *_sendProgressManager;
	}
	[[nodiscard]] Storage::DownloadManagerMtproto &downloader() const {
		return *_downloader;
	}
	[[nodiscard]] Storage::Uploader &uploader() const {
		return *_uploader;
	}
	[[nodiscard]] Storage::Facade &storage() const {
		return *_storage;
	}
	[[nodiscard]] Stickers::EmojiPack &emojiStickersPack() const {
		return *_emojiStickersPack;
	}
	[[nodiscard]] Stickers::DicePacks &diceStickersPacks() const {
		return *_diceStickersPacks;
	}
	[[nodiscard]] Stickers::GiftBoxPack &giftBoxStickersPacks() const {
		return *_giftBoxStickersPacks;
	}
	[[nodiscard]] Data::Session &data() const {
		return *_data;
	}
	[[nodiscard]] SessionSettings &settings() const {
		return *_settings;
	}
	[[nodiscard]] SendAsPeers &sendAsPeers() const {
		return *_sendAsPeers;
	}
	[[nodiscard]] InlineBots::AttachWebView &attachWebView() const {
		return *_attachWebView;
	}
	[[nodiscard]] Data::PromoSuggestions &promoSuggestions() const {
		return *_promoSuggestions;
	}
	[[nodiscard]] auto cachedReactionIconFactory() const
	-> HistoryView::Reactions::CachedIconFactory & {
		return *_cachedReactionIconFactory;
	}

	void saveSettings();
	void saveSettingsDelayed(crl::time delay = kDefaultSaveDelay);
	void saveSettingsNowIfNeeded();

	void addWindow(not_null<Window::SessionController*> controller);
	[[nodiscard]] auto windows() const
		-> const base::flat_set<not_null<Window::SessionController*>> &;
	[[nodiscard]] Window::SessionController *tryResolveWindow(
		PeerData *forPeer = nullptr) const;

	// Shortcuts.
	void notifyDownloaderTaskFinished();
	[[nodiscard]] rpl::producer<> downloaderTaskFinished() const;
	[[nodiscard]] MTP::DcId mainDcId() const;
	[[nodiscard]] MTP::Instance &mtp() const;
	[[nodiscard]] const MTP::ConfigFields &serverConfig() const;
	[[nodiscard]] ApiWrap &api() {
		return *_api;
	}

	// Terms lock.
	void lockByTerms(const Window::TermsLock &data);
	void unlockTerms();
	void termsDeleteNow();
	[[nodiscard]] std::optional<Window::TermsLock> termsLocked() const;
	rpl::producer<bool> termsLockChanges() const;
	rpl::producer<bool> termsLockValue() const;

	[[nodiscard]] QString createInternalLink(const QString &query) const;
	[[nodiscard]] QString createInternalLinkFull(const QString &query) const;
	[[nodiscard]] TextWithEntities createInternalLink(
		const TextWithEntities &query) const;
	[[nodiscard]] TextWithEntities createInternalLinkFull(
		TextWithEntities query) const;

	void setTmpPassword(const QByteArray &password, TimeId validUntil);
	[[nodiscard]] QByteArray validTmpPassword() const;

	// Can be called only right before ~Session.
	void finishLogout();

	// Uploads cancel with confirmation.
	[[nodiscard]] bool uploadsInProgress() const;
	void uploadsStopWithConfirmation(Fn<void()> done);
	void uploadsStop();

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

	[[nodiscard]] bool supportMode() const;
	[[nodiscard]] Support::Helper &supportHelper() const;
	[[nodiscard]] Support::Templates &supportTemplates() const;
	[[nodiscard]] Support::FastButtonsBots &fastButtonsBots() const;

	[[nodiscard]] FreezeInfo frozen() const;
	[[nodiscard]] rpl::producer<FreezeInfo> frozenValue() const;

	[[nodiscard]] auto colorIndicesValue()
		-> rpl::producer<Ui::ColorIndicesCompressed>;

private:
	static constexpr auto kDefaultSaveDelay = crl::time(1000);

	void appConfigRefreshed();

	const UserId _userId;
	const not_null<Account*> _account;

	const std::unique_ptr<SessionSettings> _settings;
	const std::unique_ptr<Data::Changes> _changes;
	const std::unique_ptr<ApiWrap> _api;
	const std::unique_ptr<Api::Updates> _updates;
	const std::unique_ptr<Api::SendProgressManager> _sendProgressManager;
	const std::unique_ptr<Storage::DownloadManagerMtproto> _downloader;
	const std::unique_ptr<Storage::Uploader> _uploader;
	const std::unique_ptr<Storage::Facade> _storage;

	// _data depends on _downloader / _uploader.
	const std::unique_ptr<Data::Session> _data;
	const not_null<UserData*> _user;

	// _emojiStickersPack depends on _data.
	const std::unique_ptr<Stickers::EmojiPack> _emojiStickersPack;
	const std::unique_ptr<Stickers::DicePacks> _diceStickersPacks;
	const std::unique_ptr<Stickers::GiftBoxPack> _giftBoxStickersPacks;
	const std::unique_ptr<SendAsPeers> _sendAsPeers;
	const std::unique_ptr<InlineBots::AttachWebView> _attachWebView;
	const std::unique_ptr<Data::RecentPeers> _recentPeers;
	const std::unique_ptr<Data::RecentSharedMediaGifts> _recentSharedGifts;
	const std::unique_ptr<Data::ScheduledMessages> _scheduledMessages;
	const std::unique_ptr<Data::SponsoredMessages> _sponsoredMessages;
	const std::unique_ptr<Data::TopPeers> _topPeers;
	const std::unique_ptr<Data::TopPeers> _topBotApps;
	const std::unique_ptr<Data::Factchecks> _factchecks;
	const std::unique_ptr<Data::LocationPickers> _locationPickers;
	const std::unique_ptr<Data::Credits> _credits;
	const std::unique_ptr<Data::PromoSuggestions> _promoSuggestions;

	using ReactionIconFactory = HistoryView::Reactions::CachedIconFactory;
	const std::unique_ptr<ReactionIconFactory> _cachedReactionIconFactory;

	const std::unique_ptr<Support::Helper> _supportHelper;
	const std::unique_ptr<Support::FastButtonsBots> _fastButtonsBots;

	std::shared_ptr<QImage> _selfUserpicView;
	rpl::variable<bool> _premiumPossible = false;

	rpl::event_stream<bool> _termsLockChanges;
	std::unique_ptr<Window::TermsLock> _termsLock;

	base::flat_set<not_null<Window::SessionController*>> _windows;
	base::Timer _saveSettingsTimer;

	rpl::variable<FreezeInfo> _frozen;

	QByteArray _tmpPassword;
	TimeId _tmpPasswordValidUntil = 0;

	rpl::lifetime _lifetime;

};

} // namespace Main
