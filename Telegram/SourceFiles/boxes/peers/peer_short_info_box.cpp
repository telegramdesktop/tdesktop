/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/peer_short_info_box.h"

#include "ui/widgets/labels.h"
#include "ui/image/image_prepare.h"
#include "media/streaming/media_streaming_instance.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace {

} // namespace

PeerShortInfoBox::PeerShortInfoBox(
	QWidget*,
	PeerShortInfoType type,
	rpl::producer<PeerShortInfoFields> fields,
	rpl::producer<QString> status,
	rpl::producer<PeerShortInfoUserpic> userpic)
: _type(type)
, _fields(std::move(fields))
, _name(this, nameValue(), st::shortInfoName)
, _status(this, std::move(status), st::shortInfoStatus) {
	std::move(
		userpic
	) | rpl::start_with_next([=](PeerShortInfoUserpic &&value) {
		applyUserpic(std::move(value));
	}, lifetime());
}

PeerShortInfoBox::~PeerShortInfoBox() = default;

rpl::producer<> PeerShortInfoBox::openRequests() const {
	return _openRequests.events();
}

void PeerShortInfoBox::prepare() {
	addButton(tr::lng_close(), [=] { closeBox(); });

	// Perhaps a new lang key should be added for opening a group.
	addLeftButton((_type == PeerShortInfoType::User)
		? tr::lng_profile_send_message()
		: (_type == PeerShortInfoType::Group)
		? tr::lng_view_button_group()
		: tr::lng_profile_view_channel(), [=] { _openRequests.fire({}); });

	setNoContentMargin(true);
	setDimensions(st::shortInfoWidth, st::shortInfoWidth);
}

RectParts PeerShortInfoBox::customCornersFilling() {
	return RectPart::FullTop;
}

void PeerShortInfoBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
}

void PeerShortInfoBox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto coverSize = st::shortInfoWidth;
	if (_userpicImage.isNull()) {
		const auto size = coverSize * style::DevicePixelRatio();
		auto image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::black);
		Images::prepareRound(
			image,
			ImageRoundRadius::Small,
			RectPart::TopLeft | RectPart::TopRight);
	}
	p.drawImage(QRect(0, 0, coverSize, coverSize), _userpicImage);
}

rpl::producer<QString> PeerShortInfoBox::nameValue() const {
	return _fields.value() | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.name;
	}) | rpl::distinct_until_changed();
}

void PeerShortInfoBox::applyUserpic(PeerShortInfoUserpic &&value) {
	if (!value.photo.isNull()
		&& _userpicImage.cacheKey() != value.photo.cacheKey()) {
		_userpicImage = std::move(value.photo);
		update();
	}
}
