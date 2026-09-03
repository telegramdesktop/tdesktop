/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_key_protection.h"

#include "base/openssl_help.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "data/data_user.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/passcode_strength_meter.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/passcode_strength.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_session.h"

#include "styles/style_layers.h"
#include "styles/style_passcode_strength_meter.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

// A function-local static, so a provider registered from another translation
// unit's initializer cannot observe an unconstructed vector.
[[nodiscard]] std::vector<std::unique_ptr<ProtectionProvider>> &Providers() {
	static auto result = std::vector<std::unique_ptr<ProtectionProvider>>();
	return result;
}

// task.md asks for availability to be re-evaluated when the box opens, not
// while it is open: a row appearing or vanishing under the user's finger
// would move the selection. The local lifetime unsubscribes before the
// answer is used, so a provider that answers asynchronously reads as
// unavailable, which is the fail-closed verdict.
[[nodiscard]] bool AvailableNow(const ProtectionProvider &provider) {
	auto result = false;
	auto lifetime = rpl::lifetime();
	provider.available(
	) | rpl::take(1) | rpl::on_next([&](bool value) {
		result = value;
	}, lifetime);
	return result;
}

[[nodiscard]] Ui::PasscodeStrengthBand PasscodeBand(
		const SecureBytes &passcode) {
	auto text = QString::fromUtf8(
		reinterpret_cast<const char*>(passcode.span().data()),
		passcode.size());
	const auto cleanse = gsl::finally([&] {
		if (!text.isEmpty()) {
			OPENSSL_cleanse(text.data(), text.size() * sizeof(QChar));
		}
	});
	return Ui::EstimatePasscodeStrength(text).band;
}

[[nodiscard]] QString RemovalWalletNames(
		const std::vector<not_null<Main::Account*>> &accounts) {
	auto names = QStringList();
	for (const auto &account : accounts) {
		if (const auto session = account->maybeSession()) {
			names.push_back(session->user()->name());
		}
	}
	return names.join(u", "_q);
}

// Opens the vault key that the account's committed wrap seals: Passcode
// derives its wrap key from the typed bytes and Open from the wrap's own
// secret, while a kind at or above kFirstReservedVaultKind belongs to a
// registered provider and is opened by it. The answer arrives through a
// callback because that hand-off is asynchronous by contract.
//
// The provider branch cannot run in this build: ParseVaultHeader turns every
// reserved kind into Unsupported and ShowKeyProtectionBox refuses a header
// that does not read, so no hardware wrap ever reaches here. It is written
// for 2026/08/31/passcode-touch-id-unlock and
// 2026/08/31/passcode-windows-hello-unlock, each of which teaches
// wallet_vault.cpp's reader its own kind in the commit that registers its
// provider.
void AcquireVaultKey(
		not_null<Storage::Account*> local,
		const VaultWrap &wrap,
		const SecureBytes &passcode,
		Fn<void(std::optional<SecureBytes>)> done) {
	if (quint32(wrap.kind) >= kFirstReservedVaultKind) {
		const auto provider = ProtectionProviderFor(wrap.kind);
		if (!provider) {
			done(std::nullopt);
			return;
		}
		provider->unwrap(local, wrap, [done](ProtectionUnwrapResult result) {
			if (result.error != ProtectionError::None) {
				done(std::nullopt);
			} else {
				done(std::move(result.key));
			}
		});
		return;
	}
	auto utf8 = QByteArray(
		reinterpret_cast<const char*>(passcode.span().data()),
		passcode.size());
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
	});
	const auto wrapKey = DeriveVaultWrapKey(wrap, utf8);
	if (!wrapKey) {
		done(std::nullopt);
		return;
	}
	done(UnwrapVaultKey(wrap, *wrapKey));
}

// The passcode row's wrap. The UTF-8 the derivation reads is cleansed before
// this returns, so the only lasting copy of the typed bytes stays the box's.
[[nodiscard]] std::optional<VaultPreparedWrap> PreparePasscodeWrap(
		const SecureBytes &passcode) {
	auto utf8 = QByteArray(
		reinterpret_cast<const char*>(passcode.span().data()),
		passcode.size());
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
	});
	return PrepareVaultPasscodeWrap(utf8);
}

// One removal's whole walk, behind a shared_ptr because the key material in
// it is move-only and every continuation below has to fit in a copyable Fn.
// The accounts are weak: Main::Domain owns them and one can be logged out and
// dropped while the box that started the walk is open.
//
// Every listed vault is transitioned onto a copy of the one prepared wrap
// rather than onto a freshly prepared one of its own: VaultPreparedWrap is
// move-only, and preparing one per account would put PrepareVaultOpenWrap()
// behind call sites the warning box does not guard.
struct VaultRemovalWalk {
	std::vector<base::weak_ptr<Main::Account>> accounts;
	std::vector<not_null<Main::Account*>> changed;
	SecureBytes passcode;
	VaultPreparedWrap prepared;
	VaultHeader header;
	VaultKind kind = VaultKind::Passcode;
	Fn<void(KeyProtectionResult)> done;
	int index = 0;
};

// The one answer the walk gives, after the last account or at the first
// account it could not finish. Each vault key went out of scope with its own
// transition; the typed passcode and the prepared wrap key go here, before
// the caller hears anything.
void FinishVaultRemoval(
		const std::shared_ptr<VaultRemovalWalk> &walk,
		bool failed) {
	auto result = KeyProtectionResult{
		.cancelled = false,
		.failed = failed,
		.kind = walk->kind,
		.changed = std::move(walk->changed),
	};
	walk->passcode.clear();
	walk->prepared.wrapKey.clear();
	walk->done(std::move(result));
}

// One account at a time, with account N + 1 chained from account N's
// completion: acquiring a vault key is asynchronous by contract, so the list
// cannot be walked in a loop. The loop here only skips accounts that are gone.
void WalkVaultRemoval(std::shared_ptr<VaultRemovalWalk> walk) {
	Expects(walk != nullptr);

	while (walk->index < int(walk->accounts.size())) {
		const auto weak = walk->accounts[walk->index];
		++walk->index;
		const auto account = weak.get();
		if (!account) {
			// Logged out and dropped while the box was open: Main::Domain
			// no longer owns it and there is no vault left to transition.
			continue;
		}
		auto reading = ReadVaultHeader(account->local());
		if (reading.state != VaultReading::State::Read) {
			FinishVaultRemoval(walk, true);
			return;
		}
		walk->header = std::move(reading.header);
		const auto committed = walk->header.committedWrap();
		Assert(committed != nullptr);
		const auto acquired = [=](std::optional<SecureBytes> key) {
			const auto live = weak.get();
			if (!live) {
				WalkVaultRemoval(walk);
				return;
			} else if (!key) {
				FinishVaultRemoval(walk, true);
				return;
			}
			const auto result = TransitionVaultWrap(
				live->local(),
				walk->header,
				*key,
				VaultPreparedWrap{
					.wrap = walk->prepared.wrap,
					.wrapKey = walk->prepared.wrapKey.copy(),
				});
			key.reset();
			if (result != VaultTransitionResult::Done) {
				FinishVaultRemoval(walk, true);
				return;
			}
			walk->changed.push_back(live);
			WalkVaultRemoval(walk);
		};
		AcquireVaultKey(
			&account->local(),
			*committed,
			walk->passcode,
			acquired);
		return;
	}
	FinishVaultRemoval(walk, false);
}

// What the wallet-only passcode create box answers with. An empty passcode
// and failed == false is the box dismissed without creating anything; failed
// marks the half-failure below, where the passcode was created but the app
// lock could not be put back off.
struct WalletPasscodeCreated {
	SecureBytes passcode;
	bool failed = false;
};

// The passcode this box creates is created in the wallet-only role: the app
// lock is turned straight back off and neither the auto-lock nor the system
// unlock setting is touched.
void WalletPasscodeCreateBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(WalletPasscodeCreated)> done) {
	struct State {
		WalletPasscodeCreated result;
		bool finished = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();

	box->setTitle(tr::lng_wallet_protection_create_title());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_protection_create_about(),
			st::boxLabel),
		st::walletProtectionIntroMargin);

	const auto &fieldSt = st::settingLocalPasscodeInputField;
	const auto addField = [&](rpl::producer<QString> placeholder) {
		const auto wrap = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::walletProtectionCreateFieldMargin);
		wrap->resize(wrap->width(), fieldSt.heightMin);
		const auto field = Ui::CreateChild<Ui::PasswordInput>(
			wrap,
			fieldSt,
			std::move(placeholder));
		wrap->widthValue(
		) | rpl::on_next([=](int width) {
			field->moveToLeft((width - field->width()) / 2, 0);
		}, wrap->lifetime());
		return field;
	};
	const auto first = addField(tr::lng_wallet_protection_create_enter());
	const auto meter = box->addRow(
		object_ptr<Ui::PasscodeStrengthMeter>(
			box,
			st::defaultPasscodeStrengthMeter),
		st::walletProtectionMeterMargin);
	const auto second = addField(tr::lng_wallet_protection_create_confirm());
	const auto error = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::settingLocalPasscodeError),
		st::walletPasscodeErrorMargin,
		style::al_top);
	error->hide();
	const auto showError = [=](const QString &text) {
		error->show();
		error->setText(text);
	};
	QObject::connect(first, &Ui::MaskedInputField::changed, [=] {
		meter->showCandidate(first->text());
		error->hide();
	});
	QObject::connect(second, &Ui::MaskedInputField::changed, [=] {
		error->hide();
	});
	box->setFocusCallback([=] {
		first->setFocusFast();
	});

	const auto save = [=] {
		if (state->finished) {
			return;
		}
		const auto typed = first->text();
		// Exactly two refusals, an empty passcode and a mismatch: no length
		// rule and no band gates this box, the meter only advises.
		if (typed.isEmpty()) {
			first->setFocus();
			first->showError();
			return;
		} else if (typed != second->text()) {
			second->setFocus();
			second->showError();
			second->selectAll();
			showError(tr::lng_passcode_differ(tr::now));
			return;
		}
		auto utf8 = typed.toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		auto &local = show->session().domain().local();
		// The default verification token is accepted exactly because the
		// file carries no passcode wrap yet, so setPasscode() never reaches
		// its NeedsVerification branch here. Both writes zero the single-use
		// verification nonce in a gsl::finally, and neither depends on it -
		// there is nothing to prove at the first and the second takes the
		// enabled == false branch - so that zeroing is inert on this path.
		cSetPasscodeBadTries(0);
		const auto set = local.setPasscode(
			utf8,
			Storage::PasscodeVerification());
		if (set != Storage::SetPasscodeResult::Success) {
			first->setFocus();
			first->showError();
			showError(Lang::Hard::SecureSaveError());
			return;
		}
		// setPasscode() drops the open wrap while it creates a first
		// passcode, which turns the app lock on. This one exists for the
		// wallet alone, so it goes straight back off; that write asks for no
		// verification token, its enabled == false branch never reads one.
		const auto lock = local.setAppLockEnabled(false);
		if (lock != Storage::SetPasscodeResult::Success) {
			// A passcode exists now and Telegram will ask for it at launch.
			// That is not the role this box offered, so it is not reported
			// as a success and the chooser writes no vault under it.
			state->finished = true;
			state->result = { .failed = true };
			showError(tr::lng_wallet_protection_create_lock_error(tr::now));
			box->clearButtons();
			box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
			return;
		}
		Core::App().localPasscodeChanged();
		state->finished = true;
		state->result = { .passcode = SecureBytes(utf8) };
		box->closeBox();
	};
	const auto submit = [=] {
		if (second->hasFocus() || first->text().isEmpty()) {
			save();
		} else {
			second->setFocus();
		}
	};
	QObject::connect(first, &Ui::MaskedInputField::submitted, submit);
	QObject::connect(second, &Ui::MaskedInputField::submitted, submit);
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	// One answer on the way out, whatever closed the box: the created
	// passcode, the half-failure above, or nothing at all.
	box->boxClosing() | rpl::on_next([=] {
		if (state->reported) {
			return;
		}
		state->reported = true;
		if (done) {
			done(std::move(state->result));
		}
	}, box->lifetime());
}

// header carries the account's committed vault header in Switch and is empty
// in Install (there is no vault yet) and in Removal (there is one per listed
// account). passcode is taken by value and moved into the box's own state,
// so the box owns the only live copy of the typed bytes and they are
// cleansed with it on every close path.
void KeyProtectionBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		KeyProtectionArgs args,
		std::optional<VaultHeader> header,
		SecureBytes passcode) {
	// busy is the re-entry guard: a provider call and a sub-box are both
	// asynchronous, so a second Save while one is in flight would start a
	// second write of the same vault. header is the box's own copy of the
	// Switch header, which TransitionVaultWrap rewrites in place and which
	// must therefore outlive an asynchronous key acquisition.
	struct State {
		SecureBytes passcode;
		std::optional<VaultHeader> header;
		KeyProtectionResult result;
		bool busy = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();
	// State lives in the box's lifetime, so every continuation a provider
	// answers on - each of them asynchronous by contract - is guarded with the
	// box. A layer teardown while a system prompt is up (setupPasscodeLock()
	// hides every layer instantly) would otherwise leave them dereferencing
	// freed memory.
	const auto weak = base::make_weak(box);
	state->passcode = std::move(passcode);
	state->header = std::move(header);
	// A save that finished stashes its verdict and closes; boxClosing() is
	// the one place that answers the caller, so every dismissal - Cancel,
	// Escape, the layer being replaced - still reports the default
	// { .cancelled = true } and the caller persists nothing.
	const auto closeWith = [=](KeyProtectionResult result) {
		state->result = std::move(result);
		box->closeBox();
	};
	// Nothing was written, so the box stays open on the user's own choice.
	const auto refuse = [=] {
		state->busy = false;
		show->showToast(tr::lng_wallet_protection_error(tr::now));
	};
	// A write was attempted and could not be finished: the box closes and the
	// caller hears failed, so nothing downstream assumes the new kind.
	const auto fail = [=] {
		show->showToast(tr::lng_wallet_protection_error(tr::now));
		closeWith({ .cancelled = false, .failed = true });
	};
	const auto mode = args.mode;
	const auto removal = (mode == KeyProtectionMode::Removal);

	box->setTitle(tr::lng_wallet_protection_title());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			(removal
				? tr::lng_wallet_protection_removal_about
				: tr::lng_wallet_protection_about)(),
			st::boxLabel),
		st::walletProtectionIntroMargin);
	if (removal && args.accounts.size() > 1) {
		auto names = RemovalWalletNames(args.accounts);
		if (!names.isEmpty()) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_wallet_protection_removal_wallets(
						lt_accounts,
						rpl::single(std::move(names))),
					st::walletProtectionAboutLabel),
				st::walletProtectionIntroMargin);
		}
	}

	auto hardware = std::vector<not_null<ProtectionProvider*>>();
	for (const auto &provider : ProtectionProviders()) {
		if (AvailableNow(*provider)) {
			hardware.push_back(provider.get());
		}
	}
	const auto preselected = [&] {
		switch (mode) {
		case KeyProtectionMode::Install:
			return hardware.empty()
				? VaultKind::Passcode
				: hardware.front()->kind();
		case KeyProtectionMode::Switch: {
			const auto committed = state->header
				? state->header->committedWrap()
				: nullptr;
			return committed ? committed->kind : VaultKind::Passcode;
		}
		case KeyProtectionMode::Removal:
			return VaultKind::Passcode;
		}
		Unexpected("Mode in KeyProtectionBox.");
	}();
	const auto group = std::make_shared<Ui::RadioenumGroup<VaultKind>>(
		preselected);

	const auto addRow = [&](
			VaultKind kind,
			rpl::producer<QString> title,
			rpl::producer<QString> about,
			const style::Checkbox &checkboxSt) {
		box->addSkip(st::walletProtectionRowSkip);
		const auto row = box->addRow(object_ptr<Ui::VerticalLayout>(box));
		const auto radio = row->add(object_ptr<Ui::Radioenum<VaultKind>>(
			box,
			group,
			kind,
			QString(),
			checkboxSt));
		std::move(title) | rpl::on_next([=](const QString &text) {
			radio->setText(text);
		}, radio->lifetime());
		row->add(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(about),
				st::walletProtectionAboutLabel),
			st::walletProtectionAboutMargin);
		return row;
	};
	// The click helper covers the whole row, so it is created after the row's
	// last label: it must stay on top of them to receive their clicks.
	const auto makeClickable = [&](
			not_null<Ui::VerticalLayout*> row,
			VaultKind kind) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(row.get());
		row->sizeValue(
		) | rpl::on_next([=](QSize size) {
			button->resize(size);
		}, button->lifetime());
		button->setClickedCallback([=] { group->setValue(kind); });
	};

	for (const auto &provider : hardware) {
		const auto kind = provider->kind();
		const auto row = addRow(
			kind,
			provider->title(),
			provider->description(),
			st::defaultBoxCheckbox);
		row->add(
			object_ptr<Ui::FlatLabel>(
				box,
				provider->binding(),
				st::walletProtectionAboutLabel),
			st::walletProtectionBindingMargin);
		makeClickable(row, kind);
	}

	const auto passcodeRow = addRow(
		VaultKind::Passcode,
		(removal
			? tr::lng_wallet_protection_passcode_keep
			: tr::lng_wallet_protection_passcode)(),
		(removal
			? tr::lng_wallet_protection_passcode_keep_about
			: tr::lng_wallet_protection_passcode_about)(),
		st::defaultBoxCheckbox);
	if (state->passcode.empty()) {
		passcodeRow->add(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_protection_passcode_create(),
				st::walletProtectionAboutLabel),
			st::walletProtectionStrengthMargin);
	} else {
		// The band is a local: nothing derived from the typed passcode
		// reaches a member of anything that outlives the box.
		const auto band = PasscodeBand(state->passcode);
		const auto strength = passcodeRow->add(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_protection_passcode_current(
					lt_band,
					Ui::PasscodeStrengthBandName(band)),
				st::walletProtectionAboutLabel),
			st::walletProtectionStrengthMargin);
		rpl::single(rpl::empty) | rpl::then(
			style::PaletteChanged()
		) | rpl::on_next([=] {
			strength->setTextColorOverride(
				Ui::PasscodeStrengthBandColor(band)->c);
		}, strength->lifetime());
	}
	makeClickable(passcodeRow, VaultKind::Passcode);

	// The Open wording follows the app lock as it will be once this operation
	// is done: a Removal is the caller dropping the local passcode, so the
	// launch prompt is gone by then whatever it says now.
	const auto appLockAfter = !removal
		&& show->session().domain().local().appLockEnabled();
	const auto openRow = addRow(
		VaultKind::Open,
		tr::lng_wallet_protection_open(),
		(appLockAfter
			? tr::lng_wallet_protection_open_about_lock
			: tr::lng_wallet_protection_open_about_nolock)(),
		st::walletProtectionAttentionCheckbox);
	makeClickable(openRow, VaultKind::Open);

	// Held weakly from here on, for the same reason the walk holds them so:
	// Main::Domain owns the accounts and one can be logged out and dropped
	// between this box opening and its save finishing.
	auto weakAccounts = std::vector<base::weak_ptr<Main::Account>>();
	weakAccounts.reserve(args.accounts.size());
	for (const auto &account : args.accounts) {
		weakAccounts.push_back(base::make_weak(account));
	}

	// Where the three save paths converge: Install arms the prepared wrap as
	// the account's creation policy, Switch transitions this account's vault
	// onto it and Removal walks the caller's list of them.
	const auto apply = [=](VaultKind kind, VaultPreparedWrap prepared) {
		auto &session = show->session();
		switch (mode) {
		case KeyProtectionMode::Install: {
			// Nothing reaches the disk here: the caller's store writes the
			// header when it seals its first record under this policy.
			auto &vault = session.wallet().vault();
			vault.arm(std::move(prepared));
			closeWith({
				.cancelled = false,
				.kind = kind,
				.grant = vault.grant(),
			});
		} return;
		case KeyProtectionMode::Switch: {
			Assert(state->header.has_value());
			const auto committed = state->header->committedWrap();
			Assert(committed != nullptr);
			const auto retired = *committed;
			const auto next = std::make_shared<VaultPreparedWrap>(
				std::move(prepared));
			const auto local = &session.local();
			const auto acquired = [=](std::optional<SecureBytes> key) {
				if (!key) {
					refuse();
					return;
				}
				const auto result = TransitionVaultWrap(
					*local,
					*state->header,
					*key,
					std::move(*next));
				key.reset();
				if (result != VaultTransitionResult::Done) {
					// The primitive has already put the previous header
					// back, so the old kind still opens this vault.
					fail();
					return;
				}
				// The retiring provider is told only once the new wrap is
				// committed, so no failure above can retire the wrap that
				// is still the one opening this vault. Only a hardware kind
				// has a provider: the registry refuses every kind this
				// build defines itself.
				const auto old = ProtectionProviderFor(retired.kind);
				if (old) {
					old->remove(local, retired, [](ProtectionError) {});
				}
				closeWith({ .cancelled = false, .kind = kind });
			};
			AcquireVaultKey(
				local,
				retired,
				state->passcode,
				crl::guard(weak, acquired));
		} return;
		case KeyProtectionMode::Removal:
			// The walk copies the typed bytes because it outlives this
			// frame by contract; it cleanses that copy before it answers.
			WalkVaultRemoval(std::make_shared<VaultRemovalWalk>(
				VaultRemovalWalk{
					.accounts = weakAccounts,
					.passcode = state->passcode.copy(),
					.prepared = std::move(prepared),
					.kind = kind,
					.done = crl::guard(weak, closeWith),
				}));
			return;
		}
		Unexpected("Mode in KeyProtectionBox.");
	};
	// The bytes the passcode row protects the vault with: the ones typed at
	// the gate, or the ones the wallet-only create box has just made.
	const auto withPasscode = [=](const SecureBytes &bytes) {
		if (removal) {
			// Removal's row keeps the passcode the caller came to drop, in
			// the wallet-only role: the launch prompt goes off, no vault is
			// rewritten at all and every dependent wrap keeps opening.
			auto &local = show->session().domain().local();
			if (local.setAppLockEnabled(false)
				!= Storage::SetPasscodeResult::Success) {
				fail();
				return;
			}
			Core::App().localPasscodeChanged();
			closeWith({ .cancelled = false, .kind = VaultKind::Passcode });
			return;
		}
		auto prepared = PreparePasscodeWrap(bytes);
		if (!prepared) {
			refuse();
			return;
		}
		apply(VaultKind::Passcode, std::move(*prepared));
	};
	const auto savePasscode = [=] {
		if (!state->passcode.empty()) {
			withPasscode(state->passcode);
			return;
		}
		const auto created = [=](WalletPasscodeCreated result) {
			if (result.failed) {
				// A passcode exists now, but in the app-lock role this box
				// did not offer, so nothing is written under it here.
				closeWith({ .cancelled = false, .failed = true });
			} else if (result.passcode.empty()) {
				state->busy = false;
			} else {
				withPasscode(result.passcode);
			}
		};
		show->showBox(Box(WalletPasscodeCreateBox, show, created));
	};
	// The product's only call site of PrepareVaultOpenWrap(): this box is the
	// one surface that can put a vault under VaultKind::Open, and it prepares
	// that wrap only past the confirmation. Cancel returns to the chooser
	// with nothing prepared and nothing written.
	const auto saveOpen = [=] {
		show->showBox(Ui::MakeConfirmBox({
			.text = (appLockAfter
				? tr::lng_wallet_protection_open_warning_lock
				: tr::lng_wallet_protection_open_warning_nolock)(tr::now),
			.confirmed = [=](Fn<void()> close) {
				close();
				auto prepared = PrepareVaultOpenWrap();
				if (!prepared) {
					refuse();
					return;
				}
				apply(VaultKind::Open, std::move(*prepared));
			},
			.cancelled = [=](Fn<void()> close) {
				state->busy = false;
				close();
			},
			.confirmText = tr::lng_wallet_protection_open_confirm(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_wallet_protection_open_title(),
		}));
	};
	// The registered provider enrolls a wrap, and that wrap is what the outcome
	// above writes. No vault key is handed in: only the enrolled wrap key is
	// contractual, because every consumer re-seals the vault key under it and
	// overwrites the wrap's own blob.
	//
	// Nothing here runs in any build this task ships: no provider is
	// registered on any platform in it. 2026/08/31/passcode-touch-id-unlock
	// and 2026/08/31/passcode-windows-hello-unlock each register one, and
	// each of them must teach wallet_vault.cpp's reader its own kind - name
	// it in WrapIsWellFormed and lift it out of the reserved-kind branch of
	// ParseVaultHeader - in the same commit that registers it. Otherwise an
	// Install under that kind writes a header that afterwards reads as
	// Unsupported.
	const auto saveHardware = [=](VaultKind kind) {
		const auto provider = ProtectionProviderFor(kind);
		if (!provider) {
			refuse();
			return;
		}
		const auto local = &show->session().local();
		const auto enrolled = [=](ProtectionEnrollResult result) {
			if (result.error == ProtectionError::Cancelled) {
				// A dismissed system prompt changes nothing and leaves the
				// user in the box on the row they picked.
				state->busy = false;
			} else if (result.error != ProtectionError::None
				|| !result.wrap) {
				refuse();
			} else {
				apply(kind, std::move(*result.wrap));
			}
		};
		provider->enroll(local, crl::guard(weak, enrolled));
	};
	const auto save = [=] {
		if (state->busy) {
			return;
		}
		const auto kind = group->current();
		// In Switch the preselection is the kind the vault already carries,
		// so saving it writes nothing: what the caller wants is already
		// true. Removal's passcode row is not that case - it keeps the
		// vault's kind but still has the app lock to turn off.
		if (mode == KeyProtectionMode::Switch && kind == preselected) {
			closeWith({ .cancelled = false, .kind = kind });
			return;
		}
		state->busy = true;
		if (kind == VaultKind::Passcode) {
			savePasscode();
		} else if (kind == VaultKind::Open) {
			saveOpen();
		} else {
			saveHardware(kind);
		}
	};
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([state, done = args.done] {
		if (state->reported) {
			return;
		}
		state->reported = true;
		if (done) {
			done(std::move(state->result));
		}
	}, box->lifetime());
}

} // namespace

void RegisterProtectionProvider(
		std::unique_ptr<ProtectionProvider> provider) {
	Expects(provider != nullptr);

	const auto kind = provider->kind();
	if (quint32(kind) < kFirstReservedVaultKind) {
		LOG(("Wallet Error: a protection provider claims vault kind %1, "
			"which this build defines itself.").arg(quint32(kind)));
		return;
	} else if (ProtectionProviderFor(kind)) {
		LOG(("Wallet Error: a second protection provider claims vault "
			"kind %1.").arg(quint32(kind)));
		return;
	}
	Providers().push_back(std::move(provider));
}

auto ProtectionProviders()
-> const std::vector<std::unique_ptr<ProtectionProvider>> & {
	return Providers();
}

ProtectionProvider *ProtectionProviderFor(VaultKind kind) {
	const auto &list = Providers();
	const auto i = ranges::find_if(
		list,
		[&](const std::unique_ptr<ProtectionProvider> &provider) {
			return (provider->kind() == kind);
		});
	return (i != end(list)) ? i->get() : nullptr;
}

rpl::producer<QString> ProtectionLabel(
		VaultKind kind,
		bool appLockEnabled) {
	switch (kind) {
	case VaultKind::Passcode:
		return tr::lng_wallet_protection_label_passcode();
	case VaultKind::Open:
		return appLockEnabled
			? tr::lng_wallet_protection_label_open_lock()
			: tr::lng_wallet_protection_label_open_nolock();
	}
	if (const auto provider = ProtectionProviderFor(kind)) {
		return provider->title();
	}
	return tr::lng_wallet_vault_unavailable();
}

void ShowKeyProtectionBox(
		std::shared_ptr<Main::SessionShow> show,
		KeyProtectionArgs args) {
	auto &session = show->session();
	const auto report = [done = args.done](KeyProtectionResult result) {
		if (done) {
			done(std::move(result));
		}
	};
	auto header = std::optional<VaultHeader>();
	switch (args.mode) {
	case KeyProtectionMode::Install:
		// Install is asked for an account that has no vault yet; a header
		// that reads is the caller's contract broken, not a user error.
		if (ReadVaultHeader(session.local()).state
			== VaultReading::State::Read) {
			LOG(("Wallet Error: key protection asked to install over a vault "
				"this account already carries."));
			report({ .failed = true });
			return;
		}
		break;
	case KeyProtectionMode::Switch: {
		auto reading = ReadVaultHeader(session.local());
		if (reading.state != VaultReading::State::Read) {
			show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
			report({});
			return;
		}
		header = std::move(reading.header);
	} break;
	case KeyProtectionMode::Removal:
		for (const auto &account : args.accounts) {
			if (ReadVaultHeader(account->local()).state
				!= VaultReading::State::Read) {
				show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
				report({});
				return;
			}
		}
		break;
	}
	auto chooser = [=](SecureBytes passcode) mutable {
		show->showBox(Box(
			KeyProtectionBox,
			show,
			std::move(args),
			std::move(header),
			std::move(passcode)));
	};
	// The passcode comes first in every mode and whatever the vault's
	// retention window says: no branch here consults vault.retained().
	if (!session.domain().local().hasPasscode()) {
		chooser(SecureBytes());
		return;
	}
	show->showBox(Box(WalletPasscodeBox, WalletPasscodeBoxArgs{
		.show = show,
		.check = WalletPasscodeCheck::KeyDataAndVault,
		.passed = [chooser](WalletPasscodeGate gate) mutable {
			chooser(std::move(gate.passcode));
		},
		.cancelled = [report] { report({}); },
	}));
}

} // namespace Wallet
