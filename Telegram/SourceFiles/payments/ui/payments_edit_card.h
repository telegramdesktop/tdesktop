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
} // namespace Ui

namespace Passport::Ui {
class PanelDetailsRow;
} // namespace Passport::Ui

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;

class EditCard final : public RpWidget {
public:
	EditCard(
		QWidget *parent,
		const NativePaymentDetails &native,
		CardField field,
		not_null<PanelDelegate*> delegate);

	void showError(CardField field);
	void setFocus(CardField field);

private:
	using Row = Passport::Ui::PanelDetailsRow;

	void resizeEvent(QResizeEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;

	void setupControls();
	[[nodiscard]] not_null<Ui::RpWidget*> setupContent();
	void updateControlsGeometry();
	[[nodiscard]] Row *controlForField(CardField field) const;

	[[nodiscard]] UncheckedCardDetails collect() const;

	const not_null<PanelDelegate*> _delegate;
	NativePaymentDetails _native;

	object_ptr<ScrollArea> _scroll;
	object_ptr<FadeShadow> _topShadow;
	object_ptr<FadeShadow> _bottomShadow;
	object_ptr<RoundButton> _done;

	Row *_number = nullptr;
	Row *_cvc = nullptr;
	Row *_expire = nullptr;
	Row *_name = nullptr;
	Row *_country = nullptr;
	Row *_zip = nullptr;

	CardField _focusField = CardField::Number;

};

} // namespace Payments::Ui
