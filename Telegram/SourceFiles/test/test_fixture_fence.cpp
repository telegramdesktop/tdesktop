/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_fixture_fence.h"

#ifdef _DEBUG

#include "mtproto/details/mtproto_dump_to_text.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "scheme-dump_to_text.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QRegularExpression>
#include <QtCore/QThread>

#include <limits>

namespace Test {
namespace {

using MTP::details::SerializedRequest;

// The self-test's synthetic request ids. Nothing is ever handed to an
// MTP::Instance, so they only have to be distinct inside the self-test.
constexpr auto kFirstSelfTestId = mtpRequestId(0x7E5F0001);

// The self-test's registered values: a message id above every id a real
// chat serves, as consumers choose theirs, and a bare peer id in the
// reserved 0xFD << 32 range.
constexpr auto kSelfTestMessage = int32(2070000004);
constexpr auto kSelfTestPeer = (uint64(0xFD) << 32) | uint64(1);

// Unrelated values: no word and no adjacent low/high word pair of theirs
// equals kSelfTestMessage or kSelfTestPeer.
constexpr auto kSelfTestChannel = uint64(1234567890123ULL);
constexpr auto kSelfTestAccessHash = uint64(0x0123456789ABCDEFULL);
constexpr auto kSelfTestSearchHash = uint64(0x0FEDCBA987654321ULL);

const auto kPollMethod = u"messages_getMessagesReactions"_q;
const auto kChannelsMethod = u"channels_getChannels"_q;

// The request body, read in place: the length word holds its size in
// bytes, and the buffer may carry padding past it.
struct BodyWords {
	const mtpPrime *from = nullptr;
	const mtpPrime *till = nullptr;
};

// One self-test request handed to a private fence, and what it left.
struct Offered {
	mtpRequestId id = 0;
	bool refused = false;
	int refusals = 0;
	std::optional<FixtureFenceRefusal> first;
	std::vector<QString> rows;
};

struct SelfTestState {
	FixtureFence empty{ u"fixture-fence-selftest-empty"_q };
	FixtureFence registered{ u"fixture-fence-selftest-registered"_q };
	FixtureFence clean{ u"fixture-fence-selftest-clean"_q };
	FixtureFence refused{ u"fixture-fence-selftest-refused"_q };
	FixtureFence uninspected{ u"fixture-fence-selftest-uninspected"_q };
	mtpRequestId nextId = kFirstSelfTestId;
};

[[nodiscard]] QString Bit(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] BodyWords Body(const SerializedRequest &request) {
	constexpr auto kBody = int(SerializedRequest::kMessageBodyPosition);
	constexpr auto kLength = SerializedRequest::kMessageLengthPosition;
	const auto size = request ? int(request->size()) : 0;
	if (size <= kBody) {
		return {};
	}
	const auto data = request->constData();
	const auto words = int(uint32(data[kLength]) / sizeof(mtpPrime));
	const auto from = data + kBody;
	return { from, from + std::min(words, size - kBody) };
}

// The prefilter: a word equal to a registered message id or an adjacent
// low/high word pair equal to a registered bare peer id. Only a hit pays
// for the dump; the dump alone decides.
[[nodiscard]] bool MayName(
		const BodyWords &body,
		const base::flat_set<int32> &messages,
		const base::flat_set<uint64> &peers) {
	for (auto i = body.from; i != body.till; ++i) {
		if (messages.contains(*i)) {
			return true;
		} else if (peers.empty() || (i + 1 == body.till)) {
			continue;
		}
		const auto pair = uint64(uint32(*i))
			| (uint64(uint32(*(i + 1))) << 32);
		if (peers.contains(pair)) {
			return true;
		}
	}
	return false;
}

// The generated dumper's rendering, the text the MTP log prints for every
// Send. A request reaching sendPrepared is a complete generated
// serialization, so a body it cannot decode completely is a harness or
// schema defect, never a request to let through.
[[nodiscard]] QString DumpBody(const BodyWords &body) {
	auto buffer = MTP::details::DumpToTextBuffer();
	auto from = body.from;
	if (!MTP::details::DumpToTextType(buffer, from, body.till)
		|| (from != body.till)) {
		Unexpected("Undecodable request body in Test::FixtureFence.");
	}
	return QString::fromUtf8(buffer.p, buffer.size);
}

// The outer constructor name: every dump of a request opens "{ <name>".
[[nodiscard]] QString MethodName(const QString &dump) {
	static const auto expression = QRegularExpression(
		u"^\\{ ([A-Za-z0-9_]+)"_q);
	const auto match = expression.match(dump);
	if (!match.hasMatch()) {
		Unexpected("Request dump without a name in Test::FixtureFence.");
	}
	return match.captured(1);
}

// Registered values among the dump's typed scalars: a message id only as
// "<n> [INT]", a bare peer id only as "<n> [LONG]", never inside a quoted
// string, whose backslashes and quotes the dumper escapes. Hex BYTES,
// vector counts, constructor names and the other scalar tags never match.
[[nodiscard]] std::vector<FixtureIdMatch> TypedMatches(
		const QString &dump,
		const base::flat_set<int32> &messages,
		const base::flat_set<uint64> &peers) {
	static const auto quoted = QRegularExpression(
		u"\"(?:[^\"\\\\]++|\\\\.)*+\""_q,
		QRegularExpression::DotMatchesEverythingOption);
	static const auto scalar = QRegularExpression(
		u"(?<!\\S)(-?\\d+) \\[(INT|LONG)\\]"_q);
	const auto unquoted = QString(dump).replace(quoted, QString());
	auto result = std::vector<FixtureIdMatch>();
	auto i = scalar.globalMatch(unquoted);
	while (i.hasNext()) {
		const auto match = i.next();
		const auto digits = match.capturedView(1);
		auto ok = false;
		auto found = std::optional<FixtureIdMatch>();
		if (match.captured(2) == u"INT"_q) {
			const auto value = digits.toInt(&ok);
			if (ok && messages.contains(value)) {
				found = FixtureIdMatch{
					.kind = FixtureIdKind::Message,
					.value = uint64(value),
				};
			}
		} else {
			const auto value = digits.toULongLong(&ok);
			if (ok && peers.contains(uint64(value))) {
				found = FixtureIdMatch{
					.kind = FixtureIdKind::Peer,
					.value = uint64(value),
				};
			}
		}
		if (found && !ranges::contains(result, *found)) {
			result.push_back(*found);
		}
	}
	return result;
}

[[nodiscard]] QString KindName(FixtureIdKind kind) {
	switch (kind) {
	case FixtureIdKind::Message:
		return u"message"_q;
	case FixtureIdKind::Peer:
		return u"peer"_q;
	}
	Unexpected("Kind in Test::KindName.");
}

[[nodiscard]] QString StateName(FixtureFenceState state) {
	switch (state) {
	case FixtureFenceState::Clean:
		return u"clean"_q;
	case FixtureFenceState::Refusals:
		return u"refusals"_q;
	case FixtureFenceState::NothingInspected:
		return u"nothing-inspected"_q;
	}
	Unexpected("State in Test::StateName.");
}

[[nodiscard]] QString MatchesText(
		const std::vector<FixtureIdMatch> &matches) {
	auto list = QStringList();
	for (const auto &match : matches) {
		list.push_back(KindName(match.kind)
			+ QChar(':')
			+ QString::number(match.value));
	}
	return list.isEmpty() ? u"none"_q : list.join(QChar(','));
}

[[nodiscard]] QString RefusalRow(const FixtureFenceRefusal &refusal) {
	return u"refused method=%1 request=%2 ids=%3"_q.arg(
		refusal.method,
		QString::number(refusal.requestId),
		MatchesText(refusal.matches));
}

[[nodiscard]] QString RowsText(const std::vector<QString> &rows) {
	auto list = QStringList();
	for (const auto &row : rows) {
		list.push_back(row);
	}
	return list.isEmpty() ? u"none"_q : list.join(u"; "_q);
}

[[nodiscard]] SerializedRequest Identified(
		SerializedRequest request,
		mtpRequestId id) {
	request->requestId = id;
	return request;
}

[[nodiscard]] MTPInputPeer SelfTestChannelPeer(uint64 channelId) {
	return MTP_inputPeerChannel(
		MTP_long(channelId),
		MTP_long(kSelfTestAccessHash));
}

[[nodiscard]] SerializedRequest Poll(
		mtpRequestId id,
		const std::vector<int32> &ids) {
	auto list = QVector<MTPint>();
	list.reserve(int(ids.size()));
	for (const auto value : ids) {
		list.push_back(MTP_int(value));
	}
	return Identified(
		SerializedRequest::Serialize(MTPmessages_GetMessagesReactions(
			SelfTestChannelPeer(kSelfTestChannel),
			MTP_vector<MTPint>(std::move(list)))),
		id);
}

[[nodiscard]] SerializedRequest GetChannels(
		mtpRequestId id,
		uint64 channelId) {
	return Identified(
		SerializedRequest::Serialize(MTPchannels_GetChannels(
			MTP_vector<MTPInputChannel>(
				1,
				MTP_inputChannel(
					MTP_long(channelId),
					MTP_long(kSelfTestAccessHash))))),
		id);
}

// The registered message id as a channel LONG and, as text, inside the
// query; every other int and the flags are zero.
[[nodiscard]] SerializedRequest Search(mtpRequestId id) {
	return Identified(
		SerializedRequest::Serialize(MTPmessages_Search(
			MTP_flags(0),
			SelfTestChannelPeer(uint64(kSelfTestMessage)),
			MTP_string(u"%1 [INT]"_q.arg(kSelfTestMessage)),
			MTP_inputPeerEmpty(), // from_id
			MTP_inputPeerEmpty(), // saved_peer_id
			MTP_vector<MTPReaction>(), // saved_reaction
			MTP_int(0), // top_msg_id
			MTP_inputMessagesFilterEmpty(),
			MTP_int(0), // min_date
			MTP_int(0), // max_date
			MTP_int(0), // offset_id
			MTP_int(0), // add_offset
			MTP_int(0), // limit
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(kSelfTestSearchHash))),
		id);
}

// The prefilter's premise, read from a copy of the body rather than
// through the fence: a word equal to |value|.
[[nodiscard]] bool RawWordHit(
		const SerializedRequest &request,
		int32 value) {
	auto body = mtpBuffer();
	request.write(body);
	return body.contains(value);
}

// The same for an adjacent low/high word pair equal to |value|.
[[nodiscard]] bool RawPairHit(
		const SerializedRequest &request,
		uint64 value) {
	auto body = mtpBuffer();
	request.write(body);
	for (auto i = 1; i < int(body.size()); ++i) {
		const auto pair = uint64(uint32(body[i - 1]))
			| (uint64(uint32(body[i])) << 32);
		if (pair == value) {
			return true;
		}
	}
	return false;
}

// Hands |request| to |fence| the way the overlay hook does, and reads the
// refusals and rows it added.
[[nodiscard]] Offered Offer(
		FixtureFence &fence,
		const SerializedRequest &request) {
	auto result = Offered{ .id = request->requestId };
	const auto refusalMark = fence.mark();
	const auto rowsMark = fence.probe().mark();
	result.refused = fence.inspect(nullptr, request);
	result.refusals = fence.refusalCount(refusalMark);
	if (result.refusals > 0) {
		result.first = fence.refusal(refusalMark);
	}
	result.rows = fence.probe().rowsSince(rowsMark);
	return result;
}

[[nodiscard]] QString OfferedText(const Offered &offered) {
	return u"request=%1 decision=%2 refusals=%3 method=%4 matches=[%5] "
		"instance=%6 rows=[%7]"_q.arg(
			QString::number(offered.id),
			Bit(offered.refused),
			QString::number(offered.refusals),
			offered.first ? offered.first->method : u"none"_q,
			offered.first ? MatchesText(offered.first->matches) : u"none"_q,
			Bit(offered.first && !offered.first->instance.isNull()),
			RowsText(offered.rows));
}

// Exactly one refusal of the offered request: |method|, |expected| as its
// only match, no instance, and exactly the row the module page documents.
[[nodiscard]] bool RefusedOnly(
		const Offered &offered,
		const QString &method,
		FixtureIdMatch expected,
		const QString &ids) {
	const auto row = u"refused method=%1 request=%2 ids=%3"_q.arg(
		method,
		QString::number(offered.id),
		ids);
	return offered.refused
		&& (offered.refusals == 1)
		&& offered.first
		&& (offered.first->requestId == offered.id)
		&& (offered.first->method == method)
		&& (offered.first->matches == std::vector<FixtureIdMatch>{ expected })
		&& offered.first->instance.isNull()
		&& (offered.rows == std::vector<QString>{ row });
}

[[nodiscard]] bool Passed(const Offered &offered) {
	return !offered.refused && !offered.refusals && offered.rows.empty();
}

void RegisterSelfTestMessage(FixtureFence &fence) {
	fence.registerMessage(FullMsgId(
		peerFromChannel(ChannelId(kSelfTestChannel)),
		MsgId(kSelfTestMessage)));
}

void RegisterSelfTestPeer(FixtureFence &fence) {
	fence.registerPeer(peerFromChannel(ChannelId(kSelfTestPeer)));
}

} // namespace

FixtureFence::FixtureFence(QString probeName)
: _probe(std::move(probeName)) {
}

void FixtureFence::registerMessage(FullMsgId id) {
	Expects(id.msg.bare > 0);
	Expects(id.msg.bare <= std::numeric_limits<int32>::max());

	const auto value = int32(id.msg.bare);
	if (_messages.insert(value).second) {
		_probe.record(u"registered message:%1 peer=%2"_q.arg(
			QString::number(value),
			QString::number(id.peer.value)));
	}
}

void FixtureFence::registerPeer(PeerId peer) {
	Expects((peer.value & PeerId::kChatTypeMask) != 0);

	const auto bare = uint64(peer.value & PeerId::kChatTypeMask);
	if (_peers.insert(bare).second) {
		_probe.record(u"registered peer:%1 peer=%2"_q.arg(
			QString::number(bare),
			QString::number(peer.value)));
	}
}

bool FixtureFence::inspect(
		MTP::Instance *instance,
		const MTP::details::SerializedRequest &request) {
	++_inspected;
	if (_messages.empty() && _peers.empty()) {
		return false;
	}
	const auto body = Body(request);
	if ((body.from == body.till) || !MayName(body, _messages, _peers)) {
		return false;
	}
	const auto dump = DumpBody(body);
	auto matches = TypedMatches(dump, _messages, _peers);
	if (matches.empty()) {
		return false;
	}
	_refusals.push_back({
		.instance = instance,
		.requestId = request->requestId,
		.method = MethodName(dump),
		.matches = std::move(matches),
	});
	_probe.record(RefusalRow(_refusals.back()));
	return true;
}

int FixtureFence::inspectedCount() const {
	return _inspected;
}

int FixtureFence::mark() const {
	return int(_refusals.size());
}

int FixtureFence::refusalCount(int from) const {
	Expects(from >= 0 && from <= int(_refusals.size()));

	return int(_refusals.size()) - from;
}

const FixtureFenceRefusal &FixtureFence::refusal(int index) const {
	Expects(index >= 0 && index < int(_refusals.size()));

	return _refusals[index];
}

FixtureFenceVerdict FixtureFence::verdict() const {
	auto result = FixtureFenceVerdict{
		.state = !_inspected
			? FixtureFenceState::NothingInspected
			: !_refusals.empty()
			? FixtureFenceState::Refusals
			: FixtureFenceState::Clean,
		.inspected = _inspected,
		.registered = int(_messages.size() + _peers.size()),
		.refusals = int(_refusals.size()),
	};
	auto rows = std::vector<QString>();
	rows.reserve(_refusals.size());
	for (const auto &refusal : _refusals) {
		rows.push_back(RefusalRow(refusal));
	}
	const auto format = HelperNumberFormat();
	result.observation = u"state=%1 inspected=%2 registered=%3 "
		"refusals=%4 rows=[%5]"_q.arg(
			StateName(result.state),
			HelperNumber(result.inspected, format),
			HelperNumber(result.registered, format),
			HelperNumber(result.refusals, format),
			RowsText(rows));
	return result;
}

Probe &FixtureFence::probe() {
	return _probe;
}

FixtureFence &ProcessFixtureFence() {
	static auto &result = *new FixtureFence();
	return result;
}

bool FenceFixtureRequest(
		not_null<MTP::Instance*> instance,
		const MTP::details::SerializedRequest &request) {
	Expects(QThread::currentThread() == QCoreApplication::instance()->thread());

	return Active() && ProcessFixtureFence().inspect(instance.get(), request);
}

void AppendFixtureFenceCheck(not_null<Runner*> runner, const QString &what) {
	const auto claim = what
		+ u": the fixture fence inspected this run's requests and refused "
		"none"_q;
	runner->add({
		.name = claim,
		.then = [=] {
			const auto verdict = ProcessFixtureFence().verdict();
			Check(verdict.clean(), claim, verdict.observation);
		},
	});
}

void AppendFixtureFenceSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfTestState>();
	const auto messageIds = u"message:%1"_q.arg(kSelfTestMessage);
	const auto peerIds = u"peer:%1"_q.arg(kSelfTestPeer);
	const auto message = FixtureIdMatch{
		.kind = FixtureIdKind::Message,
		.value = uint64(kSelfTestMessage),
	};
	const auto peer = FixtureIdMatch{
		.kind = FixtureIdKind::Peer,
		.value = kSelfTestPeer,
	};

	runner->add({
		.name = u"fixture fence self-test: S1 an empty fence"_q,
		.then = [=] {
			auto &fence = state->empty;
			const auto rowsMark = fence.probe().mark();
			const auto poll = Offer(
				fence,
				Poll(state->nextId++, { 15, kSelfTestMessage, 16 }));
			const auto channels = Offer(
				fence,
				GetChannels(state->nextId++, kSelfTestPeer));
			const auto rows = fence.probe().rowsSince(rowsMark);
			Check(
				Passed(poll)
					&& Passed(channels)
					&& (fence.inspectedCount() == 2)
					&& !fence.refusalCount()
					&& rows.empty(),
				u"fixture fence self-test: S1 with nothing registered, a "
				"poll naming the fixture message id and a getChannels "
				"naming the fixture peer id both pass, are both counted "
				"and write no row"_q,
				u"poll=[%1] getChannels=[%2] inspected=%3 refusals=%4 "
				"rows=[%5]"_q.arg(
					OfferedText(poll),
					OfferedText(channels),
					QString::number(fence.inspectedCount()),
					QString::number(fence.refusalCount()),
					RowsText(rows)));
		},
	});

	runner->add({
		.name = u"fixture fence self-test: S2 a registered message id"_q,
		.then = [=] {
			auto &fence = state->registered;
			RegisterSelfTestMessage(fence);
			RegisterSelfTestPeer(fence);

			const auto mixed = Offer(
				fence,
				Poll(state->nextId++, { 15, kSelfTestMessage, 16 }));
			Check(
				RefusedOnly(mixed, kPollMethod, message, messageIds),
				u"fixture fence self-test: S2 a poll naming the registered "
				"message id among two real ids is refused and recorded "
				"with that id alone"_q,
				OfferedText(mixed));

			const auto neighbour = Offer(
				fence,
				Poll(state->nextId++, { 15, kSelfTestMessage + 1 }));
			Check(
				Passed(neighbour),
				u"fixture fence self-test: S2 control: the same poll naming "
				"the neighbour id passes and writes no row"_q,
				OfferedText(neighbour));

			const auto request = Search(state->nextId++);
			const auto rawHit = RawWordHit(request, kSelfTestMessage);
			const auto search = Offer(fence, request);
			Check(
				rawHit && Passed(search),
				u"fixture fence self-test: S2 a messages.search whose "
				"channel LONG equals the registered message id and whose "
				"query quotes it with an INT tag passes and writes no row, "
				"although a raw body word holds the id"_q,
				u"rawWordHit=%1 %2"_q.arg(
					Bit(rawHit),
					OfferedText(search)));
		},
	});

	runner->add({
		.name = u"fixture fence self-test: S3 a registered peer id"_q,
		.then = [=] {
			auto &fence = state->registered;

			const auto named = Offer(
				fence,
				GetChannels(state->nextId++, kSelfTestPeer));
			Check(
				RefusedOnly(named, kChannelsMethod, peer, peerIds),
				u"fixture fence self-test: S3 a getChannels naming the "
				"registered peer id is refused and recorded with that id "
				"alone"_q,
				OfferedText(named));

			const auto neighbour = Offer(
				fence,
				GetChannels(state->nextId++, kSelfTestPeer + 1));
			Check(
				Passed(neighbour),
				u"fixture fence self-test: S3 control: the same getChannels "
				"naming the neighbour id passes and writes no row"_q,
				OfferedText(neighbour));

			const auto request = Poll(state->nextId++, { 1, 253 });
			const auto rawHit = RawPairHit(request, kSelfTestPeer);
			const auto halves = Offer(fence, request);
			Check(
				rawHit && Passed(halves),
				u"fixture fence self-test: S3 a poll naming ids 1 and 253 "
				"passes and writes no row, although their adjacent raw "
				"words equal the registered peer id"_q,
				u"rawPairHit=%1 %2"_q.arg(
					Bit(rawHit),
					OfferedText(halves)));
		},
	});

	runner->add({
		.name = u"fixture fence self-test: S4 the verdict states"_q,
		.then = [=] {
			RegisterSelfTestMessage(state->clean);
			const auto passed = Offer(
				state->clean,
				Poll(state->nextId++, { 15, kSelfTestMessage + 1 }));
			const auto clean = state->clean.verdict();
			Check(
				Passed(passed)
					&& (clean.state == FixtureFenceState::Clean)
					&& clean.clean()
					&& (clean.inspected == 1)
					&& (clean.registered == 1)
					&& !clean.refusals
					&& clean.observation.startsWith(u"state=clean "_q),
				u"fixture fence self-test: S4 a registered fence that "
				"inspected a request and refused none reads clean"_q,
				u"clean=%1 %2 offered=[%3]"_q.arg(
					Bit(clean.clean()),
					clean.observation,
					OfferedText(passed)));

			RegisterSelfTestMessage(state->refused);
			const auto refusedRequest = state->nextId++;
			const auto refused = Offer(
				state->refused,
				Poll(refusedRequest, { 15, kSelfTestMessage, 16 }));
			const auto refusals = state->refused.verdict();
			const auto row = u"refused method=%1 request=%2 ids=%3"_q.arg(
				kPollMethod,
				QString::number(refusedRequest),
				messageIds);
			Check(
				RefusedOnly(refused, kPollMethod, message, messageIds)
					&& (refusals.state == FixtureFenceState::Refusals)
					&& !refusals.clean()
					&& (refusals.inspected == 1)
					&& (refusals.registered == 1)
					&& (refusals.refusals == 1)
					&& refusals.observation.startsWith(u"state=refusals "_q)
					&& refusals.observation.contains(row),
				u"fixture fence self-test: S4 one refusal reads refusals, "
				"not clean, and names its row"_q,
				u"clean=%1 %2 offered=[%3]"_q.arg(
					Bit(refusals.clean()),
					refusals.observation,
					OfferedText(refused)));

			RegisterSelfTestMessage(state->uninspected);
			const auto nothing = state->uninspected.verdict();
			Check(
				(nothing.state == FixtureFenceState::NothingInspected)
					&& !nothing.clean()
					&& !nothing.inspected
					&& (nothing.registered == 1)
					&& !nothing.refusals
					&& nothing.observation.startsWith(
						u"state=nothing-inspected "_q),
				u"fixture fence self-test: S4 a registered fence that "
				"inspected nothing reads nothing-inspected, not clean"_q,
				u"clean=%1 %2"_q.arg(
					Bit(nothing.clean()),
					nothing.observation));
		},
	});
}

} // namespace Test

#endif // _DEBUG
