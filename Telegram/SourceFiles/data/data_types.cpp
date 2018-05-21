/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_types.h"

#include "data/data_document.h"
#include "ui/widgets/input_fields.h"

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

void MessageCursor::fillFrom(not_null<const Ui::InputField*> field) {
	const auto cursor = field->textCursor();
	position = cursor.position();
	anchor = cursor.anchor();
	const auto top = field->scrollTop().current();
	scroll = (top != field->scrollTopMax()) ? top : QFIXED_MAX;
}

void MessageCursor::applyTo(not_null<Ui::InputField*> field) {
	auto cursor = field->textCursor();
	cursor.setPosition(anchor, QTextCursor::MoveAnchor);
	cursor.setPosition(position, QTextCursor::KeepAnchor);
	field->setTextCursor(cursor);
	field->scrollTo(scroll);
}

HistoryItem *FileClickHandler::getActionItem() const {
	return context()
		? App::histItemById(context())
		: nullptr;
}
