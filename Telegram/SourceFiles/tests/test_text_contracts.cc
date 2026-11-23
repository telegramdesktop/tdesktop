/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/integration.h"
#include "tests/test_named_checks.hpp"
#include "ui/effects/animations.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_font.h"
#include "ui/style/style_core_scale.h"
#include "ui/text/text.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/emoji_config.h"
#include "ui/integration.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"

#include "styles/style_basic.h"
#include "styles/style_widgets.h"

#include <crl/crl_on_main.h>
#include <rpl/never.h>

#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtGui/QClipboard>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontInfo>
#include <QtGui/QImage>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

#include <array>
#include <cstdio>
#include <vector>

namespace {

using Test::NamedChecks::Check;
using Test::NamedChecks::FirstFailure;

constexpr auto kParseFlags = TextParseLinks
	| TextParseMentions
	| TextParseHashtags
	| TextParseBotCommands;

constexpr auto kLineHeight = 18;
constexpr auto kOrigin = QPoint(4, 4);
constexpr auto kCanvas = QRect(0, 0, 200, 120);

struct SymbolHit {
	int symbol = 0;
	bool upon = false;
	bool after = false;

	friend inline bool operator==(
		const SymbolHit &,
		const SymbolHit &) = default;
};

using InputMethodAttributes = QList<QInputMethodEvent::Attribute>;

struct InputMethodRow {
	const char *name = nullptr;
	TextWithTags initial;
	int breakAt = -1;
	int maxLength = 0;
	int from = 3;
	int till = 3;
	QString preedit;
	QString commit;
	int replacementStart = 0;
	int replacementLength = 0;
	TextWithTags expected;
	std::optional<TextWithTags> undone;
	QString changes;
	bool splices = true;
	Fn<void(not_null<Ui::InputField*>)> setup;
	Fn<InputMethodAttributes(not_null<Ui::InputField*>)> attributes;
	bool typed = false;
	std::optional<TextWithTags> paste;
};

class BaseIntegration final : public base::Integration {
public:
	using Integration::Integration;

	void enterFromEventLoop(FnMut<void()> &&method) override;
	bool logSkipDebug() override;
	void logMessageDebug(const QString &message) override;
	void logMessage(const QString &message) override;

};

class UiIntegration final : public Ui::Integration {
public:
	void postponeCall(FnMut<void()> &&callable) override;
	void registerLeaveSubscription(not_null<QWidget*> widget) override;
	void unregisterLeaveSubscription(not_null<QWidget*> widget) override;
	[[nodiscard]] QString emojiCacheFolder() override;
	[[nodiscard]] QString fontsCacheFolder() override;
	[[nodiscard]] QString openglCheckFilePath() override;
	[[nodiscard]] QString angleBackendFilePath() override;
	void touchCounterIncrement() override;
	[[nodiscard]] int touchCounterNow() override;

};

class RejectedMime final : public QMimeData {
public:
	RejectedMime(not_null<Ui::InputField*> field, int position);

	bool hasFormat(const QString &mimeType) const override;

private:
	const not_null<Ui::InputField*> _field;
	const int _position = 0;

};

void BaseIntegration::enterFromEventLoop(FnMut<void()> &&method) {
	method();
}

bool BaseIntegration::logSkipDebug() {
	return true;
}

void BaseIntegration::logMessageDebug(const QString &message) {
}

void BaseIntegration::logMessage(const QString &message) {
}

void UiIntegration::postponeCall(FnMut<void()> &&callable) {
}

void UiIntegration::registerLeaveSubscription(not_null<QWidget*> widget) {
}

void UiIntegration::unregisterLeaveSubscription(not_null<QWidget*> widget) {
}

QString UiIntegration::emojiCacheFolder() {
	return QString();
}

QString UiIntegration::fontsCacheFolder() {
	return QString();
}

QString UiIntegration::openglCheckFilePath() {
	return QString();
}

QString UiIntegration::angleBackendFilePath() {
	return QString();
}

void UiIntegration::touchCounterIncrement() {
}

int UiIntegration::touchCounterNow() {
	return 0;
}

RejectedMime::RejectedMime(
	not_null<Ui::InputField*> field,
	int position)
: _field(field)
, _position(position) {
	setParent(field);
}

bool RejectedMime::hasFormat(const QString &mimeType) const {
	auto cursor = _field->textCursor();
	cursor.setPosition(_position);
	_field->setTextCursor(cursor);
	return false;
}

[[nodiscard]] QString StartOffscreenStyle() {
	const auto platform = QGuiApplication::platformName();
	if (platform != u"offscreen"_q) {
		return u"platform '%1', expected 'offscreen'"_q.arg(platform);
	}
	const auto screen = QGuiApplication::primaryScreen();
	if (!screen) {
		return u"no primary screen"_q;
	}
	const auto dpi = screen->logicalDotsPerInch();
	const auto ratio = screen->devicePixelRatio();
	if (dpi != 96. || ratio != 1.) {
		return u"screen %1 dpi at ratio %2, expected 96 dpi at ratio 1"_q
			.arg(dpi)
			.arg(ratio);
	}
	const auto before = QFontDatabase::families();
	if (!before.isEmpty()) {
		return u"families before registration: "_q + before.join(u", "_q);
	}
	style::SetDevicePixelRatio(1);
	style::internal::StartFonts();
	const auto after = QFontDatabase::families();
	const auto bundled = QSet<QString>{
		u"Open Sans"_q,
		u"Vazirmatn UI NL"_q,
	};
	if (QSet<QString>(after.begin(), after.end()) != bundled) {
		return u"families after registration: "_q + after.join(u", "_q);
	}
	style::StartManager(style::kScaleDefault);
	if (style::Scale() != style::kScaleDefault) {
		return u"style scale %1, expected %2"_q
			.arg(style::Scale())
			.arg(style::kScaleDefault);
	}
	const auto family = QFontInfo(st::defaultTextStyle.font->f).family();
	if (family != u"Open Sans"_q) {
		return u"default text style family '%1'"_q.arg(family);
	}
	return QString();
}

[[nodiscard]] QString Escaped(const QString &text) {
	auto result = QString();
	for (const auto ch : text) {
		const auto code = int(ch.unicode());
		if (code >= 0x20 && code < 0x7F) {
			result.append(ch);
		} else {
			result.append(u"\\u%1"_q.arg(code, 4, 16, QChar('0')));
		}
	}
	return result;
}

[[nodiscard]] QString Describe(const TextWithEntities &value) {
	auto result = u"'%1'"_q.arg(Escaped(value.text));
	for (const auto &entity : value.entities) {
		result += u" (%1,%2,%3)"_q
			.arg(int(entity.type()))
			.arg(entity.offset())
			.arg(entity.length());
	}
	return result;
}

[[nodiscard]] QString ExpectEntities(
		const QString &text,
		const EntitiesInText &entities) {
	const auto parsed = TextUtilities::ParseEntities(text, kParseFlags);
	const auto expected = TextWithEntities{ text, entities };
	return (parsed == expected)
		? QString()
		: u"got %1, expected %2"_q.arg(Describe(parsed), Describe(expected));
}

[[nodiscard]] QString ExpectFolded(
		const QString &text,
		const QString &expected) {
	const auto folded = TextUtilities::RemoveAccents(text);
	return (folded == expected)
		? QString()
		: u"'%1' folded to '%2', expected '%3'"_q.arg(
			Escaped(text),
			Escaped(folded),
			Escaped(expected));
}

[[nodiscard]] QString ExpectWords(
		const QString &query,
		const QStringList &expected,
		const QRegularExpression *splitter = nullptr) {
	const auto words = TextUtilities::PrepareSearchWords(query, splitter);
	return (words == expected)
		? QString()
		: u"'%1' gave [%2], expected [%3]"_q.arg(
			Escaped(query),
			Escaped(words.join(u","_q)),
			Escaped(expected.join(u","_q)));
}

[[nodiscard]] QString CheckKnownBareDomain() {
	return ExpectEntities(
		u"Visit telegram.org now"_q,
		{ EntityInText(EntityType::Url, 6, 12) });
}

[[nodiscard]] QString CheckCyrillicDomain() {
	return ExpectEntities(
		u"site \u043F\u0440\u0438\u043C\u0435\u0440.\u0440\u0444."_q,
		{ EntityInText(EntityType::Url, 5, 9) });
}

[[nodiscard]] QString CheckUnknownBareSuffix() {
	return ExpectEntities(u"Read notes.txt and telegram.app"_q, {});
}

[[nodiscard]] QString CheckExplicitProtocol() {
	return FirstFailure({
		ExpectEntities(
			u"Go https://example.unknownsuffix/path?x=1."_q,
			{ EntityInText(EntityType::Url, 3, 38) }),
		ExpectEntities(u"gopher://telegram.org"_q, {}),
	});
}

[[nodiscard]] QString CheckAfterSupplementary() {
	return ExpectEntities(
		u"\U0001F600 telegram.org"_q,
		{ EntityInText(EntityType::Url, 3, 12) });
}

[[nodiscard]] QString CheckPrecomposedAccents() {
	return ExpectFolded(
		u"Caf\u00E9 \u00C5ngstr\u00F6m"_q,
		u"Cafe angstrom"_q);
}

[[nodiscard]] QString CheckCyrillicAccents() {
	return ExpectFolded(
		u"\u0451\u043B\u043A\u0430"_q,
		u"\u0435\u043B\u043A\u0430"_q);
}

[[nodiscard]] QString CheckCombiningMarks() {
	return ExpectFolded(u"e\u0301x"_q, u"ex"_q);
}

[[nodiscard]] QString CheckUnmappedCharacters() {
	return ExpectFolded(u"a\U0001F600b"_q, u"a\U0001F600b"_q);
}

[[nodiscard]] QString CheckSearchNormalization() {
	const auto commas = QRegularExpression(u","_q);
	return FirstFailure({
		ExpectWords(
			u"  Hello, WORLD-wide  "_q,
			{ u"hello"_q, u"world"_q, u"wide"_q }),
		ExpectWords(u"Caf\u00E9 \u00DCber"_q, { u"cafe"_q, u"uber"_q }),
		ExpectWords(
			u"One,  Two  ,   ,Three"_q,
			{ u"one"_q, u"two"_q, u"three"_q },
			&commas),
	});
}

[[nodiscard]] QString Describe(const TextWithTags &value) {
	auto result = u"'%1'"_q.arg(Escaped(value.text));
	for (const auto &tag : value.tags) {
		result += u" (%1,%2,%3)"_q
			.arg(tag.offset)
			.arg(tag.length)
			.arg(Escaped(tag.id));
	}
	return result;
}

[[nodiscard]] TextWithTags BoldBase() {
	return { u"ab bold cd"_q, { { 3, 4, Ui::InputField::kTagBold } } };
}

[[nodiscard]] QString IdentityMimeTag(QStringView tag) {
	return tag.toString();
}

[[nodiscard]] not_null<Ui::InputField*> CreateField(
		not_null<QWidget*> parent,
		const TextWithTags &initial) {
	const auto result = Ui::CreateChild<Ui::InputField>(
		parent.get(),
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine);
	result->resize(400, 200);
	result->setTextWithTags(initial, Ui::InputField::HistoryAction::Clear);
	return result;
}

void Select(not_null<Ui::InputField*> field, int from, int till) {
	auto cursor = field->textCursor();
	cursor.setPosition(from);
	cursor.setPosition(till, QTextCursor::KeepAnchor);
	field->setTextCursor(cursor);
}

void Insert(
		not_null<Ui::InputField*> field,
		int position,
		const QString &text) {
	Select(field, position, position);
	field->rawTextEdit()->insertPlainText(text);
}

void SendInputMethod(
		not_null<Ui::InputField*> field,
		const InputMethodRow &row) {
	Select(field, row.from, row.till);
	auto event = QInputMethodEvent(
		row.preedit,
		row.attributes ? row.attributes(field) : InputMethodAttributes());
	event.setCommitString(
		row.commit,
		row.replacementStart,
		row.replacementLength);
	QCoreApplication::sendEvent(field->rawTextEdit(), &event);
}

void Paste(
		not_null<Ui::InputField*> field,
		int from,
		int till,
		TextWithTags &&text) {
	Select(field, from, till);
	QGuiApplication::clipboard()->setMimeData(
		TextUtilities::MimeDataFromText(std::move(text)).release());
	field->rawTextEdit()->paste();
}

[[nodiscard]] QString ExpectExport(
		not_null<const Ui::InputField*> field,
		const TextWithTags &expected) {
	const auto &exported = field->getTextWithTags();
	return (exported == expected)
		? QString()
		: u"got %1, expected %2"_q.arg(
			Describe(exported),
			Describe(expected));
}

[[nodiscard]] QString CheckLeftEdgeInsertion() {
	auto parent = Ui::RpWidget();
	const auto field = CreateField(&parent, BoldBase());
	Insert(field, 3, u"X"_q);
	return ExpectExport(
		field,
		{ u"ab Xbold cd"_q, { { 4, 4, Ui::InputField::kTagBold } } });
}

[[nodiscard]] QString CheckRightEdgeInsertion() {
	const auto &bold = Ui::InputField::kTagBold;
	auto parent = Ui::RpWidget();
	const auto digit = CreateField(&parent, BoldBase());
	Insert(digit, 7, u"7"_q);
	const auto separated = CreateField(&parent, BoldBase());
	Insert(separated, 7, u" Q"_q);
	return FirstFailure({
		ExpectExport(digit, { u"ab bold7 cd"_q, { { 3, 5, bold } } }),
		ExpectExport(separated, { u"ab bold Q cd"_q, { { 3, 4, bold } } }),
	});
}

[[nodiscard]] QString CheckInsertionInsideTag() {
	auto parent = Ui::RpWidget();
	const auto field = CreateField(&parent, BoldBase());
	Insert(field, 5, u" "_q);
	return ExpectExport(
		field,
		{ u"ab bo ld cd"_q, { { 3, 5, Ui::InputField::kTagBold } } });
}

[[nodiscard]] QString CheckMimeTags() {
	const auto &bold = Ui::InputField::kTagBold;
	const auto &italic = Ui::InputField::kTagItalic;
	auto parent = Ui::RpWidget();
	const auto inserted = CreateField(&parent, BoldBase());
	inserted->setTagMimeProcessor(IdentityMimeTag);
	Paste(inserted, 2, 2, { u"new"_q, { { 0, 3, italic } } });
	const auto filtered = CreateField(&parent, BoldBase());
	Paste(filtered, 2, 2, { u"new"_q, { { 0, 3, italic } } });
	const auto replaced = CreateField(&parent, BoldBase());
	replaced->setTagMimeProcessor(IdentityMimeTag);
	Paste(replaced, 4, 6, { u"xy"_q, { { 0, 2, italic } } });
	const auto split = TextWithTags{
		u"ab bxyd cd"_q,
		{ { 3, 1, bold }, { 4, 2, italic }, { 6, 1, bold } },
	};
	return FirstFailure({
		ExpectExport(
			inserted,
			{ u"abnew bold cd"_q, { { 2, 3, italic }, { 6, 4, bold } } }),
		ExpectExport(filtered, { u"abnew bold cd"_q, { { 6, 4, bold } } }),
		ExpectExport(replaced, split),
	});
}

[[nodiscard]] QString CheckIncompatibleFormatting() {
	const auto &bold = Ui::InputField::kTagBold;
	const auto &code = Ui::InputField::kTagCode;
	const auto &pre = Ui::InputField::kTagPre;
	auto parent = Ui::RpWidget();
	const auto block = CreateField(
		&parent,
		{ u"x\ncode\ny"_q, { { 2, 4, pre } } });
	block->setTagMimeProcessor(IdentityMimeTag);
	Paste(block, 4, 4, { u"BB"_q, { { 0, 2, bold } } });
	const auto toggled = CreateField(&parent, BoldBase());
	Select(toggled, 2, 5);
	toggled->toggleCurrentMarkdownTag(code);
	return FirstFailure({
		ExpectExport(block, { u"x\ncoBBde\ny"_q, { { 2, 6, pre } } }),
		ExpectExport(
			toggled,
			{ u"ab bold cd"_q, { { 2, 3, code }, { 5, 2, bold } } }),
	});
}

[[nodiscard]] QString CheckUnicodeBoundaries() {
	const auto &bold = Ui::InputField::kTagBold;
	const auto initial = TextWithTags{
		u"a\U0001D400b"_q,
		{ { 1, 2, bold } },
	};
	auto parent = Ui::RpWidget();
	const auto letter = CreateField(&parent, initial);
	Insert(letter, 3, u"c"_q);
	const auto supplementary = CreateField(&parent, initial);
	Insert(supplementary, 3, u"\U0001D401"_q);
	const auto digit = CreateField(&parent, initial);
	Insert(digit, 3, u"\U0001D7CE"_q);
	const auto symbol = CreateField(&parent, initial);
	Insert(symbol, 3, u"\U0001D6C1"_q);
	return FirstFailure({
		ExpectExport(letter, { u"a\U0001D400cb"_q, { { 1, 3, bold } } }),
		ExpectExport(
			supplementary,
			{ u"a\U0001D400\U0001D401b"_q, { { 1, 4, bold } } }),
		ExpectExport(
			digit,
			{ u"a\U0001D400\U0001D7CEb"_q, { { 1, 4, bold } } }),
		ExpectExport(
			symbol,
			{ u"a\U0001D400\U0001D6C1b"_q, { { 1, 2, bold } } }),
	});
}

[[nodiscard]] QString CheckFieldUndo() {
	const auto &bold = Ui::InputField::kTagBold;
	auto parent = Ui::RpWidget();
	const auto field = CreateField(&parent, BoldBase());
	Insert(field, 7, u"!"_q);
	const auto inserted = ExpectExport(
		field,
		{ u"ab bold! cd"_q, { { 3, 4, bold } } });
	field->undo();
	const auto uncorrected = ExpectExport(
		field,
		{ u"ab bold! cd"_q, { { 3, 5, bold } } });
	field->undo();
	return FirstFailure({
		inserted,
		uncorrected,
		ExpectExport(field, BoldBase()),
	});
}

[[nodiscard]] not_null<Ui::InputField*> CreateInputMethodField(
		not_null<QWidget*> parent,
		const InputMethodRow &row) {
	const auto result = CreateField(parent, row.initial);
	if (row.breakAt >= 0) {
		Insert(result, row.breakAt, u"\n"_q);
	}
	if (row.maxLength > 0) {
		result->setMaxLength(row.maxLength);
	}
	if (row.setup) {
		row.setup(result);
	}
	return result;
}

[[nodiscard]] bool IsSplice(
		QStringView before,
		QStringView after,
		int position,
		int removed,
		int added) {
	return (position >= 0)
		&& (removed >= 0)
		&& (added >= 0)
		&& (position + removed <= before.size())
		&& (position + added <= after.size())
		&& (before.first(position) == after.first(position))
		&& (before.sliced(position + removed)
			== after.sliced(position + added));
}

void AddRowFailure(
		QStringList &failures,
		const InputMethodRow &row,
		const QString &failure) {
	if (!failure.isEmpty()) {
		failures.push_back(QString::fromLatin1(row.name) + u": "_q + failure);
	}
}

[[nodiscard]] QString CheckInputMethodCommits() {
	const auto &bold = Ui::InputField::kTagBold;
	const auto &italic = Ui::InputField::kTagItalic;
	const auto edge = TextWithTags{ u"a\U0001D400Z"_q, { { 1, 2, bold } } };
	const auto tail = TextWithTags{ u"a\U0001D400b"_q, { { 1, 2, bold } } };
	const auto end = TextWithTags{ u"a\U0001D400"_q, { { 1, 2, bold } } };
	const auto mixed = TextWithTags{
		u"a\U0001D400\U0001D402Z"_q,
		{ { 1, 2, bold }, { 3, 2, italic } },
	};
	const auto rows = std::vector<InputMethodRow>{
		{
			.name = "letter",
			.initial = edge,
			.breakAt = 3,
			.commit = u"\U0001D401"_q,
			.expected = { u"a\U0001D400\U0001D401\nZ"_q, { { 1, 4, bold } } },
		},
		{
			.name = "space",
			.initial = edge,
			.breakAt = 3,
			.commit = u" "_q,
			.expected = { u"a\U0001D400 \nZ"_q, { { 1, 2, bold } } },
		},
		{
			.name = "exclamation",
			.initial = edge,
			.breakAt = 3,
			.commit = u"!"_q,
			.expected = { u"a\U0001D400!\nZ"_q, { { 1, 2, bold } } },
		},
		{
			.name = "ascii",
			.initial = edge,
			.breakAt = 3,
			.commit = u"B"_q,
			.expected = { u"a\U0001D400B\nZ"_q, { { 1, 3, bold } } },
		},
		{
			.name = "symbol-before-text",
			.initial = tail,
			.commit = u"\U0001D6C1"_q,
			.expected = { u"a\U0001D400\U0001D6C1b"_q, { { 1, 2, bold } } },
		},
		{
			.name = "symbol-at-end",
			.initial = end,
			.commit = u"\U0001D6C1"_q,
			.expected = { u"a\U0001D400\U0001D6C1"_q, { { 1, 2, bold } } },
		},
		{
			.name = "letters-before-italic",
			.initial = mixed,
			.breakAt = 5,
			.commit = u"abc"_q,
			.expected = {
				u"a\U0001D400abc\U0001D402\nZ"_q,
				{ { 1, 5, bold }, { 6, 2, italic } },
			},
		},
		{
			.name = "preedit",
			.initial = edge,
			.breakAt = 3,
			.preedit = u"draft"_q,
			.expected = { u"a\U0001D400\nZ"_q, { { 1, 2, bold } } },
		},
		{
			.name = "preedit-no-newlines",
			.initial = edge,
			.breakAt = 3,
			.preedit = u"draft"_q,
			.expected = { u"a\U0001D400\nZ"_q, { { 1, 2, bold } } },
			.setup = [](not_null<Ui::InputField*> field) {
				field->setMode(Ui::InputField::Mode::NoNewlines);
			},
		},
		{
			.name = "selection",
			.initial = tail,
			.till = 4,
			.commit = u"x"_q,
			.expected = { u"a\U0001D400x"_q, { { 1, 3, bold } } },
		},
		{
			.name = "relative",
			.initial = tail,
			.from = 4,
			.till = 4,
			.commit = u"x"_q,
			.replacementStart = -1,
			.replacementLength = 1,
			.expected = { u"a\U0001D400x"_q, { { 1, 2, bold } } },
		},
		{
			.name = "gap-limit",
			.initial = { u"abcde"_q },
			.maxLength = 5,
			.till = 4,
			.commit = u"WXYZ"_q,
			.replacementStart = -2,
			.replacementLength = 1,
			.expected = { u"aWXce"_q },
		},
	};
	auto failures = QStringList();
	for (const auto &row : rows) {
		auto parent = Ui::RpWidget();
		const auto field = CreateInputMethodField(&parent, row);
		SendInputMethod(field, row);
		AddRowFailure(failures, row, ExpectExport(field, row.expected));
	}
	return failures.join(u"; "_q);
}

[[nodiscard]] InputMethodAttributes MoveToFirstBlock(
		not_null<Ui::InputField*>) {
	return {
		QInputMethodEvent::Attribute(QInputMethodEvent::Selection, -2, 0),
	};
}

[[nodiscard]] InputMethodAttributes MimeDataAttributes(QMimeData *payload) {
	return {
		QInputMethodEvent::Attribute(
			QInputMethodEvent::MimeData,
			0,
			0,
			QVariant::fromValue(payload)),
	};
}

[[nodiscard]] auto RejectedMimeAt(int position)
-> Fn<InputMethodAttributes(not_null<Ui::InputField*>)> {
	return [=](not_null<Ui::InputField*> field) {
		return MimeDataAttributes(new RejectedMime(field, position));
	};
}

[[nodiscard]] auto ServicesPayload(TextWithTags payload)
-> Fn<InputMethodAttributes(not_null<Ui::InputField*>)> {
	return [=](not_null<Ui::InputField*> field) {
		const auto data = TextUtilities::MimeDataFromText(
			TextWithTags(payload)).release();
		data->setParent(field);
		return MimeDataAttributes(data);
	};
}

[[nodiscard]] Fn<void(not_null<Ui::InputField*>)> MoveCaretOnUndoAvailable(
		int position) {
	return [=](not_null<Ui::InputField*> field) {
		const auto kept = field->lifetime().make_state<QTextCursor>();
		QObject::connect(
			field->rawTextEdit(),
			&QTextEdit::undoAvailable,
			field,
			[=] {
				*kept = field->textCursor();
				Select(field, position, position);
			},
			Qt::SingleShotConnection);
	};
}

void QueryPasteOnCommand(not_null<Ui::InputField*> field) {
	QGuiApplication::clipboard()->setText(u"clipboard"_q);
	const auto edit = field->rawTextEdit();
	QObject::connect(
		edit->document(),
		&QTextDocument::undoCommandAdded,
		field,
		[=] { edit->canPaste(); },
		Qt::SingleShotConnection);
}

[[nodiscard]] QString CheckInputMethodChanges() {
	const auto &bold = Ui::InputField::kTagBold;
	const auto &italic = Ui::InputField::kTagItalic;
	const auto gap = InputMethodRow{
		.name = "gap",
		.initial = { u"abcde"_q },
		.till = 4,
		.commit = u"WXYZ"_q,
		.replacementStart = -2,
		.replacementLength = 1,
		.expected = { u"aWXYZce"_q },
		.changes = u"(3,1,0),(1,1,4)"_q,
	};
	auto limited = gap;
	limited.name = "gap-limit";
	limited.maxLength = 5;
	limited.expected = { u"aWXce"_q };
	limited.changes = u"(3,1,0),(1,1,2)"_q;
	auto undone = limited;
	undone.name = "undo-redo";
	undone.undone = TextWithTags{ u"abcde"_q };
	undone.changes = u"(3,1,0),(1,1,2),(1,3,3),(1,3,3)"_q;
	const auto emptyRelative = InputMethodRow{
		.name = "empty-relative",
		.initial = { u"abcXYZ"_q },
		.from = 1,
		.till = 2,
		.replacementStart = 3,
		.replacementLength = 3,
		.expected = { u"acXYZ"_q },
		.changes = u"(1,1,0)"_q,
	};
	auto emptyRelativeBreak = emptyRelative;
	emptyRelativeBreak.name = "empty-relative-break";
	emptyRelativeBreak.breakAt = 3;
	emptyRelativeBreak.expected = { u"ac\nXYZ"_q };
	const auto canPaste = InputMethodRow{
		.name = "can-paste",
		.initial = { u"aXYcd"_q },
		.maxLength = 6,
		.from = 1,
		.till = 1,
		.commit = u"XY"_q,
		.expected = { u"aXXYcd"_q },
		.changes = u"(1,0,1)"_q,
		.setup = QueryPasteOnCommand,
	};
	auto canPasteBold = canPaste;
	canPasteBold.name = "can-paste-bold";
	canPasteBold.initial = { u"aXbcd"_q, { { 1, 1, bold } } };
	canPasteBold.maxLength = 5;
	canPasteBold.commit = u"X"_q;
	canPasteBold.expected = canPasteBold.initial;
	canPasteBold.changes = QString();
	const auto full = InputMethodRow{
		.name = "full",
		.initial = { u"ABCDE"_q },
		.maxLength = 5,
		.from = 1,
		.till = 1,
		.commit = u"x"_q,
		.expected = { u"ABCDE"_q },
	};
	const auto collapsedQuote = InputMethodRow{
		.name = "collapsed-quote",
		.from = 0,
		.till = 0,
		.commit = u"x"_q,
		.expected = {
			u"x\nP\nQ"_q,
			{ { 2, 1, Ui::InputField::kTagBlockquoteCollapsed } },
		},
		.changes = u"(0,0,2)"_q,
		.setup = [](not_null<Ui::InputField*> field) {
			field->setCustomTextContext({});
			field->setTextWithTags(
				{
					u"PQ"_q,
					{ { 0, 1, Ui::InputField::kTagBlockquoteCollapsed } },
				},
				Ui::InputField::HistoryAction::Clear);
		},
	};
	const auto keycap = InputMethodRow{
		.name = "keycap",
		.initial = { u"abc\uFE0F\u20E3 tail"_q },
		.commit = u"#"_q,
		.expected = { u"abc#\uFE0F\u20E3 tail"_q },
		.changes = u"(3,2,1)"_q,
	};
	const auto keycapSameLength = InputMethodRow{
		.name = "keycap-same-length",
		.initial = { u"A\uFE0F\u20E3Z"_q },
		.from = 1,
		.till = 1,
		.commit = u"x1"_q,
		.expected = { u"Ax1\uFE0F\u20E3Z"_q },
		.changes = u"(1,2,2)"_q,
	};
	const auto mimeRejected = InputMethodRow{
		.name = "mime-rejected",
		.initial = { u"aXbcd"_q, { { 1, 1, bold } } },
		.from = 1,
		.till = 1,
		.commit = u"X"_q,
		.expected = { u"aXXbcd"_q, { { 2, 1, bold } } },
		.changes = u"(1,0,1)"_q,
		.splices = false,
		.attributes = RejectedMimeAt(1),
	};
	const auto tagged = TextWithTags{ u"xy"_q, { { 0, 2, italic } } };
	const auto servicesTagged = InputMethodRow{
		.name = "services-tagged",
		.initial = BoldBase(),
		.from = 4,
		.till = 6,
		.commit = u"xy"_q,
		.expected = {
			u"ab bxyd cd"_q,
			{ { 3, 1, bold }, { 4, 2, italic }, { 6, 1, bold } },
		},
		.undone = BoldBase(),
		.changes = u"(4,2,2),(4,2,2),(4,2,2)"_q,
		.setup = [](not_null<Ui::InputField*> field) {
			field->setTagMimeProcessor(IdentityMimeTag);
		},
		.attributes = ServicesPayload(tagged),
	};
	const auto servicesPlain = InputMethodRow{
		.name = "services-plain",
		.initial = BoldBase(),
		.from = 6,
		.till = 7,
		.commit = u"x!"_q,
		.expected = { u"ab bolx! cd"_q, { { 3, 4, bold } } },
		.changes = u"(6,1,2)"_q,
		.attributes = ServicesPayload({ u"x!"_q }),
	};
	const auto refusedLetter = InputMethodRow{
		.name = "services-refused-letter",
		.initial = { u"a\U0001D400Z"_q, { { 1, 2, bold } } },
		.breakAt = 3,
		.commit = u"\U0001D401"_q,
		.expected = { u"a\U0001D400\U0001D401\nZ"_q, { { 1, 4, bold } } },
		.changes = u"(3,0,2)"_q,
		.attributes = RejectedMimeAt(3),
	};
	auto refusedSpace = refusedLetter;
	refusedSpace.name = "services-refused-space";
	refusedSpace.commit = u" "_q;
	refusedSpace.expected = { u"a\U0001D400 \nZ"_q, { { 1, 2, bold } } };
	refusedSpace.changes = u"(3,0,1)"_q;
	const auto typed = [](InputMethodRow row, const char *name) {
		row.name = name;
		row.typed = true;
		return row;
	};
	const auto committed = [](InputMethodRow row, const char *name) {
		row.name = name;
		row.splices = true;
		row.attributes = nullptr;
		return row;
	};
	const auto pasted = [&](
			InputMethodRow row,
			const char *name,
			TextWithTags text) {
		row.paste = std::move(text);
		return committed(std::move(row), name);
	};
	const auto rows = std::vector<InputMethodRow>{
		{
			.name = "caret",
			.initial = { u"a\U0001D400b"_q, { { 1, 2, bold } } },
			.commit = u"x"_q,
			.expected = { u"a\U0001D400xb"_q, { { 1, 3, bold } } },
			.changes = u"(3,0,1)"_q,
		},
		{
			.name = "preedit",
			.initial = { u"a\U0001D400Z"_q, { { 1, 2, bold } } },
			.breakAt = 3,
			.preedit = u"draft"_q,
			.expected = { u"a\U0001D400\nZ"_q, { { 1, 2, bold } } },
			.changes = u"(4,0,0)"_q,
		},
		gap,
		limited,
		undone,
		emptyRelative,
		emptyRelativeBreak,
		{
			.name = "empty-relative-cross-block",
			.initial = { u"PQabc"_q },
			.breakAt = 2,
			.from = 4,
			.till = 5,
			.replacementStart = -4,
			.replacementLength = 99,
			.expected = { u"PQ\nac"_q },
			.changes = u"(4,1,0)"_q,
			.setup = [](not_null<Ui::InputField*> field) {
				field->setMode(Ui::InputField::Mode::NoNewlines);
			},
		},
		full,
		collapsedQuote,
		{
			.name = "null-cursor",
			.initial = { u"abc"_q },
			.commit = u"x"_q,
			.expected = { u"abc"_q },
			.setup = [](not_null<Ui::InputField*> field) {
				field->setTextCursor(QTextCursor());
			},
		},
		keycap,
		keycapSameLength,
		mimeRejected,
		canPaste,
		canPasteBold,
		{
			.name = "undo-available",
			.initial = { u"abXXcde"_q },
			.maxLength = 7,
			.from = 1,
			.till = 2,
			.commit = u"XX"_q,
			.expected = { u"aXXcXde"_q },
			.changes = u"(1,1,0),(4,0,1)"_q,
			.setup = MoveCaretOnUndoAvailable(5),
		},
		{
			.name = "width-change",
			.initial = { u"abcdefg"_q },
			.maxLength = 7,
			.from = 1,
			.till = 2,
			.commit = u"WX"_q,
			.replacementLength = 3,
			.expected = { u"acdeWfg"_q },
			.changes = u"(1,1,0),(4,0,1)"_q,
			.setup = MoveCaretOnUndoAvailable(5),
		},
		{
			.name = "fresh-ambiguity",
			.initial = { u"aXbc"_q },
			.breakAt = 2,
			.maxLength = 5,
			.from = 4,
			.till = 5,
			.commit = u"XX"_q,
			.replacementStart = -2,
			.expected = { u"aXX\nb"_q },
			.changes = u"(4,1,0),(2,0,1)"_q,
			.setup = [](not_null<Ui::InputField*> field) {
				field->document()->clearUndoRedoStacks();
			},
		},
		{
			.name = "merged-edit",
			.initial = { u"PabcY"_q },
			.breakAt = 1,
			.commit = u"b"_q,
			.expected = { u"P\nabcdY"_q },
			.changes = u"(3,2,3),(2,0,0)"_q,
			.setup = [](not_null<Ui::InputField*> field) {
				const auto document = field->rawTextEdit()->document();
				QObject::connect(
					document,
					&QTextDocument::undoCommandAdded,
					field,
					[=] {
						auto cursor = QTextCursor(document);
						cursor.setPosition(4);
						cursor.setPosition(6, QTextCursor::KeepAnchor);
						cursor.insertText(u"cd"_q);
					},
					Qt::SingleShotConnection);
			},
			.attributes = MoveToFirstBlock,
		},
		{
			.name = "capped",
			.initial = { u"Pwrod tail"_q },
			.breakAt = 1,
			.maxLength = 11,
			.commit = u"x"_q,
			.expected = { u"P\nwrod tail"_q },
			.changes = u"(2,0,0)"_q,
			.attributes = MoveToFirstBlock,
		},
		typed(full, "full-typed"),
		typed(collapsedQuote, "collapsed-quote-typed"),
		typed(keycap, "keycap-typed"),
		typed(keycapSameLength, "keycap-same-length-typed"),
		servicesTagged,
		pasted(servicesTagged, "services-tagged-paste", tagged),
		servicesPlain,
		pasted(servicesPlain, "services-plain-paste", { u"x!"_q }),
		refusedLetter,
		committed(refusedLetter, "services-refused-letter-commit"),
		refusedSpace,
		committed(refusedSpace, "services-refused-space-commit"),
		committed(mimeRejected, "mime-rejected-commit"),
	};
	auto failures = QStringList();
	for (const auto &row : rows) {
		auto parent = Ui::RpWidget();
		const auto field = CreateInputMethodField(&parent, row);
		const auto document = field->rawTextEdit()->document();
		auto documents = QStringList{ document->toRawText() };
		auto changes = std::vector<std::array<int, 3>>();
		auto lifetime = rpl::lifetime();
		field->documentContentsChanges(
		) | rpl::on_next([&](const auto &value) {
			const auto &[position, removed, added] = value;
			changes.push_back({ position, removed, added });
			documents.push_back(document->toRawText());
		}, lifetime);
		if (row.typed) {
			Insert(field, row.from, row.commit);
		} else if (row.paste) {
			Paste(field, row.from, row.till, TextWithTags(*row.paste));
		} else {
			SendInputMethod(field, row);
		}
		if (row.undone) {
			field->undo();
			AddRowFailure(failures, row, ExpectExport(field, *row.undone));
			field->redo();
		}
		const auto exported = field->getTextWithTags();
		auto list = QStringList();
		auto spliced = true;
		for (auto i = 0; i != int(changes.size()); ++i) {
			const auto &[position, removed, added] = changes[i];
			list.push_back(u"(%1,%2,%3)"_q
				.arg(position)
				.arg(removed)
				.arg(added));
			spliced = spliced
				&& (!row.splices
					|| IsSplice(
						documents[i],
						documents[i + 1],
						position,
						removed,
						added));
		}
		const auto got = list.join(u","_q);
		const auto ok = (exported == row.expected)
			&& (got == row.changes)
			&& spliced;
		AddRowFailure(
			failures,
			row,
			ok
				? QString()
				: u"got %1 with [%2]%3, expected %4 with [%5]"_q.arg(
					Describe(exported),
					got,
					spliced ? QString() : u" (not a splice)"_q,
					Describe(row.expected),
					row.changes));
	}
	return failures.join(u"; "_q);
}

[[nodiscard]] Ui::Text::String MakeText(const QString &text) {
	return Ui::Text::String(st::defaultTextStyle, text);
}

[[nodiscard]] QRect Band(int line, int count = 1) {
	return QRect(
		kCanvas.x(),
		kOrigin.y() + line * kLineHeight,
		kCanvas.width(),
		count * kLineHeight);
}

[[nodiscard]] QImage Blank() {
	auto result = QImage(kCanvas.size(), QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	return result;
}

[[nodiscard]] QImage Render(
		const Ui::Text::String &text,
		const Ui::Text::PaintContext &context,
		QColor pen = Qt::black) {
	auto result = Blank();
	auto p = QPainter(&result);
	p.setPen(pen);
	text.draw(p, context);
	p.end();
	return result;
}

[[nodiscard]] QRect ChangedBounds(
		const QImage &a,
		const QImage &b,
		QRect area = kCanvas) {
	auto result = QRect();
	for (auto y = area.top(); y <= area.bottom(); ++y) {
		for (auto x = area.left(); x <= area.right(); ++x) {
			if (a.pixel(x, y) != b.pixel(x, y)) {
				result |= QRect(x, y, 1, 1);
			}
		}
	}
	return result;
}

[[nodiscard]] QRect InkBounds(const QImage &image, QRect area = kCanvas) {
	return ChangedBounds(image, Blank(), area);
}

[[nodiscard]] QString Describe(QRect rect) {
	return rect.isEmpty()
		? u"none"_q
		: u"(%1,%2,%3,%4)"_q
			.arg(rect.x())
			.arg(rect.y())
			.arg(rect.width())
			.arg(rect.height());
}

[[nodiscard]] QString ExpectBounds(
		const QString &what,
		QRect bounds,
		QRect expected) {
	return (bounds == expected)
		? QString()
		: u"%1 %2, expected %3"_q
			.arg(what)
			.arg(Describe(bounds))
			.arg(Describe(expected));
}

[[nodiscard]] QString ExpectInk(
		const QString &what,
		const QImage &image,
		QRect area = kCanvas) {
	return InkBounds(image, area).isEmpty()
		? (what + u" has no ink"_q)
		: QString();
}

[[nodiscard]] QString ExpectInkWithin(
		const QString &what,
		const QImage &image,
		QRect area) {
	const auto ink = InkBounds(image);
	return ink.isEmpty()
		? (what + u" has no ink"_q)
		: !area.contains(ink)
		? u"%1 ink %2 outside %3"_q
			.arg(what)
			.arg(Describe(ink))
			.arg(Describe(area))
		: QString();
}

[[nodiscard]] QString ExpectPixel(
		const QString &what,
		const QImage &image,
		QPoint point,
		QRgb expected) {
	const auto pixel = image.pixel(point);
	return (pixel == expected)
		? QString()
		: u"%1 pixel at (%2,%3) is %4, expected %5"_q
			.arg(what)
			.arg(point.x())
			.arg(point.y())
			.arg(pixel, 8, 16, QChar('0'))
			.arg(expected, 8, 16, QChar('0'));
}

[[nodiscard]] QString Describe(const SymbolHit &hit) {
	return u"(%1,%2,%3)"_q
		.arg(hit.symbol)
		.arg(hit.upon ? u"true"_q : u"false"_q)
		.arg(hit.after ? u"true"_q : u"false"_q);
}

[[nodiscard]] QString ExpectHit(
		const Ui::Text::String &text,
		int width,
		QPoint point,
		const SymbolHit &expected) {
	auto request = Ui::Text::StateRequest();
	request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
	const auto state = text.getState(point, width, request);
	const auto hit = SymbolHit{
		.symbol = int(state.symbol),
		.upon = state.uponSymbol,
		.after = state.afterSymbol,
	};
	return (hit == expected)
		? QString()
		: u"(%1,%2) at width %3 hit %4, expected %5"_q
			.arg(point.x())
			.arg(point.y())
			.arg(width)
			.arg(Describe(hit))
			.arg(Describe(expected));
}

[[nodiscard]] QString Describe(
		const Ui::Text::String::DimensionsResult &dimensions) {
	auto lines = QStringList();
	for (const auto width : dimensions.lineWidths) {
		lines.push_back(QString::number(width));
	}
	return u"%1x%2 [%3]"_q
		.arg(dimensions.width)
		.arg(dimensions.height)
		.arg(lines.join(u","_q));
}

[[nodiscard]] QString ExpectDimensions(
		const Ui::Text::String &text,
		int width,
		const Ui::Text::String::DimensionsResult &expected) {
	const auto counted = text.countDimensions(
		Ui::Text::SimpleGeometry(width, 0, 0, false),
		{ .lineWidths = true });
	return (counted.width == expected.width
		&& counted.height == expected.height
		&& counted.lineWidths == expected.lineWidths)
		? QString()
		: u"at width %1 counted %2, expected %3"_q
			.arg(width)
			.arg(Describe(counted))
			.arg(Describe(expected));
}

[[nodiscard]] QString CheckWrapping() {
	const auto text = MakeText(u"alpha beta gamma"_q);
	const auto image = Render(
		text,
		{ .position = kOrigin, .availableWidth = 65 });
	return FirstFailure({
		ExpectDimensions(text, 65, { 63, 36, { 63, 45 } }),
		ExpectHit(text, 65, QPoint(2, 5), { 0, true, false }),
		ExpectHit(text, 65, QPoint(66, 5), { 10, false, true }),
		ExpectHit(text, 65, QPoint(2, 23), { 11, true, false }),
		ExpectInk(u"line 0"_q, image, Band(0)),
		ExpectInk(u"line 1"_q, image, Band(1)),
		ExpectInkWithin(u"wrapped render"_q, image, Band(0, 2)),
	});
}

[[nodiscard]] QString CheckTrailingSpaces() {
	const auto text = MakeText(u"alpha    beta"_q);
	auto context = Ui::Text::PaintContext{
		.position = kOrigin,
		.availableWidth = 35,
	};
	const auto plain = Render(text, context);
	context.selection = TextSelection(0, 9);
	const auto full = Render(text, context);
	context.fullWidthSelection = false;
	const auto partial = Render(text, context);
	const auto exported = MakeText(u"alpha   "_q).toString();
	return FirstFailure({
		ExpectDimensions(text, 35, { 33, 36, { 33, 27 } }),
		ExpectHit(text, 35, QPoint(36, 5), { 8, false, true }),
		ExpectHit(text, 35, QPoint(2, 23), { 9, true, false }),
		ExpectInk(u"line 0"_q, plain, Band(0)),
		ExpectBounds(
			u"selection [0,9) without full width changed"_q,
			ChangedBounds(plain, partial),
			QRect(4, 4, 33, 18)),
		ExpectBounds(
			u"full-width selection [0,9) changed"_q,
			ChangedBounds(plain, full),
			QRect(4, 4, 35, 18)),
		(exported == u"alpha"_q)
			? QString()
			: u"'alpha   ' exported as '%1'"_q.arg(exported),
	});
}

[[nodiscard]] QString CheckClipping() {
	const auto text = MakeText(u"alpha beta gamma delta epsilon"_q);
	auto context = Ui::Text::PaintContext{
		.position = kOrigin,
		.availableWidth = 51,
	};
	const auto full = Render(text, context);
	context.clip = Band(1);
	const auto clipped = Render(text, context);
	return FirstFailure({
		ExpectInk(u"full render line 0"_q, full, Band(0)),
		ExpectInk(u"full render line 1"_q, full, Band(1)),
		ExpectInk(u"full render line 2"_q, full, Band(2)),
		ExpectInk(u"full render line 3"_q, full, Band(3)),
		ExpectInk(u"full render line 4"_q, full, Band(4)),
		ExpectBounds(
			u"clipped render changed on line 1"_q,
			ChangedBounds(full, clipped, Band(1)),
			QRect()),
		ExpectInkWithin(u"clipped render"_q, clipped, Band(1)),
	});
}

[[nodiscard]] QString CheckElision() {
	const auto text = MakeText(u"alpha beta gamma delta epsilon"_q);
	auto context = Ui::Text::PaintContext{
		.position = kOrigin,
		.availableWidth = 51,
	};
	const auto full = Render(text, context);
	context.elisionLines = 1;
	const auto elided = Render(text, context);
	context.availableWidth = text.maxWidth();
	const auto fittingElided = Render(text, context);
	context.elisionLines = 0;
	const auto fitting = Render(text, context);
	const auto visible = Render(
		MakeText(u"alpha ..."_q),
		{ .position = kOrigin });
	const auto word = QRect(0, 4, 4 + 33, 18);
	const auto tail = QRect(4 + 36, 4, 50 - 36, 18);
	return FirstFailure({
		ExpectInkWithin(u"elided render"_q, elided, QRect(0, 4, 4 + 51, 18)),
		ExpectInk(u"full render first word"_q, full, word),
		ExpectBounds(
			u"elided render changed the first word"_q,
			ChangedBounds(full, elided, word),
			QRect()),
		ExpectInk(u"elided render after the first word"_q, elided, tail),
		ExpectBounds(
			u"elided render differs from 'alpha ...'"_q,
			ChangedBounds(visible, elided),
			QRect()),
		ExpectBounds(
			u"full render ink after the first word"_q,
			InkBounds(full, tail),
			QRect()),
		ExpectInk(u"render at max width"_q, fitting),
		ExpectBounds(
			u"elision at max width changed"_q,
			ChangedBounds(fitting, fittingElided),
			QRect()),
	});
}

[[nodiscard]] QString CheckBidiRuns() {
	const auto ltr = MakeText(u"abc \u0627\u0628\u062A def"_q);
	const auto rtl = MakeText(u"\u0627\u0628\u062A abc \u062B\u062C\u062D"_q);
	auto context = Ui::Text::PaintContext{
		.position = kOrigin,
		.availableWidth = 70,
	};
	const auto plain = Render(rtl, context);
	context.selection = TextSelection(4, 7);
	const auto selected = Render(rtl, context);
	const auto firstWord = QRect(4 + 50, 4, 69 - 50, 18);
	const auto lastWord = QRect(4, 4, 23, 18);
	return FirstFailure({
		ExpectHit(ltr, 66, QPoint(2, 5), { 0, true, false }),
		ExpectHit(ltr, 66, QPoint(32, 5), { 6, true, false }),
		ExpectHit(ltr, 66, QPoint(48, 5), { 8, true, false }),
		ExpectHit(rtl, 70, QPoint(2, 5), { 10, true, true }),
		ExpectHit(rtl, 70, QPoint(28, 5), { 4, true, false }),
		ExpectHit(rtl, 70, QPoint(67, 5), { 0, true, true }),
		ExpectInk(u"plain render of [0,3)"_q, plain, firstWord),
		ExpectInk(u"plain render of [8,11)"_q, plain, lastWord),
		ExpectBounds(
			u"selection [4,7) changed"_q,
			ChangedBounds(plain, selected),
			QRect(30, 4, 21, 18)),
	});
}

[[nodiscard]] QString CheckSelection() {
	const auto text = MakeText(u"alpha beta gamma"_q);
	auto context = Ui::Text::PaintContext{ .position = kOrigin };
	const auto plain = Render(text, context);
	context.selection = TextSelection(6, 10);
	const auto selected = Render(text, context);
	context.selection = TextSelection(6, 6);
	const auto collapsed = Render(text, context);
	const auto background = QPoint(41, 5);
	return FirstFailure({
		ExpectInk(u"plain render"_q, plain),
		ExpectBounds(
			u"selection [6,10) changed"_q,
			ChangedBounds(plain, selected),
			QRect(4 + 36, 4, 63 - 36, 18)),
		ExpectPixel(
			u"selected"_q,
			selected,
			background,
			st::defaultTextPalette.selectBg->c.rgba()),
		ExpectPixel(
			u"plain"_q,
			plain,
			background,
			QColor(Qt::transparent).rgba()),
		ExpectBounds(
			u"collapsed selection changed"_q,
			ChangedBounds(plain, collapsed),
			QRect()),
	});
}

[[nodiscard]] QString CheckNegativeControls() {
	const auto empty = MakeText(QString());
	const auto text = MakeText(u"alpha beta gamma"_q);
	const auto context = Ui::Text::PaintContext{ .position = kOrigin };
	return FirstFailure({
		ExpectBounds(
			u"empty text ink"_q,
			InkBounds(Render(empty, context)),
			QRect()),
		ExpectBounds(
			u"transparent pen ink"_q,
			InkBounds(Render(text, context, Qt::transparent)),
			QRect()),
		ExpectInk(u"black pen render"_q, Render(text, context)),
	});
}

[[nodiscard]] std::vector<Check> Checks() {
	return {
		{ "entities.known-bare-domain", CheckKnownBareDomain },
		{ "entities.cyrillic-domain", CheckCyrillicDomain },
		{ "entities.unknown-bare-suffix", CheckUnknownBareSuffix },
		{ "entities.explicit-protocol", CheckExplicitProtocol },
		{ "entities.after-supplementary", CheckAfterSupplementary },
		{ "accents.precomposed", CheckPrecomposedAccents },
		{ "accents.cyrillic", CheckCyrillicAccents },
		{ "accents.combining-marks", CheckCombiningMarks },
		{ "accents.unmapped", CheckUnmappedCharacters },
		{ "search.normalization", CheckSearchNormalization },
		{ "field.left-edge", CheckLeftEdgeInsertion },
		{ "field.right-edge", CheckRightEdgeInsertion },
		{ "field.inside-tag", CheckInsertionInsideTag },
		{ "field.mime-tags", CheckMimeTags },
		{ "field.incompatible-formatting", CheckIncompatibleFormatting },
		{ "field.unicode-boundaries", CheckUnicodeBoundaries },
		{ "field.undo", CheckFieldUndo },
		{ "field.input-method", CheckInputMethodCommits },
		{ "field.input-method-changes", CheckInputMethodChanges },
		{ "layout.wrapping", CheckWrapping },
		{ "layout.trailing-spaces", CheckTrailingSpaces },
		{ "layout.clipping", CheckClipping },
		{ "layout.elision", CheckElision },
		{ "layout.bidi-runs", CheckBidiRuns },
		{ "layout.selection", CheckSelection },
		{ "layout.negative-controls", CheckNegativeControls },
	};
}

} // namespace

int main(int argc, char *argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	auto app = QApplication(argc, argv);
	auto baseIntegration = BaseIntegration(argc, argv);
	base::Integration::Set(&baseIntegration);
	auto uiIntegration = UiIntegration();
	Ui::Integration::Set(&uiIntegration);
	const auto failure = StartOffscreenStyle();
	if (!failure.isEmpty()) {
		std::printf("FAIL setup: %s\n", failure.toUtf8().constData());
		return 2;
	}
	Ui::Emoji::Init();
	auto animations = Ui::Animations::Manager();
	return Test::NamedChecks::Run(Checks());
}

namespace crl {

rpl::producer<> on_main_update_requests() {
	return rpl::never<>();
}

} // namespace crl
