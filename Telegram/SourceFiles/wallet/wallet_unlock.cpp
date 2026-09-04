/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_unlock.h"

#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "ui/layers/generic_box.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_session.h"

namespace Wallet {
namespace {

[[nodiscard]] VaultAuthorization Share(VaultGrant grant) {
	return grant.valid()
		? std::make_shared<VaultGrant>(std::move(grant))
		: nullptr;
}

// The runtime releases a key only through a grant's destructor, while
// unlockOpen(), unlockWithPasscode() and unlockWith() install one without
// minting a grant. So every successful unlock here is followed immediately
// by grant(): the handle that answers is what keeps the key alive, and its
// last copy going away is what cleanses it.
void UnlockByKind(
		std::shared_ptr<Main::SessionShow> show,
		VaultReading reading,
		bool ignoreRetention,
		Fn<void(VaultAuthorization)> done) {
	auto &session = show->session();
	auto &vault = session.wallet().vault();
	auto &local = session.local();
	const auto wrap = reading.header.committedWrap();
	Assert(wrap != nullptr);
	// A wrap that reads but does not open is the same dead end a Broken
	// header is, so it states the same line; a dismissed prompt states
	// nothing, because the user already knows what they answered.
	const auto unavailable = [show, done] {
		show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
		done(nullptr);
	};
	if (quint32(wrap->kind) >= kFirstReservedVaultKind) {
		const auto provider = ProtectionProviderFor(wrap->kind);
		if (!provider) {
			unavailable();
			return;
		}
		// The provider answers many main-thread turns after the ask, so a
		// clear trigger can land inside its prompt. The epoch is read here,
		// before the ask, and the runtime refuses a key that was opened
		// before that clear rather than undoing it.
		const auto epoch = vault.clearEpoch();
		provider->unwrap(&local, *wrap, [show, done, unavailable, epoch](
				ProtectionUnwrapResult result) {
			if (result.error == ProtectionError::Cancelled) {
				done(nullptr);
				return;
			} else if (result.error != ProtectionError::None || !result.key) {
				unavailable();
				return;
			}
			auto &runtime = show->session().wallet().vault();
			if (!runtime.unlockWith(std::move(*result.key), epoch)) {
				unavailable();
				return;
			}
			done(Share(runtime.grant()));
		});
	} else if (wrap->kind == VaultKind::Open) {
		if (vault.unlockOpen(local)) {
			done(Share(vault.grant()));
		} else {
			unavailable();
		}
	} else if (!ignoreRetention && vault.retained()) {
		done(Share(vault.grant()));
	} else {
		// A destroyed panel drops the box silently, and a box that was never
		// shown never emits boxClosing(), so neither passed nor cancelled
		// would ever run and the flow's own latch would stay set for the life
		// of the session. Fail closed instead of showing nothing.
		if (!show->valid()) {
			done(nullptr);
			return;
		}
		show->showBox(Box(WalletPasscodeBox, WalletPasscodeBoxArgs{
			.show = show,
			.check = WalletPasscodeCheck::Vault,
			.passed = [done](WalletPasscodeGate gate) {
				done(Share(std::move(gate.grant)));
			},
			.cancelled = [done] { done(nullptr); },
		}));
	}
}

} // namespace

bool KeyAuthorization::valid() const {
	return (grant != nullptr) || (install != nullptr);
}

void AcquireVaultUnlock(VaultUnlockArgs args) {
	Expects(args.show != nullptr);

	using State = VaultReading::State;
	const auto show = args.show;
	const auto mayInstall = args.mayInstall;
	const auto done = std::move(args.done);
	const auto answer = [=](KeyAuthorization result) {
		if (done) {
			done(std::move(result));
		}
	};
	// The installer travels beside the grant for every header state a read
	// was acquired for, not only for the one that has no vault: a store
	// resolved through it re-routes by kind at store time, which is what
	// keeps a passcode vault asked at install even when a retained window
	// had already handed this flow a prompt-free read grant.
	const auto acquired = [=](VaultAuthorization grant) {
		if (!grant) {
			answer({});
			return;
		}
		auto result = KeyAuthorization{ .grant = std::move(grant) };
		if (mayInstall) {
			result.install = MakeCustodyInstaller(show);
		}
		answer(std::move(result));
	};
	auto &session = show->session();
	auto reading = session.wallet().vault().reading(session.local());
	switch (reading.state) {
	case State::Read:
		UnlockByKind(show, std::move(reading), false, acquired);
		return;
	case State::Absent:
		if (mayInstall) {
			answer(KeyAuthorization{
				.install = MakeCustodyInstaller(show),
			});
		} else {
			show->showToast(VaultLockedText(&session));
			answer({});
		}
		return;
	case State::Broken:
	case State::Unsupported:
		show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
		answer({});
		return;
	}
	Unexpected("Vault reading state in AcquireVaultUnlock.");
}

CustodyInstaller MakeCustodyInstaller(
		std::shared_ptr<Main::SessionShow> show) {
	Expects(show != nullptr);

	// Only show and the caller's own callback are captured, so the ladder is
	// safe to invoke long after the box that acquired it is gone. The header
	// is re-read here because ShowKeyProtectionBox refuses Install over a
	// vault that already reads, and because the state may have moved between
	// the acquisition and the store.
	return [show](Fn<void(CustodyInstall)> ready) {
		using State = VaultReading::State;
		const auto answer = [ready](CustodyInstall install) {
			if (ready) {
				ready(std::move(install));
			}
		};
		// Every arm below ends in a box or a toast, and a destroyed panel
		// drops both silently, so an installer invoked after the wallet
		// window is gone would never answer and would strand the flow's
		// latch. Fail closed before anything is read.
		if (!show->valid()) {
			answer({});
			return;
		}
		auto &session = show->session();
		auto reading = session.wallet().vault().reading(session.local());
		switch (reading.state) {
		case State::Absent:
			ShowKeyProtectionBox(show, {
				.mode = KeyProtectionMode::Install,
				.done = [answer](KeyProtectionResult result) {
					auto grant = (!result.cancelled && !result.failed)
						? Share(std::move(result.grant))
						: nullptr;
					if (!grant) {
						answer({});
					} else {
						answer({ .grant = std::move(grant), .created = true });
					}
				},
			});
			return;
		case State::Read:
			UnlockByKind(
				show,
				std::move(reading),
				true,
				[answer](VaultAuthorization grant) {
					if (!grant) {
						answer({});
					} else {
						answer({ .grant = std::move(grant) });
					}
				});
			return;
		case State::Broken:
		case State::Unsupported:
			show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
			answer({});
			return;
		}
		Unexpected("Vault reading state in the custody installer.");
	};
}

QString VaultLockedText(not_null<Main::Session*> session) {
	return session->domain().local().hasPasscode()
		? tr::lng_wallet_vault_locked(tr::now)
		: tr::lng_wallet_vault_no_passcode(tr::now);
}

} // namespace Wallet
