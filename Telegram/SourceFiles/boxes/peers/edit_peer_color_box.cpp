/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_color_box.h"

#include "data/data_changes.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace {

using namespace Settings;

class ColorSample final : public Ui::RpWidget {
public:
	ColorSample(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::ChatStyle> st,
		rpl::producer<uint8> colorIndex,
		const QString &name);

	int naturalWidth() const override;

private:
	void paintEvent(QPaintEvent *e) override;

	std::shared_ptr<Ui::ChatStyle> _st;
	Ui::Text::String _name;
	uint8 _index = 0;

};

ColorSample::ColorSample(
	not_null<QWidget*> parent,
	std::shared_ptr<Ui::ChatStyle> st,
	rpl::producer<uint8> colorIndex,
	const QString &name)
: RpWidget(parent)
, _st(st)
, _name(st::semiboldTextStyle, name) {
	std::move(
		colorIndex
	) | rpl::start_with_next([=](uint8 index) {
		_index = index;
		update();
	}, lifetime());
}

void ColorSample::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = _st->coloredValues(false, _index);
	if (!colors.outlines[1].alpha()) {
		const auto radius = height() / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(colors.bg);
		p.drawRoundedRect(rect(), radius, radius);

		const auto padding = st::settingsColorSamplePadding;
		p.setPen(colors.name);
		p.setBrush(Qt::NoBrush);
		p.setFont(st::semiboldFont);
		_name.drawLeftElided(
			p,
			padding.left(),
			padding.top(),
			width() - padding.left() - padding.right(),
			width(),
			1,
			style::al_top);
	} else {
		const auto size = width();
		const auto half = size / 2.;
		p.translate(size / 2., size / 2.);
		p.rotate(-45.);
		p.setPen(Qt::NoPen);
		p.setClipRect(-size, -size, 3 * size, size);
		p.setBrush(colors.outlines[0]);
		p.drawEllipse(-half, -half, size, size);
		p.setClipRect(-size, 0, 3 * size, size);
		p.setBrush(colors.outlines[1]);
		p.drawEllipse(-half, -half, size, size);
		if (colors.outlines[2].alpha()) {
			const auto center = st::settingsColorSampleCenter;
			const auto radius = st::settingsColorSampleCenterRadius;
			p.setClipping(false);
			p.setBrush(colors.outlines[2]);
			p.drawRoundedRect(
				QRectF(-center / 2., -center / 2., center, center),
				radius,
				radius);
		}
	}
}

int ColorSample::naturalWidth() const {
	if (_st->colorPatternIndex(_index)) {
		return st::settingsColorSampleSize;
	}
	const auto padding = st::settingsColorSamplePadding;
	return std::max(
		padding.left() + _name.maxWidth() + padding.right(),
		padding.top() + st::semiboldFont->height + padding.bottom());
}

} // namespace

void EditPeerColorBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	std::shared_ptr<Ui::ChatStyle> st) {

}

void AddPeerColorButton(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer) {
	const auto button = AddButton(
		container,
		(peer->isSelf()
			? tr::lng_settings_theme_name_color()
			: tr::lng_edit_channel_color()),
		st::settingsColorButton,
		{ &st::menuIconChangeColors });

	auto colorIndexValue = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::Color
	) | rpl::map([=] {
		return peer->colorIndex();
	});
	const auto name = peer->shortName();
	const auto st = std::make_shared<Ui::ChatStyle>(
		peer->session().colorIndicesValue());
	const auto sample = Ui::CreateChild<ColorSample>(
		button.get(),
		st,
		rpl::duplicate(colorIndexValue),
		name);
	sample->show();

	rpl::combine(
		button->widthValue(),
		tr::lng_settings_theme_name_color(),
		rpl::duplicate(colorIndexValue)
	) | rpl::start_with_next([=](
			int width,
			const QString &button,
			int colorIndex) {
		const auto sampleSize = st::settingsColorSampleSize;
		const auto available = width
			- st::settingsButton.padding.left()
			- (st::settingsColorButton.padding.right() - sampleSize)
			- st::settingsButton.style.font->width(button)
			- st::settingsButtonRightSkip;
		if (st->colorPatternIndex(colorIndex)) {
			sample->resize(sampleSize, sampleSize);
		} else {
			const auto padding = st::settingsColorSamplePadding;
			const auto wantedHeight = padding.top()
				+ st::semiboldFont->height
				+ padding.bottom();
			const auto wantedWidth = sample->naturalWidth();
			sample->resize(std::min(wantedWidth, available), wantedHeight);
		}
		sample->update();
	}, sample->lifetime());

	rpl::combine(
		button->sizeValue(),
		sample->sizeValue(),
		std::move(colorIndexValue)
	) | rpl::start_with_next([=](QSize outer, QSize inner, int colorIndex) {
		const auto right = st::settingsColorButton.padding.right()
			- st::settingsColorSampleSkip
			- st::settingsColorSampleSize
			- (st->colorPatternIndex(colorIndex)
				? 0
				: st::settingsColorSamplePadding.right());
		sample->move(
			outer.width() - right - inner.width(),
			(outer.height() - inner.height()) / 2);
	}, sample->lifetime());

	sample->setAttribute(Qt::WA_TransparentForMouseEvents);
}
