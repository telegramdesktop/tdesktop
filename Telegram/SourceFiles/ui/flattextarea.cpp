/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "flattextarea.h"

#include "mainwindow.h"

QByteArray FlatTextarea::serializeTagsList(const TagList &tags) {
	if (tags.isEmpty()) {
		return QByteArray();
	}

	QByteArray tagsSerialized;
	{
		QBuffer buffer(&tagsSerialized);
		buffer.open(QIODevice::WriteOnly);
		QDataStream stream(&buffer);
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

	QBuffer buffer(&data);
	buffer.open(QIODevice::ReadOnly);

	QDataStream stream(&buffer);
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

FlatTextarea::FlatTextarea(QWidget *parent, const style::flatTextarea &st, const QString &pholder, const QString &v, const TagList &tags) : QTextEdit(parent)
, _phVisible(!v.length())
, a_phLeft(_phVisible ? 0 : st.phShift)
, a_phAlpha(_phVisible ? 1 : 0)
, a_phColor(st.phColor->c)
, _a_appearance(animation(this, &FlatTextarea::step_appearance))
, _lastTextWithTags { v, tags }
, _st(st) {
	setAcceptRichText(false);
	resize(_st.width, _st.font->height);

	setFont(_st.font->f);
	setAlignment(_st.align);

	setPlaceholder(pholder);

	QPalette p(palette());
	p.setColor(QPalette::Text, _st.textColor->c);
	setPalette(p);

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

	connect(document(), SIGNAL(contentsChange(int,int,int)), this, SLOT(onDocumentContentsChange(int,int,int)));
	connect(document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(this, SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(this, SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	if (!_lastTextWithTags.text.isEmpty()) {
		setTextWithTags(_lastTextWithTags, ClearUndoHistory);
	}
}

FlatTextarea::TextWithTags FlatTextarea::getTextWithTagsPart(int start, int end) {
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
	if (_a_appearance.animating()) {
		a_phLeft.finish();
		a_phAlpha.finish();
		_a_appearance.stop();
		update();
	}
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

	myEnsureResized(this);

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
	case QEvent::TouchBegin:
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
		break;

	case QEvent::TouchUpdate:
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchPress) return;
		if (!_touchMove && window()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(window()->mapFromGlobal(_touchStart));

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		}
		_touchTimer.stop();
		_touchPress = _touchMove = _touchRightButton = false;
		break;

	case QEvent::TouchCancel:
		_touchPress = false;
		_touchTimer.stop();
		break;
	}
}

QRect FlatTextarea::getTextRect() const {
	return rect().marginsRemoved(_st.textMrg + st::textRectMargins);
}

int32 FlatTextarea::fakeMargin() const {
	return _fakeMargin;
}

void FlatTextarea::paintEvent(QPaintEvent *e) {
	QPainter p(viewport());
	QRect r(rect().intersected(e->rect()));
	p.fillRect(r, _st.bgColor->b);
	bool phDraw = _phVisible;
	if (_a_appearance.animating()) {
		p.setOpacity(a_phAlpha.current());
		phDraw = true;
	}
	if (phDraw) {
		p.save();
		p.setClipRect(r);
		p.setFont(_st.font);
		p.setPen(a_phColor.current());
		if (_st.phAlign == style::al_topleft && _phAfter > 0) {
			int skipWidth = _st.font->width(getTextWithTags().text.mid(0, _phAfter));
			p.drawText(_st.textMrg.left() - _fakeMargin + a_phLeft.current() + skipWidth, _st.textMrg.top() - _fakeMargin - st::lineWidth + _st.font->ascent, _ph);
		} else {
			QRect phRect(_st.textMrg.left() - _fakeMargin + _st.phPos.x() + a_phLeft.current(), _st.textMrg.top() - _fakeMargin + _st.phPos.y(), width() - _st.textMrg.left() - _st.textMrg.right(), height() - _st.textMrg.top() - _st.textMrg.bottom());
			p.drawText(phRect, _ph, QTextOption(_st.phAlign));
		}
		p.restore();
		p.setOpacity(1);
	}
	QTextEdit::paintEvent(e);
}

void FlatTextarea::focusInEvent(QFocusEvent *e) {
	a_phColor.start(_st.phFocusColor->c);
	_a_appearance.start();
	QTextEdit::focusInEvent(e);
}

void FlatTextarea::focusOutEvent(QFocusEvent *e) {
	a_phColor.start(_st.phColor->c);
	_a_appearance.start();
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
		QTextCharFormat format = fragment.charFormat();
		QString imageName = static_cast<QTextImageFormat*>(&format)->name();
		if (imageName.startsWith(qstr("emoji://e."))) {
			return emojiFromUrl(imageName);
		}
	}
	return nullptr;
}

QString FlatTextarea::getInlineBotQuery(UserData **outInlineBot, QString *outInlineBotUsername) const {
	t_assert(outInlineBot != nullptr);
	t_assert(outInlineBotUsername != nullptr);

	auto &text = getTextWithTags().text;

	int32 inlineUsernameStart = 1, inlineUsernameLength = 0, size = text.size();
	if (size > 2 && text.at(0) == '@' && text.at(1).isLetter()) {
		inlineUsernameLength = 1;
		for (int32 i = inlineUsernameStart + 1, l = text.size(); i < l; ++i) {
			if (text.at(i).isLetterOrNumber() || text.at(i).unicode() == '_') {
				++inlineUsernameLength;
				continue;
			}
			if (!text.at(i).isSpace()) {
				inlineUsernameLength = 0;
			}
			break;
		}
		if (inlineUsernameLength && inlineUsernameStart + inlineUsernameLength < text.size() && text.at(inlineUsernameStart + inlineUsernameLength).isSpace()) {
			QStringRef username = text.midRef(inlineUsernameStart, inlineUsernameLength);
			if (username != *outInlineBotUsername) {
				*outInlineBotUsername = username.toString();
				PeerData *peer = App::peerByName(*outInlineBotUsername);
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
				return text.mid(inlineUsernameStart + inlineUsernameLength + 1);
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

		QTextCharFormat f = fr.charFormat();
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
		t_assert(fragment.isValid());

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
		QTextCharFormat format = cursor.charFormat();
		format.setAnchor(false);
		format.setAnchorName(QString());
		format.clearForeground();
		cursor.insertText(text + ' ', format);
	} else {
		_insertedTags.clear();
		_insertedTags.push_back({ 0, text.size(), tagId });
		_insertedTagsAreFromMime = false;
		cursor.insertText(text + ' ');
		_insertedTags.clear();
	}
}

void FlatTextarea::setTagMimeProcessor(std_::unique_ptr<TagMimeProcessor> &&processor) {
	_tagMimeProcessor = std_::move(processor);
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

			QTextCharFormat f = fr.charFormat();
			QString t(fr.text());
			if (p < start) {
				t = t.mid(start - p, end - start);
			} else if (e > end) {
				t = t.mid(0, end - p);
			}
			if (f.isImageFormat() && !t.isEmpty() && t.at(0).unicode() == QChar::ObjectReplacementCharacter) {
				QString imageName = static_cast<QTextImageFormat*>(&f)->name();
				if (imageName.startsWith(qstr("emoji://e."))) {
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
			t_assert(randomPartPosition > 0);

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
				FlatTextarea::Tag tag = {
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
				case QChar::LineSeparator:
					*uc = QLatin1Char('\n');
					break;
				case QChar::Nbsp:
					*uc = QLatin1Char(' ');
					break;
				case QChar::ObjectReplacementCharacter:
					if (emojiText.isEmpty() && f.isImageFormat()) {
						QString imageName = static_cast<QTextImageFormat*>(&f)->name();
						if (imageName.startsWith(qstr("emoji://e."))) {
							if (EmojiPtr emoji = emojiFromUrl(imageName)) {
								emojiText = emojiString(emoji);
							}
						}
					}
					if (uc > ub) result.append(ub, uc - ub);
					if (!emojiText.isEmpty()) result.append(emojiText);
					ub = uc + 1;
				break;
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

	initLinkSets();

	int32 len = text.size();
	const QChar *start = text.unicode(), *end = start + text.size();
	for (int32 offset = 0, matchOffset = offset; offset < len;) {
		QRegularExpressionMatch m = reDomain().match(text, matchOffset);
		if (!m.hasMatch()) break;

		int32 domainOffset = m.capturedStart();

		QString protocol = m.captured(1).toLower();
		QString topDomain = m.captured(3).toLower();

		bool isProtocolValid = protocol.isEmpty() || validProtocols().contains(hashCrc32(protocol.constData(), protocol.size() * sizeof(QChar)));
		bool isTopDomainValid = !protocol.isEmpty() || validTopDomains().contains(hashCrc32(topDomain.constData(), topDomain.size() * sizeof(QChar)));

		if (protocol.isEmpty() && domainOffset > offset + 1 && *(start + domainOffset - 1) == QChar('@')) {
			QString forMailName = text.mid(offset, domainOffset - offset - 1);
			QRegularExpressionMatch mMailName = reMailName().match(forMailName);
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
	QTextImageFormat imageFormat;
	int32 ew = ESize + st::emojiPadding * cIntRetinaFactor() * 2, eh = _st.font->height * cIntRetinaFactor();
	imageFormat.setWidth(ew / cIntRetinaFactor());
	imageFormat.setHeight(eh / cIntRetinaFactor());
	imageFormat.setName(qsl("emoji://e.") + QString::number(emojiKey(emoji), 16));
	imageFormat.setVerticalAlignment(QTextCharFormat::AlignBaseline);
	if (c.charFormat().isAnchor()) {
		imageFormat.setAnchor(true);
		imageFormat.setAnchorName(c.charFormat().anchorName());
		imageFormat.setForeground(st::defaultTextStyle.linkFg);
	}
	static QString objectReplacement(QChar::ObjectReplacementCharacter);
	c.insertText(objectReplacement, imageFormat);
}

QVariant FlatTextarea::loadResource(int type, const QUrl &name) {
	QString imageName = name.toDisplayString();
	if (imageName.startsWith(qstr("emoji://e."))) {
		if (EmojiPtr emoji = emojiFromUrl(imageName)) {
			return QVariant(App::emojiSingle(emoji, _st.font->height));
		}
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

void removeTags(QTextDocument *document, int from, int end) {
	QTextCursor c(document->docHandle(), 0);
	c.setPosition(from);
	c.setPosition(end, QTextCursor::KeepAnchor);

	QTextCharFormat format;
	format.setAnchor(false);
	format.setAnchorName(QString());
	format.setForeground(st::black);
	c.mergeCharFormat(format);
}

// Returns the position of the first inserted tag or "changedEnd" value if none found.
int processInsertedTags(QTextDocument *document, int changedPosition, int changedEnd, const FlatTextarea::TagList &tags, FlatTextarea::TagMimeProcessor *processor) {
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
				removeTags(document, applyNoTagFrom, tagFrom);
			}
			QTextCursor c(document->docHandle(), 0);
			c.setPosition(tagFrom);
			c.setPosition(tagTo, QTextCursor::KeepAnchor);

			QTextCharFormat format;
			format.setAnchor(true);
			format.setAnchorName(tagId + '/' + QString::number(rand_value<uint32>()));
			format.setForeground(st::defaultTextStyle.linkFg);
			c.mergeCharFormat(format);

			applyNoTagFrom = tagTo;
		}
	}
	if (applyNoTagFrom < changedEnd) {
		removeTags(document, applyNoTagFrom, changedEnd);
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
	auto regularFont = qsl("Open Sans"), semiboldFont = qsl("Open Sans Semibold");
	bool tildeFormatting = !cRetina() && (font().pixelSize() == 13) && (font().family() == regularFont);
	bool isTildeFragment = false;

	// First tag handling (the one we inserted text to).
	bool startTagFound = false;
	bool breakTagOnNotLetter = false;

	auto doc = document();

	// Apply inserted tags.
	auto insertedTagsProcessor = _insertedTagsAreFromMime ? _tagMimeProcessor.get() : nullptr;
	int breakTagOnNotLetterTill = processInsertedTags(doc, insertPosition, insertEnd,
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
				t_assert(fragment.isValid());

				int fragmentPosition = fragment.position();
				if (insertPosition >= fragmentPosition + fragment.length()) {
					continue;
				}
				int changedPositionInFragment = insertPosition - fragmentPosition; // Can be negative.
				int changedEndInFragment = insertEnd - fragmentPosition;
				if (changedEndInFragment <= 0) {
					break;
				}

				auto charFormat = fragment.charFormat();
				if (tildeFormatting) {
					isTildeFragment = (charFormat.fontFamily() == semiboldFont);
				}

				auto fragmentText = fragment.text();
				auto *textStart = fragmentText.constData();
				auto *textEnd = textStart + fragmentText.size();

				if (!startTagFound) {
					startTagFound = true;
					auto tagName = charFormat.anchorName();
					if (!tagName.isEmpty()) {
						breakTagOnNotLetter = wasInsertTillTheEndOfTag(block, fragmentIt, insertEnd);
					}
				}

				auto *ch = textStart + qMax(changedPositionInFragment, 0);
				for (; ch < textEnd; ++ch) {
					int emojiLength = 0;
					if (auto emoji = emojiFromText(ch, textEnd, &emojiLength)) {
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
				format.setForeground(st::black);
				c.mergeCharFormat(format);
			} else if (action.type == ActionType::TildeFont) {
				QTextCharFormat format;
				format.setFontFamily(action.isTilde ? semiboldFont : regularFont);
				c.mergeCharFormat(format);
				insertPosition = action.intervalEnd;
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

void FlatTextarea::step_appearance(float64 ms, bool timer) {
	float dt = ms / _st.phDuration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_phLeft.finish();
		a_phAlpha.finish();
		a_phColor.finish();
		a_phLeft = anim::ivalue(a_phLeft.current());
		a_phAlpha = anim::fvalue(a_phAlpha.current());
		a_phColor = anim::cvalue(a_phColor.current());
	} else {
		a_phLeft.update(dt, _st.phLeftFunc);
		a_phAlpha.update(dt, _st.phAlphaFunc);
		a_phColor.update(dt, _st.phColorFunc);
	}
	if (timer) update();
}

void FlatTextarea::setPlaceholder(const QString &ph, int32 afterSymbols) {
	_ph = ph;
	if (_phAfter != afterSymbols) {
		_phAfter = afterSymbols;
		updatePlaceholder();
	}
	int skipWidth = 0;
	if (_phAfter) {
		skipWidth = _st.font->width(getTextWithTags().text.mid(0, _phAfter));
	}
	_phelided = _st.font->elided(_ph, width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1 - skipWidth);
	if (_phVisible) update();
}

void FlatTextarea::updatePlaceholder() {
	bool vis = (getTextWithTags().text.size() <= _phAfter);
	if (vis == _phVisible) return;

	a_phLeft.start(vis ? 0 : _st.phShift);
	a_phAlpha.start(vis ? 1 : 0);
	_a_appearance.start();

	_phVisible = vis;
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
		QTextCursor tc(textCursor());
		if (enter && ctrl) {
			e->setModifiers(e->modifiers() & ~Qt::ControlModifier);
		}
		bool spaceOrReturn = false;
		QString t(e->text());
		if (!t.isEmpty() && t.size() < 3) {
			if (t.at(0) == '\n' || t.at(0) == '\r' || t.at(0).isSpace() || t.at(0) == QChar::LineSeparator) {
				spaceOrReturn = true;
			}
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
		if (spaceOrReturn) emit spacedReturnedPasted();
	}
}

void FlatTextarea::resizeEvent(QResizeEvent *e) {
	_phelided = _st.font->elided(_ph, width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1);
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
	if (QMenu *menu = createStandardContextMenu()) {
		(new PopupMenu(menu))->popup(e->globalPos());
	}
}
