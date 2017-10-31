/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/search_field_controller.h"

#include "styles/style_widgets.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"

namespace Ui {

base::unique_qptr<Ui::RpWidget> SearchFieldController::createView(
		QWidget *parent,
		const style::SearchFieldRow &st) {
	auto result = base::make_unique_q<Ui::FixedHeightWidget>(
		parent,
		st.height);

	auto cancel = CreateChild<Ui::CrossButton>(
		result.get(),
		st.fieldCancel);
	cancel->addClickHandler([=] { clearQuery(); });

	auto field = CreateChild<Ui::InputField>(
		result.get(),
		st.field,
		langFactory(lng_dlg_filter),
		_query.current());
	field->show();
	field->connect(field, &Ui::InputField::changed, [=] {
		setQueryFromField(field->getLastText());
	});
	field->connect(field, &Ui::InputField::cancelled, [=] {
		clearQuery();
	});

	auto shadow = CreateChild<Ui::PlainShadow>(result.get());
	shadow->show();

	result->widthValue()
		| rpl::start_with_next([=, &st](int newWidth) {
			auto availableWidth = newWidth
				- st.fieldIconSkip
				- st.fieldCancelSkip;
			field->setGeometryToLeft(
				st.padding.left() + st.fieldIconSkip,
				st.padding.top(),
				availableWidth,
				field->height());
			cancel->moveToRight(0, 0);
			shadow->setGeometry(
				0,
				st.height - st::lineWidth,
				newWidth,
				st::lineWidth);
		}, result->lifetime());
	result->paintRequest()
		| rpl::start_with_next([=, &st] {
			Painter p(_view.wrap);
			st.fieldIcon.paint(
				p,
				st.padding.left(),
				st.padding.top(),
				_view.wrap->width());
		}, result->lifetime());

	_view.wrap.reset(result.get());
	_view.cancel = cancel;
	_view.field = field;
	return std::move(result);
}

void SearchFieldController::setQueryFromField(const QString &query) {
	_query = query;
	if (_view.cancel) {
		_view.cancel->toggleAnimated(!query.isEmpty());
	}
}

void SearchFieldController::clearQuery() {
	if (_view.field) {
		_view.field->setText(QString());
	} else {
		setQueryFromField(QString());
	}
}

} // namespace Ui
