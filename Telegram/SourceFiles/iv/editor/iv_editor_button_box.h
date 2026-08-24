/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item_reply_markup.h"
#include "ui/text/text_entity.h"

#include <memory>
#include <optional>

namespace Main {
class SessionShow;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Iv::Editor {

struct RichButtonEditData {
	TextWithEntities label;
	QByteArray payload;
	HistoryMessageMarkupButton::Type type
		= HistoryMessageMarkupButton::Type::Url;
	HistoryMessageMarkupButton::Color color
		= HistoryMessageMarkupButton::Color::Normal;
};

struct RichButtonEditBoxArgs {
	RichButtonEditData data;
	Fn<QString(QString)> validateUrl;
	std::optional<bool> separateLine;
	bool editingExisting = false;
};

struct RichButtonEditResult {
	RichButtonEditData data;
	bool separateLine = false;
};

void EditRichButtonBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	RichButtonEditBoxArgs args,
	Fn<void(RichButtonEditResult)> callback,
	Fn<void(bool)> setExternalInteractionActive,
	Fn<void()> restoreFocus);

} // namespace Iv::Editor
