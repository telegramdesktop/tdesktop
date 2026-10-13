/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "mtproto/sender.h"
#include "wallet/wallet_unlock.h"

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class FlatLabel;
class SettingsButton;
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Wallet {

struct TonConnectKey;
struct TonConnectKeyResult;
struct TonConnectReply;
enum class TonConnectKeyError : uchar;

struct TonConnectLoginRequest {
	uint64 sessionId = 0;
	QString clientId;
	QString domain;
	uint64 botId = 0;
};

[[nodiscard]] std::optional<TonConnectLoginRequest> ParseTonConnectLogin(
	const MTPDurlAuthResultRequest &data);

void PreloadTonConnectLoginWallet(
	not_null<Main::Session*> session,
	Fn<void()> done,
	Fn<void()> lost = nullptr);

struct TonConnectLoginArgs {
	not_null<Main::Session*> session;
	std::shared_ptr<Main::SessionShow> show;
	TonConnectLoginRequest request;
	rpl::producer<QString> botName;
	Fn<void(bool)> busyChanged;
};

class TonConnectLoginSection final : public base::has_weak_ptr {
public:
	TonConnectLoginSection(
		not_null<Ui::VerticalLayout*> container,
		TonConnectLoginArgs &&args);
	~TonConnectLoginSection();

	[[nodiscard]] bool wanted() const;
	[[nodiscard]] bool busy() const;
	void confirm(Fn<void(MTPInputTonConnectOauthSession)> ready);

private:
	enum class State : uchar {
		Loading,
		Allowed,
		NoKey,
		KeyChanging,
		Unavailable,
		LoadFailed,
		Reserved,
	};

	[[nodiscard]] static rpl::producer<TextWithEntities> StateReason(
		State state);

	void setup();
	void refresh();
	[[nodiscard]] State computeState();
	void applyState(State state);
	void rebuildRow();
	void stopResolving();
	void restorePressed();
	void restored(KeyAuthorization auth);
	void start();
	void unlocked(KeyAuthorization auth);
	void create();
	void created(const MTPTonConnectSession &result);
	void createFailed(const MTP::Error &error);
	void manifestChanged();
	void pollManifest();
	void manifestPollFailed(const MTP::Error &error);
	void stopManifestWait();
	void keyReady(TonConnectKeyResult result);
	void registerKey(TonConnectKey key);
	void registered(TonConnectKey key, QByteArray challenge, uint64 eventId);
	void registerFailed(const MTP::Error &error);
	void prepared(TonConnectReply reply);
	void prepareFailed(TonConnectKeyError error);
	void fail(const QString &text);
	void setBusy(bool busy);

	const not_null<Ui::VerticalLayout*> _container;
	const not_null<Main::Session*> _session;
	const std::shared_ptr<Main::SessionShow> _show;
	const TonConnectLoginRequest _request;
	const QString _manifestUrl;
	const Fn<void(bool)> _busyChanged;
	MTP::Sender _api;
	rpl::variable<QString> _botName;
	rpl::variable<State> _state = State::Loading;
	rpl::variable<bool> _toggled;
	QPointer<Ui::VerticalLayout> _rowSlot;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _error;
	QPointer<Ui::SettingsButton> _toggle;
	QString _rowAddress;
	QString _address;
	VaultAuthorization _grant;
	Fn<void(MTPInputTonConnectOauthSession)> _ready;
	base::Timer _resolveTimer;
	base::Timer _manifestTimer;
	rpl::lifetime _restoreLifetime;
	rpl::lifetime _manifestLifetime;
	mtpRequestId _manifestRequestId = 0;
	int _manifestPolls = 0;
	bool _wanted = true;
	bool _busy = false;
	bool _queued = false;
	bool _polling = false;
	bool _timedOut = false;
	bool _restoring = false;

};

[[nodiscard]] auto AddTonConnectLoginSection(
	not_null<Ui::VerticalLayout*> container,
	TonConnectLoginArgs &&args)
-> base::weak_ptr<TonConnectLoginSection>;

} // namespace Wallet
