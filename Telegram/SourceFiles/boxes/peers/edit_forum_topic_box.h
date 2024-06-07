/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

class History;

namespace Data {
struct TopicIconDescriptor;
enum class CustomEmojiSizeTag : uchar;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

void NewForumTopicBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<History*> forum);

void EditForumTopicBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<History*> forum,
	MsgId rootId);

[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> MakeTopicIconEmoji(
	Data::TopicIconDescriptor descriptor,
	Fn<void()> repaint,
	Data::CustomEmojiSizeTag tag);
