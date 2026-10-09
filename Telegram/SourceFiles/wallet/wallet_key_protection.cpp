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
#include "core/core_settings.h"
#include "data/data_user.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_busy.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/passcode_strength_meter.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h"
#include "wallet/wallet_palette.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_unlock.h"

#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_passcode_strength_meter.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

[[nodiscard]] QByteArray Utf8Copy(const SecureBytes &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.span().data()),
		bytes.size());
}

struct LocalPasscodeChangeJob {
	Storage::PasscodeDerivation proof;
	Storage::PasscodeDerivation keyData;
	std::optional<VaultWrap> wrap;
	SecureBytes oldPasscode;
	SecureBytes newPasscode;
	std::optional<SecureBytes> key;
	std::optional<VaultPreparedWrap> prepared;

	void run();
};

void LocalPasscodeChangeJob::run() {
	proof.run();
	keyData.run();
	if (!wrap) {
		return;
	}
	auto oldUtf8 = Utf8Copy(oldPasscode);
	auto newUtf8 = Utf8Copy(newPasscode);
	const auto cleanse = gsl::finally([&] {
		if (!oldUtf8.isEmpty()) {
			OPENSSL_cleanse(oldUtf8.data(), oldUtf8.size());
		}
		if (!newUtf8.isEmpty()) {
			OPENSSL_cleanse(newUtf8.data(), newUtf8.size());
		}
		oldPasscode.clear();
		newPasscode.clear();
	});
	const auto wrapKey = DeriveVaultWrapKey(*wrap, oldUtf8);
	key = wrapKey ? UnwrapVaultKey(*wrap, *wrapKey) : std::nullopt;
	if (key) {
		prepared = PrepareVaultPasscodeWrap(newUtf8);
	}
}

} // namespace

void ChangeLocalPasscode(
		not_null<QObject*> guard,
		const SecureBytes &current,
		const QByteArray &updated,
		Fn<void(LocalPasscodeChangeResult)> done) {
	using Result = LocalPasscodeChangeResult;
	const auto domain = base::make_weak(&Core::App().domain());
	const auto &local = domain->local();
	const auto runtime = domain->walletKeyring().shared_from_this();
	const auto reading = runtime->reading();
	const auto epoch = runtime->clearEpoch();
	auto utf8 = Utf8Copy(current);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
	});
	auto job = LocalPasscodeChangeJob{
		.proof = local.prepareOpen(utf8),
		.keyData = local.prepareNewWrap(updated),
	};
	if (reading.state == KeyringReading::State::Read
		&& reading.keyring.wrap.kind == VaultKind::Passcode
		&& runtime->hasLiveEntries(reading.keyring)) {
		job.wrap = reading.keyring.wrap;
		job.oldPasscode = current.copy();
		job.newPasscode = SecureBytes(updated);
	}
	const auto weak = QPointer<QObject>(guard.get());
	Storage::DeriveOnWorker(
		std::move(job),
		crl::guard(guard.get(), [=](LocalPasscodeChangeJob &&job) {
			if (!domain) {
				return;
			}
			auto &local = domain->local();
			auto latest = runtime->reading();
			auto replacement = std::optional<DeviceKeyringRotation>();
			if (latest.state == KeyringReading::State::Read
				&& latest.keyring.wrap.kind == VaultKind::Passcode
				&& runtime->hasLiveEntries(latest.keyring)) {
				if (runtime->clearEpoch() != epoch
					|| !job.wrap || !job.key || !job.prepared
					|| !CurrentVaultWrap(*runtime, *job.wrap)) {
					done(Result::VaultFailed);
					return;
				}
				replacement = RotateDeviceKeyring(
					runtime->liveKeyring(std::move(latest.keyring)),
					*job.key,
					*job.prepared);
				if (!replacement) {
					done(Result::VaultFailed);
					return;
				}
			}
			const auto verification = local.verifyPasscode(std::move(job.proof));
			if (!verification) {
				done(Result::Stale);
				return;
			}
			// The complete replacement owns its keys independently of the UI.
			// key_data synchronously clears every old grant through its change
			// signal; that expected clear must not abort this write pair or
			// restore old authority. The application notification follows both
			// writes because its auto-lock fan-out may destroy the chooser.
			cSetPasscodeBadTries(0);
			const auto written = local.setPasscode(
				std::move(job.keyData),
				*verification);
			auto result = (written == Storage::SetPasscodeResult::NeedsVerification)
				? Result::Stale
				: Result::PasscodeFailed;
			if (written == Storage::SetPasscodeResult::Success) {
				const auto committed = !replacement
					|| (domain && WriteDeviceKeyring(
						domain->local(),
						replacement->keyring));
				result = committed ? Result::Done : Result::CommitFailed;
				if (replacement) {
					if (!committed) {
						LOG(("Wallet Error: passcode changed but the device "
							"keyring replacement failed."));
					}
					runtime->notifyProtectionChanged();
				}
				Core::App().localPasscodeChanged();
			}
			if (weak) {
				done(result);
			}
		}));
}

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

[[nodiscard]] QString RemovalWalletNames(
		const std::vector<base::weak_ptr<Main::Account>> &accounts) {
	auto names = QStringList();
	for (const auto &weak : accounts) {
		const auto account = weak.get();
		if (!account) {
			continue;
		} else if (const auto session = account->maybeSession()) {
			names.push_back(session->user()->name());
		}
	}
	return names.join(u", "_q);
}

// A passcode already verified for key_data must still open D's own wrap.
// The job copies the current wrap and scoped typed bytes, touches no store,
// and cleanses the bytes after the derivation. Its main-thread consumer
// checks the clear epoch and current factor before accepting that D.
struct VaultKeyAcquisition {
	VaultWrap wrap;
	SecureBytes passcode;
	std::optional<SecureBytes> key;

	void run();
};

void VaultKeyAcquisition::run() {
	auto utf8 = Utf8Copy(passcode);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
		passcode.clear();
	});
	if (const auto wrapKey = DeriveVaultWrapKey(wrap, utf8)) {
		key = UnwrapVaultKey(wrap, *wrapKey);
	}
}

// The passcode row's wrap as one worker job: the typed bytes as SecureBytes
// and the fresh wrap with its wrap key. run() cleanses the typed bytes on
// every exit; the wrap key lives in SecureBytes and dies with the job on the
// main thread, applied or not.
struct PasscodeWrapPreparation {
	SecureBytes passcode;
	std::optional<VaultPreparedWrap> prepared;

	void run();
};

void PasscodeWrapPreparation::run() {
	auto utf8 = Utf8Copy(passcode);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
		passcode.clear();
	});
	prepared = PrepareVaultPasscodeWrap(utf8);
}

// The passcode row's wrap, derived on the worker. The job takes its copy of
// the typed bytes before this returns, so a caller may pass bytes it does
// not own past this frame, and cleanses that copy on the worker: the only
// lasting copy of the typed bytes stays the box's.
//
// The wrap is worth sealing only while key_data still holds the passcode
// these bytes are. A change that lands before the wrap is armed - while the
// chooser is open, during this derivation, or while the confirmation of an
// unusable vault's reset holds the wrap - is refused by the chooser's
// passcodeChanged latch, asked on this call's answer and again on that
// confirmation. One that lands after the arm reaches VaultRuntime's
// synchronous clear() on localPasscodeChanged(), which drops the armed
// policy and moves the epoch the seal compares.
void PreparePasscodeWrap(
		const SecureBytes &passcode,
		Fn<void(std::optional<VaultPreparedWrap>)> done) {
	Storage::DeriveOnWorker(
		PasscodeWrapPreparation{ .passcode = passcode.copy() },
		[done](PasscodeWrapPreparation &&job) {
			done(std::move(job.prepared));
		});
}

// What the wallet-only passcode create box answers with. An empty passcode
// is the box closed without creating anything.
struct WalletPasscodeCreated {
	SecureBytes passcode;
	quint32 previousEpoch = 0;
	quint32 epoch = 0;
};

// What the passcode change box answers with. An empty passcode is the box
// closed without a change; stale says the bytes it was opened with no longer
// open key_data, or that a vault still opens with them, so the chooser that
// holds those bytes has to close as well.
struct WalletPasscodeChanged {
	SecureBytes passcode;
	bool stale = false;
};

// The fields both passcode boxes share: the passcode with its strength meter,
// its confirmation and the error line under them. validate() answers the
// typed passcode, or nothing once it has shown why it cannot be used. Exactly
// two refusals, an empty passcode and a mismatch: no length rule and no band
// gates these boxes, the meter only advises.
struct PasscodeFields {
	not_null<Ui::PasswordInput*> first;
	not_null<Ui::PasswordInput*> second;
	Fn<void(const QString &)> showError;
	Fn<std::optional<QString>()> validate;
};

[[nodiscard]] PasscodeFields AddPasscodeFields(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> enter,
		rpl::producer<QString> confirm) {
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
		return not_null(field);
	};
	const auto first = addField(std::move(enter));
	const auto meter = [&] {
		auto object = object_ptr<Ui::PasscodeStrengthMeter>(
			box,
			st::defaultPasscodeStrengthMeter);
		object->setNaturalWidth(fieldSt.width);
		return box->addRow(
			std::move(object),
			st::walletProtectionMeterMargin,
			style::al_top);
	}();
	const auto second = addField(std::move(confirm));
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
	const auto validate = [=]() -> std::optional<QString> {
		const auto typed = first->text();
		if (typed.isEmpty()) {
			first->setFocus();
			first->showError();
			return std::nullopt;
		} else if (typed != second->text()) {
			second->setFocus();
			second->showError();
			second->selectAll();
			showError(tr::lng_passcode_differ(tr::now));
			return std::nullopt;
		}
		return typed;
	};
	return {
		.first = first,
		.second = second,
		.showError = showError,
		.validate = validate,
	};
}

// Enter in the first field moves to the confirmation, which saves.
void SubmitPasscodeFields(const PasscodeFields &fields, Fn<void()> save) {
	const auto first = fields.first;
	const auto second = fields.second;
	const auto submit = [=] {
		if (second->hasFocus() || first->text().isEmpty()) {
			save();
		} else {
			second->setFocus();
		}
	};
	QObject::connect(first, &Ui::MaskedInputField::submitted, submit);
	QObject::connect(second, &Ui::MaskedInputField::submitted, submit);
}

// The passcode this box creates is created straight in the wallet-only role,
// through Storage::Domain::createPasscodeWithoutAppLock(): the app lock never
// turns on, and neither the auto-lock nor the system unlock setting is
// touched.
void WalletPasscodeCreateBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		quint32 epoch,
		Fn<bool()> current,
		Fn<void(WalletPasscodeCreated)> done) {
	struct State {
		WalletPasscodeCreated result;
		SecureBytes typed;
		QPointer<Ui::RoundButton> save;
		bool finished = false;
		bool busy = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto weak = base::make_weak(box);
	const auto weakSession = base::make_weak(&show->session());
	const auto runtime = show->session().wallet().vault().shared_from_this();

	box->setTitle(tr::lng_passcode_create_title());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_protection_create_about(),
			st::boxLabel),
		st::walletProtectionIntroMargin);

	const auto fields = AddPasscodeFields(
		box,
		tr::lng_passcode_enter_first(),
		tr::lng_passcode_confirm_new());
	const auto first = fields.first;
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		first->setDisabled(busy);
		fields.second->setDisabled(busy);
		Ui::SetButtonBusy(state->save.data(), busy);
		if (!busy) {
			first->setFocus();
		}
	};

	const auto save = [=] {
		if (state->finished || state->busy || !weakSession || !show->valid()) {
			return;
		}
		if (!current() || runtime->clearEpoch() != epoch) {
			box->closeBox();
			return;
		}
		const auto typed = fields.validate();
		if (!typed) {
			return;
		}
		auto utf8 = typed->toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		const auto &local = show->session().domain().local();
		state->typed = SecureBytes(utf8);
		cSetPasscodeBadTries(0);
		setBusy(true);
		// One derivation on the worker, the new passcode's key_data wrap, and
		// one logical write in the wallet-only role:
		// createPasscodeWithoutAppLock() keeps the verified open wrap, so the
		// launch lock never turns on and nothing is left half-created. That
		// entry checks again at the write itself that no passcode exists
		// yet: a passcode another window created while the derivation ran
		// is refused with NeedsVerification, and this box closes with nothing
		// created. That window's write has already set the chooser's
		// passcodeChanged latch, which turns the empty answer into a close.
		Storage::DeriveOnWorker(
			local.prepareNewWrap(utf8),
			crl::guard(box, [=](Storage::PasscodeDerivation &&derived) {
				if (!weakSession || !show->valid() || state->reported
					|| !current() || runtime->clearEpoch() != epoch) {
					box->closeBox();
					return;
				}
				auto &local = weakSession->domain().local();
				const auto set = local.createPasscodeWithoutAppLock(
					std::move(derived));
				if (!weak || !weakSession || state->reported) {
					return;
				}
				if (set == Storage::SetPasscodeResult::NeedsVerification) {
					state->typed.clear();
					box->closeBox();
					return;
				} else if (set != Storage::SetPasscodeResult::Success) {
					setBusy(false);
					state->typed.clear();
					first->setFocus();
					first->showError();
					fields.showError(Lang::Hard::SecureSaveError());
					return;
				}
				state->finished = true;
				auto result = WalletPasscodeCreated{
					.passcode = std::move(state->typed),
					.previousEpoch = epoch,
					.epoch = runtime->clearEpoch(),
				};
				Core::App().localPasscodeChanged();
				if (!weak || !weakSession || state->reported) {
					return;
				}
				if (result.epoch == runtime->clearEpoch()) {
					state->result = std::move(result);
				}
				box->closeBox();
			}));
	};
	SubmitPasscodeFields(fields, save);
	state->save = box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	// One answer on the way out, whatever closed the box: the created
	// passcode or nothing at all.
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

// Changes the passcode the chooser was opened with. current are the bytes the
// chooser's gate accepted; a change made elsewhere makes them stale, which an
// idle box answers by closing and a busy one learns from its own proof.
void WalletPasscodeChangeBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		SecureBytes current,
		Fn<void(WalletPasscodeChanged)> done) {
	struct State {
		SecureBytes current;
		SecureBytes typed;
		WalletPasscodeChanged result;
		QPointer<Ui::RoundButton> save;
		bool busy = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->current = std::move(current);

	box->setTitle(tr::lng_passcode_change());
	box->addSkip(st::walletProtectionRowSkip);
	const auto fields = AddPasscodeFields(
		box,
		tr::lng_passcode_enter_new(),
		tr::lng_passcode_confirm_new());
	const auto first = fields.first;
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		first->setDisabled(busy);
		fields.second->setDisabled(busy);
		Ui::SetButtonBusy(state->save.data(), busy);
		if (!busy) {
			first->setFocus();
		}
	};
	// The box's own write fires this too, while busy.
	show->session().domain().local().localPasscodeChanged(
	) | rpl::filter([=] {
		return !state->busy;
	}) | rpl::on_next([=] {
		state->result.stale = true;
		crl::on_main(box, [=] { box->closeBox(); });
	}, box->lifetime());

	const auto save = [=] {
		if (state->busy) {
			return;
		}
		const auto typed = fields.validate();
		if (!typed) {
			return;
		}
		auto utf8 = typed->toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		if (!bytes::compare(state->current.span(), bytes::make_span(utf8))) {
			first->setFocus();
			first->showError();
			first->selectAll();
			fields.showError(tr::lng_passcode_is_same(tr::now));
			return;
		}
		state->typed = SecureBytes(utf8);
		setBusy(true);
		using Result = LocalPasscodeChangeResult;
		ChangeLocalPasscode(box, state->current, utf8, [=](Result result) {
			switch (result) {
			case Result::Done:
				state->result = { .passcode = std::move(state->typed) };
				box->closeBox();
				return;
			case Result::CommitFailed:
				show->showToast(
					tr::lng_wallet_protection_passcode_changed_error(tr::now));
				[[fallthrough]];
			case Result::Stale:
				state->typed.clear();
				state->result.stale = true;
				box->closeBox();
				return;
			case Result::VaultFailed:
			case Result::PasscodeFailed:
				state->typed.clear();
				setBusy(false);
				first->showError();
				fields.showError((result == Result::VaultFailed)
					? tr::lng_wallet_protection_error(tr::now)
					: Lang::Hard::SecureSaveError());
				return;
			}
			Unexpected("Result in WalletPasscodeChangeBox.");
		});
	};
	SubmitPasscodeFields(fields, save);
	state->save = box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=] {
		if (state->reported) {
			return;
		}
		state->reported = true;
		state->current.clear();
		if (done) {
			done(std::move(state->result));
		}
	}, box->lifetime());
}

void RetireProtectionWrap(
		base::weak_ptr<Main::Account> account,
		base::weak_ptr<Main::Domain> domain,
		const VaultWrap &wrap) {
	const auto provider = ProtectionProviderFor(wrap.kind);
	if (!provider) {
		return;
	}
	auto context = account.get();
	if (!context) {
		if (const auto live = domain.get()) {
			if (!live->accounts().empty()) {
				context = live->accounts().front().account.get();
			}
		}
	}
	if (context) {
		provider->remove(&context->local(), wrap, [](ProtectionError) {});
	}
}

struct PreparedProtection {
	base::weak_ptr<Main::Account> account;
	base::weak_ptr<Main::Domain> domain;
	std::optional<VaultPreparedWrap> prepared;

	~PreparedProtection();
	void discard();
};

PreparedProtection::~PreparedProtection() {
	discard();
}

void PreparedProtection::discard() {
	if (const auto abandoned = base::take(prepared)) {
		RetireProtectionWrap(account, domain, abandoned->wrap);
	}
}

struct HardwareEnrollArgs {
	std::shared_ptr<Main::SessionShow> show;
	not_null<ProtectionProvider*> provider;
	Fn<void(ProtectionEnrollResult)> done;
	// A wrap the provider answered with after the box reported: nothing can
	// be enrolled any more, so it is only for the caller to retire.
	Fn<void(ProtectionEnrollResult)> abandoned;
};

// The enrolling half of the same layer the unlock box raises: the provider
// asks its sheet as this opens, a dismissed sheet leaves Retry standing, and
// every other answer is the caller's to read.
void HardwareEnrollBox(
		not_null<Ui::GenericBox*> box,
		HardwareEnrollArgs args) {
	struct State {
		rpl::variable<bool> asking = true;
		Fn<void()> ask;
		bool reported = false;
		bool busy = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto show = args.show;
	const auto provider = args.provider;
	const auto done = args.done;
	const auto abandoned = args.abandoned;
	const auto weak = base::make_weak(box);
	const auto weakSession = base::make_weak(&show->session());
	const auto retry = [=] { state->ask(); };
	SetupSystemPromptBox(box, provider, state->asking.value(), retry);
	state->ask = [=] {
		if (state->busy || !weakSession || !show->valid()) {
			return;
		}
		state->busy = true;
		state->asking = true;
		provider->enroll(&weakSession->local(), [=](
				ProtectionEnrollResult result) {
			if (!weak || state->reported) {
				abandoned(std::move(result));
				return;
			}
			state->busy = false;
			state->asking = false;
			if (result.error == ProtectionError::Cancelled) {
				return;
			}
			state->reported = true;
			box->closeBox();
			done(std::move(result));
		});
	};
	// A box shown over this one sends showFinished() again as it closes, so
	// only the first one asks: every later ask is the user's to make.
	box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
		state->ask();
	}, box->lifetime());
	box->boxClosing() | rpl::on_next([=] {
		if (!state->reported) {
			state->reported = true;
			done({ .error = ProtectionError::Cancelled });
		}
	}, box->lifetime());
}

void KeyProtectionBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		KeyProtectionArgs args,
		KeyringReading reading,
		SecureBytes passcode) {
	struct State {
		SecureBytes passcode;
		std::optional<VaultWrap> wrap;
		VaultAuthorization authorization;
		KeyProtectionResult result;
		std::vector<not_null<Ui::Radioenum<VaultKind>*>> radios;
		QPointer<Ui::RoundButton> save;
		std::shared_ptr<PreparedProtection> pendingInstall;
		std::optional<quint32> passcodeCreatedFromEpoch;
		quint32 epoch = 0;
		bool busy = false;
		bool passcodeChanged = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto weak = base::make_weak(box);
	const auto weakSession = base::make_weak(&show->session());
	const auto account = base::make_weak(&show->session().account());
	const auto domain = base::make_weak(&show->session().domain());
	state->passcode = std::move(passcode);
	if (reading.state == KeyringReading::State::Read) {
		state->wrap = std::move(reading.keyring.wrap);
	}
	state->epoch = show->session().wallet().vault().clearEpoch();
	const auto alive = [=] {
		return weak && weakSession && show->valid() && !state->reported
			&& weakSession->account().maybeSession() == weakSession.get();
	};
	const auto current = [=] {
		if (!alive()) {
			return false;
		}
		const auto &vault = weakSession->wallet().vault();
		return state->epoch == vault.clearEpoch()
			&& (state->wrap
				? CurrentVaultWrap(vault, *state->wrap)
				: !vault.hasLiveEntries());
	};
	const auto closeWith = [=](KeyProtectionResult result) {
		state->result = std::move(result);
		box->closeBox();
	};
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		for (const auto &radio : state->radios) {
			radio->setDisabled(busy);
		}
		Ui::SetButtonBusy(state->save.data(), busy);
		if (!busy && state->passcodeChanged) {
			crl::on_main(box, [=] { box->closeBox(); });
		}
	};
	const auto refuse = [=] {
		state->authorization.reset();
		setBusy(false);
		show->showToast(tr::lng_wallet_protection_error(tr::now));
	};
	const auto fail = [=] {
		show->showToast(tr::lng_wallet_protection_error(tr::now));
		closeWith({ .cancelled = false, .failed = true });
	};
	show->session().domain().local().localPasscodeChanged(
	) | rpl::on_next([=] {
		state->passcodeChanged = true;
		if (!state->busy) {
			crl::on_main(box, [=] { box->closeBox(); });
		}
	}, box->lifetime());
	const auto mode = args.mode;
	const auto resetUnusableVault = args.resetUnusableVault;
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
		if (AvailableNow(*provider)
			|| (mode == KeyProtectionMode::Change
				&& state->wrap && state->wrap->kind == provider->kind())) {
			hardware.push_back(provider.get());
		}
	}
	const auto preselected = [&] {
		switch (mode) {
		case KeyProtectionMode::Install:
			return hardware.empty()
				? VaultKind::Passcode
				: hardware.front()->kind();
		case KeyProtectionMode::Change:
			return state->wrap
				? state->wrap->kind
				: VaultKind::Passcode;
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
		state->radios.push_back(radio);
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
		button->setClickedCallback([=] {
			if (!state->busy) {
				group->setValue(kind);
			}
		});
	};

	for (const auto &provider : hardware) {
		const auto kind = provider->kind();
		const auto row = addRow(
			kind,
			provider->title(),
			provider->description(),
			st::defaultBoxCheckbox);
		makeClickable(row, kind);
	}

	const auto passcodeRow = addRow(
		VaultKind::Passcode,
		(removal
			? tr::lng_wallet_protection_passcode_keep
			: tr::lng_wallet_protection_passcode)(),
		tr::lng_wallet_protection_passcode_about(),
		st::defaultBoxCheckbox);
	makeClickable(passcodeRow, VaultKind::Passcode);
	// The passcode exists independently of whether this box already holds
	// verified bytes. The link remains available before lazy verification,
	// only for a normal protection change, and stays above the row's click
	// helper so it keeps its own action while the passcode row is selected.
	const auto changeLink = (mode == KeyProtectionMode::Change
		&& show->session().domain().local().hasPasscode())
		? passcodeRow->add(object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			passcodeRow,
			object_ptr<Ui::FlatLabel>(
				passcodeRow,
				tr::lng_wallet_protection_passcode_change(
					lt_arrow,
					rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
					tr::link),
				st::walletProtectionAboutLabel),
			st::walletProtectionChangeMargin))
		: nullptr;
	if (changeLink) {
		changeLink->toggleOn(group->value() | rpl::map([](VaultKind kind) {
			return (kind == VaultKind::Passcode);
		}));
		changeLink->finishAnimating();
	}

	const auto openRow = addRow(
		VaultKind::Open,
		tr::lng_wallet_protection_open(),
		tr::lng_wallet_protection_open_about(),
		st::walletProtectionAttentionCheckbox);
	makeClickable(openRow, VaultKind::Open);

	const auto apply = [=](VaultKind kind, VaultPreparedWrap prepared) {
		const auto next = std::make_shared<PreparedProtection>();
		next->account = account;
		next->domain = domain;
		next->prepared = std::move(prepared);
		if (!current() || state->passcodeChanged) {
			if (alive()) {
				box->closeBox();
			}
			return;
		}
		auto &vault = weakSession->wallet().vault();
		if (mode == KeyProtectionMode::Install) {
			const auto arm = [=] {
				if (!alive()) {
					return;
				}
				auto prepared = base::take(next->prepared);
				auto grant = weakSession->wallet().vault().arm(
					weakSession->uniqueId(),
					std::move(*prepared));
				if (!grant.valid()) {
					refuse();
					return;
				}
				closeWith({
					.cancelled = false,
					.kind = kind,
					.grant = std::move(grant),
					.passcodeCreatedFromEpoch = state->passcodeCreatedFromEpoch,
				});
			};
			if (!resetUnusableVault) {
				const auto latest = vault.reading();
				if (vault.hasLiveEntries()
					&& (latest.state != KeyringReading::State::Read
						|| latest.keyring.wrap.kind != VaultKind::Open)) {
					refuse();
					return;
				}
				arm();
				return;
			}
			state->pendingInstall = next;
			show->showBox(Ui::MakeConfirmBox({
				.text = tr::lng_wallet_vault_reset_about(tr::now),
				.confirmed = crl::guard(weak, [=](Fn<void()> close) {
					close();
					if (!current() || state->passcodeChanged) {
						if (alive()) {
							box->closeBox();
						}
						return;
					}
					const auto resetEpoch = quint32(state->epoch + 1);
					const auto resetDone = [=](bool reset) {
						if (!alive()) {
							return;
						} else if (!reset
							|| weakSession->wallet().vault().clearEpoch() != resetEpoch) {
							box->closeBox();
							return;
						}
						state->epoch = resetEpoch;
						state->wrap.reset();
						arm();
					};
					resetUnusableVault(state->passcodeCreatedFromEpoch, resetDone);
				}),
				.cancelled = crl::guard(weak, [=](Fn<void()> close) {
					close();
					if (alive()) {
						box->closeBox();
					}
				}),
				.confirmText = tr::lng_wallet_passcode_forgot_confirm(),
				.confirmStyle = &st::attentionBoxButton,
				.title = tr::lng_wallet_vault_reset_title(),
			}));
			return;
		}
		auto latest = vault.reading();
		if (latest.state != KeyringReading::State::Read
			|| weakSession->wallet().custodyBusy()) {
			refuse();
			return;
		}
		const auto retired = latest.keyring.wrap;
		auto live = vault.liveKeyring(std::move(latest.keyring));
		auto key = vault.keyForRead(weakSession->uniqueId(), state->epoch);
		if (!live.entries.empty()
			&& (!state->authorization
				|| !state->authorization->valid()
				|| !key)) {
			refuse();
			return;
		}
		const auto deviceKey = key ? std::move(*key) : SecureBytes();
		const auto rotation = RotateDeviceKeyring(
			live,
			deviceKey,
			*next->prepared);
		if (!rotation) {
			refuse();
			return;
		} else if (!WriteDeviceKeyring(
				weakSession->domain().local(),
				rotation->keyring)) {
			fail();
			return;
		}
		next->prepared.reset();
		vault.clear();
		vault.notifyProtectionChanged();
		RetireProtectionWrap(account, domain, retired);
		closeWith({ .cancelled = false, .kind = kind });
		if (mode == KeyProtectionMode::Change) {
			DropUnusedPasscode();
		}
	};
	const auto savePasscode = [=] {
		if (removal) {
			MintVerificationOnWorker(box, state->passcode, [=](
					std::optional<Storage::PasscodeVerification> verification) {
				if (!current() || state->passcodeChanged) {
					if (alive()) {
						box->closeBox();
					}
					return;
				}
				auto &local = weakSession->domain().local();
				const auto result = verification
					? local.setAppLockEnabled(false, *verification)
					: Storage::SetPasscodeResult::NeedsVerification;
				if (result != Storage::SetPasscodeResult::Success) {
					if (alive()) {
						closeWith({ .cancelled = false, .failed = true });
					}
					return;
				}
				Core::App().localPasscodeChanged();
				if (alive()) {
					closeWith({ .cancelled = false, .kind = VaultKind::Passcode });
				}
			});
			return;
		}
		PreparePasscodeWrap(state->passcode, crl::guard(weak, [=](
				std::optional<VaultPreparedWrap> prepared) {
			if (!current() || state->passcodeChanged) {
				if (alive()) {
					box->closeBox();
				}
			} else if (!prepared) {
				refuse();
			} else {
				apply(VaultKind::Passcode, std::move(*prepared));
			}
		}));
	};
	const auto saveOpen = [=] {
		const auto appLockAfter = !removal
			&& weakSession->domain().local().appLockEnabled();
		show->showBox(Ui::MakeConfirmBox({
			.text = (appLockAfter
				? tr::lng_wallet_protection_open_warning_lock
				: tr::lng_wallet_protection_open_warning_nolock)(tr::now),
			.confirmed = crl::guard(weak, [=](Fn<void()> close) {
				close();
				if (!current()) {
					if (alive()) {
						box->closeBox();
					}
					return;
				}
				auto prepared = PrepareVaultOpenWrap();
				if (!prepared) {
					refuse();
					return;
				}
				apply(VaultKind::Open, std::move(*prepared));
			}),
			.cancelled = crl::guard(weak, [=](Fn<void()> close) {
				close();
				if (alive()) {
					state->authorization.reset();
					setBusy(false);
				}
			}),
			.confirmText = tr::lng_wallet_protection_open_confirm(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_wallet_protection_open_title(),
		}));
	};
	const auto saveHardware = [=](VaultKind kind) {
		const auto provider = ProtectionProviderFor(kind);
		if (!provider) {
			refuse();
			return;
		}
		const auto enrolled = [=](ProtectionEnrollResult result) {
			if (!current() || state->passcodeChanged
				|| result.error != ProtectionError::None
				|| !result.wrap) {
				if (result.wrap) {
					RetireProtectionWrap(account, domain, result.wrap->wrap);
				}
				if (!alive()) {
					return;
				} else if (!current() || state->passcodeChanged) {
					box->closeBox();
				} else if (result.error == ProtectionError::Cancelled) {
					state->authorization.reset();
					setBusy(false);
				} else {
					refuse();
				}
				return;
			}
			apply(kind, std::move(*result.wrap));
		};
		show->showBox(Box(HardwareEnrollBox, HardwareEnrollArgs{
			.show = show,
			.provider = provider,
			.done = enrolled,
			.abandoned = [=](ProtectionEnrollResult result) {
				if (result.wrap) {
					RetireProtectionWrap(account, domain, result.wrap->wrap);
				}
			},
		}));
	};
	const auto prepare = [=](VaultKind kind) {
		if (!current() || state->passcodeChanged) {
			if (alive()) {
				box->closeBox();
			}
			return;
		} else if (kind == VaultKind::Passcode) {
			savePasscode();
		} else if (kind == VaultKind::Open) {
			saveOpen();
		} else {
			saveHardware(kind);
		}
	};
	const auto acquire = [=](VaultKind kind) {
		if (!current() || state->passcodeChanged) {
			if (alive()) {
				box->closeBox();
			}
			return;
		}
		auto &vault = weakSession->wallet().vault();
		if (mode == KeyProtectionMode::Install
			|| !vault.hasLiveEntries()
			|| (removal && kind == VaultKind::Passcode)) {
			prepare(kind);
			return;
		}
		const auto acquired = [=](VaultAuthorization authorization) {
			if (!current()) {
				if (alive()) {
					box->closeBox();
				}
				return;
			} else if (!authorization || !authorization->valid()) {
				setBusy(false);
				return;
			}
			state->authorization = std::move(authorization);
			prepare(kind);
		};
		if (state->wrap->kind == VaultKind::Passcode
			&& !state->passcode.empty()
			&& !vault.retained()) {
			Storage::DeriveOnWorker(
				VaultKeyAcquisition{
					.wrap = *state->wrap,
					.passcode = state->passcode.copy(),
				},
				crl::guard(weak, [=](VaultKeyAcquisition &&job) {
					if (!current()) {
						if (alive()) {
							box->closeBox();
						}
						return;
					}
					auto &vault = weakSession->wallet().vault();
					if (!job.key
						|| !vault.unlockWith(std::move(*job.key), state->epoch)) {
						refuse();
						return;
					}
					acquired(std::make_shared<VaultGrant>(
						vault.grant(weakSession->uniqueId())));
				}));
		} else {
			AcquireVaultUnlock({
				.show = show,
				.done = crl::guard(weak, [=](KeyAuthorization auth) {
					acquired(std::move(auth.grant));
				}),
			});
		}
	};
	const auto ensurePasscode = [=](Fn<void()> done) {
		if (!state->passcode.empty()) {
			done();
		} else if (weakSession->domain().local().hasPasscode()) {
			show->showBox(Box(WalletPasscodeBox, WalletPasscodeBoxArgs{
				.show = show,
				.check = WalletPasscodeCheck::KeyData,
				.passed = crl::guard(weak, [=](WalletPasscodeGate result) {
					if (!current()) {
						if (alive()) {
							box->closeBox();
						}
						return;
					}
					state->passcode = std::move(result.passcode);
					done();
				}),
				.cancelled = crl::guard(weak, [=] {
					if (alive()) {
						setBusy(false);
					}
				}),
			}));
		} else {
			show->showBox(Box(
				WalletPasscodeCreateBox,
				show,
				state->epoch,
				current,
				crl::guard(weak, [=](WalletPasscodeCreated result) {
					if (!weakSession || state->reported
						|| weakSession->account().maybeSession() != weakSession.get()) {
						return;
					} else if (result.passcode.empty()) {
						if (alive()) {
							setBusy(false);
						}
						return;
					}
					auto &vault = weakSession->wallet().vault();
					if (result.previousEpoch != state->epoch
						|| result.epoch != quint32(result.previousEpoch + 1)
						|| result.epoch != vault.clearEpoch()
						|| (state->wrap
							? !CurrentVaultWrap(vault, *state->wrap)
							: vault.hasLiveEntries())) {
						box->closeBox();
						return;
					}
					// Creating key_data clears the owning comment scope's epoch.
					// Hand off just that checked transition before alive() asks
					// the show to validate it. The owner also accepts this closed
					// prompt before wrap derivation yields; cancellation and an
					// unrelated clear must still retire the original attempt.
					if (args.passcodeCreated
						&& !args.passcodeCreated(result.previousEpoch, result.epoch)) {
						if (weak && !state->reported) {
							box->closeBox();
						}
						return;
					} else if (!alive()) {
						return;
					}
					state->epoch = result.epoch;
					state->passcodeCreatedFromEpoch = result.previousEpoch;
					state->passcodeChanged = false;
					state->passcode = std::move(result.passcode);
					done();
				})));
		}
	};
	const auto save = [=] {
		if (state->busy || !alive()) {
			return;
		}
		const auto kind = group->current();
		if (mode == KeyProtectionMode::Change && kind == preselected) {
			closeWith({ .cancelled = false, .kind = kind });
			return;
		} else if (!current() || state->passcodeChanged) {
			box->closeBox();
			return;
		}
		setBusy(true);
		if (kind == VaultKind::Passcode) {
			ensurePasscode([=] { acquire(kind); });
		} else {
			acquire(kind);
		}
	};
	if (changeLink) {
		changeLink->entity()->overrideLinkClickHandler([=] {
			if (state->busy || !current()) {
				return;
			}
			setBusy(true);
			ensurePasscode([=] {
				show->showBox(Box(
					WalletPasscodeChangeBox,
					show,
					state->passcode.copy(),
					crl::guard(weak, [=](WalletPasscodeChanged result) {
						if (!alive()) {
							return;
						} else if (result.stale) {
							box->closeBox();
							return;
						} else if (result.passcode.empty()) {
							setBusy(false);
							return;
						}
						auto &vault = weakSession->wallet().vault();
						auto reading = vault.reading();
						if (reading.state != KeyringReading::State::Read) {
							box->closeBox();
							return;
						}
						state->wrap = std::move(reading.keyring.wrap);
						state->epoch = vault.clearEpoch();
						state->passcode = std::move(result.passcode);
						state->passcodeChanged = false;
						setBusy(false);
						save();
					})));
			});
		});
	}
	state->save = box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	SubmitBoxOnEnter(box, save);
	box->boxClosing(
	) | rpl::on_next([state, done = args.done] {
		if (state->reported) {
			return;
		}
		state->reported = true;
		if (state->pendingInstall) {
			state->pendingInstall->discard();
			state->pendingInstall.reset();
		}
		state->authorization.reset();
		state->passcode.clear();
		if (done) {
			done(std::move(state->result));
		}
	}, box->lifetime());
}

} // namespace

rpl::producer<QString> ProtectionProvider::label() const {
	return title();
}

void RegisterProtectionProvider(
		std::unique_ptr<ProtectionProvider> provider) {
	Expects(provider != nullptr);

	const auto kind = provider->kind();
	if (quint32(kind) < kFirstReservedVaultKind) {
		LOG(("Wallet Error: a protection provider claims vault kind %1, "
			"which this build opens without a provider.").arg(quint32(kind)));
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

void MintVerificationOnWorker(
		not_null<QObject*> guard,
		const SecureBytes &passcode,
		Fn<void(std::optional<Storage::PasscodeVerification>)> done) {
	auto utf8 = Utf8Copy(passcode);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
	});
	const auto &local = Core::App().domain().local();
	Storage::DeriveOnWorker(local.prepareOpen(utf8), crl::guard(guard, [done](
			Storage::PasscodeDerivation &&derived) {
		auto &local = Core::App().domain().local();
		done(local.verifyPasscode(std::move(derived)));
	}));
}

bool CurrentVaultWrap(const VaultRuntime &vault, const VaultWrap &wrap) {
	const auto reading = vault.reading();
	if (reading.state != KeyringReading::State::Read) {
		return false;
	}
	const auto &current = reading.keyring.wrap;
	return current.kind == wrap.kind
		&& current.kdf.kind == wrap.kdf.kind
		&& current.kdf.memory == wrap.kdf.memory
		&& current.kdf.time == wrap.kdf.time
		&& current.kdf.parallel == wrap.kdf.parallel
		&& current.salt == wrap.salt
		&& current.openSecret == wrap.openSecret
		&& current.blob == wrap.blob;
}

void SubmitBoxOnEnter(not_null<Ui::GenericBox*> box, Fn<void()> submit) {
	box->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return;
		}
		const auto key = static_cast<QKeyEvent*>(e.get());
		if (!key->isAutoRepeat()
			&& (key->key() == Qt::Key_Enter
				|| key->key() == Qt::Key_Return)) {
			submit();
		}
	}, box->lifetime());
}

void SetupSystemPromptBox(
		not_null<Ui::GenericBox*> box,
		not_null<ProtectionProvider*> provider,
		rpl::producer<bool> asking,
		Fn<void()> retry) {
	box->setTitle(provider->title());

	const auto &loading = st::walletUnlockPromptLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto content = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, side),
		st::walletUnlockPromptPadding);
	// The kind is what the sheet will look like, and only platform code
	// could answer this from the provider itself.
	const auto icon = (provider->kind() == VaultKind::WindowsHello)
		? &st::menuIconWinHello
		: (provider->kind() == VaultKind::TouchId)
		? &st::menuIconTouchID
		: nullptr;
	if (icon) {
		content->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(content);
			icon->paintInCenter(p, content->rect());
		}, content->lifetime());
	}
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		content,
		side,
		&loading);
	Info::Statistics::AddChildToWidgetCenter(content, indicator);

	auto shared = std::move(asking) | rpl::start_spawning(box->lifetime());
	indicator->showOn(rpl::duplicate(shared));
	std::move(shared) | rpl::on_next([=](bool asking) {
		box->clearButtons();
		if (!asking) {
			box->addButton(
				tr::lng_bot_download_retry(),
				retry);
		}
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}, box->lifetime());
	SubmitBoxOnEnter(box, retry);
}

void ShowKeyProtectionBox(
		std::shared_ptr<Main::SessionShow> show,
		KeyProtectionArgs args,
		SecureBytes verified) {
	if (!show->valid()) {
		if (args.done) {
			args.done({});
		}
		return;
	}
	auto &session = show->session();
	auto &vault = session.wallet().vault();
	auto reading = vault.reading();
	const auto live = reading.state == KeyringReading::State::Read
		&& vault.hasLiveEntries(reading.keyring);
	if ((args.mode == KeyProtectionMode::Install
			&& live
			&& reading.keyring.wrap.kind != VaultKind::Open
			&& !args.resetUnusableVault)
		|| (args.mode != KeyProtectionMode::Install
			&& reading.state != KeyringReading::State::Read)) {
		show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
		if (args.done) {
			args.done({});
		}
		return;
	}
	show->showBox(Box(
		KeyProtectionBox,
		show,
		std::move(args),
		std::move(reading),
		std::move(verified)));
}

std::optional<VaultKind> LiveKeyProtection() {
	const auto &runtime = Core::App().domain().walletKeyring();
	const auto reading = runtime.reading();
	return (reading.state == KeyringReading::State::Read
		&& runtime.hasLiveEntries(reading.keyring))
		? std::make_optional(reading.keyring.wrap.kind)
		: std::nullopt;
}

} // namespace Wallet
