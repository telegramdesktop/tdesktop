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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"

#include "flattextarea.h"
#include "window.h"

FlatTextarea::FlatTextarea(QWidget *parent, const style::flatTextarea &st, const QString &pholder, const QString &v) : QTextEdit(parent),
_minHeight(-1), _maxHeight(-1), _maxLength(-1), _ctrlEnterSubmit(true),
_oldtext(v), _phVisible(!v.length()),
a_phLeft(_phVisible ? 0 : st.phShift), a_phAlpha(_phVisible ? 1 : 0), a_phColor(st.phColor->c),
_st(st), _undoAvailable(false), _redoAvailable(false), _inDrop(false), _inHeightCheck(false), _fakeMargin(0),
_touchPress(false), _touchRightButton(false), _touchMove(false), _correcting(false) {
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

	connect(document(), SIGNAL(contentsChange(int, int, int)), this, SLOT(onDocumentContentsChange(int, int, int)));
	connect(document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(this, SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(this, SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	if (!v.isEmpty()) {
		setPlainText(v);
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
	if (animating()) {
		p.setOpacity(a_phAlpha.current());
		phDraw = true;
	}
	if (phDraw) {
		p.save();
		p.setClipRect(r);
		QRect phRect(_st.textMrg.left() - _fakeMargin + _st.phPos.x() + a_phLeft.current(), _st.textMrg.top() - _fakeMargin + _st.phPos.y(), width() - _st.textMrg.left() - _st.textMrg.right(), height() - _st.textMrg.top() - _st.textMrg.bottom());
		p.setFont(_st.font->f);
		p.setPen(a_phColor.current());
		p.drawText(phRect, _ph, QTextOption(_st.phAlign));
		p.restore();
	}
	QTextEdit::paintEvent(e);
}

void FlatTextarea::focusInEvent(QFocusEvent *e) {
	a_phColor.start(_st.phFocusColor->c);
	anim::start(this);
	QTextEdit::focusInEvent(e);
}

void FlatTextarea::focusOutEvent(QFocusEvent *e) {
	a_phColor.start(_st.phColor->c);
	anim::start(this);
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
	return 0;
}

void FlatTextarea::getMentionHashtagBotCommandStart(QString &start) const {
	int32 pos = textCursor().position();
	if (textCursor().anchor() != pos) return;

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
					start = t.mid(i - 1, pos - p - i + 1);
				} else if ((pos - p - i < 1 || t.at(i).isLetter()) && i > 2 && (t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_') && !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				return;
			} else if (t.at(i - 1) == '#') {
				if (i < 2 || !(t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_')) {
					start = t.mid(i - 1, pos - p - i + 1);
				}
				return;
			} else if (t.at(i - 1) == '/') {
				if (i < 2) {
					start = t.mid(i - 1, pos - p - i + 1);
				}
				return;
			}
			if (pos - p - i > 127 || (!mentionInCommand && (pos - p - i > 63))) break;
			if (!t.at(i - 1).isLetterOrNumber() && t.at(i - 1) != '_') break;
		}
		return;
	}
}

void FlatTextarea::onMentionHashtagOrBotCommandInsert(QString str) {
	QTextCursor c(textCursor());
	int32 pos = c.position();

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
			if (t.at(i - 1) == '@' || t.at(i - 1) == '#' || t.at(i - 1) == '/') {
				if ((i == pos - p || t.at(i).isLetter() || t.at(i - 1) == '#') && (i < 2 || !(t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_'))) {
					c.setPosition(p + i - 1, QTextCursor::MoveAnchor);
					int till = p + i;
					for (; (till < e) && (till - p - i + 1 < str.size()); ++till) {
						if (t.at(till - p).toLower() != str.at(till - p - i + 1).toLower()) {
							break;
						}
					}
					if (till - p - i + 1 == str.size() && till < e && t.at(till - p) == ' ') {
						++till;
					}
					c.setPosition(till, QTextCursor::KeepAnchor);
					c.insertText(str + ' ');
					return;
				} else if ((i == pos - p || t.at(i).isLetter()) && t.at(i - 1) == '@' && i > 2 && (t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_') && !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				break;
			}
			if (pos - p - i > 127 || (!mentionInCommand && (pos - p - i > 63))) break;
			if (!t.at(i - 1).isLetterOrNumber() && t.at(i - 1) != '_') break;
		}
		break;
	}
	c.insertText(str + ' ');
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

QString FlatTextarea::getText(int32 start, int32 end) const {
	if (end >= 0 && end <= start) return QString();

	if (start < 0) start = 0;
	bool full = (start == 0) && (end < 0);

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
		newLinks.push_back(qMakePair(domainOffset - 1, p - start - domainOffset + 2));
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
		for (LinkRanges::const_iterator i = _links.cbegin(), e = _links.cend(); i != e; ++i) {
			result.push_back(text.mid(i->first + 1, i->second - 2));
		}
	}
	return result;
}

void FlatTextarea::insertFromMimeData(const QMimeData *source) {
	QTextEdit::insertFromMimeData(source);
	if (!_inDrop) emit spacedReturnedPasted();
}

void FlatTextarea::correctValue(const QString &was, QString &now) {
}

void FlatTextarea::insertEmoji(EmojiPtr emoji, QTextCursor c) {
	QTextImageFormat imageFormat;
	int32 ew = ESize + st::emojiPadding * cIntRetinaFactor() * 2, eh = _st.font->height * cIntRetinaFactor();
	imageFormat.setWidth(ew / cIntRetinaFactor());
	imageFormat.setHeight(eh / cIntRetinaFactor());
	imageFormat.setName(qsl("emoji://e.") + QString::number(emojiKey(emoji), 16));
	imageFormat.setVerticalAlignment(QTextCharFormat::AlignBaseline);

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

void FlatTextarea::processDocumentContentsChange(int position, int charsAdded) {
	int32 emojiPosition = 0, emojiLen = 0;
	const EmojiData *emoji = 0;

	QTextDocument *doc(document());

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

				QString t(fragment.text());
				const QChar *ch = t.constData(), *e = ch + t.size();
				for (; ch != e; ++ch) {
					emoji = emojiFromText(ch, e, emojiLen);
					if (emoji) {
						emojiPosition = fp + (ch - t.constData());
						break;
					}
					if (ch + 1 < e && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) ++ch;
				}
				if (emoji) break;
			}
			if (emoji) break;
		}
		if (emoji) {
			if (!document()->pageSize().isNull()) {
				document()->setPageSize(QSizeF(0, 0));
			}

			QTextCursor c(doc->docHandle(), emojiPosition);
			c.setPosition(emojiPosition + emojiLen, QTextCursor::KeepAnchor);
			int32 removedUpto = c.position();

			insertEmoji(emoji, c);

			charsAdded -= removedUpto - position;
			position = emojiPosition + 1;

			emoji = 0;
			emojiPosition = 0;
		} else {
			break;
		}
	}
}

void FlatTextarea::onDocumentContentsChange(int position, int charsRemoved, int charsAdded) {
	if (_correcting) return;

	QTextCursor(document()->docHandle(), 0).joinPreviousEditBlock();

	_correcting = true;
	if (_maxLength >= 0) {
		QTextCursor c(document()->docHandle(), 0);
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
			} else {
				c.setPosition(position + (charsAdded - toRemove));
				c.setPosition(position + charsAdded, QTextCursor::KeepAnchor);
				c.removeSelectedText();
			}
		}
	}
	_correcting = false;

	if (!_links.isEmpty()) {
		bool changed = false;
		for (LinkRanges::iterator i = _links.begin(); i != _links.end();) {
			if (i->first + i->second <= position) {
				++i;
			} else if (i->first >= position + charsRemoved) {
				i->first += charsAdded - charsRemoved;
				++i;
			} else {
				i = _links.erase(i);
				changed = true;
			}
		}
		if (changed) emit linksChanged();
	}

	if (document()->availableRedoSteps() > 0) {
		QTextCursor(document()->docHandle(), 0).endEditBlock();
		return;
	}

	const int takeBack = 3;

	position -= takeBack;
	charsAdded += takeBack;
	if (position < 0) {
		charsAdded += position;
		position = 0;
	}
	if (charsAdded <= 0) {
		QTextCursor(document()->docHandle(), 0).endEditBlock();
		return;
	}

	_correcting = true;
	QSizeF s = document()->pageSize();
	processDocumentContentsChange(position, charsAdded);
	if (document()->pageSize() != s) {
		document()->setPageSize(s);
	}
	_correcting = false;

	QTextCursor(document()->docHandle(), 0).endEditBlock();
}

void FlatTextarea::onDocumentContentsChanged() {
	if (_correcting) return;

	QString curText(getText());
	_correcting = true;
	correctValue(_oldtext, curText);
	_correcting = false;
	if (_oldtext != curText) {
		_oldtext = curText;
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

bool FlatTextarea::animStep(float64 ms) {
	float dt = ms / _st.phDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
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
	update();
	return res;
}

void FlatTextarea::setPlaceholder(const QString &ph) {
	_ph = ph;
	_phelided = _st.font->elided(_ph, width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1);
	if (_phVisible) update();
}

void FlatTextarea::updatePlaceholder() {
	bool vis = getLastText().isEmpty();
	if (vis == _phVisible) return;

	a_phLeft.start(vis ? 0 : _st.phShift);
	a_phAlpha.start(vis ? 1 : 0);
	anim::start(this);

	_phVisible = vis;
}

QMimeData *FlatTextarea::createMimeDataFromSelection() const {
	QMimeData *result = new QMimeData();
	QTextCursor c(textCursor());
	int32 start = c.selectionStart(), end = c.selectionEnd();
	if (end > start) {
		result->setText(getText(start, end));
	}
	return result;
}

void FlatTextarea::setCtrlEnterSubmit(bool ctrlEnterSubmit) {
	_ctrlEnterSubmit = ctrlEnterSubmit;
}

void FlatTextarea::keyPressEvent(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
	bool macmeta = (cPlatform() == dbipMac) && e->modifiers().testFlag(Qt::ControlModifier) && !e->modifiers().testFlag(Qt::MetaModifier) && !e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = (ctrl && _ctrlEnterSubmit) || (!ctrl && !shift && !_ctrlEnterSubmit) || (ctrl && shift);
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);

	if (macmeta && e->key() == Qt::Key_Backspace) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (enter && ctrlGood) {
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
	emit spacedReturnedPasted();
}
