/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_gram_reconcile.h"

#include "base/weak_ptr.h"
#include "main/main_session.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_unlock.h"
#include "wallet/wallet_vault.h"
#include "settings.h"
#include "wallet_engine.hpp"

#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>

#include <atomic>

namespace Test {
namespace {

namespace engine = wallet_engine;

constexpr auto kKeySize = Wallet::kCustodyPublicKeySize;
constexpr auto kReconcileTimeout = crl::time(60000);
constexpr auto kAnchorHalf = 12;

// Every call the harness-owned lifecycle makes into its host. The row
// prints it: ton_connect_account() is documented to read no secret, so
// anything but 0 means that assumption broke and the run must stop.
std::atomic<int> HostCalls = 0;

struct PhraseKeys {
	QByteArray anchor;
	QByteArray signing;
	bool ok = false;
};

struct AddressCheck {
	bool derives = false;
	bool failed = false;
};

struct Derivation {
	PhraseKeys keys;
	AddressCheck judged;
	AddressCheck real;
};

enum class Route {
	None,
	CaseA,
	CaseB,
};

struct Flow {
	base::weak_ptr<Main::Session> session;
	GramReconcileDrive drive;
	GramReconcileResult result;
	std::vector<QString> fileWords;
	QByteArray servedKey;
	QByteArray realServedKey;
	QString servedAddress;
	QString realServedAddress;
	QByteArray currentAnchor;
	QByteArray currentSigning;
	QString fileNote;
	bool preV4Unresolved = false;
	bool realFileQualifies = false;
	bool fileAnchorMatchesCurrent = false;
	Route route = Route::None;
	Wallet::VaultAuthorization grant;
	std::shared_ptr<QString> step;
	Fn<void(GramReconcileResult)> done;
	bool finished = false;
};

// A host that refuses every call, the way wallet_engine.cpp's HostFailed
// throws a typed host error. The reconciliation's lifecycle only derives
// addresses, which reads no secret and writes no journal.
class RefusingHost final : public engine::WalletPlatformHost {
public:
	std::vector<uint8_t> read_protected_secret(
		const engine::ProtectedSecretRead &request) override;
	void store_protected_secret(
		const engine::ProtectedSecretStore &request) override;
	void delete_protected_secret(
		const engine::ProtectedSecretRef &secret_ref) override;
	std::optional<engine::JournalRecord> load_journal(
		const engine::JournalKey &key) override;
	engine::JournalCompareExchangeResult compare_exchange_journal(
		const engine::JournalCompareExchange &mutation) override;

};

[[nodiscard]] engine::protected_secret_host_error::Failed SecretRefused() {
	++HostCalls;
	const auto diagnostic = std::string("gram reconcile host refuses");
	auto result = engine::protected_secret_host_error::Failed(diagnostic);
	result.kind = engine::ProtectedSecretHostErrorKind::kPolicyViolation;
	result.diagnostic = diagnostic;
	return result;
}

[[nodiscard]] engine::journal_host_error::Failed JournalRefused() {
	++HostCalls;
	const auto diagnostic = std::string("gram reconcile host refuses");
	auto result = engine::journal_host_error::Failed(diagnostic);
	result.kind = engine::JournalHostErrorKind::kOther;
	result.diagnostic = diagnostic;
	return result;
}

std::vector<uint8_t> RefusingHost::read_protected_secret(
		const engine::ProtectedSecretRead &) {
	throw SecretRefused();
}

void RefusingHost::store_protected_secret(
		const engine::ProtectedSecretStore &) {
	throw SecretRefused();
}

void RefusingHost::delete_protected_secret(
		const engine::ProtectedSecretRef &) {
	throw SecretRefused();
}

std::optional<engine::JournalRecord> RefusingHost::load_journal(
		const engine::JournalKey &) {
	throw JournalRefused();
}

engine::JournalCompareExchangeResult RefusingHost::compare_exchange_journal(
		const engine::JournalCompareExchange &) {
	throw JournalRefused();
}

// The product's lifecycle is private to Wallet::Session (its _engine has
// no accessor), so the harness owns a second one over a refusing host.
[[nodiscard]] std::shared_ptr<engine::WalletLifecycle> Lifecycle() {
	static const auto result = []()
	-> std::shared_ptr<engine::WalletLifecycle> {
		try {
			return engine::WalletLifecycle::init(
				std::make_shared<RefusingHost>());
		} catch (...) {
			return nullptr;
		}
	}();
	return result;
}

[[nodiscard]] QString NormalizeWord(const QString &word) {
	return word.trimmed().toLower();
}

[[nodiscard]] QStringList Normalized(const std::vector<QString> &words) {
	auto result = QStringList();
	result.reserve(int(words.size()));
	for (const auto &word : words) {
		result.push_back(NormalizeWord(word));
	}
	return result;
}

[[nodiscard]] bool SameWords(
		const std::vector<QString> &a,
		const std::vector<QString> &b) {
	return Normalized(a) == Normalized(b);
}

[[nodiscard]] QString Hex(const QByteArray &key) {
	return QString::fromLatin1(key.toHex());
}

[[nodiscard]] std::optional<QByteArray> MnemonicKey(const QStringList &words) {
	try {
		const auto key = engine::rotation_mnemonic_public_key(
			words.join(QChar(' ')).toStdString());
		if (int(key.size()) != kKeySize) {
			return std::nullopt;
		}
		return QByteArray(
			reinterpret_cast<const char*>(key.data()),
			int(key.size()));
	} catch (...) {
		return std::nullopt;
	}
}

// A transcription of DerivePhraseIdentity (wallet_session.cpp): it sits in
// an anonymous namespace there, and this harness makes no production
// change. Words are normalized with trimmed().toLower(); the count must be
// 12 or 24; the anchor is rotation_mnemonic_public_key over all the words,
// and the signing key is the same function over words 13-24 of a 24-word
// phrase, or the anchor of a 12-word one. Runs off the main thread.
[[nodiscard]] PhraseKeys DerivePhraseKeys(const std::vector<QString> &words) {
	const auto normalized = Normalized(words);
	const auto count = int(normalized.size());
	if (count != kAnchorHalf && count != 2 * kAnchorHalf) {
		return {};
	}
	const auto anchor = MnemonicKey(normalized);
	if (!anchor) {
		return {};
	}
	const auto signing = (count == 2 * kAnchorHalf)
		? MnemonicKey(normalized.mid(kAnchorHalf))
		: anchor;
	if (!signing) {
		return {};
	}
	return { .anchor = *anchor, .signing = *signing, .ok = true };
}

// The same secret-free check AnchorDerivesOtherAddress (wallet_session.cpp)
// runs: the engine refuses a descriptor whose anchor derives another
// address with InvalidRecordId, and otherwise answers the canonical raw
// address it derived.
[[nodiscard]] AddressCheck AnchorDerives(
		const QByteArray &anchor,
		const QString &addressRaw) {
	const auto lifecycle = Lifecycle();
	if (!lifecycle || anchor.size() != kKeySize || addressRaw.isEmpty()) {
		return { .failed = true };
	}
	const auto recordId = std::string("gram-reconcile-check");
	try {
		const auto info = lifecycle->ton_connect_account(
			engine::WalletDescriptor{
				.record_id = recordId,
				.address = addressRaw.toStdString(),
				.public_key = std::vector<uint8_t>(
					anchor.constData(),
					anchor.constData() + anchor.size()),
				.network = engine::Network::kMainnet,
				.secret_ref = engine::ProtectedSecretRef{
					.value = "wallet:" + recordId + ":mnemonic",
				},
			});
		const auto derived = Wallet::CanonicalAddress(
			QString::fromStdString(info.address));
		return { .derives = (derived == addressRaw) };
	} catch (const engine::wallet_lifecycle_error::InvalidRecordId &) {
		return {};
	} catch (...) {
		return { .failed = true };
	}
}

// |words| are captured by value and never logged; the answer carries only
// public keys and booleans back to main.
void DeriveAsync(
		std::vector<QString> words,
		QString judgedAddress,
		QString realAddress,
		Fn<void(Derivation)> done) {
	crl::async([=, words = std::move(words)] {
		auto result = Derivation{ .keys = DerivePhraseKeys(words) };
		if (result.keys.ok) {
			result.judged = AnchorDerives(result.keys.anchor, judgedAddress);
			result.real = (realAddress == judgedAddress)
				? result.judged
				: AnchorDerives(result.keys.anchor, realAddress);
		}
		crl::on_main([=] {
			done(result);
		});
	});
}

[[nodiscard]] int KeyringKind(not_null<Main::Session*> session) {
	const auto reading = session->wallet().vault().reading();
	return (reading.state == Wallet::KeyringReading::State::Read)
		? int(reading.keyring.wrap.kind)
		: 0;
}

void Finish(
		const std::shared_ptr<Flow> &flow,
		GramReconcileCase kind,
		GramReconcileGate gate,
		const QString &detail) {
	if (flow->finished) {
		return;
	}
	flow->finished = true;
	flow->grant = nullptr;
	auto &result = flow->result;
	result.kind = kind;
	result.gate = gate;
	result.detail = flow->fileNote.isEmpty()
		? detail
		: (detail + u"; "_q + flow->fileNote);
	result.hostCalls = HostCalls.load();
	*flow->step = u"finished"_q;
	if (const auto done = base::take(flow->done)) {
		done(result);
	}
}

void Refuse(
		const std::shared_ptr<Flow> &flow,
		GramReconcileGate gate,
		const QString &detail) {
	Finish(flow, GramReconcileCase::Refused, gate, detail);
}

[[nodiscard]] Main::Session *Alive(const std::shared_ptr<Flow> &flow) {
	const auto session = flow->session.get();
	if (!session) {
		Refuse(flow, GramReconcileGate::ServedUnknown, u"session gone"_q);
	}
	return session;
}

void OnRevealed(
	const std::shared_ptr<Flow> &flow,
	std::vector<QString> words);

// The structural guard against wallet.exportSecretPhrase: revealPhrase
// takes its local branch exactly when revealsLocally() holds, and falls
// through to the server export otherwise, so the check and the call share
// one synchronous turn.
void Reveal(const std::shared_ptr<Flow> &flow) {
	const auto session = Alive(flow);
	if (!session) {
		return;
	}
	auto &wallet = session->wallet();
	*flow->step = u"reveal"_q;
	if (!wallet.revealsLocally()) {
		Refuse(
			flow,
			GramReconcileGate::NoReadableCurrentRecord,
			u"revealsLocally() is false at the reveal, which would need "
			"the server"_q);
		return;
	}
	flow->result.revealRequested = true;
	wallet.revealPhrase(
		Wallet::KeyAuthorization{ .grant = flow->grant },
		std::nullopt,
		[=](std::vector<QString> words, Wallet::CustodyOutcome) {
			OnRevealed(flow, std::move(words));
		},
		[=](const QString &error) {
			Refuse(
				flow,
				GramReconcileGate::RevealFailed,
				u"reveal failed "_q + error);
		});
}

void FinishCaseB(
		const std::shared_ptr<Flow> &flow,
		const std::vector<QString> &words,
		const Derivation &derived) {
	auto &result = flow->result;
	if (!derived.keys.ok) {
		Refuse(
			flow,
			GramReconcileGate::IdentityUnderivable,
			u"the revealed phrase's identity is underivable"_q);
		return;
	} else if (!result.revealedSignsServed) {
		Refuse(
			flow,
			GramReconcileGate::RevealedNotServedKey,
			u"revealed phrase does not sign for the served key"_q);
		return;
	} else if (!result.revealedDerivesServed) {
		Refuse(
			flow,
			GramReconcileGate::RevealedNotServedAddress,
			u"revealed phrase does not derive the served address%1"_q.arg(
				derived.judged.failed ? u" (address check failed)"_q : QString()));
		return;
	} else if (result.revealedWords < result.fileWords) {
		Refuse(
			flow,
			GramReconcileGate::RevealedShorterThanFile,
			u"revealed phrase is shorter than the file"_q);
		return;
	} else if (flow->drive.active()) {
		Refuse(
			flow,
			GramReconcileGate::DrivenWriteSuppressed,
			u"would rewrite"_q);
		return;
	} else if (derived.keys.signing != flow->realServedKey
		|| !derived.real.derives) {
		// Unreachable undriven (the judged values are the real ones); it
		// keeps the write itself tied to the real served wallet.
		Refuse(
			flow,
			GramReconcileGate::RevealedNotServedKey,
			u"revealed phrase does not qualify for the real served "
			"wallet"_q);
		return;
	}
	*flow->step = u"rewrite"_q;
	const auto written = RewriteGramAccountWords(
		words,
		flow->realServedAddress);
	result.live = written.live;
	result.golden = written.golden;
	if (!written.live) {
		Refuse(
			flow,
			GramReconcileGate::RewriteFailed,
			u"the live copy was not written"_q);
		return;
	}
	const auto reread = GramAccount();
	result.rereadEqual = reread
		&& SameWords(reread->words, words)
		&& (reread->addressRaw == flow->realServedAddress);
	if (!result.rereadEqual) {
		Refuse(
			flow,
			GramReconcileGate::RereadMismatch,
			u"GramAccount() does not re-read the revealed words at the "
			"served address (rereadWords=%1)"_q.arg(
				reread ? int(reread->words.size()) : -1));
		return;
	}
	Finish(
		flow,
		GramReconcileCase::RewroteFromReveal,
		GramReconcileGate::None,
		u"rewrote the fixture from the local reveal"_q);
}

void OnRevealedDerived(
		const std::shared_ptr<Flow> &flow,
		const std::vector<QString> &words,
		const Derivation &derived) {
	if (!Alive(flow)) {
		return;
	}
	auto &result = flow->result;
	if (derived.keys.ok) {
		result.revealedAnchorHex = Hex(derived.keys.anchor);
		result.revealedSigningHex = Hex(derived.keys.signing);
	}
	result.revealedSignsServed = derived.keys.ok
		&& (derived.keys.signing == flow->servedKey);
	result.revealedDerivesServed = derived.keys.ok
		&& derived.judged.derives;
	if (flow->route == Route::CaseA) {
		result.confirmEqual = result.revealedEqualsFile;
		if (!result.confirmEqual) {
			Refuse(
				flow,
				GramReconcileGate::ConfirmMismatch,
				u"the local reveal after the restore differs from the "
				"file"_q);
			return;
		}
		Finish(
			flow,
			GramReconcileCase::RestoredFromFile,
			GramReconcileGate::None,
			u"restored custody from the file and confirmed the local "
			"reveal"_q);
		return;
	}
	FinishCaseB(flow, words, derived);
}

void OnRevealed(
		const std::shared_ptr<Flow> &flow,
		std::vector<QString> words) {
	if (!Alive(flow)) {
		return;
	}
	auto &result = flow->result;
	result.revealedWords = int(words.size());
	result.revealedEqualsFile = SameWords(words, flow->fileWords);
	result.phrases.push_back(words);
	*flow->step = u"derive revealed identity"_q;
	DeriveAsync(
		words,
		flow->servedAddress,
		flow->realServedAddress,
		[=](Derivation derived) {
			OnRevealedDerived(flow, words, derived);
		});
}

void StartCaseA(const std::shared_ptr<Flow> &flow) {
	const auto session = Alive(flow);
	if (!session) {
		return;
	}
	auto &wallet = session->wallet();
	flow->route = Route::CaseA;
	*flow->step = u"case (a): silent grant"_q;
	flow->grant = Wallet::AcquireSilentVaultUnlock(session);
	if (!flow->grant) {
		Refuse(
			flow,
			GramReconcileGate::NoOpenGrant,
			u"keyringKind=%1; drive the protection chooser once"_q.arg(
				flow->result.keyringKind));
		return;
	}
	if (flow->drive.active()) {
		const auto readable = wallet.revealsLocally();
		if (!flow->realFileQualifies
			|| !readable
			|| !flow->fileAnchorMatchesCurrent) {
			Refuse(
				flow,
				GramReconcileGate::DrivenWriteSuppressed,
				u"a driven restore needs the real file to qualify and "
				"equal the readable current record: realFileQualifies=%1 "
				"realReadable=%2 sameAsCurrent=%3"_q
					.arg(flow->realFileQualifies ? 1 : 0)
					.arg(readable ? 1 : 0)
					.arg(flow->fileAnchorMatchesCurrent ? 1 : 0));
			return;
		}
	}
	*flow->step = u"case (a): restore"_q;
	wallet.restoreFromPhrase(
		Wallet::KeyAuthorization{ .grant = flow->grant },
		flow->fileWords,
		[=] {
			*flow->step = u"case (a): confirm"_q;
			Reveal(flow);
		},
		[=](const QString &error) {
			Refuse(
				flow,
				GramReconcileGate::RestoreFailed,
				u"restore failed "_q + error);
		});
}

void StartCaseB(const std::shared_ptr<Flow> &flow) {
	const auto session = Alive(flow);
	if (!session) {
		return;
	}
	flow->route = Route::CaseB;
	if (!flow->result.currentReadable) {
		Refuse(
			flow,
			GramReconcileGate::NoReadableCurrentRecord,
			u"the file does not qualify and no current record reveals "
			"locally"_q);
		return;
	}
	*flow->step = u"case (b): silent grant"_q;
	flow->grant = Wallet::AcquireSilentVaultUnlock(session);
	if (!flow->grant) {
		Refuse(
			flow,
			GramReconcileGate::NoOpenGrant,
			u"keyringKind=%1; drive the protection chooser once"_q.arg(
				flow->result.keyringKind));
		return;
	}
	Reveal(flow);
}

void OnFileDerived(
		const std::shared_ptr<Flow> &flow,
		const Derivation &derived) {
	if (!Alive(flow)) {
		return;
	}
	auto &result = flow->result;
	if (derived.keys.ok) {
		result.fileAnchorHex = Hex(derived.keys.anchor);
		result.fileSigningHex = Hex(derived.keys.signing);
		flow->fileAnchorMatchesCurrent = !flow->currentAnchor.isEmpty()
			&& (flow->currentAnchor == derived.keys.anchor)
			&& (flow->currentSigning == derived.keys.signing);
	} else {
		flow->fileNote = u"file identity underivable"_q;
	}
	if (derived.judged.failed || derived.real.failed) {
		flow->fileNote = u"file address check failed"_q;
	}
	result.fileSignsServed = derived.keys.ok
		&& (derived.keys.signing == flow->servedKey);
	result.fileDerivesServed = derived.keys.ok && derived.judged.derives;
	flow->realFileQualifies = derived.keys.ok
		&& (derived.keys.signing == flow->realServedKey)
		&& derived.real.derives;
	const auto qualifies = result.fileSignsServed
		&& result.fileDerivesServed;
	if (!qualifies) {
		StartCaseB(flow);
		return;
	} else if (result.currentRecord && result.currentReadable) {
		Finish(
			flow,
			GramReconcileCase::NoOp,
			GramReconcileGate::None,
			u"already reconciled"_q);
		return;
	} else if (!result.currentRecord
		&& !flow->preV4Unresolved
		&& (result.localWords < result.fileWords)) {
		StartCaseA(flow);
		return;
	}
	const auto why = result.currentRecord
		? u"the current record is unreadable (vault key unusable or "
			"secret unreadable)"_q
		: flow->preV4Unresolved
		? u"a pre-v4 unresolved record of the served wallet: its count "
			"is unknown, and revealParked would write custody"_q
		: u"the held obsolete record is not shorter than the file "
			"(localWords=%1 fileWords=%2)"_q
			.arg(result.localWords)
			.arg(result.fileWords);
	Refuse(flow, GramReconcileGate::NoReadableCurrentRecord, why);
}

void Start(
		const std::shared_ptr<Flow> &flow,
		not_null<Main::Session*> session) {
	auto &result = flow->result;
	const auto &drive = flow->drive;
	result.driven = drive.active();
	*flow->step = u"gates"_q;
	if (!Active()) {
		Refuse(
			flow,
			GramReconcileGate::NotActive,
			u"not a -testagent launch"_q);
		return;
	}
	auto &wallet = session->wallet();
	const auto address = wallet.address();
	const auto key = wallet.publicKey();
	if (!address || address->isEmpty() || key.size() != kKeySize) {
		Refuse(
			flow,
			GramReconcileGate::ServedUnknown,
			u"served address or key not settled (keySize=%1)"_q.arg(
				key.size()));
		return;
	}
	flow->realServedAddress = Wallet::CanonicalAddress(*address);
	flow->realServedKey = key;
	flow->servedAddress = drive.servedAddressRaw.value_or(
		flow->realServedAddress);
	flow->servedKey = drive.servedKey.value_or(key);
	result.servedAddressRaw = flow->servedAddress;
	result.servedKeyHex = Hex(flow->servedKey);

	const auto fixture = GramAccount();
	if (!fixture) {
		Refuse(
			flow,
			GramReconcileGate::FixtureAbsent,
			u"test_gram_account.txt is absent or malformed"_q);
		return;
	}
	flow->fileWords = fixture->words;
	result.fileWords = int(fixture->words.size());
	result.phrases.push_back(fixture->words);
	if (fixture->addressRaw != flow->realServedAddress) {
		Refuse(
			flow,
			GramReconcileGate::FixtureOtherWallet,
			u"fixture=%1"_q.arg(fixture->addressRaw));
		return;
	}

	const auto store = Wallet::ReadCustodyStore(session->local());
	if (!store) {
		Refuse(
			flow,
			GramReconcileGate::CustodyUnreadable,
			u"the custody store could not be read"_q);
		return;
	}
	const auto busy = wallet.custodyBusy();
	const auto pending = store->pendingRotation.has_value();
	const auto awaiting = store->anyAwaitingServerKey();
	if (busy || pending || awaiting) {
		Refuse(
			flow,
			GramReconcileGate::CustodyBusy,
			u"custodyBusy=%1 pendingRotation=%2 awaitingServerKey=%3"_q
				.arg(busy ? 1 : 0)
				.arg(pending ? 1 : 0)
				.arg(awaiting ? 1 : 0));
		return;
	}

	*flow->step = u"record facts"_q;
	const auto current = store->current(
		flow->realServedAddress,
		flow->realServedKey);
	const auto held = store->forAddress(flow->realServedAddress);
	if (current) {
		flow->currentAnchor = current->publicKey;
		flow->currentSigning = current->signingKey.isEmpty()
			? current->publicKey
			: current->signingKey;
	}
	result.currentRecord = (current != nullptr);
	result.obsoleteHeld = !current && held && !held->signingKey.isEmpty();
	result.localWords = !result.obsoleteHeld
		? 0
		: (held->signingKey == held->publicKey)
		? kAnchorHalf
		: 2 * kAnchorHalf;
	flow->preV4Unresolved = !current && held && held->signingKey.isEmpty();
	result.currentReadable = wallet.revealsLocally();
	result.keyringKind = KeyringKind(session);
	result.openGrant = Wallet::VaultUnlockSilent(session);
	if (drive.currentRecordReadable) {
		result.currentReadable = *drive.currentRecordReadable;
	}
	if (drive.obsoleteRecordWords) {
		result.obsoleteHeld = true;
		result.localWords = *drive.obsoleteRecordWords;
		result.currentRecord = false;
		result.currentReadable = false;
		flow->preV4Unresolved = false;
	}

	*flow->step = u"derive file identity"_q;
	DeriveAsync(
		fixture->words,
		flow->servedAddress,
		flow->realServedAddress,
		[=](Derivation derived) {
			OnFileDerived(flow, derived);
		});
}

void StartFlow(
		not_null<Main::Session*> session,
		GramReconcileDrive drive,
		Fn<void(GramReconcileResult)> done,
		std::shared_ptr<QString> step) {
	const auto flow = std::make_shared<Flow>();
	flow->session = base::make_weak(session);
	flow->drive = std::move(drive);
	flow->step = std::move(step);
	flow->done = std::move(done);
	Start(flow, session);
}

[[nodiscard]] QString Bool(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString OrNone(const QString &value) {
	return value.isEmpty() ? u"-"_q : value;
}

// File metadata only: reading it never opens the fixture.
struct FileStamp {
	bool exists = false;
	qint64 size = 0;
	qint64 modifiedMs = 0;

	friend bool operator==(const FileStamp &, const FileStamp &) = default;
};

[[nodiscard]] FileStamp Stamp(const QString &path) {
	const auto info = QFileInfo(path);
	if (!info.exists()) {
		return {};
	}
	return {
		.exists = true,
		.size = info.size(),
		.modifiedMs = info.lastModified().toMSecsSinceEpoch(),
	};
}

[[nodiscard]] QString StampText(const FileStamp &stamp) {
	return stamp.exists
		? u"size=%1 mtimeMs=%2"_q.arg(stamp.size).arg(stamp.modifiedMs)
		: u"absent"_q;
}

} // namespace

QString GramReconcileCaseName(GramReconcileCase value) {
	switch (value) {
	case GramReconcileCase::NoOp: return u"noop"_q;
	case GramReconcileCase::RewroteFromReveal: return u"b"_q;
	case GramReconcileCase::RestoredFromFile: return u"a"_q;
	case GramReconcileCase::Refused: return u"refused"_q;
	}
	return u"unknown"_q;
}

QString GramReconcileGateName(GramReconcileGate gate) {
	using Gate = GramReconcileGate;
	switch (gate) {
	case Gate::None: return u"None"_q;
	case Gate::NotActive: return u"NotActive"_q;
	case Gate::ServedUnknown: return u"ServedUnknown"_q;
	case Gate::FixtureAbsent: return u"FixtureAbsent"_q;
	case Gate::FixtureOtherWallet: return u"FixtureOtherWallet"_q;
	case Gate::CustodyUnreadable: return u"CustodyUnreadable"_q;
	case Gate::CustodyBusy: return u"CustodyBusy"_q;
	case Gate::NoReadableCurrentRecord: return u"NoReadableCurrentRecord"_q;
	case Gate::NoOpenGrant: return u"NoOpenGrant"_q;
	case Gate::RevealFailed: return u"RevealFailed"_q;
	case Gate::RevealedNotServedKey: return u"RevealedNotServedKey"_q;
	case Gate::RevealedNotServedAddress:
		return u"RevealedNotServedAddress"_q;
	case Gate::RevealedShorterThanFile: return u"RevealedShorterThanFile"_q;
	case Gate::IdentityUnderivable: return u"IdentityUnderivable"_q;
	case Gate::RewriteFailed: return u"RewriteFailed"_q;
	case Gate::RereadMismatch: return u"RereadMismatch"_q;
	case Gate::RestoreFailed: return u"RestoreFailed"_q;
	case Gate::ConfirmMismatch: return u"ConfirmMismatch"_q;
	case Gate::DrivenWriteSuppressed: return u"DrivenWriteSuppressed"_q;
	}
	return u"Unknown"_q;
}

bool GramReconcileDrive::active() const {
	return servedKey.has_value()
		|| servedAddressRaw.has_value()
		|| currentRecordReadable.has_value()
		|| obsoleteRecordWords.has_value();
}

bool GramReconcileResult::reconciled() const {
	return (kind == GramReconcileCase::NoOp)
		|| (kind == GramReconcileCase::RewroteFromReveal)
		|| (kind == GramReconcileCase::RestoredFromFile);
}

QString GramReconcileRow(const GramReconcileResult &r) {
	return (u"GRAM_RECONCILE: case=%1 gate=%2 driven=%3 fileWords=%4 "
		"localWords=%5 revealedWords=%6 fileSignsServed=%7 "
		"fileDerivesServed=%8 revealedSignsServed=%9 "_q
		.arg(GramReconcileCaseName(r.kind))
		.arg(GramReconcileGateName(r.gate))
		.arg(Bool(r.driven))
		.arg(r.fileWords)
		.arg(r.localWords)
		.arg(r.revealedWords)
		.arg(Bool(r.fileSignsServed))
		.arg(Bool(r.fileDerivesServed))
		.arg(Bool(r.revealedSignsServed)))
		+ (u"revealedDerivesServed=%1 revealedEqualsFile=%2 "
			"currentRecord=%3 currentReadable=%4 obsoleteHeld=%5 "
			"openGrant=%6 revealRequested=%7 keyringKind=%8 live=%9 "_q
			.arg(Bool(r.revealedDerivesServed))
			.arg(Bool(r.revealedEqualsFile))
			.arg(Bool(r.currentRecord))
			.arg(Bool(r.currentReadable))
			.arg(Bool(r.obsoleteHeld))
			.arg(Bool(r.openGrant))
			.arg(Bool(r.revealRequested))
			.arg(r.keyringKind)
			.arg(Bool(r.live)))
		+ (u"golden=%1 rereadEqual=%2 confirmEqual=%3 served=%4 "
			"servedKey=%5 fileAnchor=%6 fileSigning=%7 revealedAnchor=%8 "
			"revealedSigning=%9 "_q
			.arg(Bool(r.golden))
			.arg(Bool(r.rereadEqual))
			.arg(Bool(r.confirmEqual))
			.arg(OrNone(r.servedAddressRaw))
			.arg(OrNone(r.servedKeyHex))
			.arg(OrNone(r.fileAnchorHex))
			.arg(OrNone(r.fileSigningHex))
			.arg(OrNone(r.revealedAnchorHex))
			.arg(OrNone(r.revealedSigningHex)))
		+ u"hostCalls=%1 detail=%2"_q
			.arg(r.hostCalls)
			.arg(OrNone(r.detail));
}

void ReconcileGramAccount(
		not_null<Main::Session*> session,
		GramReconcileDrive drive,
		Fn<void(GramReconcileResult)> done) {
	StartFlow(
		session,
		std::move(drive),
		std::move(done),
		std::make_shared<QString>());
}

void AppendGramReconcile(
		not_null<Runner*> runner,
		GramReconcileStageArgs args) {
	struct State {
		std::optional<GramReconcileResult> result;
		std::shared_ptr<QString> step = std::make_shared<QString>(
			u"not started"_q);
	};
	const auto state = std::make_shared<State>();
	const auto name = args.name;
	runner->add({
		.name = name,
		.skipReason = args.skipReason,
		.run = [=] {
			if (args.prepare) {
				args.prepare();
			}
			const auto session = args.resolve ? args.resolve() : nullptr;
			if (!session) {
				auto result = GramReconcileResult();
				result.gate = GramReconcileGate::ServedUnknown;
				result.driven = args.drive.active();
				result.detail = u"no session"_q;
				state->result = std::move(result);
				return;
			}
			StartFlow(
				session,
				args.drive,
				[=](GramReconcileResult result) {
					state->result = std::move(result);
				},
				state->step);
		},
		.until = [=] {
			return state->result.has_value();
		},
		.then = [=] {
			const auto &result = *state->result;
			const auto row = GramReconcileRow(result);
			Note(row);
			if (args.expectCase) {
				const auto ok = (result.kind == *args.expectCase)
					&& (!args.expectGate
						|| (result.gate == *args.expectGate));
				Check(ok, name, row);
			} else if (result.reconciled()) {
				Pass(name, row);
			} else {
				Skipped(
					name,
					u"fixture gate: P0 "_q
						+ GramReconcileGateName(result.gate)
						+ u" - "_q
						+ result.detail);
			}
			if (args.then) {
				args.then(result);
			}
		},
		.timeout = kReconcileTimeout,
		.timeoutDetails = [=] {
			return u"step=%1"_q.arg(*state->step);
		},
	});
}

std::vector<QString> GramCustodySecretTokens(
		not_null<Main::Session*> session) {
	auto result = std::vector<QString>();
	const auto add = [&](const QString &value) {
		if (!value.isEmpty()
			&& (ranges::find(result, value) == end(result))) {
			result.push_back(value);
		}
	};
	const auto addRef = [&](const QString &secretRef) {
		add(secretRef);
		if (!secretRef.isEmpty()) {
			add(Wallet::VaultSecretStorageKey(secretRef));
		}
	};
	const auto store = Wallet::ReadCustodyStore(session->local());
	if (!store) {
		return result;
	}
	for (const auto &record : store->records) {
		add(record.recordId);
	}
	if (const auto &pending = store->pendingRotation) {
		add(pending->recordId);
	}
	auto copy = *store;
	Wallet::ForEachCustodySecretRef(copy, [&](const QString &secretRef) {
		addRef(secretRef);
		return false;
	});
	return result;
}

void AppendGramReconcileSelfTest(
		not_null<Runner*> runner,
		Fn<Main::Session*()> resolve) {
	struct State {
		bool gated = false;
		bool staged = false;
		bool restorePending = false;
		FileStamp live;
		FileStamp golden;
		// In process only, never printed.
		std::vector<QString> words;
		std::vector<QString> preStageWords;
		QString preStageAddress;
	};
	const auto state = std::make_shared<State>();
	// A staged 12-word live copy that case (b) did not rewrite is put back,
	// or no later P0 run without a grant could repair it.
	const auto restoreStaged = [=](const QString &name) {
		if (!base::take(state->restorePending)) {
			return;
		}
		const auto restored = RestoreGramAccountLiveWords(
			state->preStageWords,
			state->preStageAddress);
		const auto reread = GramAccount();
		const auto equal = reread
			&& SameWords(reread->words, state->preStageWords);
		Check(
			restored && equal,
			name + u": the staged live copy was restored to its words "
			"before staging"_q,
			u"restored=%1 rereadWords=%2 wordsBefore=%3 rereadEqual=%4"_q
				.arg(Bool(restored))
				.arg(reread ? int(reread->words.size()) : -1)
				.arg(int(state->preStageWords.size()))
				.arg(Bool(equal)));
	};
	runner->onFinish([=] {
		restoreStaged(u"gram_reconcile_self_test finish"_q);
		state->words.clear();
		state->preStageWords.clear();
	});

	const auto snapshot = [=] {
		state->live = Stamp(GramAccountLivePath());
		state->golden = Stamp(GramAccountGoldenPath());
		const auto fixture = GramAccount();
		state->words = fixture ? fixture->words : std::vector<QString>();
	};
	const auto stamps = [=] {
		return u"liveBefore=[%1] liveAfter=[%2] goldenBefore=[%3] "
			"goldenAfter=[%4]"_q
			.arg(StampText(state->live))
			.arg(StampText(Stamp(GramAccountLivePath())))
			.arg(StampText(state->golden))
			.arg(StampText(Stamp(GramAccountGoldenPath())));
	};
	const auto writesNothing = [=](
			const QString &name,
			const GramReconcileResult &result) {
		const auto fixture = GramAccount();
		const auto wordsEqual = fixture
			&& SameWords(fixture->words, state->words);
		const auto same = (Stamp(GramAccountLivePath()) == state->live)
			&& (Stamp(GramAccountGoldenPath()) == state->golden);
		Check(
			!result.live && !result.golden && same && wordsEqual,
			name + u": writes nothing"_q,
			u"live=%1 golden=%2 %3 wordsEqual=%4"_q
				.arg(Bool(result.live))
				.arg(Bool(result.golden))
				.arg(stamps())
				.arg(Bool(wordsEqual)));
	};
	const auto gated = [=] {
		return state->gated
			? u"fixture gate: initial P0 reconciliation refused"_q
			: QString();
	};
	// The initial P0 run can be a grant-free NoOp, so every stage whose
	// path reaches a reveal or a restore is gated on the grant itself.
	const auto grantGated = [=](const QString &what) {
		const auto reason = gated();
		if (!reason.isEmpty()) {
			return reason;
		}
		const auto session = resolve ? resolve() : nullptr;
		if (!session) {
			return u"fixture gate: %1 (no session)"_q.arg(what);
		} else if (!Wallet::VaultUnlockSilent(session)) {
			return u"fixture gate: %1 NoOpenGrant keyringKind=%2; drive the "
				"protection chooser once"_q
				.arg(what)
				.arg(KeyringKind(session));
		}
		return QString();
	};
	const auto otherKey = QByteArray(kKeySize, char(0x5a));

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_initial"_q,
		.resolve = resolve,
		.then = [=](const GramReconcileResult &result) {
			state->gated = !result.reconciled();
			if (!state->gated) {
				Check(
					result.hostCalls == 0,
					u"gram_reconcile_initial: the harness lifecycle "
					"never called its refusing host"_q,
					u"hostCalls=%1"_q.arg(result.hostCalls));
			}
		},
	});

	runner->add({
		.name = u"gram_reconcile_stage_stale"_q,
		.skipReason = [=] {
			const auto reason = grantGated(
				u"cannot stage an older, shorter phrase"_q);
			if (!reason.isEmpty()) {
				return reason;
			}
			const auto session = resolve ? resolve() : nullptr;
			const auto fixture = GramAccount();
			if (!session || !session->wallet().revealsLocally()) {
				return u"fixture gate: cannot stage an older, shorter "
					"phrase (no readable current record)"_q;
			} else if (!QFileInfo::exists(GramAccountLivePath())) {
				return u"fixture gate: cannot stage an older, shorter "
					"phrase (no live copy)"_q;
			} else if (!fixture || int(fixture->words.size()) <= kAnchorHalf) {
				return u"fixture gate: cannot stage an older, shorter "
					"phrase (12 words or fewer)"_q;
			}
			return QString();
		},
		.then = [=] {
			const auto name = u"gram_reconcile_stage_stale"_q;
			snapshot();
			const auto fixture = GramAccount();
			const auto &before = state->words;
			const auto prefix = std::vector<QString>(
				before.begin(),
				before.begin() + std::min(int(before.size()), kAnchorHalf));
			const auto staged = fixture
				&& StageGramAccountLiveWords(prefix, fixture->addressRaw);
			if (staged) {
				state->preStageWords = before;
				state->preStageAddress = fixture->addressRaw;
				state->restorePending = true;
			}
			Check(
				staged,
				name + u": the live copy was staged to its first 12 "
				"words"_q,
				u"wordsBefore=%1"_q.arg(int(before.size())));
			const auto reread = GramAccount();
			const auto isPrefix = reread
				&& (int(reread->words.size()) == kAnchorHalf)
				&& (int(before.size()) > kAnchorHalf)
				&& std::equal(
					reread->words.begin(),
					reread->words.end(),
					before.begin());
			Check(
				isPrefix,
				name + u": GramAccount() re-reads 12 words that are a "
				"prefix of the words before staging"_q,
				u"rereadWords=%1"_q.arg(
					reread ? int(reread->words.size()) : -1));
			const auto golden = Stamp(GramAccountGoldenPath());
			Check(
				golden == state->golden,
				name + u": staging left the golden copy untouched"_q,
				stamps());
			state->staged = staged && isPrefix;
		},
	});

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_case_b"_q,
		.resolve = resolve,
		.expectCase = GramReconcileCase::RewroteFromReveal,
		.skipReason = [=] {
			return !state->staged
				? u"fixture gate: the stale fixture was not staged"_q
				: grantGated(u"cannot reveal the current record"_q);
		},
		.prepare = snapshot,
		.then = [=](const GramReconcileResult &result) {
			const auto name = u"gram_reconcile_case_b"_q;
			if (result.live) {
				// The reveal replaced the staged words; nothing to put back.
				state->restorePending = false;
			}
			if (result.kind != GramReconcileCase::RewroteFromReveal) {
				return;
			}
			Check(
				(result.fileWords == kAnchorHalf)
					&& (result.revealedWords > result.fileWords)
					&& (result.hostCalls == 0),
				name + u": the staged 12-word file met the longer local "
				"reveal"_q,
				u"fileWords=%1 revealedWords=%2 hostCalls=%3"_q
					.arg(result.fileWords)
					.arg(result.revealedWords)
					.arg(result.hostCalls));
			const auto goldenExists = state->golden.exists;
			Check(
				result.live && (result.golden || !goldenExists),
				name + u": the live copy and, when it exists, the golden "
				"copy were written"_q,
				u"live=%1 golden=%2 goldenExists=%3"_q
					.arg(Bool(result.live))
					.arg(Bool(result.golden))
					.arg(Bool(goldenExists)));
			const auto reread = GramAccount();
			const auto &revealed = result.phrases.back();
			const auto equal = result.rereadEqual
				&& reread
				&& (int(reread->words.size()) == result.revealedWords)
				&& SameWords(reread->words, revealed);
			Check(
				equal,
				name + u": GramAccount() re-reads the revealed words, "
				"equal in count and in words"_q,
				u"rereadEqual=%1 rereadWords=%2 revealedWords=%3"_q
					.arg(Bool(result.rereadEqual))
					.arg(reread ? int(reread->words.size()) : -1)
					.arg(result.revealedWords));
			const auto liveChanged = (Stamp(GramAccountLivePath())
				!= state->live);
			const auto goldenChanged = (Stamp(GramAccountGoldenPath())
				!= state->golden);
			Check(
				liveChanged && (goldenChanged || !goldenExists),
				name + u": the rewritten copies changed on disk"_q,
				stamps());
		},
	});

	runner->add({
		.name = u"gram_reconcile_restore_staged"_q,
		.skipReason = [=] {
			return state->restorePending
				? QString()
				: u"no staged live copy is left to restore"_q;
		},
		.then = [=] {
			restoreStaged(u"gram_reconcile_restore_staged"_q);
		},
	});

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_noop"_q,
		.resolve = resolve,
		.expectCase = GramReconcileCase::NoOp,
		.skipReason = gated,
		.prepare = snapshot,
		.then = [=](const GramReconcileResult &result) {
			writesNothing(u"gram_reconcile_noop"_q, result);
		},
	});

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_refuse_signing"_q,
		.resolve = resolve,
		.drive = { .servedKey = otherKey },
		.expectCase = GramReconcileCase::Refused,
		.expectGate = GramReconcileGate::RevealedNotServedKey,
		.skipReason = [=] {
			return grantGated(u"cannot reveal the current record"_q);
		},
		.prepare = snapshot,
		.then = [=](const GramReconcileResult &result) {
			writesNothing(u"gram_reconcile_refuse_signing"_q, result);
		},
	});

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_refuse_address"_q,
		.resolve = resolve,
		.drive = {
			.servedAddressRaw = Wallet::CanonicalAddress(
				u"0:"_q + QString(64, QChar('0'))),
		},
		.expectCase = GramReconcileCase::Refused,
		.expectGate = GramReconcileGate::RevealedNotServedAddress,
		.skipReason = [=] {
			return grantGated(u"cannot reveal the current record"_q);
		},
		.prepare = snapshot,
		.then = [=](const GramReconcileResult &result) {
			writesNothing(u"gram_reconcile_refuse_address"_q, result);
		},
	});

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_refuse_no_record"_q,
		.resolve = resolve,
		.drive = {
			.servedKey = otherKey,
			.currentRecordReadable = false,
		},
		.expectCase = GramReconcileCase::Refused,
		.expectGate = GramReconcileGate::NoReadableCurrentRecord,
		.skipReason = gated,
		.prepare = snapshot,
		.then = [=](const GramReconcileResult &result) {
			const auto name = u"gram_reconcile_refuse_no_record"_q;
			writesNothing(name, result);
			Check(
				!result.revealRequested,
				name + u": no reveal was requested"_q,
				u"revealRequested=%1"_q.arg(Bool(result.revealRequested)));
		},
	});

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_case_a_driven"_q,
		.resolve = resolve,
		.drive = { .obsoleteRecordWords = kAnchorHalf },
		.expectCase = GramReconcileCase::RestoredFromFile,
		.skipReason = [=] {
			const auto reason = grantGated(
				u"cannot restore custody from the file"_q);
			if (!reason.isEmpty()) {
				return reason;
			}
			const auto fixture = GramAccount();
			return (!fixture || int(fixture->words.size()) <= kAnchorHalf)
				? u"fixture gate: a 12-word fixture cannot be longer "
					"than a 12-word obsolete record"_q
				: QString();
		},
		.prepare = snapshot,
		.then = [=](const GramReconcileResult &result) {
			const auto name = u"gram_reconcile_case_a_driven"_q;
			Check(
				result.confirmEqual
					&& (result.localWords == kAnchorHalf)
					&& (result.fileWords == 2 * kAnchorHalf),
				name + u": custody restored from the file and the local "
				"reveal confirmed equal"_q,
				u"confirmEqual=%1 localWords=%2 fileWords=%3"_q
					.arg(Bool(result.confirmEqual))
					.arg(result.localWords)
					.arg(result.fileWords));
			const auto session = resolve ? resolve() : nullptr;
			const auto readable = session
				&& session->wallet().revealsLocally();
			Check(
				readable,
				name + u": the restored record reveals locally"_q,
				u"revealsLocally=%1"_q.arg(Bool(readable)));
			writesNothing(name, result);
		},
	});

	AppendGramReconcile(runner, {
		.name = u"gram_reconcile_noop_after_a"_q,
		.resolve = resolve,
		.expectCase = GramReconcileCase::NoOp,
		.skipReason = gated,
		.prepare = snapshot,
		.then = [=](const GramReconcileResult &result) {
			writesNothing(u"gram_reconcile_noop_after_a"_q, result);
		},
	});
}

} // namespace Test

#endif // _DEBUG
