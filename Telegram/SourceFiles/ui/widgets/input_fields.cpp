/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/input_fields.h"

#include "ui/widgets/popup_menu.h"
#include "ui/countryinput.h"
#include "ui/emoji_config.h"
#include "emoji_suggestions_data.h"
#include "chat_helpers/emoji_suggestions_helper.h"
#include "chat_helpers/message_field.h" // ConvertTextTagsToEntities
#include "platform/platform_info.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h"
#include "data/data_user.h"
#include "data/data_countries.h" // Data::ValidPhoneCode
#include "mainwindow.h"
#include "numbers.h"
#include "main/main_session.h"
#include "core/application.h"

namespace Ui {
namespace {

constexpr auto kMaxUsernameLength = 32;
constexpr auto kInstantReplaceRandomId = QTextFormat::UserProperty;
constexpr auto kInstantReplaceWhatId = QTextFormat::UserProperty + 1;
constexpr auto kInstantReplaceWithId = QTextFormat::UserProperty + 2;
constexpr auto kReplaceTagId = QTextFormat::UserProperty + 3;
constexpr auto kTagProperty = QTextFormat::UserProperty + 4;
const auto kObjectReplacementCh = QChar(QChar::ObjectReplacementCharacter);
const auto kObjectReplacement = QString::fromRawData(
	&kObjectReplacementCh,
	1);
const auto &kTagBold = InputField::kTagBold;
const auto &kTagItalic = InputField::kTagItalic;
const auto &kTagUnderline = InputField::kTagUnderline;
const auto &kTagStrikeOut = InputField::kTagStrikeOut;
const auto &kTagCode = InputField::kTagCode;
const auto &kTagPre = InputField::kTagPre;
const auto kNewlineChars = QString("\r\n")
	+ QChar(0xfdd0) // QTextBeginningOfFrame
	+ QChar(0xfdd1) // QTextEndOfFrame
	+ QChar(QChar::ParagraphSeparator)
	+ QChar(QChar::LineSeparator);

class InputDocument : public QTextDocument {
public:
	InputDocument(QObject *parent, const style::InputField &st);

protected:
	QVariant loadResource(int type, const QUrl &name) override;

private:
	const style::InputField &_st;
	std::map<QUrl, QVariant> _emojiCache;
	rpl::lifetime _lifetime;

};

InputDocument::InputDocument(QObject *parent, const style::InputField &st)
: QTextDocument(parent)
, _st(st) {
	Ui::Emoji::Updated(
	) | rpl::start_with_next([=] {
		_emojiCache.clear();
	}, _lifetime);
}

QVariant InputDocument::loadResource(int type, const QUrl &name) {
	if (type != QTextDocument::ImageResource
		|| name.scheme() != qstr("emoji")) {
		return QTextDocument::loadResource(type, name);
	}
	const auto i = _emojiCache.find(name);
	if (i != _emojiCache.end()) {
		return i->second;
	}
	auto result = [&] {
		if (const auto emoji = Ui::Emoji::FromUrl(name.toDisplayString())) {
			const auto height = std::max(
				_st.font->height * cIntRetinaFactor(),
				Ui::Emoji::GetSizeNormal());
			return QVariant(Ui::Emoji::SinglePixmap(emoji, height));
		}
		return QVariant();
	}();
	_emojiCache.emplace(name, result);
	return result;
}

bool IsNewline(QChar ch) {
	return (kNewlineChars.indexOf(ch) >= 0);
}

QString GetFullSimpleTextTag(const TextWithTags &textWithTags) {
	const auto &text = textWithTags.text;
	const auto &tags = textWithTags.tags;
	const auto tag = (tags.size() == 1) ? tags[0] : TextWithTags::Tag();
	auto from = 0;
	auto till = int(text.size());
	for (; from != till; ++from) {
		if (!IsNewline(text[from]) && !chIsSpace(text[from])) {
			break;
		}
	}
	while (till != from) {
		if (!IsNewline(text[till - 1]) && !chIsSpace(text[till - 1])) {
			break;
		}
		--till;
	}
	return ((tag.offset <= from)
		&& (tag.offset + tag.length >= till))
		? (tag.id == kTagPre ? kTagCode : tag.id)
		: QString();
}

class TagAccumulator {
public:
	TagAccumulator(TextWithTags::Tags &tags) : _tags(tags) {
	}

	bool changed() const {
		return _changed;
	}

	void feed(const QString &randomTagId, int currentPosition) {
		if (randomTagId == _currentTagId) {
			return;
		}

		if (!_currentTagId.isEmpty()) {
			const auto tag = TextWithTags::Tag {
				_currentStart,
				currentPosition - _currentStart,
				_currentTagId
			};
			if (tag.length > 0) {
				if (_currentTag >= _tags.size()) {
					_changed = true;
					_tags.push_back(tag);
				} else if (_tags[_currentTag] != tag) {
					_changed = true;
					_tags[_currentTag] = tag;
				}
				++_currentTag;
			}
		}
		_currentTagId = randomTagId;
		_currentStart = currentPosition;
	};

	void finish() {
		if (_currentTag < _tags.size()) {
			_tags.resize(_currentTag);
			_changed = true;
		}
	}

private:
	TextWithTags::Tags &_tags;
	bool _changed = false;

	int _currentTag = 0;
	int _currentStart = 0;
	QString _currentTagId;

};

struct TagStartExpression {
	QString tag;
	QString goodBefore;
	QString badAfter;
	QString badBefore;
	QString goodAfter;
};

constexpr auto kTagBoldIndex = 0;
constexpr auto kTagItalicIndex = 1;
//constexpr auto kTagUnderlineIndex = 2;
constexpr auto kTagStrikeOutIndex = 2;
constexpr auto kTagCodeIndex = 3;
constexpr auto kTagPreIndex = 4;
constexpr auto kInvalidPosition = std::numeric_limits<int>::max() / 2;

class TagSearchItem {
public:
	enum class Edge {
		Open,
		Close,
	};

	int matchPosition(Edge edge) const {
		return (_position >= 0) ? _position : kInvalidPosition;
	}

	void applyOffset(int offset) {
		if (_position < offset) {
			_position = -1;
		}
		accumulate_max(_offset, offset);
	}

	void fill(
			const QString &text,
			Edge edge,
			const TagStartExpression &expression) {
		const auto length = text.size();
		const auto &tag = expression.tag;
		const auto tagLength = tag.size();
		const auto isGoodBefore = [&](QChar ch) {
			return expression.goodBefore.isEmpty()
				|| (expression.goodBefore.indexOf(ch) >= 0);
		};
		const auto isBadAfter = [&](QChar ch) {
			return !expression.badAfter.isEmpty()
				&& (expression.badAfter.indexOf(ch) >= 0);
		};
		const auto isBadBefore = [&](QChar ch) {
			return !expression.badBefore.isEmpty()
				&& (expression.badBefore.indexOf(ch) >= 0);
		};
		const auto isGoodAfter = [&](QChar ch) {
			return expression.goodAfter.isEmpty()
				|| (expression.goodAfter.indexOf(ch) >= 0);
		};
		const auto check = [&](Edge edge) {
			if (_position > 0) {
				const auto before = text[_position - 1];
				if ((edge == Edge::Open && !isGoodBefore(before))
					|| (edge == Edge::Close && isBadBefore(before))) {
					return false;
				}
			}
			if (_position + tagLength < length) {
				const auto after = text[_position + tagLength];
				if ((edge == Edge::Open && isBadAfter(after))
					|| (edge == Edge::Close && !isGoodAfter(after))) {
					return false;
				}
			}
			return true;
		};
		const auto edgeIndex = static_cast<int>(edge);
		if (_position >= 0) {
			if (_checked[edgeIndex]) {
				return;
			} else if (check(edge)) {
				_checked[edgeIndex] = true;
				return;
			} else {
				_checked = { { false, false } };
			}
		}
		while (true) {
			_position = text.indexOf(tag, _offset);
			if (_position < 0) {
				_offset = _position = kInvalidPosition;
				break;
			}
			_offset = _position + tagLength;
			if (check(edge)) {
				break;
			} else {
				continue;
			}
		}
		if (_position == kInvalidPosition) {
			_checked = { { true, true } };
		} else {
			_checked = { { false, false } };
			_checked[edgeIndex] = true;
		}
	}

private:
	int _offset = 0;
	int _position = -1;
	std::array<bool, 2> _checked = { { false, false } };

};

const std::vector<TagStartExpression> &TagStartExpressions() {
	static auto cached = std::vector<TagStartExpression> {
		{
			kTagBold,
			TextUtilities::MarkdownBoldGoodBefore(),
			TextUtilities::MarkdownBoldBadAfter(),
			TextUtilities::MarkdownBoldBadAfter(),
			TextUtilities::MarkdownBoldGoodBefore()
		},
		{
			kTagItalic,
			TextUtilities::MarkdownItalicGoodBefore(),
			TextUtilities::MarkdownItalicBadAfter(),
			TextUtilities::MarkdownItalicBadAfter(),
			TextUtilities::MarkdownItalicGoodBefore()
		},
		//{
		//	kTagUnderline,
		//	TextUtilities::MarkdownUnderlineGoodBefore(),
		//	TextUtilities::MarkdownUnderlineBadAfter(),
		//	TextUtilities::MarkdownUnderlineBadAfter(),
		//	TextUtilities::MarkdownUnderlineGoodBefore()
		//},
		{
			kTagStrikeOut,
			TextUtilities::MarkdownStrikeOutGoodBefore(),
			TextUtilities::MarkdownStrikeOutBadAfter(),
			TextUtilities::MarkdownStrikeOutBadAfter(),
			QString(),
		},
		{
			kTagCode,
			TextUtilities::MarkdownCodeGoodBefore(),
			TextUtilities::MarkdownCodeBadAfter(),
			TextUtilities::MarkdownCodeBadAfter(),
			TextUtilities::MarkdownCodeGoodBefore()
		},
		{
			kTagPre,
			TextUtilities::MarkdownPreGoodBefore(),
			TextUtilities::MarkdownPreBadAfter(),
			TextUtilities::MarkdownPreBadAfter(),
			TextUtilities::MarkdownPreGoodBefore()
		},
	};
	return cached;
}

const std::map<QString, int> &TagIndices() {
	static auto cached = std::map<QString, int> {
		{ kTagBold, kTagBoldIndex },
		{ kTagItalic, kTagItalicIndex },
		//{ kTagUnderline, kTagUnderlineIndex },
		{ kTagStrikeOut, kTagStrikeOutIndex },
		{ kTagCode, kTagCodeIndex },
		{ kTagPre, kTagPreIndex },
	};
	return cached;
}

bool DoesTagFinishByNewline(const QString &tag) {
	return (tag == kTagCode);
}

class MarkdownTagAccumulator {
public:
	using Edge = TagSearchItem::Edge;

	MarkdownTagAccumulator(std::vector<InputField::MarkdownTag> *tags)
	: _tags(tags)
	, _expressions(TagStartExpressions())
	, _tagIndices(TagIndices())
	, _items(_expressions.size()) {
	}

	// Here we use the fact that text either contains only emoji
	// { adjustedTextLength = text.size() * (emojiLength - 1) }
	// or contains no emoji at all and can have tag edges in the middle
	// { adjustedTextLength = 0 }.
	//
	// Otherwise we would have to pass emoji positions inside text.
	void feed(
			const QString &text,
			int adjustedTextLength,
			const QString &textTag) {
		if (!_tags) {
			return;
		}
		const auto guard = gsl::finally([&] {
			_currentInternalLength += text.size();
			_currentAdjustedLength += adjustedTextLength;
		});
		if (!textTag.isEmpty()) {
			finishTags();
			return;
		}
		for (auto &item : _items) {
			item = TagSearchItem();
		}
		auto tryFinishTag = _currentTag;
		while (true) {
			for (; tryFinishTag != _currentFreeTag; ++tryFinishTag) {
				auto &tag = (*_tags)[tryFinishTag];
				if (tag.internalLength >= 0) {
					continue;
				}

				const auto i = _tagIndices.find(tag.tag);
				Assert(i != end(_tagIndices));
				const auto tagIndex = i->second;

				const auto atLeastOffset =
					tag.internalStart
					+ tag.tag.size()
					+ 1
					- _currentInternalLength;
				_items[tagIndex].applyOffset(atLeastOffset);

				fillItem(
					tagIndex,
					text,
					Edge::Close);
				if (finishByNewline(tryFinishTag, text, tagIndex)) {
					continue;
				}
				const auto position = matchPosition(tagIndex, Edge::Close);
				if (position < kInvalidPosition) {
					const auto till = position + tag.tag.size();
					finishTag(tryFinishTag, till, true);
					_items[tagIndex].applyOffset(till);
				}
			}
			for (auto i = 0, count = int(_items.size()); i != count; ++i) {
				fillItem(i, text, Edge::Open);
			}
			const auto min = minIndex(Edge::Open);
			if (min < 0) {
				return;
			}
			startTag(matchPosition(min, Edge::Open), _expressions[min].tag);
		}
	}

	void finish() {
		if (!_tags) {
			return;
		}
		finishTags();
		if (_currentTag < _tags->size()) {
			_tags->resize(_currentTag);
		}
	}

private:
	void finishTag(int index, int offsetFromAccumulated, bool closed) {
		Expects(_tags != nullptr);
		Expects(index >= 0 && index < _tags->size());

		auto &tag = (*_tags)[index];
		if (tag.internalLength < 0) {
			tag.internalLength = _currentInternalLength
				+ offsetFromAccumulated
				- tag.internalStart;
			tag.adjustedLength = _currentAdjustedLength
				+ offsetFromAccumulated
				- tag.adjustedStart;
			tag.closed = closed;
		}
		if (index == _currentTag) {
			++_currentTag;
		}
	}
	bool finishByNewline(
			int index,
			const QString &text,
			int tagIndex) {
		Expects(_tags != nullptr);
		Expects(index >= 0 && index < _tags->size());

		auto &tag = (*_tags)[index];

		if (!DoesTagFinishByNewline(tag.tag)) {
			return false;
		}
		const auto endPosition = newlinePosition(
			text,
			std::max(0, tag.internalStart + 1 - _currentInternalLength));
		if (matchPosition(tagIndex, Edge::Close) <= endPosition) {
			return false;
		}
		finishTag(index, endPosition, false);
		return true;
	}
	void finishTags() {
		while (_currentTag != _currentFreeTag) {
			finishTag(_currentTag, 0, false);
		}
	}
	void startTag(int offsetFromAccumulated, const QString &tag) {
		Expects(_tags != nullptr);

		const auto newTag = InputField::MarkdownTag{
			_currentInternalLength + offsetFromAccumulated,
			-1,
			_currentAdjustedLength + offsetFromAccumulated,
			-1,
			false,
			tag
		};
		if (_currentFreeTag < _tags->size()) {
			(*_tags)[_currentFreeTag] = newTag;
		} else {
			_tags->push_back(newTag);
		}
		++_currentFreeTag;
	}
	void fillItem(int index, const QString &text, Edge edge) {
		Expects(index >= 0 && index < _items.size());

		_items[index].fill(text, edge, _expressions[index]);
	}
	int matchPosition(int index, Edge edge) const {
		Expects(index >= 0 && index < _items.size());

		return _items[index].matchPosition(edge);
	}
	int newlinePosition(const QString &text, int offset) const {
		const auto length = text.size();
		if (offset < length) {
			const auto begin = text.data();
			const auto end = begin + length;
			for (auto ch = begin + offset; ch != end; ++ch) {
				if (IsNewline(*ch)) {
					return (ch - begin);
				}
			}
		}
		return kInvalidPosition;
	}
	int minIndex(Edge edge) const {
		auto result = -1;
		auto minPosition = kInvalidPosition;
		for (auto i = 0, count = int(_items.size()); i != count; ++i) {
			const auto position = matchPosition(i, edge);
			if (position < minPosition) {
				minPosition = position;
				result = i;
			}
		}
		return result;
	}
	int minIndexForFinish(const std::vector<int> &indices) const {
		const auto tagIndex = indices[0];
		auto result = -1;
		auto minPosition = kInvalidPosition;
		for (auto i : indices) {
			const auto edge = (i == tagIndex) ? Edge::Close : Edge::Open;
			const auto position = matchPosition(i, edge);
			if (position < minPosition) {
				minPosition = position;
				result = i;
			}
		}
		return result;
	}

	std::vector<InputField::MarkdownTag> *_tags = nullptr;
	const std::vector<TagStartExpression> &_expressions;
	const std::map<QString, int> &_tagIndices;
	std::vector<TagSearchItem> _items;

	int _currentTag = 0;
	int _currentFreeTag = 0;
	int _currentInternalLength = 0;
	int _currentAdjustedLength = 0;

};

template <typename InputClass>
class InputStyle : public QCommonStyle {
public:
	InputStyle() {
		setParent(QCoreApplication::instance());
	}

	void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override {
	}
	QRect subElementRect(SubElement r, const QStyleOption *opt, const QWidget *widget = nullptr) const override {
		switch (r) {
			case SE_LineEditContents:
				const auto w = widget ? qobject_cast<const InputClass*>(widget) : nullptr;
				return w ? w->getTextRect() : QCommonStyle::subElementRect(r, opt, widget);
			break;
		}
		return QCommonStyle::subElementRect(r, opt, widget);
	}

	static InputStyle<InputClass> *instance() {
		if (!_instance) {
			if (!QGuiApplication::instance()) {
				return nullptr;
			}
			_instance = new InputStyle<InputClass>();
		}
		return _instance;
	}

	~InputStyle() {
		_instance = nullptr;
	}

private:
	static InputStyle<InputClass> *_instance;

};

template <typename InputClass>
InputStyle<InputClass> *InputStyle<InputClass>::_instance = nullptr;

template <typename Iterator>
QString AccumulateText(Iterator begin, Iterator end) {
	auto result = QString();
	result.reserve(end - begin);
	for (auto i = end; i != begin;) {
		result.push_back(*--i);
	}
	return result;
}

QTextImageFormat PrepareEmojiFormat(EmojiPtr emoji, const QFont &font) {
	const auto factor = cIntRetinaFactor();
	const auto size = Ui::Emoji::GetSizeNormal();
	const auto width = size + st::emojiPadding * factor * 2;
	const auto height = std::max(QFontMetrics(font).height() * factor, size);
	auto result = QTextImageFormat();
	result.setWidth(width / factor);
	result.setHeight(height / factor);
	result.setName(emoji->toUrl());
	result.setVerticalAlignment(QTextCharFormat::AlignBaseline);
	return result;
}

// Optimization: with null page size document does not re-layout
// on each insertText / mergeCharFormat.
void PrepareFormattingOptimization(not_null<QTextDocument*> document) {
	if (!document->pageSize().isNull()) {
		document->setPageSize(QSizeF(0, 0));
	}
}

void RemoveDocumentTags(
		const style::InputField &st,
		not_null<QTextDocument*> document,
		int from,
		int end) {
	auto cursor = QTextCursor(document->docHandle(), from);
	cursor.setPosition(end, QTextCursor::KeepAnchor);

	auto format = QTextCharFormat();
	format.setProperty(kTagProperty, QString());
	format.setProperty(kReplaceTagId, QString());
	format.setForeground(st.textFg);
	format.setFont(st.font);
	cursor.mergeCharFormat(format);
}

style::font AdjustFont(
		const style::font &font,
		const style::font &original) {
	return (font->size() != original->size()
		|| font->flags() != original->flags())
		? style::font(original->size(), original->flags(), font->family())
		: font;
}

bool IsValidMarkdownLink(const QString &link) {
	return (link.indexOf('.') >= 0) || (link.indexOf(':') >= 0);
}

QTextCharFormat PrepareTagFormat(
		const style::InputField &st,
		QString tag) {
	auto result = QTextCharFormat();
	if (IsValidMarkdownLink(tag)) {
		result.setForeground(st::defaultTextPalette.linkFg);
		result.setFont(st.font);
	} else if (tag == kTagBold) {
		auto semibold = st::semiboldFont;
		if (semibold->size() != st.font->size()
			|| semibold->flags() != st.font->flags()) {
			semibold = style::font(
				st.font->size(),
				st.font->flags(),
				semibold->family());
		}
		result.setForeground(st.textFg);
		result.setFont(AdjustFont(st::semiboldFont, st.font));
	} else if (tag == kTagItalic) {
		result.setForeground(st.textFg);
		result.setFont(st.font->italic());
	} else if (tag == kTagUnderline) {
		result.setForeground(st.textFg);
		result.setFont(st.font->underline());
	} else if (tag == kTagStrikeOut) {
		result.setForeground(st.textFg);
		result.setFont(st.font->strikeout());
	} else if (tag == kTagCode || tag == kTagPre) {
		result.setForeground(st::defaultTextPalette.monoFg);
		result.setFont(AdjustFont(App::monofont(), st.font));
	} else {
		result.setForeground(st.textFg);
		result.setFont(st.font);
	}
	result.setProperty(kTagProperty, tag);
	return result;
}

void ApplyTagFormat(QTextCharFormat &to, const QTextCharFormat &from) {
	to.setProperty(kTagProperty, from.property(kTagProperty));
	to.setProperty(kReplaceTagId, from.property(kReplaceTagId));
	to.setFont(from.font());
	to.setForeground(from.foreground());
}

// Returns the position of the first inserted tag or "changedEnd" value if none found.
int ProcessInsertedTags(
		const style::InputField &st,
		not_null<QTextDocument*> document,
		int changedPosition,
		int changedEnd,
		const TextWithTags::Tags &tags,
		InputField::TagMimeProcessor *processor) {
	int firstTagStart = changedEnd;
	int applyNoTagFrom = changedEnd;
	for (const auto &tag : tags) {
		int tagFrom = changedPosition + tag.offset;
		int tagTo = tagFrom + tag.length;
		accumulate_max(tagFrom, changedPosition);
		accumulate_min(tagTo, changedEnd);
		auto tagId = processor ? processor->tagFromMimeTag(tag.id) : tag.id;
		if (tagTo > tagFrom && !tagId.isEmpty()) {
			accumulate_min(firstTagStart, tagFrom);

			PrepareFormattingOptimization(document);

			if (applyNoTagFrom < tagFrom) {
				RemoveDocumentTags(
					st,
					document,
					applyNoTagFrom,
					tagFrom);
			}
			QTextCursor c(document->docHandle(), 0);
			c.setPosition(tagFrom);
			c.setPosition(tagTo, QTextCursor::KeepAnchor);

			c.mergeCharFormat(PrepareTagFormat(st, tagId));

			applyNoTagFrom = tagTo;
		}
	}
	if (applyNoTagFrom < changedEnd) {
		RemoveDocumentTags(st, document, applyNoTagFrom, changedEnd);
	}

	return firstTagStart;
}

// When inserting a part of text inside a tag we need to have
// a way to know if the insertion replaced the end of the tag
// or it was strictly inside (in the middle) of the tag.
bool WasInsertTillTheEndOfTag(
		QTextBlock block,
		QTextBlock::iterator fragmentIt,
		int insertionEnd) {
	const auto format = fragmentIt.fragment().charFormat();
	const auto insertTagName = format.property(kTagProperty);
	while (true) {
		for (; !fragmentIt.atEnd(); ++fragmentIt) {
			const auto fragment = fragmentIt.fragment();
			const auto position = fragment.position();
			const auto outsideInsertion = (position >= insertionEnd);
			if (outsideInsertion) {
				const auto format = fragment.charFormat();
				return (format.property(kTagProperty) != insertTagName);
			}
			const auto end = position + fragment.length();
			const auto notFullFragmentInserted = (end > insertionEnd);
			if (notFullFragmentInserted) {
				return false;
			}
		}
		if (block.isValid()) {
			fragmentIt = block.begin();
			block = block.next();
		} else {
			break;
		}
	}
	// Insertion goes till the end of the text => not strictly inside a tag.
	return true;
}

struct FormattingAction {
	enum class Type {
		Invalid,
		InsertEmoji,
		TildeFont,
		RemoveTag,
		RemoveNewline,
		ClearInstantReplace,
	};

	Type type = Type::Invalid;
	EmojiPtr emoji = nullptr;
	bool isTilde = false;
	QString tildeTag;
	int intervalStart = 0;
	int intervalEnd = 0;

};

QString ExpandCustomLinks(const TextWithTags &text) {
	const auto entities = ConvertTextTagsToEntities(text.tags);
	auto &&urls = ranges::make_iterator_range(
		entities.begin(),
		entities.end()
	) | ranges::view::filter([](const EntityInText &entity) {
		return entity.type() == EntityType::CustomUrl;
	});
	const auto &original = text.text;
	if (urls.begin() == urls.end()) {
		return original;
	}
	auto result = QString();
	auto offset = 0;
	for (const auto &entity : urls) {
		const auto till = entity.offset() + entity.length();
		if (till > offset) {
			result.append(original.midRef(offset, till - offset));
		}
		result.append(qstr(" (")).append(entity.data()).append(')');
		offset = till;
	}
	if (original.size() > offset) {
		result.append(original.midRef(offset));
	}
	return result;
}

} // namespace

const QString InputField::kTagBold = qsl("**");
const QString InputField::kTagItalic = qsl("__");
const QString InputField::kTagUnderline = qsl("^^"); // Not for Markdown.
const QString InputField::kTagStrikeOut = qsl("~~");
const QString InputField::kTagCode = qsl("`");
const QString InputField::kTagPre = qsl("```");

class InputField::Inner final : public QTextEdit {
public:
	Inner(not_null<InputField*> parent) : QTextEdit(parent) {
	}

protected:
	bool viewportEvent(QEvent *e) override {
		return outer()->viewportEventInner(e);
	}
	void focusInEvent(QFocusEvent *e) override {
		return outer()->focusInEventInner(e);
	}
	void focusOutEvent(QFocusEvent *e) override {
		return outer()->focusOutEventInner(e);
	}
	void keyPressEvent(QKeyEvent *e) override {
		return outer()->keyPressEventInner(e);
	}
	void contextMenuEvent(QContextMenuEvent *e) override {
		return outer()->contextMenuEventInner(e);
	}
	void dropEvent(QDropEvent *e) override {
		return outer()->dropEventInner(e);
	}
	void inputMethodEvent(QInputMethodEvent *e) override {
		return outer()->inputMethodEventInner(e);
	}

	bool canInsertFromMimeData(const QMimeData *source) const override {
		return outer()->canInsertFromMimeDataInner(source);
	}
	void insertFromMimeData(const QMimeData *source) override {
		return outer()->insertFromMimeDataInner(source);
	}
	QMimeData *createMimeDataFromSelection() const override {
		return outer()->createMimeDataFromSelectionInner();
	}

private:
	not_null<InputField*> outer() const {
		return static_cast<InputField*>(parentWidget());
	}
	friend class InputField;

};

void InsertEmojiAtCursor(QTextCursor cursor, EmojiPtr emoji) {
	const auto currentFormat = cursor.charFormat();
	auto format = PrepareEmojiFormat(emoji, currentFormat.font());
	ApplyTagFormat(format, currentFormat);
	cursor.insertText(kObjectReplacement, format);
}

void InstantReplaces::add(const QString &what, const QString &with) {
	auto node = &reverseMap;
	for (auto i = what.end(), b = what.begin(); i != b;) {
		node = &node->tail.emplace(*--i, Node()).first->second;
	}
	node->text = with;
	accumulate_max(maxLength, int(what.size()));
}

const InstantReplaces &InstantReplaces::Default() {
	static const auto result = [] {
		auto result = InstantReplaces();
		result.add("--", QString(1, QChar(8212)));
		result.add("<<", QString(1, QChar(171)));
		result.add(">>", QString(1, QChar(187)));
		result.add(
			":shrug:",
			QChar(175) + QString("\\_(") + QChar(12484) + ")_/" + QChar(175));
		result.add(":o ", QString(1, QChar(0xD83D)) + QChar(0xDE28));
		result.add("xD ", QString(1, QChar(0xD83D)) + QChar(0xDE06));
		const auto &replacements = Emoji::internal::GetAllReplacements();
		for (const auto &one : replacements) {
			const auto with = Emoji::QStringFromUTF16(one.emoji);
			const auto what = Emoji::QStringFromUTF16(one.replacement);
			result.add(what, with);
		}
		const auto &pairs = Emoji::internal::GetReplacementPairs();
		for (const auto &[what, index] : pairs) {
			const auto emoji = Emoji::internal::ByIndex(index);
			Assert(emoji != nullptr);
			result.add(what, emoji->text());
		}
		return result;
	}();
	return result;
}

const InstantReplaces &InstantReplaces::TextOnly() {
	static const auto result = [] {
		auto result = InstantReplaces();
		result.add("--", QString(1, QChar(8212)));
		result.add("<<", QString(1, QChar(171)));
		result.add(">>", QString(1, QChar(187)));
		result.add(
			":shrug:",
			QChar(175) + QString("\\_(") + QChar(12484) + ")_/" + QChar(175));
		return result;
	}();
	return result;
}

FlatInput::FlatInput(
	QWidget *parent,
	const style::FlatInput &st,
	rpl::producer<QString> placeholder,
	const QString &v)
: RpWidgetWrap<QLineEdit>(v, parent)
, _oldtext(v)
, _placeholderFull(std::move(placeholder))
, _placeholderVisible(!v.length())
, _st(st)
, _textMrg(_st.textMrg) {
	setCursor(style::cur_text);
	resize(_st.width, _st.height);

	setFont(_st.font->f);
	setAlignment(_st.align);

	_placeholderFull.value(
	) | rpl::start_with_next([=](const QString &text) {
		refreshPlaceholder(text);
	}, lifetime());

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			updatePalette();
		}
	});
	updatePalette();

	connect(this, SIGNAL(textChanged(const QString &)), this, SLOT(onTextChange(const QString &)));
	connect(this, SIGNAL(textEdited(const QString &)), this, SLOT(onTextEdited()));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	setStyle(InputStyle<FlatInput>::instance());
	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

void FlatInput::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Text, _st.textColor->c);
	setPalette(p);
}

void FlatInput::customUpDown(bool custom) {
	_customUpDown = custom;
}

void FlatInput::onTouchTimer() {
	_touchRightButton = true;
}

bool FlatInput::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		const auto ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
		}
	}
	return Parent::eventHook(e);
}

void FlatInput::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchPress) return;
		auto weak = make_weak(this);
		if (!_touchMove && window()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(window()->mapFromGlobal(_touchStart));

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		}
		if (weak) {
			_touchTimer.stop();
			_touchPress = _touchMove = _touchRightButton = false;
		}
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.stop();
	} break;
	}
}

void FlatInput::setTextMrg(const QMargins &textMrg) {
	_textMrg = textMrg;
	refreshPlaceholder(_placeholderFull.current());
	update();
}

QRect FlatInput::getTextRect() const {
	return rect().marginsRemoved(_textMrg + QMargins(-2, -1, -2, -1));
}

void FlatInput::finishAnimations() {
	_placeholderFocusedAnimation.stop();
	_placeholderVisibleAnimation.stop();
}

void FlatInput::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = crl::now();
	auto placeholderFocused = _placeholderFocusedAnimation.value(_focused ? 1. : 0.);
	auto pen = anim::pen(_st.borderColor, _st.borderActive, placeholderFocused);
	pen.setWidth(_st.borderWidth);
	p.setPen(pen);
	p.setBrush(anim::brush(_st.bgColor, _st.bgActive, placeholderFocused));
	{
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(QRectF(0, 0, width(), height()).marginsRemoved(QMarginsF(_st.borderWidth / 2., _st.borderWidth / 2., _st.borderWidth / 2., _st.borderWidth / 2.)), st::buttonRadius - (_st.borderWidth / 2.), st::buttonRadius - (_st.borderWidth / 2.));
	}

	if (!_st.icon.empty()) {
		_st.icon.paint(p, 0, 0, width());
	}

	const auto placeholderOpacity = _placeholderVisibleAnimation.value(
		_placeholderVisible ? 1. : 0.);
	if (placeholderOpacity > 0.) {
		p.setOpacity(placeholderOpacity);

		auto left = anim::interpolate(_st.phShift, 0, placeholderOpacity);

		p.save();
		p.setClipRect(rect());
		QRect phRect(placeholderRect());
		phRect.moveLeft(phRect.left() + left);
		phPrepare(p, placeholderFocused);
		p.drawText(phRect, _placeholder, QTextOption(_st.phAlign));
		p.restore();
	}
	QLineEdit::paintEvent(e);
}

void FlatInput::focusInEvent(QFocusEvent *e) {
	if (!_focused) {
		_focused = true;
		_placeholderFocusedAnimation.start(
			[=] { update(); },
			0.,
			1.,
			_st.phDuration);
		update();
	}
	QLineEdit::focusInEvent(e);
	emit focused();
}

void FlatInput::focusOutEvent(QFocusEvent *e) {
	if (_focused) {
		_focused = false;
		_placeholderFocusedAnimation.start(
			[=] { update(); },
			1.,
			0.,
			_st.phDuration);
		update();
	}
	QLineEdit::focusOutEvent(e);
	emit blurred();
}

void FlatInput::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder(_placeholderFull.current());
	return QLineEdit::resizeEvent(e);
}

void FlatInput::setPlaceholder(rpl::producer<QString> placeholder) {
	_placeholderFull = std::move(placeholder);
}

void FlatInput::refreshPlaceholder(const QString &text) {
	const auto availw = width() - _textMrg.left() - _textMrg.right() - _st.phPos.x() - 1;
	if (_st.font->width(text) > availw) {
		_placeholder = _st.font->elided(text, availw);
	} else {
		_placeholder = text;
	}
	update();
}

void FlatInput::contextMenuEvent(QContextMenuEvent *e) {
	if (auto menu = createStandardContextMenu()) {
		(new Ui::PopupMenu(this, menu))->popup(e->globalPos());
	}
}

QSize FlatInput::sizeHint() const {
	return geometry().size();
}

QSize FlatInput::minimumSizeHint() const {
	return geometry().size();
}

void FlatInput::updatePlaceholder() {
	auto hasText = !text().isEmpty();
	if (!hasText) {
		hasText = _lastPreEditTextNotEmpty;
	} else {
		_lastPreEditTextNotEmpty = false;
	}
	auto placeholderVisible = !hasText;
	if (_placeholderVisible != placeholderVisible) {
		_placeholderVisible = placeholderVisible;
		_placeholderVisibleAnimation.start(
			[=] { update(); },
			_placeholderVisible ? 0. : 1.,
			_placeholderVisible ? 1. : 0.,
			_st.phDuration);
	}
}

void FlatInput::inputMethodEvent(QInputMethodEvent *e) {
	QLineEdit::inputMethodEvent(e);
	auto lastPreEditTextNotEmpty = !e->preeditString().isEmpty();
	if (_lastPreEditTextNotEmpty != lastPreEditTextNotEmpty) {
		_lastPreEditTextNotEmpty = lastPreEditTextNotEmpty;
		updatePlaceholder();
	}
}

QRect FlatInput::placeholderRect() const {
	return QRect(_textMrg.left() + _st.phPos.x(), _textMrg.top() + _st.phPos.y(), width() - _textMrg.left() - _textMrg.right(), height() - _textMrg.top() - _textMrg.bottom());
}

void FlatInput::correctValue(const QString &was, QString &now) {
}

void FlatInput::phPrepare(Painter &p, float64 placeholderFocused) {
	p.setFont(_st.font);
	p.setPen(anim::pen(_st.phColor, _st.phFocusColor, placeholderFocused));
}

void FlatInput::keyPressEvent(QKeyEvent *e) {
	QString wasText(_oldtext);

	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
		e->ignore();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	QString newText(text());
	if (wasText == newText) { // call correct manually
		correctValue(wasText, newText);
		_oldtext = newText;
		if (wasText != _oldtext) emit changed();
		updatePlaceholder();
	}
	if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		emit submitted(e->modifiers());
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto selected = selectedText();
		if (!selected.isEmpty() && echoMode() == QLineEdit::Normal) {
			QApplication::clipboard()->setText(selected, QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	}
}

void FlatInput::onTextEdited() {
	QString wasText(_oldtext), newText(text());

	correctValue(wasText, newText);
	_oldtext = newText;
	if (wasText != _oldtext) emit changed();
	updatePlaceholder();

	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void FlatInput::onTextChange(const QString &text) {
	_oldtext = text;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

InputField::InputField(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &value)
: InputField(
	parent,
	st,
	Mode::SingleLine,
	std::move(placeholder),
	{ value, {} }) {
}

InputField::InputField(
	QWidget *parent,
	const style::InputField &st,
	Mode mode,
	rpl::producer<QString> placeholder,
	const QString &value)
: InputField(
	parent,
	st,
	mode,
	std::move(placeholder),
	{ value, {} }) {
}

InputField::InputField(
	QWidget *parent,
	const style::InputField &st,
	Mode mode,
	rpl::producer<QString> placeholder,
	const TextWithTags &value)
: RpWidget(parent)
, _st(st)
, _mode(mode)
, _minHeight(st.heightMin)
, _maxHeight(st.heightMax)
, _inner(std::make_unique<Inner>(this))
, _lastTextWithTags(value)
, _placeholderFull(std::move(placeholder)) {
	_inner->setDocument(Ui::CreateChild<InputDocument>(_inner.get(), _st));

	_inner->setAcceptRichText(false);
	resize(_st.width, _minHeight);

	if (_st.textBg->c.alphaF() >= 1.) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	_inner->setFont(_st.font->f);
	_inner->setAlignment(_st.textAlign);
	if (_mode == Mode::SingleLine) {
		_inner->setWordWrapMode(QTextOption::NoWrap);
	}

	_placeholderFull.value(
	) | rpl::start_with_next([=](const QString &text) {
		refreshPlaceholder(text);
	}, lifetime());

	subscribe(Window::Theme::Background(), [=](
			const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			updatePalette();
		}
	});

	_defaultCharFormat = _inner->textCursor().charFormat();
	updatePalette();
	_inner->textCursor().setCharFormat(_defaultCharFormat);

	_inner->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_inner->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	_inner->setFrameStyle(QFrame::NoFrame | QFrame::Plain);
	_inner->viewport()->setAutoFillBackground(false);

	_inner->setContentsMargins(0, 0, 0, 0);
	_inner->document()->setDocumentMargin(0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_inner->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	connect(_inner->document(), SIGNAL(contentsChange(int,int,int)), this, SLOT(onDocumentContentsChange(int,int,int)));
	connect(_inner.get(), SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(_inner.get(), SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	connect(_inner.get(), SIGNAL(cursorPositionChanged()), this, SLOT(onCursorPositionChanged()));
	if (App::wnd()) {
		connect(_inner.get(), SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));
	}

	const auto bar = _inner->verticalScrollBar();
	_scrollTop = bar->value();
	connect(bar, &QScrollBar::valueChanged, [=] {
		_scrollTop = bar->value();
	});

	setCursor(style::cur_text);
	heightAutoupdated();

	if (!_lastTextWithTags.text.isEmpty()) {
		setTextWithTags(_lastTextWithTags, HistoryAction::Clear);
	}

	startBorderAnimation();
	startPlaceholderAnimation();
	finishAnimating();
}

const rpl::variable<int> &InputField::scrollTop() const {
	return _scrollTop;
}

int InputField::scrollTopMax() const {
	return _inner->verticalScrollBar()->maximum();
}

void InputField::scrollTo(int top) {
	_inner->verticalScrollBar()->setValue(top);
}

bool InputField::viewportEventInner(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		const auto ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			handleTouchEvent(ev);
		}
	}
	return _inner->QTextEdit::viewportEvent(e);
}

void InputField::updatePalette() {
	auto p = _inner->palette();
	p.setColor(QPalette::Text, _st.textFg->c);
	_inner->setPalette(p);

	_defaultCharFormat.merge(PrepareTagFormat(_st, QString()));
	auto cursor = textCursor();

	const auto document = _inner->document();
	auto block = document->begin();
	const auto end = document->end();
	for (; block != end; block = block.next()) {
		auto till = block.position();
		for (auto i = block.begin(); !i.atEnd();) {
			for (; !i.atEnd(); ++i) {
				const auto fragment = i.fragment();
				if (!fragment.isValid() || fragment.position() < till) {
					continue;
				}
				till = fragment.position() + fragment.length();

				auto format = fragment.charFormat();
				const auto tag = format.property(kTagProperty).toString();
				format.setForeground(PrepareTagFormat(_st, tag).foreground());
				cursor.setPosition(fragment.position());
				cursor.setPosition(till, QTextCursor::KeepAnchor);
				cursor.mergeCharFormat(format);
				i = block.begin();
				break;
			}
		}
	}

	cursor = textCursor();
	if (!cursor.hasSelection()) {
		auto format = cursor.charFormat();
		format.merge(PrepareTagFormat(
			_st,
			format.property(kTagProperty).toString()));
		cursor.setCharFormat(format);
		setTextCursor(cursor);
	}
}

void InputField::onTouchTimer() {
	_touchRightButton = true;
}

void InputField::setInstantReplaces(const InstantReplaces &replaces) {
	_mutableInstantReplaces = replaces;
}

void InputField::setInstantReplacesEnabled(rpl::producer<bool> enabled) {
	std::move(
		enabled
	) | rpl::start_with_next([=](bool value) {
		_instantReplacesEnabled = value;
	}, lifetime());
}

void InputField::setMarkdownReplacesEnabled(rpl::producer<bool> enabled) {
	std::move(
		enabled
	) | rpl::start_with_next([=](bool value) {
		if (_markdownEnabled != value) {
			_markdownEnabled = value;
			if (_markdownEnabled) {
				handleContentsChanged();
			} else {
				_lastMarkdownTags = {};
			}
		}
	}, lifetime());
}

void InputField::setTagMimeProcessor(
		std::unique_ptr<TagMimeProcessor> &&processor) {
	_tagMimeProcessor = std::move(processor);
}

void InputField::setAdditionalMargin(int margin) {
	_inner->setStyleSheet(qsl("QTextEdit { margin: %1px; }").arg(margin));
	_additionalMargin = margin;
	checkContentHeight();
}

void InputField::setMaxLength(int length) {
	if (_maxLength != length) {
		_maxLength = length;
		if (_maxLength > 0) {
			const auto document = _inner->document();
			_correcting = true;
			QTextCursor(document->docHandle(), 0).joinPreviousEditBlock();
			const auto guard = gsl::finally([&] {
				_correcting = false;
				QTextCursor(document->docHandle(), 0).endEditBlock();
				handleContentsChanged();
			});

			auto cursor = QTextCursor(document->docHandle(), 0);
			cursor.movePosition(QTextCursor::End);
			chopByMaxLength(0, cursor.position());
		}
	}
}

void InputField::setMinHeight(int height) {
	_minHeight = height;
}

void InputField::setMaxHeight(int height) {
	_maxHeight = height;
}

void InputField::insertTag(const QString &text, QString tagId) {
	auto cursor = textCursor();
	const auto position = cursor.position();

	const auto document = _inner->document();
	auto block = document->findBlock(position);
	for (auto iter = block.begin(); !iter.atEnd(); ++iter) {
		auto fragment = iter.fragment();
		Assert(fragment.isValid());

		const auto fragmentPosition = fragment.position();
		const auto fragmentEnd = (fragmentPosition + fragment.length());
		if (fragmentPosition >= position || fragmentEnd < position) {
			continue;
		}

		const auto format = fragment.charFormat();
		if (format.isImageFormat()) {
			continue;
		}

		auto mentionInCommand = false;
		const auto fragmentText = fragment.text();
		for (auto i = position - fragmentPosition; i > 0; --i) {
			const auto previous = fragmentText[i - 1];
			if (previous == '@' || previous == '#' || previous == '/') {
				if ((i == position - fragmentPosition
					|| (previous == '/'
						? fragmentText[i].isLetterOrNumber()
						: fragmentText[i].isLetter())
					|| previous == '#') &&
					(i < 2 || !(fragmentText[i - 2].isLetterOrNumber()
						|| fragmentText[i - 2] == '_'))) {
					cursor.setPosition(fragmentPosition + i - 1);
					auto till = fragmentPosition + i;
					for (; (till < fragmentEnd && till < position); ++till) {
						const auto ch = fragmentText[till - fragmentPosition];
						if (!ch.isLetterOrNumber() && ch != '_' && ch != '@') {
							break;
						}
					}
					if (till < fragmentEnd
						&& fragmentText[till - fragmentPosition] == ' ') {
						++till;
					}
					cursor.setPosition(till, QTextCursor::KeepAnchor);
					break;
				} else if ((i == position - fragmentPosition
					|| fragmentText[i].isLetter())
					&& fragmentText[i - 1] == '@'
					&& (i > 2)
					&& (fragmentText[i - 2].isLetterOrNumber()
						|| fragmentText[i - 2] == '_')
					&& !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				break;
			}
			if (position - fragmentPosition - i > 127
				|| (!mentionInCommand
					&& (position - fragmentPosition - i > 63))
				|| (!fragmentText[i - 1].isLetterOrNumber()
					&& fragmentText[i - 1] != '_')) {
				break;
			}
		}
		break;
	}
	if (tagId.isEmpty()) {
		cursor.insertText(text + ' ', _defaultCharFormat);
	} else {
		_insertedTags.clear();
		_insertedTags.push_back({ 0, text.size(), tagId });
		_insertedTagsAreFromMime = false;
		cursor.insertText(text + ' ');
		_insertedTags.clear();
	}
}

bool InputField::heightAutoupdated() {
	if (_minHeight < 0
		|| _maxHeight < 0
		|| _inHeightCheck
		|| _mode == Mode::SingleLine) {
		return false;
	}
	_inHeightCheck = true;
	const auto guard = gsl::finally([&] { _inHeightCheck = false; });

	SendPendingMoveResizeEvents(this);

	const auto contentHeight = int(std::ceil(document()->size().height()))
		+ _st.textMargins.top()
		+ _st.textMargins.bottom()
		+ 2 * _additionalMargin;
	const auto newHeight = snap(contentHeight, _minHeight, _maxHeight);
	if (height() != newHeight) {
		resize(width(), newHeight);
		return true;
	}
	return false;
}

void InputField::checkContentHeight() {
	if (heightAutoupdated()) {
		emit resized();
	}
}

void InputField::handleTouchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchPress) return;
		auto weak = make_weak(this);
		if (!_touchMove && window()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(window()->mapFromGlobal(_touchStart));

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		}
		if (weak) {
			_touchTimer.stop();
			_touchPress = _touchMove = _touchRightButton = false;
		}
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.stop();
	} break;
	}
}

void InputField::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto r = rect().intersected(e->rect());
	if (_st.textBg->c.alphaF() > 0.) {
		p.fillRect(r, _st.textBg);
	}
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg);
	}
	auto errorDegree = _a_error.value(_error ? 1. : 0.);
	auto focusedDegree = _a_focused.value(_focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.value(1.);
	auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = snap(_borderAnimationStart, 0, width());
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width() - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height() - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}

	if (_st.placeholderScale > 0. && !_placeholderPath.isEmpty()) {
		auto placeholderShiftDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		p.save();
		p.setClipRect(r);

		auto placeholderTop = anim::interpolate(0, _st.placeholderShift, placeholderShiftDegree);

		QRect r(rect().marginsRemoved(_st.textMargins + _st.placeholderMargins));
		r.moveTop(r.top() + placeholderTop);
		if (rtl()) r.moveLeft(width() - r.left() - r.width());

		auto placeholderScale = 1. - (1. - _st.placeholderScale) * placeholderShiftDegree;
		auto placeholderFg = anim::color(_st.placeholderFg, _st.placeholderFgActive, focusedDegree);
		placeholderFg = anim::color(placeholderFg, _st.placeholderFgError, errorDegree);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(placeholderFg);
		p.translate(r.topLeft());
		p.scale(placeholderScale, placeholderScale);
		p.drawPath(_placeholderPath);

		p.restore();
	} else if (!_placeholder.isEmpty()) {
		const auto placeholderHiddenDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			const auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			p.setFont(_st.placeholderFont);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));

			if (_st.placeholderAlign == style::al_topleft && _placeholderAfterSymbols > 0) {
				const auto skipWidth = placeholderSkipWidth();
				p.drawText(
					_st.textMargins.left() + _st.placeholderMargins.left() + skipWidth,
					_st.textMargins.top() + _st.placeholderMargins.top() + _st.placeholderFont->ascent,
					_placeholder);
			} else {
				auto r = rect().marginsRemoved(_st.textMargins + _st.placeholderMargins);
				r.moveLeft(r.left() + placeholderLeft);
				if (rtl()) r.moveLeft(width() - r.left() - r.width());
				p.drawText(r, _placeholder, _st.placeholderAlign);
			}

			p.restore();
		}
	}
	RpWidget::paintEvent(e);
}

int InputField::placeholderSkipWidth() const {
	if (!_placeholderAfterSymbols) {
		return 0;
	}
	const auto &text = getTextWithTags().text;
	auto result = _st.font->width(text.mid(0, _placeholderAfterSymbols));
	if (_placeholderAfterSymbols > text.size()) {
		result += _st.font->spacew;
	}
	return result;
}

void InputField::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([this] { update(); }, 0., 1., _st.duration);
			} else {
				_a_borderShown.start([this] { update(); }, 0., 1., _st.duration);
			}
		} else {
			_a_borderOpacity.start([this] { update(); }, 1., 0., _st.duration);
		}
	}
}

void InputField::focusInEvent(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason)
		? mapFromGlobal(QCursor::pos()).x()
		: (width() / 2);
	QTimer::singleShot(0, this, SLOT(onFocusInner()));
}

void InputField::mousePressEvent(QMouseEvent *e) {
	_borderAnimationStart = e->pos().x();
	QTimer::singleShot(0, this, SLOT(onFocusInner()));
}

void InputField::onFocusInner() {
	auto borderStart = _borderAnimationStart;
	_inner->setFocus();
	_borderAnimationStart = borderStart;
}

int InputField::borderAnimationStart() const {
	return _borderAnimationStart;
}

void InputField::contextMenuEvent(QContextMenuEvent *e) {
	_inner->contextMenuEvent(e);
}

void InputField::focusInEventInner(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason)
		? mapFromGlobal(QCursor::pos()).x()
		: (width() / 2);
	setFocused(true);
	_inner->QTextEdit::focusInEvent(e);
	emit focused();
}

void InputField::focusOutEventInner(QFocusEvent *e) {
	setFocused(false);
	_inner->QTextEdit::focusOutEvent(e);
	emit blurred();
}

void InputField::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start([this] { update(); }, _focused ? 0. : 1., _focused ? 1. : 0., _st.duration);
		startPlaceholderAnimation();
		startBorderAnimation();
	}
}

QSize InputField::sizeHint() const {
	return geometry().size();
}

QSize InputField::minimumSizeHint() const {
	return geometry().size();
}

bool InputField::hasText() const {
	const auto document = _inner->document();
	const auto from = document->begin();
	const auto till = document->end();

	if (from == till) {
		return false;
	}

	for (auto item = from.begin(); !item.atEnd(); ++item) {
		const auto fragment = item.fragment();
		if (!fragment.isValid()) {
			continue;
		} else if (!fragment.text().isEmpty()) {
			return true;
		}
	}
	return (from.next() != till);
}

QString InputField::getTextPart(
		int start,
		int end,
		TagList &outTagsList,
		bool &outTagsChanged,
		std::vector<MarkdownTag> *outMarkdownTags) const {
	Expects((start == 0 && end < 0) || outMarkdownTags == nullptr);

	if (end >= 0 && end <= start) {
		outTagsChanged = !outTagsList.isEmpty();
		outTagsList.clear();
		return QString();
	}

	if (start < 0) {
		start = 0;
	}
	const auto full = (start == 0 && end < 0);

	auto lastTag = QString();
	TagAccumulator tagAccumulator(outTagsList);
	MarkdownTagAccumulator markdownTagAccumulator(outMarkdownTags);
	const auto newline = outMarkdownTags ? QString(1, '\n') : QString();

	const auto document = _inner->document();
	const auto from = full ? document->begin() : document->findBlock(start);
	auto till = (end < 0) ? document->end() : document->findBlock(end);
	if (till.isValid()) {
		till = till.next();
	}

	auto possibleLength = 0;
	for (auto block = from; block != till; block = block.next()) {
		possibleLength += block.length();
	}
	auto result = QString();
	result.reserve(possibleLength);
	if (!full && end < 0) {
		end = possibleLength;
	}

	for (auto block = from; block != till;) {
		for (auto item = block.begin(); !item.atEnd(); ++item) {
			const auto fragment = item.fragment();
			if (!fragment.isValid()) {
				continue;
			}

			const auto fragmentPosition = full ? 0 : fragment.position();
			const auto fragmentEnd = full
				? 0
				: (fragmentPosition + fragment.length());
			const auto format = fragment.charFormat();
			if (!full) {
				if (fragmentPosition == end) {
					tagAccumulator.feed(
						format.property(kTagProperty).toString(),
						result.size());
					break;
				} else if (fragmentPosition > end) {
					break;
				} else if (fragmentEnd <= start) {
					continue;
				}
			}

			const auto emojiText = [&] {
				if (format.isImageFormat()) {
					const auto imageName = format.toImageFormat().name();
					if (const auto emoji = Ui::Emoji::FromUrl(imageName)) {
						return emoji->text();
					}
				}
				return QString();
			}();
			auto text = [&] {
				const auto result = fragment.text();
				if (!full) {
					if (fragmentPosition < start) {
						return result.mid(start - fragmentPosition, end - start);
					} else if (fragmentEnd > end) {
						return result.mid(0, end - fragmentPosition);
					}
				}
				return result;
			}();

			if (full || !text.isEmpty()) {
				lastTag = format.property(kTagProperty).toString();
				tagAccumulator.feed(lastTag, result.size());
			}

			auto begin = text.data();
			auto ch = begin;
			auto adjustedLength = text.size();
			for (const auto end = begin + text.size(); ch != end; ++ch) {
				if (IsNewline(*ch) && ch->unicode() != '\r') {
					*ch = QLatin1Char('\n');
				} else switch (ch->unicode()) {
				case QChar::Nbsp: {
					*ch = QLatin1Char(' ');
				} break;
				case QChar::ObjectReplacementCharacter: {
					if (ch > begin) {
						result.append(begin, ch - begin);
					}
					adjustedLength += (emojiText.size() - 1);
					if (!emojiText.isEmpty()) {
						result.append(emojiText);
					}
					begin = ch + 1;
				} break;
				}
			}
			if (ch > begin) {
				result.append(begin, ch - begin);
			}

			if (full || !text.isEmpty()) {
				markdownTagAccumulator.feed(text, adjustedLength, lastTag);
			}
		}

		block = block.next();
		if (block != till) {
			result.append('\n');
			markdownTagAccumulator.feed(newline, 1, lastTag);
		}
	}

	tagAccumulator.feed(QString(), result.size());
	tagAccumulator.finish();
	markdownTagAccumulator.finish();

	outTagsChanged = tagAccumulator.changed();
	return result;
}

bool InputField::isUndoAvailable() const {
	return _undoAvailable;
}

bool InputField::isRedoAvailable() const {
	return _redoAvailable;
}

void InputField::processFormatting(int insertPosition, int insertEnd) {
	// Tilde formatting.
	const auto tildeFormatting = (_st.font->f.pixelSize() * cIntRetinaFactor() == 13)
		&& (_st.font->f.family() == qstr("Open Sans"));
	auto isTildeFragment = false;
	const auto tildeFixedFont = AdjustFont(st::semiboldFont, _st.font);

	// First tag handling (the one we inserted text to).
	bool startTagFound = false;
	bool breakTagOnNotLetter = false;

	auto document = _inner->document();

	// Apply inserted tags.
	auto insertedTagsProcessor = _insertedTagsAreFromMime
		? _tagMimeProcessor.get()
		: nullptr;
	const auto breakTagOnNotLetterTill = ProcessInsertedTags(
		_st,
		document,
		insertPosition,
		insertEnd,
		_insertedTags,
		insertedTagsProcessor);
	using ActionType = FormattingAction::Type;
	while (true) {
		FormattingAction action;

		auto fromBlock = document->findBlock(insertPosition);
		auto tillBlock = document->findBlock(insertEnd);
		if (tillBlock.isValid()) tillBlock = tillBlock.next();

		for (auto block = fromBlock; block != tillBlock; block = block.next()) {
			for (auto fragmentIt = block.begin(); !fragmentIt.atEnd(); ++fragmentIt) {
				auto fragment = fragmentIt.fragment();
				Assert(fragment.isValid());

				int fragmentPosition = fragment.position();
				if (insertPosition >= fragmentPosition + fragment.length()) {
					continue;
				}
				int changedPositionInFragment = insertPosition - fragmentPosition; // Can be negative.
				int changedEndInFragment = insertEnd - fragmentPosition;
				if (changedEndInFragment <= 0) {
					break;
				}

				auto format = fragment.charFormat();
				if (!format.hasProperty(kTagProperty)) {
					action.type = ActionType::RemoveTag;
					action.intervalStart = fragmentPosition;
					action.intervalEnd = fragmentPosition + fragment.length();
					break;
				}
				if (tildeFormatting) {
					isTildeFragment = (format.font() == tildeFixedFont);
				}

				auto fragmentText = fragment.text();
				auto *textStart = fragmentText.constData();
				auto *textEnd = textStart + fragmentText.size();

				const auto with = format.property(kInstantReplaceWithId);
				if (with.isValid()) {
					const auto string = with.toString();
					if (fragmentText != string) {
						action.type = ActionType::ClearInstantReplace;
						action.intervalStart = fragmentPosition
							+ (fragmentText.startsWith(string)
								? string.size()
								: 0);
						action.intervalEnd = fragmentPosition
							+ fragmentText.size();
						break;
					}
				}

				if (!startTagFound) {
					startTagFound = true;
					auto tagName = format.property(kTagProperty).toString();
					if (!tagName.isEmpty()) {
						breakTagOnNotLetter = WasInsertTillTheEndOfTag(
							block,
							fragmentIt,
							insertEnd);
					}
				}

				auto *ch = textStart + qMax(changedPositionInFragment, 0);
				for (; ch < textEnd; ++ch) {
					const auto removeNewline = (_mode != Mode::MultiLine)
						&& IsNewline(*ch);
					if (removeNewline) {
						if (action.type == ActionType::Invalid) {
							action.type = ActionType::RemoveNewline;
							action.intervalStart = fragmentPosition + (ch - textStart);
							action.intervalEnd = action.intervalStart + 1;
						}
						break;
					}

					auto emojiLength = 0;
					if (const auto emoji = Ui::Emoji::Find(ch, textEnd, &emojiLength)) {
						// Replace emoji if no current action is prepared.
						if (action.type == ActionType::Invalid) {
							action.type = ActionType::InsertEmoji;
							action.emoji = emoji;
							action.intervalStart = fragmentPosition + (ch - textStart);
							action.intervalEnd = action.intervalStart + emojiLength;
						}
						break;
					}

					if (breakTagOnNotLetter && !ch->isLetter()) {
						// Remove tag name till the end if no current action is prepared.
						if (action.type != ActionType::Invalid) {
							break;
						}
						breakTagOnNotLetter = false;
						if (fragmentPosition + (ch - textStart) < breakTagOnNotLetterTill) {
							action.type = ActionType::RemoveTag;
							action.intervalStart = fragmentPosition + (ch - textStart);
							action.intervalEnd = breakTagOnNotLetterTill;
							break;
						}
					}
					if (tildeFormatting) { // Tilde symbol fix in OpenSans.
						bool tilde = (ch->unicode() == '~');
						if ((tilde && !isTildeFragment) || (!tilde && isTildeFragment)) {
							if (action.type == ActionType::Invalid) {
								action.type = ActionType::TildeFont;
								action.intervalStart = fragmentPosition + (ch - textStart);
								action.intervalEnd = action.intervalStart + 1;
								action.tildeTag = format.property(kTagProperty).toString();
								action.isTilde = tilde;
							} else {
								++action.intervalEnd;
							}
						} else if (action.type == ActionType::TildeFont) {
							break;
						}
					}

					if (ch + 1 < textEnd && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) {
						++ch;
					}
				}
				if (action.type != ActionType::Invalid) {
					break;
				}
			}
			if (action.type != ActionType::Invalid) {
				break;
			} else if (_mode != Mode::MultiLine
				&& block.next() != document->end()) {
				action.type = ActionType::RemoveNewline;
				action.intervalStart = block.next().position() - 1;
				action.intervalEnd = action.intervalStart + 1;
				break;
			}
		}
		if (action.type != ActionType::Invalid) {
			PrepareFormattingOptimization(document);

			auto cursor = QTextCursor(
				document->docHandle(),
				action.intervalStart);
			cursor.setPosition(action.intervalEnd, QTextCursor::KeepAnchor);
			if (action.type == ActionType::InsertEmoji) {
				InsertEmojiAtCursor(cursor, action.emoji);
				insertPosition = action.intervalStart + 1;
				if (insertEnd >= action.intervalEnd) {
					insertEnd -= action.intervalEnd
						- action.intervalStart
						- 1;
				}
			} else if (action.type == ActionType::RemoveTag) {
				RemoveDocumentTags(
					_st,
					document,
					action.intervalStart,
					action.intervalEnd);
			} else if (action.type == ActionType::TildeFont) {
				auto format = QTextCharFormat();
				format.setFont(action.isTilde
					? tildeFixedFont
					: PrepareTagFormat(_st, action.tildeTag).font());
				cursor.mergeCharFormat(format);
				insertPosition = action.intervalEnd;
			} else if (action.type == ActionType::ClearInstantReplace) {
				auto format = _defaultCharFormat;
				ApplyTagFormat(format, cursor.charFormat());
				cursor.setCharFormat(format);
			} else if (action.type == ActionType::RemoveNewline) {
				cursor.removeSelectedText();
				insertPosition = action.intervalStart;
				if (insertEnd >= action.intervalEnd) {
					insertEnd -= action.intervalEnd - action.intervalStart;
				}
			}
		} else {
			break;
		}
	}
}

void InputField::onDocumentContentsChange(
		int position,
		int charsRemoved,
		int charsAdded) {
	if (_correcting) {
		return;
	}

	const auto document = _inner->document();

	// Qt bug workaround https://bugreports.qt.io/browse/QTBUG-49062
	if (!position) {
		auto cursor = QTextCursor(document->docHandle(), 0);
		cursor.movePosition(QTextCursor::End);
		if (position + charsAdded > cursor.position()) {
			const auto delta = position + charsAdded - cursor.position();
			if (charsRemoved >= delta) {
				charsAdded -= delta;
				charsRemoved -= delta;
			}
		}
	}

	const auto insertPosition = (_realInsertPosition >= 0)
		? _realInsertPosition
		: position;
	const auto insertLength = (_realInsertPosition >= 0)
		? _realCharsAdded
		: charsAdded;

	const auto removePosition = position;
	const auto removeLength = charsRemoved;

	_correcting = true;
	QTextCursor(document->docHandle(), 0).joinPreviousEditBlock();
	const auto guard = gsl::finally([&] {
		_correcting = false;
		QTextCursor(document->docHandle(), 0).endEditBlock();
		handleContentsChanged();
	});

	chopByMaxLength(insertPosition, insertLength);

	if (document->availableRedoSteps() == 0 && insertLength > 0) {
		const auto pageSize = document->pageSize();
		processFormatting(insertPosition, insertPosition + insertLength);
		if (document->pageSize() != pageSize) {
			document->setPageSize(pageSize);
		}
	}
}

void InputField::onCursorPositionChanged() {
	auto cursor = textCursor();
	if (!cursor.hasSelection() && !cursor.position()) {
		cursor.setCharFormat(_defaultCharFormat);
		setTextCursor(cursor);
	}
}

void InputField::chopByMaxLength(int insertPosition, int insertLength) {
	Expects(_correcting);

	if (_maxLength < 0) {
		return;
	}

	auto cursor = QTextCursor(document()->docHandle(), 0);
	cursor.movePosition(QTextCursor::End);
	const auto fullSize = cursor.position();
	const auto toRemove = fullSize - _maxLength;
	if (toRemove > 0) {
		if (toRemove > insertLength) {
			if (insertLength) {
				cursor.setPosition(insertPosition);
				cursor.setPosition(
					(insertPosition + insertLength),
					QTextCursor::KeepAnchor);
				cursor.removeSelectedText();
			}
			cursor.setPosition(fullSize - (toRemove - insertLength));
			cursor.setPosition(fullSize, QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
		} else {
			cursor.setPosition(
				insertPosition + (insertLength - toRemove));
			cursor.setPosition(
				insertPosition + insertLength,
				QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
		}
	}
}

void InputField::handleContentsChanged() {
	setErrorShown(false);

	auto tagsChanged = false;
	const auto currentText = getTextPart(
		0,
		-1,
		_lastTextWithTags.tags,
		tagsChanged,
		_markdownEnabled ? &_lastMarkdownTags : nullptr);

	//highlightMarkdown();

	if (tagsChanged || (_lastTextWithTags.text != currentText)) {
		_lastTextWithTags.text = currentText;
		const auto weak = make_weak(this);
		emit changed();
		if (!weak) {
			return;
		}
		checkContentHeight();
	}
	startPlaceholderAnimation();
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::highlightMarkdown() {
	// Highlighting may interfere with markdown parsing -> inaccurate.
	// For debug.
	auto from = 0;
	auto applyColor = [&](int a, int b, QColor color) {
		auto cursor = textCursor();
		cursor.setPosition(a);
		cursor.setPosition(b, QTextCursor::KeepAnchor);
		auto format = QTextCharFormat();
		format.setForeground(color);
		cursor.mergeCharFormat(format);
		from = b;
	};
	for (const auto &tag : _lastMarkdownTags) {
		if (tag.internalStart > from) {
			applyColor(from, tag.internalStart, QColor(0, 0, 0));
		} else if (tag.internalStart < from) {
			continue;
		}
		applyColor(
			tag.internalStart,
			tag.internalStart + tag.internalLength,
			(tag.closed
				? QColor(0, 128, 0)
				: QColor(128, 0, 0)));
	}
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	if (const auto till = cursor.position(); till > from) {
		applyColor(from, till, QColor(0, 0, 0));
	}
}

void InputField::onUndoAvailable(bool avail) {
	_undoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::onRedoAvailable(bool avail) {
	_redoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::setDisplayFocused(bool focused) {
	setFocused(focused);
	finishAnimating();
}

void InputField::selectAll() {
	auto cursor = _inner->textCursor();
	cursor.setPosition(0);
	cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	_inner->setTextCursor(cursor);
}

void InputField::finishAnimating() {
	_a_focused.stop();
	_a_error.stop();
	_a_placeholderShifted.stop();
	_a_borderShown.stop();
	_a_borderOpacity.stop();
	update();
}

void InputField::setPlaceholderHidden(bool forcePlaceholderHidden) {
	_forcePlaceholderHidden = forcePlaceholderHidden;
	startPlaceholderAnimation();
}

void InputField::startPlaceholderAnimation() {
	const auto textLength = [&] {
		return getTextWithTags().text.size() + _lastPreEditText.size();
	};
	const auto placeholderShifted = _forcePlaceholderHidden
		|| (_focused && _st.placeholderScale > 0.)
		|| (textLength() > _placeholderAfterSymbols);
	if (_placeholderShifted != placeholderShifted) {
		_placeholderShifted = placeholderShifted;
		_a_placeholderShifted.start(
			[=] { update(); },
			_placeholderShifted ? 0. : 1.,
			_placeholderShifted ? 1. : 0.,
			_st.duration);
	}
}

QMimeData *InputField::createMimeDataFromSelectionInner() const {
	auto result = std::make_unique<QMimeData>();
	const auto cursor = _inner->textCursor();
	const auto start = cursor.selectionStart();
	const auto end = cursor.selectionEnd();
	if (end > start) {
		auto textWithTags = getTextWithTagsPart(start, end);
		result->setText(ExpandCustomLinks(textWithTags));
		if (!textWithTags.tags.isEmpty()) {
			if (_tagMimeProcessor) {
				for (auto &tag : textWithTags.tags) {
					tag.id = _tagMimeProcessor->mimeTagFromTag(tag.id);
				}
			}
			result->setData(
				TextUtilities::TagsTextMimeType(),
				textWithTags.text.toUtf8());
			result->setData(
				TextUtilities::TagsMimeType(),
				TextUtilities::SerializeTags(textWithTags.tags));
		}
	}
	return result.release();
}

void InputField::customUpDown(bool isCustom) {
	_customUpDown = isCustom;
}

void InputField::customTab(bool isCustom) {
	_customTab = isCustom;
}

void InputField::setSubmitSettings(SubmitSettings settings) {
	_submitSettings = settings;
}

not_null<QTextDocument*> InputField::document() {
	return _inner->document();
}

not_null<const QTextDocument*> InputField::document() const {
	return _inner->document();
}

void InputField::setTextCursor(const QTextCursor &cursor) {
	return _inner->setTextCursor(cursor);
}

QTextCursor InputField::textCursor() const {
	return _inner->textCursor();
}

void InputField::setCursorPosition(int pos) {
	auto cursor = _inner->textCursor();
	cursor.setPosition(pos);
	_inner->setTextCursor(cursor);
}

void InputField::setText(const QString &text) {
	setTextWithTags({ text, {} });
}

void InputField::setTextWithTags(
		const TextWithTags &textWithTags,
		HistoryAction historyAction) {
	_insertedTags = textWithTags.tags;
	_insertedTagsAreFromMime = false;
	_realInsertPosition = 0;
	_realCharsAdded = textWithTags.text.size();
	const auto document = _inner->document();
	auto cursor = QTextCursor(document->docHandle(), 0);
	if (historyAction == HistoryAction::Clear) {
		document->setUndoRedoEnabled(false);
		cursor.beginEditBlock();
	} else if (historyAction == HistoryAction::MergeEntry) {
		cursor.joinPreviousEditBlock();
	} else {
		cursor.beginEditBlock();
	}
	cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	cursor.insertText(textWithTags.text);
	cursor.movePosition(QTextCursor::End);
	cursor.endEditBlock();
	if (historyAction == HistoryAction::Clear) {
		document->setUndoRedoEnabled(true);
	}
	_insertedTags.clear();
	_realInsertPosition = -1;
	finishAnimating();
}

TextWithTags InputField::getTextWithTagsPart(int start, int end) const {
	auto changed = false;
	auto result = TextWithTags();
	result.text = getTextPart(start, end, result.tags, changed);
	return result;
}

TextWithTags InputField::getTextWithAppliedMarkdown() const {
	if (!_markdownEnabled || _lastMarkdownTags.empty()) {
		return getTextWithTags();
	}
	const auto &originalText = _lastTextWithTags.text;
	const auto &originalTags = _lastTextWithTags.tags;

	// Ignore tags that partially intersect some http-links.
	// This will allow sending http://test.com/__test__/test correctly.
	const auto links = TextUtilities::ParseEntities(
		originalText,
		0).entities;

	auto result = TextWithTags();
	result.text.reserve(originalText.size());
	result.tags.reserve(originalTags.size() + _lastMarkdownTags.size());
	auto removed = 0;
	auto originalTag = originalTags.begin();
	const auto originalTagsEnd = originalTags.end();
	const auto addOriginalTagsUpTill = [&](int offset) {
		while (originalTag != originalTagsEnd
			&& originalTag->offset + originalTag->length <= offset) {
			result.tags.push_back(*originalTag++);
			result.tags.back().offset -= removed;
		}
	};
	auto from = 0;
	const auto addOriginalTextUpTill = [&](int offset) {
		if (offset > from) {
			result.text.append(originalText.midRef(from, offset - from));
		}
	};
	auto link = links.begin();
	const auto linksEnd = links.end();
	for (const auto &tag : _lastMarkdownTags) {
		const auto tagLength = int(tag.tag.size());
		if (!tag.closed || tag.adjustedStart < from) {
			continue;
		}
		auto entityLength = tag.adjustedLength - 2 * tagLength;
		if (entityLength <= 0) {
			continue;
		}
		addOriginalTagsUpTill(tag.adjustedStart);
		const auto tagAdjustedEnd = tag.adjustedStart + tag.adjustedLength;
		if (originalTag != originalTagsEnd
			&& originalTag->offset < tagAdjustedEnd) {
			continue;
		}
		while (link != linksEnd
			&& link->offset() + link->length() <= tag.adjustedStart) {
			++link;
		}
		if (link != linksEnd
			&& link->offset() < tagAdjustedEnd
			&& (link->offset() + link->length() > tagAdjustedEnd
				|| link->offset() < tag.adjustedStart)) {
			continue;
		}
		addOriginalTextUpTill(tag.adjustedStart);

		auto entityStart = tag.adjustedStart + tagLength;
		if (tag.tag == kTagPre) {
			// Remove redundant newlines for pre.
			// If ``` is on a separate line add only one newline.
			if (IsNewline(originalText[entityStart])
				&& (result.text.isEmpty()
					|| IsNewline(result.text[result.text.size() - 1]))) {
				++entityStart;
				--entityLength;
			}
			const auto entityEnd = entityStart + entityLength;
			if (IsNewline(originalText[entityEnd - 1])
				&& (originalText.size() <= entityEnd + tagLength
					|| IsNewline(originalText[entityEnd + tagLength]))) {
				--entityLength;
			}
		}

		if (entityLength > 0) {
			// Add tag text and entity.
			result.tags.push_back(TextWithTags::Tag{
				int(result.text.size()),
				entityLength,
				tag.tag });
			result.text.append(originalText.midRef(
				entityStart,
				entityLength));
		}

		from = tag.adjustedStart + tag.adjustedLength;
		removed += (tag.adjustedLength - entityLength);
	}
	addOriginalTagsUpTill(originalText.size());
	addOriginalTextUpTill(originalText.size());
	return result;
}

void InputField::clear() {
	_inner->clear();
	startPlaceholderAnimation();
}

bool InputField::hasFocus() const {
	return _inner->hasFocus();
}

void InputField::setFocus() {
	_inner->setFocus();
}

void InputField::clearFocus() {
	_inner->clearFocus();
}

void InputField::ensureCursorVisible() {
	_inner->ensureCursorVisible();
}

not_null<QTextEdit*> InputField::rawTextEdit() {
	return _inner.get();
}

not_null<const QTextEdit*> InputField::rawTextEdit() const {
	return _inner.get();
}

bool InputField::ShouldSubmit(
		SubmitSettings settings,
		Qt::KeyboardModifiers modifiers) {
	const auto shift = modifiers.testFlag(Qt::ShiftModifier);
	const auto ctrl = modifiers.testFlag(Qt::ControlModifier)
		|| modifiers.testFlag(Qt::MetaModifier);
	return (ctrl && shift)
		|| (ctrl
			&& settings != SubmitSettings::None
			&& settings != SubmitSettings::Enter)
		|| (!ctrl
			&& !shift
			&& settings != SubmitSettings::None
			&& settings != SubmitSettings::CtrlEnter);
}

void InputField::keyPressEventInner(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool macmeta = Platform::IsMac() && e->modifiers().testFlag(Qt::ControlModifier) && !e->modifiers().testFlag(Qt::MetaModifier) && !e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier);
	bool enterSubmit = (_mode != Mode::MultiLine)
		|| ShouldSubmit(_submitSettings, e->modifiers());
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);
	if (e->key() == Qt::Key_Left
		|| e->key() == Qt::Key_Right
		|| e->key() == Qt::Key_Up
		|| e->key() == Qt::Key_Down
		|| e->key() == Qt::Key_Home
		|| e->key() == Qt::Key_End) {
		_reverseMarkdownReplacement = false;
	}

	if (macmeta && e->key() == Qt::Key_Backspace) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (e->key() == Qt::Key_Backspace
		&& e->modifiers() == 0
		&& revertFormatReplace()) {
		e->accept();
	} else if (enter && enterSubmit) {
		emit submitted(e->modifiers());
	} else if (e->key() == Qt::Key_Escape) {
		e->ignore();
		emit cancelled();
	} else if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
		if (alt || ctrl) {
			e->ignore();
		} else if (_customTab) {
			emit tabbed();
		} else if (!focusNextPrevChild(e->key() == Qt::Key_Tab && !shift)) {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Search || e == QKeySequence::Find) {
		e->ignore();
	} else if (handleMarkdownKey(e)) {
		e->accept();
	} else if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
		e->ignore();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		const auto cursor = textCursor();
		const auto start = cursor.selectionStart();
		const auto end = cursor.selectionEnd();
		if (end > start) {
			QApplication::clipboard()->setText(
				getTextWithTagsPart(start, end).text,
				QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	} else {
		const auto text = e->text();
		const auto oldPosition = textCursor().position();
		if (enter && ctrl) {
			e->setModifiers(e->modifiers() & ~Qt::ControlModifier);
		}
		_inner->QTextEdit::keyPressEvent(e);
		auto cursor = textCursor();
		if (cursor.position() == oldPosition) {
			bool check = false;
			if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_Up) {
				cursor.movePosition(QTextCursor::Start, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_Down) {
				cursor.movePosition(QTextCursor::End, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Backspace) {
				e->ignore();
			}
			if (check) {
				if (oldPosition == cursor.position()) {
					e->ignore();
				} else {
					setTextCursor(cursor);
				}
			}
		}
		if (!processMarkdownReplaces(text)) {
			processInstantReplaces(text);
		}
	}
}

TextWithTags InputField::getTextWithTagsSelected() const {
	const auto cursor = textCursor();
	const auto start = cursor.selectionStart();
	const auto end = cursor.selectionEnd();
	return (end > start) ? getTextWithTagsPart(start, end) : TextWithTags();
}

bool InputField::handleMarkdownKey(QKeyEvent *e) {
	if (!_markdownEnabled) {
		return false;
	}
	const auto matches = [&](const QKeySequence &sequence) {
		const auto searchKey = (e->modifiers() | e->key())
			& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
		const auto events = QKeySequence(searchKey);
		return sequence.matches(events) == QKeySequence::ExactMatch;
	};
	if (e == QKeySequence::Bold) {
		toggleSelectionMarkdown(kTagBold);
	} else if (e == QKeySequence::Italic) {
		toggleSelectionMarkdown(kTagItalic);
	} else if (e == QKeySequence::Underline) {
		toggleSelectionMarkdown(kTagUnderline);
	} else if (matches(kStrikeOutSequence)) {
		toggleSelectionMarkdown(kTagStrikeOut);
	} else if (matches(kMonospaceSequence)) {
		toggleSelectionMarkdown(kTagCode);
	} else if (matches(kClearFormatSequence)) {
		clearSelectionMarkdown();
	} else if (matches(kEditLinkSequence) && _editLinkCallback) {
		const auto cursor = textCursor();
		editMarkdownLink({
			cursor.selectionStart(),
			cursor.selectionEnd()
		});
	} else {
		return false;
	}
	return true;
}

auto InputField::selectionEditLinkData(EditLinkSelection selection) const
-> EditLinkData {
	Expects(_editLinkCallback != nullptr);

	const auto position = (selection.from == selection.till
		&& selection.from > 0)
		? (selection.from - 1)
		: selection.from;
	const auto link = [&] {
		return (position != selection.till)
			? GetFullSimpleTextTag(
				getTextWithTagsPart(position, selection.till))
			: QString();
	}();
	const auto simple = EditLinkData {
		selection.from,
		selection.till,
		QString()
	};
	if (!_editLinkCallback(selection, {}, link, EditLinkAction::Check)) {
		return simple;
	}
	Assert(!link.isEmpty());

	struct State {
		QTextBlock block;
		QTextBlock::iterator i;
	};
	const auto document = _inner->document();
	const auto skipInvalid = [&](State &state) {
		if (state.block == document->end()) {
			return false;
		}
		while (state.i.atEnd()) {
			state.block = state.block.next();
			if (state.block == document->end()) {
				return false;
			}
			state.i = state.block.begin();
		}
		return true;
	};
	const auto moveToNext = [&](State &state) {
		Expects(state.block != document->end());
		Expects(!state.i.atEnd());

		++state.i;
	};
	const auto moveToPrevious = [&](State &state) {
		Expects(state.block != document->end());
		Expects(!state.i.atEnd());

		while (state.i == state.block.begin()) {
			if (state.block == document->begin()) {
				state.block = document->end();
				return false;
			}
			state.block = state.block.previous();
			state.i = state.block.end();
		}
		--state.i;
		return true;
	};
	const auto stateTag = [&](const State &state) {
		const auto format = state.i.fragment().charFormat();
		return format.property(kTagProperty).toString();
	};
	const auto stateStart = [&](const State &state) {
		return state.i.fragment().position();
	};
	const auto stateEnd = [&](const State &state) {
		const auto fragment = state.i.fragment();
		return fragment.position() + fragment.length();
	};
	auto state = State{ document->findBlock(position) };
	if (state.block != document->end()) {
		state.i = state.block.begin();
	}
	for (; skipInvalid(state); moveToNext(state)) {
		const auto fragmentStart = stateStart(state);
		const auto fragmentEnd = stateEnd(state);
		if (fragmentEnd <= position) {
			continue;
		} else if (fragmentStart >= selection.till) {
			break;
		}
		if (stateTag(state) == link) {
			auto start = fragmentStart;
			auto finish = fragmentEnd;
			auto copy = state;
			while (moveToPrevious(copy) && (stateTag(copy) == link)) {
				start = stateStart(copy);
			}
			while (skipInvalid(state) && (stateTag(state) == link)) {
				finish = stateEnd(state);
				moveToNext(state);
			}
			return { start, finish, link };
		}
	}
	return simple;
}

auto InputField::editLinkSelection(QContextMenuEvent *e) const
-> EditLinkSelection {
	const auto cursor = textCursor();
	if (!cursor.hasSelection() && e->reason() == QContextMenuEvent::Mouse) {
		const auto clickCursor = _inner->cursorForPosition(
			_inner->viewport()->mapFromGlobal(e->globalPos()));
		if (!clickCursor.isNull() && !clickCursor.hasSelection()) {
			return {
				clickCursor.position(),
				clickCursor.position()
			};
		}
	}
	return {
		cursor.selectionStart(),
		cursor.selectionEnd()
	};
}

void InputField::editMarkdownLink(EditLinkSelection selection) {
	if (!_editLinkCallback) {
		return;
	}
	const auto data = selectionEditLinkData(selection);
	_editLinkCallback(
		selection,
		getTextWithTagsPart(data.from, data.till).text,
		data.link,
		EditLinkAction::Edit);
}

void InputField::inputMethodEventInner(QInputMethodEvent *e) {
	const auto preedit = e->preeditString();
	if (_lastPreEditText != preedit) {
		_lastPreEditText = preedit;
		startPlaceholderAnimation();
	}
	const auto text = e->commitString();
	_inner->QTextEdit::inputMethodEvent(e);
	if (!processMarkdownReplaces(text)) {
		processInstantReplaces(text);
	}
}

const InstantReplaces &InputField::instantReplaces() const {
	return _mutableInstantReplaces;
}

// Disable markdown instant replacement.
bool InputField::processMarkdownReplaces(const QString &appended) {
	//if (appended.size() != 1 || !_markdownEnabled) {
	//	return false;
	//}
	//const auto ch = appended[0];
	//if (ch == '`') {
	//	return processMarkdownReplace(kTagCode)
	//		|| processMarkdownReplace(kTagPre);
	//} else if (ch == '*') {
	//	return processMarkdownReplace(kTagBold);
	//} else if (ch == '_') {
	//	return processMarkdownReplace(kTagItalic);
	//}
	return false;
}

//bool InputField::processMarkdownReplace(const QString &tag) {
//	const auto position = textCursor().position();
//	const auto tagLength = tag.size();
//	const auto start = [&] {
//		for (const auto &possible : _lastMarkdownTags) {
//			const auto end = possible.start + possible.length;
//			if (possible.start + 2 * tagLength >= position) {
//				return MarkdownTag();
//			} else if (end >= position || end + tagLength == position) {
//				if (possible.tag == tag) {
//					return possible;
//				}
//			}
//		}
//		return MarkdownTag();
//	}();
//	if (start.tag.isEmpty()) {
//		return false;
//	}
//	return commitMarkdownReplacement(start.start, position, tag, tag);
//}

void InputField::processInstantReplaces(const QString &appended) {
	const auto &replaces = instantReplaces();
	if (appended.size() != 1
		|| !_instantReplacesEnabled
		|| !replaces.maxLength) {
		return;
	}
	const auto it = replaces.reverseMap.tail.find(appended[0]);
	if (it == end(replaces.reverseMap.tail)) {
		return;
	}
	const auto position = textCursor().position();
	for (const auto &tag : _lastMarkdownTags) {
		if (tag.internalStart < position
			&& tag.internalStart + tag.internalLength >= position
			&& (tag.tag == kTagCode || tag.tag == kTagPre)) {
			return;
		}
	}
	const auto typed = getTextWithTagsPart(
		std::max(position - replaces.maxLength, 0),
		position - 1).text;
	auto node = &it->second;
	auto i = typed.size();
	do {
		if (!node->text.isEmpty()) {
			applyInstantReplace(typed.mid(i) + appended, node->text);
			return;
		} else if (!i) {
			return;
		}
		const auto it = node->tail.find(typed[--i]);
		if (it == end(node->tail)) {
			return;
		}
		node = &it->second;
	} while (true);
}

void InputField::applyInstantReplace(
		const QString &what,
		const QString &with) {
	const auto length = int(what.size());
	const auto cursor = textCursor();
	const auto position = cursor.position();
	if (cursor.hasSelection()) {
		return;
	} else if (position < length) {
		return;
	}
	commitInstantReplacement(position - length, position, with, what, true);
}

void InputField::commitInstantReplacement(
		int from,
		int till,
		const QString &with) {
	commitInstantReplacement(from, till, with, std::nullopt, false);
}

void InputField::commitInstantReplacement(
		int from,
		int till,
		const QString &with,
		std::optional<QString> checkOriginal,
		bool checkIfInMonospace) {
	const auto original = getTextWithTagsPart(from, till).text;
	if (checkOriginal
		&& checkOriginal->compare(original, Qt::CaseInsensitive) != 0) {
		return;
	}

	auto cursor = textCursor();
	if (checkIfInMonospace) {
		const auto currentTag = cursor.charFormat().property(kTagProperty);
		if (currentTag == kTagPre || currentTag == kTagCode) {
			return;
		}
	}
	cursor.setPosition(from);
	cursor.setPosition(till, QTextCursor::KeepAnchor);

	auto format = [&]() -> QTextCharFormat {
		auto emojiLength = 0;
		const auto emoji = Ui::Emoji::Find(with, &emojiLength);
		if (!emoji || with.size() != emojiLength) {
			return _defaultCharFormat;
		}
		const auto use = [&] {
			if (!emoji->hasVariants()) {
				return emoji;
			}
			const auto nonColored = emoji->nonColoredId();
			const auto it = cEmojiVariants().constFind(nonColored);
			return (it != cEmojiVariants().cend())
				? emoji->variant(it.value())
				: emoji;
		}();
		Ui::Emoji::AddRecent(use);
		return PrepareEmojiFormat(use, _st.font);
	}();
	const auto replacement = format.isImageFormat()
		? kObjectReplacement
		: with;
	format.setProperty(kInstantReplaceWhatId, original);
	format.setProperty(kInstantReplaceWithId, replacement);
	format.setProperty(kInstantReplaceRandomId, rand_value<uint32>());
	ApplyTagFormat(format, cursor.charFormat());
	cursor.insertText(replacement, format);
}

bool InputField::commitMarkdownReplacement(
		int from,
		int till,
		const QString &tag,
		const QString &edge) {
	const auto end = [&] {
		auto cursor = QTextCursor(document()->docHandle(), 0);
		cursor.movePosition(QTextCursor::End);
		return cursor.position();
	}();

	// In case of 'pre' tag extend checked text by one symbol.
	// So that we'll know if we need to insert additional newlines.
	// "Test ```test``` Test" should become three-line text.
	const auto blocktag = (tag == kTagPre);
	const auto extendLeft = (blocktag && from > 0) ? 1 : 0;
	const auto extendRight = (blocktag && till < end) ? 1 : 0;
	const auto extended = getTextWithTagsPart(
		from - extendLeft,
		till + extendRight).text;
	const auto outer = extended.midRef(
		extendLeft,
		extended.size() - extendLeft - extendRight);
	if ((outer.size() <= 2 * edge.size())
		|| (!edge.isEmpty()
			&& !(outer.startsWith(edge) && outer.endsWith(edge)))) {
		return false;
	}

	// In case of 'pre' tag check if we need to remove one of two newlines.
	// "Test\n```\ntest\n```" should become two-line text + newline.
	const auto innerRight = edge.size();
	const auto checkIfTwoNewlines = blocktag
		&& (extendLeft > 0)
		&& IsNewline(extended[0]);
	const auto innerLeft = [&] {
		const auto simple = edge.size();
		if (!checkIfTwoNewlines) {
			return simple;
		}
		const auto last = outer.size() - innerRight;
		for (auto check = simple; check != last; ++check) {
			const auto ch = outer.at(check);
			if (IsNewline(ch)) {
				return check + 1;
			} else if (!chIsSpace(ch)) {
				break;
			}
		}
		return simple;
	}();
	const auto innerLength = outer.size() - innerLeft - innerRight;

	// Prepare the final "insert" replacement for the "outer" text part.
	const auto newlineleft = blocktag
		&& (extendLeft > 0)
		&& !IsNewline(extended[0])
		&& !IsNewline(outer.at(innerLeft));
	const auto newlineright = blocktag
		&& (!extendRight || !IsNewline(extended[extended.size() - 1]))
		&& !IsNewline(outer.at(outer.size() - innerRight - 1));
	const auto insert = (newlineleft ? "\n" : "")
		+ outer.mid(innerLeft, innerLength).toString()
		+ (newlineright ? "\n" : "");

	// Trim inserted tag, so that all newlines are left outside.
	_insertedTags.clear();
	auto tagFrom = newlineleft ? 1 : 0;
	auto tagTill = insert.size() - (newlineright ? 1 : 0);
	for (; tagFrom != tagTill; ++tagFrom) {
		const auto ch = insert.at(tagFrom);
		if (!IsNewline(ch)) {
			break;
		}
	}
	for (; tagTill != tagFrom; --tagTill) {
		const auto ch = insert.at(tagTill - 1);
		if (!IsNewline(ch)) {
			break;
		}
	}
	if (tagTill > tagFrom) {
		_insertedTags.push_back({
			tagFrom,
			tagTill - tagFrom,
			tag,
		});
	}

	// Replace.
	auto cursor = _inner->textCursor();
	cursor.setPosition(from);
	cursor.setPosition(till, QTextCursor::KeepAnchor);
	auto format = _defaultCharFormat;
	if (!edge.isEmpty()) {
		format.setProperty(kReplaceTagId, edge);
		_reverseMarkdownReplacement = true;
	}
	_insertedTagsAreFromMime = false;
	cursor.insertText(insert, format);
	_insertedTags.clear();

	cursor.setCharFormat(_defaultCharFormat);
	_inner->setTextCursor(cursor);
	return true;
}

bool InputField::IsValidMarkdownLink(const QString &link) {
	return ::Ui::IsValidMarkdownLink(link);
}

void InputField::commitMarkdownLinkEdit(
		EditLinkSelection selection,
		const QString &text,
		const QString &link) {
	if (text.isEmpty()
		|| !IsValidMarkdownLink(link)
		|| !_editLinkCallback) {
		return;
	}
	_insertedTags.clear();
	_insertedTags.push_back({ 0, text.size(), link });

	auto cursor = textCursor();
	const auto editData = selectionEditLinkData(selection);
	cursor.setPosition(editData.from);
	cursor.setPosition(editData.till, QTextCursor::KeepAnchor);
	auto format = _defaultCharFormat;
	_insertedTagsAreFromMime = false;
	cursor.insertText(
		(editData.from == editData.till) ? (text + QChar(' ')) : text,
		_defaultCharFormat);
	_insertedTags.clear();

	_reverseMarkdownReplacement = false;
	cursor.setCharFormat(_defaultCharFormat);
	_inner->setTextCursor(cursor);
}

void InputField::toggleSelectionMarkdown(const QString &tag) {
	_reverseMarkdownReplacement = false;
	const auto cursor = textCursor();
	const auto position = cursor.position();
	const auto from = cursor.selectionStart();
	const auto till = cursor.selectionEnd();
	if (from == till) {
		return;
	}
	if (tag.isEmpty()
		|| GetFullSimpleTextTag(getTextWithTagsSelected()) == tag) {
		RemoveDocumentTags(_st, document(), from, till);
		return;
	}
	const auto commitTag = [&] {
		if (tag != kTagCode) {
			return tag;
		}
		const auto leftForBlock = [&] {
			if (!from) {
				return true;
			}
			const auto text = getTextWithTagsPart(from - 1, from + 1).text;
			return text.isEmpty()
				|| IsNewline(text[0])
				|| IsNewline(text[text.size() - 1]);
		}();
		const auto rightForBlock = [&] {
			const auto text = getTextWithTagsPart(till - 1, till + 1).text;
			return text.isEmpty()
				|| IsNewline(text[0])
				|| IsNewline(text[text.size() - 1]);
		}();
		return (leftForBlock && rightForBlock) ? kTagPre : kTagCode;
	}();
	commitMarkdownReplacement(from, till, commitTag);
	auto restorePosition = textCursor();
	restorePosition.setPosition((position == till) ? from : till);
	restorePosition.setPosition(position, QTextCursor::KeepAnchor);
	setTextCursor(restorePosition);
}

void InputField::clearSelectionMarkdown() {
	toggleSelectionMarkdown(QString());
}

bool InputField::revertFormatReplace() {
	const auto cursor = textCursor();
	const auto position = cursor.position();
	if (position <= 0 || cursor.hasSelection()) {
		return false;
	}
	const auto inside = position - 1;
	const auto document = _inner->document();
	const auto block = document->findBlock(inside);
	if (block == document->end()) {
		return false;
	}
	for (auto i = block.begin(); !i.atEnd(); ++i) {
		const auto fragment = i.fragment();
		const auto fragmentStart = fragment.position();
		const auto fragmentEnd = fragmentStart + fragment.length();
		if (fragmentEnd <= inside) {
			continue;
		} else if (fragmentStart > inside || fragmentEnd != position) {
			return false;
		}
		const auto current = fragment.charFormat();
		if (current.hasProperty(kInstantReplaceWithId)) {
			const auto with = current.property(kInstantReplaceWithId);
			const auto string = with.toString();
			if (fragment.text() != string) {
				return false;
			}
			auto replaceCursor = cursor;
			replaceCursor.setPosition(fragmentStart);
			replaceCursor.setPosition(fragmentEnd, QTextCursor::KeepAnchor);
			const auto what = current.property(kInstantReplaceWhatId);
			auto format = _defaultCharFormat;
			ApplyTagFormat(format, current);
			replaceCursor.insertText(what.toString(), format);
			return true;
		} else if (_reverseMarkdownReplacement
			&& current.hasProperty(kReplaceTagId)) {
			const auto tag = current.property(kReplaceTagId).toString();
			if (tag.isEmpty()) {
				return false;
			} else if (auto test = i; !(++test).atEnd()) {
				const auto format = test.fragment().charFormat();
				if (format.property(kReplaceTagId).toString() == tag) {
					return false;
				}
			} else if (auto test = block; test.next() != document->end()) {
				const auto begin = test.begin();
				if (begin != test.end()) {
					const auto format = begin.fragment().charFormat();
					if (format.property(kReplaceTagId).toString() == tag) {
						return false;
					}
				}
			}

			const auto first = [&] {
				auto checkBlock = block;
				auto checkLast = i;
				while (true) {
					for (auto j = checkLast; j != checkBlock.begin();) {
						--j;
						const auto format = j.fragment().charFormat();
						if (format.property(kReplaceTagId) != tag) {
							return ++j;
						}
					}
					if (checkBlock == document->begin()) {
						return checkBlock.begin();
					}
					checkBlock = checkBlock.previous();
					checkLast = checkBlock.end();
				}
			}();
			const auto from = first.fragment().position();
			const auto till = fragmentEnd;
			auto replaceCursor = cursor;
			replaceCursor.setPosition(from);
			replaceCursor.setPosition(till, QTextCursor::KeepAnchor);
			replaceCursor.insertText(
				tag + getTextWithTagsPart(from, till).text + tag,
				_defaultCharFormat);
			return true;
		}
		return false;
	}
	return false;
}

void InputField::contextMenuEventInner(QContextMenuEvent *e) {
	if (const auto menu = _inner->createStandardContextMenu()) {
		addMarkdownActions(menu, e);
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(this, menu);
		_contextMenu->popup(e->globalPos());
	}
}

void InputField::addMarkdownActions(
		not_null<QMenu*> menu,
		QContextMenuEvent *e) {
	if (!_markdownEnabled) {
		return;
	}
	const auto formatting = new QAction(tr::lng_menu_formatting(tr::now), menu);
	addMarkdownMenuAction(menu, formatting);

	const auto submenu = new QMenu(menu);
	formatting->setMenu(submenu);

	const auto textWithTags = getTextWithTagsSelected();
	const auto &text = textWithTags.text;
	const auto &tags = textWithTags.tags;
	const auto hasText = !text.isEmpty();
	const auto hasTags = !tags.isEmpty();
	const auto disabled = (!_editLinkCallback && !hasText);
	formatting->setDisabled(disabled);
	if (disabled) {
		return;
	}
	const auto fullTag = GetFullSimpleTextTag(textWithTags);
	const auto add = [&](
			const QString &base,
			QKeySequence sequence,
			bool disabled,
			auto callback) {
		const auto add = sequence.isEmpty()
			? QString()
			: QChar('\t') + sequence.toString(QKeySequence::NativeText);
		const auto action = new QAction(base + add, submenu);
		connect(action, &QAction::triggered, this, callback);
		action->setDisabled(disabled);
		submenu->addAction(action);
	};
	const auto addtag = [&](
			const QString &base,
			QKeySequence sequence,
			const QString &tag) {
		const auto disabled = (fullTag == tag)
			|| (fullTag == kTagPre && tag == kTagCode);
		add(base, sequence, (!hasText || fullTag == tag), [=] {
			toggleSelectionMarkdown(tag);
		});
	};
	const auto addlink = [&] {
		const auto selection = editLinkSelection(e);
		const auto data = selectionEditLinkData(selection);
		const auto base = data.link.isEmpty()
			? tr::lng_menu_formatting_link_create(tr::now)
			: tr::lng_menu_formatting_link_edit(tr::now);
		add(base, kEditLinkSequence, false, [=] {
			editMarkdownLink(selection);
		});
	};
	const auto addclear = [&] {
		const auto disabled = !hasText || !hasTags;
		add(tr::lng_menu_formatting_clear(tr::now), kClearFormatSequence, disabled, [=] {
			clearSelectionMarkdown();
		});
	};

	addtag(tr::lng_menu_formatting_bold(tr::now), QKeySequence::Bold, kTagBold);
	addtag(tr::lng_menu_formatting_italic(tr::now), QKeySequence::Italic, kTagItalic);
	addtag(tr::lng_menu_formatting_underline(tr::now), QKeySequence::Underline, kTagUnderline);
	addtag(tr::lng_menu_formatting_strike_out(tr::now), kStrikeOutSequence, kTagStrikeOut);
	addtag(tr::lng_menu_formatting_monospace(tr::now), kMonospaceSequence, kTagCode);

	if (_editLinkCallback) {
		submenu->addSeparator();
		addlink();
	}

	submenu->addSeparator();
	addclear();
}

void InputField::addMarkdownMenuAction(
		not_null<QMenu*> menu,
		not_null<QAction*> action) {
	const auto actions = menu->actions();
	const auto before = [&] {
		auto seenAfter = false;
		for (const auto action : actions) {
			if (seenAfter) {
				return action;
			} else if (action->objectName() == qstr("edit-delete")) {
				seenAfter = true;
			}
		}
		return (QAction*)nullptr;
	}();
	menu->insertSeparator(before);
	menu->insertAction(before, action);
}

void InputField::dropEventInner(QDropEvent *e) {
	_inDrop = true;
	_inner->QTextEdit::dropEvent(e);
	_inDrop = false;
	_insertedTags.clear();
	_realInsertPosition = -1;
	window()->activateWindow();
}

bool InputField::canInsertFromMimeDataInner(const QMimeData *source) const {
	if (source
		&& _mimeDataHook
		&& _mimeDataHook(source, MimeAction::Check)) {
		return true;
	}
	return _inner->QTextEdit::canInsertFromMimeData(source);
}

void InputField::insertFromMimeDataInner(const QMimeData *source) {
	if (source
		&& _mimeDataHook
		&& _mimeDataHook(source, MimeAction::Insert)) {
		return;
	}
	const auto text = [&] {
		const auto textMime = TextUtilities::TagsTextMimeType();
		const auto tagsMime = TextUtilities::TagsMimeType();
		if (!source->hasFormat(textMime) || !source->hasFormat(tagsMime)) {
			_insertedTags.clear();
			return source->text();
		}
		auto result = QString::fromUtf8(source->data(textMime));
		_insertedTags = TextUtilities::DeserializeTags(
			source->data(tagsMime),
			result.size());
		_insertedTagsAreFromMime = true;
		return result;
	}();
	auto cursor = textCursor();
	_realInsertPosition = cursor.selectionStart();
	_realCharsAdded = text.size();
	if (_realCharsAdded > 0) {
		cursor.insertFragment(QTextDocumentFragment::fromPlainText(text));
	}
	ensureCursorVisible();
	if (!_inDrop) {
		_insertedTags.clear();
		_realInsertPosition = -1;
	}
}

void InputField::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder(_placeholderFull.current());
	_inner->setGeometry(rect().marginsRemoved(_st.textMargins));
	_borderAnimationStart = width() / 2;
	RpWidget::resizeEvent(e);
	checkContentHeight();
}

void InputField::refreshPlaceholder(const QString &text) {
	const auto availableWidth = width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		const auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(text, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			_placeholderPath.addText(0, QFontMetrics(placeholderFont).ascent(), placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(text, availableWidth);
	}
	update();
}

void InputField::setPlaceholder(
		rpl::producer<QString> placeholder,
		int afterSymbols) {
	_placeholderFull = std::move(placeholder);
	if (_placeholderAfterSymbols != afterSymbols) {
		_placeholderAfterSymbols = afterSymbols;
		startPlaceholderAnimation();
	}
}

void InputField::setEditLinkCallback(
	Fn<bool(
		EditLinkSelection selection,
		QString text,
		QString link,
		EditLinkAction action)> callback) {
	_editLinkCallback = std::move(callback);
}

void InputField::showError() {
	setErrorShown(true);
	if (!hasFocus()) {
		_inner->setFocus();
	}
}

void InputField::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start([this] { update(); }, _error ? 0. : 1., _error ? 1. : 0., _st.duration);
		startBorderAnimation();
	}
}

InputField::~InputField() = default;

MaskedInputField::MaskedInputField(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: Parent(val, parent)
, _st(st)
, _oldtext(val)
, _placeholderFull(std::move(placeholder)) {
	resize(_st.width, _st.heightMin);

	setFont(_st.font);
	setAlignment(_st.textAlign);

	_placeholderFull.value(
	) | rpl::start_with_next([=](const QString &text) {
		refreshPlaceholder(text);
	}, lifetime());

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			updatePalette();
		}
	});
	updatePalette();

	setAttribute(Qt::WA_OpaquePaintEvent);

	connect(this, SIGNAL(textChanged(const QString&)), this, SLOT(onTextChange(const QString&)));
	connect(this, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onCursorPositionChanged(int,int)));

	connect(this, SIGNAL(textEdited(const QString&)), this, SLOT(onTextEdited()));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	setStyle(InputStyle<MaskedInputField>::instance());
	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	setTextMargins(_st.textMargins);

	startPlaceholderAnimation();
	startBorderAnimation();
	finishAnimating();
}

void MaskedInputField::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Text, _st.textFg->c);
	setPalette(p);
}

void MaskedInputField::setCorrectedText(QString &now, int &nowCursor, const QString &newText, int newPos) {
	if (newPos < 0 || newPos > newText.size()) {
		newPos = newText.size();
	}
	auto updateText = (newText != now);
	if (updateText) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	auto updateCursorPosition = (newPos != nowCursor) || updateText;
	if (updateCursorPosition) {
		nowCursor = newPos;
		setCursorPosition(nowCursor);
	}
}

void MaskedInputField::customUpDown(bool custom) {
	_customUpDown = custom;
}

int MaskedInputField::borderAnimationStart() const {
	return _borderAnimationStart;
}

void MaskedInputField::setTextMargins(const QMargins &mrg) {
	_textMargins = mrg;
	refreshPlaceholder(_placeholderFull.current());
}

void MaskedInputField::onTouchTimer() {
	_touchRightButton = true;
}

bool MaskedInputField::eventHook(QEvent *e) {
	auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		auto event = static_cast<QTouchEvent*>(e);
		if (event->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(event);
		}
	}
	return Parent::eventHook(e);
}

void MaskedInputField::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchPress) return;
		auto weak = make_weak(this);
		if (!_touchMove && window()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(window()->mapFromGlobal(_touchStart));

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		}
		if (weak) {
			_touchTimer.stop();
			_touchPress = _touchMove = _touchRightButton = false;
		}
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.stop();
	} break;
	}
}

QRect MaskedInputField::getTextRect() const {
	return rect().marginsRemoved(_textMargins + QMargins(-2, -1, -2, -1));
}

void MaskedInputField::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto r = rect().intersected(e->rect());
	p.fillRect(r, _st.textBg);
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg->b);
	}
	auto errorDegree = _a_error.value(_error ? 1. : 0.);
	auto focusedDegree = _a_focused.value(_focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.value(1.);
	auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = snap(_borderAnimationStart, 0, width());
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width() - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height() - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}

	p.setClipRect(r);
	if (_st.placeholderScale > 0. && !_placeholderPath.isEmpty()) {
		auto placeholderShiftDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		p.save();
		p.setClipRect(r);

		auto placeholderTop = anim::interpolate(0, _st.placeholderShift, placeholderShiftDegree);

		QRect r(rect().marginsRemoved(_textMargins + _st.placeholderMargins));
		r.moveTop(r.top() + placeholderTop);
		if (rtl()) r.moveLeft(width() - r.left() - r.width());

		auto placeholderScale = 1. - (1. - _st.placeholderScale) * placeholderShiftDegree;
		auto placeholderFg = anim::color(_st.placeholderFg, _st.placeholderFgActive, focusedDegree);
		placeholderFg = anim::color(placeholderFg, _st.placeholderFgError, errorDegree);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(placeholderFg);
		p.translate(r.topLeft());
		p.scale(placeholderScale, placeholderScale);
		p.drawPath(_placeholderPath);

		p.restore();
	} else if (!_placeholder.isEmpty()) {
		auto placeholderHiddenDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			QRect r(rect().marginsRemoved(_textMargins + _st.placeholderMargins));
			r.moveLeft(r.left() + placeholderLeft);
			if (rtl()) r.moveLeft(width() - r.left() - r.width());

			p.setFont(_st.placeholderFont);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));
			p.drawText(r, _placeholder, _st.placeholderAlign);

			p.restore();
			p.setOpacity(1.);
		}
	}

	paintAdditionalPlaceholder(p);
	QLineEdit::paintEvent(e);
}

void MaskedInputField::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([this] { update(); }, 0., 1., _st.duration);
			} else {
				_a_borderShown.start([this] { update(); }, 0., 1., _st.duration);
			}
		} else if (qFuzzyCompare(_a_borderShown.value(1.), 0.)) {
			_a_borderShown.stop();
			_a_borderOpacity.stop();
		} else {
			_a_borderOpacity.start([this] { update(); }, 1., 0., _st.duration);
		}
	}
}

void MaskedInputField::focusInEvent(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason) ? mapFromGlobal(QCursor::pos()).x() : (width() / 2);
	setFocused(true);
	QLineEdit::focusInEvent(e);
	emit focused();
}

void MaskedInputField::focusOutEvent(QFocusEvent *e) {
	setFocused(false);
	QLineEdit::focusOutEvent(e);
	emit blurred();
}

void MaskedInputField::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start([this] { update(); }, _focused ? 0. : 1., _focused ? 1. : 0., _st.duration);
		startPlaceholderAnimation();
		startBorderAnimation();
	}
}

void MaskedInputField::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder(_placeholderFull.current());
	_borderAnimationStart = width() / 2;
	QLineEdit::resizeEvent(e);
}

void MaskedInputField::refreshPlaceholder(const QString &text) {
	const auto availableWidth = width() - _textMargins.left() - _textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		const auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(text, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			_placeholderPath.addText(0, QFontMetrics(placeholderFont).ascent(), placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(text, availableWidth);
	}
	update();
}

void MaskedInputField::setPlaceholder(rpl::producer<QString> placeholder) {
	_placeholderFull = std::move(placeholder);
}

void MaskedInputField::contextMenuEvent(QContextMenuEvent *e) {
	if (const auto menu = createStandardContextMenu()) {
		(new Ui::PopupMenu(this, menu))->popup(e->globalPos());
	}
}

void MaskedInputField::inputMethodEvent(QInputMethodEvent *e) {
	QLineEdit::inputMethodEvent(e);
	_lastPreEditText = e->preeditString();
	update();
}

void MaskedInputField::showError() {
	setErrorShown(true);
	if (!hasFocus()) {
		setFocus();
	}
}

void MaskedInputField::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start([this] { update(); }, _error ? 0. : 1., _error ? 1. : 0., _st.duration);
		startBorderAnimation();
	}
}

QSize MaskedInputField::sizeHint() const {
	return geometry().size();
}

QSize MaskedInputField::minimumSizeHint() const {
	return geometry().size();
}

void MaskedInputField::setDisplayFocused(bool focused) {
	setFocused(focused);
	finishAnimating();
}

void MaskedInputField::finishAnimating() {
	_a_focused.stop();
	_a_error.stop();
	_a_placeholderShifted.stop();
	_a_borderShown.stop();
	_a_borderOpacity.stop();
	update();
}

void MaskedInputField::setPlaceholderHidden(bool forcePlaceholderHidden) {
	_forcePlaceholderHidden = forcePlaceholderHidden;
	startPlaceholderAnimation();
}

void MaskedInputField::startPlaceholderAnimation() {
	auto placeholderShifted = _forcePlaceholderHidden || (_focused && _st.placeholderScale > 0.) || !getLastText().isEmpty();
	if (_placeholderShifted != placeholderShifted) {
		_placeholderShifted = placeholderShifted;
		_a_placeholderShifted.start([this] { update(); }, _placeholderShifted ? 0. : 1., _placeholderShifted ? 1. : 0., _st.duration);
	}
}

QRect MaskedInputField::placeholderRect() const {
	return rect().marginsRemoved(_textMargins + _st.placeholderMargins);
}

void MaskedInputField::placeholderAdditionalPrepare(Painter &p) {
	p.setFont(_st.font);
	p.setPen(_st.placeholderFg);
}

void MaskedInputField::keyPressEvent(QKeyEvent *e) {
	QString wasText(_oldtext);
	int32 wasCursor(_oldcursor);

	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
		e->ignore();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	auto newText = text();
	auto newCursor = cursorPosition();
	if (wasText == newText && wasCursor == newCursor) { // call correct manually
		correctValue(wasText, wasCursor, newText, newCursor);
		_oldtext = newText;
		_oldcursor = newCursor;
		if (wasText != _oldtext) emit changed();
		startPlaceholderAnimation();
	}
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
		emit cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		emit submitted(e->modifiers());
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto selected = selectedText();
		if (!selected.isEmpty() && echoMode() == QLineEdit::Normal) {
			QApplication::clipboard()->setText(selected, QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	}
}

void MaskedInputField::onTextEdited() {
	QString wasText(_oldtext), newText(text());
	int32 wasCursor(_oldcursor), newCursor(cursorPosition());

	correctValue(wasText, wasCursor, newText, newCursor);
	_oldtext = newText;
	_oldcursor = newCursor;
	if (wasText != _oldtext) emit changed();
	startPlaceholderAnimation();

	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void MaskedInputField::onTextChange(const QString &text) {
	_oldtext = QLineEdit::text();
	setErrorShown(false);
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void MaskedInputField::onCursorPositionChanged(int oldPosition, int position) {
	_oldcursor = position;
}

CountryCodeInput::CountryCodeInput(QWidget *parent, const style::InputField &st) : MaskedInputField(parent, st)
, _nosignal(false) {
}

void CountryCodeInput::startErasing(QKeyEvent *e) {
	setFocus();
	keyPressEvent(e);
}

void CountryCodeInput::codeSelected(const QString &code) {
	auto wasText = getLastText();
	auto wasCursor = cursorPosition();
	auto newText = '+' + code;
	auto newCursor = newText.size();
	setText(newText);
	_nosignal = true;
	correctValue(wasText, wasCursor, newText, newCursor);
	_nosignal = false;
	emit changed();
}

void CountryCodeInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText, addToNumber;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), start = 0, digits = 5;
	newText.reserve(oldLen + 1);
	if (oldLen && now[0] == '+') {
		if (start == oldPos) {
			newPos = newText.length();
		}
		++start;
	}
	newText += '+';
	for (int i = start; i < oldLen; ++i) {
		if (i == oldPos) {
			newPos = newText.length();
		}
		auto ch = now[i];
		if (ch.isDigit()) {
			if (!digits || !--digits) {
				addToNumber += ch;
			} else {
				newText += ch;
			}
		}
	}
	if (!addToNumber.isEmpty()) {
		auto validCode = Data::ValidPhoneCode(newText.mid(1));
		addToNumber = newText.mid(1 + validCode.length()) + addToNumber;
		newText = '+' + validCode;
	}
	setCorrectedText(now, nowCursor, newText, newPos);

	if (!_nosignal && was != newText) {
		emit codeChanged(newText.mid(1));
	}
	if (!addToNumber.isEmpty()) {
		emit addedToNumber(addToNumber);
	}
}

PhonePartInput::PhonePartInput(QWidget *parent, const style::InputField &st) : MaskedInputField(parent, st/*, tr::lng_phone_ph(tr::now)*/) {
}

void PhonePartInput::paintAdditionalPlaceholder(Painter &p) {
	if (!_pattern.isEmpty()) {
		auto t = getDisplayedText();
		auto ph = _additionalPlaceholder.mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			auto phRect = placeholderRect();
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				placeholderAdditionalPrepare(p);
				p.drawText(phRect, ph, style::al_topleft);
			}
		}
	}
}

void PhonePartInput::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Backspace && getLastText().isEmpty()) {
		emit voidBackspace(e);
	} else {
		MaskedInputField::keyPressEvent(e);
	}
}

void PhonePartInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (now[i].isDigit()) {
			++digitCount;
		}
	}
	if (digitCount > MaxPhoneTailLength) digitCount = MaxPhoneTailLength;

	bool inPart = !_pattern.isEmpty();
	int curPart = -1, leftInPart = 0;
	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos && newPos < 0) {
			newPos = newText.length();
		}

		auto ch = now[i];
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			if (inPart) {
				if (leftInPart) {
					--leftInPart;
				} else {
					newText += ' ';
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? (_pattern.at(curPart) - 1) : 0;

					++oldPos;
				}
			}
			newText += ch;
		} else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
			if (inPart) {
				if (leftInPart) {
				} else {
					newText += ch;
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? _pattern.at(curPart) : 0;
				}
			} else {
				newText += ch;
			}
		}
	}
	auto newlen = newText.size();
	while (newlen > 0 && newText.at(newlen - 1).isSpace()) {
		--newlen;
	}
	if (newlen < newText.size()) {
		newText = newText.mid(0, newlen);
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

void PhonePartInput::addedToNumber(const QString &added) {
	setFocus();
	auto wasText = getLastText();
	auto wasCursor = cursorPosition();
	auto newText = added + wasText;
	auto newCursor = newText.size();
	setText(newText);
	setCursorPosition(added.length());
	correctValue(wasText, wasCursor, newText, newCursor);
	startPlaceholderAnimation();
}

void PhonePartInput::onChooseCode(const QString &code) {
	_pattern = phoneNumberParse(code);
	if (!_pattern.isEmpty() && _pattern.at(0) == code.size()) {
		_pattern.pop_front();
	} else {
		_pattern.clear();
	}
	_additionalPlaceholder = QString();
	if (!_pattern.isEmpty()) {
		_additionalPlaceholder.reserve(20);
		for (const auto part : _pattern) {
			_additionalPlaceholder.append(' ');
			_additionalPlaceholder.append(QString(part, QChar(0x2212)));
		}
	}
	setPlaceholderHidden(!_additionalPlaceholder.isEmpty());

	auto wasText = getLastText();
	auto wasCursor = cursorPosition();
	auto newText = getLastText();
	auto newCursor = newText.size();
	correctValue(wasText, wasCursor, newText, newCursor);

	startPlaceholderAnimation();
}

PasswordInput::PasswordInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	setEchoMode(QLineEdit::Password);
}

PortInput::PortInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	if (!val.toInt() || val.toInt() > 65535) {
		setText(QString());
	}
}

void PortInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = now.size(); i < l; ++i) {
		if (now.at(i).isDigit()) {
			newText.append(now.at(i));
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	if (!newText.toInt()) {
		newText = QString();
		newPos = 0;
	} else if (newText.toInt() > 65535) {
		newText = was;
		newPos = wasCursor;
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

HexInput::HexInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	if (!QRegularExpression("^[a-fA-F0-9]+$").match(val).hasMatch()) {
		setText(QString());
	}
}

void HexInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = now.size(); i < l; ++i) {
		const auto ch = now[i];
		if ((ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'f')
			|| (ch >= 'A' && ch <= 'F')) {
			newText.append(ch);
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

UsernameInput::UsernameInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val,
	bool isLink)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	setLinkPlaceholder(
		isLink ? Core::App().createInternalLink(QString()) : QString());
}

void UsernameInput::setLinkPlaceholder(const QString &placeholder) {
	_linkPlaceholder = placeholder;
	if (!_linkPlaceholder.isEmpty()) {
		setTextMargins(style::margins(_st.textMargins.left() + _st.font->width(_linkPlaceholder), _st.textMargins.top(), _st.textMargins.right(), _st.textMargins.bottom()));
		setPlaceholderHidden(true);
	}
}

void UsernameInput::paintAdditionalPlaceholder(Painter &p) {
	if (!_linkPlaceholder.isEmpty()) {
		p.setFont(_st.font);
		p.setPen(_st.placeholderFg);
		p.drawText(QRect(_st.textMargins.left(), _st.textMargins.top(), width(), height() - _st.textMargins.top() - _st.textMargins.bottom()), _linkPlaceholder, style::al_topleft);
	}
}

void UsernameInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto newPos = nowCursor;
	auto from = 0, len = now.size();
	for (; from < len; ++from) {
		if (!now.at(from).isSpace()) {
			break;
		}
		if (newPos > 0) --newPos;
	}
	len -= from;
	if (len > kMaxUsernameLength) {
		len = kMaxUsernameLength + (now.at(from) == '@' ? 1 : 0);
	}
	for (int32 to = from + len; to > from;) {
		--to;
		if (!now.at(to).isSpace()) {
			break;
		}
		--len;
	}
	setCorrectedText(now, nowCursor, now.mid(from, len), newPos);
}

PhoneInput::PhoneInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &defaultValue,
	QString value)
: MaskedInputField(parent, st, std::move(placeholder), value)
, _defaultValue(defaultValue) {
	if (value.isEmpty()) {
		clearText();
	} else {
		auto pos = value.size();
		correctValue(QString(), 0, value, pos);
	}
}

void PhoneInput::focusInEvent(QFocusEvent *e) {
	MaskedInputField::focusInEvent(e);
	setSelection(cursorPosition(), cursorPosition());
}

void PhoneInput::clearText() {
	auto value = _defaultValue;
	setText(value);
	auto pos = value.size();
	correctValue(QString(), 0, value, pos);
}

void PhoneInput::paintAdditionalPlaceholder(Painter &p) {
	if (!_pattern.isEmpty()) {
		auto t = getDisplayedText();
		auto ph = _additionalPlaceholder.mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			auto phRect = placeholderRect();
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				placeholderAdditionalPrepare(p);
				p.drawText(phRect, ph, style::al_topleft);
			}
		}
	}
}

void PhoneInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto digits = now;
	digits.replace(QRegularExpression(qsl("[^\\d]")), QString());
	_pattern = phoneNumberParse(digits);

	QString newPlaceholder;
	if (_pattern.isEmpty()) {
		newPlaceholder = QString();
	} else if (_pattern.size() == 1 && _pattern.at(0) == digits.size()) {
		newPlaceholder = QString(_pattern.at(0) + 2, ' ') + tr::lng_contact_phone(tr::now);
	} else {
		newPlaceholder.reserve(20);
		for (int i = 0, l = _pattern.size(); i < l; ++i) {
			if (i) {
				newPlaceholder.append(' ');
			} else {
				newPlaceholder.append('+');
			}
			newPlaceholder.append(i ? QString(_pattern.at(i), QChar(0x2212)) : digits.mid(0, _pattern.at(i)));
		}
	}
	if (_additionalPlaceholder != newPlaceholder) {
		_additionalPlaceholder = newPlaceholder;
		setPlaceholderHidden(!_additionalPlaceholder.isEmpty());
		update();
	}

	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), digitCount = qMin(digits.size(), MaxPhoneCodeLength + MaxPhoneTailLength);

	bool inPart = !_pattern.isEmpty(), plusFound = false;
	int curPart = 0, leftInPart = inPart ? _pattern.at(curPart) : 0;
	newText.reserve(oldLen + 1);
	newText.append('+');
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos && newPos < 0) {
			newPos = newText.length();
		}

		QChar ch(now[i]);
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			if (inPart) {
				if (leftInPart) {
					--leftInPart;
				} else {
					newText += ' ';
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? (_pattern.at(curPart) - 1) : 0;

					++oldPos;
				}
			}
			newText += ch;
		} else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
			if (inPart) {
				if (leftInPart) {
				} else {
					newText += ch;
					++curPart;
					inPart = curPart < _pattern.size();
					leftInPart = inPart ? _pattern.at(curPart) : 0;
				}
			} else {
				newText += ch;
			}
		} else if (ch == '+') {
			plusFound = true;
		}
	}
	if (!plusFound && newText == qstr("+")) {
		newText = QString();
		newPos = 0;
	}
	int32 newlen = newText.size();
	while (newlen > 0 && newText.at(newlen - 1).isSpace()) {
		--newlen;
	}
	if (newlen < newText.size()) {
		newText = newText.mid(0, newlen);
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

} // namespace Ui
