/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_publication_identity_scan.h"

#include "test/test_log.h"
#include "test/test_runner.h"

#include <QtCore/QTemporaryDir>

namespace Test {
namespace {

const auto kDay = QDate(2026, 1, 15);
const auto kDayIndex = u"20260115"_q;
const auto kPlanted = u"publication-synthetic-planted-3d8f941a"_q;
const auto kEarlierControl = u"publication-synthetic-earlier-6e294c1b"_q;
template <typename Type>
concept HasCleanVerdict = requires(const Type &reading) { reading.clean; };

static_assert(!HasCleanVerdict<LaunchLogEnumerationReading>);
static_assert(!std::is_convertible_v<LaunchLogEnumerationReading, SecrecyReading>);
static_assert(!std::is_convertible_v<PublicationIdentityReading, SecrecyReading>);

const auto kClasses = std::array{
	SecrecyClass::TestLog,
	SecrecyClass::AppLog,
	SecrecyClass::DebugLog,
	SecrecyClass::MtpLog,
};

[[nodiscard]] PublicationIdentity Identity() {
	return {
		.firstName = u"Li"_q,
		.lastName = u"Syntheticriver"_q,
		.username = u"audit_example_handle"_q,
		.phone = u"+15551234567"_q,
		.userId = u"7"_q,
	};
}

[[nodiscard]] QString NamedControl(SecrecyClass cls) {
	return u"publication-synthetic-named-"_q + SecrecyClassName(cls);
}

[[nodiscard]] QString Entry(const QString &body) {
	return u"[10:07:00.001 01-0000001] "_q + body;
}

[[nodiscard]] QString MtpEntry(const QString &body) {
	return Entry(u"(dc:2_main) "_q + body);
}

[[nodiscard]] QStringList MtpBase() {
	return {
		kDayIndex,
		MtpEntry(u"Send: { msgs_ack }"_q),
		MtpEntry(u"Recv: { msgs_ack }"_q),
		MtpEntry(kPlanted),
		MtpEntry(NamedControl(SecrecyClass::MtpLog)),
	};
}

struct Fixture {
	QStringList test;
	QStringList app;
	std::map<QString, QStringList> parts;
	LaunchLogEnumerationArgs args;
	QString planted = kPlanted;
	QDateTime start = QDateTime(kDay, QTime(10, 5));
	QDateTime end = QDateTime(kDay, QTime(10, 20));
	Fn<void(SecrecyLaunchLogs&)> change;
	bool testFile = true;
	bool strict = false;
};

[[nodiscard]] Fixture CleanFixture() {
	auto result = Fixture();
	result.test = { kPlanted, NamedControl(SecrecyClass::TestLog) };
	result.app = {
		u"[2026.01.15 10:05:00] "_q + kPlanted,
		u"[2026.01.15 10:05:01] "_q + NamedControl(SecrecyClass::AppLog),
	};
	for (const auto &minute : { u"00"_q, u"15"_q }) {
		result.parts[u"log_10_"_q + minute + u".txt"_q] = {
			kDayIndex,
			Entry(kPlanted),
			Entry(NamedControl(SecrecyClass::DebugLog)),
		};
		result.parts[u"mtp_10_"_q + minute + u".txt"_q] = MtpBase();
	}
	result.args.testLogControl = NamedControl(SecrecyClass::TestLog);
	result.args.appLogControl = NamedControl(SecrecyClass::AppLog);
	result.args.debugLogControl = NamedControl(SecrecyClass::DebugLog);
	result.args.mtpLogControl = NamedControl(SecrecyClass::MtpLog);
	return result;
}

enum class Placement { Plain, Send, Recv };

[[nodiscard]] QString PlacementName(Placement placement) {
	switch (placement) {
	case Placement::Plain: return u"plain"_q;
	case Placement::Send: return u"Send"_q;
	case Placement::Recv: return u"Recv"_q;
	}
	Unexpected("Placement");
}

void Inject(
		Fixture &fixture,
		SecrecyClass cls,
		const QString &body,
		Placement placement = Placement::Plain) {
	auto lines = body.split(QChar('\n'));
	if (placement != Placement::Plain) {
		lines.front().prepend(PlacementName(placement) + u": "_q);
	}
	if (cls == SecrecyClass::TestLog) {
		fixture.test.push_back(lines.join(u"\\u000A"_q));
	} else if (cls == SecrecyClass::AppLog) {
		lines.front().prepend(u"[2026.01.15 10:07:00] "_q);
		fixture.app += lines;
	} else {
		lines.front() = (cls == SecrecyClass::MtpLog)
			? MtpEntry(lines.front())
			: Entry(lines.front());
		fixture.parts[(cls == SecrecyClass::MtpLog)
			? u"mtp_10_15.txt"_q
			: u"log_10_15.txt"_q] += lines;
	}
}

[[nodiscard]] bool WriteLines(const QString &path, const QStringList &lines) {
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const auto bytes = (lines.join(QChar('\n')) + QChar('\n')).toUtf8();
	return file.write(bytes) == bytes.size();
}

struct Result {
	bool prepared = false;
	SecrecyLaunchLogs logs;
	LaunchLogEnumerationReading enumeration;
	PublicationIdentityReading identity;
	SecrecyReading emptySecrets;
	SecrecyReading suppliedSecrets;
};

[[nodiscard]] Result RunFixture(
		const Fixture &fixture,
		const PublicationIdentity &identity = Identity()) {
	auto result = Result();
	const auto dir = QTemporaryDir();
	if (!dir.isValid()) {
		return result;
	}
	const auto evidence = dir.path() + u"/evidence/"_q;
	const auto working = dir.path() + u"/work/"_q;
	if (!QDir().mkpath(evidence)
		|| !QDir().mkpath(working + u"DebugLogs"_q)) {
		return result;
	}
	if (fixture.testFile
		&& !WriteLines(evidence + u"test_log.txt"_q, fixture.test)) {
		return result;
	}
	for (const auto &[name, lines] : fixture.parts) {
		if (!WriteLines(working + u"DebugLogs/"_q + name, lines)) {
			return result;
		}
	}
	result.logs = SelectLaunchLogs(
		working,
		evidence,
		fixture.app.isEmpty()
			? QString()
			: fixture.app.join(QChar('\n')) + QChar('\n'),
		fixture.start,
		fixture.end,
		fixture.args.earlierParts);
	if (fixture.change) {
		fixture.change(result.logs);
	}
	result.enumeration = ReadLaunchLogEnumeration(
		result.logs,
		fixture.planted,
		fixture.args);
	result.identity = ReadPublicationIdentity(
		identity,
		result.logs,
		fixture.planted,
		fixture.args);
	if (fixture.strict) {
		result.emptySecrets = ReadSecrecy({}, result.logs, fixture.planted);
		result.suppliedSecrets = ReadSecrecy(
			{ .tokens = { u"synthetic-strict-token-a489d03f"_q } },
			result.logs,
			fixture.planted);
	}
	result.prepared = true;
	return result;
}

struct Counts {
	int profiles = 0;
	int usernames = 0;
	int phones = 0;
	int userIds = 0;
};

[[nodiscard]] bool ExactCounts(
		const PublicationIdentityReading &reading,
		SecrecyClass injected,
		Counts expected) {
	if (reading.classes.size() != 4 && reading.classes.size() != 5) {
		return false;
	}
	for (const auto cls : kClasses) {
		if (ranges::count_if(reading.classes, [=](const auto &item) {
				return item.cls == cls;
			}) != 1) {
			return false;
		}
	}
	if (reading.classes.size() == 5
		&& ranges::count_if(reading.classes, [](const auto &item) {
			return item.cls == SecrecyClass::EarlierParts;
		}) != 1) {
		return false;
	}
	for (const auto &item : reading.classes) {
		const auto want = item.cls == injected ? expected : Counts();
		if (item.profiles != want.profiles
			|| item.usernames != want.usernames
			|| item.phones != want.phones
			|| item.userIds != want.userIds) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString Details(const PublicationIdentityReading &reading) {
	return PublicationIdentityRows(reading).join(u" | "_q);
}

void CheckCounts(
		const Result &result,
		const QString &name,
		SecrecyClass cls = SecrecyClass::TestLog,
		Counts counts = {},
		int families = 4) {
	const auto dirty = counts.profiles + counts.usernames
		+ counts.phones + counts.userIds;
	const auto &reading = result.identity;
	Check(result.prepared && result.enumeration.decided
		&& reading.decided
		&& reading.clean == !dirty
		&& reading.controlsHold
		&& reading.controlFamilies == families
		&& reading.positiveControls == families
		&& reading.negativeControls == families
		&& ExactCounts(reading, cls, counts),
		u"publication self-test: "_q + name,
		Details(reading));
}

[[nodiscard]] bool HasRefusal(
		const LaunchLogEnumerationReading &reading,
		std::optional<SecrecyClass> cls,
		LaunchLogRefusal refusal) {
	if (!cls) {
		return ranges::contains(reading.refusals, refusal);
	}
	const auto item = reading.find(*cls);
	return item && !item->decided && ranges::contains(item->refusals, refusal);
}

void CheckEnumerationRefusal(
		const Result &result,
		const QString &name,
		std::optional<SecrecyClass> cls,
		LaunchLogRefusal refusal) {
	Check(result.prepared && !result.enumeration.decided
		&& !result.identity.decided && !result.identity.clean
		&& HasRefusal(result.enumeration, cls, refusal)
		&& result.enumeration.classes.size() >= kClasses.size(),
		u"publication self-test: enumeration refuses "_q + name,
		LaunchLogEnumerationRows(result.enumeration).join(u" | "_q));
}

void CheckParserRefusal(
		const Result &result,
		const QString &name,
		PublicationIdentityRefusal refusal) {
	Check(result.prepared && result.enumeration.decided
		&& !result.identity.decided && !result.identity.clean
		&& ranges::contains(result.identity.refusals, refusal),
		u"publication self-test: parser refuses "_q + name,
		Details(result.identity));
}

void SelfPublicationCoincidences() {
	CheckCounts(RunFixture(CleanFixture()), u"controlled empty tree"_q);
	const auto negatives = QStringList{
		u"Li counter=7 timing=7p coordinates=7 request_id=7 id=7"_q,
		u"audit_example_handle_extra xaudit_example_handle audit_example_hand"_q,
		u"https://t.me/audit_example_handle_extra /audit_example_handle_suffix/"_q,
		u"audit_example_handle\u0301 \u03B1audit_example_handle"_q,
		u"+155512345678 suffix=5551234567 phone=155512345678"_q,
		u"+1555123456 x+15551234567 +15551234567x _+15551234567"_q,
		u"+1555 count=1234567 phone=1555 other=1234567"_q,
		u"(+155512345678) (+1 (555) 123-45678)"_q,
		u"+15551234567 8"_q,
		u"+15551234567 (8)"_q,
		u"+15551234567-suffix"_q,
		u"(+15551234567x) (x+15551234567)"_q,
		u"+1555 (work) 1234567"_q,
		u"(+1555123456) suffix=5551234567"_q,
		u"+1 (-555) 123-45678"_q,
		u"+1 (-555) 123-4567 8"_q,
		u"+1 (-555) 123-4567 (-8)"_q,
		u"+1 (-555) 123-4567x"_q,
		u"+1 (-555) 123-4567-suffix"_q,
		u"+1 (-555) 123-456"_q,
		u"+1 (-work) 5551234567"_q,
		u"+1 (--555x) 1234567"_q,
		u"+1 ((-555)) 123-4567"_q,
		u"+1 (-555 123-4567"_q,
		u"{ channel id: 7 } { user child: { channel id: 7 } }"_q,
		u"{ unrelated user_id: 7 id: 7 } user_id=7.5 user_id=7p"_q,
		u"{ user first_name: \"Li\" } { user last_name: \"Syntheticriver\" }"_q,
		u"{ user first_name: \"Li\" child: { other last_name: \"Syntheticriver\" } }"_q,
		u"{ unrelated text: \"} user_id: 7 { user id: 7\" }"_q,
		u"{ user first_name: \"Other\" text: \"last_name: \\\"Syntheticriver\\\" }\" }"_q,
		u"{ user first_name: \"Li\" last_name: \"Other\" }"_q,
		u"{ user first_name: \"Other\" last_name: \"Syntheticriver\" }"_q,
		u"{ user first_name: \"_Li Syntheticriver_\" last_name: \"\" }"_q,
		u"{ user first_name: \"Li Syntheticriver\u0301\" last_name: \"\" }"_q,
		u"_Li Syntheticriver_ Li Syntheticriver\u0301"_q,
		u"Li\\\\u000ASyntheticriver"_q,
		u"Li\\u000A{ other }\\u000ASyntheticriver"_q,
		u"\"Li\"\\u000A\"Syntheticriver\""_q,
		u"{ user first_name: \"Li\" }\\u000A"
			"{ user last_name: \"Syntheticriver\" }"_q,
	};
	for (const auto cls : kClasses) {
		for (auto i = 0; i < negatives.size(); ++i) {
			auto fixture = CleanFixture();
			Inject(fixture, cls, negatives[i]);
			CheckCounts(RunFixture(fixture),
				u"coincidence %1 %2"_q.arg(SecrecyClassName(cls)).arg(i));
		}
	}
	auto collision = PublicationIdentity{ .username = u"profile"_q };
	const auto run = RunFixture(CleanFixture(), collision);
	const auto rows = PublicationIdentityRows(run.identity);
	CheckCounts(run, u"fixed classification collision"_q,
		SecrecyClass::TestLog, {}, 1);
	Check(rows.join(QChar('\n')).contains(u"family=profile"_q),
		u"publication self-test: fixed schema may equal supplied public token"_q);
}

void SelfPublicationPresentations() {
	struct Presentation {
		QString name;
		QString body;
		Counts counts;
	};
	const auto isolated = std::vector<Presentation>{
		{ u"display"_q, u"Li Syntheticriver"_q, { .profiles = 1 } },
		{ u"username"_q, u"https://t.me/audit_example_handle"_q,
			{ .usernames = 1 } },
		{ u"phone"_q, u"+1 (555) 123-4567"_q, { .phones = 1 } },
		{ u"user id"_q, u"user_id: 7"_q, { .userIds = 1 } },
	};
	for (const auto cls : kClasses) {
		for (const auto placement : {
				Placement::Plain, Placement::Send, Placement::Recv }) {
			for (const auto &item : isolated) {
				const auto name = SecrecyClassName(cls) + QChar(' ')
					+ PlacementName(placement) + QChar(' ') + item.name;
				CheckCounts(RunFixture(CleanFixture()), name + u" zero control"_q);
				auto fixture = CleanFixture();
				Inject(fixture, cls, item.body, placement);
				CheckCounts(RunFixture(fixture), name, cls, item.counts);
			}
		}
	}
	for (const auto cls : kClasses) {
		auto fixture = CleanFixture();
		Inject(fixture, cls, u"user_id=7"_q);
		CheckCounts(RunFixture(fixture), u"explicit id equality "_q
			+ SecrecyClassName(cls), cls, { .userIds = 1 });
	}
	const auto spellings = QStringList{
		u"audit_example_handle"_q,
		u"@AUDIT_EXAMPLE_HANDLE"_q,
		u"t.me/audit_example_handle"_q,
		u"http://t.me/audit_example_handle"_q,
		u"https://t.me/audit_example_handle"_q,
		u"http://telegram.me/audit_example_handle"_q,
		u"https://telegram.me/audit_example_handle"_q,
		u"tg://resolve?domain=audit_example_handle"_q,
		u"/synthetic/screenshots/audit_example_handle/image.png"_q,
	};
	for (auto i = 0; i < spellings.size(); ++i) {
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::TestLog, spellings[i]);
		CheckCounts(RunFixture(fixture), u"username spelling %1"_q.arg(i),
			SecrecyClass::TestLog, { .usernames = 1 });
	}
	for (const auto &body : {
			u"+15551234567"_q,
			u"call +15551234567 tomorrow"_q,
			u"call +1 (555) 123-4567 tomorrow"_q,
			u"phone=15551234567"_q,
			u"phone: \"15551234567\""_q,
			u"{ user phone: \"1 (555) 123-4567\" }"_q }) {
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::MtpLog, body);
		CheckCounts(RunFixture(fixture), u"complete phone shape"_q,
			SecrecyClass::MtpLog, { .phones = 1 });
	}
	for (const auto cls : kClasses) {
		for (const auto &body : {
				u"+1 (555) 123-4567"_q,
				u"+1 (-555) 123-4567"_q,
				u"+1 ( -555-) 123-4567"_q,
				u"+1 (-- 555) 123-4567"_q,
				u"(+1 (-555) 123-4567)"_q,
				u"call +1 (-555) 123-4567 (work)"_q,
				u"call +1 (-555) 123-4567 (-work)"_q,
				u"+1 (-) 555 123-4567"_q }) {
			auto fixture = CleanFixture();
			Inject(fixture, cls, body);
			CheckCounts(RunFixture(fixture), u"phone separator boundary "_q
				+ SecrecyClassName(cls), cls, { .phones = 1 });
		}
		for (const auto &body : {
				u"(+15551234567)"_q,
				u"(+1 (555) 123-4567)"_q,
				u"call +15551234567 (work)"_q,
				u"call +1 (555) 123-4567 (work)"_q }) {
			auto fixture = CleanFixture();
			Inject(fixture, cls, body);
			CheckCounts(RunFixture(fixture), u"bounded phone punctuation "_q
				+ SecrecyClassName(cls), cls, { .phones = 1 });
		}
		for (const auto &body : {
				u"{ user first_name: \"Li Syntheticriver\" last_name: \"\" }"_q,
				u"{ user first_name: \"\" last_name: \"Li Syntheticriver\" }"_q,
				u"{ user first_name: \"Li\" last_name: \"Syntheticriver\" }"_q }) {
			auto fixture = CleanFixture();
			Inject(fixture, cls, body);
			CheckCounts(RunFixture(fixture), u"complete display in name fields "_q
				+ SecrecyClassName(cls), cls, { .profiles = 1 });
		}
	}
	struct QuoteBoundary {
		PublicationIdentity identity;
		QString omitted;
		QString decoded;
		QString structured;
	};
	const auto quoteBoundaries = std::vector<QuoteBoundary>{
		{
			{ .firstName = u"Li\""_q, .lastName = u"Syntheticriver"_q },
			u"Li\"unrelated\" Syntheticriver"_q,
			u"\"Li\\\" Syntheticriver\""_q,
			u"{ user first_name: \"Li\\\"\" last_name: \"Syntheticriver\" }"_q,
		},
		{
			{ .firstName = u"Li"_q, .lastName = u"\" Syntheticriver"_q },
			u"Li \"hidden\" Syntheticriver"_q,
			u"{ other text: \"Li \\\" Syntheticriver\" }"_q,
			u"{ user first_name: \"Li\" last_name: \"\\\" Syntheticriver\" }"_q,
		},
		{
			{ .firstName = u"Mira"_q, .lastName = u"Boundary"_q },
			u"Mira\"inserted\" Boundary"_q,
			u"\"Mira Boundary\""_q,
			u"{ user first_name: \"Mira\" last_name: \"Boundary\" }"_q,
		},
		{
			{ .firstName = u"A'B"_q, .lastName = u"C-D"_q },
			u"A'B \"inserted\" C-D"_q,
			u"\"A'B C-D\""_q,
			u"{ user first_name: \"A'B\" last_name: \"C-D\" }"_q,
		},
		{
			{ .firstName = u"A\" Z"_q, .lastName = u"Omega"_q },
			u"A\"inserted\" Z Omega"_q,
			u"\"A\\\" Z Omega\""_q,
			u"{ user first_name: \"A\\\" Z\" last_name: \"Omega\" }"_q,
		},
	};
	for (const auto cls : kClasses) {
		for (auto i = 0; i < quoteBoundaries.size(); ++i) {
			const auto &item = quoteBoundaries[i];
			for (const auto &[body, expected] : std::array{
					std::pair{ item.omitted, 0 },
					std::pair{ item.decoded, 1 },
					std::pair{ item.structured, 1 } }) {
				auto fixture = CleanFixture();
				Inject(fixture, cls, body);
				CheckCounts(RunFixture(fixture, item.identity),
					u"quote matching boundary %1 %2 %3"_q
						.arg(i).arg(expected).arg(SecrecyClassName(cls)),
					cls, { .profiles = expected }, 1);
			}
		}
		for (const auto &[body, expected] : std::array{
				std::pair{ u"\"hidden\" Li Syntheticriver"_q, 1 },
				std::pair{ u"Li Syntheticriver \"hidden\""_q, 1 },
				std::pair{
					u"Li Syntheticriver \"hidden\" Li Syntheticriver"_q, 2 },
				std::pair{ u"Li\\u000A\"hidden\"\\u000ASyntheticriver"_q, 0 } }) {
			auto fixture = CleanFixture();
			Inject(fixture, cls, body);
			CheckCounts(RunFixture(fixture), u"unquoted segment boundary "_q
				+ SecrecyClassName(cls), cls, { .profiles = expected });
		}
	}
	for (const auto first : { false, true }) {
		auto fixture = CleanFixture();
		const auto identity = PublicationIdentity{
			.firstName = first ? u"Li Syntheticriver"_q : QString(),
			.lastName = first ? QString() : u"Li Syntheticriver"_q,
		};
		Inject(fixture, SecrecyClass::TestLog, first
			? u"{ user first_name: \"Li Syntheticriver\" last_name: \"\" }"_q
			: u"{ user first_name: \"\" last_name: \"Li Syntheticriver\" }"_q);
		CheckCounts(RunFixture(fixture, identity),
			u"multipart component counts once %1"_q.arg(first),
			SecrecyClass::TestLog, { .profiles = 1 }, 1);
	}
	for (const auto &[constructor, field] : std::array{
			std::pair{ u"user"_q, u"id"_q },
			std::pair{ u"userEmpty"_q, u"id"_q },
			std::pair{ u"userFull"_q, u"id"_q },
			std::pair{ u"peerUser"_q, u"user_id"_q },
			std::pair{ u"inputPeerUser"_q, u"user_id"_q },
			std::pair{ u"inputUser"_q, u"user_id"_q } }) {
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::MtpLog,
			u"{ "_q + constructor + QChar(' ') + field + u": 7 [LONG] }"_q,
			Placement::Recv);
		CheckCounts(RunFixture(fixture), u"tiny id owner "_q + constructor,
			SecrecyClass::MtpLog, { .userIds = 1 });
	}
	for (const auto cls : kClasses) {
		auto fixture = CleanFixture();
		Inject(fixture, cls,
			u"{ user\n first_name: \"Li\" [STRING]\n"
			" last_name: \"Syntheticriver\" [STRING]\n}"_q);
		CheckCounts(RunFixture(fixture), u"same-object multiline "_q
			+ SecrecyClassName(cls), cls, { .profiles = 1 });
	}
	{
		auto identity = Identity();
		identity.lastName.clear();
		for (const auto &[field, counts] : std::vector<std::pair<QString, Counts>>{
			{ u"id: 7"_q, { .profiles = 1, .userIds = 1 } },
			{ u"username: \"audit_example_handle\""_q,
				{ .profiles = 1, .usernames = 1 } },
			{ u"phone: \"15551234567\""_q, { .profiles = 1, .phones = 1 } },
		}) {
			auto fixture = CleanFixture();
			Inject(fixture, SecrecyClass::MtpLog,
				u"{ user first_name: \"Li\" "_q + field + u" }"_q);
			CheckCounts(RunFixture(fixture, identity), u"one-part plus identifying field"_q,
				SecrecyClass::MtpLog, counts);
		}
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::MtpLog,
			u"{ user first_name: \"Li\" }"_q);
		CheckCounts(RunFixture(fixture, identity), u"one-part without second field"_q);
	}
	{
		auto fixture = CleanFixture();
		auto identity = PublicationIdentity{
			.firstName = u"\u00C9lo\u00EFse"_q,
			.lastName = u"Syntheticriver"_q,
		};
		Inject(fixture, SecrecyClass::AppLog,
			u"E\u0301LO\u00CFSE   SYNTHETICRIVER"_q);
		CheckCounts(RunFixture(fixture, identity), u"NFC case whitespace"_q,
			SecrecyClass::AppLog, { .profiles = 1 }, 1);
	}
	{
		auto fixture = CleanFixture();
		const auto identity = PublicationIdentity{
			.firstName = u"A\"\\B\nC"_q,
			.lastName = u"Syntheticriver"_q,
		};
		Inject(fixture, SecrecyClass::TestLog,
			u"{ user\n first_name: \"A\\\"\\\\B\\nC\"\n"
			" last_name: \"Syntheticriver\"\n}"_q);
		CheckCounts(RunFixture(fixture, identity), u"TL string escapes"_q,
			SecrecyClass::TestLog, { .profiles = 1 }, 1);
	}
	for (const auto &separator : {
			u"\\u000A"_q, u"\\u000B"_q, u"\\u000C"_q, u"\\u000D"_q,
			u"\\u001C"_q, u"\\u001D"_q, u"\\u001E"_q,
			u"\\u0085"_q, u"\\u2028"_q, u"\\u2029"_q }) {
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::TestLog,
			u"{ user"_q + separator + u"first_name: \"Li\""_q
				+ separator + u"last_name: \"Syntheticriver\""_q
				+ separator + u"}"_q);
		CheckCounts(RunFixture(fixture), u"visible separator "_q + separator,
			SecrecyClass::TestLog, { .profiles = 1 });
		fixture = CleanFixture();
		Inject(fixture, SecrecyClass::TestLog,
			u"Li"_q + separator + u"Syntheticriver"_q);
		CheckCounts(RunFixture(fixture), u"plain display visible separator "_q
			+ separator, SecrecyClass::TestLog, { .profiles = 1 });
	}
}

void RemoveControl(Fixture &fixture, SecrecyClass cls, const QString &control) {
	const auto strip = [&](QStringList &lines) {
		for (auto &line : lines) {
			line.replace(control, u"removed-control"_q);
		}
	};
	if (cls == SecrecyClass::TestLog) {
		strip(fixture.test);
	} else if (cls == SecrecyClass::AppLog) {
		strip(fixture.app);
	} else {
		for (auto &[name, lines] : fixture.parts) {
			if (name.startsWith(cls == SecrecyClass::MtpLog
					? u"mtp_"_q : u"log_"_q)) {
				strip(lines);
			}
		}
	}
}

void AddEarlier(Fixture &fixture) {
	fixture.parts[u"log_09_00.txt"_q] = { kDayIndex, Entry(kEarlierControl) };
	fixture.args.earlierParts.push_back({
		.name = u"log_09_00.txt"_q,
		.day = kDay,
		.control = kEarlierControl,
	});
}

void SelfLogEnumeration() {
	const auto base = RunFixture(CleanFixture());
	auto inventory = base.prepared && base.enumeration.decided
		&& base.enumeration.classes.size() == 4
		&& base.enumeration.candidates == 4 && base.enumeration.accepted == 4
		&& !base.enumeration.foreign && !base.enumeration.missing
		&& !base.enumeration.unreadable;
	for (auto i = 0; i < int(kClasses.size()); ++i) {
		const auto item = base.enumeration.find(kClasses[i]);
		const auto parts = i < 2 ? 1 : 2;
		const auto lines = i < 2 ? 2 : i == 2 ? 6 : 10;
		inventory = inventory && item && item->decided
			&& base.enumeration.classes[i].cls == kClasses[i]
			&& item->candidates == parts && item->files == parts
			&& item->plantedHits == parts && item->controlHits == parts
			&& item->lines == lines
			&& item->sendHeaders == (i == 3 ? 2 : 0)
			&& item->recvHeaders == (i == 3 ? 2 : 0);
	}
	Check(inventory, u"publication self-test: enumeration exact complete inventory"_q,
		LaunchLogEnumerationRows(base.enumeration).join(u" | "_q));
	for (const auto cls : kClasses) {
		for (const auto identity : { PartIdentity::Missing, PartIdentity::Foreign }) {
			auto fixture = CleanFixture();
			fixture.change = [=](SecrecyLaunchLogs &logs) {
				for (auto &source : logs.sources) {
					if (source.cls == cls) {
						source.identity = identity;
					}
				}
			};
			CheckEnumerationRefusal(RunFixture(fixture),
				SecrecyClassName(cls) + QChar(' ') + PartIdentityName(identity),
				cls,
				identity == PartIdentity::Missing
					? LaunchLogRefusal::MissingSource
					: LaunchLogRefusal::ForeignSource);
		}
		{
			auto fixture = CleanFixture();
			fixture.change = [=](SecrecyLaunchLogs &logs) {
				std::erase_if(logs.sources, [=](const auto &source) {
					return source.cls == cls;
				});
			};
			CheckEnumerationRefusal(RunFixture(fixture),
				u"omitted "_q + SecrecyClassName(cls), cls,
				LaunchLogRefusal::NoCandidates);
		}
		{
			auto fixture = CleanFixture();
			fixture.change = [=](SecrecyLaunchLogs &logs) {
				for (auto &source : logs.sources) {
					if (source.cls == cls) {
						source.path = logs.sources.front().path + u".nonexistent"_q;
						source.fromText = false;
					}
				}
			};
			CheckEnumerationRefusal(RunFixture(fixture),
				u"actual open "_q + SecrecyClassName(cls), cls,
				LaunchLogRefusal::UnreadableSource);
		}
		{
			auto fixture = CleanFixture();
			RemoveControl(fixture, cls, kPlanted);
			CheckEnumerationRefusal(RunFixture(fixture),
				u"planted control "_q + SecrecyClassName(cls), cls,
				LaunchLogRefusal::MissingPlantedControl);
			fixture = CleanFixture();
			RemoveControl(fixture, cls, NamedControl(cls));
			CheckEnumerationRefusal(RunFixture(fixture),
				u"named control "_q + SecrecyClassName(cls), cls,
				LaunchLogRefusal::MissingNamedControl);
		}
		CheckCounts(RunFixture(CleanFixture()),
			u"restored class "_q + SecrecyClassName(cls));
	}
	{
		auto fixture = CleanFixture();
		fixture.testFile = false;
		CheckEnumerationRefusal(RunFixture(fixture), u"physical test file"_q,
			SecrecyClass::TestLog, LaunchLogRefusal::MissingSource);
		fixture = CleanFixture();
		fixture.app.clear();
		CheckEnumerationRefusal(RunFixture(fixture), u"empty app text"_q,
			SecrecyClass::AppLog, LaunchLogRefusal::MissingSource);
		fixture = CleanFixture();
		fixture.planted.clear();
		CheckEnumerationRefusal(RunFixture(fixture), u"empty planted control"_q,
			std::nullopt, LaunchLogRefusal::EmptyPlantedControl);
	}
	for (const auto direction : { Placement::Send, Placement::Recv }) {
		auto fixture = CleanFixture();
		for (auto &[name, lines] : fixture.parts) {
			if (name.startsWith(u"mtp_"_q)) {
				lines.removeIf([&](const auto &line) {
					return line.contains(PlacementName(direction) + u": "_q);
				});
			}
		}
		fixture.change = [](SecrecyLaunchLogs &logs) {
			for (auto &source : logs.sources) {
				if (source.cls == SecrecyClass::MtpLog) {
					source.name = u"arbitrary-public-name"_q;
				}
			}
		};
		CheckEnumerationRefusal(RunFixture(fixture),
			u"MtpLog direction "_q + PlacementName(direction), SecrecyClass::MtpLog,
			direction == Placement::Send
				? LaunchLogRefusal::MissingSendHeader
				: LaunchLogRefusal::MissingRecvHeader);
	}
	{
		auto fixture = CleanFixture();
		fixture.change = [](SecrecyLaunchLogs &logs) {
			++logs.candidates;
		};
		CheckEnumerationRefusal(RunFixture(fixture), u"candidate inventory"_q,
			std::nullopt, LaunchLogRefusal::CandidateInventoryMismatch);
		fixture.change = [](SecrecyLaunchLogs &logs) {
			logs.selection = LaunchLogSelection::Unspecified;
		};
		CheckEnumerationRefusal(RunFixture(fixture), u"manual selection"_q,
			std::nullopt, LaunchLogRefusal::UnspecifiedSelection);
		fixture = CleanFixture();
		fixture.start = QDateTime();
		CheckEnumerationRefusal(RunFixture(fixture), u"invalid time"_q,
			std::nullopt, LaunchLogRefusal::InvalidTimes);
		fixture = CleanFixture();
		fixture.end = fixture.start.addSecs(-1);
		CheckEnumerationRefusal(RunFixture(fixture), u"reversed window"_q,
			std::nullopt, LaunchLogRefusal::ReversedWindow);
	}
	for (const auto quarters : { 199, 200 }) {
		auto fixture = CleanFixture();
		fixture.parts.clear();
		fixture.start = QDateTime(kDay, QTime(0, 0));
		fixture.end = fixture.start.addSecs(quarters * 15 * 60);
		const auto result = RunFixture(fixture);
		Check(result.prepared && result.logs.candidates == 400
			&& result.logs.selection == (quarters == 199
				? LaunchLogSelection::Complete : LaunchLogSelection::CappedWindow)
			&& !result.enumeration.decided,
			u"publication self-test: selector quarter boundary %1"_q.arg(quarters),
			LaunchLogEnumerationRows(result.enumeration).join(u" | "_q));
		if (quarters == 200) {
			CheckEnumerationRefusal(result, u"capped window"_q,
				std::nullopt, LaunchLogRefusal::CappedWindow);
		}
	}
	{
		auto fixture = CleanFixture();
		fixture.change = [](SecrecyLaunchLogs &logs) {
			for (auto &source : logs.sources) {
				if (source.name == u"DebugLogs/log_10_15.txt"_q) {
					source.path += u".nonexistent"_q;
				}
			}
		};
		const auto result = RunFixture(fixture);
		CheckEnumerationRefusal(result, u"unreadable beside accepted"_q,
			SecrecyClass::DebugLog, LaunchLogRefusal::UnreadableSource);
		const auto item = result.enumeration.find(SecrecyClass::DebugLog);
		Check(item && item->files == 1 && item->unreadable == 1
			&& item->plantedHits == 1,
			u"publication self-test: unreadable bytes are not examined"_q);
	}
	for (auto variant = 0; variant != 8; ++variant) {
		auto fixture = CleanFixture();
		AddEarlier(fixture);
		if (variant == 1) {
			fixture.args.earlierParts.front().name = u"../log_09_00.txt"_q;
		} else if (variant == 2) {
			fixture.args.earlierParts.front().day = QDate();
		} else if (variant == 3) {
			fixture.parts[u"log_09_00.txt"_q].front() = u"20260114"_q;
		} else if (variant == 4) {
			fixture.parts[u"log_09_00.txt"_q].pop_back();
		} else if (variant == 5) {
			fixture.args.earlierParts.front().control.clear();
		} else if (variant == 6) {
			fixture.change = [](SecrecyLaunchLogs &logs) {
				std::erase_if(logs.sources, [](const auto &source) {
					return source.cls == SecrecyClass::EarlierParts;
				});
			};
		} else if (variant == 7) {
			fixture.parts.erase(u"log_09_00.txt"_q);
		}
		const auto result = RunFixture(fixture);
		if (!variant) {
			CheckCounts(result, u"explicit controlled earlier part"_q);
			const auto item = result.enumeration.find(SecrecyClass::EarlierParts);
			Check(item && item->decided && item->files == 1
				&& item->controlHits == 1 && item->lines == 2,
				u"publication self-test: earlier exact inventory"_q);
		} else {
			const auto refusal = variant <= 2
				? LaunchLogRefusal::InvalidEarlierName
				: variant == 3
				? LaunchLogRefusal::ForeignSource
				: variant <= 5
				? LaunchLogRefusal::MissingEarlierControl
				: variant == 6
				? LaunchLogRefusal::EarlierInventoryMismatch
				: LaunchLogRefusal::MissingSource;
			CheckEnumerationRefusal(result, u"earlier variant %1"_q.arg(variant),
				variant == 6 ? std::nullopt : std::optional(SecrecyClass::EarlierParts),
				refusal);
		}
	}
}

void SelfPublicationSelection() {
	for (const auto cls : kClasses) {
		auto fixture = CleanFixture();
		Inject(fixture, cls, u"Li"_q);
		Inject(fixture, cls, u"Syntheticriver"_q);
		CheckCounts(RunFixture(fixture), u"plain display separate entries "_q
			+ SecrecyClassName(cls));
		fixture = CleanFixture();
		Inject(fixture, cls, u"Li\nSyntheticriver"_q);
		CheckCounts(RunFixture(fixture), u"plain display physical row boundary "_q
			+ SecrecyClassName(cls), cls,
			{ .profiles = cls == SecrecyClass::TestLog ? 1 : 0 });
	}
	{
		auto fixture = CleanFixture();
		fixture.parts[u"log_10_00.txt"_q].push_back(Entry(u"Li"_q));
		fixture.parts[u"log_10_15.txt"_q].push_back(Entry(u"Syntheticriver"_q));
		CheckCounts(RunFixture(fixture), u"plain display separate sources"_q);
	}
	for (auto variant = 0; variant != 3; ++variant) {
		auto fixture = CleanFixture();
		fixture.parts[u"log_09_45.txt"_q] = {
			kDayIndex, Entry(u"Li Syntheticriver"_q),
		};
		if (variant == 0) {
			fixture.parts[u"log_10_15.txt"_q] = {
				u"20260114"_q, Entry(u"Li Syntheticriver"_q),
			};
		} else if (variant == 1) {
			fixture.parts.erase(u"log_10_15.txt"_q);
		} else {
			Inject(fixture, SecrecyClass::DebugLog, u"Li Syntheticriver"_q);
		}
		const auto result = RunFixture(fixture);
		CheckCounts(result, u"window selection %1"_q.arg(variant),
			SecrecyClass::DebugLog, { .profiles = variant == 2 ? 1 : 0 });
		const auto item = result.enumeration.find(SecrecyClass::DebugLog);
		Check(item && item->candidates == 2 && item->files == (variant == 2 ? 2 : 1)
			&& item->foreign == (variant == 0 ? 1 : 0)
			&& item->missing == (variant == 1 ? 1 : 0),
			u"publication self-test: rejected window candidate inventory %1"_q.arg(variant));
	}
	for (const auto dirtyCurrent : { false, true }) {
		auto fixture = CleanFixture();
		fixture.parts[u"log_10_15.txt"_q] = {
			kDayIndex,
			Entry(dirtyCurrent ? u"old clean"_q : u"Li Syntheticriver"_q),
			u"NEW LOGGING INSTANCE STARTED!!!"_q,
			Entry(dirtyCurrent ? u"Li Syntheticriver"_q : u"current clean"_q),
			Entry(kPlanted),
			Entry(NamedControl(SecrecyClass::DebugLog)),
		};
		const auto result = RunFixture(fixture);
		CheckCounts(result, u"last-banner current dirty=%1"_q.arg(dirtyCurrent),
			SecrecyClass::DebugLog, { .profiles = dirtyCurrent ? 1 : 0 });
		const auto item = result.enumeration.find(SecrecyClass::DebugLog);
		Check(item && item->files == 2 && item->lines == 7
			&& item->plantedHits == 2 && item->controlHits == 2,
			u"publication self-test: only accepted banner slice examined"_q);
	}
	{
		auto fixture = CleanFixture();
		AddEarlier(fixture);
		fixture.parts[u"log_09_00.txt"_q].push_back(Entry(u"Li Syntheticriver"_q));
		CheckCounts(RunFixture(fixture), u"explicit earlier subject"_q,
			SecrecyClass::EarlierParts, { .profiles = 1 });
		fixture.args.earlierParts.clear();
		CheckCounts(RunFixture(fixture), u"unnamed earlier bytes ignored"_q);
	}
	{
		auto fixture = CleanFixture();
		fixture.parts[u"log_10_00.txt"_q].push_back(
			Entry(u"{ user first_name: \"Li\" }"_q));
		fixture.parts[u"log_10_15.txt"_q].push_back(
			Entry(u"{ user last_name: \"Syntheticriver\" }"_q));
		CheckCounts(RunFixture(fixture), u"profiles do not join across sources"_q);
		fixture = CleanFixture();
		Inject(fixture, SecrecyClass::DebugLog,
			u"{ user first_name: \"Li\" }"_q);
		Inject(fixture, SecrecyClass::DebugLog,
			u"{ user last_name: \"Syntheticriver\" }"_q);
		CheckCounts(RunFixture(fixture), u"profiles do not join across entries"_q);
	}
}

void SelfAuditSeparation() {
	{
		auto fixture = CleanFixture();
		fixture.strict = true;
		const auto result = RunFixture(fixture);
		CheckCounts(result, u"separate identity claim"_q);
		Check(result.enumeration.decided && result.identity.clean
			&& !result.emptySecrets.decided && !result.emptySecrets.clean
			&& result.emptySecrets.undecidedReasons.contains(u"no secret given"_q)
			&& result.suppliedSecrets.decided && result.suppliedSecrets.clean,
			u"publication self-test: coverage identity and supplied-secret claims differ"_q);
	}
	struct InputCase {
		QString name;
		PublicationIdentity identity;
		PublicationIdentityRefusal refusal;
	};
	auto cases = std::vector<InputCase>{
		{ u"empty"_q, {}, PublicationIdentityRefusal::NoUsableIdentity },
		{ u"lone tiny name"_q, { .firstName = u"Li"_q },
			PublicationIdentityRefusal::NoUsableIdentity },
	};
	for (auto variant = 0; variant != 13; ++variant) {
		auto identity = Identity();
		auto refusal = PublicationIdentityRefusal::InvalidFirstName;
		if (variant == 0) {
			identity.firstName = QString(257, QChar('a'));
		} else if (variant == 1) {
			identity.firstName = u"   "_q;
		} else if (variant == 2) {
			identity.firstName = QString(QChar(0xD800));
		} else if (variant == 3) {
			identity.lastName = QString(257, QChar('b'));
			refusal = PublicationIdentityRefusal::InvalidLastName;
		} else if (variant <= 6) {
			identity.username = variant == 4 ? QString(65, QChar('a'))
				: variant == 5 ? u"bad-handle"_q : u"123_456"_q;
			refusal = PublicationIdentityRefusal::InvalidUsername;
		} else if (variant <= 9) {
			identity.phone = variant == 7 ? u"+1234567890123456"_q
				: variant == 8 ? u"15551234567"_q : u"+012345"_q;
			refusal = PublicationIdentityRefusal::InvalidPhone;
		} else {
			identity.userId = variant == 10 ? QString(21, QChar('7'))
				: variant == 11 ? u"07"_q : u"0"_q;
			refusal = PublicationIdentityRefusal::InvalidUserId;
		}
		cases.push_back({ u"invalid descriptor %1"_q.arg(variant), identity, refusal });
	}
	for (const auto &item : cases) {
		CheckParserRefusal(RunFixture(CleanFixture(), item.identity), item.name,
			item.refusal);
	}
	{
		const auto identity = PublicationIdentity{
			.firstName = QString(256, QChar('a')),
			.lastName = QString(256, QChar('b')),
			.username = QString(64, QChar('c')),
			.phone = u"+123456789012345"_q,
			.userId = u"12345678901234567890"_q,
		};
		CheckCounts(RunFixture(CleanFixture(), identity),
			u"all descriptor supported bounds"_q);
	}
	for (const auto &[name, body, refusal] : std::vector<std::tuple<
			QString, QString, PublicationIdentityRefusal>>{
		{ u"final source EOF"_q, u"{ user first_name: \"Li\""_q,
			PublicationIdentityRefusal::UnterminatedProfile },
		{ u"unterminated relevant string"_q, u"{ user first_name: \"Li"_q,
			PublicationIdentityRefusal::UnterminatedString },
		{ u"unsupported relevant escape"_q, u"{ user first_name: \"Li\\t\" }"_q,
			PublicationIdentityRefusal::UnsupportedStringEscape },
		{ u"lost closer"_q, u"}"_q, PublicationIdentityRefusal::LostObjectScope },
		{ u"unquoted profile field"_q, u"{ user first_name: Li }"_q,
			PublicationIdentityRefusal::LostObjectScope },
	}) {
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::MtpLog, body);
		CheckParserRefusal(RunFixture(fixture), name, refusal);
	}
	for (const auto &boundary : { u"entry"_q, u"source"_q }) {
		auto fixture = CleanFixture();
		fixture.parts[u"log_10_00.txt"_q].push_back(
			Entry(u"{ user first_name: \"Li\""_q));
		if (boundary == u"entry"_q) {
			fixture.parts[u"log_10_00.txt"_q].push_back(Entry(u"next entry"_q));
		}
		CheckParserRefusal(RunFixture(fixture), boundary + u" flush"_q,
			PublicationIdentityRefusal::UnterminatedProfile);
	}
	for (const auto depth : { 32, 33 }) {
		auto fixture = CleanFixture();
		const auto body = QString(u"{ other "_q).repeated(depth)
			+ QString(depth, QChar('}'));
		Inject(fixture, SecrecyClass::MtpLog, body);
		const auto result = RunFixture(fixture);
		if (depth == 32) {
			CheckCounts(result, u"supported depth 32"_q);
		} else {
			CheckParserRefusal(result, u"depth 33"_q,
				PublicationIdentityRefusal::ParserDepthLimit);
		}
	}
	for (const auto length : { 4096, 4097 }) {
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::MtpLog,
			u"{ user first_name: \""_q + QString(length, QChar('x')) + u"\" }"_q);
		const auto result = RunFixture(fixture);
		if (length == 4096) {
			CheckCounts(result, u"supported decoded string 4096"_q);
		} else {
			CheckParserRefusal(result, u"decoded string 4097"_q,
				PublicationIdentityRefusal::ParserStringLimit);
		}
	}
	for (const auto lines : { 256, 257 }) {
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::MtpLog,
			u"{ user\n"_q + QString(u" filler\n"_q).repeated(lines - 2) + u"}"_q);
		const auto result = RunFixture(fixture);
		if (lines == 256) {
			CheckCounts(result, u"supported profile lines 256"_q);
		} else {
			CheckParserRefusal(result, u"profile lines 257"_q,
				PublicationIdentityRefusal::ParserProfileLimit);
		}
	}
	{
		auto fixture = CleanFixture();
		Inject(fixture, SecrecyClass::MtpLog,
			u"{ other text: \"\\\\u000A } user_id: 7 { user\" }"_q);
		CheckCounts(RunFixture(fixture), u"escaped backslash never decoded twice"_q);
	}
}

[[nodiscard]] QByteArray Appended(qint64 mark) {
	auto file = QFile(EvidenceDir() + u"test_log.txt"_q);
	if (!file.open(QIODevice::ReadOnly) || !file.seek(mark)) {
		return {};
	}
	return file.readAll();
}

[[nodiscard]] bool FixedRows(const QStringList &rows) {
	const auto cls = u"(TestLog|AppLog|DebugLog|MtpLog|EarlierParts)"_q;
	const auto selection = u"(unspecified|complete|invalid_times|"
		"reversed_window|capped_window)"_q;
	const auto coverageRefusal = u"(unspecified_selection|invalid_times|"
		"reversed_window|capped_window|candidate_inventory_mismatch|"
		"earlier_inventory_mismatch|empty_planted_control|no_candidates|"
		"no_accepted_source|missing_source|foreign_source|unreadable_source|"
		"invalid_earlier_name|missing_planted_control|missing_named_control|"
		"missing_earlier_control|missing_send_header|missing_recv_header)"_q;
	const auto identityRefusal = u"(no_usable_identity|invalid_first_name|"
		"invalid_last_name|invalid_username|invalid_phone|invalid_user_id|"
		"parser_depth_limit|parser_string_limit|parser_profile_limit|"
		"unterminated_profile|unterminated_string|unsupported_string_escape|"
		"lost_object_scope|matcher_control_failed)"_q;
	const auto patterns = QStringList{
		u"LOG_ENUMERATION_SCAN: decided=[01] classes=[0-9]+ candidates=[0-9]+ "
			"accepted=[0-9]+ foreign=[0-9]+ missing=[0-9]+ unreadable=[0-9]+"_q,
		u"LOG_ENUMERATION_SELECTION: state="_q + selection,
		u"LOG_ENUMERATION_REFUSAL: scope=(scan|"_q + cls
			+ u") reason="_q + coverageRefusal + u" count=1"_q,
		u"LOG_ENUMERATION_CLASS: class="_q + cls
			+ u" decided=[01] candidates=[0-9]+ files=[0-9]+ foreign=[0-9]+ "
			"missing=[0-9]+ unreadable=[0-9]+ invalid_names=[0-9]+ "
			"without_control=[0-9]+ lines=[0-9]+ send_headers=[0-9]+ "
			"recv_headers=[0-9]+ planted_hits=[0-9]+ control_hits=[0-9]+"_q,
		u"PUBLICATION_IDENTITY_SCAN: decided=[01] clean=[01] "
			"findings=[0-9]+ classes=[0-9]+"_q,
		u"PUBLICATION_IDENTITY_CONTROLS: families=[0-9]+ positive=[0-9]+ "
			"negative=[0-9]+ hold=[01]"_q,
		u"PUBLICATION_IDENTITY_COVERAGE: decided=[01]"_q,
		u"PUBLICATION_IDENTITY_REFUSAL: reason="_q + identityRefusal + u" count=1"_q,
		u"PUBLICATION_IDENTITY_CLASS: class="_q + cls
			+ u" family=(profile|username|phone|user_id) count=[0-9]+"_q,
	};
	auto grammar = std::vector<QRegularExpression>();
	for (const auto &pattern : patterns) {
		grammar.emplace_back(QChar('^') + pattern + QChar('$'));
	}
	return !rows.isEmpty() && ranges::all_of(rows, [&](const auto &row) {
		return ranges::any_of(grammar, [&](const auto &pattern) {
			return pattern.match(row).hasMatch();
		});
	});
}

[[nodiscard]] PublicationIdentity OutputIdentity() {
	return {
		.firstName = u"Syntheticoutputfirst"_q,
		.lastName = u"Syntheticoutputlast"_q,
		.username = u"synthetic_output_handle_a7c39d1e"_q,
		.phone = u"+123456789012345"_q,
		.userId = u"12345678901234567890"_q,
	};
}

[[nodiscard]] bool NoPresentations(const QStringList &rows) {
	const auto output = OutputIdentity();
	const auto forbidden = QStringList{
		output.firstName,
		output.lastName,
		output.username,
		output.phone,
		output.userId,
		u"Li Syntheticriver"_q,
		u"audit_example_handle"_q,
		u"+15551234567"_q,
		kPlanted,
		kEarlierControl,
		u"malicious-source-name"_q,
		u"/synthetic/absolute/path"_q,
		u"raw-fixture-marker-5c29"_q,
	};
	return ranges::all_of(rows, [&](const auto &row) {
		return !row.contains(u"SECRECY_"_q)
			&& ranges::none_of(forbidden, [&](const auto &value) {
				return row.contains(value);
			});
	});
}

void SelfPublicationOutput() {
	for (const auto oneRow : { false, true }) {
		const auto mark = QFileInfo(EvidenceDir() + u"test_log.txt"_q).size();
		if (oneRow) {
			Note(u"Li\nSyntheticriver"_q);
		} else {
			Note(u"Li"_q);
			Note(u"Syntheticriver"_q);
		}
		const auto bytes = Appended(mark);
		const auto expected = oneRow
			? QByteArray("NOTE: Li\\u000ASyntheticriver\n")
			: QByteArray("NOTE: Li\nNOTE: Syntheticriver\n");
		Check(bytes == expected,
			u"publication self-test: actual display logger rows %1"_q.arg(oneRow));
		auto fixture = CleanFixture();
		fixture.test += QString::fromUtf8(bytes).chopped(1).split(QChar('\n'));
		CheckCounts(RunFixture(fixture), u"actual escaped display rows %1"_q
			.arg(oneRow), SecrecyClass::TestLog, { .profiles = oneRow ? 1 : 0 });
	}
	auto cleanFixture = CleanFixture();
	cleanFixture.change = [](SecrecyLaunchLogs &logs) {
		for (auto &source : logs.sources) {
			source.name = u"malicious-source-name\n} /synthetic/absolute/path"_q;
		}
	};
	Inject(cleanFixture, SecrecyClass::TestLog, u"raw-fixture-marker-5c29"_q);
	const auto clean = RunFixture(cleanFixture, OutputIdentity());
	auto dirtyFixture = cleanFixture;
	Inject(dirtyFixture, SecrecyClass::TestLog,
		u"https://t.me/"_q + OutputIdentity().username);
	const auto dirty = RunFixture(dirtyFixture, OutputIdentity());
	auto refusedFixture = cleanFixture;
	refusedFixture.planted.clear();
	const auto refused = RunFixture(refusedFixture, {});
	CheckCounts(clean, u"output clean control"_q);
	CheckCounts(dirty, u"output independent dirty control"_q,
		SecrecyClass::TestLog, { .usernames = 1 });
	for (const auto *result : { &clean, &dirty, &refused }) {
		const auto rows = LaunchLogEnumerationRows(result->enumeration)
			+ PublicationIdentityRows(result->identity);
		const auto mark = QFileInfo(EvidenceDir() + u"test_log.txt"_q).size();
		for (const auto &row : rows) {
			Note(row);
		}
		const auto bytes = Appended(mark);
		const auto expected = (u"NOTE: "_q + rows.join(u"\nNOTE: "_q)
			+ QChar('\n')).toUtf8();
		Check(result->prepared && FixedRows(rows) && NoPresentations(rows)
			&& bytes == expected && bytes.count('\n') == rows.size(),
			u"publication self-test: fixed actual row bytes"_q,
			u"bytes=%1 rows=%2 decided=%3"_q
				.arg(bytes.size()).arg(rows.size()).arg(result->identity.decided));
	}
	for (const auto identityWrapper : { false, true }) {
		const auto mark = QFileInfo(EvidenceDir() + u"test_log.txt"_q).size();
		const auto failures = FailureCount();
		const auto ok = identityWrapper
			? CheckPublicationIdentity({
				.identity = { .username = u"wrapper_absent_identity_f7c29d81"_q },
			})
			: CheckLaunchLogEnumeration();
		const auto bytes = Appended(mark);
		auto resultRows = QStringList();
		auto setupRows = 0;
		auto scanRows = 0;
		auto identityRows = 0;
		for (const auto &line : QString::fromUtf8(bytes).split(QChar('\n'))) {
			if (line.startsWith(u"NOTE: PUBLICATION_AUDIT_CONTROL: "_q)) {
				++setupRows;
			} else if (line.startsWith(u"NOTE: LOG_ENUMERATION_"_q)
				|| line.startsWith(u"NOTE: PUBLICATION_IDENTITY_"_q)) {
				resultRows.push_back(line.mid(6));
				scanRows += line.startsWith(u"NOTE: LOG_ENUMERATION_SCAN:"_q);
				identityRows += line.startsWith(u"NOTE: PUBLICATION_IDENTITY_SCAN:"_q);
			}
		}
		Check(ok && FailureCount() == failures && setupRows == 1 && scanRows == 1
			&& bytes.count('\n') == (identityWrapper ? 36 : 16)
			&& identityRows == (identityWrapper ? 1 : 0)
			&& FixedRows(resultRows) && NoPresentations(resultRows)
			&& !bytes.contains("SECRECY_")
			&& !resultRows.join(QChar('\n')).contains(
				u"publication-audit-control-"_q)
			&& !resultRows.join(QChar('\n')).contains(
				u"wrapper_absent_identity_f7c29d81"_q),
			u"publication self-test: actual healthy wrapper %1"_q.arg(identityWrapper),
			u"bytes=%1 result_rows=%2 setup_rows=%3"_q
				.arg(bytes.size()).arg(resultRows.size()).arg(setupRows));
	}
}

} // namespace

void AppendPublicationIdentityScanSelfTest(not_null<Runner*> runner) {
	const auto stages = std::vector<std::pair<QString, Fn<void()>>>{
		{ u"publication_self_coincidences"_q, SelfPublicationCoincidences },
		{ u"publication_self_presentations"_q, SelfPublicationPresentations },
		{ u"publication_self_enumeration"_q, SelfLogEnumeration },
		{ u"publication_self_selection"_q, SelfPublicationSelection },
		{ u"publication_self_separation"_q, SelfAuditSeparation },
		{ u"publication_self_output"_q, SelfPublicationOutput },
	};
	for (const auto &[name, then] : stages) {
		runner->add({
			.name = name,
			.then = then,
			.timeout = kDefaultStageTimeout,
		});
	}
}

} // namespace Test

#endif // _DEBUG
