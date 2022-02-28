/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/confirm_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Ui {

void ConfirmBox(not_null<Ui::GenericBox*> box, ConfirmBoxArgs &&args) {
	const auto weak = Ui::MakeWeak(box);
	const auto lifetime = box->lifetime().make_state<rpl::lifetime>();

	const auto label = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			v::text::take_marked(std::move(args.text)),
			args.labelStyle ? *args.labelStyle : st::boxLabel),
		st::boxPadding);

	const auto prepareCallback = [&](ConfirmBoxArgs::Callback &callback) {
		return [=, confirmed = std::move(callback)]() {
			if (const auto callbackPtr = std::get_if<1>(&confirmed)) {
				if (auto callback = (*callbackPtr)) {
					callback();
				}
			} else if (const auto callbackPtr = std::get_if<2>(&confirmed)) {
				if (auto callback = (*callbackPtr)) {
					callback(crl::guard(weak, [=] { weak->closeBox(); }));
				}
			} else if (weak) {
				weak->closeBox();
			}
		};
	};

	const auto &defaultButtonStyle = box->getDelegate()->style().button;

	box->addButton(
		v::text::take_plain(std::move(args.confirmText), tr::lng_box_ok()),
		[=, c = prepareCallback(args.confirmed)]() {
			lifetime->destroy();
			c();
		},
		args.confirmStyle ? *args.confirmStyle : defaultButtonStyle);

	if (!args.inform) {
		const auto cancelButton = box->addButton(
			v::text::take_plain(std::move(args.cancelText), tr::lng_cancel()),
			crl::guard(weak, [=, c = prepareCallback(args.cancelled)]() {
				lifetime->destroy();
				c();
			}),
			args.cancelStyle ? *args.cancelStyle : defaultButtonStyle);

		box->boxClosing(
		) | rpl::start_with_next(crl::guard(cancelButton, [=] {
			cancelButton->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
		}), *lifetime);
	}

	if (args.labelFilter) {
		label->setClickHandlerFilter(std::move(args.labelFilter));
	}
	if (args.strictCancel) {
		lifetime->destroy();
	}
}

object_ptr<Ui::GenericBox> MakeConfirmBox(ConfirmBoxArgs &&args) {
	return Box(ConfirmBox, std::move(args));
}

object_ptr<Ui::GenericBox> MakeInformBox(v::text::data text) {
	return MakeConfirmBox({
		.text = std::move(text),
		.inform = true,
	});
}

ConfirmDontWarnBox::ConfirmDontWarnBox(
	QWidget*,
	rpl::producer<TextWithEntities> text,
	const QString &checkbox,
	rpl::producer<QString> confirm,
	FnMut<void(bool)> callback)
: _confirm(std::move(confirm))
, _content(setupContent(std::move(text), checkbox, std::move(callback))) {
}

void ConfirmDontWarnBox::prepare() {
	setDimensionsToContent(st::boxWidth, _content);
	addButton(std::move(_confirm), [=] { _callback(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

not_null<Ui::RpWidget*> ConfirmDontWarnBox::setupContent(
		rpl::producer<TextWithEntities> text,
		const QString &checkbox,
		FnMut<void(bool)> callback) {
	const auto result = Ui::CreateChild<Ui::VerticalLayout>(this);
	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			std::move(text),
			st::boxLabel),
		st::boxPadding);
	const auto control = result->add(
		object_ptr<Ui::Checkbox>(
			result,
			checkbox,
			false,
			st::defaultBoxCheckbox),
		style::margins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_callback = [=, callback = std::move(callback)]() mutable {
		const auto checked = control->checked();
		auto local = std::move(callback);
		closeBox();
		local(checked);
	};
	return result;
}

} // namespace Ui
