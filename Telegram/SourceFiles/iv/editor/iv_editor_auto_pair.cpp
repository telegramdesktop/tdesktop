/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_auto_pair.h"

#include "ui/widgets/fields/input_field.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QTextCursor>

namespace Iv::Editor {
namespace {

[[nodiscard]] QChar Closing(QChar opening) {
	switch (opening.unicode()) {
	case '(': return QChar(')');
	case '[': return QChar(']');
	case '{': return QChar('}');
	case '"': return QChar('"');
	}
	return QChar();
}

[[nodiscard]] bool IsClosing(QChar ch) {
	switch (ch.unicode()) {
	case ')':
	case ']':
	case '}':
	case '"': return true;
	}
	return false;
}

[[nodiscard]] QChar CharBefore(const QTextCursor &cursor) {
	auto check = cursor;
	check.setPosition(cursor.selectionStart());
	if (!check.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor)) {
		return QChar();
	}
	const auto text = check.selectedText();
	return text.isEmpty() ? QChar() : text[text.size() - 1];
}

[[nodiscard]] QChar CharAfter(const QTextCursor &cursor) {
	auto check = cursor;
	check.setPosition(cursor.selectionEnd());
	if (!check.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor)) {
		return QChar();
	}
	const auto text = check.selectedText();
	return text.isEmpty() ? QChar() : text[0];
}

[[nodiscard]] bool AllowedBefore(QChar next) {
	return next.isNull()
		|| next.isSpace()
		|| IsClosing(next)
		|| u".,;:!?"_q.contains(next);
}

[[nodiscard]] bool AllowedAfter(QChar typed, QChar previous) {
	if (typed == QChar('"')) {
		return !previous.isLetterOrNumber();
	} else if (previous.isNull()
		|| previous.isSpace()
		|| previous.isLetterOrNumber()) {
		return true;
	}
	// Smileys like ":-(" become emoji only if the field gets the key.
	return !Closing(previous).isNull() || IsClosing(previous);
}

bool RemovePair(not_null<Ui::InputField*> field) {
	auto edit = field->textCursor();
	edit.beginEditBlock();
	edit.deletePreviousChar();
	edit.deleteChar();
	edit.endEditBlock();
	field->setTextCursor(edit);
	return true;
}

bool SkipClosing(not_null<Ui::InputField*> field) {
	auto skip = field->textCursor();
	skip.movePosition(QTextCursor::Right);
	field->setTextCursor(skip);
	return true;
}

bool WrapSelection(
		not_null<Ui::InputField*> field,
		QChar opening,
		QChar closing) {
	const auto cursor = field->textCursor();
	const auto from = cursor.selectionStart();
	const auto till = cursor.selectionEnd();
	auto edit = field->textCursor();
	edit.beginEditBlock();
	edit.setPosition(till);
	edit.insertText(QString(closing));
	edit.setPosition(from);
	edit.insertText(QString(opening));
	edit.endEditBlock();
	auto wrapped = field->textCursor();
	wrapped.setPosition(from + 1);
	wrapped.setPosition(till + 1, QTextCursor::KeepAnchor);
	field->setTextCursor(wrapped);
	return true;
}

bool InsertPair(
		not_null<Ui::InputField*> field,
		QChar opening,
		QChar closing) {
	auto edit = field->textCursor();
	edit.beginEditBlock();
	edit.insertText(QString(opening) + closing);
	edit.endEditBlock();
	edit.setPosition(edit.position() - 1);
	field->setTextCursor(edit);
	return true;
}

} // namespace

bool HandleAutoPairKey(
		not_null<Ui::InputField*> field,
		not_null<QKeyEvent*> e) {
	const auto modifiers = e->modifiers();
	if ((modifiers & (Qt::ControlModifier | Qt::MetaModifier))
		&& !(modifiers & Qt::AltModifier)) {
		return false;
	}
	const auto cursor = field->textCursor();
	const auto previous = CharBefore(cursor);
	const auto next = CharAfter(cursor);
	if (e->key() == Qt::Key_Backspace) {
		const auto closing = Closing(previous);
		return !cursor.hasSelection()
			&& !closing.isNull()
			&& (closing == next)
			&& RemovePair(field);
	}
	const auto text = e->text();
	if (text.size() != 1) {
		return false;
	}
	const auto typed = text[0];
	if (!cursor.hasSelection()
		&& (next == typed)
		&& IsClosing(typed)) {
		return SkipClosing(field);
	}
	const auto closing = Closing(typed);
	if (closing.isNull()) {
		return false;
	} else if (cursor.hasSelection()) {
		return WrapSelection(field, typed, closing);
	}
	return AllowedAfter(typed, previous)
		&& AllowedBefore(next)
		&& InsertPair(field, typed, closing);
}

} // namespace Iv::Editor
