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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"

#include "flattextarea.h"
#include "window.h"

FlatTextarea::FlatTextarea(QWidget *parent, const style::flatTextarea &st, const QString &pholder, const QString &v) : QTextEdit(v, parent),
	_ph(pholder), _oldtext(v), _phVisible(!v.length()),
    a_phLeft(_phVisible ? 0 : st.phShift), a_phAlpha(_phVisible ? 1 : 0), a_phColor(st.phColor->c),
    _st(st), _undoAvailable(false), _redoAvailable(false), _fakeMargin(0),
    _touchPress(false), _touchRightButton(false), _touchMove(false), _replacingEmojis(false) {
	setAcceptRichText(false);
	resize(_st.width, _st.font->height);
	
	setFont(_st.font->f);
	setAlignment(_st.align);
	
	_phelided = _st.font->m.elidedText(_ph, Qt::ElideRight, width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1);

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
}

void FlatTextarea::onTouchTimer() {
	_touchRightButton = true;
}

void FlatTextarea::insertFromMimeData(const QMimeData *source) {
	QTextEdit::insertFromMimeData(source);
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
	p.fillRect(rect(), _st.bgColor->b);
	bool phDraw = _phVisible;
	if (animating()) {
		p.setOpacity(a_phAlpha.current());
		phDraw = true;
	}
	if (phDraw) {
		p.save();
		p.setClipRect(rect());
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
		QString imageName = static_cast<const QTextImageFormat*>(&format)->name();
		return getEmoji(imageName.mid(8).toUInt(0, 16));
	}
	return 0;
}

void FlatTextarea::getMentionHashtagStart(QString &start) const {
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

		QString t(fr.text());
		for (int i = pos - p; i > 0; --i) {
			if (t.at(i - 1) == '@') {
				if ((pos - p - i < 1 || t.at(i).isLetter()) && (i < 2 || !(t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_'))) {
					start = t.mid(i - 1, pos - p - i + 1);
				}
				return;
			} else if (t.at(i - 1) == '#') {
				if (i < 2 || !(t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_')) {
					start = t.mid(i - 1, pos - p - i + 1);
				}
				return;
			}
			if (pos - p - i > 63) break;
			if (!t.at(i - 1).isLetterOrNumber() && t.at(i - 1) != '_') break;
		}
		return;
	}
}

void FlatTextarea::onMentionOrHashtagInsert(QString mentionOrHashtag) {
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

		QString t(fr.text());
		for (int i = pos - p; i > 0; --i) {
			if (t.at(i - 1) == '@' || t.at(i - 1) == '#') {
				if ((i == pos - p || t.at(i).isLetter() || t.at(i - 1) == '#') && (i < 2 || !(t.at(i - 2).isLetterOrNumber() || t.at(i - 2) == '_'))) {
					c.setPosition(p + i - 1, QTextCursor::MoveAnchor);
					int till = p + i;
					for (; (till < e) && (till - p - i + 1 < mentionOrHashtag.size()); ++till) {
						if (t.at(till - p).toLower() != mentionOrHashtag.at(till - p - i + 1).toLower()) {
							break;
						}
					}
					if (till - p - i + 1 == mentionOrHashtag.size() && till < e && t.at(till - p) == ' ') {
						++till;
					}
					c.setPosition(till, QTextCursor::KeepAnchor);
					c.insertText(mentionOrHashtag + ' ');
					return;
				}
				break;
			}
			if (pos - p - i > 63) break;
			if (!t.at(i - 1).isLetterOrNumber() && t.at(i - 1) != '_') break;
		}
	}
	c.insertText(mentionOrHashtag + ' ');
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
				if (imageName.midRef(0, 8) == qsl("emoji://")) {
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
						if (imageName.midRef(0, 8) == qsl("emoji://")) {
							uint32 index = imageName.mid(8).toUInt(0, 16);
							const EmojiData *emoji = getEmoji(index);
							if (emoji) {
								emojiText = textEmojiString(emoji);
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

void FlatTextarea::insertEmoji(EmojiPtr emoji, QTextCursor c) {
	c.removeSelectedText();

	QPixmap img(App::emojiSingle(emoji, _st.font->height));
	QString url = qsl("emoji://") + QString::number(emoji->code, 16);
	document()->addResource(QTextDocument::ImageResource, QUrl(url), QVariant(img));
	QTextImageFormat imageFormat;
	imageFormat.setWidth(img.width() / cIntRetinaFactor());
	imageFormat.setHeight(img.height() / cIntRetinaFactor());
	imageFormat.setName(url);
	imageFormat.setVerticalAlignment(QTextCharFormat::AlignBaseline);
	c.insertImage(imageFormat);
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

				int32 p = fragment.position(), e = p + fragment.length();
				if (p >= end || e <= start) {
					continue;
				}

				QString t(fragment.text());
				for (const QChar *ch = t.constData(), *e = ch + t.size(); ch != e; ++ch) {
					if (ch + 1 < e && (ch->isHighSurrogate() || (((ch->unicode() >= 48 && ch->unicode() < 58) || ch->unicode() == 35) && (ch + 1)->unicode() == 0x20E3))) {
						emoji = getEmoji((ch->unicode() << 16) | (ch + 1)->unicode());
						if (emoji) {
							if (emoji->len == 4 && (ch + 3 >= e || ((uint32((ch + 2)->unicode()) << 16) | uint32((ch + 3)->unicode())) != emoji->code2)) {
								emoji = 0;
							} else {
								emojiPosition = p + (ch - t.constData());
								emojiLen = emoji->len + ((ch + emoji->len < e && (ch + emoji->len)->unicode() == 0xFE0F) ? 1 : 0);
								break;
							}
						}
						++ch;
					} else {
						emoji = getEmoji(ch->unicode());
						if (emoji) {
							emojiPosition = p + (ch - t.constData());
							emojiLen = emoji->len + ((ch + emoji->len < e && (ch + emoji->len)->unicode() == 0xFE0F) ? 1 : 0);
							break;
						}
					}
				}
				if (emoji) break;
			}
			if (emoji) break;
		}
		if (emoji) {
			QTextCursor c(doc->docHandle(), emojiPosition);
			c.setPosition(emojiPosition + emojiLen, QTextCursor::KeepAnchor);
			int32 removedUpto = c.position();

			insertEmoji(emoji, c);

			for (Insertions::iterator i = _insertions.begin(), e = _insertions.end(); i != e; ++i) {
				if (i->first >= removedUpto) {
					i->first -= removedUpto - emojiPosition - 1;
				} else if (i->first >= emojiPosition) {
					i->second -= removedUpto - emojiPosition;
					i->first = emojiPosition + 1;
				} else if (i->first + i->second > emojiPosition + 1) {
					i->second -= qMin(removedUpto, i->first + i->second) - emojiPosition;
				}
			}

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
	if (_replacingEmojis || document()->availableRedoSteps() > 0) return;

	const int takeBack = 3;

	position -= takeBack;
	charsAdded += takeBack;
	if (position < 0) {
		charsAdded += position;
		position = 0;
	}
	if (charsAdded <= 0) return;

	_insertions.push_back(Insertion(position, charsAdded));
}

void FlatTextarea::onDocumentContentsChanged() {
	if (_replacingEmojis) return;

	if (!_insertions.isEmpty()) {
		if (document()->availableRedoSteps() > 0) {
			_insertions.clear();
		} else {
			_replacingEmojis = true;
			do {
				Insertion i = _insertions.front();
				_insertions.pop_front();
				if (i.second > 0) {
					processDocumentContentsChange(i.first, i.second);
				}
			} while (!_insertions.isEmpty());
			_replacingEmojis = false;
		}
	}

	QString curText(getText());
	if (_oldtext != curText) {
		_oldtext = curText;
		emit changed();
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

void FlatTextarea::updatePlaceholder() {
	bool vis = !hasText();
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

void FlatTextarea::keyPressEvent(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = (ctrl && cCtrlEnter()) || (!ctrl && !shift && !cCtrlEnter()) || (ctrl && shift);
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);

	if (enter && ctrlGood) {
		emit submitted(ctrl && shift);
	} else if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	} else if (e->key() == Qt::Key_Tab || (ctrl && e->key() == Qt::Key_Backtab)) {
		if (ctrl) {
			e->ignore();
		} else {
			emit tabbed();
		}
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

void FlatTextarea::resizeEvent(QResizeEvent *e) {
	_phelided = _st.font->m.elidedText(_ph, Qt::ElideRight, width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1);
	QTextEdit::resizeEvent(e);
}

void FlatTextarea::mousePressEvent(QMouseEvent *e) {
	QTextEdit::mousePressEvent(e);
}
