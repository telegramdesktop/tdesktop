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
#include "chat_helpers/message_field.h"

#include "history/history_widget.h"
#include "base/qthelp_regex.h"
#include "styles/style_history.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "auth_session.h"

namespace {

// For mention tags save and validate userId, ignore tags for different userId.
class FieldTagMimeProcessor : public Ui::FlatTextarea::TagMimeProcessor {
public:
	QString mimeTagFromTag(const QString &tagId) override {
		return ConvertTagToMimeTag(tagId);
	}

	QString tagFromMimeTag(const QString &mimeTag) override {
		if (mimeTag.startsWith(qstr("mention://"))) {
			auto match = QRegularExpression(":(\\d+)$").match(mimeTag);
			if (!match.hasMatch() || match.capturedRef(1).toInt() != Auth().userId()) {
				return QString();
			}
			return mimeTag.mid(0, mimeTag.size() - match.capturedLength());
		}
		return mimeTag;
	}

};

} // namespace

QString ConvertTagToMimeTag(const QString &tagId) {
	if (tagId.startsWith(qstr("mention://"))) {
		return tagId + ':' + QString::number(Auth().userId());
	}
	return tagId;
}

EntitiesInText ConvertTextTagsToEntities(const TextWithTags::Tags &tags) {
	EntitiesInText result;
	if (tags.isEmpty()) {
		return result;
	}

	result.reserve(tags.size());
	auto mentionStart = qstr("mention://user.");
	for_const (auto &tag, tags) {
		if (tag.id.startsWith(mentionStart)) {
			if (auto match = qthelp::regex_match("^(\\d+\\.\\d+)(/|$)", tag.id.midRef(mentionStart.size()))) {
				result.push_back(EntityInText(EntityInTextMentionName, tag.offset, tag.length, match->captured(1)));
			}
		}
	}
	return result;
}

TextWithTags::Tags ConvertEntitiesToTextTags(const EntitiesInText &entities) {
	TextWithTags::Tags result;
	if (entities.isEmpty()) {
		return result;
	}

	result.reserve(entities.size());
	for_const (auto &entity, entities) {
		if (entity.type() == EntityInTextMentionName) {
			auto match = QRegularExpression("^(\\d+\\.\\d+)$").match(entity.data());
			if (match.hasMatch()) {
				result.push_back({ entity.offset(), entity.length(), qstr("mention://user.") + entity.data() });
			}
		}
	}
	return result;
}

std::unique_ptr<QMimeData> MimeDataFromTextWithEntities(const TextWithEntities &forClipboard) {
	if (forClipboard.text.isEmpty()) {
		return nullptr;
	}

	auto result = std::make_unique<QMimeData>();
	result->setText(forClipboard.text);
	auto tags = ConvertEntitiesToTextTags(forClipboard.entities);
	if (!tags.isEmpty()) {
		for (auto &tag : tags) {
			tag.id = ConvertTagToMimeTag(tag.id);
		}
		result->setData(Ui::FlatTextarea::tagsMimeType(), Ui::FlatTextarea::serializeTagsList(tags));
	}
	return result;
}

MessageField::MessageField(QWidget *parent, not_null<Window::Controller*> controller, const style::FlatTextarea &st, base::lambda<QString()> placeholderFactory, const QString &val) : Ui::FlatTextarea(parent, st, std::move(placeholderFactory), val)
, _controller(controller) {
	setMinHeight(st::historySendSize.height() - 2 * st::historySendPadding);
	setMaxHeight(st::historyComposeFieldMaxHeight);

	setTagMimeProcessor(std::make_unique<FieldTagMimeProcessor>());
}

bool MessageField::hasSendText() const {
	auto &text = getTextWithTags().text;
	for (auto *ch = text.constData(), *e = ch + text.size(); ch != e; ++ch) {
		auto code = ch->unicode();
		if (code != ' ' && code != '\n' && code != '\r' && !chReplacedBySpace(code)) {
			return true;
		}
	}
	return false;
}

void MessageField::onEmojiInsert(EmojiPtr emoji) {
	if (isHidden()) return;
	insertEmoji(emoji, textCursor());
}

void MessageField::dropEvent(QDropEvent *e) {
	FlatTextarea::dropEvent(e);
	if (e->isAccepted()) {
		_controller->window()->activateWindow();
	}
}

bool MessageField::canInsertFromMimeData(const QMimeData *source) const {
	if (source->hasUrls()) {
		int32 files = 0;
		for (int32 i = 0; i < source->urls().size(); ++i) {
			if (source->urls().at(i).isLocalFile()) {
				++files;
			}
		}
		if (files > 1) return false; // multiple confirm with "compressed" checkbox
	}
	if (source->hasImage()) return true;
	return FlatTextarea::canInsertFromMimeData(source);
}

void MessageField::insertFromMimeData(const QMimeData *source) {
	if (_insertFromMimeDataHook && _insertFromMimeDataHook(source)) {
		return;
	}
	FlatTextarea::insertFromMimeData(source);
}

void MessageField::focusInEvent(QFocusEvent *e) {
	FlatTextarea::focusInEvent(e);
	emit focused();
}
