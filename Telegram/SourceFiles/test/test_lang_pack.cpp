/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_lang_pack.h"

#include "base/unique_qptr.h"
#include "core/version.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_text_reads.h"
#include "ui/widgets/labels.h"

#include "lang_auto_counts.h" // kKeysCount.

#include "styles/style_widgets.h"

namespace Test {
namespace {

// Untagged phrase<> keys, all present in the generated table. Three of
// them are taken per self-test run - the two subject keys and the
// holder's throwaway key - and they are chosen by resolvability alone,
// never by what the running pack currently holds for them, for the
// reason ChooseLangKey below states.
//
// Three classes are deliberately absent. lng_language_name is the value
// Instance::name() and nativeName() fall back to
// (lang_instance.cpp:341-351), so overwriting it would rewrite the very
// identity a fixture freezes. lng_send_action_choose_sticker and
// lng_user_action_choose_sticker re-run updateChoosingStickerReplacement()
// from both applyValue (:740-747) and resetValue (:778-785). And every
// TAGGED phrase is excluded from this list. These stages' label
// read-back compares accessibilityName() to the raw override, and
// ValueParser stores a four-character kTextCommand replacer (:137-141)
// that would not match. The placeholder arm below certifies those
// keys without that read-back.
constexpr const char *kKeyCandidates[] = {
	"lng_cancel",
	"lng_continue",
	"lng_close",
	"lng_box_ok",
	"lng_menu_settings",
	"lng_settings_information",
	"lng_profile_copy_phone",
	"lng_maps_point",
};

// The chosen key with the readings the choice was made from. |readings|
// is never empty: it carries every candidate's index, override and live
// value, so a refusal names what it looked at.
struct LangKeyChoice {
	QByteArray key;
	ushort index = 0;
	QString readings;
	QString refusal;
};

[[nodiscard]] std::vector<LangPackFixture*> &LiveFixtures() {
	static auto result = std::vector<LangPackFixture*>();
	return result;
}

[[nodiscard]] bool &FinishRegistered() {
	static auto result = false;
	return result;
}

// Lang::GetKeyIndex answers kKeysCount for a key the generated table does
// not know, Instance::getValue is Expects(key < _values.size())
// (lang_instance.h:90-93) and Lang::GetOriginalValue is
// Expects(key < kKeysCount) (the generated lang_auto.cpp), so reading
// either for an unresolved index would abort a Debug build. The unknown
// branch therefore prints what it can read by name alone and touches
// neither. |original| is printed beside |value| because a reading that
// says a key is default has to show what default it means.
[[nodiscard]] QString FormatKeyReading(const QByteArray &key) {
	const auto &instance = Lang::GetInstance();
	const auto index = Lang::GetKeyIndex(QLatin1String(key));
	const auto nonDefault = instance.getNonDefaultValue(key);
	return (index == Lang::kKeysCount)
		? u"key=%1 index=unknown nonDefault=\"%2\" value=<n/a> "
			"original=<n/a>"_q.arg(
				QString::fromUtf8(key),
				nonDefault)
		: u"key=%1 index=%2 nonDefault=\"%3\" value=\"%4\" "
			"original=\"%5\""_q.arg(
				QString::fromUtf8(key),
				QString::number(index),
				nonDefault,
				instance.getValue(index),
				Lang::GetOriginalValue(index));
}

[[nodiscard]] QString NameList(const std::vector<QByteArray> &keys) {
	auto names = QStringList();
	for (const auto &key : keys) {
		names.push_back(QString::fromUtf8(key));
	}
	return names.isEmpty() ? u"none"_q : names.join(QChar(','));
}

[[nodiscard]] QString KeyList(const std::vector<LangOverride> &overrides) {
	auto keys = std::vector<QByteArray>();
	keys.reserve(overrides.size());
	for (const auto &entry : overrides) {
		keys.push_back(entry.key);
	}
	return NameList(keys);
}

[[nodiscard]] QString FormatComparison(
		const LangFrozenValue &frozen,
		const QString &expected,
		const QString &live,
		bool isNonDefaultNow) {
	// One multi-argument arg(), never a chain: the compared strings are
	// arbitrary product text and may themselves carry a % sequence, which
	// a chained arg() would take for the next placeholder.
	return u"key=%1 index=%2 expected=\"%3\" live=\"%4\" frozen=\"%5\" "
		"original=\"%6\" wasNonDefault=%7 isNonDefaultNow=%8"_q.arg(
			QString::fromUtf8(frozen.key),
			QString::number(frozen.index),
			expected,
			live,
			frozen.value,
			frozen.original,
			QString::number(frozen.wasNonDefault ? 1 : 0),
			QString::number(isNonDefaultNow ? 1 : 0));
}

[[nodiscard]] QString PackRefusal() {
	const auto &instance = Lang::GetInstance();
	return instance.isCustom()
		? u"custom language pack refused: id=%1 - switchToId would rewrite "
			"every value through PrepareTestValue and fire Lang::Updated() "
			"(lang_instance.cpp:252-259), and fillFromSerialized could "
			"reach Local::writeLangPack() through its custom-file branch "
			"(:473-487)"_q.arg(instance.id())
		: QString();
}

[[nodiscard]] QString RefuseInstall(
		const std::vector<LangOverride> &overrides) {
	const auto pack = PackRefusal();
	if (!pack.isEmpty()) {
		return pack;
	} else if (overrides.empty()) {
		return u"empty override list refused: a fixture that installs "
			"nothing certifies nothing, so every later check against it "
			"would pass vacuously"_q;
	}
	for (const auto &entry : overrides) {
		const auto index = Lang::GetKeyIndex(QLatin1String(entry.key));
		if (index == Lang::kKeysCount) {
			return u"unknown language key refused: %1 - GetKeyIndex answers "
				"kKeysCount, which is a stale or mistyped literal, or the "
				"base name of a plural phrase whose suffixed forms "
				"(#zero .. #other) are the only keys the generated table "
				"knows"_q.arg(FormatKeyReading(entry.key));
		} else if (entry.value.isEmpty()) {
			return u"empty override value refused: %1 - getNonDefaultValue "
				"answers an empty QString for both \"no override\" and \"an "
				"override that is the empty string\", so an empty install "
				"could not be told apart from a default"_q.arg(
					FormatKeyReading(entry.key));
		}
	}
	return QString();
}

[[nodiscard]] QVector<MTPLangPackString> MakeStrings(
		const std::vector<LangOverride> &overrides) {
	auto result = QVector<MTPLangPackString>();
	result.reserve(int(overrides.size()));
	for (const auto &entry : overrides) {
		result.push_back(MTP_langPackString(
			MTP_bytes(entry.key),              // key
			MTP_bytes(entry.value.toUtf8()))); // value
	}
	return result;
}

// Both Expects in applyDifferenceToMe (lang_instance.cpp:686-687) hold by
// construction here: the lang code is LanguageIdOrDefault of the
// instance's own id, read immediately before the call, and from_version
// is always 0. The version is restated rather than invented, so the
// assignment at :689 writes back the value the pack already holds - which
// matters because CloudManager::applyLangPackData
// (lang_cloud_manager.cpp:386) branches on it. The difference is bound to
// a named local because data() hands out a reference into the boxed data
// that local owns. An EMPTY |strings| applies and resets nothing, so its
// only effect is the _updated fire at :698 - the notification half.
void ApplyStrings(
		Lang::Instance &instance,
		const QVector<MTPLangPackString> &strings) {
	const auto difference = MTP_langPackDifference(
		MTP_string(Lang::LanguageIdOrDefault(instance.id())), // lang_code
		MTP_int(0),                                           // from_version
		MTP_int(instance.version(Lang::Pack::Current)),       // version
		MTP_vector<MTPLangPackString>(strings));              // strings
	instance.applyDifference(Lang::Pack::Current, difference.data());
}

// Chosen by RESOLVABILITY alone. Whether a key is currently DEFAULT is
// deliberately NOT a criterion: on any client that has ever downloaded a
// cloud language pack, every key the generated table knows already
// carries an override - fillFromSerialized logs the cached pack's
// non-default count (lang_instance.cpp:543) and an ordinary account read
// 10993 of them against kKeysCount = 10948 - so a search for a
// currently-default key finds none and gates the whole self-test out,
// and a longer candidate list cannot help because the property is
// universal over the table. AppendLangPackSelfTest's first stage
// arranges that precondition through switchToId instead.
[[nodiscard]] LangKeyChoice ChooseLangKey(
		const std::vector<QByteArray> &besides) {
	auto result = LangKeyChoice();
	auto readings = QStringList();
	for (const auto candidate : kKeyCandidates) {
		const auto key = QByteArray(candidate);
		readings.push_back(FormatKeyReading(key));
		if (!result.key.isEmpty()
			|| (ranges::find(besides, key) != end(besides))) {
			continue;
		}
		const auto index = Lang::GetKeyIndex(QLatin1String(key));
		if (index != Lang::kKeysCount) {
			result.key = key;
			result.index = index;
		}
	}
	result.readings = readings.join(u"; "_q);
	const auto pack = PackRefusal();
	result.refusal = !pack.isEmpty()
		? pack
		: !result.key.isEmpty()
		? QString()
		: u"no candidate language key resolves in the generated table "
			"(besides=%1): %2"_q.arg(NameList(besides), result.readings);
	return result;
}

} // namespace

LangPackFixture::~LangPackFixture() {
	remove();
}

bool LangPackFixture::installed() const {
	return _installed;
}

QString LangPackFixture::refusal() const {
	return _refusal;
}

const LangFrozenValue *LangPackFixture::frozen(
		const QByteArray &key) const {
	const auto i = ranges::find(_frozen, key, &LangFrozenValue::key);
	if (i != end(_frozen)) {
		return &*i;
	}
	auto names = QStringList();
	for (const auto &value : _frozen) {
		names.push_back(QString::fromUtf8(value.key));
	}
	Fail(
		u"lang pack fixture: no frozen reading for key %1"_q.arg(
			QString::fromUtf8(key)),
		u"frozen=[%1] refusal=%2"_q.arg(
			names.isEmpty() ? u"none"_q : names.join(QChar(',')),
			_refusal.isEmpty() ? u"none"_q : _refusal));
	return nullptr;
}

QString LangPackFixture::certifyRefusal() const {
	return _wasInstalled
		? QString()
		: u"fixture never installed: %1"_q.arg(
			_refusal.isEmpty() ? u"no refusal recorded"_q : _refusal);
}

void LangPackFixture::checkInstalled(const QString &what) {
	const auto refused = certifyRefusal();
	if (!refused.isEmpty()) {
		Fail(what, refused);
		return;
	} else if (!_installed) {
		Fail(
			what,
			u"fixture already removed: keys=[%1] - checkInstalled certifies "
			"the installed window only"_q.arg(KeyList(_overrides)));
		return;
	}
	const auto &instance = Lang::GetInstance();
	auto ok = true;
	auto details = QStringList();
	for (auto i = 0, count = int(_frozen.size()); i != count; ++i) {
		const auto &frozen = _frozen[i];
		const auto &requested = _overrides[i].value;
		const auto stored = instance.getValue(frozen.index);
		const auto nonDefault = instance.getNonDefaultValue(frozen.key);
		const auto isNonDefaultNow = !nonDefault.isEmpty();
		// WHY: ParseStrings is the product parser applyValue uses, run
		// on the requested override alone. getNonDefaultValue is the raw
		// text applyValue writes before that parse, so it would certify
		// a placeholder the product refused.
		auto one = std::vector<LangOverride>();
		one.push_back(_overrides[i]);
		const auto parsed = Lang::Instance::ParseStrings(
			MTP_vector<MTPLangPackString>(MakeStrings(one)));
		const auto parsedAt = parsed.find(frozen.index);
		if (parsedAt == parsed.end()) {
			ok = false;
			const auto unchanged = (stored == frozen.value);
			details.push_back(
				u"key=%1 index=%2 parser refused: ParseStrings omitted "
				"the key nonDefault=\"%3\" renderedUnchanged=%4 "
				"requested=\"%5\" stored=\"%6\" frozen=\"%7\""_q.arg(
					QString::fromUtf8(frozen.key),
					QString::number(frozen.index),
					nonDefault,
					QString::number(unchanged ? 1 : 0),
					requested,
					stored,
					frozen.value));
			continue;
		}
		const auto hasPlaceholder = requested.contains(QLatin1String("{"));
		const auto &expected = hasPlaceholder
			? parsedAt->second
			: requested;
		if ((stored != expected) || !isNonDefaultNow) {
			ok = false;
		}
		auto line = FormatComparison(
			frozen,
			expected,
			stored,
			isNonDefaultNow);
		if (hasPlaceholder) {
			line += u" requested=\"%1\""_q.arg(requested);
		}
		details.push_back(std::move(line));
	}
	Check(ok, what, details.join(u"; "_q));
}

void LangPackFixture::checkRestored(const QString &what) {
	const auto refused = certifyRefusal();
	if (!refused.isEmpty()) {
		Fail(what, refused);
		return;
	} else if (_installed) {
		Fail(
			what,
			u"fixture still installed: keys=[%1] - checkRestored certifies "
			"the state after remove()"_q.arg(KeyList(_overrides)));
		return;
	}
	const auto &instance = Lang::GetInstance();
	auto ok = true;
	auto details = QStringList();
	for (const auto &frozen : _frozen) {
		const auto live = instance.getValue(frozen.index);
		const auto isNonDefaultNow
			= !instance.getNonDefaultValue(frozen.key).isEmpty();
		if ((live != frozen.value)
			|| (isNonDefaultNow != frozen.wasNonDefault)) {
			ok = false;
		}
		details.push_back(
			FormatComparison(frozen, frozen.value, live, isNonDefaultNow));
	}
	details.push_back(u"updatedFires=%1"_q.arg(_updatedFires));
	Check(ok, what, details.join(u"; "_q));
}

void LangPackFixture::remove() {
	if (!_installed) {
		return;
	}
	auto &live = LiveFixtures();
	const auto i = ranges::find(live, this);
	if ((i != end(live)) && ((i + 1) != end(live))) {
		auto above = QStringList();
		for (auto j = i + 1; j != end(live); ++j) {
			above.push_back(KeyList((*j)->_overrides));
		}
		Fail(
			u"lang pack fixture: out-of-order removal"_q,
			u"removing=[%1] stillInstalledAbove=[%2] - the restore below "
			"runs anyway so the pack ends deterministic, but this "
			"fixture's snapshot predates every fixture above it"_q.arg(
				KeyList(_overrides),
				above.join(u" | "_q)));
	}
	auto &instance = Lang::GetInstance();

	// The plural id is deliberately left empty. Instance::reset derives
	// computedPluralId = pluralId ?: baseId ?: id
	// (lang_instance.cpp:282-286), and fillFromSerialized then overwrites
	// _pluralId with the serialized one (:532-536) and re-runs
	// updatePluralRules() (:547), so the serialized pack decides it.
	// There is no public pluralId() getter to freeze it from, and none is
	// needed.
	instance.switchToId({
		.id = _frozenId,
		.baseId = _frozenBaseId,
		.name = _frozenName,
		.nativeName = _frozenNativeName,
	});
	instance.fillFromSerialized(_frozenSnapshot, AppVersion);
	if (_fault == LangRestoreFault::SuppressNotification) {
		Note(u"lang pack fixture: LangRestoreFault::SuppressNotification - "
			"the empty difference that delivers Lang::Updated() is "
			"deliberately skipped, so the values are restored while every "
			"already-painted label keeps the fixture's text"_q);
	} else {
		ApplyStrings(instance, QVector<MTPLangPackString>());
	}
	if (_fault == LangRestoreFault::LeaveOneInstalled) {
		Note(u"lang pack fixture: LangRestoreFault::LeaveOneInstalled - the "
			"correct restore is complete and key %1 is deliberately "
			"re-installed, so that key's checkRestored reading and its "
			"label read-back are both expected to FAIL"_q.arg(
				QString::fromUtf8(_overrides.front().key)));
		ApplyStrings(instance, MakeStrings({ _overrides.front() }));
	}
	const auto j = ranges::find(live, this);
	if (j != end(live)) {
		live.erase(j);
	}
	_installed = false;
}

std::shared_ptr<LangPackFixture> LangPackFixture::Install(
		not_null<Runner*> runner,
		std::vector<LangOverride> overrides,
		LangRestoreFault fault) {
	auto result = std::shared_ptr<LangPackFixture>(new LangPackFixture());
	result->_fault = fault;
	result->_overrides = std::move(overrides);

	const auto refusal = RefuseInstall(result->_overrides);
	if (!refusal.isEmpty()) {
		result->_refusal = refusal;
		// A Note rather than a Fail: the caller decides between a fixture
		// gate and a FAIL, and every oracle on this fixture FAILs by name
		// anyway, so an ignored refusal is loud rather than silent.
		Note(u"lang pack fixture: install refused - %1"_q.arg(refusal));
		return result;
	}

	auto &instance = Lang::GetInstance();
	result->_frozenId = instance.id();
	result->_frozenBaseId = instance.baseId();
	result->_frozenName = instance.name();
	result->_frozenNativeName = instance.nativeName();
	result->_frozenSnapshot = instance.serialize();
	const auto atMs = crl::now();
	for (const auto &entry : result->_overrides) {
		const auto index = Lang::GetKeyIndex(QLatin1String(entry.key));
		const auto wasNonDefault
			= !instance.getNonDefaultValue(entry.key).isEmpty();
		result->_frozen.push_back({
			.key = entry.key,
			.index = index,
			.value = instance.getValue(index),
			.original = Lang::GetOriginalValue(index),
			.wasNonDefault = wasNonDefault,
			.atMs = atMs,
		});
	}
	const auto raw = result.get();
	instance.updated(
	) | rpl::on_next([=] {
		++raw->_updatedFires;
	}, result->_updatedLifetime);

	LiveFixtures().push_back(raw);
	result->_installed = true;
	result->_wasInstalled = true;
	ApplyStrings(instance, MakeStrings(result->_overrides));

	if (!FinishRegistered()) {
		FinishRegistered() = true;

		// One registration for the whole process. Runner::finish() runs
		// its callbacks in registration order (test_runner.cpp:453-456),
		// which is FIFO, so one callback per fixture would remove the
		// outermost first and restore a snapshot taken before the inner
		// fixtures installed. Unwinding LIFO from a single callback fixes
		// that with no recursion in remove(), which erases the fixture it
		// removed, so this loop always shortens.
		runner->onFinish([] {
			auto &live = LiveFixtures();
			while (!live.empty()) {
				live.back()->remove();
			}
		});
	}
	return result;
}

std::shared_ptr<LangPackFixture> InstallLangPack(
		not_null<Runner*> runner,
		std::vector<LangOverride> overrides) {
	return LangPackFixture::Install(
		runner,
		std::move(overrides),
		LangRestoreFault::None);
}

void AppendLangPackSelfTest(
		not_null<Runner*> runner,
		LangRestoreFault fault) {
	struct State {
		LangKeyChoice keyHolder;
		LangKeyChoice keyA;
		LangKeyChoice keyB;
		QString skipReason;
		QString beforeReset;
		QString afterReset;
		QString defaultA;
		std::shared_ptr<LangPackFixture> holder;
		std::shared_ptr<LangPackFixture> preexisting;
		std::shared_ptr<LangPackFixture> subject;
		std::shared_ptr<LangPackFixture> tagged;
		std::shared_ptr<LangPackFixture> taggedBroken;
		QByteArray taggedKey;
		QString taggedSkip;
		base::unique_qptr<Ui::FlatLabel> labelA;
		base::unique_qptr<Ui::FlatLabel> labelB;
		QString readA;
		QString readB;
	};
	// Leaked on purpose, the way this directory's other self-tests leak
	// theirs: the stages outlive this call. The teardown stage releases
	// both labels and removes the two fixtures still installed, but a
	// timed-out stage and the watchdog skip every stage after them, so
	// that stage is not the release point (README.md:483-490). Two
	// distinct Runner::onFinish backstops cover such a run: the module's
	// single registration unwinds the FIXTURES and captures nothing, so
	// it cannot reach this State, and the registration just below does
	// reach it - it releases the LABELS, each of which holds a live
	// consumer inside Lang::Instance::_updated for as long as it exists
	// (lib_ui/ui/widgets/labels.cpp:239-244), which is exactly what
	// README.md:473-481 makes the scenario own, and it takes this
	// self-test's own fixtures down in reverse installation order, so
	// the live cloud pack the holder froze is restored even on a run
	// that never reaches the teardown stage.
	const auto state = new State();

	// finish() runs on every path that reaches it, and also when a
	// teardown stage already ran (test_runner.h:85-95), so this has to be
	// safe afterwards: assigning nullptr to an already-null
	// base::unique_qptr is a no-op, and remove() is idempotent - a no-op
	// on a fixture already removed and on one that never installed. The
	// removals are in REVERSE installation order, because remove() FAILs
	// by name when a fixture is not the top of the module's live stack,
	// and they follow the label release so no Lang::Updated() they fire
	// reaches a label. This registration is made at append time and the
	// module's own on the first install, and finish() runs its callbacks
	// FIFO (test_runner.cpp:453-456), so the module's unwind runs after
	// these and finds nothing left to unwind.
	runner->onFinish([=] {
		state->labelA = nullptr;
		state->labelB = nullptr;
		if (state->taggedBroken) {
			state->taggedBroken->remove();
		}
		if (state->tagged) {
			state->tagged->remove();
		}
		if (state->subject) {
			state->subject->remove();
		}
		if (state->preexisting) {
			state->preexisting->remove();
		}
		if (state->holder) {
			state->holder->remove();
		}
	});

	// Resolved eagerly, at append time rather than in a stage:
	// Local::readLangPack() runs inside storage/localstorage.cpp:426, long
	// before Application::run() reaches Test::Start(), so the pack is
	// loaded here and every stage's skipReason stays a pure read.
	state->keyA = ChooseLangKey({});
	state->keyB = ChooseLangKey({ state->keyA.key });
	state->keyHolder = ChooseLangKey({ state->keyA.key, state->keyB.key });
	state->skipReason = !state->keyA.refusal.isEmpty()
		? u"fixture gate: %1"_q.arg(state->keyA.refusal)
		: !state->keyB.refusal.isEmpty()
		? u"fixture gate: %1"_q.arg(state->keyB.refusal)
		: !state->keyHolder.refusal.isEmpty()
		? u"fixture gate: %1"_q.arg(state->keyHolder.refusal)
		: QString();

	// Not one of kKeyCandidates: those stages compare a label to the raw
	// override. This key is phrase<lngtag_user>, not a plural and not a
	// wallet key, and it accepts {user} only, so {amount} is the
	// parser's unexpected-tag refusal.
	state->taggedKey = QByteArray("lng_dlg_search_from");
	state->taggedSkip = (Lang::GetKeyIndex(QLatin1String(state->taggedKey))
			== Lang::kKeysCount)
		? u"fixture gate: lng_dlg_search_from does not resolve in the "
			"generated table"_q
		: QString();

	const auto gate = [=] { return state->skipReason; };
	const auto taggedGate = [=] {
		return !state->skipReason.isEmpty()
			? state->skipReason
			: state->taggedSkip;
	};
	const auto heldByHolder = u"lang pack self-test holder override"_q;
	const auto arrangedB = u"lang pack self-test arranged override"_q;
	const auto installedA = u"lang pack self-test installed value A"_q;
	const auto installedB = u"lang pack self-test installed value B"_q;
	const auto taggedInstalled = u"Harness from {user}"_q;
	const auto taggedRefused = u"Harness from {amount}"_q;

	runner->add({
		.name = u"lang pack self-test: hold the live pack and reset it so "
			"both keys are default"_q,
		.skipReason = gate,
		.run = [=] {
			auto &instance = Lang::GetInstance();
			state->beforeReset = u"%1; %2"_q.arg(
				FormatKeyReading(state->keyA.key),
				FormatKeyReading(state->keyB.key));

			// Installed BEFORE the reset below and removed LAST, so the
			// live pack - identity, serialize() snapshot and every
			// reading - is frozen inside this fixture and comes back
			// through the facility's own restore. Its one throwaway
			// override is what makes the install legal, an empty list
			// being refused, and it names a third key so neither subject
			// key carries a reading this fixture took. Always None: an
			// arm reaching the holder would leave the process's real pack
			// mutated after the run.
			auto overrides = std::vector<LangOverride>();
			overrides.push_back({
				.key = state->keyHolder.key,
				.value = heldByHolder,
			});
			state->holder = LangPackFixture::Install(
				runner,
				std::move(overrides),
				LangRestoreFault::None);
			if (!state->holder->installed()) {
				// Nothing would put the live pack back afterwards, so
				// the reset is not performed at all and the check below
				// FAILs on the refusal Install already logged.
				state->afterReset = u"not reset, holder refused: %1"_q.arg(
					state->holder->refusal());
				return;
			}

			// The identity is read from the instance itself, so this is
			// the running pack's own id and not a language switch.
			// reset (lang_instance.cpp:281-305) rewrites every _values[i]
			// from GetOriginalValue(i), clears _nonDefaultValues, zeroes
			// _nonDefaultSet and sets _version to 0; on an ordinary id
			// switchToId fires _idChanges only and never _updated
			// (:250-261). Every key is default afterwards - the
			// precondition the four stages below need, which no client
			// that has downloaded a cloud pack provides on its own.
			instance.switchToId({
				.id = instance.id(),
				.baseId = instance.baseId(),
				.name = instance.name(),
				.nativeName = instance.nativeName(),
			});
			state->afterReset = u"%1; %2"_q.arg(
				FormatKeyReading(state->keyA.key),
				FormatKeyReading(state->keyB.key));
		},
		.then = [=] {
			const auto &instance = Lang::GetInstance();
			const auto isDefault = [&](const LangKeyChoice &choice) {
				return instance.getNonDefaultValue(choice.key).isEmpty()
					&& (instance.getValue(choice.index)
						== Lang::GetOriginalValue(choice.index));
			};
			// The arrangement's own premise, asserted rather than
			// assumed: if the holder did not install or the reset did
			// not take, every row after this one would be measuring
			// something other than a restored previously-default key, so
			// it fails here and by name instead of silently later.
			Check(
				(state->holder->installed()
					&& isDefault(state->keyA)
					&& isDefault(state->keyB)),
				u"lang pack self-test: both keys are default after the "
				"live pack was held and reset"_q,
				u"before=[%1] after=[%2] holder=[%3]"_q.arg(
					state->beforeReset,
					state->afterReset,
					FormatKeyReading(state->keyHolder.key)));
		},
	});

	runner->add({
		.name = u"lang pack self-test: arrange a default key and a "
			"pre-existing override"_q,
		.skipReason = gate,
		.run = [=] {
			const auto &instance = Lang::GetInstance();
			state->defaultA = instance.getValue(state->keyA.index);
			auto overrides = std::vector<LangOverride>();
			overrides.push_back({
				.key = state->keyB.key,
				.value = arrangedB,
			});
			state->preexisting = LangPackFixture::Install(
				runner,
				std::move(overrides),
				LangRestoreFault::None);

			// Parentless and never shown: both labels subscribe to the
			// product's own producer for their key, and
			// accessibilityName() returns the parsed text the label owns
			// before any layout, paint or grab, so this self-test asks
			// the process for no primary window and has no fixture gate
			// to report for one.
			state->labelA = base::make_unique_q<Ui::FlatLabel>(
				nullptr,
				Lang::details::Value(state->keyA.index),
				st::defaultFlatLabel);
			state->labelB = base::make_unique_q<Ui::FlatLabel>(
				nullptr,
				Lang::details::Value(state->keyB.index),
				st::defaultFlatLabel);
			state->readA = state->labelA->accessibilityName();
			state->readB = state->labelB->accessibilityName();
		},
		.then = [=] {
			CheckTextReads(
				state->readA,
				state->defaultA,
				u"lang pack self-test: key A reads its default value "
				"before the install"_q);
			CheckTextReads(
				state->readB,
				arrangedB,
				u"lang pack self-test: key B reads the arranged "
				"pre-existing override"_q);
		},
	});

	runner->add({
		.name = u"lang pack self-test: install the synthetic overrides"_q,
		.skipReason = gate,
		.run = [=] {
			auto overrides = std::vector<LangOverride>();
			overrides.push_back({
				.key = state->keyA.key,
				.value = installedA,
			});
			overrides.push_back({
				.key = state->keyB.key,
				.value = installedB,
			});
			state->subject = LangPackFixture::Install(
				runner,
				std::move(overrides),
				fault);
			state->readA = state->labelA->accessibilityName();
			state->readB = state->labelB->accessibilityName();
		},
		.then = [=] {
			state->subject->checkInstalled(
				u"lang pack self-test: both keys carry the synthetic "
				"values after the install"_q);
			CheckTextReads(
				state->readA,
				installedA,
				u"lang pack self-test: label A took the installed value "
				"with no interaction"_q);
			CheckTextReads(
				state->readB,
				installedB,
				u"lang pack self-test: label B took the installed value "
				"with no interaction"_q);
		},
	});

	runner->add({
		.name = u"lang pack self-test: remove the overrides"_q,
		.skipReason = gate,
		.run = [=] {
			state->subject->remove();
			state->readA = state->labelA->accessibilityName();
			state->readB = state->labelB->accessibilityName();
		},
		.then = [=] {
			if (fault == LangRestoreFault::LeaveOneInstalled) {
				Note(u"lang pack self-test: "
					"LangRestoreFault::LeaveOneInstalled - the "
					"checkRestored row and label A's read-back row below "
					"are this arm's deliberate falsification and are both "
					"expected to FAIL"_q);
			} else if (fault == LangRestoreFault::SuppressNotification) {
				Note(u"lang pack self-test: "
					"LangRestoreFault::SuppressNotification - both label "
					"read-back rows below are this arm's deliberate "
					"falsification and are expected to FAIL, while "
					"checkRestored is expected to PASS because the values "
					"themselves are restored"_q);
			}
			state->subject->checkRestored(
				u"lang pack self-test: both keys are back to the readings "
				"frozen before the install"_q);
			const auto frozenA = state->subject->frozen(state->keyA.key);
			const auto frozenB = state->subject->frozen(state->keyB.key);
			if (frozenA) {
				CheckTextReads(
					state->readA,
					frozenA->value,
					u"lang pack self-test: label A returned to its frozen "
					"text with no further interaction"_q);
			}
			if (frozenB) {
				CheckTextReads(
					state->readB,
					frozenB->value,
					u"lang pack self-test: label B returned to its frozen "
					"pre-existing override with no further interaction"_q);
			}
		},
	});

	runner->add({
		.name = u"lang pack self-test: install a placeholder-key override"_q,
		.skipReason = taggedGate,
		.run = [=] {
			auto overrides = std::vector<LangOverride>();
			overrides.push_back({
				.key = state->taggedKey,
				.value = taggedInstalled,
			});
			state->tagged = LangPackFixture::Install(
				runner,
				std::move(overrides),
				LangRestoreFault::None);
		},
		.then = [=] {
			state->tagged->checkInstalled(
				u"lang pack self-test: a placeholder key carries the "
				"installed override"_q);
		},
	});

	runner->add({
		.name = u"lang pack self-test: a placeholder the key does not "
			"accept"_q,
		.skipReason = taggedGate,
		.run = [=] {
			auto overrides = std::vector<LangOverride>();
			overrides.push_back({
				.key = state->taggedKey,
				.value = taggedRefused,
			});
			state->taggedBroken = LangPackFixture::Install(
				runner,
				std::move(overrides),
				LangRestoreFault::None);
		},
		.then = [=] {
			Note(u"lang pack self-test: the checkInstalled row below is "
				"a deliberate falsification - {amount} is a placeholder "
				"lng_dlg_search_from does not accept, so the row is "
				"expected to FAIL"_q);
			const auto before = FailureCount();
			state->taggedBroken->checkInstalled(
				u"lang pack self-test: a placeholder the key does not "
				"accept is not certified installed"_q);
			const auto after = FailureCount();
			Check(
				after == before + 1,
				u"lang pack self-test: the refused placeholder failed "
				"checkInstalled exactly once"_q,
				u"failures before=%1 after=%2"_q.arg(before).arg(after));
			state->taggedBroken->remove();
			state->tagged->remove();
		},
	});

	runner->add({
		.name = u"lang pack self-test: teardown"_q,
		.skipReason = gate,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips
			// every stage after it, so anything still held here would
			// outlive the run: both labels are parentless top levels this
			// State alone owns, and releasing the unique_qptrs is what
			// destroys them. That run is covered by two Runner::onFinish
			// backstops rather than by this stage - this self-test's own,
			// registered at append time, releases the LABELS and takes
			// this State's own fixtures down in reverse order,
			// and the module's single registration is the generic
			// backstop for whatever is still live after that.
			state->labelA = nullptr;
			state->labelB = nullptr;
			if (state->preexisting) {
				state->preexisting->remove();
			}
			// The holder is removed LAST for two reasons: it is the
			// bottom of the module's live stack, and remove() FAILs by
			// name on an out-of-order removal; and its snapshot is the
			// one carrying the live cloud pack the first stage's reset
			// cleared, so this call is what puts that pack back.
			if (state->holder) {
				state->holder->remove();
			}
			Note(u"lang pack self-test: teardown - labels released, live "
				"pack restored, %1, %2, %3"_q.arg(
					FormatKeyReading(state->keyA.key),
					FormatKeyReading(state->keyB.key),
					FormatKeyReading(state->keyHolder.key)));
		},
	});
}

} // namespace Test

#endif // _DEBUG
