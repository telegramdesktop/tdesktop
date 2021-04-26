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
class InputField;
class MaskedInputField;
class Checkbox;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;
class Field;

class EditInformation final : public RpWidget {
public:
	EditInformation(
		QWidget *parent,
		const Invoice &invoice,
		const RequestedInformation &current,
		InformationField field,
		not_null<PanelDelegate*> delegate);
	~EditInformation();

	void setFocus(InformationField field);
	void setFocusFast(InformationField field);
	void showError(InformationField field);

private:
	void resizeEvent(QResizeEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;

	void setupControls();
	[[nodiscard]] not_null<Ui::RpWidget*> setupContent();
	void updateControlsGeometry();
	[[nodiscard]] Field *lookupField(InformationField field) const;

	[[nodiscard]] RequestedInformation collect() const;

	const not_null<PanelDelegate*> _delegate;
	Invoice _invoice;
	RequestedInformation _information;

	object_ptr<ScrollArea> _scroll;
	object_ptr<FadeShadow> _topShadow;
	object_ptr<FadeShadow> _bottomShadow;
	object_ptr<RoundButton> _submit;
	object_ptr<RoundButton> _cancel;

	std::unique_ptr<Field> _street1;
	std::unique_ptr<Field> _street2;
	std::unique_ptr<Field> _city;
	std::unique_ptr<Field> _state;
	std::unique_ptr<Field> _country;
	std::unique_ptr<Field> _postcode;
	std::unique_ptr<Field> _name;
	std::unique_ptr<Field> _email;
	std::unique_ptr<Field> _phone;
	Checkbox *_save = nullptr;

	InformationField _focusField = InformationField::ShippingStreet;

};

} // namespace Payments::Ui
