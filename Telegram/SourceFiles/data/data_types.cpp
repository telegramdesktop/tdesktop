/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_types.h"

#include "data/data_document.h"

void AudioMsgId::setTypeFromAudio() {
	if (_audio->isVoiceMessage() || _audio->isVideoMessage()) {
		_type = Type::Voice;
	} else if (_audio->isVideoFile()) {
		_type = Type::Video;
	} else if (_audio->isAudioFile()) {
		_type = Type::Song;
	} else {
		_type = Type::Unknown;
	}
}

void MessageCursor::fillFrom(const QTextEdit *edit) {
	QTextCursor c = edit->textCursor();
	position = c.position();
	anchor = c.anchor();
	QScrollBar *s = edit->verticalScrollBar();
	scroll = (s && (s->value() != s->maximum()))
		? s->value()
		: QFIXED_MAX;
}

void MessageCursor::applyTo(QTextEdit *edit) {
	auto cursor = edit->textCursor();
	cursor.setPosition(anchor, QTextCursor::MoveAnchor);
	cursor.setPosition(position, QTextCursor::KeepAnchor);
	edit->setTextCursor(cursor);
	if (auto scrollbar = edit->verticalScrollBar()) {
		scrollbar->setValue(scroll);
	}
}

HistoryItem *FileClickHandler::getActionItem() const {
	return context()
		? App::histItemById(context())
		: App::hoveredLinkItem()
		? App::hoveredLinkItem()
		: App::contextItem()
		? App::contextItem()
		: nullptr;
}
