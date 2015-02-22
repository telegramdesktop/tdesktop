/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qtextlayout.h"
#include "qtextengine_p.h"

#include <qthread.h>
#include <qfont.h>
#include <qmath.h>
#include <qpainter.h>
#include <qvarlengtharray.h>
#include <qtextformat.h>
#include <qabstracttextdocumentlayout.h>
#include "qtextdocument_p.h"
#include "qtextformat_p.h"
#include "qpainterpath.h"
#include "qglyphrun.h"
#include "qglyphrun_p.h"
#include "qrawfont.h"
#include "qrawfont_p.h"
#include <limits.h>

#include <qdebug.h>

#include "qfontengine_p.h"
#include <private/qpainter_p.h>

QT_BEGIN_NAMESPACE

#define ObjectSelectionBrush (QTextFormat::ForegroundBrush + 1)
#define SuppressText 0x5012
#define SuppressBackground 0x513

/*!
    \class QTextLayout::FormatRange
    \reentrant

    \brief The QTextLayout::FormatRange structure is used to apply extra formatting information
    for a specified area in the text layout's content.
    \inmodule QtGui

    \sa QTextLayout::setAdditionalFormats(), QTextLayout::draw()
*/

/*!
    \variable QTextLayout::FormatRange::start
    Specifies the beginning of the format range within the text layout's text.
*/

/*!
    \variable QTextLayout::FormatRange::length
    Specifies the numer of characters the format range spans.
*/

/*!
    \variable QTextLayout::FormatRange::format
    Specifies the format to apply.
*/

/*!
    \class QTextInlineObject
    \reentrant

    \brief The QTextInlineObject class represents an inline object in
    a QAbstractTextDocumentLayout and its implementations.
    \inmodule QtGui

    \ingroup richtext-processing

    Normally, you do not need to create a QTextInlineObject. It is
    used by QAbstractTextDocumentLayout to handle inline objects when
    implementing a custom layout.

    The inline object has various attributes that can be set, for
    example using, setWidth(), setAscent(), and setDescent(). The
    rectangle it occupies is given by rect(), and its direction by
    textDirection(). Its position in the text layout is given by
    textPosition(), and its format is given by format().
*/

/*!
    \fn QTextInlineObject::QTextInlineObject(int i, QTextEngine *e)
    \internal

    Creates a new inline object for the item at position \a i in the
    text engine \a e.
*/

/*!
    \fn QTextInlineObject::QTextInlineObject()

    \internal
*/

/*!
    \fn bool QTextInlineObject::isValid() const

    Returns \c true if this inline object is valid; otherwise returns
    false.
*/

/*!
    Returns the inline object's rectangle.

    \sa ascent(), descent(), width()
*/
QRectF QTextInlineObject::rect() const
{
    QScriptItem& si = eng->layoutData->items[itm];
    return QRectF(0, -si.ascent.toReal(), si.width.toReal(), si.height().toReal());
}

/*!
    Returns the inline object's width.

    \sa ascent(), descent(), rect()
*/
qreal QTextInlineObject::width() const
{
    return eng->layoutData->items[itm].width.toReal();
}

/*!
    Returns the inline object's ascent.

    \sa descent(), width(), rect()
*/
qreal QTextInlineObject::ascent() const
{
    return eng->layoutData->items[itm].ascent.toReal();
}

/*!
    Returns the inline object's descent.

    \sa ascent(), width(), rect()
*/
qreal QTextInlineObject::descent() const
{
    return eng->layoutData->items[itm].descent.toReal();
}

/*!
    Returns the inline object's total height. This is equal to
    ascent() + descent() + 1.

    \sa ascent(), descent(), width(), rect()
*/
qreal QTextInlineObject::height() const
{
    return eng->layoutData->items[itm].height().toReal();
}

/*!
    Sets the inline object's width to \a w.

    \sa width(), ascent(), descent(), rect()
*/
void QTextInlineObject::setWidth(qreal w)
{
    eng->layoutData->items[itm].width = QFixed::fromReal(w);
}

/*!
    Sets the inline object's ascent to \a a.

    \sa ascent(), setDescent(), width(), rect()
*/
void QTextInlineObject::setAscent(qreal a)
{
    eng->layoutData->items[itm].ascent = QFixed::fromReal(a);
}

/*!
    Sets the inline object's descent to \a d.

    \sa descent(), setAscent(), width(), rect()
*/
void QTextInlineObject::setDescent(qreal d)
{
    eng->layoutData->items[itm].descent = QFixed::fromReal(d);
}

/*!
    The position of the inline object within the text layout.
*/
int QTextInlineObject::textPosition() const
{
    return eng->layoutData->items[itm].position;
}

/*!
    Returns an integer describing the format of the inline object
    within the text layout.
*/
int QTextInlineObject::formatIndex() const
{
    return eng->formatIndex(&eng->layoutData->items[itm]);
}

/*!
    Returns format of the inline object within the text layout.
*/
QTextFormat QTextInlineObject::format() const
{
    return eng->format(&eng->layoutData->items[itm]);
}

/*!
    Returns if the object should be laid out right-to-left or left-to-right.
*/
Qt::LayoutDirection QTextInlineObject::textDirection() const
{
    return (eng->layoutData->items[itm].analysis.bidiLevel % 2 ? Qt::RightToLeft : Qt::LeftToRight);
}

/*!
    \class QTextLayout
    \reentrant

    \brief The QTextLayout class is used to lay out and render text.
    \inmodule QtGui

    \ingroup richtext-processing

    It offers many features expected from a modern text layout
    engine, including Unicode compliant rendering, line breaking and
    handling of cursor positioning. It can also produce and render
    device independent layout, something that is important for WYSIWYG
    applications.

    The class has a rather low level API and unless you intend to
    implement your own text rendering for some specialized widget, you
    probably won't need to use it directly.

    QTextLayout can be used with both plain and rich text.

    QTextLayout can be used to create a sequence of QTextLine
    instances with given widths and can position them independently
    on the screen. Once the layout is done, these lines can be drawn
    on a paint device.

    The text to be laid out can be provided in the constructor or set with
    setText().

    The layout can be seen as a sequence of QTextLine objects; use createLine()
    to create a QTextLine instance, and lineAt() or lineForTextPosition() to retrieve
    created lines.

    Here is a code snippet that demonstrates the layout phase:
    \snippet code/src_gui_text_qtextlayout.cpp 0

    The text can then be rendered by calling the layout's draw() function:
    \snippet code/src_gui_text_qtextlayout.cpp 1

    For a given position in the text you can find a valid cursor position with
    isValidCursorPosition(), nextCursorPosition(), and previousCursorPosition().

    The QTextLayout itself can be positioned with setPosition(); it has a
    boundingRect(), and a minimumWidth() and a maximumWidth().

    \sa QStaticText
*/

/*!
    \enum QTextLayout::CursorMode

    \value SkipCharacters
    \value SkipWords
*/

/*!
    \fn QTextEngine *QTextLayout::engine() const
    \internal

    Returns the text engine used to render the text layout.
*/

/*!
    Constructs an empty text layout.

    \sa setText()
*/
QTextLayout::QTextLayout()
{ d = new QTextEngine(); }

/*!
    Constructs a text layout to lay out the given \a text.
*/
QTextLayout::QTextLayout(const QString& text)
{
    d = new QTextEngine();
    d->text = text;
}

/*!
    Constructs a text layout to lay out the given \a text with the specified
    \a font.

    All the metric and layout calculations will be done in terms of
    the paint device, \a paintdevice. If \a paintdevice is 0 the
    calculations will be done in screen metrics.
*/
QTextLayout::QTextLayout(const QString& text, const QFont &font, QPaintDevice *paintdevice)
{
    QFont f(font);
    if (paintdevice)
        f = QFont(font, paintdevice);
    d = new QTextEngine((text.isNull() ? (const QString&)QString::fromLatin1("") : text), f);
}

/*!
    \internal
    Constructs a text layout to lay out the given \a block.
*/
QTextLayout::QTextLayout(const QTextBlock &block)
{
    d = new QTextEngine();
    d->block = block;
}

/*!
    Destructs the layout.
*/
QTextLayout::~QTextLayout()
{
    if (!d->stackEngine)
        delete d;
}

#ifndef QT_NO_RAWFONT
/*!
    \internal
    Sets a raw font, to be used with QTextLayout::glyphRuns.
    Note that this only supports the needs of WebKit.
    Use of this function with e.g. QTextLayout::draw will result
    in undefined behaviour.
*/
void QTextLayout::setRawFont(const QRawFont &rawFont)
{
    d->rawFont = rawFont;
    d->useRawFont = true;
    d->resetFontEngineCache();
}
#endif

/*!
    Sets the layout's font to the given \a font. The layout is
    invalidated and must be laid out again.

    \sa font()
*/
void QTextLayout::setFont(const QFont &font)
{
    d->fnt = font;
#ifndef QT_NO_RAWFONT
    d->useRawFont = false;
#endif
    d->resetFontEngineCache();
}

/*!
    Returns the current font that is used for the layout, or a default
    font if none is set.

    \sa setFont()
*/
QFont QTextLayout::font() const
{
    return d->font();
}

/*!
    Sets the layout's text to the given \a string. The layout is
    invalidated and must be laid out again.

    Notice that when using this QTextLayout as part of a QTextDocument this
    method will have no effect.

    \sa text()
*/
void QTextLayout::setText(const QString& string)
{
    d->invalidate();
    d->clearLineData();
    d->text = string;
}

/*!
    Returns the layout's text.

    \sa setText()
*/
QString QTextLayout::text() const
{
    return d->text;
}

/*!
    Sets the text option structure that controls the layout process to the
    given \a option.

    \sa textOption()
*/
void QTextLayout::setTextOption(const QTextOption &option)
{
    d->option = option;
}

/*!
    Returns the current text option used to control the layout process.

    \sa setTextOption()
*/
const QTextOption &QTextLayout::textOption() const
{
    return d->option;
}

/*!
    Sets the \a position and \a text of the area in the layout that is
    processed before editing occurs. The layout is
    invalidated and must be laid out again.

    \sa preeditAreaPosition(), preeditAreaText()
*/
void QTextLayout::setPreeditArea(int position, const QString &text)
{
    if (d->preeditAreaPosition() == position && d->preeditAreaText() == text)
        return;
    d->setPreeditArea(position, text);

    if (d->block.docHandle())
        d->block.docHandle()->documentChange(d->block.position(), d->block.length());
}

/*!
    Returns the position of the area in the text layout that will be
    processed before editing occurs.

    \sa preeditAreaText()
*/
int QTextLayout::preeditAreaPosition() const
{
    return d->preeditAreaPosition();
}

/*!
    Returns the text that is inserted in the layout before editing occurs.

    \sa preeditAreaPosition()
*/
QString QTextLayout::preeditAreaText() const
{
    return d->preeditAreaText();
}


/*!
    Sets the additional formats supported by the text layout to \a formatList.
    The formats are applied with preedit area text in place.

    \sa additionalFormats(), clearAdditionalFormats()
*/
void QTextLayout::setAdditionalFormats(const QList<FormatRange> &formatList)
{
    d->setFormats(formatList);

    if (d->block.docHandle())
        d->block.docHandle()->documentChange(d->block.position(), d->block.length());
}

/*!
    Returns the list of additional formats supported by the text layout.

    \sa setAdditionalFormats(), clearAdditionalFormats()
*/
QList<QTextLayout::FormatRange> QTextLayout::additionalFormats() const
{
    return d->formats();
}

/*!
    Clears the list of additional formats supported by the text layout.

    \sa additionalFormats(), setAdditionalFormats()
*/
void QTextLayout::clearAdditionalFormats()
{
    setAdditionalFormats(QList<FormatRange>());
}

/*!
    Enables caching of the complete layout information if \a enable is
    true; otherwise disables layout caching. Usually
    QTextLayout throws most of the layouting information away after a
    call to endLayout() to reduce memory consumption. If you however
    want to draw the laid out text directly afterwards enabling caching
    might speed up drawing significantly.

    \sa cacheEnabled()
*/
void QTextLayout::setCacheEnabled(bool enable)
{
    d->cacheGlyphs = enable;
}

/*!
    Returns \c true if the complete layout information is cached; otherwise
    returns \c false.

    \sa setCacheEnabled()
*/
bool QTextLayout::cacheEnabled() const
{
    return d->cacheGlyphs;
}

/*!
    Sets the visual cursor movement style to the given \a style. If the
    QTextLayout is backed by a document, you can ignore this and use the option
    in QTextDocument, this option is for widgets like QLineEdit or custom
    widgets without a QTextDocument. Default value is Qt::LogicalMoveStyle.

    \sa cursorMoveStyle()
*/
void QTextLayout::setCursorMoveStyle(Qt::CursorMoveStyle style)
{
    d->visualMovement = style == Qt::VisualMoveStyle;
}

/*!
    The cursor movement style of this QTextLayout. The default is
    Qt::LogicalMoveStyle.

    \sa setCursorMoveStyle()
*/
Qt::CursorMoveStyle QTextLayout::cursorMoveStyle() const
{
    return d->visualMovement ? Qt::VisualMoveStyle : Qt::LogicalMoveStyle;
}

/*!
    Begins the layout process.

    \sa endLayout()
*/
void QTextLayout::beginLayout()
{
#ifndef QT_NO_DEBUG
    if (d->layoutData && d->layoutData->layoutState == QTextEngine::InLayout) {
        qWarning("QTextLayout::beginLayout: Called while already doing layout");
        return;
    }
#endif
    d->invalidate();
    d->clearLineData();
    d->itemize();
    d->layoutData->layoutState = QTextEngine::InLayout;
}

/*!
    Ends the layout process.

    \sa beginLayout()
*/
void QTextLayout::endLayout()
{
#ifndef QT_NO_DEBUG
    if (!d->layoutData || d->layoutData->layoutState == QTextEngine::LayoutEmpty) {
        qWarning("QTextLayout::endLayout: Called without beginLayout()");
        return;
    }
#endif
    int l = d->lines.size();
    if (l && d->lines.at(l-1).length < 0) {
        QTextLine(l-1, d).setNumColumns(INT_MAX);
    }
    d->layoutData->layoutState = QTextEngine::LayoutEmpty;
    if (!d->cacheGlyphs)
        d->freeMemory();
}

/*!
    \since 4.4

    Clears the line information in the layout. After having called
    this function, lineCount() returns 0.
*/
void QTextLayout::clearLayout()
{
    d->clearLineData();
}

/*!
    Returns the next valid cursor position after \a oldPos that
    respects the given cursor \a mode.
    Returns value of \a oldPos, if \a oldPos is not a valid cursor position.

    \sa isValidCursorPosition(), previousCursorPosition()
*/
int QTextLayout::nextCursorPosition(int oldPos, CursorMode mode) const
{
    const QCharAttributes *attributes = d->attributes();
    int len = d->block.isValid() ? d->block.length() - 1
                                 : d->layoutData->string.length();
    Q_ASSERT(len <= d->layoutData->string.length());
    if (!attributes || oldPos < 0 || oldPos >= len)
        return oldPos;

    if (mode == SkipCharacters) {
        oldPos++;
        while (oldPos < len && !attributes[oldPos].graphemeBoundary)
            oldPos++;
    } else {
		while (oldPos < len && d->atSpace(oldPos))
			oldPos++;

		if (oldPos < len && d->atWordSeparator(oldPos)) {
            oldPos++;
            while (oldPos < len && d->atWordSeparator(oldPos))
                oldPos++;
        } else {
            while (oldPos < len && !d->atSpace(oldPos) && !d->atWordSeparator(oldPos))
                oldPos++;
        }
    }

    return oldPos;
}

/*!
    Returns the first valid cursor position before \a oldPos that
    respects the given cursor \a mode.
    Returns value of \a oldPos, if \a oldPos is not a valid cursor position.

    \sa isValidCursorPosition(), nextCursorPosition()
*/
int QTextLayout::previousCursorPosition(int oldPos, CursorMode mode) const
{
    const QCharAttributes *attributes = d->attributes();
    int len = d->block.isValid() ? d->block.length() - 1
                                 : d->layoutData->string.length();
    Q_ASSERT(len <= d->layoutData->string.length());
    if (!attributes || oldPos <= 0 || oldPos > len)
        return oldPos;

    if (mode == SkipCharacters) {
        oldPos--;
        while (oldPos && !attributes[oldPos].graphemeBoundary)
            oldPos--;
    } else {
        while (oldPos && d->atSpace(oldPos-1))
            oldPos--;

        if (oldPos && d->atWordSeparator(oldPos-1)) {
            oldPos--;
            while (oldPos && d->atWordSeparator(oldPos-1))
                oldPos--;
        } else {
            while (oldPos && !d->atSpace(oldPos-1) && !d->atWordSeparator(oldPos-1))
                oldPos--;
        }
    }

    return oldPos;
}

/*!
    Returns the cursor position to the right of \a oldPos, next to it.
    It's dependent on the visual position of characters, after bi-directional
    reordering.

    \sa leftCursorPosition(), nextCursorPosition()
*/
int QTextLayout::rightCursorPosition(int oldPos) const
{
    int newPos = d->positionAfterVisualMovement(oldPos, QTextCursor::Right);
//    qDebug("%d -> %d", oldPos, newPos);
    return newPos;
}

/*!
    Returns the cursor position to the left of \a oldPos, next to it.
    It's dependent on the visual position of characters, after bi-directional
    reordering.

    \sa rightCursorPosition(), previousCursorPosition()
*/
int QTextLayout::leftCursorPosition(int oldPos) const
{
    int newPos = d->positionAfterVisualMovement(oldPos, QTextCursor::Left);
//    qDebug("%d -> %d", oldPos, newPos);
    return newPos;
}

/*!/
    Returns \c true if position \a pos is a valid cursor position.

    In a Unicode context some positions in the text are not valid
    cursor positions, because the position is inside a Unicode
    surrogate or a grapheme cluster.

    A grapheme cluster is a sequence of two or more Unicode characters
    that form one indivisible entity on the screen. For example the
    latin character `\unicode{0xC4}' can be represented in Unicode by two
    characters, `A' (0x41), and the combining diaresis (0x308). A text
    cursor can only validly be positioned before or after these two
    characters, never between them since that wouldn't make sense. In
    indic languages every syllable forms a grapheme cluster.
*/
bool QTextLayout::isValidCursorPosition(int pos) const
{
    const QCharAttributes *attributes = d->attributes();
    if (!attributes || pos < 0 || pos > (int)d->layoutData->string.length())
        return false;
    return attributes[pos].graphemeBoundary;
}

/*!
    Returns a new text line to be laid out if there is text to be
    inserted into the layout; otherwise returns an invalid text line.

    The text layout creates a new line object that starts after the
    last line in the layout, or at the beginning if the layout is empty.
    The layout maintains an internal cursor, and each line is filled
    with text from the cursor position onwards when the
    QTextLine::setLineWidth() function is called.

    Once QTextLine::setLineWidth() is called, a new line can be created and
    filled with text. Repeating this process will lay out the whole block
    of text contained in the QTextLayout. If there is no text left to be
    inserted into the layout, the QTextLine returned will not be valid
    (isValid() will return false).
*/
QTextLine QTextLayout::createLine()
{
#ifndef QT_NO_DEBUG
    if (!d->layoutData || d->layoutData->layoutState == QTextEngine::LayoutEmpty) {
        qWarning("QTextLayout::createLine: Called without layouting");
        return QTextLine();
    }
#endif
    if (d->layoutData->layoutState == QTextEngine::LayoutFailed)
        return QTextLine();

    int l = d->lines.size();
    if (l && d->lines.at(l-1).length < 0) {
        QTextLine(l-1, d).setNumColumns(INT_MAX);
    }
    int from = l > 0 ? d->lines.at(l-1).from + d->lines.at(l-1).length + d->lines.at(l-1).trailingSpaces : 0;
    int strlen = d->layoutData->string.length();
    if (l && from >= strlen) {
        if (!d->lines.at(l-1).length || d->layoutData->string.at(strlen - 1) != QChar::LineSeparator)
            return QTextLine();
    }

    QScriptLine line;
    line.from = from;
    line.length = -1;
    line.justified = false;
    line.gridfitted = false;

    d->lines.append(line);
    return QTextLine(l, d);
}

/*!
    Returns the number of lines in this text layout.

    \sa lineAt()
*/
int QTextLayout::lineCount() const
{
    return d->lines.size();
}

/*!
    Returns the \a{i}-th line of text in this text layout.

    \sa lineCount(), lineForTextPosition()
*/
QTextLine QTextLayout::lineAt(int i) const
{
    return i < lineCount() ? QTextLine(i, d) : QTextLine();
}

/*!
    Returns the line that contains the cursor position specified by \a pos.

    \sa isValidCursorPosition(), lineAt()
*/
QTextLine QTextLayout::lineForTextPosition(int pos) const
{
    int lineNum = d->lineNumberForTextPosition(pos);
    return lineNum >= 0 ? lineAt(lineNum) : QTextLine();
}

/*!
    \since 4.2

    The global position of the layout. This is independent of the
    bounding rectangle and of the layout process.

    \sa setPosition()
*/
QPointF QTextLayout::position() const
{
    return d->position;
}

/*!
    Moves the text layout to point \a p.

    \sa position()
*/
void QTextLayout::setPosition(const QPointF &p)
{
    d->position = p;
}

/*!
    The smallest rectangle that contains all the lines in the layout.
*/
QRectF QTextLayout::boundingRect() const
{
    if (d->lines.isEmpty())
        return QRectF();

    QFixed xmax, ymax;
    QFixed xmin = d->lines.at(0).x;
    QFixed ymin = d->lines.at(0).y;

    for (int i = 0; i < d->lines.size(); ++i) {
        const QScriptLine &si = d->lines[i];
        xmin = qMin(xmin, si.x);
        ymin = qMin(ymin, si.y);
        QFixed lineWidth = si.width < QFIXED_MAX ? qMax(si.width, si.textWidth) : si.textWidth;
        xmax = qMax(xmax, si.x+lineWidth);
        // ### shouldn't the ascent be used in ymin???
        ymax = qMax(ymax, si.y+si.height().ceil());
    }
    return QRectF(xmin.toReal(), ymin.toReal(), (xmax-xmin).toReal(), (ymax-ymin).toReal());
}

/*!
    The minimum width the layout needs. This is the width of the
    layout's smallest non-breakable substring.

    \warning This function only returns a valid value after the layout
    has been done.

    \sa maximumWidth()
*/
qreal QTextLayout::minimumWidth() const
{
    return d->minWidth.toReal();
}

/*!
    The maximum width the layout could expand to; this is essentially
    the width of the entire text.

    \warning This function only returns a valid value after the layout
    has been done.

    \sa minimumWidth()
*/
qreal QTextLayout::maximumWidth() const
{
    return d->maxWidth.toReal();
}


/*!
    \internal
*/
void QTextLayout::setFlags(int flags)
{
    if (flags & Qt::TextJustificationForced) {
        d->option.setAlignment(Qt::AlignJustify);
        d->forceJustification = true;
    }

    if (flags & (Qt::TextForceLeftToRight|Qt::TextForceRightToLeft)) {
        d->ignoreBidi = true;
        d->option.setTextDirection((flags & Qt::TextForceLeftToRight) ? Qt::LeftToRight : Qt::RightToLeft);
    }
}

static void addSelectedRegionsToPath(QTextEngine *eng, int lineNumber, const QPointF &pos, QTextLayout::FormatRange *selection,
                                     QPainterPath *region, QRectF boundingRect)
{
    const QScriptLine &line = eng->lines[lineNumber];

    QTextLineItemIterator iterator(eng, lineNumber, pos, selection);



    const qreal selectionY = pos.y() + line.y.toReal();
    const qreal lineHeight = line.height().toReal();

    QFixed lastSelectionX = iterator.x;
    QFixed lastSelectionWidth;

    while (!iterator.atEnd()) {
        iterator.next();

        QFixed selectionX, selectionWidth;
        if (iterator.getSelectionBounds(&selectionX, &selectionWidth)) {
            if (selectionX == lastSelectionX + lastSelectionWidth) {
                lastSelectionWidth += selectionWidth;
                continue;
            }

            if (lastSelectionWidth > 0) {
                QRectF rect = boundingRect & QRectF(lastSelectionX.toReal(), selectionY, lastSelectionWidth.toReal(), lineHeight);
                rect.moveLeft(qFloor(rect.left()));
                rect.moveTop(qFloor(rect.top()));
                region->addRect(rect);
            }

            lastSelectionX = selectionX;
            lastSelectionWidth = selectionWidth;
        }
    }
    if (lastSelectionWidth > 0) {
        QRectF rect = boundingRect & QRectF(lastSelectionX.toReal(), selectionY, lastSelectionWidth.toReal(), lineHeight);
        rect.moveLeft(qFloor(rect.left()));
        rect.moveTop(qFloor(rect.top()));
        region->addRect(rect);
    }
}

static inline QRectF clipIfValid(const QRectF &rect, const QRectF &clip)
{
    return clip.isValid() ? (rect & clip) : rect;
}


/*!
    Returns the glyph indexes and positions for all glyphs corresponding to the \a length characters
    starting at the position \a from in this QTextLayout. This is an expensive function, and should
    not be called in a time sensitive context.

    If \a from is less than zero, then the glyph run will begin at the first character in the
    layout. If \a length is less than zero, it will span the entire string from the start position.

    \since 4.8

    \sa draw(), QPainter::drawGlyphRun()
*/
#if !defined(QT_NO_RAWFONT)
QList<QGlyphRun> QTextLayout::glyphRuns(int from, int length) const
{
    if (from < 0)
        from = 0;
    if (length < 0)
        length = text().length();

    QHash<QPair<QFontEngine *, int>, QGlyphRun> glyphRunHash;
    for (int i=0; i<d->lines.size(); ++i) {
        if (d->lines[i].from > from + length)
            break;
        else if (d->lines[i].from + d->lines[i].length >= from) {
            QList<QGlyphRun> glyphRuns = QTextLine(i, d).glyphRuns(from, length);

            for (int j = 0; j < glyphRuns.size(); j++) {
                const QGlyphRun &glyphRun = glyphRuns.at(j);
                QRawFont rawFont = glyphRun.rawFont();

                QFontEngine *fontEngine = rawFont.d->fontEngine;
                QGlyphRun::GlyphRunFlags flags = glyphRun.flags();
                QPair<QFontEngine *, int> key(fontEngine, int(flags));
                // merge the glyph runs using the same font
                if (glyphRunHash.contains(key)) {
                    QGlyphRun &oldGlyphRun = glyphRunHash[key];

                    QVector<quint32> indexes = oldGlyphRun.glyphIndexes();
                    QVector<QPointF> positions = oldGlyphRun.positions();

                    indexes += glyphRun.glyphIndexes();
                    positions += glyphRun.positions();

                    oldGlyphRun.setGlyphIndexes(indexes);
                    oldGlyphRun.setPositions(positions);
                } else {
                    glyphRunHash[key] = glyphRun;
                }
            }
        }
    }

    return glyphRunHash.values();
}
#endif // QT_NO_RAWFONT

/*!
    Draws the whole layout on the painter \a p at the position specified by \a pos.
    The rendered layout includes the given \a selections and is clipped within
    the rectangle specified by \a clip.
*/
void QTextLayout::draw(QPainter *p, const QPointF &pos, const QVector<FormatRange> &selections, const QRectF &clip) const
{
    if (d->lines.isEmpty())
        return;

    if (!d->layoutData)
        d->itemize();

    QPointF position = pos + d->position;

    QFixed clipy = (INT_MIN/256);
    QFixed clipe = (INT_MAX/256);
    if (clip.isValid()) {
        clipy = QFixed::fromReal(clip.y() - position.y());
        clipe = clipy + QFixed::fromReal(clip.height());
    }

    int firstLine = 0;
    int lastLine = d->lines.size();
    for (int i = 0; i < d->lines.size(); ++i) {
        QTextLine l(i, d);
        const QScriptLine &sl = d->lines[i];

        if (sl.y > clipe) {
            lastLine = i;
            break;
        }
        if ((sl.y + sl.height()) < clipy) {
            firstLine = i;
            continue;
        }
    }

    QPainterPath excludedRegion;
    QPainterPath textDoneRegion;
    for (int i = 0; i < selections.size(); ++i) {
        FormatRange selection = selections.at(i);
        const QBrush bg = selection.format.background();

        QPainterPath region;
        region.setFillRule(Qt::WindingFill);

        for (int line = firstLine; line < lastLine; ++line) {
            const QScriptLine &sl = d->lines[line];
            QTextLine tl(line, d);

            QRectF lineRect(tl.naturalTextRect());
            lineRect.translate(position);
            lineRect.adjust(0, 0, d->leadingSpaceWidth(sl).toReal(), 0);

            bool isLastLineInBlock = (line == d->lines.size()-1);
            int sl_length = sl.length + (isLastLineInBlock? 1 : 0); // the infamous newline


            if (sl.from > selection.start + selection.length || sl.from + sl_length <= selection.start)
                continue; // no actual intersection

            const bool selectionStartInLine = sl.from <= selection.start;
            const bool selectionEndInLine = selection.start + selection.length < sl.from + sl_length;

            if (sl.length && (selectionStartInLine || selectionEndInLine)) {
                addSelectedRegionsToPath(d, line, position, &selection, &region, clipIfValid(lineRect, clip));
            } else {
                region.addRect(clipIfValid(lineRect, clip));
            }

            if (selection.format.boolProperty(QTextFormat::FullWidthSelection)) {
                QRectF fullLineRect(tl.rect());
                fullLineRect.translate(position);
                fullLineRect.setRight(QFIXED_MAX);
                if (!selectionEndInLine)
                    region.addRect(clipIfValid(QRectF(lineRect.topRight(), fullLineRect.bottomRight()), clip));
                if (!selectionStartInLine)
                    region.addRect(clipIfValid(QRectF(fullLineRect.topLeft(), lineRect.bottomLeft()), clip));
            } else if (!selectionEndInLine
                && isLastLineInBlock
                &&!(d->option.flags() & QTextOption::ShowLineAndParagraphSeparators)) {
                region.addRect(clipIfValid(QRectF(lineRect.right(), lineRect.top(),
                                                  lineRect.height()/4, lineRect.height()), clip));
            }

        }
        {
            const QPen oldPen = p->pen();
            const QBrush oldBrush = p->brush();

            p->setPen(selection.format.penProperty(QTextFormat::OutlinePen));
            p->setBrush(selection.format.brushProperty(QTextFormat::BackgroundBrush));
            p->drawPath(region);

            p->setPen(oldPen);
            p->setBrush(oldBrush);
        }



        bool hasText = (selection.format.foreground().style() != Qt::NoBrush);
        bool hasBackground= (selection.format.background().style() != Qt::NoBrush);

        if (hasBackground) {
            selection.format.setProperty(ObjectSelectionBrush, selection.format.property(QTextFormat::BackgroundBrush));
            // don't just clear the property, set an empty brush that overrides a potential
            // background brush specified in the text
            selection.format.setProperty(QTextFormat::BackgroundBrush, QBrush());
            selection.format.clearProperty(QTextFormat::OutlinePen);
        }

        selection.format.setProperty(SuppressText, !hasText);

        if (hasText && !hasBackground && !(textDoneRegion & region).isEmpty())
            continue;

        p->save();
        p->setClipPath(region, Qt::IntersectClip);

        for (int line = firstLine; line < lastLine; ++line) {
            QTextLine l(line, d);
            l.draw(p, position, &selection);
        }
        p->restore();

        if (hasText) {
            textDoneRegion += region;
        } else {
            if (hasBackground)
                textDoneRegion -= region;
        }

        excludedRegion += region;
    }

    QPainterPath needsTextButNoBackground = excludedRegion - textDoneRegion;
    if (!needsTextButNoBackground.isEmpty()){
        p->save();
        p->setClipPath(needsTextButNoBackground, Qt::IntersectClip);
        FormatRange selection;
        selection.start = 0;
        selection.length = INT_MAX;
        selection.format.setProperty(SuppressBackground, true);
        for (int line = firstLine; line < lastLine; ++line) {
            QTextLine l(line, d);
            l.draw(p, position, &selection);
        }
        p->restore();
    }

    if (!excludedRegion.isEmpty()) {
        p->save();
        QPainterPath path;
        QRectF br = boundingRect().translated(position);
        br.setRight(QFIXED_MAX);
        if (!clip.isNull())
            br = br.intersected(clip);
        path.addRect(br);
        path -= excludedRegion;
        p->setClipPath(path, Qt::IntersectClip);
    }

    for (int i = firstLine; i < lastLine; ++i) {
        QTextLine l(i, d);
        l.draw(p, position);
    }
    if (!excludedRegion.isEmpty())
        p->restore();


    if (!d->cacheGlyphs)
        d->freeMemory();
}

/*!
    \fn void QTextLayout::drawCursor(QPainter *painter, const QPointF &position, int cursorPosition) const
    \overload

    Draws a text cursor with the current pen at the given \a position using the
    \a painter specified.
    The corresponding position within the text is specified by \a cursorPosition.
*/
void QTextLayout::drawCursor(QPainter *p, const QPointF &pos, int cursorPosition) const
{
    drawCursor(p, pos, cursorPosition, 1);
}

/*!
    \fn void QTextLayout::drawCursor(QPainter *painter, const QPointF &position, int cursorPosition, int width) const

    Draws a text cursor with the current pen and the specified \a width at the given \a position using the
    \a painter specified.
    The corresponding position within the text is specified by \a cursorPosition.
*/
void QTextLayout::drawCursor(QPainter *p, const QPointF &pos, int cursorPosition, int width) const
{
    if (d->lines.isEmpty())
        return;

    if (!d->layoutData)
        d->itemize();

    QPointF position = pos + d->position;

    cursorPosition = qBound(0, cursorPosition, d->layoutData->string.length());
    int line = d->lineNumberForTextPosition(cursorPosition);
    if (line < 0)
        line = 0;
    if (line >= d->lines.size())
        return;

    QTextLine l(line, d);
    const QScriptLine &sl = d->lines[line];

    qreal x = position.x() + l.cursorToX(cursorPosition);

    int itm;

    if (d->visualCursorMovement()) {
        if (cursorPosition == sl.from + sl.length)
            cursorPosition--;
        itm = d->findItem(cursorPosition);
    } else
        itm = d->findItem(cursorPosition - 1);

    QFixed base = sl.base();
    QFixed descent = sl.descent;
    bool rightToLeft = d->isRightToLeft();
    if (itm >= 0) {
        const QScriptItem &si = d->layoutData->items.at(itm);
        if (si.ascent > 0)
            base = si.ascent;
        if (si.descent > 0)
            descent = si.descent;
        rightToLeft = si.analysis.bidiLevel % 2;
    }
    qreal y = position.y() + (sl.y + sl.base() - base).toReal();
    bool toggleAntialiasing = !(p->renderHints() & QPainter::Antialiasing)
                              && (p->transform().type() > QTransform::TxTranslate);
    if (toggleAntialiasing)
        p->setRenderHint(QPainter::Antialiasing);
    p->fillRect(QRectF(x, y, qreal(width), (base + descent).toReal()), p->pen().brush());
    if (toggleAntialiasing)
        p->setRenderHint(QPainter::Antialiasing, false);
    if (d->layoutData->hasBidi) {
        const int arrow_extent = 4;
        int sign = rightToLeft ? -1 : 1;
        p->drawLine(QLineF(x, y, x + (sign * arrow_extent/2), y + arrow_extent/2));
        p->drawLine(QLineF(x, y+arrow_extent, x + (sign * arrow_extent/2), y + arrow_extent/2));
    }
    return;
}

/*!
    \class QTextLine
    \reentrant

    \brief The QTextLine class represents a line of text inside a QTextLayout.
    \inmodule QtGui

    \ingroup richtext-processing

    A text line is usually created by QTextLayout::createLine().

    After being created, the line can be filled using the setLineWidth()
    or setNumColumns() functions. A line has a number of attributes including the
    rectangle it occupies, rect(), its coordinates, x() and y(), its
    textLength(), width() and naturalTextWidth(), and its ascent() and descent()
    relative to the text. The position of the cursor in terms of the
    line is available from cursorToX() and its inverse from
    xToCursor(). A line can be moved with setPosition().
*/

/*!
    \enum QTextLine::Edge

    \value Leading
    \value Trailing
*/

/*!
    \enum QTextLine::CursorPosition

    \value CursorBetweenCharacters
    \value CursorOnCharacter
*/

/*!
    \fn QTextLine::QTextLine(int line, QTextEngine *e)
    \internal

    Constructs a new text line using the line at position \a line in
    the text engine \a e.
*/

/*!
    \fn QTextLine::QTextLine()

    Creates an invalid line.
*/

/*!
    \fn bool QTextLine::isValid() const

    Returns \c true if this text line is valid; otherwise returns \c false.
*/

/*!
    \fn int QTextLine::lineNumber() const

    Returns the position of the line in the text engine.
*/


/*!
    Returns the line's bounding rectangle.

    \sa x(), y(), textLength(), width()
*/
QRectF QTextLine::rect() const
{
    const QScriptLine& sl = eng->lines[index];
    return QRectF(sl.x.toReal(), sl.y.toReal(), sl.width.toReal(), sl.height().toReal());
}

/*!
    Returns the rectangle covered by the line.
*/
QRectF QTextLine::naturalTextRect() const
{
    const QScriptLine& sl = eng->lines[index];
    QFixed x = sl.x + eng->alignLine(sl);

    QFixed width = sl.textWidth;
    if (sl.justified)
        width = sl.width;

    return QRectF(x.toReal(), sl.y.toReal(), width.toReal(), sl.height().toReal());
}

/*!
    Returns the line's x position.

    \sa rect(), y(), textLength(), width()
*/
qreal QTextLine::x() const
{
    return eng->lines[index].x.toReal();
}

/*!
    Returns the line's y position.

    \sa x(), rect(), textLength(), width()
*/
qreal QTextLine::y() const
{
    return eng->lines[index].y.toReal();
}

/*!
    Returns the line's width as specified by the layout() function.

    \sa naturalTextWidth(), x(), y(), textLength(), rect()
*/
qreal QTextLine::width() const
{
    return eng->lines[index].width.toReal();
}


/*!
    Returns the line's ascent.

    \sa descent(), height()
*/
qreal QTextLine::ascent() const
{
    return eng->lines[index].ascent.toReal();
}

/*!
    Returns the line's descent.

    \sa ascent(), height()
*/
qreal QTextLine::descent() const
{
    return eng->lines[index].descent.toReal();
}

/*!
    Returns the line's height. This is equal to ascent() + descent()
    if leading is not included. If leading is included, this equals to
    ascent() + descent() + leading().

    \sa ascent(), descent(), leading(), setLeadingIncluded()
*/
qreal QTextLine::height() const
{
    return eng->lines[index].height().ceil().toReal();
}

/*!
    \since 4.6

    Returns the line's leading.

    \sa ascent(), descent(), height()
*/
qreal QTextLine::leading() const
{
    return eng->lines[index].leading.toReal();
}

/*!
    \since 4.6

    Includes positive leading into the line's height if \a included is true;
    otherwise does not include leading.

    By default, leading is not included.

    Note that negative leading is ignored, it must be handled
    in the code using the text lines by letting the lines overlap.

    \sa leadingIncluded()

*/
void QTextLine::setLeadingIncluded(bool included)
{
    eng->lines[index].leadingIncluded= included;

}

/*!
    \since 4.6

    Returns \c true if positive leading is included into the line's height;
    otherwise returns \c false.

    By default, leading is not included.

    \sa setLeadingIncluded()
*/
bool QTextLine::leadingIncluded() const
{
    return eng->lines[index].leadingIncluded;
}

/*!
    Returns the width of the line that is occupied by text. This is
    always \<= to width(), and is the minimum width that could be used
    by layout() without changing the line break position.
*/
qreal QTextLine::naturalTextWidth() const
{
    return eng->lines[index].textWidth.toReal();
}

/*!
    \since 4.7
    Returns the horizontal advance of the text. The advance of the text
    is the distance from its position to the next position at which
    text would naturally be drawn.

    By adding the advance to the position of the text line and using this
    as the position of a second text line, you will be able to position
    the two lines side-by-side without gaps in-between.
*/
qreal QTextLine::horizontalAdvance() const
{
    return eng->lines[index].textAdvance.toReal();
}

/*!
    Lays out the line with the given \a width. The line is filled from
    its starting position with as many characters as will fit into
    the line. In case the text cannot be split at the end of the line,
    it will be filled with additional characters to the next whitespace
    or end of the text.
*/
void QTextLine::setLineWidth(qreal width)
{
    QScriptLine &line = eng->lines[index];
    if (!eng->layoutData) {
        qWarning("QTextLine: Can't set a line width while not layouting.");
        return;
    }

    if (width > QFIXED_MAX)
        width = QFIXED_MAX;

    line.width = QFixed::fromReal(width);
    if (line.length
        && line.textWidth <= line.width
        && line.from + line.length == eng->layoutData->string.length())
        // no need to do anything if the line is already layouted and the last one. This optimization helps
        // when using things in a single line layout.
        return;
    line.length = 0;
    line.textWidth = 0;

    layout_helper(INT_MAX);
}

/*!
    Lays out the line. The line is filled from its starting position
    with as many characters as are specified by \a numColumns. In case
    the text cannot be split until \a numColumns characters, the line
    will be filled with as many characters to the next whitespace or
    end of the text.
*/
void QTextLine::setNumColumns(int numColumns)
{
    QScriptLine &line = eng->lines[index];
    line.width = QFIXED_MAX;
    line.length = 0;
    line.textWidth = 0;
    layout_helper(numColumns);
}

/*!
    Lays out the line. The line is filled from its starting position
    with as many characters as are specified by \a numColumns. In case
    the text cannot be split until \a numColumns characters, the line
    will be filled with as many characters to the next whitespace or
    end of the text. The provided \a alignmentWidth is used as reference
    width for alignment.
*/
void QTextLine::setNumColumns(int numColumns, qreal alignmentWidth)
{
    QScriptLine &line = eng->lines[index];
    line.width = QFixed::fromReal(alignmentWidth);
    line.length = 0;
    line.textWidth = 0;
    layout_helper(numColumns);
}

#if 0
#define LB_DEBUG qDebug
#else
#define LB_DEBUG if (0) qDebug
#endif

namespace {

    struct LineBreakHelper
    {
        LineBreakHelper()
            : glyphCount(0), maxGlyphs(0), currentPosition(0), fontEngine(0), logClusters(0),
              manualWrap(false), whiteSpaceOrObject(true)
        {
        }


        QScriptLine tmpData;
        QScriptLine spaceData;

        QGlyphLayout glyphs;

        int glyphCount;
        int maxGlyphs;
        int currentPosition;
        glyph_t previousGlyph;

        QFixed minw;
        QFixed softHyphenWidth;
        QFixed rightBearing;
        QFixed minimumRightBearing;

        QFontEngine *fontEngine;
        const unsigned short *logClusters;

        bool manualWrap;
        bool whiteSpaceOrObject;

        bool checkFullOtherwiseExtend(QScriptLine &line);

        QFixed calculateNewWidth(const QScriptLine &line) const {
            return line.textWidth + tmpData.textWidth + spaceData.textWidth + softHyphenWidth
                    - qMin(rightBearing, QFixed());
        }

        inline glyph_t currentGlyph() const
        {
            Q_ASSERT(currentPosition > 0);
            Q_ASSERT(logClusters[currentPosition - 1] < glyphs.numGlyphs);

            return glyphs.glyphs[logClusters[currentPosition - 1]];
        }

        inline void saveCurrentGlyph()
        {
            previousGlyph = 0;
            if (currentPosition > 0 &&
                logClusters[currentPosition - 1] < glyphs.numGlyphs) {
                previousGlyph = currentGlyph(); // needed to calculate right bearing later
            }
        }

        inline void adjustRightBearing(glyph_t glyph)
        {
            qreal rb;
            fontEngine->getGlyphBearings(glyph, 0, &rb);
            rightBearing = qMin(QFixed(), QFixed::fromReal(rb));
        }

        inline void adjustRightBearing()
        {
            if (currentPosition <= 0)
                return;
            adjustRightBearing(currentGlyph());
        }

        inline void adjustPreviousRightBearing()
        {
            if (previousGlyph > 0)
                adjustRightBearing(previousGlyph);
        }

        inline void resetRightBearing()
        {
            rightBearing = QFixed(1); // Any positive number is defined as invalid since only
                                      // negative right bearings are interesting to us.
        }
    };

inline bool LineBreakHelper::checkFullOtherwiseExtend(QScriptLine &line)
{
    LB_DEBUG("possible break width %f, spacew=%f", tmpData.textWidth.toReal(), spaceData.textWidth.toReal());

    QFixed newWidth = calculateNewWidth(line);
    if (line.length && !manualWrap && (newWidth > line.width || glyphCount > maxGlyphs))
        return true;

    minw = qMax(minw, tmpData.textWidth);
    line += tmpData;
    line.textWidth += spaceData.textWidth;

    line.length += spaceData.length;
    tmpData.textWidth = 0;
    tmpData.length = 0;
    spaceData.textWidth = 0;
    spaceData.length = 0;

    return false;
}

} // anonymous namespace


static inline void addNextCluster(int &pos, int end, QScriptLine &line, int &glyphCount,
                                  const QScriptItem &current, const unsigned short *logClusters,
                                  const QGlyphLayout &glyphs)
{
    int glyphPosition = logClusters[pos];
    do { // got to the first next cluster
        ++pos;
        ++line.length;
    } while (pos < end && logClusters[pos] == glyphPosition);
    do { // calculate the textWidth for the rest of the current cluster.
        if (!glyphs.attributes[glyphPosition].dontPrint)
            line.textWidth += glyphs.advances[glyphPosition];
        ++glyphPosition;
    } while (glyphPosition < current.num_glyphs && !glyphs.attributes[glyphPosition].clusterStart);

    Q_ASSERT((pos == end && glyphPosition == current.num_glyphs) || logClusters[pos] == glyphPosition);

    ++glyphCount;
}


// fill QScriptLine
void QTextLine::layout_helper(int maxGlyphs)
{
    QScriptLine &line = eng->lines[index];
    line.length = 0;
    line.trailingSpaces = 0;
    line.textWidth = 0;
    line.hasTrailingSpaces = false;

    if (!eng->layoutData->items.size() || line.from >= eng->layoutData->string.length()) {
        line.setDefaultHeight(eng);
        return;
    }

    Q_ASSERT(line.from < eng->layoutData->string.length());

    LineBreakHelper lbh;

    lbh.maxGlyphs = maxGlyphs;

    QTextOption::WrapMode wrapMode = eng->option.wrapMode();
    bool breakany = (wrapMode == QTextOption::WrapAnywhere);
    lbh.manualWrap = (wrapMode == QTextOption::ManualWrap || wrapMode == QTextOption::NoWrap);

    int item = -1;
    int newItem = eng->findItem(line.from);
    Q_ASSERT(newItem >= 0);

    LB_DEBUG("from: %d: item=%d, total %d, width available %f", line.from, newItem, eng->layoutData->items.size(), line.width.toReal());

    Qt::Alignment alignment = eng->option.alignment();

    const QCharAttributes *attributes = eng->attributes();
    if (!attributes)
        return;
    lbh.currentPosition = line.from;
    int end = 0;
    lbh.logClusters = eng->layoutData->logClustersPtr;
    lbh.previousGlyph = 0;

    while (newItem < eng->layoutData->items.size()) {
        lbh.resetRightBearing();
        lbh.softHyphenWidth = 0;
        if (newItem != item) {
            item = newItem;
            const QScriptItem &current = eng->layoutData->items[item];
            if (!current.num_glyphs) {
                eng->shape(item);
                attributes = eng->attributes();
                if (!attributes)
                    return;
                lbh.logClusters = eng->layoutData->logClustersPtr;
            }
            lbh.currentPosition = qMax(line.from, current.position);
            end = current.position + eng->length(item);
            lbh.glyphs = eng->shapedGlyphs(&current);
            QFontEngine *fontEngine = eng->fontEngine(current);
            if (lbh.fontEngine != fontEngine) {
                lbh.fontEngine = fontEngine;
                lbh.minimumRightBearing = qMin(QFixed(),
                                               QFixed::fromReal(fontEngine->minRightBearing()));
            }
        }
        const QScriptItem &current = eng->layoutData->items[item];

        lbh.tmpData.leading = qMax(lbh.tmpData.leading + lbh.tmpData.ascent,
                                   current.leading + current.ascent) - qMax(lbh.tmpData.ascent,
                                                                            current.ascent);
        lbh.tmpData.ascent = qMax(lbh.tmpData.ascent, current.ascent);
        lbh.tmpData.descent = qMax(lbh.tmpData.descent, current.descent);

        if (current.analysis.flags == QScriptAnalysis::Tab && (alignment & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignCenter | Qt::AlignJustify))) {
            lbh.whiteSpaceOrObject = true;
            if (lbh.checkFullOtherwiseExtend(line))
                goto found;

            QFixed x = line.x + line.textWidth + lbh.tmpData.textWidth + lbh.spaceData.textWidth;
            QFixed tabWidth = eng->calculateTabWidth(item, x);

            lbh.spaceData.textWidth += tabWidth;
            lbh.spaceData.length++;
            newItem = item + 1;

            QFixed averageCharWidth = eng->fontEngine(current)->averageCharWidth();
            lbh.glyphCount += qRound(tabWidth / averageCharWidth);

            if (lbh.checkFullOtherwiseExtend(line))
                goto found;
        } else if (current.analysis.flags == QScriptAnalysis::LineOrParagraphSeparator) {
            lbh.whiteSpaceOrObject = true;
            // if the line consists only of the line separator make sure
            // we have a sane height
            if (!line.length && !lbh.tmpData.length)
                line.setDefaultHeight(eng);
            if (eng->option.flags() & QTextOption::ShowLineAndParagraphSeparators) {
                if (lbh.checkFullOtherwiseExtend(line))
                    goto found;

                addNextCluster(lbh.currentPosition, end, lbh.tmpData, lbh.glyphCount,
                               current, lbh.logClusters, lbh.glyphs);
            } else {
                lbh.tmpData.length++;
                lbh.adjustPreviousRightBearing();
            }
            line += lbh.tmpData;
            goto found;
        } else if (current.analysis.flags == QScriptAnalysis::Object) {
            lbh.whiteSpaceOrObject = true;
            lbh.tmpData.length++;

            if (eng->block.docHandle()) {
                QTextInlineObject inlineObject(item, eng);
                eng->docLayout()->positionInlineObject(inlineObject, eng->block.position() + current.position, inlineObject.format());
            }

            lbh.tmpData.textWidth += current.width;

            newItem = item + 1;
            ++lbh.glyphCount;
            if (lbh.checkFullOtherwiseExtend(line))
                goto found;
        } else if (attributes[lbh.currentPosition].whiteSpace) {
            lbh.whiteSpaceOrObject = true;
            while (lbh.currentPosition < end && attributes[lbh.currentPosition].whiteSpace)
                addNextCluster(lbh.currentPosition, end, lbh.spaceData, lbh.glyphCount,
                               current, lbh.logClusters, lbh.glyphs);

            if (!lbh.manualWrap && lbh.spaceData.textWidth > line.width) {
                lbh.spaceData.textWidth = line.width; // ignore spaces that fall out of the line.
                goto found;
            }
        } else {
            lbh.whiteSpaceOrObject = false;
            bool sb_or_ws = false;
            lbh.saveCurrentGlyph();
            do {
                addNextCluster(lbh.currentPosition, end, lbh.tmpData, lbh.glyphCount,
                               current, lbh.logClusters, lbh.glyphs);

                // This is a hack to fix a regression caused by the introduction of the
                // whitespace flag to non-breakable spaces and will cause the non-breakable
                // spaces to behave as in previous Qt versions in the line breaking algorithm.
                // The line breaks do not currently follow the Unicode specs, but fixing this would
                // require refactoring the code and would cause behavioral regressions.
                bool isBreakableSpace = lbh.currentPosition < eng->layoutData->string.length()
                                        && attributes[lbh.currentPosition].whiteSpace
                                        && eng->layoutData->string.at(lbh.currentPosition).decompositionTag() != QChar::NoBreak;

                if (lbh.currentPosition >= eng->layoutData->string.length()
                    || isBreakableSpace
                    || attributes[lbh.currentPosition].lineBreak) {
                    sb_or_ws = true;
                    break;
                } else if (breakany && attributes[lbh.currentPosition].graphemeBoundary) {
                    break;
                }
            } while (lbh.currentPosition < end);
            lbh.minw = qMax(lbh.tmpData.textWidth, lbh.minw);

            if (lbh.currentPosition > 0 && lbh.currentPosition < end
                && attributes[lbh.currentPosition].lineBreak
                && eng->layoutData->string.at(lbh.currentPosition - 1).unicode() == QChar::SoftHyphen) {
                // if we are splitting up a word because of
                // a soft hyphen then we ...
                //
                //  a) have to take the width of the soft hyphen into
                //     account to see if the first syllable(s) /and/
                //     the soft hyphen fit into the line
                //
                //  b) if we are so short of available width that the
                //     soft hyphen is the first breakable position, then
                //     we don't want to show it. However we initially
                //     have to take the width for it into account so that
                //     the text document layout sees the overflow and
                //     switch to break-anywhere mode, in which we
                //     want the soft-hyphen to slip into the next line
                //     and thus become invisible again.
                //
                if (line.length)
                    lbh.softHyphenWidth = lbh.glyphs.advances[lbh.logClusters[lbh.currentPosition - 1]];
                else if (breakany)
                    lbh.tmpData.textWidth += lbh.glyphs.advances[lbh.logClusters[lbh.currentPosition - 1]];
            }

            // The actual width of the text needs to take the right bearing into account. The
            // right bearing is left-ward, which means that if the rightmost pixel is to the right
            // of the advance of the glyph, the bearing will be negative. We flip the sign
            // for the code to be more readable. Logic borrowed from qfontmetrics.cpp.
            // We ignore the right bearing if the minimum negative bearing is too little to
            // expand the text beyond the edge.
            if (sb_or_ws|breakany) {
                QFixed rightBearing = lbh.rightBearing; // store previous right bearing
                if (lbh.calculateNewWidth(line) - lbh.minimumRightBearing > line.width)
                    lbh.adjustRightBearing();
                if (lbh.checkFullOtherwiseExtend(line)) {
                    // we are too wide, fix right bearing
                    if (rightBearing <= 0)
                        lbh.rightBearing = rightBearing; // take from cache
                    else
                        lbh.adjustPreviousRightBearing();

                    if (!breakany) {
                        line.textWidth += lbh.softHyphenWidth;
                    }

                    goto found;
                }
            }
            lbh.saveCurrentGlyph();
        }
        if (lbh.currentPosition == end)
            newItem = item + 1;
    }
    LB_DEBUG("reached end of line");
    lbh.checkFullOtherwiseExtend(line);
found:
    if (lbh.rightBearing > 0 && !lbh.whiteSpaceOrObject) // If right bearing has not yet been adjusted
        lbh.adjustRightBearing();
    line.textAdvance = line.textWidth;
    line.textWidth -= qMin(QFixed(), lbh.rightBearing);

    if (line.length == 0) {
        LB_DEBUG("no break available in line, adding temp: length %d, width %f, space: length %d, width %f",
               lbh.tmpData.length, lbh.tmpData.textWidth.toReal(),
               lbh.spaceData.length, lbh.spaceData.textWidth.toReal());
        line += lbh.tmpData;
    }

    LB_DEBUG("line length = %d, ascent=%f, descent=%f, textWidth=%f (spacew=%f)", line.length, line.ascent.toReal(),
           line.descent.toReal(), line.textWidth.toReal(), lbh.spaceData.width.toReal());
    LB_DEBUG("        : '%s'", eng->layoutData->string.mid(line.from, line.length).toUtf8().data());

    if (lbh.manualWrap) {
        eng->minWidth = qMax(eng->minWidth, line.textWidth);
        eng->maxWidth = qMax(eng->maxWidth, line.textWidth);
    } else {
        eng->minWidth = qMax(eng->minWidth, lbh.minw);
        eng->maxWidth += line.textWidth;
    }

    if (line.textWidth > 0 && item < eng->layoutData->items.size())
        eng->maxWidth += lbh.spaceData.textWidth;
    if (eng->option.flags() & QTextOption::IncludeTrailingSpaces)
        line.textWidth += lbh.spaceData.textWidth;
    if (lbh.spaceData.length) {
        line.trailingSpaces = lbh.spaceData.length;
        line.hasTrailingSpaces = true;
    }

    line.justified = false;
    line.gridfitted = false;

    if (eng->option.wrapMode() == QTextOption::WrapAtWordBoundaryOrAnywhere) {
        if ((lbh.maxGlyphs != INT_MAX && lbh.glyphCount > lbh.maxGlyphs)
            || (lbh.maxGlyphs == INT_MAX && line.textWidth > line.width)) {

            eng->option.setWrapMode(QTextOption::WrapAnywhere);
            line.length = 0;
            line.textWidth = 0;
            layout_helper(lbh.maxGlyphs);
            eng->option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        }
    }
}

/*!
    Moves the line to position \a pos.
*/
void QTextLine::setPosition(const QPointF &pos)
{
    eng->lines[index].x = QFixed::fromReal(pos.x());
    eng->lines[index].y = QFixed::fromReal(pos.y());
}

/*!
    Returns the line's position relative to the text layout's position.
*/
QPointF QTextLine::position() const
{
    return QPointF(eng->lines[index].x.toReal(), eng->lines[index].y.toReal());
}

// ### DOC: I have no idea what this means/does.
// You create a text layout with a string of text. Once you laid
// it out, it contains a number of QTextLines. from() returns the position
// inside the text string where this line starts. If you e.g. has a
// text of "This is a string", laid out into two lines (the second
// starting at the word 'a'), layout.lineAt(0).from() == 0 and
// layout.lineAt(1).from() == 8.
/*!
    Returns the start of the line from the beginning of the string
    passed to the QTextLayout.
*/
int QTextLine::textStart() const
{
    return eng->lines[index].from;
}

/*!
    Returns the length of the text in the line.

    \sa naturalTextWidth()
*/
int QTextLine::textLength() const
{
    if (eng->option.flags() & QTextOption::ShowLineAndParagraphSeparators
        && eng->block.isValid() && index == eng->lines.count()-1) {
        return eng->lines[index].length - 1;
    }
    return eng->lines[index].length + eng->lines[index].trailingSpaces;
}

static void setPenAndDrawBackground(QPainter *p, const QPen &defaultPen, const QTextCharFormat &chf, const QRectF &r)
{
    QBrush c = chf.foreground();
    if (c.style() == Qt::NoBrush) {
        p->setPen(defaultPen);
    }

    QBrush bg = chf.background();
    if (bg.style() != Qt::NoBrush && !chf.property(SuppressBackground).toBool())
        p->fillRect(QRectF(qFloor(r.x()), qFloor(r.y()), r.width(), r.height()), bg);
    if (c.style() != Qt::NoBrush) {
        p->setPen(QPen(c, 0));
    }

}

#if !defined(QT_NO_RAWFONT)
static QGlyphRun glyphRunWithInfo(QFontEngine *fontEngine,
                                  const QGlyphLayout &glyphLayout,
                                  const QPointF &pos,
                                  const QGlyphRun::GlyphRunFlags &flags,
                                  const QFixed &selectionX,
                                  const QFixed &selectionWidth,
                                  int glyphsStart,
                                  int glyphsEnd,
                                  unsigned short *logClusters,
                                  int textPosition,
                                  int textLength)
{
    Q_ASSERT(logClusters != 0);

    QGlyphRun glyphRun;

    QGlyphRunPrivate *d = QGlyphRunPrivate::get(glyphRun);

    int rangeStart = textPosition;
    while (*logClusters != glyphsStart && rangeStart < textPosition + textLength) {
        ++logClusters;
        ++rangeStart;
    }

    int rangeEnd = rangeStart;
    while (*logClusters != glyphsEnd && rangeEnd < textPosition + textLength) {
        ++logClusters;
        ++rangeEnd;
    }

    d->textRangeStart = rangeStart;
    d->textRangeEnd = rangeEnd;

    // Make a font for this particular engine
    QRawFont font;
    QRawFontPrivate *fontD = QRawFontPrivate::get(font);
    fontD->fontEngine = fontEngine;
    fontD->thread = QThread::currentThread();
    fontD->fontEngine->ref.ref();
    QVarLengthArray<glyph_t> glyphsArray;
    QVarLengthArray<QFixedPoint> positionsArray;

    QTextItem::RenderFlags renderFlags;
    if (flags.testFlag(QGlyphRun::Overline))
        renderFlags |= QTextItem::Overline;
    if (flags.testFlag(QGlyphRun::Underline))
        renderFlags |= QTextItem::Underline;
    if (flags.testFlag(QGlyphRun::StrikeOut))
        renderFlags |= QTextItem::StrikeOut;
    if (flags.testFlag(QGlyphRun::RightToLeft))
        renderFlags |= QTextItem::RightToLeft;

    fontEngine->getGlyphPositions(glyphLayout, QTransform(), renderFlags, glyphsArray,
                                  positionsArray);
    Q_ASSERT(glyphsArray.size() == positionsArray.size());

    qreal fontHeight = font.ascent() + font.descent();
    qreal minY = 0;
    qreal maxY = 0;
    QVector<quint32> glyphs;
    QVector<QPointF> positions;
    for (int i=0; i<glyphsArray.size(); ++i) {
        glyphs.append(glyphsArray.at(i) & 0xffffff);

        QPointF position = positionsArray.at(i).toPointF() + pos;
        positions.append(position);

        if (i == 0) {
            maxY = minY = position.y();
        } else {
            minY = qMin(minY, position.y());
            maxY = qMax(maxY, position.y());
        }
    }

    qreal height = maxY + fontHeight - minY;

    glyphRun.setGlyphIndexes(glyphs);
    glyphRun.setPositions(positions);
    glyphRun.setFlags(flags);
    glyphRun.setRawFont(font);

    glyphRun.setBoundingRect(QRectF(selectionX.toReal(), minY - font.ascent(),
                                    selectionWidth.toReal(), height));

    return glyphRun;
}

/*!
    Returns the glyph indexes and positions for all glyphs in this QTextLine for characters
    in the range defined by \a from and \a length. The \a from index is relative to the beginning
    of the text in the containing QTextLayout, and the range must be within the range of QTextLine
    as given by functions textStart() and textLength().

    If \a from is negative, it will default to textStart(), and if \a length is negative it will
    default to the return value of textLength().

    \since 5.0

    \sa QTextLayout::glyphRuns()
*/
QList<QGlyphRun> QTextLine::glyphRuns(int from, int length) const
{
    const QScriptLine &line = eng->lines[index];

    if (line.length == 0)
        return QList<QGlyphRun>();

    if (from < 0)
        from = textStart();

    if (length < 0)
        length = textLength();

    if (length == 0)
        return QList<QGlyphRun>();

    QTextLayout::FormatRange selection;
    selection.start = from;
    selection.length = length;

    QTextLineItemIterator iterator(eng, index, QPointF(), &selection);
    qreal y = line.y.toReal() + line.base().toReal();
    QList<QGlyphRun> glyphRuns;
    while (!iterator.atEnd()) {
        QScriptItem &si = iterator.next();
        if (si.analysis.flags >= QScriptAnalysis::TabOrObject)
            continue;

        if (from >= 0 && length >= 0 && (from >= iterator.itemEnd || from + length <= iterator.itemStart))
            continue;

        QPointF pos(iterator.x.toReal(), y);

        QFont font;
        QGlyphRun::GlyphRunFlags flags;
        if (!eng->useRawFont) {
            font = eng->font(si);
            if (font.overline())
                flags |= QGlyphRun::Overline;
            if (font.underline())
                flags |= QGlyphRun::Underline;
            if (font.strikeOut())
                flags |= QGlyphRun::StrikeOut;
        }

        bool rtl = false;
        if (si.analysis.bidiLevel % 2) {
            flags |= QGlyphRun::RightToLeft;
            rtl = true;
        }

        int relativeFrom = qMax(iterator.itemStart, from) - si.position;
        int relativeTo = qMin(iterator.itemEnd, from + length) - 1 - si.position;

        unsigned short *logClusters = eng->logClusters(&si);
        int glyphsStart = logClusters[relativeFrom];
        int glyphsEnd = (relativeTo == iterator.itemLength) ? si.num_glyphs - 1 : logClusters[relativeTo];
        // the glyph index right next to the requested range
        int nextGlyphIndex = (relativeTo < iterator.itemLength - 1) ? logClusters[relativeTo + 1] : si.num_glyphs;
        if (nextGlyphIndex - 1 > glyphsEnd)
            glyphsEnd = nextGlyphIndex - 1;
        bool startsInsideLigature = relativeFrom > 0 && logClusters[relativeFrom - 1] == glyphsStart;
        bool endsInsideLigature = nextGlyphIndex == glyphsEnd;

        int itemGlyphsStart = logClusters[iterator.itemStart - si.position];
        int itemGlyphsEnd = logClusters[iterator.itemEnd - 1 - si.position];

        QGlyphLayout glyphLayout = eng->shapedGlyphs(&si);

        // Calculate new x position of glyph layout for a subset. This becomes somewhat complex
        // when we're breaking a RTL script item, since the expected position passed into
        // getGlyphPositions() is the left-most edge of the left-most glyph in an RTL run.
        if (relativeFrom != (iterator.itemStart - si.position) && !rtl) {
            for (int i=itemGlyphsStart; i<glyphsStart; ++i) {
                QFixed justification = QFixed::fromFixed(glyphLayout.justifications[i].space_18d6);
                pos.rx() += (glyphLayout.advances[i] + justification).toReal();
            }
        } else if (relativeTo != (iterator.itemEnd - si.position - 1) && rtl) {
            for (int i=itemGlyphsEnd; i>glyphsEnd; --i) {
                QFixed justification = QFixed::fromFixed(glyphLayout.justifications[i].space_18d6);
                pos.rx() += (glyphLayout.advances[i] + justification).toReal();
            }
        }

        glyphLayout = glyphLayout.mid(glyphsStart, glyphsEnd - glyphsStart + 1);

        QFixed x;
        QFixed width;
        iterator.getSelectionBounds(&x, &width);

        if (glyphLayout.numGlyphs > 0) {
            QFontEngine *mainFontEngine;
#ifndef QT_NO_RAWFONT
            if (eng->useRawFont && eng->rawFont.isValid())
                mainFontEngine= eng->fontEngine(si);
            else
#endif
                mainFontEngine = font.d->engineForScript(si.analysis.script);

            if (mainFontEngine->type() == QFontEngine::Multi) {
                QFontEngineMulti *multiFontEngine = static_cast<QFontEngineMulti *>(mainFontEngine);
                int end = rtl ? glyphLayout.numGlyphs : 0;
                int start = rtl ? end : 0;
                int which = glyphLayout.glyphs[rtl ? start - 1 : end] >> 24;
                for (; (rtl && start > 0) || (!rtl && end < glyphLayout.numGlyphs);
                     rtl ? --start : ++end) {
                    const int e = glyphLayout.glyphs[rtl ? start - 1 : end] >> 24;
                    if (e == which)
                        continue;

                    QGlyphLayout subLayout = glyphLayout.mid(start, end - start);
                    multiFontEngine->ensureEngineAt(which);

                    QGlyphRun::GlyphRunFlags subFlags = flags;
                    if (start == 0 && startsInsideLigature)
                        subFlags |= QGlyphRun::SplitLigature;

                    glyphRuns.append(glyphRunWithInfo(multiFontEngine->engine(which),
                                                      subLayout,
                                                      pos,
                                                      subFlags,
                                                      x,
                                                      width,
                                                      glyphsStart + start,
                                                      glyphsStart + end,
                                                      logClusters,
                                                      iterator.itemStart,
                                                      iterator.itemLength));
                    for (int i = 0; i < subLayout.numGlyphs; ++i) {
                        QFixed justification = QFixed::fromFixed(subLayout.justifications[i].space_18d6);
                        pos.rx() += (subLayout.advances[i] + justification).toReal();
                    }

                    if (rtl)
                        end = start;
                    else
                        start = end;
                    which = e;
                }

                QGlyphLayout subLayout = glyphLayout.mid(start, end - start);
                multiFontEngine->ensureEngineAt(which);

                QGlyphRun::GlyphRunFlags subFlags = flags;
                if ((start == 0 && startsInsideLigature) || endsInsideLigature)
                    subFlags |= QGlyphRun::SplitLigature;

                QGlyphRun glyphRun = glyphRunWithInfo(multiFontEngine->engine(which),
                                                      subLayout,
                                                      pos,
                                                      subFlags,
                                                      x,
                                                      width,
                                                      glyphsStart + start,
                                                      glyphsStart + end,
                                                      logClusters,
                                                      iterator.itemStart,
                                                      iterator.itemLength);
                if (!glyphRun.isEmpty())
                    glyphRuns.append(glyphRun);
            } else {
                if (startsInsideLigature || endsInsideLigature)
                    flags |= QGlyphRun::SplitLigature;
                QGlyphRun glyphRun = glyphRunWithInfo(mainFontEngine,
                                                      glyphLayout,
                                                      pos,
                                                      flags,
                                                      x,
                                                      width,
                                                      glyphsStart,
                                                      glyphsEnd,
                                                      logClusters,
                                                      iterator.itemStart,
                                                      iterator.itemLength);
                if (!glyphRun.isEmpty())
                    glyphRuns.append(glyphRun);
            }
        }
    }

    return glyphRuns;
}
#endif // QT_NO_RAWFONT

/*!
    \fn void QTextLine::draw(QPainter *painter, const QPointF &position, const QTextLayout::FormatRange *selection) const

    Draws a line on the given \a painter at the specified \a position.
    The \a selection is reserved for internal use.
*/
void QTextLine::draw(QPainter *p, const QPointF &pos, const QTextLayout::FormatRange *selection) const
{
#ifndef QT_NO_RAWFONT
    // Not intended to work with rawfont
    Q_ASSERT(!eng->useRawFont);
#endif
    const QScriptLine &line = eng->lines[index];
    QPen pen = p->pen();

    bool noText = (selection && selection->format.property(SuppressText).toBool());

    if (!line.length) {
        if (selection
            && selection->start <= line.from
            && selection->start + selection->length > line.from) {

            const qreal lineHeight = line.height().toReal();
            QRectF r(pos.x() + line.x.toReal(), pos.y() + line.y.toReal(),
                     lineHeight / 2, QFontMetrics(eng->font()).width(QLatin1Char(' ')));
            setPenAndDrawBackground(p, QPen(), selection->format, r);
            p->setPen(pen);
        }
        return;
    }


    QTextLineItemIterator iterator(eng, index, pos, selection);
    QFixed lineBase = line.base();
    eng->clearDecorations();
    eng->enableDelayDecorations();

    const QFixed y = QFixed::fromReal(pos.y()) + line.y + lineBase;

    bool suppressColors = (eng->option.flags() & QTextOption::SuppressColors);
    while (!iterator.atEnd()) {
        QScriptItem &si = iterator.next();

        if (selection && selection->start >= 0 && iterator.isOutsideSelection())
            continue;

        if (si.analysis.flags == QScriptAnalysis::LineOrParagraphSeparator
            && !(eng->option.flags() & QTextOption::ShowLineAndParagraphSeparators))
            continue;

        QFixed itemBaseLine = y;
        QFont f = eng->font(si);
        QTextCharFormat format;

        if (eng->hasFormats() || selection) {
            format = eng->format(&si);
            if (suppressColors) {
                format.clearForeground();
                format.clearBackground();
                format.clearProperty(QTextFormat::TextUnderlineColor);
            }
            if (selection)
                format.merge(selection->format);

            setPenAndDrawBackground(p, pen, format, QRectF(iterator.x.toReal(), (y - lineBase).toReal(),
                                                           iterator.itemWidth.toReal(), line.height().toReal()));

            QTextCharFormat::VerticalAlignment valign = format.verticalAlignment();
            if (valign == QTextCharFormat::AlignSuperScript || valign == QTextCharFormat::AlignSubScript) {
                QFontEngine *fe = f.d->engineForScript(si.analysis.script);
                QFixed height = fe->ascent() + fe->descent();
                if (valign == QTextCharFormat::AlignSubScript)
                    itemBaseLine += height / 6;
                else if (valign == QTextCharFormat::AlignSuperScript)
                    itemBaseLine -= height / 2;
            }
        }

        if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {

            if (eng->hasFormats()) {
                p->save();
                if (si.analysis.flags == QScriptAnalysis::Object && eng->block.docHandle()) {
                    QFixed itemY = y - si.ascent;
                    if (format.verticalAlignment() == QTextCharFormat::AlignTop) {
                        itemY = y - lineBase;
                    }

                    QRectF itemRect(iterator.x.toReal(), itemY.toReal(), iterator.itemWidth.toReal(), si.height().toReal());

                    eng->docLayout()->drawInlineObject(p, itemRect,
                                                       QTextInlineObject(iterator.item, eng),
                                                       si.position + eng->block.position(),
                                                       format);
                    if (selection) {
                        QBrush bg = format.brushProperty(ObjectSelectionBrush);
                        if (bg.style() != Qt::NoBrush) {
                            QColor c = bg.color();
                            c.setAlpha(128);
                            p->fillRect(itemRect, c);
                        }
                    }
                } else { // si.isTab
                    QFont f = eng->font(si);
                    QTextItemInt gf(si, &f, format);
                    gf.chars = 0;
                    gf.num_chars = 0;
                    gf.width = iterator.itemWidth;
                    QPainterPrivate::get(p)->drawTextItem(QPointF(iterator.x.toReal(), y.toReal()), gf, eng);
                    if (eng->option.flags() & QTextOption::ShowTabsAndSpaces) {
                        QChar visualTab(0x2192);
                        int w = QFontMetrics(f).width(visualTab);
                        qreal x = iterator.itemWidth.toReal() - w; // Right-aligned
                        if (x < 0)
                             p->setClipRect(QRectF(iterator.x.toReal(), line.y.toReal(),
                                                   iterator.itemWidth.toReal(), line.height().toReal()),
                                            Qt::IntersectClip);
                        else
                             x /= 2; // Centered
                        p->drawText(QPointF(iterator.x.toReal() + x,
                                            y.toReal()), visualTab);
                    }

                }
                p->restore();
            }

            continue;
        }

        unsigned short *logClusters = eng->logClusters(&si);
        QGlyphLayout glyphs = eng->shapedGlyphs(&si);

        QTextItemInt gf(glyphs.mid(iterator.glyphsStart, iterator.glyphsEnd - iterator.glyphsStart),
                        &f, eng->layoutData->string.unicode() + iterator.itemStart,
                        iterator.itemEnd - iterator.itemStart, eng->fontEngine(si), format);
        gf.logClusters = logClusters + iterator.itemStart - si.position;
        gf.width = iterator.itemWidth;
        gf.justified = line.justified;
        gf.initWithScriptItem(si);

        Q_ASSERT(gf.fontEngine);

        QPointF pos(iterator.x.toReal(), itemBaseLine.toReal());
        if (format.penProperty(QTextFormat::TextOutline).style() != Qt::NoPen) {
            QPainterPath path;
            path.setFillRule(Qt::WindingFill);

            if (gf.glyphs.numGlyphs)
                gf.fontEngine->addOutlineToPath(pos.x(), pos.y(), gf.glyphs, &path, gf.flags);
            if (gf.flags) {
                const QFontEngine *fe = gf.fontEngine;
                const qreal lw = fe->lineThickness().toReal();
                if (gf.flags & QTextItem::Underline) {
                    qreal offs = fe->underlinePosition().toReal();
                    path.addRect(pos.x(), pos.y() + offs, gf.width.toReal(), lw);
                }
                if (gf.flags & QTextItem::Overline) {
                    qreal offs = fe->ascent().toReal() + 1;
                    path.addRect(pos.x(), pos.y() - offs, gf.width.toReal(), lw);
                }
                if (gf.flags & QTextItem::StrikeOut) {
                    qreal offs = fe->ascent().toReal() / 3;
                    path.addRect(pos.x(), pos.y() - offs, gf.width.toReal(), lw);
                }
            }

            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            //Currently QPen with a Qt::NoPen style still returns a default
            //QBrush which != Qt::NoBrush so we need this specialcase to reset it
            if (p->pen().style() == Qt::NoPen)
                p->setBrush(Qt::NoBrush);
            else
                p->setBrush(p->pen().brush());

            p->setPen(format.textOutline());
            p->drawPath(path);
            p->restore();
        } else {
            if (noText)
                gf.glyphs.numGlyphs = 0; // slightly less elegant than it should be
            QPainterPrivate::get(p)->drawTextItem(pos, gf, eng);
        }

        if (si.analysis.flags == QScriptAnalysis::Space
            && (eng->option.flags() & QTextOption::ShowTabsAndSpaces)) {
            QBrush c = format.foreground();
            if (c.style() != Qt::NoBrush)
                p->setPen(c.color());
            QChar visualSpace((ushort)0xb7);
            p->drawText(QPointF(iterator.x.toReal(), itemBaseLine.toReal()), visualSpace);
            p->setPen(pen);
        }
    }
    eng->drawDecorations(p);

    if (eng->hasFormats())
        p->setPen(pen);
}

/*!
    \fn int QTextLine::cursorToX(int cursorPos, Edge edge) const

    \overload
*/

/*!
    Converts the cursor position \a cursorPos to the corresponding x position
    inside the line, taking account of the \a edge.

    If \a cursorPos is not a valid cursor position, the nearest valid
    cursor position will be used instead, and cpos will be modified to
    point to this valid cursor position.

    \sa xToCursor()
*/
qreal QTextLine::cursorToX(int *cursorPos, Edge edge) const
{
    if (!eng->layoutData)
        eng->itemize();

    const QScriptLine &line = eng->lines[index];
    bool lastLine = index >= eng->lines.size() - 1;

    QFixed x = line.x;
    x += eng->alignLine(line) - eng->leadingSpaceWidth(line);

    if (!index && !eng->layoutData->items.size()) {
        *cursorPos = 0;
        return x.toReal();
    }

    int lineEnd = line.from + line.length + line.trailingSpaces;
    int pos = qBound(0, *cursorPos, lineEnd);
    int itm;
    const QCharAttributes *attributes = eng->attributes();
    if (!attributes) {
        *cursorPos = 0;
        return x.toReal();
    }
    while (pos < lineEnd && !attributes[pos].graphemeBoundary)
        pos++;
    if (pos == lineEnd) {
        // end of line ensure we have the last item on the line
        itm = eng->findItem(pos-1);
    }
    else
        itm = eng->findItem(pos);
    if (itm < 0) {
        *cursorPos = 0;
        return x.toReal();
    }
    eng->shapeLine(line);

    const QScriptItem *si = &eng->layoutData->items[itm];
    if (!si->num_glyphs)
        eng->shape(itm);
    pos -= si->position;

    QGlyphLayout glyphs = eng->shapedGlyphs(si);
    unsigned short *logClusters = eng->logClusters(si);
    Q_ASSERT(logClusters);

    int l = eng->length(itm);
    if (pos > l)
        pos = l;
    if (pos < 0)
        pos = 0;

    int glyph_pos = pos == l ? si->num_glyphs : logClusters[pos];
    if (edge == Trailing && glyph_pos < si->num_glyphs) {
        // trailing edge is leading edge of next cluster
        glyph_pos++;
        while (glyph_pos < si->num_glyphs && !glyphs.attributes[glyph_pos].clusterStart)
            glyph_pos++;
    }

    bool reverse = eng->layoutData->items[itm].analysis.bidiLevel % 2;


    // add the items left of the cursor

    int firstItem = eng->findItem(line.from);
    int lastItem = eng->findItem(lineEnd - 1);
    int nItems = (firstItem >= 0 && lastItem >= firstItem)? (lastItem-firstItem+1) : 0;

    QVarLengthArray<int> visualOrder(nItems);
    QVarLengthArray<uchar> levels(nItems);
    for (int i = 0; i < nItems; ++i)
        levels[i] = eng->layoutData->items[i+firstItem].analysis.bidiLevel;
    QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());

    for (int i = 0; i < nItems; ++i) {
        int item = visualOrder[i]+firstItem;
        if (item == itm)
            break;
        QScriptItem &si = eng->layoutData->items[item];
        if (!si.num_glyphs)
            eng->shape(item);

        if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
            x += si.width;
            continue;
        }
        int start = qMax(line.from, si.position);
        int end = qMin(lineEnd, si.position + eng->length(item));

        logClusters = eng->logClusters(&si);

        int gs = logClusters[start-si.position];
        int ge = (end == si.position + eng->length(item)) ? si.num_glyphs-1 : logClusters[end-si.position-1];

        QGlyphLayout glyphs = eng->shapedGlyphs(&si);

        while (gs <= ge) {
            x += glyphs.effectiveAdvance(gs);
            ++gs;
        }
    }

    logClusters = eng->logClusters(si);
    glyphs = eng->shapedGlyphs(si);
    if (si->analysis.flags >= QScriptAnalysis::TabOrObject) {
        if (pos == (reverse ? 0 : l))
            x += si->width;
    } else {
        bool rtl = eng->isRightToLeft();
        bool visual = eng->visualCursorMovement();
        int end = qMin(lineEnd, si->position + l) - si->position;
        if (reverse) {
            int glyph_end = end == l ? si->num_glyphs : logClusters[end];
            int glyph_start = glyph_pos;
            if (visual && !rtl && !(lastLine && itm == (visualOrder[nItems - 1] + firstItem)))
                glyph_start++;
            for (int i = glyph_end - 1; i >= glyph_start; i--)
                x += glyphs.effectiveAdvance(i);
        } else {
            int start = qMax(line.from - si->position, 0);
            int glyph_start = logClusters[start];
            int glyph_end = glyph_pos;
            if (!visual || !rtl || (lastLine && itm == visualOrder[0] + firstItem))
                glyph_end--;
            for (int i = glyph_start; i <= glyph_end; i++)
                x += glyphs.effectiveAdvance(i);
        }
        x += eng->offsetInLigature(si, pos, end, glyph_pos);
    }

    if (eng->option.wrapMode() != QTextOption::NoWrap && x > line.x + line.width)
        x = line.x + line.width;
    if (eng->option.wrapMode() != QTextOption::NoWrap && x < 0)
        x = 0;

    *cursorPos = pos + si->position;
    return x.toReal();
}

/*!
    \fn int QTextLine::xToCursor(qreal x, CursorPosition cpos) const

    Converts the x-coordinate \a x, to the nearest matching cursor
    position, depending on the cursor position type, \a cpos.
    Note that result cursor position includes possible preedit area text.

    \sa cursorToX()
*/
int QTextLine::xToCursor(qreal _x, CursorPosition cpos) const
{
    QFixed x = QFixed::fromReal(_x);
    const QScriptLine &line = eng->lines[index];
    bool lastLine = index >= eng->lines.size() - 1;
    int lineNum = index;

    if (!eng->layoutData)
        eng->itemize();

    int line_length = textLength();

    if (!line_length)
        return line.from;

    int firstItem = eng->findItem(line.from);
    int lastItem = eng->findItem(line.from + line_length - 1);
    int nItems = (firstItem >= 0 && lastItem >= firstItem)? (lastItem-firstItem+1) : 0;

    if (!nItems)
        return 0;

    x -= line.x;
    x -= eng->alignLine(line);
//     qDebug("xToCursor: x=%f, cpos=%d", x.toReal(), cpos);

    QVarLengthArray<int> visualOrder(nItems);
    QVarLengthArray<unsigned char> levels(nItems);
    for (int i = 0; i < nItems; ++i)
        levels[i] = eng->layoutData->items[i+firstItem].analysis.bidiLevel;
    QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());

    bool visual = eng->visualCursorMovement();
    if (x <= 0) {
        // left of first item
        int item = visualOrder[0]+firstItem;
        QScriptItem &si = eng->layoutData->items[item];
        if (!si.num_glyphs)
            eng->shape(item);
        int pos = si.position;
        if (si.analysis.bidiLevel % 2)
            pos += eng->length(item);
        pos = qMax(line.from, pos);
        pos = qMin(line.from + line_length, pos);
        return pos;
    } else if (x < line.textWidth
               || (line.justified && x < line.width)) {
        // has to be in one of the runs
        QFixed pos;
        bool rtl = eng->isRightToLeft();

        eng->shapeLine(line);
        QVector<int> insertionPoints;
        if (visual && rtl)
            eng->insertionPointsForLine(lineNum, insertionPoints);
        int nchars = 0;
        for (int i = 0; i < nItems; ++i) {
            int item = visualOrder[i]+firstItem;
            QScriptItem &si = eng->layoutData->items[item];
            int item_length = eng->length(item);
//             qDebug("    item %d, visual %d x_remain=%f", i, item, x.toReal());

            int start = qMax(line.from - si.position, 0);
            int end = qMin(line.from + line_length - si.position, item_length);

            unsigned short *logClusters = eng->logClusters(&si);

            int gs = logClusters[start];
            int ge = (end == item_length ? si.num_glyphs : logClusters[end]) - 1;
            QGlyphLayout glyphs = eng->shapedGlyphs(&si);

            QFixed item_width = 0;
            if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
                item_width = si.width;
            } else {
                int g = gs;
                while (g <= ge) {
                    item_width += glyphs.effectiveAdvance(g);
                    ++g;
                }
            }
//             qDebug("      start=%d, end=%d, gs=%d, ge=%d item_width=%f", start, end, gs, ge, item_width.toReal());

            if (pos + item_width < x) {
                pos += item_width;
                nchars += end;
                continue;
            }
//             qDebug("      inside run");
            if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
                if (cpos == QTextLine::CursorOnCharacter)
                    return si.position;
                bool left_half = (x - pos) < item_width/2;

                if (bool(si.analysis.bidiLevel % 2) != left_half)
                    return si.position;
                return si.position + 1;
            }

            int glyph_pos = -1;
            QFixed edge;
            // has to be inside run
            if (cpos == QTextLine::CursorOnCharacter) {
                if (si.analysis.bidiLevel % 2) {
                    pos += item_width;
                    glyph_pos = gs;
                    while (gs <= ge) {
                        if (glyphs.attributes[gs].clusterStart) {
                            if (pos < x)
                                break;
                            glyph_pos = gs;
                            edge = pos;
                        }
                        pos -= glyphs.effectiveAdvance(gs);
                        ++gs;
                    }
                } else {
                    glyph_pos = gs;
                    while (gs <= ge) {
                        if (glyphs.attributes[gs].clusterStart) {
                            if (pos > x)
                                break;
                            glyph_pos = gs;
                            edge = pos;
                        }
                        pos += glyphs.effectiveAdvance(gs);
                        ++gs;
                    }
                }
            } else {
                QFixed dist = INT_MAX/256;
                if (si.analysis.bidiLevel % 2) {
                    if (!visual || rtl || (lastLine && i == nItems - 1)) {
                        pos += item_width;
                        while (gs <= ge) {
                            if (glyphs.attributes[gs].clusterStart && qAbs(x-pos) < dist) {
                                glyph_pos = gs;
                                edge = pos;
                                dist = qAbs(x-pos);
                            }
                            pos -= glyphs.effectiveAdvance(gs);
                            ++gs;
                        }
                    } else {
                        while (ge >= gs) {
                            if (glyphs.attributes[ge].clusterStart && qAbs(x-pos) < dist) {
                                glyph_pos = ge;
                                edge = pos;
                                dist = qAbs(x-pos);
                            }
                            pos += glyphs.effectiveAdvance(ge);
                            --ge;
                        }
                    }
                } else {
                    if (!visual || !rtl || (lastLine && i == 0)) {
                        while (gs <= ge) {
                            if (glyphs.attributes[gs].clusterStart && qAbs(x-pos) < dist) {
                                glyph_pos = gs;
                                edge = pos;
                                dist = qAbs(x-pos);
                            }
                            pos += glyphs.effectiveAdvance(gs);
                            ++gs;
                        }
                    } else {
                        QFixed oldPos = pos;
                        while (gs <= ge) {
                            pos += glyphs.effectiveAdvance(gs);
                            if (glyphs.attributes[gs].clusterStart && qAbs(x-pos) < dist) {
                                glyph_pos = gs;
                                edge = pos;
                                dist = qAbs(x-pos);
                            }
                            ++gs;
                        }
                        pos = oldPos;
                    }
                }
                if (qAbs(x-pos) < dist) {
                    if (visual) {
                        if (!rtl && i < nItems - 1) {
                            nchars += end;
                            continue;
                        }
                        if (rtl && nchars > 0)
                            return insertionPoints[lastLine ? nchars : nchars - 1];
                    }
                    return eng->positionInLigature(&si, end, x, pos, -1,
                                                   cpos == QTextLine::CursorOnCharacter);
                }
            }
            Q_ASSERT(glyph_pos != -1);
            return eng->positionInLigature(&si, end, x, edge, glyph_pos,
                                           cpos == QTextLine::CursorOnCharacter);
        }
    }
    // right of last item
//     qDebug() << "right of last";
    int item = visualOrder[nItems-1]+firstItem;
    QScriptItem &si = eng->layoutData->items[item];
    if (!si.num_glyphs)
        eng->shape(item);
    int pos = si.position;
    if (!(si.analysis.bidiLevel % 2))
        pos += eng->length(item);
    pos = qMax(line.from, pos);

    int maxPos = line.from + line_length;

    // except for the last line we assume that the
    // character between lines is a space and we want
    // to position the cursor to the left of that
    // character.
    // ###### breaks with japanese for example
    if (this->index < eng->lines.count() - 1)
        --maxPos;

    pos = qMin(pos, maxPos);
    return pos;
}

QT_END_NAMESPACE
