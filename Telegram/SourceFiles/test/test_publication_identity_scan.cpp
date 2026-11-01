/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_publication_identity_scan.h"

#include "base/random.h"
#include "test/test_agent.h"
#include "test/test_log_scan_internal.h"
#include "test/test_probe.h"
#include "logs.h"

namespace Test {
namespace {

[[nodiscard]] QString NamedControl(
		const LaunchLogEnumerationArgs &args,
		SecrecyClass cls) {
	switch (cls) {
	case SecrecyClass::TestLog: return args.testLogControl;
	case SecrecyClass::AppLog: return args.appLogControl;
	case SecrecyClass::DebugLog: return args.debugLogControl;
	case SecrecyClass::MtpLog: return args.mtpLogControl;
	case SecrecyClass::EarlierParts: return {};
	}
	return {};
}

[[nodiscard]] SecrecyLaunchLogs WithNamedControls(
		const SecrecyLaunchLogs &logs,
		const LaunchLogEnumerationArgs &args) {
	auto result = logs;
	for (auto &source : result.sources) {
		const auto control = NamedControl(args, source.cls);
		if (!control.isEmpty()) {
			source.control = control;
		}
	}
	return result;
}

[[nodiscard]] LaunchLogClassEnumeration EnumerateClass(
		const SecrecyClassReading &reading,
		bool emptyPlantedControl) {
	auto result = LaunchLogClassEnumeration{
		.cls = reading.cls,
		.candidates = reading.candidates,
		.files = reading.files,
		.foreign = reading.foreign,
		.unreadable = reading.unreadable,
		.missing = reading.missing,
		.invalidNames = reading.invalidNames,
		.withoutControl = reading.withoutControl,
		.lines = reading.lines,
		.sendHeaders = reading.sendHeaders,
		.recvHeaders = reading.recvHeaders,
		.plantedHits = reading.plantedHits,
		.controlHits = reading.controlHits,
	};
	auto &refusals = result.refusals;
	if (!reading.candidates) {
		refusals.push_back(LaunchLogRefusal::NoCandidates);
	}
	if (!reading.files) {
		refusals.push_back(LaunchLogRefusal::NoAcceptedSource);
		if (reading.missing) {
			refusals.push_back(LaunchLogRefusal::MissingSource);
		}
		if (reading.foreign) {
			refusals.push_back(LaunchLogRefusal::ForeignSource);
		}
	}
	if (reading.unreadable) {
		refusals.push_back(LaunchLogRefusal::UnreadableSource);
	}
	if (reading.cls == SecrecyClass::EarlierParts) {
		if (reading.invalidNames) {
			refusals.push_back(LaunchLogRefusal::InvalidEarlierName);
		}
		if (reading.files && reading.missing) {
			refusals.push_back(LaunchLogRefusal::MissingSource);
		}
		if (reading.files && reading.foreign) {
			refusals.push_back(LaunchLogRefusal::ForeignSource);
		}
		if (reading.withoutControl) {
			refusals.push_back(LaunchLogRefusal::MissingEarlierControl);
		}
	} else {
		if (emptyPlantedControl) {
			refusals.push_back(LaunchLogRefusal::EmptyPlantedControl);
		} else if (!reading.plantedHits) {
			refusals.push_back(LaunchLogRefusal::MissingPlantedControl);
		}
		if (reading.namedControl && !reading.controlHits) {
			refusals.push_back(LaunchLogRefusal::MissingNamedControl);
		}
	}
	if (reading.cls == SecrecyClass::MtpLog || reading.mtpFiles) {
		if (!reading.sendHeaders) {
			refusals.push_back(LaunchLogRefusal::MissingSendHeader);
		}
		if (!reading.recvHeaders) {
			refusals.push_back(LaunchLogRefusal::MissingRecvHeader);
		}
	}
	result.decided = refusals.empty();
	return result;
}

[[nodiscard]] bool EarlierInventoryMatches(
		const SecrecyLaunchLogs &logs,
		const LaunchLogEnumerationArgs &args) {
	if (args.earlierParts.empty()) {
		return true;
	}
	auto index = 0;
	for (const auto &source : logs.sources) {
		if (source.cls != SecrecyClass::EarlierParts) {
			continue;
		}
		if (index == int(args.earlierParts.size())) {
			return false;
		}
		const auto &part = args.earlierParts[index++];
		if (!part.day.isValid()
			|| source.invalidName
			|| source.name != u"DebugLogs/"_q + part.name
			|| source.control != part.control) {
			return false;
		}
	}
	return index == int(args.earlierParts.size());
}

[[nodiscard]] LaunchLogEnumerationReading EnumerationFrom(
		const SecrecyLaunchLogs &logs,
		const Internal::LogScanReading &scan,
		const QString &plantedControl,
		const LaunchLogEnumerationArgs &args) {
	auto result = LaunchLogEnumerationReading{
		.selection = logs.selection,
		.candidates = scan.candidates,
		.accepted = scan.accepted,
		.foreign = scan.foreign,
		.missing = scan.missing,
		.unreadable = scan.unreadable,
	};
	for (const auto &reading : scan.classes) {
		result.classes.push_back(EnumerateClass(reading, plantedControl.isEmpty()));
	}
	if (!args.earlierParts.empty() && !result.find(SecrecyClass::EarlierParts)) {
		result.classes.push_back(EnumerateClass(
			SecrecyClassReading{ .cls = SecrecyClass::EarlierParts },
			plantedControl.isEmpty()));
	}
	auto &refusals = result.refusals;
	switch (logs.selection) {
	case LaunchLogSelection::Unspecified:
		refusals.push_back(LaunchLogRefusal::UnspecifiedSelection);
		break;
	case LaunchLogSelection::Complete: break;
	case LaunchLogSelection::InvalidTimes:
		refusals.push_back(LaunchLogRefusal::InvalidTimes);
		break;
	case LaunchLogSelection::ReversedWindow:
		refusals.push_back(LaunchLogRefusal::ReversedWindow);
		break;
	case LaunchLogSelection::CappedWindow:
		refusals.push_back(LaunchLogRefusal::CappedWindow);
		break;
	}
	const auto test = result.find(SecrecyClass::TestLog);
	const auto app = result.find(SecrecyClass::AppLog);
	const auto debug = result.find(SecrecyClass::DebugLog);
	const auto mtp = result.find(SecrecyClass::MtpLog);
	if (!test || !app || !debug || !mtp
		|| test->candidates != 1
		|| app->candidates != 1
		|| debug->candidates != mtp->candidates
		|| logs.candidates != scan.candidates
		|| scan.candidates != debug->candidates + mtp->candidates
		|| !scan.candidates) {
		refusals.push_back(LaunchLogRefusal::CandidateInventoryMismatch);
	}
	if (!EarlierInventoryMatches(logs, args)) {
		refusals.push_back(LaunchLogRefusal::EarlierInventoryMismatch);
	}
	if (plantedControl.isEmpty()) {
		refusals.push_back(LaunchLogRefusal::EmptyPlantedControl);
	}
	result.decided = refusals.empty()
		&& ranges::all_of(result.classes, [](const auto &reading) {
			return reading.decided;
		});
	return result;
}

constexpr auto kNameLimit = 256;
constexpr auto kUsernameLimit = 64;
constexpr auto kIdLimit = 20;
constexpr auto kObjectLimit = 32;
constexpr auto kStringLimit = 4096;
constexpr auto kProfileLineLimit = 256;

void Refuse(
		std::vector<PublicationIdentityRefusal> &refusals,
		PublicationIdentityRefusal refusal) {
	if (!ranges::contains(refusals, refusal)) {
		refusals.push_back(refusal);
	}
}

[[nodiscard]] QString NormalizeName(const QString &text) {
	return text.normalized(QString::NormalizationForm_C)
		.toCaseFolded().simplified();
}

[[nodiscard]] QString FoldAscii(QString text) {
	for (auto &ch : text) {
		if (ch >= QChar('A') && ch <= QChar('Z')) {
			ch = QChar(ch.unicode() + 'a' - 'A');
		}
	}
	return text;
}

[[nodiscard]] bool AsciiDigit(QChar ch) {
	return ch >= QChar('0') && ch <= QChar('9');
}

[[nodiscard]] bool AsciiIdentifier(QChar ch) {
	return (ch >= QChar('a') && ch <= QChar('z'))
		|| (ch >= QChar('A') && ch <= QChar('Z'))
		|| AsciiDigit(ch)
		|| ch == QChar('_');
}

[[nodiscard]] bool IdentityBoundary(const QString &text, int at) {
	if (at < 0 || at >= text.size()) {
		return true;
	}
	auto code = uint(text[at].unicode());
	if (text[at].isHighSurrogate()
		&& at + 1 < text.size()
		&& text[at + 1].isLowSurrogate()) {
		code = QChar::surrogateToUcs4(text[at], text[at + 1]);
	} else if (text[at].isLowSurrogate()
		&& at > 0
		&& text[at - 1].isHighSurrogate()) {
		code = QChar::surrogateToUcs4(text[at - 1], text[at]);
	}
	const auto category = QChar::category(code);
	return code != '_'
		&& category != QChar::Letter_Uppercase
		&& category != QChar::Letter_Lowercase
		&& category != QChar::Letter_Titlecase
		&& category != QChar::Letter_Modifier
		&& category != QChar::Letter_Other
		&& category != QChar::Mark_NonSpacing
		&& category != QChar::Mark_SpacingCombining
		&& category != QChar::Mark_Enclosing
		&& category != QChar::Number_DecimalDigit
		&& category != QChar::Number_Letter
		&& category != QChar::Number_Other;
}

[[nodiscard]] int BoundedOccurrences(
		const QString &text,
		const QString &needle) {
	if (needle.isEmpty()) {
		return 0;
	}
	auto result = 0;
	for (auto at = 0; (at = text.indexOf(needle, at)) >= 0; ++at) {
		if (IdentityBoundary(text, at - 1)
			&& IdentityBoundary(text, at + needle.size())) {
			++result;
		}
	}
	return result;
}

struct PreparedIdentity {
	QString first;
	QString last;
	QString display;
	QString username;
	QString phone;
	QString id;
	bool multipart = false;
	bool profile = false;
};

[[nodiscard]] QString PrepareName(
		const QString &value,
		PublicationIdentityRefusal refusal,
		std::vector<PublicationIdentityRefusal> &refusals) {
	if (value.isEmpty()) {
		return {};
	}
	auto valid = value.size() <= kNameLimit;
	for (auto i = 0; i < value.size(); ++i) {
		const auto ch = value[i];
		if (ch.isHighSurrogate()) {
			if (i + 1 < value.size() && value[i + 1].isLowSurrogate()) {
				++i;
			} else {
				valid = false;
			}
		} else if (ch.isLowSurrogate()
			|| (ch.category() == QChar::Other_Control && !ch.isSpace())) {
			valid = false;
		}
	}
	const auto normalized = NormalizeName(value);
	if (!valid || normalized.isEmpty()) {
		Refuse(refusals, refusal);
		return {};
	}
	return normalized;
}

[[nodiscard]] PreparedIdentity PrepareIdentity(
		const PublicationIdentity &identity,
		std::vector<PublicationIdentityRefusal> &refusals) {
	auto result = PreparedIdentity{
		.first = PrepareName(
			identity.firstName,
			PublicationIdentityRefusal::InvalidFirstName,
			refusals),
		.last = PrepareName(
			identity.lastName,
			PublicationIdentityRefusal::InvalidLastName,
			refusals),
	};
	if (!identity.username.isEmpty()) {
		const auto username = FoldAscii(identity.username);
		const auto valid = username.size() <= kUsernameLimit
			&& ranges::all_of(username, AsciiIdentifier)
			&& ranges::any_of(username, [](QChar ch) {
				return ch >= QChar('a') && ch <= QChar('z');
			});
		if (valid) {
			result.username = username;
		} else {
			Refuse(refusals, PublicationIdentityRefusal::InvalidUsername);
		}
	}
	if (!identity.phone.isEmpty()) {
		const auto digits = identity.phone.mid(1);
		if (identity.phone.front() == QChar('+')
			&& !digits.isEmpty()
			&& digits.size() <= 15
			&& digits.front() != QChar('0')
			&& ranges::all_of(digits, AsciiDigit)) {
			result.phone = digits;
		} else {
			Refuse(refusals, PublicationIdentityRefusal::InvalidPhone);
		}
	}
	if (!identity.userId.isEmpty()) {
		if (identity.userId.size() <= kIdLimit
			&& identity.userId.front() != QChar('0')
			&& ranges::all_of(identity.userId, AsciiDigit)) {
			result.id = identity.userId;
		} else {
			Refuse(refusals, PublicationIdentityRefusal::InvalidUserId);
		}
	}
	result.display = (result.first + QChar(' ') + result.last).simplified();
	result.multipart = result.display.contains(QChar(' '));
	result.profile = !result.display.isEmpty()
		&& (result.multipart
			|| !result.username.isEmpty()
			|| !result.phone.isEmpty()
			|| !result.id.isEmpty());
	if (!result.profile
		&& result.username.isEmpty()
		&& result.phone.isEmpty()
		&& result.id.isEmpty()) {
		Refuse(refusals, PublicationIdentityRefusal::NoUsableIdentity);
	}
	return result;
}

[[nodiscard]] bool PhoneSeparator(QChar ch) {
	return ch == QChar(' ')
		|| ch == QChar('(')
		|| ch == QChar(')')
		|| ch == QChar('-');
}

[[nodiscard]] QString PhoneDigits(const QString &text) {
	auto result = QString();
	auto parentheses = 0;
	for (auto i = 0; i < text.size(); ++i) {
		const auto ch = text[i];
		if (AsciiDigit(ch)) {
			if (result.size() == 15) {
				return {};
			}
			result += ch;
		} else if (ch == QChar('(')) {
			if (++parentheses > 1) {
				return {};
			}
		} else if (ch == QChar(')')) {
			if (--parentheses < 0) {
				return {};
			}
		} else if (ch == QChar('+') && i == 0) {
		} else if (!PhoneSeparator(ch)) {
			return {};
		}
	}
	return parentheses ? QString() : result;
}

[[nodiscard]] int PhoneOccurrences(
		const QString &text,
		const QString &phone) {
	if (phone.isEmpty()) {
		return 0;
	}
	auto result = 0;
	for (auto i = 0; i < text.size(); ++i) {
		if (text[i] != QChar('+') || !IdentityBoundary(text, i - 1)) {
			continue;
		}
		auto end = i + 1;
		auto parentheses = 0;
		while (end < text.size()
			&& (AsciiDigit(text[end]) || PhoneSeparator(text[end]))) {
			if (text[end] == QChar('(')) {
				auto next = end + 1;
				while (next < text.size()
					&& (text[next] == QChar(' ') || text[next] == QChar('-'))) {
					++next;
				}
				if (next == text.size()
					|| (!AsciiDigit(text[next]) && !PhoneSeparator(text[next]))) {
					break;
				}
				++parentheses;
			} else if (text[end] == QChar(')')) {
				if (!parentheses) {
					break;
				}
				--parentheses;
			}
			++end;
		}
		auto candidateEnd = end;
		while (candidateEnd > i + 1 && text[candidateEnd - 1] == QChar(' ')) {
			--candidateEnd;
		}
		if (IdentityBoundary(text, candidateEnd)
			&& PhoneDigits(text.mid(i, candidateEnd - i)) == phone) {
			++result;
		}
		i = end - 1;
	}
	return result;
}

[[nodiscard]] int VisibleSeparator(const QString &text, int at) {
	if (at + 5 >= text.size()
		|| text[at] != QChar('\\')
		|| text[at + 1] != QChar('u')) {
		return 0;
	}
	auto valid = false;
	const auto code = text.mid(at + 2, 4).toUShort(&valid, 16);
	if (!valid) {
		return 0;
	}
	switch (code) {
	case 0x000A: case 0x000B: case 0x000C: case 0x000D:
	case 0x001C: case 0x001D: case 0x001E:
	case 0x0085: case 0x2028: case 0x2029: return code;
	}
	return 0;
}

enum class ProfileObject {
	Other,
	User,
	UserEmpty,
	UserFull,
	PeerUser,
	InputPeerUser,
	InputUser,
};

[[nodiscard]] ProfileObject ObjectType(const QString &name) {
	if (name == u"user"_q) return ProfileObject::User;
	if (name == u"userEmpty"_q) return ProfileObject::UserEmpty;
	if (name == u"userFull"_q) return ProfileObject::UserFull;
	if (name == u"peerUser"_q) return ProfileObject::PeerUser;
	if (name == u"inputPeerUser"_q) return ProfileObject::InputPeerUser;
	if (name == u"inputUser"_q) return ProfileObject::InputUser;
	return ProfileObject::Other;
}

struct ProfileFrame {
	ProfileObject type = ProfileObject::Other;
	bool first = false;
	bool last = false;
	bool identifying = false;
	int lines = 0;
};

class IdentityMatcher final {
public:
	IdentityMatcher(
		const PreparedIdentity &identity,
		PublicationIdentityReading &reading);

	void beginSource(SecrecyClass cls);
	void line(const QString &text, bool entryStart);
	void finish();

private:
	void logicalLine(const QString &text);
	void plain(const QString &text, bool profile = true);
	void stringValue(const QString &field, const QString &value);
	void numberValue(const QString &field, const QString &value);
	void closeObject();
	void refuse(PublicationIdentityRefusal refusal);
	[[nodiscard]] bool relevantString(const QString &field) const;
	[[nodiscard]] bool idField(const QString &field) const;

	const PreparedIdentity &_identity;
	PublicationIdentityReading &_reading;
	PublicationIdentityClassReading *_current = nullptr;
	SecrecyClass _class = SecrecyClass::TestLog;
	std::vector<ProfileFrame> _stack;
	// Untyped openers read while no object was open, as in webrtc's device
	// lists `list: { "<name>" <id> }`: only a closer read while still no
	// object is open ends one, and opening a named object forfeits them.
	int _nonObjectBraces = 0;
	bool _lost = false;

};

IdentityMatcher::IdentityMatcher(
		const PreparedIdentity &identity,
		PublicationIdentityReading &reading)
: _identity(identity)
, _reading(reading) {
}

void IdentityMatcher::refuse(PublicationIdentityRefusal refusal) {
	Refuse(_reading.refusals, refusal);
}

void IdentityMatcher::finish() {
	if (!_stack.empty()) {
		const auto profile = ranges::any_of(_stack, [](const auto &frame) {
			return frame.type == ProfileObject::User;
		});
		refuse(profile
			? PublicationIdentityRefusal::UnterminatedProfile
			: PublicationIdentityRefusal::LostObjectScope);
	}
	_stack.clear();
	_nonObjectBraces = 0;
	_lost = false;
}

void IdentityMatcher::beginSource(SecrecyClass cls) {
	finish();
	_class = cls;
	_current = nullptr;
	for (auto &reading : _reading.classes) {
		if (reading.cls == cls) {
			_current = &reading;
			break;
		}
	}
	Expects(_current != nullptr);
}

void IdentityMatcher::line(const QString &text, bool entryStart) {
	if (entryStart || _class == SecrecyClass::TestLog) {
		finish();
	}
	auto from = 0;
	auto plainFrom = 0;
	auto profileText = QString();
	const auto matchProfile = [&] {
		if (_identity.multipart) {
			_current->profiles += BoundedOccurrences(
				NormalizeName(profileText),
				_identity.display);
		}
		profileText.clear();
	};
	auto quoted = false;
	for (auto i = 0; i < text.size(); ++i) {
		if (text[i] == QChar('\\')) {
			if (!quoted && VisibleSeparator(text, i)) {
				profileText += text.mid(plainFrom, i - plainFrom) + QChar(' ');
				logicalLine(text.mid(from, i - from));
				i += 5;
				from = i + 1;
				plainFrom = from;
			} else if (i + 1 < text.size()) {
				++i;
			}
		} else if (text[i] == QChar('"')) {
			if (quoted) {
				plainFrom = i + 1;
			} else {
				profileText += text.mid(plainFrom, i - plainFrom);
				matchProfile();
			}
			quoted = !quoted;
		}
	}
	if (!quoted) {
		profileText += text.mid(plainFrom);
	}
	matchProfile();
	logicalLine(text.mid(from));
}

void IdentityMatcher::plain(const QString &text, bool profile) {
	if (_identity.multipart && profile) {
		_current->profiles += BoundedOccurrences(
			NormalizeName(text),
			_identity.display);
	}
	_current->usernames += BoundedOccurrences(FoldAscii(text), _identity.username);
	_current->phones += PhoneOccurrences(text, _identity.phone);
}

bool IdentityMatcher::idField(const QString &field) const {
	if (_stack.empty()) {
		return field == u"user_id"_q;
	}
	switch (_stack.back().type) {
	case ProfileObject::User:
	case ProfileObject::UserEmpty:
	case ProfileObject::UserFull: return field == u"id"_q;
	case ProfileObject::PeerUser:
	case ProfileObject::InputPeerUser:
	case ProfileObject::InputUser: return field == u"user_id"_q;
	case ProfileObject::Other: return false;
	}
	return false;
}

bool IdentityMatcher::relevantString(const QString &field) const {
	return _identity.multipart
		|| !_identity.username.isEmpty()
		|| !_identity.phone.isEmpty()
		|| (!_stack.empty() && _stack.back().type != ProfileObject::Other)
		|| (field == u"phone"_q && !_identity.phone.isEmpty())
		|| (idField(field) && !_identity.id.isEmpty());
}

void IdentityMatcher::numberValue(
		const QString &field,
		const QString &value) {
	if (!_identity.id.isEmpty() && idField(field) && value == _identity.id) {
		++_current->userIds;
		if (!_stack.empty() && _stack.back().type == ProfileObject::User) {
			_stack.back().identifying = true;
		}
	}
	if (field == u"phone"_q
		&& (_stack.empty() || _stack.back().type == ProfileObject::User)
		&& !_identity.phone.isEmpty()
		&& value == _identity.phone) {
		++_current->phones;
		if (!_stack.empty()) {
			_stack.back().identifying = true;
		}
	}
}

void IdentityMatcher::stringValue(
		const QString &field,
		const QString &value) {
	const auto user = !_stack.empty()
		&& _stack.back().type == ProfileObject::User;
	const auto name = user
		&& (field == u"first_name"_q || field == u"last_name"_q);
	const auto normalized = name ? NormalizeName(value) : QString();
	const auto component = name && normalized == (field == u"first_name"_q
		? _identity.first : _identity.last);
	plain(value, !component);
	if (user) {
		auto &frame = _stack.back();
		if (field == u"first_name"_q) {
			frame.first = !_identity.first.isEmpty()
				&& normalized == _identity.first;
		} else if (field == u"last_name"_q) {
			frame.last = !_identity.last.isEmpty()
				&& normalized == _identity.last;
		} else if (field == u"username"_q
			&& !_identity.username.isEmpty()
			&& FoldAscii(value) == _identity.username) {
			frame.identifying = true;
		}
	}
	if (field == u"phone"_q
		&& (_stack.empty() || user)
		&& !_identity.phone.isEmpty()
		&& PhoneDigits(value) == _identity.phone) {
		if (!value.startsWith(QChar('+'))) {
			++_current->phones;
		}
		if (user) {
			_stack.back().identifying = true;
		}
	}
}

void IdentityMatcher::closeObject() {
	if (_stack.empty()) {
		refuse(PublicationIdentityRefusal::LostObjectScope);
		return;
	}
	const auto &frame = _stack.back();
	if (frame.type == ProfileObject::User
		&& _identity.profile
		&& (_identity.first.isEmpty() || frame.first)
		&& (_identity.last.isEmpty() || frame.last)
		&& (_identity.multipart || frame.identifying)) {
		++_current->profiles;
	}
	_stack.pop_back();
}

void IdentityMatcher::logicalLine(const QString &text) {
	if (_lost) {
		return;
	}
	for (auto &frame : _stack) {
		if (frame.type == ProfileObject::User
			&& ++frame.lines > kProfileLineLimit) {
			refuse(PublicationIdentityRefusal::ParserProfileLimit);
			_lost = true;
			return;
		}
	}
	auto field = QString();
	auto plainFrom = 0;
	for (auto i = 0; i < text.size();) {
		const auto ch = text[i];
		if (ch == QChar('"')) {
			plain(text.mid(plainFrom, i - plainFrom), false);
			const auto relevant = relevantString(field);
			auto value = QString();
			auto closed = false;
			auto tooLong = false;
			for (++i; i < text.size(); ++i) {
				auto decoded = text[i];
				if (decoded == QChar('"')) {
					closed = true;
					++i;
					break;
				}
				if (decoded == QChar('\\')) {
					const auto separator = VisibleSeparator(text, i);
					if (separator) {
						decoded = QChar(separator);
						i += 5;
					} else if (++i == text.size()) {
						break;
					} else if (text[i] == QChar('n')) {
						decoded = QChar('\n');
					} else if (text[i] == QChar('\\') || text[i] == QChar('"')) {
						decoded = text[i];
					} else {
						if (relevant) {
							refuse(PublicationIdentityRefusal::UnsupportedStringEscape);
						}
						decoded = text[i];
					}
				}
				if (value.size() < kStringLimit) {
					value += decoded;
				} else {
					tooLong = true;
				}
			}
			if (relevant && tooLong) {
				refuse(PublicationIdentityRefusal::ParserStringLimit);
			}
			if (!closed && relevant) {
				refuse(PublicationIdentityRefusal::UnterminatedString);
			}
			if (closed && !tooLong) {
				stringValue(field, value);
			}
			field.clear();
			plainFrom = i;
		} else if (ch == QChar('{')) {
			auto start = i + 1;
			while (start < text.size() && text[start].isSpace()) {
				++start;
			}
			auto end = start;
			while (end < text.size() && AsciiIdentifier(text[end])) {
				++end;
			}
			if (end == start) {
				if (_stack.empty()) {
					++_nonObjectBraces;
				}
				++i;
				continue;
			}
			_nonObjectBraces = 0;
			if (_stack.size() == kObjectLimit) {
				refuse(PublicationIdentityRefusal::ParserDepthLimit);
				_lost = true;
				return;
			}
			_stack.push_back({
				.type = ObjectType(text.mid(start, std::min(end - start, 64))),
				.lines = 1,
			});
			field.clear();
			i = end;
		} else if (ch == QChar('}')) {
			if (_stack.empty() && _nonObjectBraces > 0) {
				--_nonObjectBraces;
			} else {
				closeObject();
			}
			field.clear();
			++i;
		} else if (AsciiIdentifier(ch) && !AsciiDigit(ch)) {
			const auto start = i++;
			while (i < text.size() && AsciiIdentifier(text[i])) {
				++i;
			}
			auto colon = i;
			while (colon < text.size() && text[colon].isSpace()) {
				++colon;
			}
			if (colon < text.size()
				&& (text[colon] == QChar(':') || text[colon] == QChar('='))
				&& IdentityBoundary(text, start - 1)) {
				field = text.mid(start, std::min(i - start, 64));
				i = colon + 1;
				while (i < text.size() && text[i].isSpace()) {
					++i;
				}
				const auto userString = !_stack.empty()
					&& _stack.back().type == ProfileObject::User
					&& (field == u"first_name"_q
						|| field == u"last_name"_q
						|| field == u"username"_q
						|| field == u"phone"_q);
				if (userString
					&& (i == text.size()
						|| (text[i] != QChar('"')
							&& !(field == u"phone"_q && AsciiDigit(text[i]))))) {
					refuse(PublicationIdentityRefusal::LostObjectScope);
				}
				const auto number = i;
				while (i < text.size() && AsciiDigit(text[i])) {
					++i;
				}
				if (i > number) {
					const auto delimiter = i == text.size()
						|| text[i].isSpace()
						|| text[i] == QChar('}')
						|| text[i] == QChar(',')
						|| text[i] == QChar(';')
						|| text[i] == QChar(')')
						|| text[i] == QChar(']')
						|| text[i] == QChar('[');
					if (delimiter && i - number <= kIdLimit) {
						numberValue(field, text.mid(number, i - number));
					}
					field.clear();
				}
			}
		} else {
			++i;
		}
	}
	plain(text.mid(plainFrom), false);
}

[[nodiscard]] QString Quoted(QString value) {
	return QChar('"') + value.replace(QChar('\\'), u"\\\\"_q)
		.replace(QChar('"'), u"\\\""_q)
		.replace(QChar('\n'), u"\\n"_q) + QChar('"');
}

[[nodiscard]] int FamilyCount(
		const PublicationIdentityClassReading &reading,
		PublicationIdentityFamily family) {
	switch (family) {
	case PublicationIdentityFamily::Profile: return reading.profiles;
	case PublicationIdentityFamily::Username: return reading.usernames;
	case PublicationIdentityFamily::Phone: return reading.phones;
	case PublicationIdentityFamily::UserId: return reading.userIds;
	}
	return 0;
}

void MatcherControls(
		const PreparedIdentity &identity,
		PublicationIdentityReading &reading) {
	const auto profile = [&] {
		if (identity.multipart) {
			return Quoted(identity.display);
		}
		auto result = u"{ user first_name: "_q + Quoted(identity.first)
			+ u" last_name: "_q + Quoted(identity.last);
		if (!identity.id.isEmpty()) {
			result += u" id: "_q + identity.id;
		} else if (!identity.username.isEmpty()) {
			result += u" username: "_q + Quoted(identity.username);
		} else {
			result += u" phone: "_q + Quoted(identity.phone);
		}
		return result + u" }"_q;
	};
	const auto controls = std::vector<std::tuple<PublicationIdentityFamily,
			bool, QString, QString>>{
		{ PublicationIdentityFamily::Profile, identity.profile,
			profile(), Quoted(QChar('_') + identity.display + QChar('_')) },
		{ PublicationIdentityFamily::Username, !identity.username.isEmpty(),
			identity.username, u"x"_q + identity.username + u"_suffix"_q },
		{ PublicationIdentityFamily::Phone, !identity.phone.isEmpty(),
			QChar('+') + identity.phone, u"suffix="_q + identity.phone.mid(1) },
		{ PublicationIdentityFamily::UserId, !identity.id.isEmpty(),
			u"user_id="_q + identity.id, u"counter="_q + identity.id },
	};
	for (const auto &[family, usable, positive, negative] : controls) {
		if (!usable) {
			continue;
		}
		++reading.controlFamilies;
		const auto check = [&](const QString &text, int expected) {
			auto control = PublicationIdentityReading();
			control.classes.push_back({ .cls = SecrecyClass::TestLog });
			auto matcher = IdentityMatcher(identity, control);
			matcher.beginSource(SecrecyClass::TestLog);
			matcher.line(text, true);
			matcher.finish();
			return control.refusals.empty()
				&& FamilyCount(control.classes.front(), family) == expected;
		};
		if (check(positive, 1)) {
			++reading.positiveControls;
		}
		if (check(negative, 0)) {
			++reading.negativeControls;
		}
	}
	reading.controlsHold = reading.controlFamilies > 0
		&& reading.positiveControls == reading.controlFamilies
		&& reading.negativeControls == reading.controlFamilies;
	if (!reading.controlsHold) {
		Refuse(reading.refusals, PublicationIdentityRefusal::MatcherControlFailed);
	}
}

[[nodiscard]] QString SelectionName(LaunchLogSelection selection) {
	switch (selection) {
	case LaunchLogSelection::Unspecified: return u"unspecified"_q;
	case LaunchLogSelection::Complete: return u"complete"_q;
	case LaunchLogSelection::InvalidTimes: return u"invalid_times"_q;
	case LaunchLogSelection::ReversedWindow: return u"reversed_window"_q;
	case LaunchLogSelection::CappedWindow: return u"capped_window"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QString RefusalName(LaunchLogRefusal refusal) {
	switch (refusal) {
	case LaunchLogRefusal::UnspecifiedSelection: return u"unspecified_selection"_q;
	case LaunchLogRefusal::InvalidTimes: return u"invalid_times"_q;
	case LaunchLogRefusal::ReversedWindow: return u"reversed_window"_q;
	case LaunchLogRefusal::CappedWindow: return u"capped_window"_q;
	case LaunchLogRefusal::CandidateInventoryMismatch:
		return u"candidate_inventory_mismatch"_q;
	case LaunchLogRefusal::EarlierInventoryMismatch:
		return u"earlier_inventory_mismatch"_q;
	case LaunchLogRefusal::EmptyPlantedControl: return u"empty_planted_control"_q;
	case LaunchLogRefusal::NoCandidates: return u"no_candidates"_q;
	case LaunchLogRefusal::NoAcceptedSource: return u"no_accepted_source"_q;
	case LaunchLogRefusal::MissingSource: return u"missing_source"_q;
	case LaunchLogRefusal::ForeignSource: return u"foreign_source"_q;
	case LaunchLogRefusal::UnreadableSource: return u"unreadable_source"_q;
	case LaunchLogRefusal::InvalidEarlierName: return u"invalid_earlier_name"_q;
	case LaunchLogRefusal::MissingPlantedControl: return u"missing_planted_control"_q;
	case LaunchLogRefusal::MissingNamedControl: return u"missing_named_control"_q;
	case LaunchLogRefusal::MissingEarlierControl: return u"missing_earlier_control"_q;
	case LaunchLogRefusal::MissingSendHeader: return u"missing_send_header"_q;
	case LaunchLogRefusal::MissingRecvHeader: return u"missing_recv_header"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QString RefusalName(PublicationIdentityRefusal refusal) {
	switch (refusal) {
	case PublicationIdentityRefusal::NoUsableIdentity: return u"no_usable_identity"_q;
	case PublicationIdentityRefusal::InvalidFirstName: return u"invalid_first_name"_q;
	case PublicationIdentityRefusal::InvalidLastName: return u"invalid_last_name"_q;
	case PublicationIdentityRefusal::InvalidUsername: return u"invalid_username"_q;
	case PublicationIdentityRefusal::InvalidPhone: return u"invalid_phone"_q;
	case PublicationIdentityRefusal::InvalidUserId: return u"invalid_user_id"_q;
	case PublicationIdentityRefusal::ParserDepthLimit: return u"parser_depth_limit"_q;
	case PublicationIdentityRefusal::ParserStringLimit: return u"parser_string_limit"_q;
	case PublicationIdentityRefusal::ParserProfileLimit: return u"parser_profile_limit"_q;
	case PublicationIdentityRefusal::UnterminatedProfile: return u"unterminated_profile"_q;
	case PublicationIdentityRefusal::UnterminatedString: return u"unterminated_string"_q;
	case PublicationIdentityRefusal::UnsupportedStringEscape:
		return u"unsupported_string_escape"_q;
	case PublicationIdentityRefusal::LostObjectScope: return u"lost_object_scope"_q;
	case PublicationIdentityRefusal::MatcherControlFailed: return u"matcher_control_failed"_q;
	}
	return u"unknown"_q;
}

struct PreparedCampaign {
	QString nonce;
	SecrecyLaunchLogs logs;
};

[[nodiscard]] PreparedCampaign PrepareCampaign(
		const LaunchLogEnumerationArgs &args) {
	const auto nonce = u"publication-audit-control-"_q
		+ QString::number(base::RandomValue<uint64>(), 16).rightJustified(
			16,
			QChar('0'));
	Note(u"PUBLICATION_AUDIT_CONTROL: "_q + nonce);
	LOG(("Test Info: publication audit control %1").arg(nonce));
	MTP_LOG(0, ("Test Info: publication audit control %1").arg(nonce));
	return {
		.nonce = nonce,
		.logs = WithNamedControls(SelectThisLaunchLogs(args.earlierParts), args),
	};
}

void ReportEnumerationClass(
		const LaunchLogClassEnumeration &reading,
		const PublicationIdentityClassReading *identity = nullptr) {
	auto scan = DiscriminatingScan(
		(identity ? u"PUBLICATION_IDENTITY: "_q : u"LOG_ENUMERATION: "_q)
			+ SecrecyClassName(reading.cls),
		identity ? u"identity presentations"_q : u"refused classes"_q,
		u"accepted source control"_q);
	scan.examined(reading.lines);
	const auto subjects = identity
		? (identity->profiles + identity->usernames
			+ identity->phones + identity->userIds)
		: (reading.decided ? 0 : 1);
	for (auto i = 0; i < subjects; ++i) {
		scan.matchedSubject();
	}
	for (auto i = 0; i < reading.plantedHits + reading.controlHits; ++i) {
		scan.matchedControl();
	}
	scan.report();
}

} // namespace

const LaunchLogClassEnumeration *LaunchLogEnumerationReading::find(
		SecrecyClass cls) const {
	for (const auto &reading : classes) {
		if (reading.cls == cls) {
			return &reading;
		}
	}
	return nullptr;
}

LaunchLogEnumerationReading ReadLaunchLogEnumeration(
		const SecrecyLaunchLogs &logs,
		const QString &plantedControl,
		const LaunchLogEnumerationArgs &args) {
	const auto selected = WithNamedControls(logs, args);
	const auto scan = Internal::ReadLogClasses(selected, plantedControl);
	return EnumerationFrom(selected, scan, plantedControl, args);
}

const PublicationIdentityClassReading *PublicationIdentityReading::find(
		SecrecyClass cls) const {
	for (const auto &reading : classes) {
		if (reading.cls == cls) {
			return &reading;
		}
	}
	return nullptr;
}

int PublicationIdentityReading::findings() const {
	auto result = 0;
	for (const auto &reading : classes) {
		result += reading.profiles + reading.usernames
			+ reading.phones + reading.userIds;
	}
	return result;
}

PublicationIdentityReading ReadPublicationIdentity(
		const PublicationIdentity &identity,
		const SecrecyLaunchLogs &logs,
		const QString &plantedControl,
		const LaunchLogEnumerationArgs &args) {
	auto result = PublicationIdentityReading();
	for (const auto cls : { SecrecyClass::TestLog, SecrecyClass::AppLog,
			SecrecyClass::DebugLog, SecrecyClass::MtpLog }) {
		result.classes.push_back({ .cls = cls });
	}
	if (!args.earlierParts.empty()
		|| ranges::any_of(logs.sources, [](const auto &source) {
			return source.cls == SecrecyClass::EarlierParts;
		})) {
		result.classes.push_back({ .cls = SecrecyClass::EarlierParts });
	}
	const auto prepared = PrepareIdentity(identity, result.refusals);
	MatcherControls(prepared, result);
	auto matcher = IdentityMatcher(prepared, result);
	auto observer = Internal::LogScanObserver{
		.beginSource = [&](SecrecyClass cls) { matcher.beginSource(cls); },
		.line = [&](const QString &line, bool entryStart) {
			matcher.line(line, entryStart);
		},
	};
	const auto selected = WithNamedControls(logs, args);
	const auto scan = Internal::ReadLogClasses(
		selected,
		plantedControl,
		nullptr,
		{},
		&observer);
	matcher.finish();
	result.enumeration = EnumerationFrom(selected, scan, plantedControl, args);
	result.decided = result.enumeration.decided
		&& result.refusals.empty()
		&& result.controlsHold;
	result.clean = result.decided && !result.findings();
	return result;
}

QStringList LaunchLogEnumerationRows(
		const LaunchLogEnumerationReading &reading) {
	auto result = QStringList{
		u"LOG_ENUMERATION_SCAN: decided=%1 classes=%2 candidates=%3 "
			"accepted=%4 foreign=%5 missing=%6 unreadable=%7"_q
			.arg(int(reading.decided))
			.arg(reading.classes.size())
			.arg(reading.candidates)
			.arg(reading.accepted)
			.arg(reading.foreign)
			.arg(reading.missing)
			.arg(reading.unreadable),
		u"LOG_ENUMERATION_SELECTION: state=%1"_q.arg(SelectionName(reading.selection)),
	};
	for (const auto refusal : reading.refusals) {
		result.push_back(u"LOG_ENUMERATION_REFUSAL: scope=scan reason=%1 count=1"_q
			.arg(RefusalName(refusal)));
	}
	for (const auto &item : reading.classes) {
		result.push_back(u"LOG_ENUMERATION_CLASS: class=%1 decided=%2 candidates=%3 "
			"files=%4 foreign=%5 missing=%6 unreadable=%7 invalid_names=%8 "
			"without_control=%9 lines=%10 send_headers=%11 recv_headers=%12 "
			"planted_hits=%13 control_hits=%14"_q
			.arg(SecrecyClassName(item.cls))
			.arg(int(item.decided))
			.arg(item.candidates)
			.arg(item.files)
			.arg(item.foreign)
			.arg(item.missing)
			.arg(item.unreadable)
			.arg(item.invalidNames)
			.arg(item.withoutControl)
			.arg(item.lines)
			.arg(item.sendHeaders)
			.arg(item.recvHeaders)
			.arg(item.plantedHits)
			.arg(item.controlHits));
		for (const auto refusal : item.refusals) {
			result.push_back(u"LOG_ENUMERATION_REFUSAL: scope=%1 reason=%2 count=1"_q
				.arg(SecrecyClassName(item.cls))
				.arg(RefusalName(refusal)));
		}
	}
	return result;
}

QStringList PublicationIdentityRows(const PublicationIdentityReading &reading) {
	auto result = QStringList{
		u"PUBLICATION_IDENTITY_SCAN: decided=%1 clean=%2 findings=%3 classes=%4"_q
			.arg(int(reading.decided))
			.arg(int(reading.clean))
			.arg(reading.findings())
			.arg(reading.classes.size()),
		u"PUBLICATION_IDENTITY_CONTROLS: families=%1 positive=%2 negative=%3 hold=%4"_q
			.arg(reading.controlFamilies)
			.arg(reading.positiveControls)
			.arg(reading.negativeControls)
			.arg(int(reading.controlsHold)),
		u"PUBLICATION_IDENTITY_COVERAGE: decided=%1"_q
			.arg(int(reading.enumeration.decided)),
	};
	for (const auto refusal : reading.refusals) {
		result.push_back(u"PUBLICATION_IDENTITY_REFUSAL: reason=%1 count=1"_q
			.arg(RefusalName(refusal)));
	}
	for (const auto &item : reading.classes) {
		for (const auto family : { PublicationIdentityFamily::Profile,
				PublicationIdentityFamily::Username, PublicationIdentityFamily::Phone,
				PublicationIdentityFamily::UserId }) {
			const auto name = [&] {
				switch (family) {
				case PublicationIdentityFamily::Profile: return u"profile"_q;
				case PublicationIdentityFamily::Username: return u"username"_q;
				case PublicationIdentityFamily::Phone: return u"phone"_q;
				case PublicationIdentityFamily::UserId: return u"user_id"_q;
				}
				return u"unknown"_q;
			}();
			result.push_back(u"PUBLICATION_IDENTITY_CLASS: class=%1 family=%2 count=%3"_q
				.arg(SecrecyClassName(item.cls))
				.arg(name)
				.arg(FamilyCount(item, family)));
		}
	}
	return result;
}

bool CheckLaunchLogEnumeration(const LaunchLogEnumerationArgs &args) {
	if (!Active()) {
		return false;
	}
	const auto prepared = PrepareCampaign(args);
	const auto reading = ReadLaunchLogEnumeration(prepared.logs, prepared.nonce, args);
	for (const auto &item : reading.classes) {
		ReportEnumerationClass(item);
	}
	for (const auto &row : LaunchLogEnumerationRows(reading)) {
		Note(row);
	}
	Check(reading.decided, u"launch log enumeration: every required class decided"_q);
	return reading.decided;
}

bool CheckPublicationIdentity(const PublicationIdentityScanArgs &args) {
	if (!Active()) {
		return false;
	}
	const auto prepared = PrepareCampaign(args.logs);
	const auto reading = ReadPublicationIdentity(
		args.identity,
		prepared.logs,
		prepared.nonce,
		args.logs);
	for (const auto &item : reading.enumeration.classes) {
		ReportEnumerationClass(item, reading.find(item.cls));
	}
	for (const auto &row : LaunchLogEnumerationRows(reading.enumeration)) {
		Note(row);
	}
	for (const auto &row : PublicationIdentityRows(reading)) {
		Note(row);
	}
	Check(reading.decided, u"publication identity: scope and coverage decided"_q);
	Check(reading.clean, u"publication identity: no supplied presentation"_q);
	return reading.decided && reading.clean;
}

} // namespace Test

#endif // _DEBUG
