/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/feature_list.h"

#include "base/object_ptr.h"
#include "info/profile/info_profile_icon.h"
#include "ui/widgets/labels.h"
#include "styles/style_info.h"

namespace Ui {

object_ptr<RpWidget> MakeFeatureListEntry(
		QWidget *parent,
		FeatureListEntry feature,
		const Text::MarkedContext &context,
		const style::FlatLabel &stTitle,
		const style::FlatLabel &stAbout) {
	auto result = object_ptr<PaddingWrap<>>(
		parent,
		object_ptr<RpWidget>(parent),
		st::infoStarsFeatureMargin);
	const auto widget = result->entity();
	const auto icon = CreateChild<Info::Profile::FloatingIcon>(
		widget,
		feature.icon,
		st::infoStarsFeatureIconPosition);
	const auto title = CreateChild<FlatLabel>(
		widget,
		feature.title,
		stTitle);
	const auto about = CreateChild<FlatLabel>(
		widget,
		rpl::single(feature.about),
		stAbout,
		st::defaultPopupMenu,
		context);
	icon->show();
	title->show();
	about->show();
	widget->widthValue(
	) | rpl::on_next([=](int width) {
		const auto left = st::infoStarsFeatureLabelLeft;
		const auto available = width - left;
		title->resizeToWidth(available);
		about->resizeToWidth(available);
		auto top = 0;
		title->move(left, top);
		top += title->height() + st::infoStarsFeatureSkip;
		about->move(left, top);
		top += about->height();
		widget->resize(width, top);
	}, widget->lifetime());
	return result;
}

} // namespace Ui
