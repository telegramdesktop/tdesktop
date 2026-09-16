/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_comment.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_unlock.h"

namespace Wallet {

TextWithEntities TransferCommentCover(const TransferItem &item) {
	constexpr auto kPayloadOverhead = 64;
	constexpr auto kMinLength = 8;
	constexpr auto kMaxLength = 128;
	const auto length = (item.encryptedFormat
		== TransferItem::EncryptedFormat::ServerPayload)
		? std::clamp(
			int(item.encryptedPayload.size()) - kPayloadOverhead,
			kMinLength,
			kMaxLength)
		: kMinLength;
	auto result = tr::marked(QString(length, QChar('x')));
	result.entities.push_back(EntityInText(EntityType::Spoiler, 0, length));
	return result;
}

TransferComment::TransferComment(
	not_null<Main::Session*> session,
	TransferItem target,
	Fn<bool()> originCurrent)
: _session(base::make_weak(session))
, _target(std::move(target))
, _originCurrent(std::move(originCurrent)) {
	Expects(_originCurrent != nullptr);

	const auto weak = base::make_weak(this);
	session->lifetime().add([weak] {
		if (weak) {
			weak->_session = nullptr;
			weak->reset();
		}
	});
	session->account().sessionChanges() | rpl::on_next([=] {
		reset();
	}, _lifetime);
	session->domain().activeSessionChanges() | rpl::on_next([=](
			Main::Session *active) {
		if (active != _session.get()) {
			reset();
		}
	}, _lifetime);
	auto &wallet = session->wallet();
	rpl::merge(
		wallet.transferWalletIdentityChanges(),
		wallet.custodyUpdates(),
		wallet.keyProtectionUpdates(),
		wallet.deviceCustodyStateValue() | rpl::to_empty
	) | rpl::on_next([=] {
		validate();
	}, _lifetime);
	rpl::merge(
		Core::App().passcodeLockChanges(),
		Core::App().screenIsLockedValue()
	) | rpl::filter([](bool locked) {
		return locked;
	}) | rpl::on_next([=] {
		reset();
	}, _lifetime);
	Core::App().systemSleepEvents() | rpl::on_next([=] {
		reset();
	}, _lifetime);
}

TransferComment::~TransferComment() {
	_lifetime.destroy();
	clear();
}

bool TransferComment::originCurrent() const {
	const auto session = _session.get();
	return session
		&& !session->account().loggingOut()
		&& !session->account().destroyingSession()
		&& session->account().maybeSession() == session
		&& !Core::App().passcodeLocked()
		&& !Core::App().screenIsLocked()
		&& _originCurrent();
}

bool TransferComment::attemptCurrent(uint64 revision) const {
	const auto scope = _scope;
	return _revision == revision
		&& _pending
		&& originCurrent()
		&& _session->wallet().commentScopeCurrent(scope);
}

void TransferComment::validate() {
	const auto weak = base::make_weak(this);
	const auto scope = _scope;
	if ((_pending || _plaintext.has_value())
		&& (!originCurrent()
			|| !_session->wallet().commentScopeCurrent(scope))) {
		if (weak) {
			weak->reset();
		}
	}
}

void TransferComment::activate(std::shared_ptr<Main::SessionShow> show) {
	const auto weak = base::make_weak(this);
	validate();
	if (!weak) {
		return;
	}
	if (_pending || _plaintext.has_value()) {
		return;
	} else if (!originCurrent()
		|| !show
		|| !show->valid()
		|| &show->session() != _session.get()) {
		return;
	}
	_scope = _session->wallet().createCommentScope(_target, _attempt);
	if (!_scope) {
		// A different wallet parked on this device blocks the scope, not
		// the comment: the conflict box resolves it, and the same click
		// then continues into the restore or import the comment needs.
		if (_session->wallet().deviceCustodyState().conflict) {
			ShowWalletConflict(show, [=] {
				if (weak) {
					weak->activate(show);
				}
			});
		} else {
			show->showToast(tr::lng_wallet_comment_unavailable(tr::now));
		}
		return;
	}
	_pending = true;
	const auto revision = ++_revision;
	_scope->cancelledChanges() | rpl::on_next([=] {
		if (weak && weak->_revision == revision) {
			weak->reset();
		}
	}, _attempt);
	const auto current = [=] {
		return weak && weak->attemptCurrent(revision);
	};
	_changes.fire({});
	if (!current()) {
		return;
	}
	// A key that has to come from the backup takes a while to fetch, and
	// neither the details box nor the message paints a pending state: the
	// busy box is that state. Cancelling it retires the scope, which drops
	// the fetch and keeps every later prompt of the ladder from opening. A
	// key held on this device asks for its unlock at once and needs none.
	const auto custody = _session->wallet().deviceCustodyState();
	if (custody.mode == DeviceMode::ReadOnlyRestorable) {
		_closeBusy = ShowWalletBusyBox(show, tr::lng_wallet_restoring_key(), [=] {
			if (weak && weak->_revision == revision && weak->_pending) {
				weak->reset();
			}
		});
	}
	AcquireTransferCommentKey(show, _scope, current, _attempt, [=](
			KeyAuthorization auth) {
		if (!current()) {
			if (weak && weak->_revision == revision) {
				weak->reset();
			}
			return;
		} else if (!auth.grant) {
			weak->reset();
			return;
		}
		const auto scope = weak->_scope;
		weak->_session->wallet().decryptComment(
			std::move(auth),
			scope,
			[=](CommentDecryptResult result) {
				if (weak) {
					weak->finish(revision, show, std::move(result));
				}
			});
	});
}

void TransferComment::finish(
		uint64 revision,
		std::shared_ptr<Main::SessionShow> show,
		CommentDecryptResult result) {
	const auto weak = base::make_weak(this);
	if (_revision != revision) {
		return;
	} else if (!attemptCurrent(revision)) {
		if (weak && weak->_revision == revision) {
			weak->reset();
		}
		return;
	}
	using Error = CommentDecryptError;
	if (result.error == Error::None) {
		_plaintext = std::move(result.text);
		_pending = false;
		if (const auto close = base::take(_closeBusy)) {
			close();
		}
		_changes.fire({});
		return;
	}
	reset();
	if (!weak || !originCurrent() || !show->valid()) {
		return;
	}
	switch (result.error) {
	case Error::None:
	case Error::Cancelled:
		break;
	case Error::Locked:
		show->showToast(VaultLockedText(_session.get()));
		break;
	case Error::Unavailable:
	case Error::Busy:
		show->showToast(tr::lng_wallet_comment_unavailable(tr::now));
		break;
	case Error::KeyUnreadable:
		show->showToast(tr::lng_wallet_comment_key_unreadable(tr::now));
		break;
	case Error::DecryptionFailed:
		show->showToast(tr::lng_wallet_comment_key_mismatch(tr::now));
		break;
	case Error::Failed:
		show->showToast(tr::lng_wallet_comment_decryption_failed(tr::now));
		break;
	}
}

void TransferComment::clear() {
	++_revision;
	_pending = false;
	_plaintext.reset();
	if (const auto close = base::take(_closeBusy)) {
		close();
	}
	const auto scope = base::take(_scope);
	auto attempt = base::take(_attempt);
	attempt.destroy();
	if (scope) {
		scope->cancel();
	}
}

void TransferComment::reset() {
	const auto weak = base::make_weak(this);
	const auto changed = _pending || _plaintext.has_value();
	clear();
	if (changed && weak) {
		weak->_changes.fire({});
	}
}

bool TransferComment::pending() const {
	return _pending;
}

const std::optional<QString> &TransferComment::plaintext() const {
	return _plaintext;
}

rpl::producer<> TransferComment::changes() const {
	return _changes.events();
}

} // namespace Wallet
