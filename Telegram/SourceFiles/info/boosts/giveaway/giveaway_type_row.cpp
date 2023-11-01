/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/giveaway/giveaway_type_row.h"

#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_options.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_boxes.h"
#include "styles/style_giveaway.h"

namespace Giveaway {

constexpr auto kColorIndexSpecific = int(4);
constexpr auto kColorIndexRandom = int(2);

GiveawayTypeRow::GiveawayTypeRow(
	not_null<Ui::RpWidget*> parent,
	Type type,
	rpl::producer<QString> subtitle)
: RippleButton(parent, st::defaultRippleAnimation)
, _type(type)
, _st((_type == Type::SpecificUsers || _type == Type::Random)
	? st::giveawayTypeListItem
	: st::giveawayGiftCodeMembersPeerList.item)
, _userpic(
	Ui::EmptyUserpic::UserpicColor((_type == Type::SpecificUsers)
		? kColorIndexSpecific
		: kColorIndexRandom),
	QString())
, _name(
	_st.nameStyle,
	(type == Type::SpecificUsers)
		? tr::lng_giveaway_award_option(tr::now)
		: (type == Type::Random)
		? tr::lng_giveaway_create_option(tr::now)
		: (type == Type::AllMembers)
		? tr::lng_giveaway_users_all(tr::now)
		: tr::lng_giveaway_users_new(tr::now),
	Ui::NameTextOptions()) {
	std::move(
		subtitle
	) | rpl::start_with_next([=] (const QString &s) {
		_status.setText(st::defaultTextStyle, s, Ui::NameTextOptions());
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
	const auto hasUserpic = (_type == Type::Random) || isSpecific;

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

		const auto &userpic = isSpecific
			? st::giveawayUserpicGroup
			: st::giveawayUserpic;
		const auto userpicRect = QRect(
			_st.photoPosition
				- QPoint(
					isSpecific ? -st::giveawayUserpicSkip : 0,
					isSpecific ? 0 : st::giveawayUserpicSkip),
			Size(_st.photoSize));
		userpic.paintInCenter(p, userpicRect);
	}

	const auto namex = _st.namePosition.x();
	const auto namey = _st.namePosition.y();
	const auto namew = outerWidth - namex - skipRight;

	p.setPen(_st.nameFg);
	_name.drawLeftElided(p, namex, namey, namew, width());

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
