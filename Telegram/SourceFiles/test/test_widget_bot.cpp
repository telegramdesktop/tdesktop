/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_bot.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_agent.h"

#include <QtCore/QStringList>

#include <utility>

namespace Test {

class WidgetBotRecord final {
public:
	WidgetBotRecord(WidgetBotTable table, crl::time armedAt);

	[[nodiscard]] WidgetBotRequest play(const std::string &data, bool wallet);
	[[nodiscard]] const std::vector<WidgetBotRequest> &requests() const;

private:
	const WidgetBotTable _table;
	const crl::time _armedAt = 0;
	std::map<std::string, int> _counts;
	std::vector<WidgetBotRequest> _requests;

};

namespace {

// twidget's tools/conformance runner's texts: the templates may come out as
// any text, and these keep the account's name out of the evidence.
constexpr auto kFrom = "Conformance";
constexpr auto kTime = "12:00";

// Keyed by session too: local message ids are allocated per session.
using ArmedMap = std::map<GlobalMsgId, std::shared_ptr<WidgetBotRecord>>;

[[nodiscard]] ArmedMap &ArmedBots() {
	static auto result = ArmedMap();
	return result;
}

void ReplaceAll(
		std::string &text,
		const std::string &what,
		const std::string &with) {
	auto i = text.find(what);
	while (i != std::string::npos) {
		text.replace(i, what.size(), with);
		i = text.find(what, i + with.size());
	}
}

[[nodiscard]] std::string Fill(std::string text, int n) {
	ReplaceAll(text, "{n}", std::to_string(n));
	ReplaceAll(text, "{from}", kFrom);
	ReplaceAll(text, "{time}", kTime);
	return text;
}

} // namespace

WidgetBotRecord::WidgetBotRecord(WidgetBotTable table, crl::time armedAt)
: _table(std::move(table))
, _armedAt(armedAt) {
}

WidgetBotRequest WidgetBotRecord::play(const std::string &data, bool wallet) {
	const auto n = ++_counts[data];
	const auto i = _table.find(data);
	const auto listed = (i != end(_table));
	_requests.push_back({
		.data = data,
		.result = (listed && i->second)
			? std::make_optional(Fill(*i->second, n))
			: std::nullopt,
		.ms = crl::now() - _armedAt,
		.n = n,
		.listed = listed,
		.wallet = wallet,
	});
	return _requests.back();
}

const std::vector<WidgetBotRequest> &WidgetBotRecord::requests() const {
	return _requests;
}

namespace {

// Peer 0 belongs to no history, so no real message of any session has such
// an id. The session values only tell the self-check's own ids apart.
constexpr auto kSelfCheckSession = uint64(1);
constexpr auto kOtherSession = uint64(2);

[[nodiscard]] GlobalMsgId SelfCheckId(
		int index,
		uint64 session = kSelfCheckSession) {
	return { FullMsgId(PeerId(), MsgId(-index)), session };
}

[[nodiscard]] QString Flag(bool value) {
	return value ? u"1"_q : u"0"_q;
}

void AddRow(
		std::vector<WidgetOpsSelfCheckRow> &rows,
		bool passed,
		const QString &name,
		const QString &details) {
	rows.push_back({
		.name = u"widget bot self-check: "_q + name,
		.details = details,
		.passed = passed,
	});
}

// What the self-check's responder was given: every outcome, in order.
struct Responses {
	std::vector<twidget::WidgetRequestOutcome> outcomes;
};

// One PlayWidgetBot() call, and whether it returned true and how many
// responses arrived before it returned.
struct Played {
	int responses = 0;
	bool returned = false;
};

[[nodiscard]] Played Play(
		GlobalMsgId id,
		const std::string &data,
		const std::shared_ptr<Responses> &responses,
		bool wallet = false) {
	const auto before = int(responses->outcomes.size());
	const auto respond = [=](const twidget::WidgetRequestOutcome &outcome) {
		responses->outcomes.push_back(outcome);
	};
	const auto returned = PlayWidgetBot(id, data, respond, wallet);
	return {
		.responses = int(responses->outcomes.size()) - before,
		.returned = returned,
	};
}

[[nodiscard]] QString OptionalText(const std::optional<std::string> &value) {
	return value
		? (u"["_q + QString::fromStdString(*value) + u"]"_q)
		: u"none"_q;
}

[[nodiscard]] QString OutcomeText(
		const twidget::WidgetRequestOutcome &outcome) {
	return u"{failure=%1 result=%2}"_q.arg(
		QString::number(int(outcome.failure)),
		OptionalText(outcome.answer.result));
}

[[nodiscard]] QString RequestsText(
		const std::vector<WidgetBotRequest> &list) {
	auto result = QStringList();
	for (const auto &request : list) {
		result.push_back(
			u"{data=[%1] listed=%2 wallet=%3 n=%4 result=%5}"_q.arg(
				QString::fromStdString(request.data),
				Flag(request.listed),
				Flag(request.wallet),
				QString::number(request.n),
				OptionalText(request.result)));
	}
	return u"["_q + result.join(u" "_q) + u"]"_q;
}

[[nodiscard]] bool Answered(
		const twidget::WidgetRequestOutcome &outcome,
		const std::optional<std::string> &result) {
	return (outcome.failure == twidget::WidgetRequestFailure::None)
		&& (outcome.answer.result == result);
}

[[nodiscard]] bool Recorded(
		const WidgetBotRequest &request,
		const std::string &data,
		bool listed,
		int n,
		const std::optional<std::string> &result,
		bool wallet = false) {
	return (request.data == data)
		&& (request.listed == listed)
		&& (request.wallet == wallet)
		&& (request.n == n)
		&& (request.result == result)
		&& (request.ms >= 0);
}

// An unarmed id, and another session's message with the armed FullMsgId,
// are left to the product: false, no response, nothing recorded.
void CheckUnarmed(std::vector<WidgetOpsSelfCheckRow> &rows) {
	const auto armed = SelfCheckId(1, kSelfCheckSession);
	const auto other = SelfCheckId(1, kOtherSession);
	const auto responses = std::make_shared<Responses>();
	const auto unarmed = Play(SelfCheckId(2), "conf:x", responses);
	const auto record = ArmWidgetBot(armed, { { "conf:x", "ok" } });
	const auto foreign = Play(other, "conf:x", responses);
	const auto requests = ReadWidgetBot(*record);
	DisarmWidgetBot(armed);
	AddRow(
		rows,
		!unarmed.returned
			&& !foreign.returned
			&& responses->outcomes.empty()
			&& requests.empty(),
		u"an unarmed id and another session's message with the armed "
			u"FullMsgId are left to the product, untouched"_q,
		u"unarmed=%1 otherSession=%2 responses=%3 recorded=%4"_q.arg(
			Flag(unarmed.returned),
			Flag(foreign.returned),
			QString::number(int(responses->outcomes.size())),
			QString::number(int(requests.size()))));
}

// A listed entry's result, its templates filled, answered once per request
// before PlayWidgetBot() returns, with {n} counting per exact data; then a
// listed entry without result and unlisted data, both answered without
// result; the records in order, readable after the disarm, which plays no
// more; and a fresh record for the next arming.
void CheckPlayed(std::vector<WidgetOpsSelfCheckRow> &rows) {
	const auto id = SelfCheckId(3);
	const auto responses = std::make_shared<Responses>();
	const auto record = ArmWidgetBot(id, {
		{ "conf:t", "n={n} from={from} at={time} n={n}" },
		{ "conf:other", "other {n}" },
		{ "conf:none", std::nullopt },
	});
	const auto first = Play(id, "conf:t", responses);
	const auto other = Play(id, "conf:other", responses);
	const auto second = Play(id, "conf:t", responses);
	const auto filled = [](int n) {
		const auto count = std::to_string(n);
		return std::make_optional(
			"n=" + count + " from=Conformance at=12:00 n=" + count);
	};
	const auto &outcomes = responses->outcomes;
	const auto listed = first.returned
		&& other.returned
		&& second.returned
		&& (first.responses == 1)
		&& (other.responses == 1)
		&& (second.responses == 1)
		&& (outcomes.size() == 3)
		&& Answered(outcomes[0], filled(1))
		&& Answered(outcomes[1], std::string("other 1"))
		&& Answered(outcomes[2], filled(2));
	auto outcomesText = QStringList();
	for (const auto &outcome : outcomes) {
		outcomesText.push_back(OutcomeText(outcome));
	}
	AddRow(
		rows,
		listed,
		u"a listed request is answered once, synchronously, with its "
			u"result's templates filled and {n} counting that exact data"_q,
		u"returned=%1%2%3 responses=%4,%5,%6 outcomes=[%7]"_q.arg(
			Flag(first.returned),
			Flag(other.returned),
			Flag(second.returned),
			QString::number(first.responses),
			QString::number(other.responses),
			QString::number(second.responses),
			outcomesText.join(u" "_q)));

	const auto none = Play(id, "conf:none", responses);
	const auto unlisted = Play(id, "conf:unlisted", responses);
	const auto fallback = none.returned
		&& unlisted.returned
		&& (none.responses == 1)
		&& (unlisted.responses == 1)
		&& (outcomes.size() == 5)
		&& Answered(outcomes[3], std::nullopt)
		&& Answered(outcomes[4], std::nullopt);
	AddRow(
		rows,
		fallback,
		u"a listed entry without result and data not in the table are "
			u"answered without result, as the reference bot answers them"_q,
		u"returned=%1%2 responses=%3,%4 outcomes=[%5 %6]"_q.arg(
			Flag(none.returned),
			Flag(unlisted.returned),
			QString::number(none.responses),
			QString::number(unlisted.responses),
			(outcomes.size() > 3) ? OutcomeText(outcomes[3]) : QString(),
			(outcomes.size() > 4) ? OutcomeText(outcomes[4]) : QString()));

	DisarmWidgetBot(id);
	const auto later = Play(id, "conf:t", responses);
	const auto requests = ReadWidgetBot(*record);
	const auto fresh = ArmWidgetBot(id, {});
	const auto freshRequests = ReadWidgetBot(*fresh);
	DisarmWidgetBot(id);
	const auto ordered = (requests.size() == 5)
		&& Recorded(requests[0], "conf:t", true, 1, filled(1))
		&& Recorded(requests[1], "conf:other", true, 1, "other 1")
		&& Recorded(requests[2], "conf:t", true, 2, filled(2))
		&& Recorded(requests[3], "conf:none", true, 1, std::nullopt)
		&& Recorded(requests[4], "conf:unlisted", false, 1, std::nullopt)
		&& (requests[0].ms <= requests[4].ms);
	AddRow(
		rows,
		ordered
			&& !later.returned
			&& (later.responses == 0)
			&& (outcomes.size() == 5)
			&& (fresh != record)
			&& freshRequests.empty(),
		u"the record keeps every request in order and stays readable "
			u"after the disarm, which plays no more; arming again gives a "
			u"fresh record"_q,
		u"afterDisarm=%1 responses=%2 fresh=%3 freshRequests=%4 "
			u"requests=%5"_q.arg(
				Flag(later.returned),
				QString::number(later.responses),
				Flag(fresh != record),
				QString::number(int(freshRequests.size())),
				RequestsText(requests)));
}

// A request that asked for the wallet is answered from the table as any
// other and recorded with |wallet| set; the same data asked right after
// without it is recorded unset, so the flag is the request's, not the
// data's or the record's.
void CheckWallet(std::vector<WidgetOpsSelfCheckRow> &rows) {
	const auto id = SelfCheckId(4);
	const auto responses = std::make_shared<Responses>();
	const auto record = ArmWidgetBot(id, { { "conf:pay", "tx {n}" } });
	const auto asked = Play(id, "conf:pay", responses, true);
	const auto plain = Play(id, "conf:pay", responses);
	const auto requests = ReadWidgetBot(*record);
	DisarmWidgetBot(id);
	const auto &outcomes = responses->outcomes;
	AddRow(
		rows,
		asked.returned
			&& plain.returned
			&& (asked.responses == 1)
			&& (plain.responses == 1)
			&& (outcomes.size() == 2)
			&& Answered(outcomes[0], std::string("tx 1"))
			&& Answered(outcomes[1], std::string("tx 2"))
			&& (requests.size() == 2)
			&& Recorded(requests[0], "conf:pay", true, 1, "tx 1", true)
			&& Recorded(requests[1], "conf:pay", true, 2, "tx 2", false),
		u"a request that asked for the wallet is answered from the table "
			u"and recorded with wallet set, the same data without it unset"_q,
		u"returned=%1%2 responses=%3,%4 requests=%5"_q.arg(
			Flag(asked.returned),
			Flag(plain.returned),
			QString::number(asked.responses),
			QString::number(plain.responses),
			RequestsText(requests)));
}

} // namespace

bool PlayWidgetBot(
		GlobalMsgId id,
		const std::string &data,
		const Fn<void(const twidget::WidgetRequestOutcome&)> &respond,
		bool wallet) {
	if (!Active()) {
		return false;
	}
	const auto &armed = ArmedBots();
	const auto i = armed.find(id);
	if (i == end(armed)) {
		return false;
	}
	const auto played = i->second->play(data, wallet);
	respond({ .answer = { .result = played.result } });
	return true;
}

std::shared_ptr<WidgetBotRecord> ArmWidgetBot(
		GlobalMsgId id,
		WidgetBotTable table) {
	auto record = std::make_shared<WidgetBotRecord>(
		std::move(table),
		crl::now());
	ArmedBots()[id] = record;
	return record;
}

void DisarmWidgetBot(GlobalMsgId id) {
	ArmedBots().erase(id);
}

std::vector<WidgetBotRequest> ReadWidgetBot(const WidgetBotRecord &record) {
	return record.requests();
}

std::vector<WidgetOpsSelfCheckRow> RunWidgetBotSelfCheck() {
	auto rows = std::vector<WidgetOpsSelfCheckRow>();
	const auto active = Active();
	AddRow(
		rows,
		active,
		u"the harness is active, so the seam can play"_q,
		u"active=%1"_q.arg(Flag(active)));
	if (!active) {
		return rows;
	}
	CheckUnarmed(rows);
	CheckPlayed(rows);
	CheckWallet(rows);
	return rows;
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

bool PlayWidgetBot(
		GlobalMsgId,
		const std::string &,
		const Fn<void(const twidget::WidgetRequestOutcome&)> &,
		bool) {
	return false;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
