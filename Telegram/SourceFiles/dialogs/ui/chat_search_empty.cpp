/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/chat_search_empty.h"

#include "base/object_ptr.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/widgets/labels.h"
#include "styles/style_dialogs.h"

namespace Dialogs {

SearchEmpty::SearchEmpty(
	QWidget *parent,
	Icon icon,
	rpl::producer<TextWithEntities> text)
: RpWidget(parent) {
	setup(icon, std::move(text));
}

void SearchEmpty::setMinimalHeight(int minimalHeight) {
	const auto minimal = st::recentPeersEmptyHeightMin;
	resize(width(), std::max(minimalHeight, minimal));
}

void SearchEmpty::setup(Icon icon, rpl::producer<TextWithEntities> text) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		this,
		std::move(text),
		st::defaultPeerListAbout);
	const auto size = st::recentPeersEmptySize;
	const auto animation = [&] {
		switch (icon) {
		case Icon::Search: return u"search"_q;
		case Icon::NoResults: return u"noresults"_q;
		}
		Unexpected("Icon in SearchEmpty::setup.");
	}();
	const auto [widget, animate] = Settings::CreateLottieIcon(
		this,
		{
			.name = animation,
			.sizeOverride = { size, size },
		},
		st::recentPeersEmptyMargin);
	const auto animated = widget.data();

	sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto padding = st::recentPeersEmptyMargin;
		const auto paddings = padding.left() + padding.right();
		label->resizeToWidth(size.width() - paddings);
		const auto x = (size.width() - animated->width()) / 2;
		const auto y = (size.height() - animated->height()) / 3;
		const auto top = y + animated->height() + st::recentPeersEmptySkip;
		const auto sub = std::max(top + label->height() - size.height(), 0);
		animated->move(x, y - sub);
		label->move((size.width() - label->width()) / 2, top - sub);
	}, lifetime());

	_animate = [animate] {
		animate(anim::repeat::once);
	};
}

void SearchEmpty::animate() {
	if (const auto onstack = _animate) {
		onstack();
	}
}

} // namespace Dialogs
