/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel.h"

#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_form.h"
#include "passport/passport_panel_password.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "lang/lang_keys.h"
#include "styles/style_passport.h"
#include "styles/style_widgets.h"
#include "styles/style_calls.h"

namespace Passport {

Panel::Panel(not_null<PanelController*> controller)
: _controller(controller)
, _widget(std::make_unique<Ui::SeparatePanel>()) {
	_widget->setTitle(tr::lng_passport_title());
	_widget->setInnerSize(st::passportPanelSize);

	_widget->closeRequests(
	) | rpl::start_with_next([=] {
		_controller->cancelAuth();
	}, _widget->lifetime());

	_widget->closeEvents(
	) | rpl::start_with_next([=] {
		_controller->cancelAuthSure();
	}, _widget->lifetime());
}

rpl::producer<> Panel::backRequests() const {
	return _widget->backRequests();
}

void Panel::setBackAllowed(bool allowed) {
	_widget->setBackAllowed(allowed);
}

not_null<Ui::RpWidget*> Panel::widget() const {
	return _widget.get();
}

int Panel::hideAndDestroyGetDuration() {
	return _widget->hideGetDuration();
}

void Panel::showAskPassword() {
	_widget->showInner(
		base::make_unique_q<PanelAskPassword>(_widget.get(), _controller));
	setBackAllowed(false);
}

void Panel::showNoPassword() {
	_widget->showInner(
		base::make_unique_q<PanelNoPassword>(_widget.get(), _controller));
	setBackAllowed(false);
}

void Panel::showCriticalError(const QString &error) {
	auto container = base::make_unique_q<Ui::PaddingWrap<Ui::FlatLabel>>(
		_widget.get(),
		object_ptr<Ui::FlatLabel>(
			_widget.get(),
			error,
			st::passportErrorLabel),
		style::margins(0, st::passportPanelSize.height() / 3, 0, 0));
	container->widthValue(
	) | rpl::start_with_next([label = container->entity()](int width) {
		label->resize(width, label->height());
	}, container->lifetime());

	_widget->showInner(std::move(container));
	setBackAllowed(false);
}

void Panel::showForm() {
	_widget->showInner(
		base::make_unique_q<PanelForm>(_widget.get(), _controller));
	setBackAllowed(false);
}

void Panel::showEditValue(object_ptr<Ui::RpWidget> from) {
	_widget->showInner(base::unique_qptr<Ui::RpWidget>(from.data()));
}

void Panel::showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated) {
	_widget->showBox(std::move(box), options, animated);
	_widget->showAndActivate();
}

void Panel::showToast(const QString &text) {
	_widget->showToast({ text });
}

Panel::~Panel() = default;

} // namespace Passport
