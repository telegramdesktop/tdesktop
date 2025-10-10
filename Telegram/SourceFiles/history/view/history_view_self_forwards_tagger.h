/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class History;

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Data {
class Session;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace Reactions {
struct ChosenReaction;
} // namespace Reactions

namespace HistoryView {

class SelfForwardsTagger final : public base::has_weak_ptr {
public:
	SelfForwardsTagger(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		Fn<Ui::RpWidget*()> listWidget,
		not_null<QWidget*> scroll,
		Fn<History*()> history);

	~SelfForwardsTagger();

private:
	void setup();
	void showSelectorForMessages(const MessageIdsList &ids);
	void showToast(const TextWithEntities &text, Fn<void()> callback);
	void showTaggedToast(DocumentId);
	void createLottieIcon(not_null<QWidget*> widget, const QString &name);
	void hideToast();
	[[nodiscard]] QRect toastGeometry() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::RpWidget*> _parent;
	const Fn<Ui::RpWidget*()> _listWidget;
	const not_null<QWidget*> _scroll;
	const Fn<History*()> _history;

	base::weak_ptr<Ui::Toast::Instance> _toast;
	rpl::lifetime _lifetime;

};

} // namespace HistoryView
