/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/editor_message_source.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Editor {

struct LinkBoxResult {
	std::shared_ptr<MessageSource> message;
	std::optional<LinkPreview> pill;
	bool dark = false;
};

struct LinkBoxArgs {
	std::shared_ptr<ChatHelpers::Show> show;
	QString url;
	std::optional<LinkPreview> editing;
	Fn<void(LinkBoxResult)> done;
};

[[nodiscard]] object_ptr<Ui::BoxContent> LinkBox(LinkBoxArgs &&args);

} // namespace Editor
