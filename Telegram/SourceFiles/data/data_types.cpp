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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
