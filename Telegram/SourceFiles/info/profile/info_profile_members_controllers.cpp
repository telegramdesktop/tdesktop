/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_members_controllers.h"

#include "boxes/peers/edit_participants_box.h"
#include "info/profile/info_profile_values.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "ui/unread_badge.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

namespace Info {
namespace Profile {

MemberListRow::MemberListRow(
	not_null<PeerData*> peer,
	Type type)
: PeerListRow(peer)
, _type(type) {
	setType(type);
}

MemberListRow::~MemberListRow() = default;

void MemberListRow::setType(Type type) {
	_type = type;
	_tagRipple = nullptr;
	_removeRipple = nullptr;
	if (_type.canAddTag) {
		_tagMode = TagMode::AddTag;
		_tagText = tr::lng_context_add_my_tag(tr::now);
	} else if (!_type.rank.isEmpty()) {
		_tagMode = (_type.rights == Rights::Admin
			|| _type.rights == Rights::Creator)
			? TagMode::AdminPill
			: TagMode::NormalText;
		_tagText = _type.rank;
	} else if (_type.rights == Rights::Creator) {
		_tagMode = TagMode::AdminPill;
		_tagText = tr::lng_owner_badge(tr::now);
	} else if (_type.rights == Rights::Admin) {
		_tagMode = TagMode::AdminPill;
		_tagText = tr::lng_admin_badge(tr::now);
	} else {
		_tagMode = TagMode::None;
		_tagText = QString();
	}
	_tagTextWidth = _tagText.isEmpty()
		? 0
		: st::normalFont->width(_tagText);
	if (_type.canRemove) {
		_removeText = _type.removeText.isEmpty()
			? tr::lng_profile_kick(tr::now)
			: _type.removeText;
		_removeTextWidth = st::normalFont->width(_removeText);
	} else {
		_removeText = QString();
		_removeTextWidth = 0;
	}
}

MemberListRow::Type MemberListRow::type() const {
	return _type;
}

UserData *MemberListRow::user() const {
	return peer()->asUser();
}

void MemberListRow::setRefreshCallback(Fn<void()> callback) {
	_refreshCallback = std::move(callback);
}

bool MemberListRow::tagInteractive() const {
	return _type.canEditTag || _type.canAddTag;
}

QSize MemberListRow::tagSize() const {
	if (_tagTextWidth == 0) {
		return QSize();
	}
	const auto usePill = (_tagMode == TagMode::AdminPill)
		|| (_tagMode == TagMode::AddTag)
		|| ((_tagMode == TagMode::NormalText) && tagInteractive());
	if (usePill) {
		const auto &p = st::memberTagPillPadding;
		const auto h = p.top() + st::normalFont->height + p.bottom();
		const auto w = p.left() + _tagTextWidth + p.right();
		return QSize(std::max(w, h), h);
	}
	return QSize(_tagTextWidth, st::normalFont->height);
}

QSize MemberListRow::rightActionSize() const {
	const auto tag = tagSize();
	const auto remove = removeSize();
	const auto w = std::max(tag.width(), remove.width());
	return (w > 0)
		? QSize(w, st::defaultPeerListItem.height)
		: QSize();
}

QMargins MemberListRow::rightActionMargins() const {
	const auto skip = st::contactsCheckPosition.x();
	return QMargins(
		skip,
		0,
		st::defaultPeerListItem.photoPosition.x() + skip,
		0);
}

QSize MemberListRow::removeSize() const {
	if (_removeTextWidth == 0) {
		return QSize();
	}
	const auto &p = st::memberTagPillPadding;
	const auto h = p.top() + st::normalFont->height + p.bottom();
	const auto w = p.left() + _removeTextWidth + p.right();
	return QSize(std::max(w, h), h);
}

int MemberListRow::elementsCount() const {
	return _type.canRemove ? 2 : 1;
}

QRect MemberListRow::elementGeometry(int element, int outerWidth) const {
	const auto skip = st::contactsCheckPosition.x();
	const auto &st = st::defaultPeerListItem;
	const auto right = st.photoPosition.x() + skip;

	if (element == kTagElement) {
		const auto size = tagSize();
		if (size.isEmpty()) {
			return QRect();
		}
		const auto left = outerWidth - right - size.width();
		if (!_type.canRemove) {
			const auto top = (st.height - size.height()) / 2;
			return QRect(QPoint(left, top), size);
		}
		const auto progress = _hoverAnimation.value(
			_wasHovered ? 1. : 0.);
		const auto centeredTop = (st.height - size.height()) / 2;
		const auto statusAlignedTop = st.statusPosition.y()
			+ (st::contactsStatusFont->height - size.height()) / 2;
		const auto top = anim::interpolate(
			centeredTop, statusAlignedTop, progress);
		return QRect(QPoint(left, top), size);
	} else if (element == kRemoveElement) {
		const auto size = removeSize();
		if (size.isEmpty()) {
			return QRect();
		}
		const auto left = outerWidth - right - size.width();
		const auto nameAlignedTop = st.namePosition.y()
			+ (st.nameStyle.font->height - size.height()) / 2;
		return QRect(QPoint(left, nameAlignedTop), size);
	}
	return QRect();
}

bool MemberListRow::elementDisabled(int element) const {
	if (element == kTagElement) {
		return !tagInteractive();
	}
	if (element == kRemoveElement) {
		const auto progress = _hoverAnimation.value(
			_wasHovered ? 1. : 0.);
		return (progress == 0.);
	}
	return false;
}

bool MemberListRow::elementOnlySelect(int element) const {
	return false;
}

void MemberListRow::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	if (!_refreshCallback) {
		_refreshCallback = updateCallback;
	}
	if (element == kTagElement) {
		if (!tagInteractive()) {
			return;
		}
		if (!_tagRipple) {
			const auto size = tagSize();
			const auto radius = size.height() / 2;
			auto mask = Ui::RippleAnimation::RoundRectMask(
				size, radius);
			_tagRipple = std::make_unique<Ui::RippleAnimation>(
				st::defaultLightButton.ripple,
				std::move(mask),
				updateCallback);
		}
		_tagRipple->add(point);
	} else if (element == kRemoveElement) {
		if (!_removeRipple) {
			const auto size = removeSize();
			const auto radius = size.height() / 2;
			auto mask = Ui::RippleAnimation::RoundRectMask(
				size, radius);
			_removeRipple = std::make_unique<Ui::RippleAnimation>(
				st::defaultLightButton.ripple,
				std::move(mask),
				updateCallback);
		}
		_removeRipple->add(point);
	}
}

void MemberListRow::elementsStopLastRipple() {
	if (_tagRipple) {
		_tagRipple->lastStop();
	}
	if (_removeRipple) {
		_removeRipple->lastStop();
	}
}

void MemberListRow::refreshStatus() {
	const auto u = user();
	if (u && u->isBot()) {
		const auto seesAllMessages = (u->botInfo->readsAllHistory
			|| _type.rights != Rights::Normal);
		setCustomStatus(seesAllMessages
			? tr::lng_status_bot_reads_all(tr::now)
			: tr::lng_status_bot_not_reads_all(tr::now));
	} else {
		PeerListRow::refreshStatus();
	}
}

void MemberListRow::checkHoverChanged(bool hovered) {
	if (!_type.canRemove || _wasHovered == hovered) {
		return;
	}
	_wasHovered = hovered;
	_hoverAnimation.start(
		_refreshCallback ? _refreshCallback : Fn<void()>([] {}),
		hovered ? 0. : 1.,
		hovered ? 1. : 0.,
		st::universalDuration);
}

int MemberListRow::pillHeight() const {
	const auto &p = st::memberTagPillPadding;
	return p.top() + st::normalFont->height + p.bottom();
}

const QImage &MemberListRow::ensurePillCircle(const QColor &color) const {
	auto &cache = *_type.circleCache;
	const auto rgba = color.rgba();
	const auto it = cache.find(rgba);
	if (it != end(cache)) {
		return it->second;
	}
	const auto h = pillHeight();
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		QSize(h, h) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(0, 0, h, h);
	}
	return cache.emplace(rgba, std::move(image)).first->second;
}

void MemberListRow::paintPill(
		Painter &p,
		int x,
		int y,
		int width,
		const QColor &bgColor) const {
	const auto h = pillHeight();
	const auto &circle = ensurePillCircle(bgColor);
	const auto ratio = style::DevicePixelRatio();
	const auto half = h / 2;
	const auto otherHalf = h - half;
	p.drawImage(
		QRect(x, y, half, h),
		circle,
		QRect(0, 0, half * ratio, h * ratio));
	if (width > h) {
		p.fillRect(
			x + half,
			y,
			width - h,
			h,
			bgColor);
	}
	p.drawImage(
		QRect(x + width - otherHalf, y, otherHalf, h),
		circle,
		QRect(half * ratio, 0, otherHalf * ratio, h * ratio));
}

void MemberListRow::paintColoredPill(
		Painter &p,
		int x,
		int y,
		int w,
		int textWidth,
		const QString &text,
		const QColor &color,
		bool over,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth) {
	const auto &pad = st::memberTagPillPadding;
	auto bgColor = color;
	bgColor.setAlphaF(over ? 0.16 : 0.12);
	paintPill(p, x, y, w, bgColor);
	if (ripple) {
		auto rippleColor = color;
		rippleColor.setAlphaF(0.12);
		ripple->paint(p, x, y, outerWidth, &rippleColor);
		if (ripple->empty()) {
			ripple.reset();
		}
	}
	p.setFont(st::normalFont);
	p.setPen(color);
	p.drawTextLeft(
		x + (w - textWidth) / 2,
		y + pad.top(),
		outerWidth,
		text,
		textWidth);
}

void MemberListRow::paintTag(
		Painter &p,
		QRect geometry,
		int outerWidth,
		bool over) {
	if (_tagTextWidth == 0) {
		return;
	}
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto &pad = st::memberTagPillPadding;
	switch (_tagMode) {
	case TagMode::AdminPill: {
		const auto nameColor = (_type.rights == Rights::Creator)
			? st::rankOwnerFg->c
			: st::rankAdminFg->c;
		const auto h = pillHeight();
		const auto cw = pad.left() + _tagTextWidth + pad.right();
		const auto w = std::max(cw, h);
		paintColoredPill(
			p,
			x,
			y,
			w,
			_tagTextWidth,
			_tagText,
			nameColor,
			over,
			_tagRipple,
			outerWidth);
	} break;
	case TagMode::NormalText: {
		if (tagInteractive()) {
			const auto h = pillHeight();
			const auto cw = pad.left() + _tagTextWidth + pad.right();
			const auto w = std::max(cw, h);
			if (over) {
				auto bgColor = st::rankUserFg->c;
				bgColor.setAlphaF(0.12);
				paintPill(p, x, y, w, bgColor);
			}
			if (_tagRipple) {
				auto rippleColor = st::rankUserFg->c;
				rippleColor.setAlphaF(0.12);
				_tagRipple->paint(
					p, x, y, outerWidth, &rippleColor);
				if (_tagRipple->empty()) {
					_tagRipple.reset();
				}
			}
			p.setFont(st::normalFont);
			p.setPen(st::rankUserFg);
			p.drawTextLeft(
				x + (w - _tagTextWidth) / 2,
				y + pad.top(),
				outerWidth,
				_tagText,
				_tagTextWidth);
		} else {
			p.setFont(st::normalFont);
			p.setPen(st::rankUserFg);
			p.drawTextLeft(
				x, y, outerWidth, _tagText, _tagTextWidth);
		}
	} break;
	case TagMode::AddTag: {
		const auto h = pillHeight();
		const auto cw = pad.left() + _tagTextWidth + pad.right();
		const auto w = std::max(cw, h);
		if (over) {
			paintPill(p, x, y, w, st::lightButtonBgOver->c);
		}
		if (_tagRipple) {
			const auto color = st::lightButtonBgRipple->c;
			_tagRipple->paint(p, x, y, outerWidth, &color);
			if (_tagRipple->empty()) {
				_tagRipple.reset();
			}
		}
		p.setFont(st::normalFont);
		p.setPen(over
			? st::lightButtonFgOver
			: st::lightButtonFg);
		p.drawTextLeft(
			x + (w - _tagTextWidth) / 2,
			y + pad.top(),
			outerWidth,
			_tagText,
			_tagTextWidth);
	} break;
	case TagMode::None:
		break;
	}
}

void MemberListRow::paintRemove(
		Painter &p,
		QRect geometry,
		int outerWidth,
		bool over) {
	if (_removeTextWidth == 0) {
		return;
	}
	const auto progress = _hoverAnimation.value(
		_wasHovered ? 1. : 0.);
	if (progress == 0.) {
		return;
	}
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto &pad = st::memberTagPillPadding;
	const auto h = pillHeight();
	const auto cw = pad.left() + _removeTextWidth + pad.right();
	const auto w = std::max(cw, h);

	auto o = p.opacity();
	p.setOpacity(o * progress);

	if (over) {
		paintPill(p, x, y, w, st::lightButtonBgOver->c);
	}
	if (_removeRipple) {
		const auto color = st::lightButtonBgRipple->c;
		_removeRipple->paint(p, x, y, outerWidth, &color);
		if (_removeRipple->empty()) {
			_removeRipple.reset();
		}
	}
	p.setFont(st::normalFont);
	p.setPen(over ? st::lightButtonFgOver : st::lightButtonFg);
	p.drawTextLeft(
		x + (w - _removeTextWidth) / 2,
		y + pad.top(),
		outerWidth,
		_removeText,
		_removeTextWidth);

	p.setOpacity(o);
}

void MemberListRow::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	checkHoverChanged(selected || (selectedElement > 0));
	if (_type.canRemove) {
		const auto removeGeometry = elementGeometry(
			kRemoveElement, outerWidth);
		if (!removeGeometry.isEmpty()) {
			paintRemove(
				p,
				removeGeometry,
				outerWidth,
				(selectedElement == kRemoveElement));
		}
	}
	const auto tagGeometry = elementGeometry(
		kTagElement, outerWidth);
	if (!tagGeometry.isEmpty()) {
		paintTag(
			p,
			tagGeometry,
			outerWidth,
			(selectedElement == kTagElement));
	}
}

std::unique_ptr<ParticipantsBoxController> CreateMembersController(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	return std::make_unique<ParticipantsBoxController>(
		navigation,
		peer,
		ParticipantsBoxController::Role::Profile);
}

} // namespace Profile
} // namespace Info
