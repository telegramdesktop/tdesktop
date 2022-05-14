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

	bool inform = false;
	// If strict cancel is set the cancel.callback() is only called
	// if the cancel button was pressed.
	bool strictCancel = false;
};

void ConfirmBox(not_null<Ui::GenericBox*> box, ConfirmBoxArgs &&args);

[[nodiscard]] object_ptr<Ui::GenericBox> MakeConfirmBox(
	ConfirmBoxArgs &&args);
[[nodiscard]] object_ptr<Ui::GenericBox> MakeInformBox(v::text::data text);

} // namespace Ui
