/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The widget ops tap's shared internals, used by its five translation
// units: test_widget_ops.cpp (the record, the scan and the arming),
// test_widget_ops_hosted.cpp (the hosted tap, its registry and
// seams, the effective theme and the helper's work),
// test_widget_ops_check.cpp (the self-checks),
// test_widget_ops_deaths.cpp (the shared hosted death reading and the
// self-check's legs of hosted deaths and finals) and
// test_widget_ops_exchanges.cpp (the self-check's leg of the classifier the
// widget helper's stream times hosted renderers with). Internal to them;
// reference: docs/helpers/test_widget_ops.md.

namespace Test {

struct HelperWorkRecord;

} // namespace Test

namespace Test::WidgetOps {

inline constexpr auto kMaxExamples = 64;
inline constexpr auto kGapBytes = 160;
inline constexpr auto kDetailBytes = 240;

// The bytes the leak or note examples of one scanned message may have
// together, each example at most a place of kDetailBytes and 200 more.
inline constexpr auto kMaxSampleBytes = std::size_t(64) << 10;

// What the tap asks a hosted widget for its effective theme, and the bytes
// a kept answer may have.
inline constexpr auto kEffectiveProbe = "JSON.stringify("
	"[TelegramWidget.colorScheme, TelegramWidget.themeParams])";
inline constexpr auto kMaxEffectiveBytes = std::size_t(64) << 10;

// The JSON values (keys counted too) one observed message may hold: over
// three times the stress kit's largest validated batch (style-churn's 8001
// ops, about 72000 values). A larger message is a scan gap and is never
// parsed, so no message can make the tap build a tree past about 30 MiB.
inline constexpr auto kMaxScanValues = std::size_t(1) << 18;

// The bytes a kept result value, theme message or helper scheme may have;
// a longer one is a scan gap and is not kept.
inline constexpr auto kMaxKeptValueBytes = std::size_t(256) << 10;
inline constexpr auto kMaxSchemeBytes = std::size_t(64);

// WidgetHost's own wrapping of a validated batch for its renderer.
inline constexpr auto kOpsPrefix
	= std::string_view("{\"type\":\"ops\",\"ops\":");
inline constexpr auto kOpsSuffix = std::string_view("}");

#ifdef _DEBUG
inline constexpr auto kHelperMirrors = true;
#else // _DEBUG
// A Release build with the harness starts a Release helper: it has no tap
// and stops a route at a Mirror control.
inline constexpr auto kHelperMirrors = false;
#endif // _DEBUG

// What a hosted widget answered kEffectiveProbe with, read outside the
// tap's lock: its scheme, the JSON of its palette, the Release theme
// message they give and the first difference from the theme it was probed
// for; or, in `missed`, why the answer is no reading.
struct EffectiveReading {
	std::string scheme;
	std::string params;
	std::string message;
	std::string mismatch;
	std::string missed;
};

// Keyed by session too: local message ids are allocated per session.
using ArmedMap = std::map<GlobalMsgId, std::shared_ptr<WidgetOpsTap>>;

// Main thread.
[[nodiscard]] ArmedMap &ArmedTaps();

// Counts the values |json| would parse into, keys included, without
// allocating: each token that starts a value (a string, number, literal,
// array or object) right after the start, `[`, `{`, `,` or `:`. A token in
// no such place makes the parse fail before it allocates for it, so the
// count bounds the tree whatever the text.
[[nodiscard]] bool WithinScanBudget(std::string_view json);

// steady_clock's now, in microseconds: the clock of a helper's work
// records.
[[nodiscard]] crl::profile_time SteadyMicros();

// What a widget answered kEffectiveProbe with, for |theme|, the theme it
// was probed after: a [scheme, palette] pair within kMaxEffectiveBytes, or
// no reading (an error the probe gave, a throw, anything else).
[[nodiscard]] EffectiveReading ReadEffective(
	std::string_view answer,
	const twidget::WidgetTheme &theme);

// The self-check's hold of a hosted host's theme probes: what the host's
// tap asks the widget with (WidgetHost::probe(kEffectiveProbe)) becomes
// |probe| for that host alone, until an empty |probe| restores it. Main
// thread, while no probe of that host runs. False when |host| has no
// installed tap.
bool ReplaceEffectiveProbe(
	twidget::WidgetHost &host,
	Fn<std::string()> probe);

// The hosted deaths leg (test_widget_ops_deaths.cpp), on |host|, the
// installed stub host of |tap|'s newest selection, whose observers only
// the caller calls, |deaths| being what the runtime's own onDeath got,
// filled while the leg runs: two deaths reported (one terminated), both
// reaching that observer and neither counting; the host's first change
// that carries a death keeping it raw in the selection's record, a later
// one changing nothing, none of it a final. The rows' names lack the
// self-check's prefix.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> CheckHostedDeaths(
	WidgetOpsTap &tap,
	twidget::WidgetHost &host,
	const std::vector<std::string> &deaths);

// The hosted finals leg (test_widget_ops_deaths.cpp), the `hosted
// finals: ...` rows, on the message |finals|: five works played as
// WidgetHostedPlay plays them, two installed on stub hosts and three never
// installed. A host's raw death reaches the runtime's own observer and
// counts nothing; each final counts once in its own field (a stalled run,
// a refusal on a spared route, a refusal whose claim found no end, a spared
// and an unaffected run) and a second call of a copy of a work's
// observation counts nothing, which is the tap's own guard and no proof
// of the runtime's once-only accounting; a raw death after a final only
// fills its record; dropping every construction balances the factories
// and wrappers while the hosted reading stays unfinished (the shared
// barrier, WidgetTapFinished()) until the last final; each record keeps its
// final and its raw death apart, in selection order. Then the `hosted
// eviction` row, on the message |evicted|: a charged first selection
// evicted from `hostedRuns` by 33 later uncharged ones stays counted, and
// ReadHostedDeath() reads dead and unmeasured for its work (a reason
// measured while its record was kept staying with that work alone) and for
// a spared later sample printing the same raw line. Then the `hosted
// reading` row over literal readings: a stalled and a refused sample
// measured from their records over a healthy or collateral line, an
// ordinary one from its own raw line or record and unmeasured at
// "terminated", an unfinished or absent sample unmeasured. Disarms both.
// The rows' names lack the self-check's prefix.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> CheckHostedFinals(
	GlobalMsgId finals,
	GlobalMsgId evicted);

// The exchange leg (test_widget_ops_exchanges.cpp): TwidgetCore's own
// encodings read through Tgw::HostedAnswers(), the kinds and keys, the
// painted frames' admission against TwidgetCore's decode(), then the end
// of a widget helper's stream on two routes that the leg plays through
// the framework's shared scripted engine end on its in-memory pair
// (threads of that stream and of two hosted clients, each wait bounded;
// past a bound the row fails), and last the frame deltas' answers and
// admission against TwidgetCore's decode(), their rebuild on a platform
// end and their answers at a stream's end on two routes. The leg stops at
// a deadline its cases share, short of kWidgetOpsExchangesTimeout: past it
// no stream starts, each open stream's waits give up and it closes, and
// the row fails. The rows' names lack the self-check's prefix
// (SelfCheckRowName()).
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> CheckHostedExchanges();

// A leg's row |name| as RunWidgetOpsSelfCheck() names it, wherever the leg
// played (its exchange leg on a WidgetOpsExchanges too).
[[nodiscard]] inline QString SelfCheckRowName(const QString &name) {
	return u"widget ops self-check: "_q + name;
}

// |record| of the host's |generation| on Telegram's profile clock: its
// stamps moved by |profile| - |steady|, the two clocks read together at
// its receipt; nullopt when it finished after |steady| (the helper reads
// the same steady clock before it sends a record, so a record that ends
// after its own receipt is malformed, however short and ordered), or when
// that offset or a moved stamp does not fit.
[[nodiscard]] std::optional<WidgetHelperWorkEvent> HelperWorkEvent(
	const HelperWorkRecord &record,
	crl::profile_time profile,
	crl::profile_time steady,
	int generation);

} // namespace Test::WidgetOps

namespace Test {

class WidgetOpsTap final {
public:
	// Returns the selection's 1-based index, its host's generation.
	[[nodiscard]] int factorySelected();
	void factoryCompleted();
	void factoryReleased();
	void wrapperDestroyed();
	// A hosted host's observers installed: a build without a scheme.
	void hostedBuilt();
	// TapHostedWidget() selected the hosted host of |selection| for |work|:
	// its record from now on, the oldest dropped past kWidgetHostedRuns,
	// and with |awaited| one more work whose final the tap awaits.
	void hostedSelected(int selection, uint64 work, bool awaited);
	// The first change of the hosted host of |selection| that carried its
	// death: |reason|, raw, kept in that selection's record alone while it
	// is kept, never counted, also after its final.
	void hostedDied(int selection, const std::string &reason);
	// The runtime's final accounting of the run of |selection| and |work|,
	// called once: counted, then kept in that selection's record while it
	// is kept.
	void hostedFinal(
		int selection,
		uint64 work,
		const Tgw::HostedDecision &decision);
	// The scheme a hosted host's renderer was built with, as reported.
	void schemeReported(int selection, const std::string &scheme);

	// Parse and scan outside the lock, merge under it; never call out.
	void hostMessage(int selection, std::string_view json);
	// A served host's message, mirrored: its `frozen` is not a death here,
	// the runtime's final accounting of the hosted run is (hostedFinal()).
	void servedMessage(int selection, std::string_view json);
	void themeSet(int selection, const std::string &theme);
	// Its trusted events count only for the marked generation, once marked.
	void rendererMessage(int selection, std::string_view json);
	// A hosted host's own rendered acknowledgment, once per host.
	void hostRendered();
	// The theme the runtime wants a hosted host to have while its theme job
	// runs, by the message it gives: pending from now on, as that job's
	// application when the job gives it (none when that was recorded), else
	// as the application of a job still to start.
	void themeSelected(int selection, std::string message);
	// A theme application the runtime scheduled for a hosted host, with the
	// theme message it gives: pending from now on, its token returned (the
	// selected one when it was pending for that message; 0 for an older
	// host's or a retired one's).
	[[nodiscard]] uint64 themeScheduled(int selection, std::string message);
	// The theme the runtime applied to a hosted host, the one it was built
	// with (|token| 0) or a later one: its serial in the generation, 0 when
	// not kept; ends the pending application when |token| and |message|
	// are that application's.
	[[nodiscard]] int themeApplied(
		int selection,
		std::string scheme,
		std::string message,
		uint64 token = 0);
	// What the widget read after the theme of |serial| was applied: kept
	// only while that is the newest applied theme and its host was not
	// retired.
	void themeEffective(
		int selection,
		int serial,
		WidgetOps::EffectiveReading seen);
	// The runtime retired a hosted host of |selection| with |work|: nothing
	// it publishes later is kept, and a mark bound to that work whose click
	// was not decided yet is revoked (`markDetached`), also for a step a
	// lookup that held the registration before this call reports later.
	void hostRetired(int selection, uint64 work);
	// A primary press the chat dispatched to |work| of a host of
	// |selection|, with the pointer |serial|: the first inside the mark's
	// press delivery, to the marked generation, is the played one.
	void pressDispatched(int selection, uint64 work, uint64 serial);
	// The mark's press delivery begins (|open|) or returns: once it returned
	// the mark is sealed, bound or not, until the next mark.
	void pressDelivery(bool open);
	// A step of a hosted host's primary |press| on |work|: counted only for
	// the marked interaction (the press the mark's dispatch played, sent
	// with its serial) while the marked generation is the newest.
	void pressObserved(
		int selection,
		uint64 work,
		uint64 press,
		const Tgw::PressStep &step);
	// The core of |work| of |selection| is being detached, its host link
	// about to close: a mark bound to that work whose click was not
	// completed yet is revoked (`markDetached`), also for a step a lookup
	// that held the registration before this call reports later.
	void detached(int selection, uint64 work);
	// An observation not taken: the reason and the start of the message.
	void gap(std::string example);

	void markEvents();
	[[nodiscard]] WidgetOpsReading read() const;
	[[nodiscard]] WidgetOpsLatest latest() const;

	void askWork();
	[[nodiscard]] bool asksWork() const;
	void reserveWork(int capacity);
	void helperWork(WidgetHelperWorkEvent event);
	void helperWorkMalformed();
	[[nodiscard]] WidgetHelperWorkReading work(crl::profile_time since) const;

private:
	void merge(int selection, std::string_view json);
	// Under the lock: whether |selection| is the newest host's.
	[[nodiscard]] bool newest(int selection) const;
	// Under the lock: false, counted as stale, for an older host.
	[[nodiscard]] bool current(int selection);
	// Under the lock: the record of the hosted selection |selection|, if
	// still kept.
	[[nodiscard]] WidgetHostedRun *hostedRun(int selection);
	// Under the lock: whether |work| of |selection| is the work the mark's
	// press was played to, while the marked generation is the newest and
	// that work's core was not detached (its host not retired) before its
	// click.
	[[nodiscard]] bool marks(int selection, uint64 work) const;

	// Where the mark's press binding is: awaiting its delivery, open while
	// DeliverMarkedPress() delivers it, sealed once that returned.
	enum class MarkPress : uchar {
		Awaiting,
		Open,
		Sealed,
	};

	mutable std::mutex _mutex;
	WidgetOpsReading _reading;
	WidgetOpsLatest _latest;
	WidgetHelperWorkReading _work;
	// The theme message of the pending application, `themePending`'s.
	std::string _pendingMessage;
	// The newest application the runtime started on the newest host, the
	// constructor's (token 0) or a job's: the theme message it gives, its
	// token and whether it was recorded.
	std::string _startedMessage;
	uint64 _startedToken = 0;
	uint64 _themeTokens = 0;
	bool _startedRecorded = false;
	MarkPress _markPress = MarkPress::Sealed;

};

} // namespace Test
