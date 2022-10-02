/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/search_field_controller.h"

#include "styles/style_widgets.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"

namespace Ui {

SearchFieldController::SearchFieldController(const QString &query)
: _query(query) {
}

auto SearchFieldController::createRowView(
		QWidget *parent,
		const style::SearchFieldRow &st) -> RowView {
	auto result = base::make_unique_q<Ui::FixedHeightWidget>(
		parent,
		st.height);
	auto wrap = result.get();

	auto field = createField(wrap, st.field).release();
	field->show();
	field->connect(field, &Ui::InputField::cancelled, [=] {
		field->setText(QString());
	});

	auto cancel = CreateChild<Ui::CrossButton>(
		wrap,
		st.fieldCancel);
	cancel->addClickHandler([=] {
		field->setText(QString());
	});
	queryValue(
	) | rpl::map([](const QString &value) {
		return !value.isEmpty();
	}) | rpl::start_with_next([cancel](bool shown) {
		cancel->toggle(shown, anim::type::normal);
	}, cancel->lifetime());
	cancel->finishAnimating();

	auto shadow = CreateChild<Ui::PlainShadow>(wrap);
	shadow->show();

	wrap->widthValue(
	) | rpl::start_with_next([=, &st](int newWidth) {
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
	}, wrap->lifetime());
	wrap->paintRequest(
	) | rpl::start_with_next([=, &st] {
		auto p = QPainter(wrap);
		st.fieldIcon.paint(
			p,
			st.padding.left(),
			st.padding.top(),
			wrap->width());
	}, wrap->lifetime());

	_view.release();
	_view.reset(wrap);
	return { std::move(result), field };
}

QString SearchFieldController::query() const {
	return _query.current();
}

rpl::producer<QString> SearchFieldController::queryValue() const {
	return _query.value();
}

rpl::producer<QString> SearchFieldController::queryChanges() const {
	return _query.changes();
}

base::unique_qptr<Ui::InputField> SearchFieldController::createField(
		QWidget *parent,
		const style::InputField &st) {
	auto result = base::make_unique_q<Ui::InputField>(
		parent,
		st,
		tr::lng_dlg_filter(),
		_query.current());
	auto field = result.get();
	field->connect(field, &Ui::InputField::changed, [=] {
		_query = field->getLastText();
	});
	_view.reset(field);
	return result;
}

} // namespace Ui
