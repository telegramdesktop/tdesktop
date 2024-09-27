/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"
#include "ui/text/text_variant.h"

namespace Ui {

struct ConfirmBoxArgs {
	using Callback = std::variant<
		v::null_t,
		Fn<void()>,
		Fn<void(Fn<void()>)>>;

	v::text::data text = v::null;
	Callback confirmed = v::null;
	Callback cancelled = v::null;

	v::text::data confirmText;
	v::text::data cancelText;

	const style::RoundButton *confirmStyle = nullptr;
	const style::RoundButton *cancelStyle = nullptr;

	const style::FlatLabel *labelStyle = nullptr;
	Fn<bool(const ClickHandlerPtr&, Qt::MouseButton)> labelFilter;
	std::optional<QMargins> labelPadding;

	v::text::data title = v::null;

	bool inform = false;
	// If strict cancel is set the cancel.callback() is only called
	// if the cancel button was pressed.
	bool strictCancel = false;
};

void ConfirmBox(not_null<GenericBox*> box, ConfirmBoxArgs &&args);

inline void InformBox(not_null<GenericBox*> box, ConfirmBoxArgs &&args) {
	args.inform = true;
	ConfirmBox(box, std::move(args));
}

[[nodiscard]] object_ptr<GenericBox> MakeConfirmBox(ConfirmBoxArgs &&args);

[[nodiscard]] inline object_ptr<GenericBox> MakeInformBox(
		ConfirmBoxArgs &&args) {
	args.inform = true;
	return MakeConfirmBox(std::move(args));
}

[[nodiscard]] inline object_ptr<GenericBox> MakeInformBox(
		v::text::data text) {
	return MakeInformBox({ .text = std::move(text) });
}

void IconWithTitle(
	not_null<VerticalLayout*> container,
	not_null<RpWidget*> icon,
	not_null<RpWidget*> title,
	RpWidget *subtitle = nullptr);

} // namespace Ui
