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
#include <rpl/lifetime.h>

#include <memory>
#include <vector>

namespace Test {

class Runner;

// A scenario that needs a long or foreign locale installs synthetic
// translations into the RUNNING language pack and then has to take them
// back down. The naive removal fails in two independent ways, and
// repairing one leaves the other silent.
//
// Half 1, the leftover value. Lang::Instance::fillFromSerialized
// (lang_instance.cpp:423) never clears _values or _nonDefaultValues
// first: after reading the header it applies only the pairs that were
// NON-DEFAULT in the serialized snapshot (:544-546). A key that was
// default before the fixture overwrote it is absent from that set, so
// the fixture's value survives the restore untouched and is still live
// afterwards.
//
// Half 2, the missing notification. _updated.fire exists at exactly four
// sites in lang_instance.cpp: :257, inside the "#TEST_X" / "#TEST_0"
// branch of switchToId (:250-261); :277, in switchToCustomFile; and :698
// and :700, both in applyDifferenceToMe. On an ordinary id such as en or
// de switchToId fires nothing, and fillFromSerialized fires only
// _idChanges (:550). Lang::details::Value(ushort) (:811-817) is
// rpl::single(Current(key)) | then(Updated() | rpl::map(...)), and that
// is the producer every tr:: phrase funnels into (lang_values.h:104-106),
// so Lang::Updated() is the ONLY signal a bound reactive label re-reads
// on: a restore that does fix the values still leaves an already-painted
// Ui::FlatLabel showing the fixture's text.
//
// The damage is not the leftover value. It is that a check written to
// observe the restore passes VACUOUSLY, because its expectation is
// rebuilt from the live producers at read time, so a restore that did
// nothing matches itself. In run 27 of
// 2026/09/07/prepare-gasless-gram-transfers-for-server-selection three
// rows reported PASS against the wrong language, and only an unrelated
// post-teardown control caught it.
//
// The removal here is switchToId(the frozen identity), then
// fillFromSerialized(the frozen snapshot, AppVersion), then one EMPTY
// MTP_langPackDifference purely to deliver Lang::Updated(). "Apply a
// difference that resets exactly the injected keys" is the wrong shape:
// Instance::resetValue (:762-787) erases the key from _nonDefaultValues
// and restores GetOriginalValue(index), which would discard a legitimate
// pre-existing override the scenario never installed. The frozen
// snapshot brings that override back; a reset would delete it.
//
// The install and the notification both go through the product's own
// reset-bearing entry, Instance::applyDifference (:668-702). Its two
// preconditions (:686-687) - LanguageIdOrDefault(_id) equal to
// qs(difference.vlang_code()), and difference.vfrom_version().v not
// greater than _version - are held HERE rather than copied into each
// scenario: the lang code is always Lang::LanguageIdOrDefault(id()) read
// from the instance immediately before the call, and from_version is
// always 0, which holds for every non-negative version.
//
// The difference's version is always restated and never invented:
// MTP_int(version(Lang::Pack::Current)) read at call time, so
// applyDifferenceToMe's assignment (:689) writes back the value the pack
// already holds. That is required rather than merely harmless -
// CloudManager::applyLangPackData (lang_cloud_manager.cpp:386) branches
// on version(pack) < data.vfrom_version().v, so an invented version
// would change how a genuine cloud difference arriving later in the same
// run is handled. The removal reads the version AFTER fillFromSerialized
// has restored it.
//
// No call this facility makes reaches Local::writeLangPack(). That is
// exact, and it is NOT a guarantee that nothing is written to disk while
// a fixture is installed: writeLangPack() has callers outside
// lang_instance.cpp - lang_cloud_manager.cpp:368, :390 and :392, and
// intro_step.cpp:231 - and applyLangPackData writes the pack whenever a
// non-empty cloud difference arrives (:388-390). A cloud difference
// landing inside the installed window would therefore persist the
// synthetic values into the portable folder. That folder is disposable
// and no end-of-run tidy-up is written for it, but the consequence is
// stated rather than implied away.
//
// Every removal fires _idChanges twice, once from switchToId's reset
// (:304) and once from fillFromSerialized (:550). The only consumers in
// the tree are settings/sections/settings_main.cpp:463 and :813, which
// refresh the Settings language row.
//
// A PLURAL phrase is installed through its suffixed keys. The generated
// table knows only lng_foo#zero .. lng_foo#other, so
// GetKeyIndex("lng_foo") for a phrase<lngtag_count> answers kKeysCount
// and the base name is refused; pass each suffixed form as its own
// LangOverride. Instance::getValue is Expects(key < _values.size())
// (lang_instance.h:90-93), so an unresolved index would abort a Debug
// build: nothing here hands one to getValue or to Lang::details::Value,
// and the unknown-key refusal is evaluated before any reading.
//
// The interface language id and QLocale::setDefault are deliberately NOT
// owned here, for four reasons. First, the subject is key/value
// overrides into the running pack, and injecting long synthetic values
// already produces the long-locale fixture a scenario wants. Second,
// Lang::Instance never SETS the locale: it reads QLocale::system()
// (lang_instance.cpp:311) only to derive a fallback system language
// code, and QLocale::setDefault is called nowhere in the code this
// project compiles (the only calls are in the kcoreaddons submodule's
// autotests, which Telegram does not build). The
// locale governs date and number formatting through a different
// mechanism with a different restore and no relation to Lang::Updated(),
// so folding it in would give one facility two unrelated symmetries and
// make its self-test depend on the host locale. Third, a language switch
// changes _id, which is the very value the applyDifference lang-code
// precondition is read from; keeping the id fixed for a fixture's
// lifetime is what makes that precondition hold by construction rather
// than by the caller's care. Fourth, the frozen snapshot is the WHOLE
// pack state, identity included, so a removal restores the frozen
// language id too - a scenario that wants a different language switches
// before installing, or owns that switch itself.

// One override to install: |key| is a language key name spelled exactly
// as the generated table spells it, |value| the synthetic text. An empty
// |value| is refused, because getNonDefaultValue
// (lang_instance.cpp:722-729) answers an empty QString for both "no
// override" and "an override that is the empty string".
struct LangOverride {
	QByteArray key;
	QString value;
};

// One key's reading, taken BEFORE the install that owns it. |value| is
// Instance::getValue(index) at freeze time and |original| the compiled-in
// Lang::GetOriginalValue(index) beside it, so a key that carried a
// legitimate pre-existing override shows the two apart; |wasNonDefault|
// is that override's presence, which the removal has to restore rather
// than reset away.
struct LangFrozenValue {
	QByteArray key;
	ushort index = 0;
	QString value;
	QString original;
	bool wasNonDefault = false;
	crl::time atMs = 0;
};

// Any value but None is a deliberate falsification arm of the self-test
// below, and a scenario must never pack one. It cannot reach a fixture a
// scenario installs: the fault is carried on the fixture and reaches it
// only through the private LangPackFixture::Install, and InstallLangPack
// always passes None. The arms are selectable solely on
// AppendLangPackSelfTest, which is public and takes the arm as a
// defaulted parameter, so packing one is forbidden rather than
// impossible - exactly what the self-test's own comment below says.
// LeaveOneInstalled performs the whole correct restore and then
// re-installs the FIRST override, so the values end asymmetric.
// SuppressNotification performs switchToId and fillFromSerialized and
// skips the empty difference, so the values end correct and every
// already-painted label still reads the fixture's text.
enum class LangRestoreFault {
	None,
	LeaveOneInstalled,
	SuppressNotification,
};

// The installed fixture, always held through the shared_ptr InstallLangPack
// returns, which is never null. A refused install returns a fixture with
// installed() == false and a non-empty refusal(), and the caller decides
// between a fixture gate (Stage::skipReason) and a FAIL - a refusal that
// is ignored is loud rather than silent, because every oracle below FAILs
// by name when it is asked to certify anything on a fixture that never
// installed.
//
// There is deliberately NO accessor that reads a key's CURRENT value.
// frozen(key) is the only value-returning read, and its content was
// captured inside the install before the difference was applied, so an
// expectation taken AFTER the install is not expressible through this
// facility - which is the whole point, because such an expectation is
// exactly what passes vacuously. The honest limit of that: nothing stops
// a caller from calling Lang::details::Current(index) itself. This
// facility makes the frozen comparison the only one it will certify; it
// does not and cannot seal the product's own accessors.
class LangPackFixture final {
public:
	// Removes the fixture if it is still installed, so the module's live
	// stack never holds a pointer to a destroyed fixture.
	~LangPackFixture();

	[[nodiscard]] bool installed() const;
	[[nodiscard]] QString refusal() const;

	// Null after one Test::Fail naming |key| and every key this fixture
	// did freeze, so a mistyped lookup is a named row rather than an
	// empty string a caller would compare against.
	[[nodiscard]] const LangFrozenValue *frozen(const QByteArray &key) const;

	// A value comparison prints key, index, frozen, original, live,
	// wasNonDefault and isNonDefaultNow through Test::Check's third
	// argument, on the passing verdict as well as the failing one. A
	// parser refusal prints its own detail: the refusal, the raw
	// non-default value, renderedUnchanged, the request, stored and
	// frozen.
	//
	// checkInstalled is what stops an install that silently landed
	// nothing from letting every later row pass vacuously. Its value
	// axis is the requested override. With no placeholder, that text is
	// the expectation. With a placeholder, the expectation is the
	// QString Instance::ParseStrings returns for that requested text,
	// the encoding applyValue stores, and the reading is getValue.
	// getNonDefaultValue is only the non-empty axis, never the value:
	// applyValue writes the raw override before parsing, so a refused
	// parse would otherwise look installed. When ParseStrings omits the
	// key, checkInstalled FAILs by name and quotes the raw non-default
	// value and whether getValue stayed at the frozen reading.
	// checkRestored asserts BOTH that the live value equals the frozen
	// value AND that the key's non-default state equals the frozen one,
	// so "was default
	// before, is default again" is proved rather than merely "reads the
	// same string"; it also carries updatedFires=N, the Lang::Updated()
	// emissions this fixture observed over its own lifetime, as an
	// observation and never as the oracle - the fixture fires that signal
	// itself, so only a bound label can prove delivery.
	//
	// checkInstalled refuses a fixture that never installed and one that
	// was already removed; checkRestored refuses a fixture that never
	// installed and one whose remove() has not run yet. Both refusals are
	// named FAIL rows.
	void checkInstalled(const QString &what);
	void checkRestored(const QString &what);

	// Idempotent, and a no-op on a refused fixture. A removal that is not
	// the top of the module's live stack logs one Test::Fail naming this
	// fixture's keys and the keys of every fixture above it, and then
	// performs its own restore anyway so the resulting state is
	// deterministic. That is detection rather than repair: an
	// out-of-order removal is a caller error.
	void remove();

private:
	LangPackFixture() = default;

	[[nodiscard]] static std::shared_ptr<LangPackFixture> Install(
		not_null<Runner*> runner,
		std::vector<LangOverride> overrides,
		LangRestoreFault fault);

	[[nodiscard]] QString certifyRefusal() const;

	bool _installed = false;
	bool _wasInstalled = false;
	QString _refusal;
	LangRestoreFault _fault = LangRestoreFault::None;
	std::vector<LangOverride> _overrides;
	std::vector<LangFrozenValue> _frozen;
	QString _frozenId;
	QString _frozenBaseId;
	QString _frozenName;
	QString _frozenNativeName;
	QByteArray _frozenSnapshot;
	int _updatedFires = 0;
	rpl::lifetime _updatedLifetime;

	friend std::shared_ptr<LangPackFixture> InstallLangPack(
		not_null<Runner*> runner,
		std::vector<LangOverride> overrides);
	friend void AppendLangPackSelfTest(
		not_null<Runner*> runner,
		LangRestoreFault fault);

};

// Installs |overrides| into the running pack through one applyDifference,
// after freezing the pack identity, the serialize() snapshot and one
// LangFrozenValue per key. Never returns null. The install is refused, by
// name and with every reading it took, for a #custom / #TEST_X / #TEST_0
// pack, an empty |overrides|, a key whose GetKeyIndex answers kKeysCount,
// and an override whose value is empty.
//
// The module registers one Runner::onFinish callback of its own, on the
// first install of the process, which unwinds the live fixtures in
// REVERSE order; AppendLangPackSelfTest registers a second one at
// append time (test_lang_pack.cpp:574) for its own labels and
// fixtures. Runner::finish() runs its callbacks in registration
// order (test_runner.cpp:453-456), which is FIFO and therefore the wrong
// order for nested fixtures, so one registration unwinding LIFO replaces
// one registration per fixture and needs no recursion in remove().
[[nodiscard]] std::shared_ptr<LangPackFixture> InstallLangPack(
	not_null<Runner*> runner,
	std::vector<LangOverride> overrides);

// The facility measuring itself, in seven stages ending with its own
// teardown.
//
// The FIRST stage ARRANGES the precondition the other four need instead
// of searching the running pack for one. Those four need two keys that
// are DEFAULT before the subject fixture installs over them, and on a
// client that has ever downloaded a cloud language pack no such key
// exists anywhere in the table: fillFromSerialized logs the cached
// pack's size as its non-default count (lang_instance.cpp:543), and a
// -testagent run against an ordinary account read "Lang Info: Loaded
// cached, keys: 10993" against a generated table of kKeysCount = 10948
// keys - two counts over different sets, since applyValue writes
// _nonDefaultValues unconditionally (lang_instance.cpp:731-732) and
// so counts cloud keys the generated table does not know - so EVERY key the
// table knows already carries a cloud override. An earlier version of
// this self-test chose its keys by "is this one still default?" and
// gated all of its stages out on that reading. Widening the candidate
// list cannot help, because the property is universal over the table
// rather than specific to the candidates tried.
//
// So the first stage installs an outer HOLDER fixture over one
// throwaway key, which freezes the live cloud pack - identity,
// serialize() snapshot and every reading - inside that fixture, and
// then calls Instance::switchToId with the identity read from the
// instance itself. switchToId's reset (:281-305) rewrites every
// _values[i] from GetOriginalValue(i), clears _nonDefaultValues, zeroes
// _nonDefaultSet and sets _version to 0, and on an ordinary id it fires
// _idChanges only and never _updated (:250-261), so afterwards both
// chosen keys are default by construction. That is the arrangement's
// own premise and it is asserted rather than assumed: one Check prints
// both keys before and after and FAILs there if the reset did not take,
// because every row after it would otherwise measure something else.
// The holder's remove() in the teardown stage puts the real cloud pack
// back through this facility's own switchToId + fillFromSerialized +
// notification path, so the arrangement is undone by the same code the
// self-test exists to measure.
//
// Two consequences of that arrangement, stated rather than implied
// away. The running client reads the compiled-in original values for
// the window between the reset and the holder's removal, so this
// self-test is not to be appended around a scenario leg that reads
// cloud text. And the reset zeroes _version, so a cloud difference
// arriving inside that window is no longer applied on top of the pack
// it was computed against: CloudManager::applyLangPackData
// (lang_cloud_manager.cpp:386) compares version(pack) against
// from_version and re-requests the pack whenever the local version is
// behind, which after the reset it is for every non-zero from_version.
// A full-pack answer (from_version 0) is still applied and written to
// the portable folder, which is the same disposable exposure the
// Local::writeLangPack() paragraph above describes and no wider.
//
// The four stages after it give the second key a legitimate
// pre-existing override through another fixture, bind a parentless
// Ui::FlatLabel to each key through Lang::details::Value(index),
// install synthetic values for both through the subject fixture, and
// after the removal prove both halves at once: checkRestored covers the
// values, including the previously-default key and the preserved
// pre-existing override, and each label's accessibilityName() read-back
// against frozen()->value covers the notification, with no further
// interaction and no re-navigation. The teardown stage then removes
// what is left in REVERSE installation order - the pre-existing
// fixture, then the holder - because remove() FAILs by name when a
// fixture is not the top of the module's live stack.
//
// Before that teardown, two stages certify one placeholder key the
// candidate list omits. lng_dlg_search_from is not a plural, not a
// wallet key and not one of the three excluded classes. The first
// installs "Harness from {user}" and checkInstalled must PASS. The
// second is announced with a Note and installs "Harness from {amount}",
// a tag that key does not accept, so checkInstalled must FAIL. The arm
// does not bind a label. Both fixtures use LangRestoreFault::None and
// are removed there, the broken one first.
//
// It asks the process for nothing: no primary window, no session, no
// chats list, no network, no wallet and no fixture secret. Nothing is
// shown, painted or grabbed - accessibilityName() (labels.h:131-133)
// returns the parsed text the label owns before any layout - and no
// screenshot is taken. It depends on nothing about WHAT the running
// pack holds, because it arranges that itself; the fixture gate that
// remains covers only the two cases the arrangement cannot create - a
// #custom / #TEST_X / #TEST_0 pack, which this facility refuses to
// touch at all, and a candidate key the generated table does not know -
// and both are resolved eagerly at append time and reported as named
// fixture gates writing TEST_RESULT: N/A rows with every candidate's
// reading, never a silent pass and never an opaque timeout.
//
// With LangRestoreFault::None the restore rows emit no deliberate
// failure. The placeholder arm's refused install is the one deliberate
// FAIL in that run, and in the other arms as well; it does not touch
// remove(). The other two arms are deliberate falsifications a
// scenario must never pack:
// LeaveOneInstalled fails checkRestored on the first key and that key's
// read-back row, and SuppressNotification fails both read-back rows while
// checkRestored still passes. Neither arm reaches the holder, which is
// always installed with None, so neither leaves the process's pack
// mutated - the holder froze the live pack before the reset, so removing
// it restores that pack - and each falsified step is announced by one
// Test::Note immediately before it.
void AppendLangPackSelfTest(
	not_null<Runner*> runner,
	LangRestoreFault fault = LangRestoreFault::None);

} // namespace Test
