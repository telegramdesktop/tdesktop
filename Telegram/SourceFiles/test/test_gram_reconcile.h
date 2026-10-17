/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Test {

class Runner;

// The P0 fixture reconciliation of test/README.md ("Account fixture
// secrets"): the golden test_gram_account.txt (Test::GramAccount(), the live
// copy first, then the golden sibling) is reconciled with the marked live
// copy's own wallet custody, in process, after the campaign's address gate,
// through the product's own local reveal and restore only. Before this
// helper every funded-wallet overlay hand-rolled it:
// 2026/09/14/disable-wallet-backup-with-key-ownership-proof and
// 2026/09/25/import-rotated-wallets-with-the-revised-layer-230-scheme
// ("P0/R" of its Run 2), and the second one first lost a whole run to a
// fixture that predated later rotations of the golden wallet.
//
// "Qualifies" below means: the phrase signs for the served key (its signing
// key - words 13-24 of a 24-word phrase, the anchor of a 12-word one - is
// Wallet::Session::publicKey()) AND derives the served address (its anchor,
// words 1-12, derives *Wallet::Session::address()). The decision table, in
// order:
// 1. Preconditions, each a refusal: NotActive; ServedUnknown (no served
//    address or no 32-byte served key); FixtureAbsent; FixtureOtherWallet
//    (the fixture names another wallet); CustodyUnreadable; CustodyBusy
//    (custodyBusy(), a pending rotation, or a record awaiting its server
//    key - such a record "signs with" any served key, so it cannot certify).
// 2. NoOp: the file qualifies and a readable current record exists. Nothing
//    is written.
// 3. Case (a), RestoredFromFile: the file qualifies, no current record
//    exists, and the local count is below the file's. The local count is
//    0 when no record of the served wallet is held, and otherwise implied
//    by the held obsolete v4 record's keys (12 when its signing key is its
//    anchor, else 24) - never read by a reveal: an obsolete record is not
//    the current record, so revealPhrase would fall through to the server
//    export. Restores custody from the file's words through
//    Session::restoreFromPhrase with the silent Open grant (no chooser),
//    then requires a local reveal equal to the file (confirmEqual).
// 4. The file qualifies but (a) does not apply: NoReadableCurrentRecord,
//    the detail naming why (the current record is unreadable, the held
//    obsolete record is not shorter, or a pre-v4 unresolved record whose
//    count is unknown - never revealParked, whose reveal writes custody).
// 5. The file does not qualify: NoReadableCurrentRecord unless
//    revealsLocally(); NoOpenGrant without a silent grant; else the local
//    reveal (RevealFailed <PHRASE_*> on failure), then RevealedNotServedKey,
//    RevealedNotServedAddress or RevealedShorterThanFile. Otherwise case
//    (b), RewroteFromReveal: RewriteGramAccountWords(revealed, served
//    address), then GramAccount() re-read equal in words and address
//    (RereadMismatch otherwise, RewriteFailed when the live copy was not
//    written).
//
// Never: wallet.replaceWallet, wallet.exportSecretPhrase or any other server
// method that mutates or reveals the wallet - revealPhrase is called only
// when revealsLocally() holds in the same synchronous turn, which is exactly
// the product's local branch; no reset and no minted wallet; no recovery
// word anywhere but the fixture files. Rows, details and Notes carry the
// case, word counts, booleans, public keys and addresses only. The golden
// sibling is written only through RewriteGramAccountWords, and only with a
// phrase proven to sign for the real served key and derive the real served
// address.
//
// The drive (GramReconcileDrive) is the acceptance's "a seam may drive the
// served key or the reveal's answer". It changes only the identity
// comparisons and the record facts it names; custody lookups always use the
// real served address and key. A driven run never writes a fixture file
// (DrivenWriteSuppressed where case (b) would have rewritten), and a driven
// case (a) restores only when the UNDRIVEN facts show the file qualifies
// against the real served wallet and equals the device's readable current
// record - a same-phrase re-import, never a different phrase.
enum class GramReconcileCase {
	NoOp,
	RewroteFromReveal,
	RestoredFromFile,
	Refused,
};

enum class GramReconcileGate {
	None,
	NotActive,
	ServedUnknown,
	FixtureAbsent,
	FixtureOtherWallet,
	CustodyUnreadable,
	CustodyBusy,
	NoReadableCurrentRecord,
	NoOpenGrant,
	RevealFailed,
	RevealedNotServedKey,
	RevealedNotServedAddress,
	RevealedShorterThanFile,
	IdentityUnderivable,
	RewriteFailed,
	RereadMismatch,
	RestoreFailed,
	ConfirmMismatch,
	DrivenWriteSuppressed,
};

[[nodiscard]] QString GramReconcileCaseName(GramReconcileCase value);
[[nodiscard]] QString GramReconcileGateName(GramReconcileGate gate);

struct GramReconcileDrive {
	std::optional<QByteArray> servedKey;
	std::optional<QString> servedAddressRaw;
	std::optional<bool> currentRecordReadable;
	// Pretend an obsolete record of N words is held and no current one.
	std::optional<int> obsoleteRecordWords;

	[[nodiscard]] bool active() const;
};

// No revealAnswer / revealError drive: no self-test stage drives them (the
// three required refusals are reached through servedKey, servedAddressRaw
// and currentRecordReadable), and a field with no user is scaffolding.
struct GramReconcileResult {
	GramReconcileCase kind = GramReconcileCase::Refused;
	GramReconcileGate gate = GramReconcileGate::None;
	QString detail; // public-only
	bool driven = false;
	int fileWords = 0;
	int localWords = 0;
	int revealedWords = 0;
	bool fileSignsServed = false;
	bool fileDerivesServed = false;
	bool revealedSignsServed = false;
	bool revealedDerivesServed = false;
	bool revealedEqualsFile = false;
	bool currentRecord = false;
	bool currentReadable = false;
	bool obsoleteHeld = false;
	bool openGrant = false;
	bool revealRequested = false;
	int keyringKind = 0;
	bool live = false; // copies written
	bool golden = false;
	bool rereadEqual = false;
	bool confirmEqual = false;
	int hostCalls = 0;
	QString servedKeyHex;
	QString servedAddressRaw;
	QString fileAnchorHex;
	QString fileSigningHex;
	QString revealedAnchorHex;
	QString revealedSigningHex;
	// In memory only, for SecrecySecrets::phrases: the file's words and
	// every revealed phrase. Never print them.
	std::vector<std::vector<QString>> phrases;

	// NoOp || RewroteFromReveal || RestoredFromFile.
	[[nodiscard]] bool reconciled() const;
};

// "GRAM_RECONCILE: case=... gate=... ..." - public fields only.
[[nodiscard]] QString GramReconcileRow(const GramReconcileResult &result);

// Runs the table above on |session| and calls |done| exactly once, on
// main. Prints nothing itself.
void ReconcileGramAccount(
	not_null<Main::Session*> session,
	GramReconcileDrive drive,
	Fn<void(GramReconcileResult)> done);

// One stage: runs ReconcileGramAccount on resolve(), waits for its answer
// (60 s), Notes the row and then either Checks |expectCase| / |expectGate|
// (self-tests) or reports NoOp / (b) / (a) as PASS and a refusal as
// TEST_RESULT: N/A with "fixture gate: P0 <gate> - <detail>". |prepare|
// runs in the stage's |run| right before the flow starts, |then| receives
// the result after the verdict.
struct GramReconcileStageArgs {
	QString name;
	Fn<Main::Session*()> resolve;
	GramReconcileDrive drive;
	std::optional<GramReconcileCase> expectCase;
	std::optional<GramReconcileGate> expectGate;
	Fn<QString()> skipReason;
	Fn<void()> prepare;
	Fn<void(const GramReconcileResult&)> then;
};
void AppendGramReconcile(
	not_null<Runner*> runner,
	GramReconcileStageArgs args);

// Record ids, secret refs and "secret/" storage keys of every custody
// record and of the pending rotation, read through ReadCustodyStore, the
// refs walked by Wallet::ForEachCustodySecretRef; in memory only, for
// SecrecySecrets::tokens. Take it at P0 and again at the
// scan: a restore mints a new record id.
[[nodiscard]] std::vector<QString> GramCustodySecretTokens(
	not_null<Main::Session*> session);

// The helper's own acceptance on the marked live copy, as stages:
// gram_reconcile_initial (undriven P0; a refusal is a fixture gate that
// skips every later stage), gram_reconcile_stage_stale (the live copy only
// is staged to the first 12 words of its own phrase through
// StageGramAccountLiveWords), gram_reconcile_case_b,
// gram_reconcile_restore_staged (puts back a staged copy case (b) did not
// rewrite, through RestoreGramAccountLiveWords; again at finish),
// gram_reconcile_noop, three driven refusals (gram_reconcile_refuse_signing
// / _address / _no_record), gram_reconcile_case_a_driven and
// gram_reconcile_noop_after_a. Every stage that reaches a reveal or a
// restore is a fixture gate (NoOpenGrant keyringKind=N) without the silent
// Open grant. It rewrites the live copy (staging) and then both copies back to the
// local reveal; it re-imports the device's current phrase once (case (a)),
// which changes the custody record id; it never touches a golden copy
// except through RewriteGramAccountWords with a phrase that signs for the
// served key. No stage emits a deliberate FAIL.
void AppendGramReconcileSelfTest(
	not_null<Runner*> runner,
	Fn<Main::Session*()> resolve);

} // namespace Test
