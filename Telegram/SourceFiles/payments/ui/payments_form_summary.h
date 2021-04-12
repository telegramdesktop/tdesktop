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
#include "styles/style_widgets.h"

namespace Ui {
class ScrollArea;
class FadeShadow;
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;

class FormSummary final : public RpWidget {
public:
	FormSummary(
		QWidget *parent,
		const Invoice &invoice,
		const RequestedInformation &current,
		const PaymentMethodDetails &method,
		const ShippingOptions &options,
		not_null<PanelDelegate*> delegate,
		int scrollTop);

	void updateThumbnail(const QImage &thumbnail);
	[[nodiscard]] rpl::producer<int> scrollTopValue() const;

	bool showCriticalError(const TextWithEntities &text);
	[[nodiscard]] int contentHeight() const;

private:
	void resizeEvent(QResizeEvent *e) override;

	void setupControls();
	void setupContent(not_null<VerticalLayout*> layout);
	void setupCover(not_null<VerticalLayout*> layout);
	void setupPrices(not_null<VerticalLayout*> layout);
	void setupSuggestedTips(not_null<VerticalLayout*> layout);
	void setupSections(not_null<VerticalLayout*> layout);
	void updateControlsGeometry();

	[[nodiscard]] QString formatAmount(
		int64 amount,
		bool forceStripDotZero = false) const;
	[[nodiscard]] int64 computeTotalAmount() const;

	const not_null<PanelDelegate*> _delegate;
	Invoice _invoice;
	PaymentMethodDetails _method;
	ShippingOptions _options;
	RequestedInformation _information;
	object_ptr<ScrollArea> _scroll;
	not_null<VerticalLayout*> _layout;
	object_ptr<FadeShadow> _topShadow;
	object_ptr<FadeShadow> _bottomShadow;
	object_ptr<RoundButton> _submit;
	object_ptr<RoundButton> _cancel;
	rpl::event_stream<QImage> _thumbnails;

	style::complex_color _tipLightBg;
	style::complex_color _tipLightRipple;
	style::complex_color _tipChosenBg;
	style::complex_color _tipChosenRipple;
	style::RoundButton _tipButton;
	style::RoundButton _tipChosen;
	int _initialScrollTop = 0;

};

} // namespace Payments::Ui
