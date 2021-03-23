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

class EditInformation final : public RpWidget {
public:
	EditInformation(
		QWidget *parent,
		const Invoice &invoice,
		const RequestedInformation &current,
		EditField field,
		not_null<PanelDelegate*> delegate);

private:
	using Row = Passport::Ui::PanelDetailsRow;

	void resizeEvent(QResizeEvent *e) override;

	void setupControls();
	[[nodiscard]] not_null<Ui::RpWidget*> setupContent();
	void updateControlsGeometry();

	[[nodiscard]] RequestedInformation collect() const;

	const not_null<PanelDelegate*> _delegate;
	Invoice _invoice;
	RequestedInformation _information;

	object_ptr<ScrollArea> _scroll;
	object_ptr<FadeShadow> _topShadow;
	object_ptr<FadeShadow> _bottomShadow;
	object_ptr<RoundButton> _done;

	Row *_street1 = nullptr;
	Row *_street2 = nullptr;
	Row *_city = nullptr;
	Row *_state = nullptr;
	Row *_country = nullptr;
	Row *_postcode = nullptr;
	Row *_name = nullptr;
	Row *_email = nullptr;
	Row *_phone = nullptr;

};

} // namespace Payments::Ui
