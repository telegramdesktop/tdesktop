/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/boosts/giveaway/giveaway_type_row.h"

#include "lang/lang_keys.h"
#include "ui/effects/premium_graphics.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_options.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_boxes.h"
#include "styles/style_giveaway.h"
#include "styles/style_statistics.h"

#include <QtSvg/QSvgRenderer>

namespace Giveaway {
namespace {

[[nodiscard]] QImage CreditsCustomUserpic(int photoSize) {
	auto svg = QSvgRenderer(Ui::Premium::Svg());
	auto result = QImage(
		Size(photoSize) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());

	constexpr auto kPoints = uint(16);
	constexpr auto kAngleStep = 2. * M_PI / kPoints;
	constexpr auto kOutlineWidth = 1.6;
	constexpr auto kStarShift = 3.8;
	const auto userpicRect = Rect(Size(photoSize));
	const auto starRect = userpicRect - Margins(userpicRect.width() / 4.);
	const auto starSize = starRect.size();
	const auto drawSingle = [&](QPainter &q) {
		const auto s = style::ConvertFloatScale(kOutlineWidth);
		q.save();
		q.setCompositionMode(QPainter::CompositionMode_Clear);
		for (auto i = 0; i < kPoints; ++i) {
			const auto angle = i * kAngleStep;
			const auto x = s * std::cos(angle);
			const auto y = s * std::sin(angle);
			svg.render(&q, QRectF(QPointF(x, y), starSize));
		}
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		svg.render(&q, Rect(starSize));
		q.restore();
	};
	{
		auto p = QPainter(&result);
		p.setPen(Qt::NoPen);
		p.setBrush(st::lightButtonFg);
		p.translate(starRect.topLeft());
		p.translate(style::ConvertFloatScale(kStarShift) / 2., 0);
		drawSingle(p);
		{
			// Remove the previous star at bottom.
			p.setCompositionMode(QPainter::CompositionMode_Clear);
			p.save();
			p.resetTransform();
			p.fillRect(
				userpicRect.x(),
				userpicRect.y(),
				userpicRect.width() / 2.,
				userpicRect.height(),
				Qt::transparent);
			p.restore();
		}
		p.translate(-style::ConvertFloatScale(kStarShift), 0);
		drawSingle(p);
	}
	return result;
}

} // namespace

constexpr auto kColorIndexSpecific = int(4);
constexpr auto kColorIndexRandom = int(2);

GiveawayTypeRow::GiveawayTypeRow(
	not_null<Ui::RpWidget*> parent,
	Type type,
	rpl::producer<QString> subtitle,
	bool group)
: GiveawayTypeRow(
	parent,
	type,
	(type == Type::SpecificUsers) ? kColorIndexSpecific : kColorIndexRandom,
	(type == Type::SpecificUsers)
		? tr::lng_giveaway_award_option()
		: (type == Type::Random)
		? tr::lng_giveaway_create_option()
		: (type == Type::AllMembers)
		? (group
			? tr::lng_giveaway_users_all_group()
			: tr::lng_giveaway_users_all())
		: (group
			? tr::lng_giveaway_users_new_group()
			: tr::lng_giveaway_users_new()),
	std::move(subtitle),
	QImage()) {
}

GiveawayTypeRow::GiveawayTypeRow(
	not_null<Ui::RpWidget*> parent,
	Type type,
	int colorIndex,
	rpl::producer<QString> title,
	rpl::producer<QString> subtitle,
	QImage badge)
: RippleButton(parent, st::defaultRippleAnimation)
, _type(type)
, _st((_type == Type::SpecificUsers
		|| _type == Type::Random
		|| _type == Type::Credits)
	? st::giveawayTypeListItem
	: (_type == Type::Prepaid)
	? st::boostsListBox.item
	: st::giveawayGiftCodeMembersPeerList.item)
, _userpic(
	Ui::EmptyUserpic::UserpicColor(Ui::EmptyUserpic::ColorIndex(colorIndex)),
	QString())
, _badge(std::move(badge)) {
	if (_type == Type::Credits) {
		_customUserpic = CreditsCustomUserpic(_st.photoSize);
	}
	std::move(
		subtitle
	) | rpl::start_with_next([=] (const QString &s) {
		_status.setText(st::defaultTextStyle, s, Ui::NameTextOptions());
	}, lifetime());
	std::move(
		title
	) | rpl::start_with_next([=] (const QString &s) {
		_name.setText(_st.nameStyle, s, Ui::NameTextOptions());
	}, lifetime());
}

int GiveawayTypeRow::resizeGetHeight(int) {
	return _st.height;
}

void GiveawayTypeRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto paintOver = (isOver() || isDown()) && !isDisabled();
	const auto skipRight = _st.photoPosition.x();
	const auto outerWidth = width();
	const auto isSpecific = (_type == Type::SpecificUsers);
	const auto isPrepaid = (_type == Type::Prepaid);
	const auto hasUserpic = (_type == Type::Random)
		|| isSpecific
		|| isPrepaid
		|| (!_customUserpic.isNull());

	if (paintOver) {
		p.fillRect(e->rect(), _st.button.textBgOver);
	}
	Ui::RippleButton::paintRipple(p, 0, 0);
	if (hasUserpic) {
		_userpic.paintCircle(
			p,
			_st.photoPosition.x(),
			_st.photoPosition.y(),
			outerWidth,
			_st.photoSize);

		const auto userpicRect = QRect(
			_st.photoPosition
				- QPoint(
					isSpecific ? -st::giveawayUserpicSkip : 0,
					isSpecific ? 0 : st::giveawayUserpicSkip),
			Size(_st.photoSize));
		if (!_customUserpic.isNull()) {
			p.drawImage(_st.photoPosition, _customUserpic);
		} else {
			const auto &userpic = isSpecific
				? st::giveawayUserpicGroup
				: st::giveawayUserpic;
			userpic.paintInCenter(p, userpicRect);
		}
	}

	const auto namex = _st.namePosition.x();
	const auto namey = _st.namePosition.y();
	const auto namew = outerWidth - namex - skipRight;

	const auto badgew = _badge.width() / style::DevicePixelRatio();

	p.setPen(_st.nameFg);
	_name.drawLeftElided(p, namex, namey, namew - badgew, width());

	if (!_badge.isNull()) {
		p.drawImage(
			std::min(
				namex + _name.maxWidth() + st::boostsListBadgePadding.left(),
				outerWidth - badgew - skipRight),
			namey + st::boostsListMiniIconSkip,
			_badge);
	}

	const auto statusx = _st.statusPosition.x();
	const auto statusy = _st.statusPosition.y();
	const auto statusw = outerWidth - statusx - skipRight;
	p.setFont(st::contactsStatusFont);
	p.setPen((isSpecific || !hasUserpic) ? st::lightButtonFg : _st.statusFg);
	_status.drawLeftElided(p, statusx, statusy, statusw, outerWidth);
}

void GiveawayTypeRow::addRadio(
		std::shared_ptr<Ui::RadioenumGroup<Type>> typeGroup) {
	const auto &st = st::defaultCheckbox;
	const auto radio = Ui::CreateChild<Ui::Radioenum<Type>>(
		this,
		std::move(typeGroup),
		_type,
		QString(),
		st);
	const auto pos = (_type == Type::SpecificUsers || _type == Type::Random)
		? st::giveawayRadioPosition
		: st::giveawayRadioMembersPosition;
	radio->moveToLeft(pos.x(), pos.y());
	radio->setAttribute(Qt::WA_TransparentForMouseEvents);
	radio->show();
}

} // namespace Giveaway
