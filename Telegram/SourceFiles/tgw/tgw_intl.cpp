/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_intl.h"

#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "platform/platform_system_intl.h"
#include "twidget/intl.h"
#include "twidget/json.h"
#include "twidget/sdk_policy.h"
#include "ui/text/format_values.h"

#include <QtCore/QCollator>
#include <QtCore/QLocale>
#include <QtCore/QTimeZone>

namespace Tgw {
namespace {

constexpr auto kMaxFractionDigits = 20;
constexpr auto kMaxIntegerDigits = 21;
constexpr auto kMaxNameLength = size_t(64);
constexpr auto kMaxTimeValue = 8.64e15;
constexpr auto kNoBreakSpace = char16_t(0x00A0);
constexpr auto kInfinity = char16_t(0x221E);

enum class Field : uchar {
	Literal,
	Weekday,
	Year,
	Month,
	Day,
	Hour,
	Minute,
	Second,
	DayPeriod,
	Zone,
	Fraction,
};

enum class Width : uchar {
	None,
	Pattern,
	Numeric,
	TwoDigit,
	Short,
	Long,
};

enum class Style : uchar {
	None,
	Full,
	Long,
	Medium,
	Short,
};

struct Token {
	Field field = Field::Literal;
	QString text;
};

using Tokens = std::vector<Token>;
using FieldList = std::array<Field, 4>;

constexpr auto kDateFields = FieldList{
	Field::Weekday,
	Field::Year,
	Field::Month,
	Field::Day,
};
constexpr auto kTimeFields = FieldList{
	Field::Hour,
	Field::Minute,
	Field::Second,
	Field::Zone,
};

struct DateFields {
	Width weekday = Width::None;
	Width year = Width::None;
	Width month = Width::None;
	Width day = Width::None;
	Width hour = Width::None;
	Width minute = Width::None;
	Width second = Width::None;
	Width zone = Width::None;
};

struct DateRequest {
	DateFields fields;
	Style dateStyle = Style::None;
	Style timeStyle = Style::None;
	std::optional<bool> hour12;
};

struct Glue {
	QString literal;
	bool timeFirst = false;
};

struct Digits {
	int minimum = 0;
	int maximum = 0;
};

struct Formatted {
	QString text;
	int precision = 0;
};

class Intl final : public twidget::IntlProvider {
public:
	Intl(QLocale fallback, QString systemTimeZone);

	std::string format(const twidget::Json &request) override;

private:
	[[nodiscard]] QLocale resolveLocale(const twidget::Json &locale) const;
	[[nodiscard]] std::string dateOptions(
		const QLocale &locale,
		const twidget::Json &options) const;

	const QLocale _fallback;
	const QString _systemTimeZone;
	twidget::BasicIntl _basic;

};

[[nodiscard]] Field FieldFor(QChar letter, int repeat) {
	switch (letter.unicode()) {
	case 'd': return (repeat > 2) ? Field::Weekday : Field::Day;
	case 'M': return Field::Month;
	case 'y': return Field::Year;
	case 'h':
	case 'H': return Field::Hour;
	case 'm': return Field::Minute;
	case 's': return Field::Second;
	case 'a':
	case 'A': return Field::DayPeriod;
	case 't': return Field::Zone;
	case 'z': return Field::Fraction;
	}
	return Field::Literal;
}

void AppendLiteral(Tokens &tokens, const QString &text) {
	if (text.isEmpty()) {
		return;
	} else if (!tokens.empty() && tokens.back().field == Field::Literal) {
		tokens.back().text += text;
	} else {
		tokens.push_back({ Field::Literal, text });
	}
}

[[nodiscard]] QString ReadQuoted(const QString &pattern, int &index) {
	const auto quote = QChar('\'');
	const auto size = int(pattern.size());
	auto result = QString();
	++index;
	if (index < size && pattern[index] == quote) {
		++index;
		return QString(quote);
	}
	while (index < size) {
		if (pattern[index] != quote) {
			result += pattern[index++];
		} else if (index + 1 < size && pattern[index + 1] == quote) {
			result += quote;
			index += 2;
		} else {
			++index;
			break;
		}
	}
	return result;
}

[[nodiscard]] Tokens Tokenize(const QString &pattern) {
	const auto size = int(pattern.size());
	auto result = Tokens();
	auto index = 0;
	while (index < size) {
		const auto letter = pattern[index];
		if (letter == QChar('\'')) {
			AppendLiteral(result, ReadQuoted(pattern, index));
			continue;
		}
		auto repeat = 1;
		if (letter == QChar('a') || letter == QChar('A')) {
			const auto next = (index + 1 < size)
				? pattern[index + 1]
				: QChar();
			repeat = (next == QChar('p') || next == QChar('P')) ? 2 : 1;
		} else {
			while (index + repeat < size
				&& pattern[index + repeat] == letter) {
				++repeat;
			}
		}
		const auto text = pattern.mid(index, repeat);
		const auto field = FieldFor(letter, repeat);
		if (field == Field::Literal) {
			AppendLiteral(result, text);
		} else {
			result.push_back({ field, text });
		}
		index += repeat;
	}
	return result;
}

[[nodiscard]] QString Compose(const Tokens &tokens) {
	const auto quote = QChar('\'');
	auto result = QString();
	for (const auto &token : tokens) {
		if (token.field != Field::Literal) {
			result += token.text;
		} else if (!token.text.isEmpty()) {
			auto escaped = token.text;
			escaped.replace(quote, QString(2, quote));
			result += quote + escaped + quote;
		}
	}
	return result;
}

[[nodiscard]] QString Literals(const QString &pattern) {
	auto result = QString();
	for (const auto &token : Tokenize(pattern)) {
		if (token.field == Field::Literal) {
			result += token.text;
		}
	}
	return result;
}

[[nodiscard]] bool IsSpace(const QString &text) {
	return !text.isEmpty()
		&& std::all_of(text.begin(), text.end(), [](QChar ch) {
			return ch.isSpace();
		});
}

[[nodiscard]] QString Attached(const QString &literal) {
	auto length = 0;
	while (length < literal.size()
		&& (literal[length].isLetter() || literal[length].isMark())) {
		++length;
	}
	return literal.left(length);
}

[[nodiscard]] QString Separator(const Tokens &tokens, int from, int till) {
	auto result = std::optional<QString>();
	for (auto i = from + 1; i < till; ++i) {
		const auto &token = tokens[i];
		if (token.field != Field::Literal) {
			continue;
		} else if (IsSpace(token.text)) {
			return token.text;
		} else if (!result || token.text.size() < result->size()) {
			result = token.text;
		}
	}
	return result.value_or(QString());
}

[[nodiscard]] Tokens Filter(
		const Tokens &tokens,
		const std::vector<int> &kept) {
	if (kept.empty()) {
		return {};
	}
	auto first = -1;
	auto last = -1;
	for (auto i = 0, count = int(tokens.size()); i != count; ++i) {
		if (tokens[i].field != Field::Literal) {
			first = (first < 0) ? i : first;
			last = i;
		}
	}
	auto result = Tokens();
	if (kept.front() == first) {
		result.insert(end(result), begin(tokens), begin(tokens) + first);
	}
	for (auto k = 0, count = int(kept.size()); k != count; ++k) {
		result.push_back(tokens[kept[k]]);
		if (k + 1 != count) {
			AppendLiteral(result, Separator(tokens, kept[k], kept[k + 1]));
		}
	}
	if (kept.back() == last) {
		result.insert(end(result), begin(tokens) + last + 1, end(tokens));
	} else if (tokens[kept.back() + 1].field == Field::Literal) {
		AppendLiteral(result, Attached(tokens[kept.back() + 1].text));
	}
	return result;
}

[[nodiscard]] Width Requested(const DateFields &fields, Field field) {
	switch (field) {
	case Field::Weekday: return fields.weekday;
	case Field::Year: return fields.year;
	case Field::Month: return fields.month;
	case Field::Day: return fields.day;
	case Field::Hour:
	case Field::DayPeriod: return fields.hour;
	case Field::Minute: return fields.minute;
	case Field::Second: return fields.second;
	case Field::Zone: return fields.zone;
	case Field::Literal:
	case Field::Fraction: break;
	}
	return Width::None;
}

[[nodiscard]] bool Requests(const DateFields &fields, const FieldList &list) {
	return ranges::any_of(list, [&](Field field) {
		return (Requested(fields, field) != Width::None);
	});
}

[[nodiscard]] int Coverage(
		const Tokens &tokens,
		const DateFields &fields,
		const FieldList &list) {
	return int(ranges::count_if(list, [&](Field field) {
		return (field != Field::Weekday)
			&& (field != Field::Zone)
			&& (Requested(fields, field) != Width::None)
			&& ranges::contains(tokens, field, &Token::field);
	}));
}

[[nodiscard]] QString HourLetters(
		const QString &text,
		Width width,
		bool twelve) {
	const auto letter = QChar(twelve ? 'h' : 'H');
	if (width == Width::TwoDigit) {
		return QString(2, letter);
	} else if (text[0] == letter) {
		return QString(std::min(int(text.size()), 2), letter);
	}
	return QString(twelve ? 1 : 2, letter);
}

[[nodiscard]] std::optional<QString> Rewrite(
		const Token &token,
		const DateFields &fields,
		bool twelve) {
	const auto width = Requested(fields, token.field);
	const auto &text = token.text;
	const auto textual = (text.size() > 2);
	if (width == Width::None) {
		return std::nullopt;
	}
	switch (token.field) {
	case Field::Weekday:
		if (width == Width::Pattern) {
			return text;
		}
		return (width == Width::Long) ? u"dddd"_q : u"ddd"_q;
	case Field::Year:
		if (width == Width::Pattern) {
			return text;
		}
		return (width == Width::TwoDigit) ? u"yy"_q : u"yyyy"_q;
	case Field::Month:
		if (width == Width::TwoDigit) {
			return u"MM"_q;
		} else if (width == Width::Numeric) {
			return textual ? u"M"_q : text;
		} else if (width == Width::Pattern || !textual) {
			return text;
		}
		return (width == Width::Long) ? u"MMMM"_q : u"MMM"_q;
	case Field::Day:
		return (width == Width::TwoDigit) ? u"dd"_q : text;
	case Field::Hour:
		return HourLetters(text, width, twelve);
	case Field::Minute:
		if (width == Width::Pattern) {
			return text;
		}
		return (width == Width::TwoDigit || fields.hour != Width::None)
			? u"mm"_q
			: u"m"_q;
	case Field::Second:
		if (width == Width::Pattern) {
			return text;
		}
		return (width == Width::TwoDigit || fields.minute != Width::None)
			? u"ss"_q
			: u"s"_q;
	case Field::DayPeriod:
		if (!twelve) {
			return std::nullopt;
		}
		return text;
	case Field::Zone:
		return text;
	case Field::Literal:
	case Field::Fraction:
		break;
	}
	return std::nullopt;
}

[[nodiscard]] bool TwelveHour(
		const Tokens &source,
		std::optional<bool> hour12) {
	if (hour12) {
		return *hour12;
	}
	const auto hour = ranges::find(source, Field::Hour, &Token::field);
	return (hour != end(source))
		&& (hour->text[0] == QChar('h'))
		&& ranges::contains(source, Field::DayPeriod, &Token::field);
}

[[nodiscard]] Tokens Select(
		const Tokens &source,
		const DateFields &fields,
		bool twelve) {
	auto rewritten = source;
	auto kept = std::vector<int>();
	for (auto i = 0, count = int(source.size()); i != count; ++i) {
		auto &token = rewritten[i];
		if (token.field == Field::Literal) {
			continue;
		} else if (auto text = Rewrite(token, fields, twelve)) {
			token.text = std::move(*text);
			kept.push_back(i);
		}
	}
	return Filter(rewritten, kept);
}

[[nodiscard]] QString DefaultLetters(Field field) {
	switch (field) {
	case Field::Weekday: return u"dddd"_q;
	case Field::Year: return u"yyyy"_q;
	case Field::Month: return u"MMMM"_q;
	case Field::Day: return u"d"_q;
	case Field::Hour: return u"H"_q;
	case Field::Minute: return u"mm"_q;
	case Field::Second: return u"ss"_q;
	case Field::Zone: return u"t"_q;
	case Field::Literal:
	case Field::DayPeriod:
	case Field::Fraction: break;
	}
	return QString();
}

void InsertMissing(
		Tokens &tokens,
		const Tokens &reference,
		Field field,
		const DateFields &fields,
		bool twelve) {
	const auto i = ranges::find(reference, field, &Token::field);
	const auto found = (i != end(reference));
	const auto text = Rewrite(
		found ? *i : Token{ field, DefaultLetters(field) },
		fields,
		twelve);
	if (!text) {
		return;
	}
	const auto token = Token{ field, *text };
	if (tokens.empty()) {
		tokens.push_back(token);
		return;
	}
	const auto isField = [](const Token &entry) {
		return (entry.field != Field::Literal);
	};
	if (found && std::none_of(begin(reference), i, isField)) {
		auto prefix = Tokens{ token };
		const auto next = i + 1;
		if (next != end(reference) && !isField(*next)) {
			prefix.push_back(*next);
		}
		tokens.insert(begin(tokens), begin(prefix), end(prefix));
		return;
	}
	const auto previous = found
		&& (i != begin(reference))
		&& !isField(*(i - 1));
	const auto separator = !found
		? u" "_q
		: previous
		? (i - 1)->text.mid(Attached((i - 1)->text).size())
		: QString();
	AppendLiteral(tokens, separator);
	tokens.push_back(token);
}

[[nodiscard]] Tokens FinishHours(Tokens tokens, bool twelve) {
	if (!twelve
		|| !ranges::contains(tokens, Field::Hour, &Token::field)
		|| ranges::contains(tokens, Field::DayPeriod, &Token::field)) {
		return tokens;
	}
	auto after = 0;
	for (auto i = 0, count = int(tokens.size()); i != count; ++i) {
		const auto field = tokens[i].field;
		if (field == Field::Hour
			|| field == Field::Minute
			|| field == Field::Second) {
			after = i;
		}
	}
	const auto period = {
		Token{ Field::Literal, u" "_q },
		Token{ Field::DayPeriod, u"AP"_q },
	};
	tokens.insert(begin(tokens) + after + 1, period);
	return tokens;
}

[[nodiscard]] Tokens Build(
		const Tokens &primary,
		const Tokens &secondary,
		const DateFields &fields,
		const FieldList &list,
		std::optional<bool> hour12) {
	const auto better = (Coverage(secondary, fields, list)
		> Coverage(primary, fields, list));
	const auto &source = better ? secondary : primary;
	const auto &reference = better ? primary : secondary;
	const auto twelve = TwelveHour(source, hour12);
	auto result = Select(source, fields, twelve);
	for (const auto field : list) {
		if (Requested(fields, field) != Width::None
			&& !ranges::contains(result, field, &Token::field)) {
			InsertMissing(result, reference, field, fields, twelve);
		}
	}
	return FinishHours(std::move(result), twelve);
}

[[nodiscard]] Tokens DateTokens(
		const QLocale &locale,
		const DateFields &fields) {
	if (!Requests(fields, kDateFields)) {
		return {};
	}
	const auto textual = (fields.month == Width::Long)
		|| (fields.month == Width::Short)
		|| (fields.year == Width::None
			&& fields.month == Width::None
			&& fields.day == Width::None);
	const auto full = Tokenize(locale.dateFormat(QLocale::LongFormat));
	const auto brief = Tokenize(locale.dateFormat(QLocale::ShortFormat));
	return Build(
		textual ? full : brief,
		textual ? brief : full,
		fields,
		kDateFields,
		std::nullopt);
}

[[nodiscard]] Tokens TimeTokens(
		const QLocale &locale,
		const DateFields &fields,
		std::optional<bool> hour12) {
	if (!Requests(fields, kTimeFields)) {
		return {};
	}
	const auto seconds = (fields.second != Width::None);
	const auto full = Tokenize(locale.timeFormat(QLocale::LongFormat));
	const auto brief = Tokenize(locale.timeFormat(QLocale::ShortFormat));
	return Build(
		seconds ? full : brief,
		seconds ? brief : full,
		fields,
		kTimeFields,
		hour12);
}

[[nodiscard]] Tokens StyledDate(const QLocale &locale, Style style) {
	if (style == Style::None) {
		return {};
	}
	auto fields = DateFields();
	fields.year = fields.month = fields.day = Width::Pattern;
	if (style == Style::Short) {
		return Select(
			Tokenize(locale.dateFormat(QLocale::ShortFormat)),
			fields,
			false);
	}
	fields.weekday = (style == Style::Full) ? Width::Pattern : Width::None;
	fields.month = (style == Style::Medium) ? Width::Short : Width::Pattern;
	return Select(
		Tokenize(locale.dateFormat(QLocale::LongFormat)),
		fields,
		false);
}

[[nodiscard]] Tokens StyledTime(
		const QLocale &locale,
		Style style,
		std::optional<bool> hour12) {
	if (style == Style::None) {
		return {};
	}
	auto fields = DateFields();
	fields.hour = fields.minute = fields.second = Width::Pattern;
	fields.zone = (style == Style::Full || style == Style::Long)
		? Width::Pattern
		: Width::None;
	const auto source = Tokenize(locale.timeFormat((style == Style::Short)
		? QLocale::ShortFormat
		: QLocale::LongFormat));
	const auto twelve = TwelveHour(source, hour12);
	return FinishHours(Select(source, fields, twelve), twelve);
}

[[nodiscard]] Glue DateTimeGlue(const QLocale &locale) {
	const auto date = locale.dateFormat(QLocale::ShortFormat);
	const auto time = locale.timeFormat(QLocale::ShortFormat);
	const auto both = locale.dateTimeFormat(QLocale::ShortFormat);
	const auto middle = both.size() - date.size() - time.size();
	if (middle >= 0 && both.startsWith(date) && both.endsWith(time)) {
		return { Literals(both.mid(date.size(), middle)) };
	} else if (middle >= 0 && both.startsWith(time) && both.endsWith(date)) {
		return { Literals(both.mid(time.size(), middle)), true };
	}
	return { u" "_q };
}

[[nodiscard]] Tokens Combine(
		const QLocale &locale,
		const Tokens &date,
		const Tokens &time) {
	if (date.empty() || time.empty()) {
		return date.empty() ? time : date;
	}
	const auto glue = DateTimeGlue(locale);
	auto result = glue.timeFirst ? time : date;
	AppendLiteral(result, glue.literal);
	for (const auto &token : (glue.timeFirst ? date : time)) {
		if (token.field == Field::Literal) {
			AppendLiteral(result, token.text);
		} else {
			result.push_back(token);
		}
	}
	return result;
}

[[nodiscard]] Width ParseWidth(const std::string &value) {
	if (value == "numeric") {
		return Width::Numeric;
	} else if (value == "2-digit") {
		return Width::TwoDigit;
	} else if (value == "long") {
		return Width::Long;
	} else if (value == "short" || value == "narrow") {
		return Width::Short;
	}
	return Width::None;
}

[[nodiscard]] Style ParseStyle(const std::string &value) {
	if (value == "full") {
		return Style::Full;
	} else if (value == "long") {
		return Style::Long;
	} else if (value == "medium") {
		return Style::Medium;
	} else if (value == "short") {
		return Style::Short;
	}
	return Style::None;
}

[[nodiscard]] std::optional<bool> ParseHour12(
		const twidget::Json &options) {
	const auto &hour12 = options["hour12"];
	const auto &cycle = options["hourCycle"].asString();
	if (hour12.isBool()) {
		return hour12.asBool();
	} else if (cycle == "h11" || cycle == "h12") {
		return true;
	} else if (cycle == "h23" || cycle == "h24") {
		return false;
	}
	return std::nullopt;
}

[[nodiscard]] DateRequest ParseDateRequest(const twidget::Json &request) {
	const auto &options = request["opts"];
	const auto &method = request["method"].asString();
	const auto methodCall = (request["kind"].asString() == "dateMethod");
	auto result = DateRequest();
	auto &fields = result.fields;
	fields.weekday = ParseWidth(options["weekday"].asString());
	fields.year = ParseWidth(options["year"].asString());
	fields.month = ParseWidth(options["month"].asString());
	fields.day = ParseWidth(options["day"].asString());
	fields.hour = ParseWidth(options["hour"].asString());
	fields.minute = ParseWidth(options["minute"].asString());
	fields.second = ParseWidth(options["second"].asString());
	fields.zone = options["timeZoneName"].asString().empty()
		? Width::None
		: Width::Pattern;
	result.dateStyle = ParseStyle(options["dateStyle"].asString());
	result.timeStyle = ParseStyle(options["timeStyle"].asString());
	result.hour12 = ParseHour12(options);

	const auto date = !methodCall || (method != "toLocaleTimeString");
	const auto time = methodCall && (method != "toLocaleDateString");
	const auto hasTime = (fields.hour != Width::None)
		|| (fields.minute != Width::None)
		|| (fields.second != Width::None);
	const auto specified = (result.dateStyle != Style::None)
		|| (result.timeStyle != Style::None)
		|| (date && Requests(fields, kDateFields))
		|| ((!methodCall || time) && hasTime);
	if (specified) {
		return result;
	}
	if (date) {
		fields.year = fields.month = fields.day = Width::Numeric;
	}
	if (time) {
		fields.hour = fields.minute = fields.second = Width::Numeric;
	}
	return result;
}

[[nodiscard]] QString DatePattern(
		const QLocale &locale,
		const DateRequest &request) {
	const auto styled = (request.dateStyle != Style::None)
		|| (request.timeStyle != Style::None);
	const auto date = styled
		? StyledDate(locale, request.dateStyle)
		: DateTokens(locale, request.fields);
	const auto time = styled
		? StyledTime(locale, request.timeStyle, request.hour12)
		: TimeTokens(locale, request.fields, request.hour12);
	return Compose(Combine(locale, date, time));
}

[[nodiscard]] std::optional<int64> TimeValue(const twidget::Json &value) {
	if (!value.isNumber()) {
		return std::nullopt;
	}
	const auto milliseconds = value.asNumber();
	if (!std::isfinite(milliseconds)
		|| std::abs(milliseconds) > kMaxTimeValue) {
		return std::nullopt;
	}
	return int64(milliseconds);
}

[[nodiscard]] std::optional<QTimeZone> ResolveTimeZone(
		const twidget::Json &value) {
	const auto &id = value.asString();
	if (id.empty() || id.size() > kMaxNameLength) {
		return std::nullopt;
	}
	const auto name = QString::fromStdString(id);
	for (const auto utc : { "UTC", "Etc/UTC", "GMT", "Etc/GMT" }) {
		if (!name.compare(QLatin1String(utc), Qt::CaseInsensitive)) {
			return QTimeZone::utc();
		}
	}
	auto zone = QTimeZone(QByteArray::fromStdString(id));
	if (!zone.isValid()) {
		return std::nullopt;
	}
	return zone;
}

[[nodiscard]] QString FormatDate(
		const QLocale &locale,
		const twidget::Json &request) {
	const auto value = TimeValue(request["value"]);
	if (!value) {
		return u"Invalid Date"_q;
	}
	const auto zone = ResolveTimeZone(request["opts"]["timeZone"]);
	const auto moment = zone
		? QDateTime::fromMSecsSinceEpoch(*value, *zone)
		: QDateTime::fromMSecsSinceEpoch(*value);
	if (!moment.isValid()) {
		return u"Invalid Date"_q;
	}
	return locale.toString(
		moment,
		DatePattern(locale, ParseDateRequest(request)));
}

} // namespace

// BasicIntl indexes its name tables with this conversion's unchecked result.
bool BasicCanConvert(const twidget::Json &value) {
	using Limits = std::numeric_limits<time_t>;
	const auto milliseconds = value.asNumber();
	if (!std::isfinite(milliseconds)) {
		return true;
	}
	const auto seconds = std::floor(milliseconds / 1000);
	if (seconds < double(Limits::min())
		|| seconds >= std::ldexp(1., Limits::digits)) {
		return false;
	}
	const auto time = time_t(seconds);
	auto parts = tm();
#ifdef Q_OS_WIN
	const auto converted = !gmtime_s(&parts, &time);
#else
	const auto converted = (gmtime_r(&time, &parts) != nullptr);
#endif // Q_OS_WIN
	return converted
		&& (parts.tm_mon >= 0)
		&& (parts.tm_mon < 12)
		&& (parts.tm_wday >= 0)
		&& (parts.tm_wday < 7);
}

namespace {

[[nodiscard]] std::string LiteralParts(const QString &text) {
	auto part = twidget::Json::object();
	part.set("type", "literal");
	part.set("value", text.toStdString());
	auto result = twidget::Json::array();
	result.push(std::move(part));
	return result.dump();
}

[[nodiscard]] std::optional<int> DigitsOption(
		const twidget::Json &options,
		std::string_view key,
		int minimum,
		int maximum) {
	const auto &value = options[key];
	if (!value.isNumber() || !std::isfinite(value.asNumber())) {
		return std::nullopt;
	}
	return int(std::clamp(
		std::floor(value.asNumber()),
		double(minimum),
		double(maximum)));
}

[[nodiscard]] Digits FractionDigits(
		const twidget::Json &options,
		int defaultMinimum,
		int defaultMaximum) {
	const auto minimum = DigitsOption(
		options,
		"minimumFractionDigits",
		0,
		kMaxFractionDigits);
	const auto maximum = DigitsOption(
		options,
		"maximumFractionDigits",
		0,
		kMaxFractionDigits);
	if (minimum && maximum) {
		return { std::min(*minimum, *maximum), *maximum };
	} else if (minimum) {
		return { *minimum, std::max(*minimum, defaultMaximum) };
	} else if (maximum) {
		return { std::min(defaultMinimum, *maximum), *maximum };
	}
	return { defaultMinimum, defaultMaximum };
}

[[nodiscard]] QString CurrencyCode(const std::string &value) {
	const auto letter = [](char ch) {
		return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
	};
	return (value.size() == 3 && ranges::all_of(value, letter))
		? QString::fromStdString(value).toUpper()
		: QString();
}

[[nodiscard]] QString CurrencySymbol(
		const QLocale &locale,
		const QString &code,
		const std::string &display) {
	if (display == "code" || display == "name") {
		return code;
	} else if (locale.currencySymbol(QLocale::CurrencyIsoCode) == code) {
		const auto own = locale.currencySymbol(QLocale::CurrencySymbol);
		if (!own.isEmpty()) {
			return own;
		}
	}
	return Ui::CurrencyName(code);
}

[[nodiscard]] Formatted FormatMagnitude(
		const QLocale &locale,
		double magnitude,
		Digits digits) {
	const auto zero = QString(locale.zeroDigit());
	const auto point = QString(locale.decimalPoint());
	auto result = Formatted{
		.text = locale.toString(magnitude, 'f', digits.maximum),
		.precision = digits.maximum,
	};
	while (result.precision > digits.minimum
		&& result.text.endsWith(zero)) {
		result.text.chop(zero.size());
		--result.precision;
	}
	if (!result.precision && result.text.endsWith(point)) {
		result.text.chop(point.size());
	}
	return result;
}

[[nodiscard]] QString PadIntegers(
		const QString &text,
		const QLocale &locale,
		int minimum) {
	const auto point = text.indexOf(QString(locale.decimalPoint()));
	const auto integers = (point >= 0) ? point : text.size();
	const auto digits = int(std::count_if(
		text.begin(),
		text.begin() + integers,
		[](QChar ch) { return ch.isDigit(); }));
	return (digits < minimum)
		? (QString(locale.zeroDigit()).repeated(minimum - digits) + text)
		: text;
}

[[nodiscard]] QString PlaceCurrency(
		const QLocale &locale,
		const Formatted &number,
		double magnitude,
		const QString &symbol) {
	const auto pattern = QLocale(locale.bcp47Name());
	const auto plain = pattern.toString(magnitude, 'f', number.precision);
	auto result = pattern.toCurrencyString(
		magnitude,
		symbol,
		number.precision);
	const auto at = result.indexOf(plain);
	if (at >= 0) {
		result.replace(at, plain.size(), number.text);
	}
	const auto from = result.indexOf(symbol);
	if (symbol.isEmpty() || from < 0) {
		return result;
	}
	const auto till = from + symbol.size();
	if (till < result.size()
		&& symbol.back().isLetter()
		&& result.at(till).isDigit()) {
		result.insert(till, QChar(kNoBreakSpace));
	}
	if (from > 0
		&& symbol.front().isLetter()
		&& result.at(from - 1).isDigit()) {
		result.insert(from, QChar(kNoBreakSpace));
	}
	return result;
}

[[nodiscard]] QString FormatNumber(
		QLocale locale,
		const twidget::Json &value,
		const twidget::Json &options) {
	if (!value.isNumber() || std::isnan(value.asNumber())) {
		return u"NaN"_q;
	}
	const auto &style = options["style"].asString();
	const auto code = (style == "currency")
		? CurrencyCode(options["currency"].asString())
		: QString();
	const auto percent = (style == "percent");
	const auto number = value.asNumber() * (percent ? 100. : 1.);
	const auto sign = std::signbit(number)
		? QString(locale.negativeSign())
		: QString();
	if (std::isinf(number)) {
		return sign + QChar(kInfinity);
	}
	const auto currencyDigits = code.isEmpty()
		? 0
		: Ui::LookupCurrencyRule(code).exponent;
	const auto digits = code.isEmpty()
		? FractionDigits(options, 0, percent ? 0 : 3)
		: FractionDigits(options, currencyDigits, currencyDigits);
	const auto &grouping = options["useGrouping"];
	if (grouping.isBool() && !grouping.asBool()) {
		locale.setNumberOptions(
			locale.numberOptions() | QLocale::OmitGroupSeparator);
	}
	const auto magnitude = std::abs(number);
	auto formatted = FormatMagnitude(locale, magnitude, digits);
	const auto integers = DigitsOption(
		options,
		"minimumIntegerDigits",
		1,
		kMaxIntegerDigits);
	if (integers) {
		formatted.text = PadIntegers(formatted.text, locale, *integers);
	}
	if (!code.isEmpty()) {
		const auto symbol = CurrencySymbol(
			locale,
			code,
			options["currencyDisplay"].asString());
		return sign + PlaceCurrency(locale, formatted, magnitude, symbol);
	} else if (percent) {
		return sign + formatted.text + QString(locale.percent());
	}
	return sign + formatted.text;
}

[[nodiscard]] QString FormatList(
		const QLocale &locale,
		const twidget::Json &value) {
	auto items = QStringList();
	for (const auto &item : value.arr()) {
		items.push_back(QString::fromStdString(item.asString()));
	}
	return locale.createSeparatedList(items);
}

[[nodiscard]] std::string Compare(
		const QLocale &locale,
		const twidget::Json &request) {
	const auto &options = request["opts"];
	const auto &sensitivity = options["sensitivity"].asString();
	auto collator = QCollator(locale);
	collator.setCaseSensitivity(
		(sensitivity == "base" || sensitivity == "accent")
			? Qt::CaseInsensitive
			: Qt::CaseSensitive);
	collator.setNumericMode(options["numeric"].asBool());
	collator.setIgnorePunctuation(options["ignorePunctuation"].asBool());
	const auto result = collator.compare(
		QString::fromStdString(request["a"].asString()),
		QString::fromStdString(request["b"].asString()));
	return (result < 0) ? "-1" : (result > 0) ? "1" : "0";
}

[[nodiscard]] twidget::Json LocaleOptions(const QLocale &locale) {
	auto name = locale.name();
	auto result = twidget::Json::object();
	result.set(
		"locale",
		name.replace(QChar('_'), QChar('-')).toStdString());
	return result;
}

Intl::Intl(QLocale fallback, QString systemTimeZone)
: _fallback(std::move(fallback))
, _systemTimeZone(std::move(systemTimeZone)) {
}

std::string Intl::format(const twidget::Json &request) {
	const auto &kind = request["kind"].asString();
	const auto &options = request["opts"];
	const auto &listType = options["type"].asString();
	if (kind == "plural"
		|| kind == "relative"
		|| (kind == "list"
			&& !listType.empty()
			&& listType != "conjunction")) {
		return _basic.format(request);
	}
	const auto locale = resolveLocale(request["locale"]);
	if (kind == "number") {
		return FormatNumber(locale, request["value"], options).toStdString();
	} else if (kind == "date" || kind == "dateMethod") {
		return FormatDate(locale, request).toStdString();
	} else if (kind == "numberOptions") {
		return LocaleOptions(locale).dump();
	} else if (kind == "dateOptions") {
		return dateOptions(locale, options);
	} else if (kind == "list") {
		return FormatList(locale, request["value"]).toStdString();
	} else if (kind == "compare") {
		return Compare(locale, request);
	} else if (kind == "upper" || kind == "lower") {
		const auto text = QString::fromStdString(request["value"].asString());
		const auto result = (kind == "upper")
			? locale.toUpper(text)
			: locale.toLower(text);
		return result.toStdString();
	} else if (kind == "dateParts" && !BasicCanConvert(request["value"])) {
		return LiteralParts(FormatDate(locale, request));
	}
	return _basic.format(request);
}

QLocale Intl::resolveLocale(const twidget::Json &locale) const {
	const auto &tag = (locale.isArray()
		? locale[size_t(0)]
		: locale).asString();
	if (!tag.empty() && tag.size() <= kMaxNameLength) {
		auto result = QLocale(QString::fromStdString(tag));
		if (result.language() != QLocale::C) {
			return result;
		}
	}
	return _fallback;
}

std::string Intl::dateOptions(
		const QLocale &locale,
		const twidget::Json &options) const {
	const auto zone = ResolveTimeZone(options["timeZone"]);
	const auto id = zone
		? QString::fromLatin1(zone->id())
		: _systemTimeZone;
	auto result = LocaleOptions(locale);
	if (!id.isEmpty()) {
		result.set("timeZone", id.toStdString());
	}
	return result.dump();
}

} // namespace

QString UiLanguageTag() {
	const auto draft = u"-raw"_q;
	auto result = Lang::GetInstance().cloudLangCode(
		Lang::Pack::Current
	).toLower();
	result.replace(QChar('_'), QChar('-'));
	if (result.endsWith(draft)) {
		result.chop(draft.size());
	}
	return twidget::sdk::checkLocale(result.toStdString())
		? result
		: Lang::DefaultLanguageId();
}

std::shared_ptr<twidget::IntlProvider> CreateIntl() {
	return std::make_shared<Intl>(
		QLocale(UiLanguageTag()),
		QString::fromLatin1(QTimeZone::systemTimeZoneId()));
}

std::shared_ptr<twidget::IntlProvider> CreateHostIntl() {
	if (auto system = Platform::CreateSystemIntl(UiLanguageTag())) {
		return system;
	}
	return CreateIntl();
}

} // namespace Tgw
