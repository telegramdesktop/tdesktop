/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/input_fields.h"

#include "ui/widgets/popup_menu.h"
#include "mainwindow.h"
#include "ui/countryinput.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h"
#include "numbers.h"
#include "messenger.h"

namespace Ui {
namespace {

constexpr auto kMaxUsernameLength = 32;
constexpr auto kInstantReplaceRandomId = QTextFormat::UserProperty;
constexpr auto kInstantReplaceWhatId = QTextFormat::UserProperty + 1;
constexpr auto kInstantReplaceWithId = QTextFormat::UserProperty + 2;
const auto kObjectReplacementCh = QChar(QChar::ObjectReplacementCharacter);
const auto kObjectReplacement = QString::fromRawData(
	&kObjectReplacementCh,
	1);

template <typename InputClass>
class InputStyle : public QCommonStyle {
public:
	InputStyle() {
		setParent(QCoreApplication::instance());
	}

	void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = 0) const {
	}
	QRect subElementRect(SubElement r, const QStyleOption *opt, const QWidget *widget = 0) const {
		switch (r) {
			case SE_LineEditContents:
				const InputClass *w = widget ? qobject_cast<const InputClass*>(widget) : 0;
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

QTextImageFormat PrepareEmojiFormat(EmojiPtr emoji, const style::font &f) {
	const auto factor = cIntRetinaFactor();
	const auto width = Ui::Emoji::Size() + st::emojiPadding * factor * 2;
	const auto height = f->height * factor;
	auto result = QTextImageFormat();
	result.setWidth(width / factor);
	result.setHeight(height / factor);
	result.setName(emoji->toUrl());
	result.setVerticalAlignment(QTextCharFormat::AlignBaseline);
	return result;
}

} // namespace

QByteArray FlatTextarea::serializeTagsList(const TagList &tags) {
	if (tags.isEmpty()) {
		return QByteArray();
	}

	QByteArray tagsSerialized;
	{
		QDataStream stream(&tagsSerialized, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(tags.size());
		for_const (auto &tag, tags) {
			stream << qint32(tag.offset) << qint32(tag.length) << tag.id;
		}
	}
	return tagsSerialized;
}

FlatTextarea::TagList FlatTextarea::deserializeTagsList(QByteArray data, int textLength) {
	TagList result;
	if (data.isEmpty()) {
		return result;
	}

	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);

	qint32 tagCount = 0;
	stream >> tagCount;
	if (stream.status() != QDataStream::Ok) {
		return result;
	}
	if (tagCount <= 0 || tagCount > textLength) {
		return result;
	}

	for (int i = 0; i < tagCount; ++i) {
		qint32 offset = 0, length = 0;
		QString id;
		stream >> offset >> length >> id;
		if (stream.status() != QDataStream::Ok) {
			return result;
		}
		if (offset < 0 || length <= 0 || offset + length > textLength) {
			return result;
		}
		result.push_back({ offset, length, id });
	}
	return result;
}

QString FlatTextarea::tagsMimeType() {
	return qsl("application/x-td-field-tags");
}

FlatTextarea::FlatTextarea(QWidget *parent, const style::FlatTextarea &st, base::lambda<QString()> placeholderFactory, const QString &v, const TagList &tags) : TWidgetHelper<QTextEdit>(parent)
, _placeholderFactory(std::move(placeholderFactory))
, _placeholderVisible(!v.length())
, _lastTextWithTags { v, tags }
, _st(st) {
	_defaultCharFormat = textCursor().charFormat();

	setCursor(style::cur_text);
	setAcceptRichText(false);
	resize(_st.width, _st.font->height);

	setFont(_st.font->f);
	setAlignment(_st.align);

	subscribe(Lang::Current().updated(), [this] { refreshPlaceholder(); });
	refreshPlaceholder();

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			updatePalette();
		}
	});
	updatePalette();

	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	setFrameStyle(QFrame::NoFrame | QFrame::Plain);
	viewport()->setAutoFillBackground(false);

	setContentsMargins(0, 0, 0, 0);

	switch (cScale()) {
	case dbisOneAndQuarter: _fakeMargin = 1; break;
	case dbisOneAndHalf: _fakeMargin = 2; break;
	case dbisTwo: _fakeMargin = 4; break;
	}
	setStyleSheet(qsl("QTextEdit { margin: %1px; }").arg(_fakeMargin));

	viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	connect(document(), SIGNAL(contentsChange(int, int, int)), this, SLOT(onDocumentContentsChange(int, int, int)));
	connect(document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(this, SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(this, SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	if (!_lastTextWithTags.text.isEmpty()) {
		setTextWithTags(_lastTextWithTags, ClearUndoHistory);
	}
}

void FlatTextarea::addInstantReplace(
		const QString &what,
		const QString &with) {
	auto node = &_reverseInstantReplaces;
	for (auto i = what.end(), b = what.begin(); i != b;) {
		node = &node->tail.emplace(*--i, InstantReplaceNode()).first->second;
	}
	node->text = with;
	accumulate_max(_instantReplaceMaxLength, int(what.size()));
}

void FlatTextarea::enableInstantReplaces(bool enabled) {
	_instantReplacesEnabled = enabled;
}

void FlatTextarea::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Text, _st.textColor->c);
	setPalette(p);
}

TextWithTags FlatTextarea::getTextWithTagsPart(int start, int end) {
	TextWithTags result;
	result.text = getTextPart(start, end, &result.tags);
	return result;
}

void FlatTextarea::setTextWithTags(const TextWithTags &textWithTags, UndoHistoryAction undoHistoryAction) {
	_insertedTags = textWithTags.tags;
	_insertedTagsAreFromMime = false;
	_realInsertPosition = 0;
	_realCharsAdded = textWithTags.text.size();
	auto doc = document();
	auto cursor = QTextCursor(doc->docHandle(), 0);
	if (undoHistoryAction == ClearUndoHistory) {
		doc->setUndoRedoEnabled(false);
		cursor.beginEditBlock();
	} else if (undoHistoryAction == MergeWithUndoHistory) {
		cursor.joinPreviousEditBlock();
	} else {
		cursor.beginEditBlock();
	}
	cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	cursor.insertText(textWithTags.text);
	cursor.movePosition(QTextCursor::End);
	cursor.endEditBlock();
	if (undoHistoryAction == ClearUndoHistory) {
		doc->setUndoRedoEnabled(true);
	}
	_insertedTags.clear();
	_realInsertPosition = -1;
	finishPlaceholder();
}

void FlatTextarea::finishPlaceholder() {
	_a_placeholderFocused.finish();
	_a_placeholderVisible.finish();
	update();
}

void FlatTextarea::setMaxLength(int32 maxLength) {
	_maxLength = maxLength;
}

void FlatTextarea::setMinHeight(int32 minHeight) {
	_minHeight = minHeight;
	heightAutoupdated();
}

void FlatTextarea::setMaxHeight(int32 maxHeight) {
	_maxHeight = maxHeight;
	heightAutoupdated();
}

bool FlatTextarea::heightAutoupdated() {
	if (_minHeight < 0 || _maxHeight < 0 || _inHeightCheck) return false;
	_inHeightCheck = true;

	SendPendingMoveResizeEvents(this);

	int newh = ceil(document()->size().height()) + 2 * fakeMargin();
	if (newh > _maxHeight) {
		newh = _maxHeight;
	} else if (newh < _minHeight) {
		newh = _minHeight;
	}
	if (height() != newh) {
		resize(width(), newh);
		_inHeightCheck = false;
		return true;
	}
	_inHeightCheck = false;
	return false;
}

void FlatTextarea::onTouchTimer() {
	_touchRightButton = true;
}

bool FlatTextarea::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return QTextEdit::viewportEvent(e);
		}
	}
	return QTextEdit::viewportEvent(e);
}

void FlatTextarea::touchEvent(QTouchEvent *e) {
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

QRect FlatTextarea::getTextRect() const {
	return rect().marginsRemoved(_st.textMrg + st::textRectMargins);
}

int32 FlatTextarea::fakeMargin() const {
	return _fakeMargin;
}

void FlatTextarea::paintEvent(QPaintEvent *e) {
	Painter p(viewport());
	auto ms = getms();
	auto r = rect().intersected(e->rect());
	p.fillRect(r, _st.bgColor);
	auto placeholderOpacity = _a_placeholderVisible.current(ms, _placeholderVisible ? 1. : 0.);
	if (placeholderOpacity > 0.) {
		p.setOpacity(placeholderOpacity);

		auto placeholderLeft = anim::interpolate(_st.phShift, 0, placeholderOpacity);

		p.save();
		p.setClipRect(r);
		p.setFont(_st.font);
		p.setPen(anim::pen(_st.phColor, _st.phFocusColor, _a_placeholderFocused.current(ms, _focused ? 1. : 0.)));
		if (_st.phAlign == style::al_topleft && _placeholderAfterSymbols > 0) {
			int skipWidth = placeholderSkipWidth();
			p.drawText(_st.textMrg.left() - _fakeMargin + placeholderLeft + skipWidth, _st.textMrg.top() - _fakeMargin - st::lineWidth + _st.font->ascent, _placeholder);
		} else {
			QRect phRect(_st.textMrg.left() - _fakeMargin + _st.phPos.x() + placeholderLeft, _st.textMrg.top() - _fakeMargin + _st.phPos.y(), width() - _st.textMrg.left() - _st.textMrg.right(), height() - _st.textMrg.top() - _st.textMrg.bottom());
			p.drawText(phRect, _placeholder, QTextOption(_st.phAlign));
		}
		p.restore();
		p.setOpacity(1);
	}
	QTextEdit::paintEvent(e);
}

int FlatTextarea::placeholderSkipWidth() const {
	if (!_placeholderAfterSymbols) {
		return 0;
	}
	auto text = getTextWithTags().text;
	auto result = _st.font->width(text.mid(0, _placeholderAfterSymbols));
	if (_placeholderAfterSymbols > text.size()) {
		result += _st.font->spacew;
	}
	return result;
}

void FlatTextarea::focusInEvent(QFocusEvent *e) {
	if (!_focused) {
		_focused = true;
		_a_placeholderFocused.start([this] { update(); }, 0., 1., _st.phDuration);
		update();
	}
	QTextEdit::focusInEvent(e);
}

void FlatTextarea::focusOutEvent(QFocusEvent *e) {
	if (_focused) {
		_focused = false;
		_a_placeholderFocused.start([this] { update(); }, 1., 0., _st.phDuration);
		update();
	}
	QTextEdit::focusOutEvent(e);
}

QSize FlatTextarea::sizeHint() const {
	return geometry().size();
}

QSize FlatTextarea::minimumSizeHint() const {
	return geometry().size();
}

EmojiPtr FlatTextarea::getSingleEmoji() const {
	QString text;
	QTextFragment fragment;

	getSingleEmojiFragment(text, fragment);

	if (!text.isEmpty()) {
		auto format = fragment.charFormat();
		auto imageName = static_cast<QTextImageFormat*>(&format)->name();
		return Ui::Emoji::FromUrl(imageName);
	}
	return nullptr;
}

QString FlatTextarea::getInlineBotQuery(UserData **outInlineBot, QString *outInlineBotUsername) const {
	Assert(outInlineBot != nullptr);
	Assert(outInlineBotUsername != nullptr);

	auto &text = getTextWithTags().text;
	auto textLength = text.size();

	int inlineUsernameStart = 1, inlineUsernameLength = 0;
	if (textLength > 2 && text.at(0) == '@' && text.at(1).isLetter()) {
		inlineUsernameLength = 1;
		for (int i = inlineUsernameStart + 1; i != textLength; ++i) {
			if (text.at(i).isLetterOrNumber() || text.at(i).unicode() == '_') {
				++inlineUsernameLength;
				continue;
			}
			if (!text.at(i).isSpace()) {
				inlineUsernameLength = 0;
			}
			break;
		}
		auto inlineUsernameEnd = inlineUsernameStart + inlineUsernameLength;
		auto inlineUsernameEqualsText = (inlineUsernameEnd == textLength);
		auto validInlineUsername = false;
		if (inlineUsernameEqualsText) {
			validInlineUsername = text.endsWith(qstr("bot"));
		} else if (inlineUsernameEnd < textLength && inlineUsernameLength) {
			validInlineUsername = text.at(inlineUsernameEnd).isSpace();
		}
		if (validInlineUsername) {
			auto username = text.midRef(inlineUsernameStart, inlineUsernameLength);
			if (username != *outInlineBotUsername) {
				*outInlineBotUsername = username.toString();
				auto peer = App::peerByName(*outInlineBotUsername);
				if (peer) {
					if (peer->isUser()) {
						*outInlineBot = peer->asUser();
					} else {
						*outInlineBot = nullptr;
					}
				} else {
					*outInlineBot = LookingUpInlineBot;
				}
			}
			if (*outInlineBot == LookingUpInlineBot) return QString();

			if (*outInlineBot && (!(*outInlineBot)->botInfo || (*outInlineBot)->botInfo->inlinePlaceholder.isEmpty())) {
				*outInlineBot = nullptr;
			} else {
				return inlineUsernameEqualsText ? QString() : text.mid(inlineUsernameEnd + 1);
			}
		} else {
			inlineUsernameLength = 0;
		}
	}
	if (inlineUsernameLength < 3) {
		*outInlineBot = nullptr;
		*outInlineBotUsername = QString();
	}
	return QString();
}

QString FlatTextarea::getMentionHashtagBotCommandPart(bool &start) const {
	start = false;

	int32 pos = textCursor().position();
	if (textCursor().anchor() != pos) return QString();

	// check mention / hashtag / bot command
	QTextDocument *doc(document());
	QTextBlock block = doc->findBlock(pos);
	for (QTextBlock::Iterator iter = block.begin(); !iter.atEnd(); ++iter) {
		QTextFragment fr(iter.fragment());
		if (!fr.isValid()) continue;

		int32 p = fr.position(), e = (p + fr.length());
		if (p >= pos || e < pos) continue;

		const auto f = fr.charFormat();
		if (f.isImageFormat()) continue;

		bool mentionInCommand = false;
		QString t(fr.text());
		for (int i = pos - p; i > 0; --i) {
			if (t.at(i - 1) == '@') {
				if ((pos - p - i < 1 || t.at(i).isLetter()) && (i < 2 || !(t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_'))) {
					start = (i == 1) && (p == 0);
					return t.mid(i - 1, pos - p - i + 1);
				} else if ((pos - p - i < 1 || t.at(i).isLetter()) && i > 2 && (t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_') && !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				return QString();
			} else if (t.at(i - 1) == '#') {
				if (i < 2 || !(t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_')) {
					start = (i == 1) && (p == 0);
					return t.mid(i - 1, pos - p - i + 1);
				}
				return QString();
			} else if (t.at(i - 1) == '/') {
				if (i < 2) {
					start = (i == 1) && (p == 0);
					return t.mid(i - 1, pos - p - i + 1);
				}
				return QString();
			}
			if (pos - p - i > 127 || (!mentionInCommand && (pos - p - i > 63))) break;
			if (!t.at(i - 1).isLetterOrNumber() && t.at(i - 1) != '_') break;
		}
		break;
	}
	return QString();
}

void FlatTextarea::insertTag(const QString &text, QString tagId) {
	auto cursor = textCursor();
	int32 pos = cursor.position();

	auto doc = document();
	auto block = doc->findBlock(pos);
	for (auto iter = block.begin(); !iter.atEnd(); ++iter) {
		auto fragment = iter.fragment();
		Assert(fragment.isValid());

		int fragmentPosition = fragment.position();
		int fragmentEnd = (fragmentPosition + fragment.length());
		if (fragmentPosition >= pos || fragmentEnd < pos) continue;

		auto format = fragment.charFormat();
		if (format.isImageFormat()) continue;

		bool mentionInCommand = false;
		auto fragmentText = fragment.text();
		for (int i = pos - fragmentPosition; i > 0; --i) {
			auto previousChar = fragmentText.at(i - 1);
			if (previousChar == '@' || previousChar == '#' || previousChar == '/') {
				if ((i == pos - fragmentPosition || (previousChar == '/' ? fragmentText.at(i).isLetterOrNumber() : fragmentText.at(i).isLetter()) || previousChar == '#') &&
					(i < 2 || !(fragmentText.at(i - 2).isLetterOrNumber() || fragmentText.at(i - 2) == '_'))) {
					cursor.setPosition(fragmentPosition + i - 1);
					int till = fragmentPosition + i;
					for (; (till < fragmentEnd && till < pos); ++till) {
						auto ch = fragmentText.at(till - fragmentPosition);
						if (!ch.isLetterOrNumber() && ch != '_' && ch != '@') {
							break;
						}
					}
					if (till < fragmentEnd && fragmentText.at(till - fragmentPosition) == ' ') {
						++till;
					}
					cursor.setPosition(till, QTextCursor::KeepAnchor);
					break;
				} else if ((i == pos - fragmentPosition || fragmentText.at(i).isLetter()) && fragmentText.at(i - 1) == '@' && i > 2 && (fragmentText.at(i - 2).isLetterOrNumber() || fragmentText.at(i - 2) == '_') && !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				break;
			}
			if (pos - fragmentPosition - i > 127 || (!mentionInCommand && (pos - fragmentPosition - i > 63))) break;
			if (!fragmentText.at(i - 1).isLetterOrNumber() && fragmentText.at(i - 1) != '_') break;
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

void FlatTextarea::setTagMimeProcessor(std::unique_ptr<TagMimeProcessor> &&processor) {
	_tagMimeProcessor = std::move(processor);
}

void FlatTextarea::getSingleEmojiFragment(QString &text, QTextFragment &fragment) const {
	int32 end = textCursor().position(), start = end - 1;
	if (textCursor().anchor() != end) return;

	if (start < 0) start = 0;

	QTextDocument *doc(document());
	QTextBlock from = doc->findBlock(start), till = doc->findBlock(end);
	if (till.isValid()) till = till.next();

	for (QTextBlock b = from; b != till; b = b.next()) {
		for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
			QTextFragment fr(iter.fragment());
			if (!fr.isValid()) continue;

			int32 p = fr.position(), e = (p + fr.length());
			if (p >= end || e <= start) {
				continue;
			}

			const auto f = fr.charFormat();
			auto t = fr.text();
			if (p < start) {
				t = t.mid(start - p, end - start);
			} else if (e > end) {
				t = t.mid(0, end - p);
			}
			if (f.isImageFormat()
				&& !t.isEmpty()
				&& t[0] == kObjectReplacementCh) {
				const auto imageName = static_cast<const QTextImageFormat*>(
					&f)->name();
				if (Ui::Emoji::FromUrl(imageName)) {
					fragment = fr;
					text = t;
					return;
				}
			}
			return;
		}
	}
	return;
}

void FlatTextarea::removeSingleEmoji() {
	QString text;
	QTextFragment fragment;

	getSingleEmojiFragment(text, fragment);

	if (!text.isEmpty()) {
		QTextCursor t(textCursor());
		t.setPosition(fragment.position());
		t.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
		t.removeSelectedText();
		setTextCursor(t);
	}
}

namespace {

class TagAccumulator {
public:
	TagAccumulator(FlatTextarea::TagList *tags) : _tags(tags) {
	}

	bool changed() const {
		return _changed;
	}

	void feed(const QString &randomTagId, int currentPosition) {
		if (randomTagId == _currentTagId) return;

		if (!_currentTagId.isEmpty()) {
			int randomPartPosition = _currentTagId.lastIndexOf('/');
			Assert(randomPartPosition > 0);

			bool tagChanged = true;
			if (_currentTag < _tags->size()) {
				auto &alreadyTag = _tags->at(_currentTag);
				if (alreadyTag.offset == _currentStart &&
					alreadyTag.length == currentPosition - _currentStart &&
					alreadyTag.id == _currentTagId.midRef(0, randomPartPosition)) {
					tagChanged = false;
				}
			}
			if (tagChanged) {
				_changed = true;
				TextWithTags::Tag tag = {
					_currentStart,
					currentPosition - _currentStart,
					_currentTagId.mid(0, randomPartPosition),
				};
				if (_currentTag < _tags->size()) {
					(*_tags)[_currentTag] = tag;
				} else {
					_tags->push_back(tag);
				}
			}
			++_currentTag;
		}
		_currentTagId = randomTagId;
		_currentStart = currentPosition;
	};

	void finish() {
		if (_currentTag < _tags->size()) {
			_tags->resize(_currentTag);
			_changed = true;
		}
	}

private:
	FlatTextarea::TagList *_tags;
	bool _changed = false;

	int _currentTag = 0;
	int _currentStart = 0;
	QString _currentTagId;

};

} // namespace

QString FlatTextarea::getTextPart(int start, int end, TagList *outTagsList, bool *outTagsChanged) const {
	if (end >= 0 && end <= start) return QString();

	if (start < 0) start = 0;
	bool full = (start == 0) && (end < 0);

	TagAccumulator tagAccumulator(outTagsList);

	QTextDocument *doc(document());
	QTextBlock from = full ? doc->begin() : doc->findBlock(start), till = (end < 0) ? doc->end() : doc->findBlock(end);
	if (till.isValid()) till = till.next();

	int32 possibleLen = 0;
	for (QTextBlock b = from; b != till; b = b.next()) {
		possibleLen += b.length();
	}
	QString result;
	result.reserve(possibleLen + 1);
	if (!full && end < 0) {
		end = possibleLen;
	}

	bool tillFragmentEnd = full;
	for (auto b = from; b != till; b = b.next()) {
		for (auto iter = b.begin(); !iter.atEnd(); ++iter) {
			QTextFragment fragment(iter.fragment());
			if (!fragment.isValid()) continue;

			int32 p = full ? 0 : fragment.position(), e = full ? 0 : (p + fragment.length());
			if (!full) {
				tillFragmentEnd = (e <= end);
				if (p == end) {
					tagAccumulator.feed(fragment.charFormat().anchorName(), result.size());
				}
				if (p >= end) {
					break;
				}
				if (e <= start) {
					continue;
				}
			}
			if (full || p >= start) {
				tagAccumulator.feed(fragment.charFormat().anchorName(), result.size());
			}

			const auto f = fragment.charFormat();
			QString emojiText;
			auto t = fragment.text();
			if (!full) {
				if (p < start) {
					t = t.mid(start - p, end - start);
				} else if (e > end) {
					t = t.mid(0, end - p);
				}
			}
			QChar *ub = t.data(), *uc = ub, *ue = uc + t.size();
			for (; uc != ue; ++uc) {
				switch (uc->unicode()) {
				case 0xfdd0: // QTextBeginningOfFrame
				case 0xfdd1: // QTextEndOfFrame
				case QChar::ParagraphSeparator:
				case QChar::LineSeparator: {
					*uc = QLatin1Char('\n');
				} break;
				case QChar::Nbsp: {
					*uc = QLatin1Char(' ');
				} break;
				case QChar::ObjectReplacementCharacter: {
					if (emojiText.isEmpty() && f.isImageFormat()) {
						const auto imageName = static_cast<const QTextImageFormat*>(&f)->name();
						if (const auto emoji = Ui::Emoji::FromUrl(imageName)) {
							emojiText = emoji->text();
						}
					}
					if (uc > ub) result.append(ub, uc - ub);
					if (!emojiText.isEmpty()) result.append(emojiText);
					ub = uc + 1;
				} break;
				}
			}
			if (uc > ub) result.append(ub, uc - ub);
		}
		result.append('\n');
	}
	result.chop(1);

	if (tillFragmentEnd) tagAccumulator.feed(QString(), result.size());
	tagAccumulator.finish();

	if (outTagsChanged) {
		*outTagsChanged = tagAccumulator.changed();
	}
	return result;
}

bool FlatTextarea::hasText() const {
	QTextDocument *doc(document());
	QTextBlock from = doc->begin(), till = doc->end();

	if (from == till) return false;

	for (QTextBlock::Iterator iter = from.begin(); !iter.atEnd(); ++iter) {
		QTextFragment fragment(iter.fragment());
		if (!fragment.isValid()) continue;
		if (!fragment.text().isEmpty()) return true;
	}
	return (from.next() != till);
}

bool FlatTextarea::isUndoAvailable() const {
	return _undoAvailable;
}

bool FlatTextarea::isRedoAvailable() const {
	return _redoAvailable;
}

void FlatTextarea::parseLinks() { // some code is duplicated in text.cpp!
	LinkRanges newLinks;

	QString text(toPlainText());
	if (text.isEmpty()) {
		if (!_links.isEmpty()) {
			_links.clear();
			emit linksChanged();
		}
		return;
	}

	auto len = text.size();
	const QChar *start = text.unicode(), *end = start + text.size();
	for (auto offset = 0, matchOffset = offset; offset < len;) {
		auto m = TextUtilities::RegExpDomain().match(text, matchOffset);
		if (!m.hasMatch()) break;

		auto domainOffset = m.capturedStart();

		auto protocol = m.captured(1).toLower();
		auto topDomain = m.captured(3).toLower();
		auto isProtocolValid = protocol.isEmpty() || TextUtilities::IsValidProtocol(protocol);
		auto isTopDomainValid = !protocol.isEmpty() || TextUtilities::IsValidTopDomain(topDomain);

		if (protocol.isEmpty() && domainOffset > offset + 1 && *(start + domainOffset - 1) == QChar('@')) {
			auto forMailName = text.mid(offset, domainOffset - offset - 1);
			auto mMailName = TextUtilities::RegExpMailNameAtEnd().match(forMailName);
			if (mMailName.hasMatch()) {
				offset = matchOffset = m.capturedEnd();
				continue;
			}
		}
		if (!isProtocolValid || !isTopDomainValid) {
			offset = matchOffset = m.capturedEnd();
			continue;
		}

		QStack<const QChar*> parenth;
		const QChar *domainEnd = start + m.capturedEnd(), *p = domainEnd;
		for (; p < end; ++p) {
			QChar ch(*p);
			if (chIsLinkEnd(ch)) break; // link finished
			if (chIsAlmostLinkEnd(ch)) {
				const QChar *endTest = p + 1;
				while (endTest < end && chIsAlmostLinkEnd(*endTest)) {
					++endTest;
				}
				if (endTest >= end || chIsLinkEnd(*endTest)) {
					break; // link finished at p
				}
				p = endTest;
				ch = *p;
			}
			if (ch == '(' || ch == '[' || ch == '{' || ch == '<') {
				parenth.push(p);
			} else if (ch == ')' || ch == ']' || ch == '}' || ch == '>') {
				if (parenth.isEmpty()) break;
				const QChar *q = parenth.pop(), open(*q);
				if ((ch == ')' && open != '(') || (ch == ']' && open != '[') || (ch == '}' && open != '{') || (ch == '>' && open != '<')) {
					p = q;
					break;
				}
			}
		}
		if (p > domainEnd) { // check, that domain ended
			if (domainEnd->unicode() != '/' && domainEnd->unicode() != '?') {
				matchOffset = domainEnd - start;
				continue;
			}
		}
		newLinks.push_back({ domainOffset - 1, static_cast<int>(p - start - domainOffset + 2) });
		offset = matchOffset = p - start;
	}

	if (newLinks != _links) {
		_links = newLinks;
		emit linksChanged();
	}
}

QStringList FlatTextarea::linksList() const {
	QStringList result;
	if (!_links.isEmpty()) {
		QString text(toPlainText());
		for_const (auto &link, _links) {
			result.push_back(text.mid(link.start + 1, link.length - 2));
		}
	}
	return result;
}

void FlatTextarea::insertFromMimeData(const QMimeData *source) {
	auto mime = tagsMimeType();
	auto text = source->text();
	if (source->hasFormat(mime)) {
		auto tagsData = source->data(mime);
		_insertedTags = deserializeTagsList(tagsData, text.size());
		_insertedTagsAreFromMime = true;
	} else {
		_insertedTags.clear();
	}
	auto cursor = textCursor();
	_realInsertPosition = qMin(cursor.position(), cursor.anchor());
	_realCharsAdded = text.size();
	QTextEdit::insertFromMimeData(source);
	if (!_inDrop) {
		emit spacedReturnedPasted();
		_insertedTags.clear();
		_realInsertPosition = -1;
	}
}

void FlatTextarea::insertEmoji(EmojiPtr emoji, QTextCursor c) {
	auto format = PrepareEmojiFormat(emoji, _st.font);
	if (c.charFormat().isAnchor()) {
		format.setAnchor(true);
		format.setAnchorName(c.charFormat().anchorName());
		format.setForeground(st::defaultTextPalette.linkFg);
	}
	c.insertText(kObjectReplacement, format);
}

QVariant FlatTextarea::loadResource(int type, const QUrl &name) {
	auto imageName = name.toDisplayString();
	if (auto emoji = Ui::Emoji::FromUrl(imageName)) {
		return QVariant(App::emojiSingle(emoji, _st.font->height));
	}
	return QVariant();
}

void FlatTextarea::checkContentHeight() {
	if (heightAutoupdated()) {
		emit resized();
	}
}

namespace {

// Optimization: with null page size document does not re-layout
// on each insertText / mergeCharFormat.
void prepareFormattingOptimization(QTextDocument *document) {
	if (!document->pageSize().isNull()) {
		document->setPageSize(QSizeF(0, 0));
	}
}

void removeTags(style::color textFg, QTextDocument *document, int from, int end) {
	QTextCursor c(document->docHandle(), 0);
	c.setPosition(from);
	c.setPosition(end, QTextCursor::KeepAnchor);

	QTextCharFormat format;
	format.setAnchor(false);
	format.setAnchorName(QString());
	format.setForeground(textFg);
	c.mergeCharFormat(format);
}

// Returns the position of the first inserted tag or "changedEnd" value if none found.
int processInsertedTags(style::color textFg, QTextDocument *document, int changedPosition, int changedEnd, const FlatTextarea::TagList &tags, FlatTextarea::TagMimeProcessor *processor) {
	int firstTagStart = changedEnd;
	int applyNoTagFrom = changedEnd;
	for_const (auto &tag, tags) {
		int tagFrom = changedPosition + tag.offset;
		int tagTo = tagFrom + tag.length;
		accumulate_max(tagFrom, changedPosition);
		accumulate_min(tagTo, changedEnd);
		auto tagId = processor ? processor->tagFromMimeTag(tag.id) : tag.id;
		if (tagTo > tagFrom && !tagId.isEmpty()) {
			accumulate_min(firstTagStart, tagFrom);

			prepareFormattingOptimization(document);

			if (applyNoTagFrom < tagFrom) {
				removeTags(textFg, document, applyNoTagFrom, tagFrom);
			}
			QTextCursor c(document->docHandle(), 0);
			c.setPosition(tagFrom);
			c.setPosition(tagTo, QTextCursor::KeepAnchor);

			QTextCharFormat format;
			format.setAnchor(true);
			format.setAnchorName(tagId + '/' + QString::number(rand_value<uint32>()));
			format.setForeground(st::defaultTextPalette.linkFg);
			c.mergeCharFormat(format);

			applyNoTagFrom = tagTo;
		}
	}
	if (applyNoTagFrom < changedEnd) {
		removeTags(textFg, document, applyNoTagFrom, changedEnd);
	}

	return firstTagStart;
}

// When inserting a part of text inside a tag we need to have
// a way to know if the insertion replaced the end of the tag
// or it was strictly inside (in the middle) of the tag.
bool wasInsertTillTheEndOfTag(QTextBlock block, QTextBlock::iterator fragmentIt, int insertionEnd) {
	auto insertTagName = fragmentIt.fragment().charFormat().anchorName();
	while (true) {
		for (; !fragmentIt.atEnd(); ++fragmentIt) {
			auto fragment = fragmentIt.fragment();
			bool fragmentOutsideInsertion = (fragment.position() >= insertionEnd);
			if (fragmentOutsideInsertion) {
				return (fragment.charFormat().anchorName() != insertTagName);
			}
			int fragmentEnd = fragment.position() + fragment.length();
			bool notFullFragmentInserted = (fragmentEnd > insertionEnd);
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
		ClearInstantReplace,
	};
	Type type = Type::Invalid;
	EmojiPtr emoji = nullptr;
	bool isTilde = false;
	int intervalStart = 0;
	int intervalEnd = 0;
};

} // namespace

void FlatTextarea::processFormatting(int insertPosition, int insertEnd) {
	// Tilde formatting.
	auto tildeFormatting = !cRetina() && (font().pixelSize() == 13) && (font().family() == qstr("Open Sans"));
	auto isTildeFragment = false;
	auto tildeRegularFont = tildeFormatting ? qsl("Open Sans") : QString();
	auto tildeFixedFont = tildeFormatting ? Fonts::GetOverride(qsl("Open Sans Semibold")) : QString();

	// First tag handling (the one we inserted text to).
	bool startTagFound = false;
	bool breakTagOnNotLetter = false;

	auto doc = document();

	// Apply inserted tags.
	auto insertedTagsProcessor = _insertedTagsAreFromMime ? _tagMimeProcessor.get() : nullptr;
	int breakTagOnNotLetterTill = processInsertedTags(_st.textColor, doc, insertPosition, insertEnd,
		_insertedTags, insertedTagsProcessor);
	using ActionType = FormattingAction::Type;
	while (true) {
		FormattingAction action;

		auto fromBlock = doc->findBlock(insertPosition);
		auto tillBlock = doc->findBlock(insertEnd);
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
				if (tildeFormatting) {
					isTildeFragment = (format.fontFamily() == tildeFixedFont);
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
					auto tagName = format.anchorName();
					if (!tagName.isEmpty()) {
						breakTagOnNotLetter = wasInsertTillTheEndOfTag(block, fragmentIt, insertEnd);
					}
				}

				auto *ch = textStart + qMax(changedPositionInFragment, 0);
				for (; ch < textEnd; ++ch) {
					int emojiLength = 0;
					if (auto emoji = Ui::Emoji::Find(ch, textEnd, &emojiLength)) {
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
						++fragmentPosition;
					}
				}
				if (action.type != ActionType::Invalid) break;
			}
			if (action.type != ActionType::Invalid) break;
		}
		if (action.type != ActionType::Invalid) {
			prepareFormattingOptimization(doc);

			QTextCursor c(doc->docHandle(), 0);
			c.setPosition(action.intervalStart);
			c.setPosition(action.intervalEnd, QTextCursor::KeepAnchor);
			if (action.type == ActionType::InsertEmoji) {
				insertEmoji(action.emoji, c);
				insertPosition = action.intervalStart + 1;
			} else if (action.type == ActionType::RemoveTag) {
				QTextCharFormat format;
				format.setAnchor(false);
				format.setAnchorName(QString());
				format.setForeground(_st.textColor);
				c.mergeCharFormat(format);
			} else if (action.type == ActionType::TildeFont) {
				QTextCharFormat format;
				format.setFontFamily(action.isTilde ? tildeFixedFont : tildeRegularFont);
				c.mergeCharFormat(format);
				insertPosition = action.intervalEnd;
			} else if (action.type == ActionType::ClearInstantReplace) {
				c.setCharFormat(_defaultCharFormat);
			}
		} else {
			break;
		}
	}
}

void FlatTextarea::onDocumentContentsChange(int position, int charsRemoved, int charsAdded) {
	if (_correcting) return;

	int insertPosition = (_realInsertPosition >= 0) ? _realInsertPosition : position;
	int insertLength = (_realInsertPosition >= 0) ? _realCharsAdded : charsAdded;

	int removePosition = position;
	int removeLength = charsRemoved;

	QTextCursor(document()->docHandle(), 0).joinPreviousEditBlock();

	_correcting = true;
	if (_maxLength >= 0) {
		QTextCursor c(document()->docHandle(), 0);
		c.movePosition(QTextCursor::End);
		int32 fullSize = c.position(), toRemove = fullSize - _maxLength;
		if (toRemove > 0) {
			if (toRemove > insertLength) {
				if (insertLength) {
					c.setPosition(insertPosition);
					c.setPosition((insertPosition + insertLength), QTextCursor::KeepAnchor);
					c.removeSelectedText();
				}
				c.setPosition(fullSize - (toRemove - insertLength));
				c.setPosition(fullSize, QTextCursor::KeepAnchor);
				c.removeSelectedText();
			} else {
				c.setPosition(insertPosition + (insertLength - toRemove));
				c.setPosition(insertPosition + insertLength, QTextCursor::KeepAnchor);
				c.removeSelectedText();
			}
		}
	}
	_correcting = false;

	if (insertPosition == removePosition) {
		if (!_links.isEmpty()) {
			bool changed = false;
			for (auto i = _links.begin(); i != _links.end();) {
				if (i->start + i->length <= insertPosition) {
					++i;
				} else if (i->start >= removePosition + removeLength) {
					i->start += insertLength - removeLength;
					++i;
				} else {
					i = _links.erase(i);
					changed = true;
				}
			}
			if (changed) emit linksChanged();
		}
	} else {
		parseLinks();
	}

	if (document()->availableRedoSteps() > 0) {
		QTextCursor(document()->docHandle(), 0).endEditBlock();
		return;
	}

	if (insertLength <= 0) {
		QTextCursor(document()->docHandle(), 0).endEditBlock();
		return;
	}

	_correcting = true;
	auto pageSize = document()->pageSize();
	processFormatting(insertPosition, insertPosition + insertLength);
	if (document()->pageSize() != pageSize) {
		document()->setPageSize(pageSize);
	}
	_correcting = false;

	QTextCursor(document()->docHandle(), 0).endEditBlock();
}

void FlatTextarea::onDocumentContentsChanged() {
	if (_correcting) return;

	auto tagsChanged = false;
	auto curText = getTextPart(0, -1, &_lastTextWithTags.tags, &tagsChanged);

	_correcting = true;
	correctValue(_lastTextWithTags.text, curText, _lastTextWithTags.tags);
	_correcting = false;

	bool textOrTagsChanged = tagsChanged || (_lastTextWithTags.text != curText);
	if (textOrTagsChanged) {
		_lastTextWithTags.text = curText;
		emit changed();
		checkContentHeight();
	}
	updatePlaceholder();
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void FlatTextarea::onUndoAvailable(bool avail) {
	_undoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void FlatTextarea::onRedoAvailable(bool avail) {
	_redoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void FlatTextarea::setPlaceholder(base::lambda<QString()> placeholderFactory, int afterSymbols) {
	_placeholderFactory = std::move(placeholderFactory);
	if (_placeholderAfterSymbols != afterSymbols) {
		_placeholderAfterSymbols = afterSymbols;
		updatePlaceholder();
	}
	refreshPlaceholder();
}

void FlatTextarea::refreshPlaceholder() {
	auto skipWidth = placeholderSkipWidth();
	auto placeholderText = _placeholderFactory ? _placeholderFactory() : QString();
	_placeholder = _st.font->elided(placeholderText, width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1 - skipWidth);
	update();
}

void FlatTextarea::updatePlaceholder() {
	auto textSize = (getTextWithTags().text.size() + textCursor().block().layout()->preeditAreaText().size());
	auto placeholderVisible = (textSize <= _placeholderAfterSymbols);
	if (_placeholderVisible != placeholderVisible) {
		_placeholderVisible = placeholderVisible;
		_a_placeholderVisible.start([this] { update(); }, _placeholderVisible ? 0. : 1., _placeholderVisible ? 1. : 0., _st.phDuration);
	}
}

QMimeData *FlatTextarea::createMimeDataFromSelection() const {
	QMimeData *result = new QMimeData();
	QTextCursor c(textCursor());
	int32 start = c.selectionStart(), end = c.selectionEnd();
	if (end > start) {
		TagList tags;
		result->setText(getTextPart(start, end, &tags));
		if (!tags.isEmpty()) {
			if (_tagMimeProcessor) {
				for (auto &tag : tags) {
					tag.id = _tagMimeProcessor->mimeTagFromTag(tag.id);
				}
			}
			result->setData(tagsMimeType(), serializeTagsList(tags));
		}
	}
	return result;
}

void FlatTextarea::setSubmitSettings(SubmitSettings settings) {
	_submitSettings = settings;
}

void FlatTextarea::keyPressEvent(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
	bool macmeta = (cPlatform() == dbipMac || cPlatform() == dbipMacOld) && e->modifiers().testFlag(Qt::ControlModifier) && !e->modifiers().testFlag(Qt::MetaModifier) && !e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier);
	bool enterSubmit = (ctrl && shift);
	if (ctrl && _submitSettings != SubmitSettings::None && _submitSettings != SubmitSettings::Enter) {
		enterSubmit = true;
	}
	if (!ctrl && !shift && _submitSettings != SubmitSettings::None && _submitSettings != SubmitSettings::CtrlEnter) {
		enterSubmit = true;
	}
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);

	if (macmeta && e->key() == Qt::Key_Backspace) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (e->key() == Qt::Key_Backspace
		&& e->modifiers() == 0
		&& revertInstantReplace()) {
		e->accept();
	} else if (enter && enterSubmit) {
		emit submitted(ctrl && shift);
	} else if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	} else if (e->key() == Qt::Key_Tab || (ctrl && e->key() == Qt::Key_Backtab)) {
		if (ctrl) {
			e->ignore();
		} else {
			emit tabbed();
		}
	} else if (e->key() == Qt::Key_Search || e == QKeySequence::Find) {
		e->ignore();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto cursor = textCursor();
		int start = cursor.selectionStart(), end = cursor.selectionEnd();
		if (end > start) {
			TagList tags;
			QApplication::clipboard()->setText(getTextPart(start, end, &tags), QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	} else {
		const auto text = e->text();
		const auto key = e->key();
		auto cursor = textCursor();
		if (enter && ctrl) {
			e->setModifiers(e->modifiers() & ~Qt::ControlModifier);
		}
		bool spaceOrReturn = false;
		if (!text.isEmpty() && text.size() < 3) {
			const auto ch = text[0];
			if (ch == '\n'
				|| ch == '\r'
				|| ch.isSpace()
				|| ch == QChar::LineSeparator) {
				spaceOrReturn = true;
			}
		}
		QTextEdit::keyPressEvent(e);
		if (cursor == textCursor()) {
			bool check = false;
			if (key == Qt::Key_PageUp || key == Qt::Key_Up) {
				cursor.movePosition(
					QTextCursor::Start,
					(e->modifiers().testFlag(Qt::ShiftModifier)
						? QTextCursor::KeepAnchor
						: QTextCursor::MoveAnchor));
				check = true;
			} else if (key == Qt::Key_PageDown || key == Qt::Key_Down) {
				cursor.movePosition(
					QTextCursor::End,
					(e->modifiers().testFlag(Qt::ShiftModifier)
						? QTextCursor::KeepAnchor
						: QTextCursor::MoveAnchor));
				check = true;
			}
			if (check) {
				if (cursor == textCursor()) {
					e->ignore();
				} else {
					setTextCursor(cursor);
				}
			}
		}
		processInstantReplaces(text);
		if (spaceOrReturn) {
			emit spacedReturnedPasted();
		}
	}
}

void FlatTextarea::processInstantReplaces(const QString &text) {
	if (text.size() != 1
		|| !_instantReplaceMaxLength
		|| !_instantReplacesEnabled) {
		return;
	}
	const auto it = _reverseInstantReplaces.tail.find(text[0]);
	if (it == end(_reverseInstantReplaces.tail)) {
		return;
	}
	const auto position = textCursor().position();
	auto tags = QVector<TextWithTags::Tag>();
	const auto typed = getTextPart(
		std::max(position - _instantReplaceMaxLength, 0),
		position - 1,
		&tags);
	auto node = &it->second;
	auto i = typed.size();
	do {
		if (!node->text.isEmpty()) {
			applyInstantReplace(typed.mid(i) + text, node->text);
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

void FlatTextarea::applyInstantReplace(
		const QString &what,
		const QString &with) {
	const auto length = int(what.size());
	const auto cursor = textCursor();
	const auto position = cursor.position();
	if (cursor.anchor() != position) {
		return;
	} else if (position < length) {
		return;
	}
	commmitInstantReplacement(position - length, position, with, what);
}

void FlatTextarea::commmitInstantReplacement(
		int from,
		int till,
		const QString &with,
		base::optional<QString> checkOriginal) {
	auto tags = QVector<TextWithTags::Tag>();
	const auto original = getTextPart(from, till, &tags);
	if (checkOriginal
		&& checkOriginal->compare(original, Qt::CaseInsensitive) != 0) {
		return;
	}

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
	auto cursor = textCursor();
	cursor.setPosition(from);
	cursor.setPosition(till, QTextCursor::KeepAnchor);
	cursor.insertText(replacement, format);
}

bool FlatTextarea::revertInstantReplace() {
	const auto cursor = textCursor();
	const auto position = cursor.position();
	if (position <= 0 || cursor.anchor() != position) {
		return false;
	}
	const auto inside = position - 1;
	const auto block = document()->findBlock(inside);
	if (block == document()->end()) {
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
		const auto format = fragment.charFormat();
		const auto with = format.property(kInstantReplaceWithId);
		if (!with.isValid()) {
			return false;
		}
		const auto string = with.toString();
		if (fragment.text() != string) {
			return false;
		}
		auto replaceCursor = cursor;
		replaceCursor.setPosition(fragmentStart);
		replaceCursor.setPosition(fragmentEnd, QTextCursor::KeepAnchor);
		const auto what = format.property(kInstantReplaceWhatId).toString();
		replaceCursor.insertText(what, _defaultCharFormat);
		return true;
	}
	return false;
}

void FlatTextarea::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder();
	QTextEdit::resizeEvent(e);
	checkContentHeight();
}

void FlatTextarea::mousePressEvent(QMouseEvent *e) {
	QTextEdit::mousePressEvent(e);
}

void FlatTextarea::dropEvent(QDropEvent *e) {
	_inDrop = true;
	QTextEdit::dropEvent(e);
	_inDrop = false;
	_insertedTags.clear();
	_realInsertPosition = -1;

	emit spacedReturnedPasted();
}

void FlatTextarea::contextMenuEvent(QContextMenuEvent *e) {
	if (auto menu = createStandardContextMenu()) {
		(new Ui::PopupMenu(nullptr, menu))->popup(e->globalPos());
	}
}

FlatInput::FlatInput(QWidget *parent, const style::FlatInput &st, base::lambda<QString()> placeholderFactory, const QString &v) : TWidgetHelper<QLineEdit>(v, parent)
, _oldtext(v)
, _placeholderFactory(std::move(placeholderFactory))
, _placeholderVisible(!v.length())
, _st(st)
, _textMrg(_st.textMrg) {
	setCursor(style::cur_text);
	resize(_st.width, _st.height);

	setFont(_st.font->f);
	setAlignment(_st.align);

	subscribe(Lang::Current().updated(), [this] { refreshPlaceholder(); });
	refreshPlaceholder();

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

bool FlatInput::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return QLineEdit::event(e);
		}
	}
	return QLineEdit::event(e);
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
	refreshPlaceholder();
	update();
}

QRect FlatInput::getTextRect() const {
	return rect().marginsRemoved(_textMrg + QMargins(-2, -1, -2, -1));
}

void FlatInput::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto placeholderFocused = _a_placeholderFocused.current(ms, _focused ? 1. : 0.);

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

	auto placeholderOpacity = _a_placeholderVisible.current(ms, _placeholderVisible ? 1. : 0.);
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
		_a_placeholderFocused.start([this] { update(); }, 0., 1., _st.phDuration);
		update();
	}
	QLineEdit::focusInEvent(e);
	emit focused();
}

void FlatInput::focusOutEvent(QFocusEvent *e) {
	if (_focused) {
		_focused = false;
		_a_placeholderFocused.start([this] { update(); }, 1., 0., _st.phDuration);
		update();
	}
	QLineEdit::focusOutEvent(e);
	emit blurred();
}

void FlatInput::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder();
	return QLineEdit::resizeEvent(e);
}

void FlatInput::setPlaceholder(base::lambda<QString()> placeholderFactory) {
	_placeholderFactory = std::move(placeholderFactory);
	refreshPlaceholder();
}

void FlatInput::refreshPlaceholder() {
	auto availw = width() - _textMrg.left() - _textMrg.right() - _st.phPos.x() - 1;
	auto placeholderText = _placeholderFactory ? _placeholderFactory() : QString();
	if (_st.font->width(placeholderText) > availw) {
		_placeholder = _st.font->elided(placeholderText, availw);
	} else {
		_placeholder = placeholderText;
	}
	update();
}

void FlatInput::contextMenuEvent(QContextMenuEvent *e) {
	if (auto menu = createStandardContextMenu()) {
		(new Ui::PopupMenu(nullptr, menu))->popup(e->globalPos());
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
		_a_placeholderVisible.start([this] { update(); }, _placeholderVisible ? 0. : 1., _placeholderVisible ? 1. : 0., _st.phDuration);
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

	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = true;
	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
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
		emit submitted(ctrl && shift);
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

InputArea::InputArea(
	QWidget *parent,
	const style::InputField &st,
	base::lambda<QString()> placeholderFactory,
	const QString &val)
: RpWidget(parent)
, _st(st)
, _inner(this)
, _oldtext(val)
, _placeholderFactory(std::move(placeholderFactory)) {
	_inner->setAcceptRichText(false);
	resize(_st.width, _st.heightMin);

	setAttribute(Qt::WA_OpaquePaintEvent);

	_inner->setFont(_st.font->f);

	subscribe(Lang::Current().updated(), [this] { refreshPlaceholder(); });
	refreshPlaceholder();

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			updatePalette();
		}
	});
	updatePalette();

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
	connect(_inner->document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(_inner, SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(_inner, SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(_inner, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	setCursor(style::cur_text);
	heightAutoupdated();

	if (!val.isEmpty()) {
		_inner->setPlainText(val);
	}
	_inner->document()->clearUndoRedoStacks();

	startBorderAnimation();
	startPlaceholderAnimation();
	finishAnimating();
}

void InputArea::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Text, _st.textFg->c);
	setPalette(p);
}

void InputArea::onTouchTimer() {
	_touchRightButton = true;
}

bool InputArea::heightAutoupdated() {
	if (_st.heightMin < 0 || _st.heightMax < 0 || _inHeightCheck) return false;
	_inHeightCheck = true;

	SendPendingMoveResizeEvents(this);

	int newh = qCeil(_inner->document()->size().height()) + _st.textMargins.top() + _st.textMargins.bottom();
	if (newh > _st.heightMax) {
		newh = _st.heightMax;
	} else if (newh < _st.heightMin) {
		newh = _st.heightMin;
	}
	if (height() != newh) {
		resize(width(), newh);
		_inHeightCheck = false;
		return true;
	}
	_inHeightCheck = false;
	return false;
}

void InputArea::checkContentHeight() {
	if (heightAutoupdated()) {
		emit resized();
	}
}

InputArea::Inner::Inner(InputArea *parent) : QTextEdit(parent) {
}

bool InputArea::Inner::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			qobject_cast<InputArea*>(parentWidget())->touchEvent(ev);
			return QTextEdit::viewportEvent(e);
		}
	}
	return QTextEdit::viewportEvent(e);
}

void InputArea::touchEvent(QTouchEvent *e) {
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

void InputArea::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto r = rect().intersected(e->rect());
	p.fillRect(r, _st.textBg);
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg);
	}
	auto errorDegree = _a_error.current(ms, _error ? 1. : 0.);
	auto focusedDegree = _a_focused.current(ms, _focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.current(ms, 1.);
	auto borderOpacity = _a_borderOpacity.current(ms, _borderVisible ? 1. : 0.);
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
		auto placeholderShiftDegree = _a_placeholderShifted.current(ms, _placeholderShifted ? 1. : 0.);
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
		auto placeholderHiddenDegree = _a_placeholderShifted.current(ms, _placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			auto r = rect().marginsRemoved(_st.textMargins + _st.placeholderMargins);
			r.moveLeft(r.left() + placeholderLeft);
			if (rtl()) r.moveLeft(width() - r.left() - r.width());

			p.setFont(_st.font);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));
			p.drawText(r, _placeholder, _st.placeholderAlign);

			p.restore();
		}
	}
	TWidget::paintEvent(e);
}

void InputArea::startBorderAnimation() {
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

void InputArea::focusInEvent(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason) ? mapFromGlobal(QCursor::pos()).x() : (width() / 2);
	QTimer::singleShot(0, this, SLOT(onFocusInner()));
}

void InputArea::mousePressEvent(QMouseEvent *e) {
	_borderAnimationStart = e->pos().x();
	QTimer::singleShot(0, this, SLOT(onFocusInner()));
}

void InputArea::onFocusInner() {
	auto borderStart = _borderAnimationStart;
	_inner->setFocus();
	_borderAnimationStart = borderStart;
}

void InputArea::contextMenuEvent(QContextMenuEvent *e) {
	_inner->contextMenuEvent(e);
}

void InputArea::Inner::focusInEvent(QFocusEvent *e) {
	f()->focusInInner(e->reason() == Qt::MouseFocusReason);
	QTextEdit::focusInEvent(e);
	emit f()->focused();
}

void InputArea::focusInInner(bool focusByMouse) {
	_borderAnimationStart = focusByMouse ? mapFromGlobal(QCursor::pos()).x() : (width() / 2);
	setFocused(true);
}

void InputArea::Inner::focusOutEvent(QFocusEvent *e) {
	f()->focusOutInner();
	QTextEdit::focusOutEvent(e);
	emit f()->blurred();
}

void InputArea::focusOutInner() {
	setFocused(false);
}

void InputArea::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start([this] { update(); }, _focused ? 0. : 1., _focused ? 1. : 0., _st.duration);
		startPlaceholderAnimation();
		startBorderAnimation();
	}
}

QSize InputArea::sizeHint() const {
	return geometry().size();
}

QSize InputArea::minimumSizeHint() const {
	return geometry().size();
}

QString InputArea::getText(int32 start, int32 end) const {
	if (end >= 0 && end <= start) return QString();

	if (start < 0) start = 0;
	bool full = (start == 0) && (end < 0);

	QTextDocument *doc(_inner->document());
	QTextBlock from = full ? doc->begin() : doc->findBlock(start), till = (end < 0) ? doc->end() : doc->findBlock(end);
	if (till.isValid()) till = till.next();

	int32 possibleLen = 0;
	for (QTextBlock b = from; b != till; b = b.next()) {
		possibleLen += b.length();
	}
	QString result;
	result.reserve(possibleLen + 1);
	if (!full && end < 0) {
		end = possibleLen;
	}

	for (QTextBlock b = from; b != till; b = b.next()) {
		for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
			QTextFragment fragment(iter.fragment());
			if (!fragment.isValid()) continue;

			int32 p = full ? 0 : fragment.position(), e = full ? 0 : (p + fragment.length());
			if (!full) {
				if (p >= end || e <= start) {
					continue;
				}
			}

			QTextCharFormat f = fragment.charFormat();
			QString emojiText;
			QString t(fragment.text());
			if (!full) {
				if (p < start) {
					t = t.mid(start - p, end - start);
				} else if (e > end) {
					t = t.mid(0, end - p);
				}
			}
			QChar *ub = t.data(), *uc = ub, *ue = uc + t.size();
			for (; uc != ue; ++uc) {
				switch (uc->unicode()) {
				case 0xfdd0: // QTextBeginningOfFrame
				case 0xfdd1: // QTextEndOfFrame
				case QChar::ParagraphSeparator:
				case QChar::LineSeparator: {
					*uc = QLatin1Char('\n');
				} break;
				case QChar::Nbsp: {
					*uc = QLatin1Char(' ');
				} break;
				case QChar::ObjectReplacementCharacter: {
					if (emojiText.isEmpty() && f.isImageFormat()) {
						auto imageName = static_cast<QTextImageFormat*>(&f)->name();
						if (auto emoji = Ui::Emoji::FromUrl(imageName)) {
							emojiText = emoji->text();
						}
					}
					if (uc > ub) result.append(ub, uc - ub);
					if (!emojiText.isEmpty()) result.append(emojiText);
					ub = uc + 1;
				} break;
				}
			}
			if (uc > ub) result.append(ub, uc - ub);
		}
		result.append('\n');
	}
	result.chop(1);
	return result;
}

bool InputArea::hasText() const {
	QTextDocument *doc(_inner->document());
	QTextBlock from = doc->begin(), till = doc->end();

	if (from == till) return false;

	for (QTextBlock::Iterator iter = from.begin(); !iter.atEnd(); ++iter) {
		QTextFragment fragment(iter.fragment());
		if (!fragment.isValid()) continue;
		if (!fragment.text().isEmpty()) return true;
	}
	return (from.next() != till);
}

bool InputArea::isUndoAvailable() const {
	return _undoAvailable;
}

bool InputArea::isRedoAvailable() const {
	return _redoAvailable;
}

void InputArea::insertEmoji(EmojiPtr emoji, QTextCursor c) {
	const auto format = PrepareEmojiFormat(emoji, _st.font);
	c.insertText(kObjectReplacement, format);
}

QVariant InputArea::Inner::loadResource(int type, const QUrl &name) {
	auto imageName = name.toDisplayString();
	if (auto emoji = Ui::Emoji::FromUrl(imageName)) {
		return QVariant(App::emojiSingle(emoji, f()->_st.font->height));
	}
	return QVariant();
}

void InputArea::processDocumentContentsChange(int position, int charsAdded) {
	int32 replacePosition = -1, replaceLen = 0;
	EmojiPtr emoji = nullptr;

	// Tilde formatting.
	auto tildeFormatting = !cRetina() && (font().pixelSize() == 13) && (font().family() == qstr("Open Sans"));
	auto isTildeFragment = false;
	auto tildeRegularFont = tildeFormatting ? qsl("Open Sans") : QString();
	auto tildeFixedFont = tildeFormatting ? Fonts::GetOverride(qsl("Open Sans Semibold")) : QString();

	QTextDocument *doc(_inner->document());
	QTextCursor c(_inner->textCursor());
	c.joinPreviousEditBlock();
	while (true) {
		int32 start = position, end = position + charsAdded;
		QTextBlock from = doc->findBlock(start), till = doc->findBlock(end);
		if (till.isValid()) till = till.next();

		for (QTextBlock b = from; b != till; b = b.next()) {
			for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
				QTextFragment fragment(iter.fragment());
				if (!fragment.isValid()) continue;

				int32 fp = fragment.position(), fe = fp + fragment.length();
				if (fp >= end || fe <= start) {
					continue;
				}

				if (tildeFormatting) {
					isTildeFragment = (fragment.charFormat().fontFamily() == tildeFixedFont);
				}

				QString t(fragment.text());
				const QChar *ch = t.constData(), *e = ch + t.size();
				for (; ch != e; ++ch, ++fp) {
					int32 emojiLen = 0;
					emoji = Ui::Emoji::Find(ch, e, &emojiLen);
					if (emoji) {
						if (replacePosition >= 0) {
							emoji = 0; // replace tilde char format first
						} else {
							replacePosition = fp;
							replaceLen = emojiLen;
						}
						break;
					}

					if (tildeFormatting && fp >= position) { // tilde fix in OpenSans
						bool tilde = (ch->unicode() == '~');
						if ((tilde && !isTildeFragment) || (!tilde && isTildeFragment)) {
							if (replacePosition < 0) {
								replacePosition = fp;
								replaceLen = 1;
							} else {
								++replaceLen;
							}
						} else if (replacePosition >= 0) {
							break;
						}
					}

					if (ch + 1 < e && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) {
						++ch;
						++fp;
					}
				}
				if (replacePosition >= 0) break;
			}
			if (replacePosition >= 0) break;
		}
		if (replacePosition >= 0) {
			if (!_inner->document()->pageSize().isNull()) {
				_inner->document()->setPageSize(QSizeF(0, 0));
			}
			QTextCursor c(doc->docHandle(), 0);
			c.setPosition(replacePosition);
			c.setPosition(replacePosition + replaceLen, QTextCursor::KeepAnchor);
			if (emoji) {
				insertEmoji(emoji, c);
			} else {
				QTextCharFormat format;
				format.setFontFamily(isTildeFragment ? tildeRegularFont : tildeFixedFont);
				c.mergeCharFormat(format);
			}
			charsAdded -= replacePosition + replaceLen - position;
			position = replacePosition + (emoji ? 1 : replaceLen);

			emoji = 0;
			replacePosition = -1;
		} else {
			break;
		}
	}
	c.endEditBlock();
}

void InputArea::onDocumentContentsChange(int position, int charsRemoved, int charsAdded) {
	if (_correcting) return;

	QString oldtext(_oldtext);
	QTextCursor(_inner->document()->docHandle(), 0).joinPreviousEditBlock();

	if (!position) { // Qt bug workaround https://bugreports.qt.io/browse/QTBUG-49062
		QTextCursor c(_inner->document()->docHandle(), 0);
		c.movePosition(QTextCursor::End);
		if (position + charsAdded > c.position()) {
			int32 toSubstract = position + charsAdded - c.position();
			if (charsRemoved >= toSubstract) {
				charsAdded -= toSubstract;
				charsRemoved -= toSubstract;
			}
		}
	}

	_correcting = true;
	if (_maxLength >= 0) {
		QTextCursor c(_inner->document()->docHandle(), 0);
		c.movePosition(QTextCursor::End);
		int32 fullSize = c.position(), toRemove = fullSize - _maxLength;
		if (toRemove > 0) {
			if (toRemove > charsAdded) {
				if (charsAdded) {
					c.setPosition(position);
					c.setPosition((position + charsAdded), QTextCursor::KeepAnchor);
					c.removeSelectedText();
				}
				c.setPosition(fullSize - (toRemove - charsAdded));
				c.setPosition(fullSize, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				position = _maxLength;
				charsAdded = 0;
				charsRemoved += toRemove;
			} else {
				c.setPosition(position + (charsAdded - toRemove));
				c.setPosition(position + charsAdded, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				charsAdded -= toRemove;
			}
		}
	}
	_correcting = false;

	QTextCursor(_inner->document()->docHandle(), 0).endEditBlock();

	if (_inner->document()->availableRedoSteps() > 0) return;

	const int takeBack = 3;

	position -= takeBack;
	charsAdded += takeBack;
	if (position < 0) {
		charsAdded += position;
		position = 0;
	}
	if (charsAdded <= 0) return;

	_correcting = true;
	QSizeF s = _inner->document()->pageSize();
	processDocumentContentsChange(position, charsAdded);
	if (_inner->document()->pageSize() != s) {
		_inner->document()->setPageSize(s);
	}
	_correcting = false;
}

void InputArea::onDocumentContentsChanged() {
	if (_correcting) return;

	setErrorShown(false);

	auto curText = getText();
	if (_oldtext != curText) {
		_oldtext = curText;
		emit changed();
		checkContentHeight();
	}
	startPlaceholderAnimation();
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputArea::onUndoAvailable(bool avail) {
	_undoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputArea::onRedoAvailable(bool avail) {
	_redoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputArea::setDisplayFocused(bool focused) {
	setFocused(focused);
	finishAnimating();
}

void InputArea::finishAnimating() {
	_a_focused.finish();
	_a_error.finish();
	_a_placeholderShifted.finish();
	_a_borderShown.finish();
	_a_borderOpacity.finish();
	update();
}

void InputArea::startPlaceholderAnimation() {
	auto placeholderShifted = (_focused && _st.placeholderScale > 0.) || !getLastText().isEmpty();
	if (_placeholderShifted != placeholderShifted) {
		_placeholderShifted = placeholderShifted;
		_a_placeholderShifted.start([this] { update(); }, _placeholderShifted ? 0. : 1., _placeholderShifted ? 1. : 0., _st.duration);
	}
}

QMimeData *InputArea::Inner::createMimeDataFromSelection() const {
	QMimeData *result = new QMimeData();
	QTextCursor c(textCursor());
	int32 start = c.selectionStart(), end = c.selectionEnd();
	if (end > start) {
		result->setText(f()->getText(start, end));
	}
	return result;
}

void InputArea::customUpDown(bool custom) {
	_customUpDown = custom;
}

void InputArea::setCtrlEnterSubmit(CtrlEnterSubmit ctrlEnterSubmit) {
	_ctrlEnterSubmit = ctrlEnterSubmit;
}

void InputArea::Inner::keyPressEvent(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool macmeta = (cPlatform() == dbipMac || cPlatform() == dbipMacOld) && e->modifiers().testFlag(Qt::ControlModifier) && !e->modifiers().testFlag(Qt::MetaModifier) && !e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier);
	bool ctrlGood = (ctrl && shift) ||
		(ctrl && (f()->_ctrlEnterSubmit == CtrlEnterSubmit::CtrlEnter || f()->_ctrlEnterSubmit == CtrlEnterSubmit::Both)) ||
		(!ctrl && !shift && (f()->_ctrlEnterSubmit == CtrlEnterSubmit::Enter || f()->_ctrlEnterSubmit == CtrlEnterSubmit::Both));
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);

	if (macmeta && e->key() == Qt::Key_Backspace) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (enter && ctrlGood) {
		emit f()->submitted(ctrl && shift);
	} else if (e->key() == Qt::Key_Escape) {
		e->ignore();
		emit f()->cancelled();
	} else if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
		if (alt || ctrl) {
			e->ignore();
		} else {
			if (!focusNextPrevChild(e->key() == Qt::Key_Tab && !shift)) {
				e->ignore();
			}
		}
	} else if (e->key() == Qt::Key_Search || e == QKeySequence::Find) {
		e->ignore();
	} else if (f()->_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto cursor = textCursor();
		int start = cursor.selectionStart(), end = cursor.selectionEnd();
		if (end > start) {
			QApplication::clipboard()->setText(f()->getText(start, end), QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	} else {
		QTextCursor tc(textCursor());
		if (enter && ctrl) {
			e->setModifiers(e->modifiers() & ~Qt::ControlModifier);
		}
		QTextEdit::keyPressEvent(e);
		if (tc == textCursor()) {
			bool check = false;
			if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_Up) {
				tc.movePosition(QTextCursor::Start, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_Down) {
				tc.movePosition(QTextCursor::End, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			}
			if (check) {
				if (tc == textCursor()) {
					e->ignore();
				} else {
					setTextCursor(tc);
				}
			}
		}
	}
}

void InputArea::Inner::contextMenuEvent(QContextMenuEvent *e) {
	if (auto menu = createStandardContextMenu()) {
		(new Ui::PopupMenu(nullptr, menu))->popup(e->globalPos());
	}
}

bool InputArea::Inner::canInsertFromMimeData(const QMimeData *source) const {
	if (source
		&& f()->_mimeDataHook
		&& f()->_mimeDataHook(source, MimeAction::Check)) {
		return true;
	}
	return QTextEdit::canInsertFromMimeData(source);
}

void InputArea::Inner::insertFromMimeData(const QMimeData *source) {
	if (source
		&& f()->_mimeDataHook
		&& f()->_mimeDataHook(source, MimeAction::Insert)) {
		return;
	}
	return QTextEdit::insertFromMimeData(source);
}

void InputArea::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder();
	_inner->setGeometry(rect().marginsRemoved(_st.textMargins));
	_borderAnimationStart = width() / 2;
	TWidget::resizeEvent(e);
	checkContentHeight();
}

void InputArea::refreshPlaceholder() {
	auto placeholderText = _placeholderFactory ? _placeholderFactory() : QString();
	auto availableWidth = width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(placeholderText, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			_placeholderPath.addText(0, QFontMetrics(placeholderFont).ascent(), placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(placeholderText, availableWidth);
	}
	update();
}

void InputArea::setPlaceholder(base::lambda<QString()> placeholderFactory) {
	_placeholderFactory = std::move(placeholderFactory);
	refreshPlaceholder();
}

void InputArea::showError() {
	setErrorShown(true);
	if (!hasFocus()) {
		_inner->setFocus();
	}
}

void InputArea::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start([this] { update(); }, _error ? 0. : 1., _error ? 1. : 0., _st.duration);
		startBorderAnimation();
	}
}

InputField::InputField(
	QWidget *parent,
	const style::InputField &st,
	base::lambda<QString()> placeholderFactory,
	const QString &val)
: RpWidget(parent)
, _st(st)
, _inner(std::make_unique<Inner>(this))
, _oldtext(val)
, _placeholderFactory(std::move(placeholderFactory)) {
	_inner->setAcceptRichText(false);
	resize(_st.width, _st.heightMin);

	_inner->setWordWrapMode(QTextOption::NoWrap);

	if (_st.textBg->c.alphaF() >= 1.) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	_inner->setFont(_st.font->f);
	_inner->setAlignment(_st.textAlign);

	subscribe(Lang::Current().updated(), [this] { refreshPlaceholder(); });
	refreshPlaceholder();

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			updatePalette();
		}
	});
	updatePalette();

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
	connect(_inner->document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(_inner.get(), SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(_inner.get(), SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(_inner.get(), SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	setCursor(style::cur_text);
	if (!val.isEmpty()) {
		_inner->setPlainText(val);
	}
	_inner->document()->clearUndoRedoStacks();

	startPlaceholderAnimation();
	startBorderAnimation();
	finishAnimating();
}

void InputField::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Text, _st.textFg->c);
	setPalette(p);
}

void InputField::onTouchTimer() {
	_touchRightButton = true;
}

InputField::Inner::Inner(InputField *parent) : QTextEdit(parent) {
}

bool InputField::Inner::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			qobject_cast<InputField*>(parentWidget())->touchEvent(ev);
			return QTextEdit::viewportEvent(e);
		}
	}
	return QTextEdit::viewportEvent(e);
}

void InputField::touchEvent(QTouchEvent *e) {
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

	case QEvent::TouchEnd:
	{
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

	auto ms = getms();
	QRect r(rect().intersected(e->rect()));
	if (_st.textBg->c.alphaF() > 0.) {
		p.fillRect(r, _st.textBg);
	}
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg);
	}
	auto errorDegree = _a_error.current(ms, _error ? 1. : 0.);
	auto focusedDegree = _a_focused.current(ms, _focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.current(ms, 1.);
	auto borderOpacity = _a_borderOpacity.current(ms, _borderVisible ? 1. : 0.);
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
		auto placeholderShiftDegree = _a_placeholderShifted.current(ms, _placeholderShifted ? 1. : 0.);
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
		auto placeholderHiddenDegree = _a_placeholderShifted.current(ms, _placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			QRect r(rect().marginsRemoved(_st.textMargins + _st.placeholderMargins));
			r.moveLeft(r.left() + placeholderLeft);
			if (rtl()) r.moveLeft(width() - r.left() - r.width());

			p.setFont(_st.font);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));
			p.drawText(r, _placeholder, _st.placeholderAlign);

			p.restore();
		}
	}
	TWidget::paintEvent(e);
}

void InputField::focusInEvent(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason) ? mapFromGlobal(QCursor::pos()).x() : (width() / 2);
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

void InputField::contextMenuEvent(QContextMenuEvent *e) {
	_inner->contextMenuEvent(e);
}

void InputField::Inner::focusInEvent(QFocusEvent *e) {
	f()->focusInInner(e->reason() == Qt::MouseFocusReason);
	QTextEdit::focusInEvent(e);
	emit f()->focused();
}

void InputField::focusInInner(bool focusByMouse) {
	_borderAnimationStart = focusByMouse ? mapFromGlobal(QCursor::pos()).x() : (width() / 2);
	setFocused(true);
}

void InputField::Inner::focusOutEvent(QFocusEvent *e) {
	f()->focusOutInner();
	QTextEdit::focusOutEvent(e);
	emit f()->blurred();
}

void InputField::focusOutInner() {
	setFocused(false);
}

void InputField::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start([this] { update(); }, _focused ? 0. : 1., _focused ? 1. : 0., _st.duration);
		startPlaceholderAnimation();
		startBorderAnimation();
	}
}

void InputField::startPlaceholderAnimation() {
	auto placeholderShifted = _forcePlaceholderHidden
		|| (_focused && _st.placeholderScale > 0.)
		|| !getLastText().isEmpty();
	if (_placeholderShifted != placeholderShifted) {
		_placeholderShifted = placeholderShifted;
		_a_placeholderShifted.start([this] { update(); }, _placeholderShifted ? 0. : 1., _placeholderShifted ? 1. : 0., _st.duration);
	}
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

QSize InputField::sizeHint() const {
	return geometry().size();
}

QSize InputField::minimumSizeHint() const {
	return geometry().size();
}

QString InputField::getText(int32 start, int32 end) const {
	if (end >= 0 && end <= start) return QString();

	if (start < 0) start = 0;
	bool full = (start == 0) && (end < 0);

	QTextDocument *doc(_inner->document());
	QTextBlock from = full ? doc->begin() : doc->findBlock(start), till = (end < 0) ? doc->end() : doc->findBlock(end);
	if (till.isValid()) till = till.next();

	int32 possibleLen = 0;
	for (QTextBlock b = from; b != till; b = b.next()) {
		possibleLen += b.length();
	}
	QString result;
	result.reserve(possibleLen + 1);
	if (!full && end < 0) {
		end = possibleLen;
	}

	for (QTextBlock b = from; b != till; b = b.next()) {
		for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
			QTextFragment fragment(iter.fragment());
			if (!fragment.isValid()) continue;

			int32 p = full ? 0 : fragment.position(), e = full ? 0 : (p + fragment.length());
			if (!full) {
				if (p >= end || e <= start) {
					continue;
				}
			}

			QTextCharFormat f = fragment.charFormat();
			QString emojiText;
			QString t(fragment.text());
			if (!full) {
				if (p < start) {
					t = t.mid(start - p, end - start);
				} else if (e > end) {
					t = t.mid(0, end - p);
				}
			}
			QChar *ub = t.data(), *uc = ub, *ue = uc + t.size();
			for (; uc != ue; ++uc) {
				switch (uc->unicode()) {
				case 0xfdd0: // QTextBeginningOfFrame
				case 0xfdd1: // QTextEndOfFrame
				case QChar::ParagraphSeparator:
				case QChar::LineSeparator: {
					*uc = QLatin1Char('\n');
				} break;
				case QChar::Nbsp: {
					*uc = QLatin1Char(' ');
				} break;
				case QChar::ObjectReplacementCharacter: {
					if (emojiText.isEmpty() && f.isImageFormat()) {
						auto imageName = static_cast<QTextImageFormat*>(&f)->name();
						if (auto emoji = Ui::Emoji::FromUrl(imageName)) {
							emojiText = emoji->text();
						}
					}
					if (uc > ub) result.append(ub, uc - ub);
					if (!emojiText.isEmpty()) result.append(emojiText);
					ub = uc + 1;
				} break;
				}
			}
			if (uc > ub) result.append(ub, uc - ub);
		}
		result.append('\n');
	}
	result.chop(1);
	return result;
}

bool InputField::hasText() const {
	QTextDocument *doc(_inner->document());
	QTextBlock from = doc->begin(), till = doc->end();

	if (from == till) return false;

	for (QTextBlock::Iterator iter = from.begin(); !iter.atEnd(); ++iter) {
		QTextFragment fragment(iter.fragment());
		if (!fragment.isValid()) continue;
		if (!fragment.text().isEmpty()) return true;
	}
	return (from.next() != till);
}

bool InputField::isUndoAvailable() const {
	return _undoAvailable;
}

bool InputField::isRedoAvailable() const {
	return _redoAvailable;
}

void InputField::insertEmoji(EmojiPtr emoji, QTextCursor c) {
	const auto format = PrepareEmojiFormat(emoji, _st.font);
	c.insertText(kObjectReplacement, format);
}

QVariant InputField::Inner::loadResource(int type, const QUrl &name) {
	QString imageName = name.toDisplayString();
	if (auto emoji = Ui::Emoji::FromUrl(imageName)) {
		return QVariant(App::emojiSingle(emoji, f()->_st.font->height));
	}
	return QVariant();
}

void InputField::processDocumentContentsChange(int position, int charsAdded) {
	int32 replacePosition = -1, replaceLen = 0;
	EmojiPtr emoji = nullptr;
	bool newlineFound = false;

	// Tilde formatting.
	auto tildeFormatting = !cRetina() && (font().pixelSize() == 13) && (font().family() == qstr("Open Sans"));
	auto isTildeFragment = false;
	auto tildeRegularFont = tildeFormatting ? qsl("Open Sans") : QString();
	auto tildeFixedFont = tildeFormatting ? Fonts::GetOverride(qsl("Open Sans Semibold")) : QString();

	QTextDocument *doc(_inner->document());
	QTextCursor c(_inner->textCursor());
	c.joinPreviousEditBlock();
	while (true) {
		int32 start = position, end = position + charsAdded;
		QTextBlock from = doc->findBlock(start), till = doc->findBlock(end);
		if (till.isValid()) till = till.next();

		for (QTextBlock b = from; b != till; b = b.next()) {
			for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
				QTextFragment fragment(iter.fragment());
				if (!fragment.isValid()) continue;

				int32 fp = fragment.position(), fe = fp + fragment.length();
				if (fp >= end || fe <= start) {
					continue;
				}

				if (tildeFormatting) {
					isTildeFragment = (fragment.charFormat().fontFamily() == tildeFixedFont);
				}

				QString t(fragment.text());
				const QChar *ch = t.constData(), *e = ch + t.size();
				for (; ch != e; ++ch, ++fp) {
					// QTextBeginningOfFrame // QTextEndOfFrame
					newlineFound = (ch->unicode() == 0xfdd0 || ch->unicode() == 0xfdd1 || ch->unicode() == QChar::ParagraphSeparator || ch->unicode() == QChar::LineSeparator || ch->unicode() == '\n' || ch->unicode() == '\r');
					if (newlineFound) {
						if (replacePosition >= 0) {
							newlineFound = false; // replace tilde char format first
						} else {
							replacePosition = fp;
							replaceLen = 1;
						}
						break;
					}

					auto emojiLen = 0;
					emoji = Ui::Emoji::Find(ch, e, &emojiLen);
					if (emoji) {
						if (replacePosition >= 0) {
							emoji = 0; // replace tilde char format first
						} else {
							replacePosition = fp;
							replaceLen = emojiLen;
						}
						break;
					}

					if (tildeFormatting && fp >= position) { // tilde fix in OpenSans
						bool tilde = (ch->unicode() == '~');
						if ((tilde && !isTildeFragment) || (!tilde && isTildeFragment)) {
							if (replacePosition < 0) {
								replacePosition = fp;
								replaceLen = 1;
							} else {
								++replaceLen;
							}
						} else if (replacePosition >= 0) {
							break;
						}
					}

					if (ch + 1 < e && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) {
						++ch;
						++fp;
					}
				}
				if (replacePosition >= 0) break;
			}
			if (replacePosition >= 0) break;

			if (b.next() != doc->end()) {
				newlineFound = true;
				replacePosition = b.next().position() - 1;
				replaceLen = 1;
				break;
			}
		}
		if (replacePosition >= 0) {
			if (!_inner->document()->pageSize().isNull()) {
				_inner->document()->setPageSize(QSizeF(0, 0));
			}
			QTextCursor c(doc->docHandle(), replacePosition);
			c.setPosition(replacePosition + replaceLen, QTextCursor::KeepAnchor);
			if (newlineFound) {
				QTextCharFormat format;
				format.setFontFamily(font().family());
				c.mergeCharFormat(format);
				c.insertText(" ");
			} else if (emoji) {
				insertEmoji(emoji, c);
			} else {
				QTextCharFormat format;
				format.setFontFamily(isTildeFragment ? tildeRegularFont : tildeFixedFont);
				c.mergeCharFormat(format);
			}
			charsAdded -= replacePosition + replaceLen - position;
			position = replacePosition + ((emoji || newlineFound) ? 1 : replaceLen);

			newlineFound = false;
			emoji = 0;
			replacePosition = -1;
		} else {
			break;
		}
	}
	c.endEditBlock();
}

void InputField::onDocumentContentsChange(int position, int charsRemoved, int charsAdded) {
	if (_correcting) return;

	QString oldtext(_oldtext);
	QTextCursor(_inner->document()->docHandle(), 0).joinPreviousEditBlock();

	if (!position) { // Qt bug workaround https://bugreports.qt.io/browse/QTBUG-49062
		QTextCursor c(_inner->document()->docHandle(), 0);
		c.movePosition(QTextCursor::End);
		if (position + charsAdded > c.position()) {
			int32 toSubstract = position + charsAdded - c.position();
			if (charsRemoved >= toSubstract) {
				charsAdded -= toSubstract;
				charsRemoved -= toSubstract;
			}
		}
	}

	_correcting = true;
	if (_maxLength >= 0) {
		QTextCursor c(_inner->document()->docHandle(), 0);
		c.movePosition(QTextCursor::End);
		int32 fullSize = c.position(), toRemove = fullSize - _maxLength;
		if (toRemove > 0) {
			if (toRemove > charsAdded) {
				if (charsAdded) {
					c.setPosition(position);
					c.setPosition((position + charsAdded), QTextCursor::KeepAnchor);
					c.removeSelectedText();
				}
				c.setPosition(fullSize - (toRemove - charsAdded));
				c.setPosition(fullSize, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				position = _maxLength;
				charsAdded = 0;
				charsRemoved += toRemove;
			} else {
				c.setPosition(position + (charsAdded - toRemove));
				c.setPosition(position + charsAdded, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				charsAdded -= toRemove;
			}
		}
	}
	_correcting = false;

	QTextCursor(_inner->document()->docHandle(), 0).endEditBlock();

	if (_inner->document()->availableRedoSteps() > 0) return;

	const int takeBack = 3;

	position -= takeBack;
	charsAdded += takeBack;
	if (position < 0) {
		charsAdded += position;
		position = 0;
	}
	if (charsAdded <= 0) return;

	_correcting = true;
	QSizeF s = _inner->document()->pageSize();
	processDocumentContentsChange(position, charsAdded);
	if (_inner->document()->pageSize() != s) {
		_inner->document()->setPageSize(s);
	}
	_correcting = false;
}

void InputField::onDocumentContentsChanged() {
	if (_correcting) return;

	setErrorShown(false);

	auto curText = getText();
	if (_oldtext != curText) {
		_oldtext = curText;
		emit changed();
	}
	startPlaceholderAnimation();
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::onUndoAvailable(bool avail) {
	_undoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::onRedoAvailable(bool avail) {
	_redoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::selectAll() {
	auto cursor = _inner->textCursor();
	cursor.setPosition(0);
	cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	_inner->setTextCursor(cursor);
}

void InputField::setDisplayFocused(bool focused) {
	setFocused(focused);
	finishAnimating();
}

void InputField::finishAnimating() {
	_a_focused.finish();
	_a_error.finish();
	_a_placeholderShifted.finish();
	_a_borderShown.finish();
	_a_borderOpacity.finish();
	update();
}

void InputField::setPlaceholderHidden(bool forcePlaceholderHidden) {
	_forcePlaceholderHidden = forcePlaceholderHidden;
	startPlaceholderAnimation();
}

QMimeData *InputField::Inner::createMimeDataFromSelection() const {
	auto result = new QMimeData();
	auto cursor = textCursor();
	auto start = cursor.selectionStart();
	auto end = cursor.selectionEnd();
	if (end > start) {
		result->setText(f()->getText(start, end));
	}
	return result;
}

void InputField::customUpDown(bool custom) {
	_customUpDown = custom;
}

void InputField::Inner::keyPressEvent(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool macmeta = (cPlatform() == dbipMac || cPlatform() == dbipMacOld) && e->modifiers().testFlag(Qt::ControlModifier) && !e->modifiers().testFlag(Qt::MetaModifier) && !e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = true;
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);

	if (macmeta && e->key() == Qt::Key_Backspace) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (enter && ctrlGood) {
		emit f()->submitted(ctrl && shift);
	} else if (e->key() == Qt::Key_Escape) {
		e->ignore();
		emit f()->cancelled();
	} else if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
		if (alt || ctrl) {
			e->ignore();
		} else {
			if (!focusNextPrevChild(e->key() == Qt::Key_Tab && !shift)) {
				e->ignore();
			}
		}
	} else if (e->key() == Qt::Key_Search || e == QKeySequence::Find) {
		e->ignore();
	} else if (f()->_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto cursor = textCursor();
		int start = cursor.selectionStart(), end = cursor.selectionEnd();
		if (end > start) {
			QApplication::clipboard()->setText(f()->getText(start, end), QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	} else {
		auto oldCursorPosition = textCursor().position();
		if (enter && ctrl) {
			e->setModifiers(e->modifiers() & ~Qt::ControlModifier);
		}
		QTextEdit::keyPressEvent(e);
		auto currentCursor = textCursor();
		if (textCursor().position() == oldCursorPosition) {
			bool check = false;
			if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_Up) {
				oldCursorPosition = currentCursor.position();
				currentCursor.movePosition(QTextCursor::Start, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_Down) {
				oldCursorPosition = currentCursor.position();
				currentCursor.movePosition(QTextCursor::End, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Backspace) {
				e->ignore();
			}
			if (check) {
				if (oldCursorPosition == currentCursor.position()) {
					e->ignore();
				} else {
					setTextCursor(currentCursor);
				}
			}
		}
	}
}

void InputField::Inner::contextMenuEvent(QContextMenuEvent *e) {
	if (auto menu = createStandardContextMenu()) {
		(new Ui::PopupMenu(nullptr, menu))->popup(e->globalPos());
	}
}

void InputField::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder();
	_inner->setGeometry(rect().marginsRemoved(_st.textMargins));
	_borderAnimationStart = width() / 2;
	TWidget::resizeEvent(e);
}

void InputField::refreshPlaceholder() {
	auto placeholderText = _placeholderFactory ? _placeholderFactory() : QString();
	auto availableWidth = width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(placeholderText, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			_placeholderPath.addText(0, QFontMetrics(placeholderFont).ascent(), placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(placeholderText, availableWidth);
	}
	update();
}

void InputField::setPlaceholder(base::lambda<QString()> placeholderFactory) {
	_placeholderFactory = std::move(placeholderFactory);
	refreshPlaceholder();
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

MaskedInputField::MaskedInputField(
	QWidget *parent,
	const style::InputField &st,
	base::lambda<QString()> placeholderFactory,
	const QString &val)
: Parent(val, parent)
, _st(st)
, _oldtext(val)
, _placeholderFactory(std::move(placeholderFactory)) {
	resize(_st.width, _st.heightMin);

	setFont(_st.font);
	setAlignment(_st.textAlign);

	subscribe(Lang::Current().updated(), [this] { refreshPlaceholder(); });
	refreshPlaceholder();

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

void MaskedInputField::setTextMargins(const QMargins &mrg) {
	_textMargins = mrg;
	refreshPlaceholder();
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

	auto ms = getms();
	auto r = rect().intersected(e->rect());
	p.fillRect(r, _st.textBg);
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg->b);
	}
	auto errorDegree = _a_error.current(ms, _error ? 1. : 0.);
	auto focusedDegree = _a_focused.current(ms, _focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.current(ms, 1.);
	auto borderOpacity = _a_borderOpacity.current(ms, _borderVisible ? 1. : 0.);
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
		auto placeholderShiftDegree = _a_placeholderShifted.current(ms, _placeholderShifted ? 1. : 0.);
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
		auto placeholderHiddenDegree = _a_placeholderShifted.current(ms, _placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			QRect r(rect().marginsRemoved(_textMargins + _st.placeholderMargins));
			r.moveLeft(r.left() + placeholderLeft);
			if (rtl()) r.moveLeft(width() - r.left() - r.width());

			p.setFont(_st.font);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));
			p.drawText(r, _placeholder, _st.placeholderAlign);

			p.restore();
			p.setOpacity(1.);
		}
	}

	paintAdditionalPlaceholder(p, ms);
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
		} else if (qFuzzyCompare(_a_borderShown.current(1.), 0.)) {
			_a_borderShown.finish();
			_a_borderOpacity.finish();
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
	refreshPlaceholder();
	_borderAnimationStart = width() / 2;
	QLineEdit::resizeEvent(e);
}

void MaskedInputField::refreshPlaceholder() {
	auto placeholderText = _placeholderFactory ? _placeholderFactory() : QString();
	auto availableWidth = width() - _textMargins.left() - _textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(placeholderText, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			_placeholderPath.addText(0, QFontMetrics(placeholderFont).ascent(), placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(placeholderText, availableWidth);
	}
	update();
}

void MaskedInputField::setPlaceholder(base::lambda<QString()> placeholderFactory) {
	_placeholderFactory = std::move(placeholderFactory);
	refreshPlaceholder();
}

void MaskedInputField::contextMenuEvent(QContextMenuEvent *e) {
	if (auto menu = createStandardContextMenu()) {
		(new Ui::PopupMenu(nullptr, menu))->popup(e->globalPos());
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
	_a_focused.finish();
	_a_error.finish();
	_a_placeholderShifted.finish();
	_a_borderShown.finish();
	_a_borderOpacity.finish();
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

void MaskedInputField::placeholderAdditionalPrepare(Painter &p, TimeMs ms) {
	p.setFont(_st.font);
	p.setPen(_st.placeholderFg);
}

void MaskedInputField::keyPressEvent(QKeyEvent *e) {
	QString wasText(_oldtext);
	int32 wasCursor(_oldcursor);

	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = true;
	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
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
		emit submitted(ctrl && shift);
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
		auto validCode = findValidCode(newText.mid(1));
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

PhonePartInput::PhonePartInput(QWidget *parent, const style::InputField &st) : MaskedInputField(parent, st/*, lang(lng_phone_ph)*/) {
}

void PhonePartInput::paintAdditionalPlaceholder(Painter &p, TimeMs ms) {
	if (!_pattern.isEmpty()) {
		auto t = getDisplayedText();
		auto ph = _additionalPlaceholder.mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			auto phRect = placeholderRect();
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				placeholderAdditionalPrepare(p, ms);
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
		for (int i = 0, l = _pattern.size(); i < l; ++i) {
			_additionalPlaceholder.append(' ');
			_additionalPlaceholder.append(QString(_pattern.at(i), QChar(0x2212)));
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

PasswordInput::PasswordInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val) : MaskedInputField(parent, st, std::move(placeholderFactory), val) {
	setEchoMode(QLineEdit::Password);
}

PortInput::PortInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val) : MaskedInputField(parent, st, std::move(placeholderFactory), val) {
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

HexInput::HexInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val) : MaskedInputField(parent, st, std::move(placeholderFactory), val) {
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

UsernameInput::UsernameInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val, bool isLink) : MaskedInputField(parent, st, std::move(placeholderFactory), val) {
	setLinkPlaceholder(isLink ? Messenger::Instance().createInternalLink(QString()) : QString());
}

void UsernameInput::setLinkPlaceholder(const QString &placeholder) {
	_linkPlaceholder = placeholder;
	if (!_linkPlaceholder.isEmpty()) {
		setTextMargins(style::margins(_st.textMargins.left() + _st.font->width(_linkPlaceholder), _st.textMargins.top(), _st.textMargins.right(), _st.textMargins.bottom()));
		setPlaceholderHidden(true);
	}
}

void UsernameInput::paintAdditionalPlaceholder(Painter &p, TimeMs ms) {
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

PhoneInput::PhoneInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val) : MaskedInputField(parent, st, std::move(placeholderFactory), val) {
	QString phone(val);
	if (phone.isEmpty()) {
		clearText();
	} else {
		int32 pos = phone.size();
		correctValue(QString(), 0, phone, pos);
	}
}

void PhoneInput::focusInEvent(QFocusEvent *e) {
	MaskedInputField::focusInEvent(e);
	setSelection(cursorPosition(), cursorPosition());
}

void PhoneInput::clearText() {
	QString phone;
	if (App::self()) {
		QVector<int> newPattern = phoneNumberParse(App::self()->phone());
		if (!newPattern.isEmpty()) {
			phone = App::self()->phone().mid(0, newPattern.at(0));
		}
	}
	setText(phone);
	int32 pos = phone.size();
	correctValue(QString(), 0, phone, pos);
}

void PhoneInput::paintAdditionalPlaceholder(Painter &p, TimeMs ms) {
	if (!_pattern.isEmpty()) {
		auto t = getDisplayedText();
		auto ph = _additionalPlaceholder.mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			auto phRect = placeholderRect();
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				placeholderAdditionalPrepare(p, ms);
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
		newPlaceholder = QString(_pattern.at(0) + 2, ' ') + lang(lng_contact_phone);
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
