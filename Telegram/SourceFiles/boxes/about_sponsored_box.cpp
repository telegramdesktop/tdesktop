/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/about_sponsored_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtGui/QDesktopServices>

namespace Ui {
namespace {

constexpr auto kUrl = "https://promote.telegram.org"_cs;

} // namespace

void AboutSponsoredBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_sponsored_title());
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });

	const auto addUrl = [&] {
		const auto &st = st::sponsoredUrlButton;
		const auto row = box->addRow(object_ptr<RpWidget>(box));
		row->resize(0, st.height + st.padding.top() + st.padding.bottom());
		const auto button = Ui::CreateChild<RoundButton>(
			row,
			rpl::single<QString>(kUrl.utf8()),
			st);
		button->setBrushOverride(Qt::NoBrush);
		button->setPenOverride(QPen(st::historyLinkInFg));
		button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		rpl::combine(
			row->sizeValue(),
			button->sizeValue()
		) | rpl::start_with_next([=](
				const QSize &rowSize,
				const QSize &buttonSize) {
			button->moveToLeft(
				(rowSize.width() - buttonSize.width()) / 2,
				(rowSize.height() - buttonSize.height()) / 2);
		}, row->lifetime());
		button->addClickHandler([=] {
			QDesktopServices::openUrl({ kUrl.utf8() });
		});
	};

	const auto &stLabel = st::aboutLabel;
	const auto info1 = box->addRow(object_ptr<FlatLabel>(box, stLabel));
	info1->setText(tr::lng_sponsored_info_description1(tr::now));

	box->addSkip(st::sponsoredUrlButtonSkip);
	addUrl();
	box->addSkip(st::sponsoredUrlButtonSkip);

	const auto info2 = box->addRow(object_ptr<FlatLabel>(box, stLabel));
	info2->setText(tr::lng_sponsored_info_description2(tr::now));
}

} // namespace Ui
