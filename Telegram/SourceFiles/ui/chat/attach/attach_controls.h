/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

namespace Ui {

class AttachControls final {
public:
	enum class Type {
		Full,
		EditOnly,
		None,
	};

	AttachControls();

	void paint(Painter &p, int x, int y);
	void setType(Type type);

	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] Type type() const;

private:
	RoundRect _rect;
	Type _type = Type::Full;

};

class AttachControlsWidget final : public RpWidget {
public:
	AttachControlsWidget(
		not_null<RpWidget*> parent,
		AttachControls::Type type = AttachControls::Type::Full);

	[[nodiscard]] rpl::producer<> editRequests() const;
	[[nodiscard]] rpl::producer<> deleteRequests() const;

private:
	const base::unique_qptr<AbstractButton> _edit;
	const base::unique_qptr<AbstractButton> _delete;
	AttachControls _controls;

};

} // namespace Ui
