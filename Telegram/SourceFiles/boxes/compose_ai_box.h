/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/object_ptr.h"
#include "ui/text/text_entity.h"

namespace Iv {
struct RichPage;
} // namespace Iv

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ChatStyle;
class GenericBox;
class RpWidget;
class Show;
} // namespace Ui

namespace HistoryView::Controls {

struct ComposeAiBoxArgs {
	not_null<Main::Session*> session;
	TextWithEntities text;
	std::shared_ptr<Ui::ChatStyle> chatStyle;
	Fn<void(TextWithEntities)> apply;
	Fn<void(TextWithEntities, Api::SendOptions, Fn<void()>)> send;
	Fn<void(not_null<Ui::RpWidget*>, Fn<void(Api::SendOptions)>)> setupMenu;
	std::shared_ptr<const Iv::RichPage> richSource;
	Fn<void(std::shared_ptr<const Iv::RichPage>)> applyRich;
};

void ComposeAiBox(not_null<Ui::GenericBox*> box, ComposeAiBoxArgs &&args);
void ShowComposeAiBox(std::shared_ptr<Ui::Show> show, ComposeAiBoxArgs &&args);

} // namespace HistoryView::Controls
