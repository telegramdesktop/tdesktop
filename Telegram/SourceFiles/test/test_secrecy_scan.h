/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <map>

namespace Test {

class Runner;

// The fixture-secret scan: a campaign on the funded golden wallet hands it
// its secrets once - recovery phrases, short secrets such as the 2SV
// password, custody record ids and secret refs - and gets back a decided
// verdict on whether any of them reached the test log, this launch's app
// log or this launch's DebugLogs parts. It prints counts, public file names,
// TL schema identifiers and the message heads and field names of plain-line
// hits only, never a secret, a field's value or a whole scanned line.
//
// It exists because a hand-rolled scan is open to three traps:
// - Selecting "modified since the scenario started" picked no app log and
//   no DebugLogs part on Windows: a file the running process holds open
//   keeps its old last-write time. Logs are therefore selected by identity
//   only - the main log through Logs::full(), the file the logger holds
//   open, and each DebugLogs quarter-hour part named for a quarter in
//   [process start, scan] whose first line is its own day index. No
//   last-write time takes part anywhere.
// - Matching a short secret as a plain substring over a campaign's MTP
//   dumps, which run to a very large number of lines, gives coincidental
//   hits inside longer, unrelated values. A short secret's occurrence is
//   therefore classed as bounded (no letter or digit right before or after
//   it) or embedded, and only bounded ones count.
// - Even a bounded match inside a dump can be content the server sent: a
//   received value that happens to hold a short secret says nothing about
//   what the client wrote. MTP dump entries are therefore classed by the
//   transport's own header ("Send: " / "Recv: ", session_private.cpp), and
//   only what the client wrote - plain lines and Send entries - decides.
//   Recv hits are reported with their "<top>/<inner>.<field>" schema site
//   and never decide.
//
// A fourth trap is the scan's own: a value the product computes and logs
// itself can equal a short secret. The checks for it come from
// 2026/09/27/report-the-site-of-short-secret-hits-in-the-secrecy-scan.
// Every bounded short-secret hit in a plain line is therefore
// reported at its "<file>|<head>|<field>" site, and a campaign may declare
// (head, field) pairs the product source shows it formats from values it
// computes: a hit that is exactly such a field's value is reported in
// SECRECY_COMPUTED and never decides, like a Recv hit.
//
// A zero is certified only when it can be told from absence: every class
// must hold at least one accepted file and a known-present control (a fresh
// public nonce the scan plants itself, plus any named control), a class
// holding mtp_ parts must show headers of both directions, and in-memory
// canaries run through the same matcher and dump walk on every call.

struct SecrecySecrets {
	// Word runs: ordered adjacent pairs of one phrase's words,
	// case-insensitive. Words are letter-and-digit runs, so any whitespace or
	// punctuation separates them, as do the dump and test-log escapes (\n,
	// \uXXXX) and the dump's type tags. A run carries across the lines of
	// one file, a number is neutral, and any other word or an entry header
	// ends it.
	std::vector<std::vector<QString>> phrases;
	// Each occurrence is bounded or embedded; only bounded ones count.
	std::vector<QString> shortSecrets;
	// Record ids, secret refs, "secret/" storage keys: every substring
	// occurrence counts. Anything shorter than 16 characters is refused as
	// "not a token; hand it as a short secret".
	std::vector<QString> tokens;
};

enum class SecrecyClass {
	TestLog,
	AppLog,
	DebugLog,
	MtpLog,
	EarlierParts,
};
[[nodiscard]] QString SecrecyClassName(SecrecyClass value);

// An explicitly named DebugLogs part of an earlier launch. It is scanned
// whole and accepted only when its first line is |day|'s index and it holds
// |control| at least once.
struct SecrecyEarlierPart {
	QString name; // "log_HH_MM.txt" or "mtp_HH_MM.txt", file name only
	QDate day;
	QString control; // a public string that part must contain
};

enum class PartIdentity {
	Missing,
	Unreadable,
	Foreign,
	Accepted,
};
[[nodiscard]] QString PartIdentityName(PartIdentity value);

struct SecrecySource {
	SecrecyClass cls = SecrecyClass::TestLog;
	QString name; // public display name, e.g. "DebugLogs/mtp_20_45.txt"
	QString path; // the file to read, empty when |text| is used
	QString text; // in-memory content (Logs::full())
	bool fromText = false;
	bool invalidName = false; // an earlier part whose name was refused
	// A window part is scanned from its last "NEW LOGGING INSTANCE
	// STARTED!!!" banner, so an earlier same-day launch's lines in an
	// appended part are not charged to this launch.
	bool sliceAtLastBanner = false;
	PartIdentity identity = PartIdentity::Accepted;
	QString control; // this source's extra known-present control
};

struct SecrecyLaunchLogs {
	std::vector<SecrecySource> sources; // every candidate with its identity
	QDateTime launchStart;
	QDateTime scanAt;
	int candidates = 0; // window candidates, both DebugLogs kinds
};

// Pure selection: the test log at |evidenceDir|/test_log.txt, the app log
// text handed in, every DebugLogs part named for a quarter hour in
// [launchStart, scanAt], each accepted only when its first line equals that
// part's own yyyyMMdd, plus |earlier| under the same identity rule against
// its own day. |workingDir| and |evidenceDir| end with a slash.
[[nodiscard]] SecrecyLaunchLogs SelectLaunchLogs(
	const QString &workingDir,
	const QString &evidenceDir,
	const QString &appLogText,
	const QDateTime &launchStart,
	const QDateTime &scanAt,
	const std::vector<SecrecyEarlierPart> &earlier);

// The real launch: SelectLaunchLogs(cWorkingDir(), EvidenceDir(),
// Logs::full(), now - crl::now(), now, earlier).
[[nodiscard]] SecrecyLaunchLogs SelectThisLaunchLogs(
	const std::vector<SecrecyEarlierPart> &earlier);

struct SecrecyClassReading {
	SecrecyClass cls = SecrecyClass::TestLog;
	int candidates = 0;
	int files = 0;
	int foreign = 0;
	int unreadable = 0;
	int missing = 0;
	int invalidNames = 0;
	int withoutControl = 0; // accepted earlier parts missing their control
	int mtpFiles = 0;
	int lines = 0;
	int wordRunsOther = 0;
	int wordRunsSend = 0;
	int wordRunsRecv = 0;
	int bounded = 0;
	int embedded = 0;
	int boundedOther = 0;
	int boundedSend = 0;
	int boundedRecv = 0;
	int boundedComputed = 0; // exactly a declared computed field's value
	int boundedQuoted = 0;
	int boundedUnquoted = 0;
	int tokensOther = 0;
	int tokensSend = 0;
	int tokensRecv = 0;
	int sendHeaders = 0;
	int recvHeaders = 0;
	int plantedHits = 0;
	int controlHits = 0;
	bool namedControl = false;
	bool fromLogger = false;
	std::map<QString, int> sendSites; // schema identifiers only
	std::map<QString, int> recvSites;
	// Plain-line bounded short-secret hits by "<file>|<head>|<field>"
	// ("<file>|?|?" withheld, "-" not exactly one field's value): the
	// deciding ones, and exact values of a declared computed field.
	std::map<QString, int> plainSites;
	std::map<QString, int> computedSites;
	QStringList names; // public file names of the scanned files
	// Per name: the 1-based line of the last banner the scan started
	// from, 0 when the file was scanned whole.
	std::vector<int> slicedFrom;
	bool decided = false;
	QString undecidedReason;

	[[nodiscard]] int clientWritten() const;
	[[nodiscard]] int received() const;
};

struct SecrecyReading {
	// TestLog, AppLog, DebugLog, MtpLog, then EarlierParts when any earlier
	// part was named.
	std::vector<SecrecyClassReading> classes;

	int canaryWordRuns = 0;
	int canaryWordRunsExpected = 0;
	int canaryBounded = 0;
	int canaryEmbedded = 0;
	int canarySend = 0;
	int canaryRecv = 0;
	int canaryShortExpected = 0;
	int canaryToken = 0;
	int canaryTokenExpected = 0;
	bool canariesHold = false;

	int phrasesGiven = 0;
	int shortGiven = 0;
	int tokensGiven = 0;
	int tokensTooShort = 0;
	int phrasesTooShort = 0;

	bool decided = false;
	bool clean = false;
	QStringList undecidedClasses;
	QStringList undecidedReasons; // scan-level reasons

	QDateTime launchStart;
	QDateTime scanAt;
	int candidates = 0;
	int accepted = 0;
	int foreign = 0;
	int missing = 0;
	int unreadable = 0;
	QStringList candidateIdentities; // "<name>=<identity>", window parts

	[[nodiscard]] const SecrecyClassReading *find(SecrecyClass cls) const;
	[[nodiscard]] int clientWritten() const;
	[[nodiscard]] int received() const;
	[[nodiscard]] int computed() const;
};

// A field the product formats from a value it computes itself, never from
// fixture input, spelled exactly as a plain-line site prints it: the
// message head of its line (the text after the timestamp or entry prefix
// and before the first name= field) and the field name.
struct SecrecyComputedField {
	QString head;
	QString field;
};

struct SecrecyScanArgs {
	SecrecySecrets secrets;
	std::vector<SecrecyEarlierPart> earlierParts;
	// Extra known-present controls per class (public strings); the planted
	// nonce is always required in addition, and a named control that is
	// set must also be hit.
	QString testLogControl;
	QString appLogControl;
	QString debugLogControl;
	QString mtpLogControl;
	// A bounded short-secret hit that is exactly such a field's value on a
	// line with that head is reported in SECRECY_COMPUTED and does not
	// decide. Declare a field only when the product source shows it is
	// formatted from a value the product computes, never from fixture
	// input. No declaration exempts a word run, a token or a Send entry.
	std::vector<SecrecyComputedField> computedFields;
};

// Pure: reads the selected files and text and prints nothing.
// |plantedControl| is the nonce every non-earlier class must contain.
// |computedFields| are the declared computed fields.
[[nodiscard]] SecrecyReading ReadSecrecy(
	const SecrecySecrets &secrets,
	const SecrecyLaunchLogs &logs,
	const QString &plantedControl,
	const std::vector<SecrecyComputedField> &computedFields = {});

// Public-only rows of a reading: SECRECY_SCAN, SECRECY_WINDOW, one
// SECRECY_CLASS per class (with its plain-line sites), SECRECY_RECEIVED and
// SECRECY_COMPUTED. Never a secret, a field's value or a whole scanned
// line.
[[nodiscard]] QStringList SecrecyRows(const SecrecyReading &reading);

// Campaign entry point. Plants a fresh public nonce through Note (the test
// log), LOG (the main log and the DebugLogs log_ part) and MTP_LOG (the
// mtp_ part, written only while debug logging is on), selects this launch's
// logs, reads them, reports each class through DiscriminatingScan, notes the
// rows, Checks "every class decided" and "no client-written fixture secret",
// then re-reads the bytes it appended to test_log.txt and Checks that they
// carry no secret either. Returns decided && clean. DebugLogs must be
// enabled (tdata/withdebug = 1), or the DebugLog and MtpLog classes are
// undecided. Hits on declared computed fields (args.computedFields) are
// reported and do not decide.
bool CheckSecrecy(const SecrecyScanArgs &args, const QString &what);

// Ten session-free stages over synthetic log trees in a QTemporaryDir,
// with synthetic secrets only: canaries, embedded vs bounded, a Recv hit
// that is reported but does not decide, ten client-written leaks (among
// them words joined by line breaks, escapes and commas, and a vector
// dumped one element per line), the plain-line site report and declared
// computed fields (an undeclared field's value fails and is named with its
// class, file, head and field; a declared one is reported and does not
// decide; another field, free text, part of the value, a word run, a token
// and a Send entry still decide; a site that would print a secret is
// withheld), the undecided refusals, identity selection (a same-name part
// of another day and an earlier launch's lines before the banner are not
// counted), the rows printing no secret, overlay telemetry (the digit
// runs of an ordinary decimal value, as synthetic short secrets, fail at
// the row's "<file>|<head>|-" site; the same value as a TelemetryNumber
// holds them only embedded and that scan is clean; every TelemetryNumber
// of the stage's values prints as documented and holds no bounded digit
// run, while the ordinary decimal of every finite value holds one), and
// the shared helpers' numbers. That last stage reads each converted
// shared-helper row through its own formatter for fixed synthetic values:
// without the request it prints today's text, and under it the same text
// with only a "p" after each number; the ordinary text's digit runs, as
// synthetic short secrets, fail the scan at that row, while the requested
// row holds none bounded and scans clean; and a synthetic phrase in its
// caller text gives the same word runs in both formats, beside a premise
// that a TelemetryNumber between phrase words ends a run. It never asks
// the process-wide request (RequestTelemetryNumbers). Emits no deliberate
// FAIL.
void AppendSecrecyScanSelfTest(not_null<Runner*> runner);

} // namespace Test
