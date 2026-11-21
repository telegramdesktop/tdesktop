/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

class DocumentData;
class QPainter;

namespace Data {
class ChatFilter;
struct ChatFilterTitle;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {

class RpWidget;

struct FilterTitleIcon {
	DocumentData *document = nullptr;
	int prefixLength = 0;
};

[[nodiscard]] FilterTitleIcon ResolveFilterTitleIcon(
	not_null<Main::Session*> session,
	const Data::ChatFilter &filter,
	const Data::ChatFilterTitle &title,
	not_null<RpWidget*> widget,
	rpl::lifetime &lifetime,
	Fn<void()> refresh);
[[nodiscard]] TextWithEntities FilterTitleWithoutIcon(
	TextWithEntities title,
	FilterTitleIcon icon);

using FilterEmojiIconPaint = Fn<bool(QPainter &, QRect, QColor)>;
[[nodiscard]] FilterEmojiIconPaint MakeFilterEmojiIcon(
	not_null<RpWidget*> widget,
	FilterTitleIcon icon,
	bool isStatic,
	Fn<bool()> paused = nullptr);

} // namespace Ui
