/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "payments/ui/payments_panel_data.h"
#include "base/object_ptr.h"

namespace Ui {
class ScrollArea;
class FadeShadow;
class RoundButton;
class Checkbox;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;
class Field;

class EditCard final : public RpWidget {
public:
	EditCard(
		QWidget *parent,
		const NativeMethodDetails &native,
		CardField field,
		not_null<PanelDelegate*> delegate);

	void setFocus(CardField field);
	void setFocusFast(CardField field);
	void showError(CardField field);

private:
	void resizeEvent(QResizeEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;

	void setupControls();
	[[nodiscard]] not_null<Ui::RpWidget*> setupContent();
	void updateControlsGeometry();
	[[nodiscard]] Field *lookupField(CardField field) const;

	[[nodiscard]] UncheckedCardDetails collect() const;

	const not_null<PanelDelegate*> _delegate;
	NativeMethodDetails _native;

	object_ptr<ScrollArea> _scroll;
	object_ptr<FadeShadow> _topShadow;
	object_ptr<FadeShadow> _bottomShadow;
	object_ptr<RoundButton> _submit;
	object_ptr<RoundButton> _cancel;

	std::unique_ptr<Field> _number;
	std::unique_ptr<Field> _cvc;
	std::unique_ptr<Field> _expire;
	std::unique_ptr<Field> _name;
	std::unique_ptr<Field> _country;
	std::unique_ptr<Field> _zip;
	Checkbox *_save = nullptr;

	CardField _focusField = CardField::Number;

};

} // namespace Payments::Ui
