/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/boosts/giveaway/giveaway_type_row.h"

#include "lang/lang_keys.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_options.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_color_indices.h"
#include "styles/style_giveaway.h"
#include "styles/style_statistics.h"

namespace Giveaway {

GiveawayTypeRow::GiveawayTypeRow(
	not_null<Ui::RpWidget*> parent,
	Type type,
	rpl::producer<QString> subtitle,
	bool group)
: GiveawayTypeRow(
	parent,
	type,
	(type == Type::SpecificUsers) ? st::colorIndexBlue : st::colorIndexGreen,
	(type == Type::SpecificUsers)
		? tr::lng_giveaway_award_option()
		: (type == Type::Random)
		? tr::lng_premium_summary_title()
		// ? tr::lng_giveaway_create_option()
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
	: ((_type == Type::Prepaid) || (_type == Type::PrepaidCredits))
	? st::boostsListBox.item
	: st::giveawayGiftCodeMembersPeerList.item)
, _userpic(
	Ui::EmptyUserpic::UserpicColor(Ui::EmptyUserpic::ColorIndex(colorIndex)),
	QString())
, _badge(std::move(badge)) {
	if (_type == Type::Credits || _type == Type::PrepaidCredits) {
		_customUserpic = Ui::CreditsWhiteDoubledIcon(_st.photoSize, 1.);
	}
	std::move(
		subtitle
	) | rpl::start_with_next([=] (QString s) {
		_status.setText(
			st::defaultTextStyle,
			s.replace(QChar('>'), QString()),
			Ui::NameTextOptions());
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
	const auto isRandom = (_type == Type::Random);
	const auto isSpecific = (_type == Type::SpecificUsers);
	const auto isPrepaid = (_type == Type::Prepaid);
	const auto hasUserpic = isRandom
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

	const auto statusIcon = isRandom ? &st::topicButtonArrow : nullptr;
	const auto statusx = _st.statusPosition.x();
	const auto statusy = _st.statusPosition.y();
	const auto statusw = outerWidth
		- statusx
		- skipRight
		- (statusIcon
			? (statusIcon->width() + st::boostsListMiniIconSkip)
			: 0);
	p.setFont(st::contactsStatusFont);
	p.setPen((isRandom || !hasUserpic) ? st::lightButtonFg : _st.statusFg);
	_status.drawLeftElided(p, statusx, statusy, statusw, outerWidth);
	if (statusIcon) {
		statusIcon->paint(
			p,
			QPoint(
				statusx
					+ std::min(_status.maxWidth(), statusw)
					+ st::boostsListMiniIconSkip,
				statusy + st::contactsStatusFont->descent),
			outerWidth,
			st::lightButtonFg->c);
	}
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
