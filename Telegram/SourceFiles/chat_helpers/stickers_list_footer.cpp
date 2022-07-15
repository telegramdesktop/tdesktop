/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_list_footer.h"

#include "chat_helpers/stickers_lottie.h"
#include "data/stickers/data_stickers_set.h"
#include "data/stickers/data_stickers.h"
#include "data/data_file_origin.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>

namespace ChatHelpers {
namespace {

constexpr auto kEmojiSectionSetIdBase = uint64(0x77FF'FFFF'FFFF'FFF0ULL);

using EmojiSection = Ui::Emoji::Section;

void UpdateAnimated(anim::value &value, int to) {
	if (int(base::SafeRound(value.to())) == to) {
		return;
	}
	value = anim::value(
		(value.from() != value.to()) ? value.from() : to,
		to);
}

void UpdateAnimated(
		anim::value &value,
		int to,
		ValidateIconAnimations animations) {
	if (animations == ValidateIconAnimations::Full) {
		value.start(to);
	} else {
		value = anim::value(to, to);
	}
}

} // namespace

uint64 EmojiSectionSetId(EmojiSection section) {
	Expects(section >= EmojiSection::Recent
		&& section <= EmojiSection::Symbols);

	return kEmojiSectionSetIdBase + static_cast<uint64>(section) + 1;
}

uint64 RecentEmojiSectionSetId() {
	return EmojiSectionSetId(EmojiSection::Recent);
}

uint64 AllEmojiSectionSetId() {
	return kEmojiSectionSetIdBase;
}

std::optional<EmojiSection> SetIdEmojiSection(uint64 id) {
	const auto base = RecentEmojiSectionSetId();
	if (id < base) {
		return {};
	}
	const auto index = id - base;
	return (index <= uint64(EmojiSection::Symbols))
		? static_cast<EmojiSection>(index)
		: std::optional<EmojiSection>();
}

StickerIcon::StickerIcon(uint64 setId) : setId(setId) {
}

StickerIcon::StickerIcon(
	not_null<Data::StickersSet*> set,
	DocumentData *sticker,
	int pixw,
	int pixh)
	: setId(set->id)
	, set(set)
	, sticker(sticker)
	, pixw(pixw)
	, pixh(pixh) {
}

StickerIcon::StickerIcon(StickerIcon&&) = default;

StickerIcon &StickerIcon::operator=(StickerIcon&&) = default;

StickerIcon::~StickerIcon() = default;

void StickerIcon::ensureMediaCreated() const {
	if (!sticker) {
		return;
	} else if (set->hasThumbnail()) {
		if (!thumbnailMedia) {
			thumbnailMedia = set->createThumbnailView();
			set->loadThumbnail();
		}
	} else if (!stickerMedia) {
		stickerMedia = sticker->createMediaView();
		stickerMedia->thumbnailWanted(sticker->stickerSetOrigin());
	}
}

template <typename UpdateCallback>
StickersListFooter::ScrollState::ScrollState(UpdateCallback &&callback)
: animation([=](crl::time now) {
	callback();
	return animationCallback(now);
}) {
}

bool StickersListFooter::ScrollState::animationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::stickerIconMove;
	}
	if (!animationStart) {
		return false;
	}
	const auto dt = (now - animationStart) / float64(st::stickerIconMove);
	if (dt >= 1.) {
		animationStart = 0;
		x.finish();
		selectionX.finish();
		selectionWidth.finish();
		return false;
	}
	x.update(dt, anim::linear);
	selectionX.update(dt, anim::linear);
	selectionWidth.update(dt, anim::linear);
	return true;
}

StickersListFooter::StickersListFooter(Descriptor &&descriptor)
: InnerFooter(descriptor.parent)
, _controller(descriptor.controller)
, _searchButtonVisible(descriptor.searchButtonVisible)
, _settingsButtonVisible(descriptor.settingsButtonVisible)
, _iconState([=] { update(); })
, _subiconState([=] { update(); })
, _selectionBg(st::roundRadiusSmall, st::windowBgRipple)
, _barSelection(descriptor.barSelection) {
	setMouseTracking(true);

	_iconsLeft = st::emojiCategorySkip + (_searchButtonVisible
		? st::stickerIconWidth
		: 0);
	_iconsRight = st::emojiCategorySkip + (_settingsButtonVisible
		? st::stickerIconWidth
		: 0);

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_premiumIcon = QImage();
	}, lifetime());
}

void StickersListFooter::validatePremiumIcon() const {
	if (!_premiumIcon.isNull()) {
		return;
	}
	const auto size = st::stickersPremium.size();
	const auto mask = st::stickersPremium.instance(Qt::white);
	const auto factor = style::DevicePixelRatio();
	_premiumIcon = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	_premiumIcon.setDevicePixelRatio(factor);

	QPainter p(&_premiumIcon);
	auto gradient = QLinearGradient(
		QPoint(0, size.height()),
		QPoint(size.width(), 0));
	gradient.setStops({
		{ 0., st::stickerPanPremium1->c },
		{ 1., st::stickerPanPremium2->c },
		});
	p.fillRect(QRect(QPoint(), size), gradient);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(QRect(QPoint(), size), mask);
}

void StickersListFooter::clearHeavyData() {
	const auto count = int(_icons.size());
	const auto iconsX = qRound(_iconState.x.current());
	enumerateIcons([&](const IconInfo &info) {
		auto &icon = _icons[info.index];
		icon.webm = nullptr;
		icon.lottie = nullptr;
		icon.lifetime.destroy();
		icon.stickerMedia = nullptr;
		if (!info.visible) {
			icon.savedFrame = QPixmap();
		}
		return true;
	});
}

void StickersListFooter::initSearch() {
	_searchField.create(
		this,
		st::gifsSearchField,
		tr::lng_stickers_search_sets());
	_searchCancel.create(this, st::gifsSearchCancel);
	_searchField->show();
	_searchCancel->show(anim::type::instant);

	const auto cancelSearch = [=] {
		if (_searchField->getLastText().isEmpty()) {
			toggleSearch(false);
		} else {
			_searchField->setText(QString());
		}
	};
	connect(_searchField, &Ui::InputField::submitted, [=] {
		_searchRequests.fire({
			.text = _searchField->getLastText(),
			.forced = true,
			});
	});
	connect(_searchField, &Ui::InputField::cancelled, cancelSearch);
	connect(_searchField, &Ui::InputField::changed, [=] {
		_searchRequests.fire({
			.text = _searchField->getLastText(),
			});
	});
	_searchCancel->setClickedCallback(cancelSearch);

	resizeSearchControls();
}

void StickersListFooter::toggleSearch(bool visible) {
	if (_searchShown == visible) {
		return;
	}
	_searchShown = visible;
	if (_searchShown) {
		initSearch();
		stealFocus();
	} else if (_searchField) {
		returnFocus();
		_searchField.destroy();
		_searchCancel.destroy();
		_focusTakenFrom = nullptr;
	}
	update();
}

void StickersListFooter::stealFocus() {
	if (_searchField) {
		if (!_focusTakenFrom) {
			_focusTakenFrom = QApplication::focusWidget();
		}
		_searchField->setFocus();
	}
}

void StickersListFooter::returnFocus() {
	if (_searchField && _focusTakenFrom) {
		if (_searchField->hasFocus()) {
			_focusTakenFrom->setFocus();
		}
		_focusTakenFrom = nullptr;
	}
}

void StickersListFooter::enumerateVisibleIcons(
		Fn<void(const IconInfo &)> callback) const {
	enumerateIcons([&](const IconInfo &info) {
		if (info.visible) {
			callback(info);
		} else if (info.adjustedLeft > 0) {
			return false;
		}
		return true;
	});
}

void StickersListFooter::enumerateIcons(
		Fn<bool(const IconInfo &)> callback) const {
	auto left = 0;
	const auto iconsX = int(base::SafeRound(_iconState.x.current()));
	const auto shift = _iconsLeft - iconsX;
	const auto emojiId = AllEmojiSectionSetId();
	const auto right = width();
	const auto single = st::stickerIconWidth;
	for (auto i = 0, count = int(_icons.size()); i != count; ++i) {
		auto &icon = _icons[i];
		const auto width = (icon.setId == emojiId)
			? _subiconsWidthAnimation.value(_subiconsExpanded
				? _subiconsWidth
				: single)
			: single;
		const auto shifted = shift + left;
		const auto visible = (shifted + width > 0 && shifted < right);
		const auto result = callback({
			.index = i,
			.left = left,
			.adjustedLeft = shifted,
			.width = int(base::SafeRound(width)),
			.visible = visible,
		});
		if (!result) {
			break;
		}
		left += width;
	}
}

void StickersListFooter::enumerateSubicons(
		Fn<bool(const IconInfo &)> callback) const {
	auto left = 0;
	const auto iconsX = int(base::SafeRound(_subiconState.x.current()));
	const auto shift = -iconsX;
	const auto right = _subiconsWidth;
	using Section = Ui::Emoji::Section;
	for (auto i = int(Section::People); i <= int(Section::Symbols); ++i) {
		const auto width = st::stickerIconWidth;
		const auto shifted = shift + left;
		const auto visible = (shifted + width > 0 && shifted < right);
		const auto result = callback({
			.index = i - int(Section::People),
			.left = left,
			.adjustedLeft = shifted,
			.width = int(base::SafeRound(width)),
			.visible = visible,
		});
		if (!result) {
			break;
		}
		left += width;
	}
}

auto StickersListFooter::iconInfo(int index) const -> IconInfo {
	auto result = IconInfo();
	enumerateIcons([&](const IconInfo &info) {
		if (info.index == index) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

auto StickersListFooter::subiconInfo(int index) const -> IconInfo {
	auto result = IconInfo();
	enumerateSubicons([&](const IconInfo &info) {
		if (info.index == index) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

void StickersListFooter::preloadImages() {
	enumerateVisibleIcons([&](const IconInfo &info) {
		const auto &icon = _icons[info.index];
		if (const auto sticker = icon.sticker) {
			Assert(icon.set != nullptr);
			if (icon.set->hasThumbnail()) {
				icon.set->loadThumbnail();
			} else {
				sticker->loadThumbnail(sticker->stickerSetOrigin());
			}
		}
	});
}

void StickersListFooter::validateSelectedIcon(
		uint64 setId,
		ValidateIconAnimations animations) {
	_activeByScrollId = setId;

	using EmojiSection = Ui::Emoji::Section;
	auto favedIconIndex = -1;
	auto newSelected = -1;
	auto newSubSelected = -1;
	const auto emojiSection = SetIdEmojiSection(setId);
	const auto isEmojiSection = emojiSection.has_value()
		&& (emojiSection != EmojiSection::Recent);
	const auto allEmojiSetId = AllEmojiSectionSetId();
	for (auto i = 0, l = int(_icons.size()); i != l; ++i) {
		if (_icons[i].setId == setId
			|| (_icons[i].setId == Data::Stickers::FavedSetId
				&& setId == Data::Stickers::RecentSetId)) {
			newSelected = i;
			break;
		} else if (_icons[i].setId == Data::Stickers::FavedSetId) {
			favedIconIndex = i;
		} else if (isEmojiSection && _icons[i].setId == allEmojiSetId) {
			newSelected = i;
			newSubSelected = setId - EmojiSectionSetId(EmojiSection::People);
		}
	}
	setSelectedIcon(
		(newSelected >= 0
			? newSelected
			: (favedIconIndex >= 0) ? favedIconIndex : 0),
		animations);
	setSelectedSubicon(
		(newSubSelected >= 0 ? newSubSelected : 0),
		animations);
}

void StickersListFooter::updateEmojiSectionWidth() {
	const auto expanded = (_iconState.selected >= 0)
		&& (_iconState.selected < _icons.size())
		&& (_icons[_iconState.selected].setId == AllEmojiSectionSetId());
	if (_subiconsExpanded == expanded) {
		return;
	}
	_subiconsExpanded = expanded;
	_subiconsWidthAnimation.start(
		[=] { updateEmojiWidthCallback(); },
		expanded ? st::stickerIconWidth : _subiconsWidth,
		expanded ? _subiconsWidth : st::stickerIconWidth,
		st::slideDuration);
}

void StickersListFooter::updateEmojiWidthCallback() {
	refreshScrollableDimensions();
	const auto info = iconInfo(_iconState.selected);
	UpdateAnimated(_iconState.selectionX, info.left);
	UpdateAnimated(_iconState.selectionWidth, info.width);
	if (_iconState.animation.animating()) {
		_iconState.animationCallback(crl::now());
	}
	update();
}

void StickersListFooter::setSelectedIcon(
		int newSelected,
		ValidateIconAnimations animations) {
	if (_iconState.selected == newSelected) {
		return;
	}
	_iconState.selected = newSelected;
	updateEmojiSectionWidth();
	const auto info = iconInfo(_iconState.selected);
	UpdateAnimated(_iconState.selectionX, info.left, animations);
	UpdateAnimated(_iconState.selectionWidth, info.width, animations);
	const auto relativeLeft = info.left - _iconsLeft;
	const auto iconsWidthForCentering = 2 * relativeLeft + info.width;
	const auto iconsXFinal = std::clamp(
		(_iconsLeft + iconsWidthForCentering + _iconsRight - width()) / 2,
		0,
		_iconState.max);
	if (animations == ValidateIconAnimations::None) {
		_iconState.x = anim::value(iconsXFinal, iconsXFinal);
		_iconState.animation.stop();
	} else {
		_iconState.x.start(iconsXFinal);
		_iconState.animationStart = crl::now();
		_iconState.animation.start();
	}
	updateSelected();
	update();
}

void StickersListFooter::setSelectedSubicon(
		int newSelected,
		ValidateIconAnimations animations) {
	if (_subiconState.selected == newSelected) {
		return;
	}
	_subiconState.selected = newSelected;
	const auto info = subiconInfo(_subiconState.selected);
	UpdateAnimated(_subiconState.selectionX, info.left, animations);
	UpdateAnimated(_subiconState.selectionWidth, info.width, animations);
	const auto relativeLeft = info.left;
	const auto subiconsWidthForCentering = 2 * relativeLeft + info.width;
	const auto subiconsXFinal = std::clamp(
		(subiconsWidthForCentering - _subiconsWidth) / 2,
		0,
		_subiconState.max);
	if (animations == ValidateIconAnimations::None) {
		_subiconState.x = anim::value(subiconsXFinal, subiconsXFinal);
		_subiconState.animation.stop();
	} else {
		_subiconState.x.start(subiconsXFinal);
		_subiconState.animationStart = crl::now();
		_subiconState.animation.start();
	}
	updateSelected();
	update();
}

void StickersListFooter::processHideFinished() {
	_selected = _pressed = SpecialOver::None;
	_iconState.animation.stop();
	_iconState.animationStart = 0;
	_iconState.x.finish();
	_iconState.selectionX.finish();
	_iconState.selectionWidth.finish();
	_subiconState.animation.stop();
	_subiconState.animationStart = 0;
	_subiconState.x.finish();
	_subiconState.selectionX.finish();
	_subiconState.selectionWidth.finish();
	_horizontal = false;
}

void StickersListFooter::leaveToChildEvent(QEvent *e, QWidget *child) {
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void StickersListFooter::setLoading(bool loading) {
	if (_searchCancel) {
		_searchCancel->setLoadingAnimation(loading);
	}
}

void StickersListFooter::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_searchButtonVisible) {
		paintSearchIcon(p);
	}
	if (_icons.empty() || _searchShown) {
		return;
	}

	if (_settingsButtonVisible && !hasOnlyFeaturedSets()) {
		paintStickerSettingsIcon(p);
	}

	auto clip = QRect(
		_iconsLeft,
		_iconsTop,
		width() - _iconsLeft - _iconsRight,
		st::emojiFooterHeight);
	if (rtl()) {
		clip.moveLeft(width() - _iconsLeft - clip.width());
	}
	p.setClipRect(clip);

	if (!_barSelection) {
		paintSelectionBg(p);
	}

	const auto now = crl::now();
	const auto paused = _controller->isGifPausedAtLeastFor(
		Window::GifPauseReason::SavedGifs);
	enumerateVisibleIcons([&](const IconInfo &info) {
		paintSetIcon(p, info, now, paused);
	});

	if (_barSelection) {
		paintSelectionBar(p);
	}
	paintLeftRightFading(p);
}

void StickersListFooter::paintSelectionBg(Painter &p) const {
	auto selxrel = _iconsLeft + qRound(_iconState.selectionX.current());
	auto selx = selxrel - qRound(_iconState.x.current());
	const auto selw = qRound(_iconState.selectionWidth.current());
	if (rtl()) {
		selx = width() - selx - selw;
	}
	const auto skip = st::emojiIconSelectSkip;
	const auto sely = _iconsTop
		+ (st::emojiFooterHeight - st::stickerIconWidth) / 2;
	const auto selh = st::stickerIconWidth;
	const auto rect = QRect(selx, sely, selw, selh);
	const auto fill = rect.marginsRemoved({ skip, skip, skip, skip });
	_selectionBg.paint(p, fill);
}

void StickersListFooter::paintSelectionBar(Painter &p) const {
	auto selxrel = _iconsLeft + qRound(_iconState.selectionX.current());
	auto selx = selxrel - qRound(_iconState.x.current());
	const auto selw = qRound(_iconState.selectionWidth.current());
	if (rtl()) {
		selx = width() - selx - selw;
	}
	p.fillRect(
		selx,
		_iconsTop + st::emojiFooterHeight - st::stickerIconPadding,
		selw,
		st::stickerIconSel,
		st::stickerIconSelColor);
}

void StickersListFooter::paintLeftRightFading(Painter &p) const {
	auto o_left = std::clamp(
		_iconState.x.current() / st::stickerIconLeft.width(),
		0.,
		1.);
	if (o_left > 0) {
		p.setOpacity(o_left);
		st::stickerIconLeft.fill(p, style::rtlrect(_iconsLeft, _iconsTop, st::stickerIconLeft.width(), st::emojiFooterHeight, width()));
		p.setOpacity(1.);
	}
	auto o_right = std::clamp(
		(_iconState.max - _iconState.x.current()) / st::stickerIconRight.width(),
		0.,
		1.);
	if (o_right > 0) {
		p.setOpacity(o_right);
		st::stickerIconRight.fill(
			p,
			style::rtlrect(
				width() - _iconsRight - st::stickerIconRight.width(),
				_iconsTop,
				st::stickerIconRight.width(),
				st::emojiFooterHeight, width()));
		p.setOpacity(1.);
	}
}

void StickersListFooter::resizeEvent(QResizeEvent *e) {
	if (_searchField) {
		resizeSearchControls();
	}
	refreshIconsGeometry(ValidateIconAnimations::None);
	refreshSubiconsGeometry();
}

void StickersListFooter::resizeSearchControls() {
	Expects(_searchField != nullptr);
	Expects(_searchCancel != nullptr);

	const auto fieldWidth = width()
		- st::gifsSearchFieldPosition.x()
		- st::gifsSearchCancelPosition.x()
		- st::gifsSearchCancel.width;
	_searchField->resizeToWidth(fieldWidth);
	_searchField->moveToLeft(st::gifsSearchFieldPosition.x(), st::gifsSearchFieldPosition.y());
	_searchCancel->moveToRight(st::gifsSearchCancelPosition.x(), st::gifsSearchCancelPosition.y());
}

rpl::producer<uint64> StickersListFooter::setChosen() const {
	return _setChosen.events();
}

rpl::producer<> StickersListFooter::openSettingsRequests() const {
	return _openSettingsRequests.events();
}

rpl::producer<StickersListFooter::SearchRequest> StickersListFooter::searchRequests() const {
	return _searchRequests.events();
}

void StickersListFooter::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (_selected == SpecialOver::Settings) {
		_openSettingsRequests.fire({});
	} else if (_selected == SpecialOver::Search) {
		toggleSearch(true);
	} else {
		_pressed = _selected;
		_iconsMouseDown = _iconsMousePos;
		_iconState.draggingStartX = qRound(_iconState.x.current());
		_subiconState.draggingStartX = qRound(_subiconState.x.current());
	}
}

void StickersListFooter::mouseMoveEvent(QMouseEvent *e) {
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (!_iconState.dragging
		&& !_icons.empty()
		&& v::is<IconId>(_pressed)) {
		if ((_iconsMousePos - _iconsMouseDown).manhattanLength() >= QApplication::startDragDistance()) {
			const auto &icon = _icons[v::get<IconId>(_pressed).index];
			(icon.setId == AllEmojiSectionSetId()
				? _subiconState
				: _iconState).dragging = true;
		}
	}
	checkDragging(_iconState);
	checkDragging(_subiconState);
}

void StickersListFooter::checkDragging(ScrollState &state) {
	if (state.dragging) {
		const auto newX = std::clamp(
			(rtl() ? -1 : 1) * (_iconsMouseDown.x() - _iconsMousePos.x())
				+ state.draggingStartX,
			0,
			state.max);
		if (newX != qRound(state.x.current())) {
			state.x = anim::value(newX, newX);
			state.animationStart = 0;
			state.animation.stop();
			update();
		}
	}
}

void StickersListFooter::mouseReleaseEvent(QMouseEvent *e) {
	if (_icons.empty()) {
		return;
	}

	const auto wasDown = std::exchange(_pressed, SpecialOver::None);

	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	if (finishDragging()) {
		return;
	}

	updateSelected();
	if (wasDown == _selected) {
		if (const auto icon = std::get_if<IconId>(&_selected)) {
			const auto info = iconInfo(icon->index);
			_iconState.selectionX = anim::value(info.left, info.left);
			_iconState.selectionWidth = anim::value(info.width, info.width);
			const auto subinfo = subiconInfo(icon->subindex);
			_subiconState.selectionX = anim::value(
				subinfo.left,
				subinfo.left);
			_subiconState.selectionWidth = anim::value(
				subinfo.width,
				subinfo.width);
			const auto setId = _icons[icon->index].setId;
			_setChosen.fire_copy((setId == AllEmojiSectionSetId())
				? EmojiSectionSetId(
					EmojiSection(int(EmojiSection::People) + icon->subindex))
				: setId);
		}
	}
}

bool StickersListFooter::finishDragging() {
	const auto icon = finishDragging(_iconState);
	const auto subicon = finishDragging(_subiconState);
	return icon || subicon;
}

bool StickersListFooter::finishDragging(ScrollState &state) {
	if (!state.dragging) {
		return false;
	}
	const auto newX = std::clamp(
		state.draggingStartX + _iconsMouseDown.x() - _iconsMousePos.x(),
		0,
		state.max);
	if (newX != qRound(state.x.current())) {
		state.x = anim::value(newX, newX);
		state.animationStart = 0;
		state.animation.stop();
		update();
	}
	state.dragging = false;
	updateSelected();
	return true;
}

bool StickersListFooter::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {
	} else if (e->type() == QEvent::Wheel) {
		if (!_icons.empty()
			&& v::is<IconId>(_selected)
			&& (_pressed == SpecialOver::None)) {
			scrollByWheelEvent(static_cast<QWheelEvent*>(e));
		}
	}
	return InnerFooter::eventHook(e);
}

void StickersListFooter::scrollByWheelEvent(
		not_null<QWheelEvent*> e) {
	auto horizontal = (e->angleDelta().x() != 0);
	auto vertical = (e->angleDelta().y() != 0);
	if (!horizontal && !vertical) {
		return;
	}
	if (horizontal) {
		_horizontal = true;
	}
	auto delta = horizontal
		? ((rtl() ? -1 : 1) * (e->pixelDelta().x()
			? e->pixelDelta().x()
			: e->angleDelta().x()))
		: (e->pixelDelta().y()
			? e->pixelDelta().y()
			: e->angleDelta().y());
	const auto use = [&](ScrollState &state) {
		const auto now = qRound(state.x.current());
		const auto used = now - delta;
		const auto next = std::clamp(used, 0, state.max);
		delta = next - used;
		if (next != now) {
			state.x = anim::value(next, next);
			state.animationStart = 0;
			state.animation.stop();
			updateSelected();
			update();
		}
	};
	const auto index = v::get<IconId>(_selected).index;
	if (_icons[index].setId == AllEmojiSectionSetId()) {
		use(_subiconState);
	}
	use(_iconState);
}

void StickersListFooter::clipCallback(
		Media::Clip::Notification notification,
		uint64 setId) {
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		enumerateIcons([&](const IconInfo &info) {
			auto &icon = _icons[info.index];
			if (icon.setId != setId || !icon.webm) {
				return true;
			} else if (icon.webm->state() == State::Error) {
				icon.webm.setBad();
			} else if (!info.visible) {
				icon.webm = nullptr;
			} else if (icon.webm->ready() && !icon.webm->started()) {
				icon.webm->start({
					.frame = { icon.pixw, icon.pixh },
					.keepAlpha = true,
				});
			}
			updateSetIconAt(info.adjustedLeft);
			return true;
		});
	} break;

	case Notification::Repaint:
		updateSetIcon(setId);
		break;
	}
}

void StickersListFooter::updateSelected() {
	if (_pressed != SpecialOver::None) {
		return;
	}

	auto p = mapFromGlobal(_iconsMousePos);
	auto x = p.x(), y = p.y();
	if (rtl()) x = width() - x;
	const auto settingsLeft = width() - _iconsRight;
	const auto searchLeft = _iconsLeft - st::stickerIconWidth;
	auto newOver = OverState(SpecialOver::None);
	if (_searchButtonVisible
		&& x >= searchLeft
		&& x < searchLeft + st::stickerIconWidth
		&& y >= _iconsTop
		&& y < _iconsTop + st::emojiFooterHeight) {
		newOver = SpecialOver::Search;
	} else if (_settingsButtonVisible
		&& x >= settingsLeft
		&& x < settingsLeft + st::stickerIconWidth
		&& y >= _iconsTop
		&& y < _iconsTop + st::emojiFooterHeight) {
		if (!_icons.empty() && !hasOnlyFeaturedSets()) {
			newOver = SpecialOver::Settings;
		}
	} else if (!_icons.empty()) {
		if (y >= _iconsTop
			&& y < _iconsTop + st::emojiFooterHeight
			&& x >= _iconsLeft
			&& x < width() - _iconsRight) {
			enumerateIcons([&](const IconInfo &info) {
				if (x >= info.adjustedLeft
					&& x < info.adjustedLeft + info.width) {
					newOver = IconId{ .index = info.index };
					if (_icons[info.index].setId == AllEmojiSectionSetId()) {
						const auto subx = (x - info.adjustedLeft);
						enumerateSubicons([&](const IconInfo &info) {
							if (subx >= info.adjustedLeft
								&& subx < info.adjustedLeft + info.width) {
								v::get<IconId>(newOver).subindex = info.index;
								return false;
							}
							return true;
						});
					}
					return false;
				}
				return true;
			});
		}
	}
	if (newOver != _selected) {
		if (newOver == SpecialOver::None) {
			setCursor(style::cur_default);
		} else if (_selected == SpecialOver::None) {
			setCursor(style::cur_pointer);
		}
		_selected = newOver;
	}
}

auto StickersListFooter::getLottieRenderer()
-> std::shared_ptr<Lottie::FrameRenderer> {
	if (auto result = _lottieRenderer.lock()) {
		return result;
	}
	auto result = Lottie::MakeFrameRenderer();
	_lottieRenderer = result;
	return result;
}

void StickersListFooter::refreshIcons(
		std::vector<StickerIcon> icons,
		Fn<std::shared_ptr<Lottie::FrameRenderer>()> renderer,
		ValidateIconAnimations animations) {
	_renderer = renderer
		? std::move(renderer)
		: [=] { return getLottieRenderer(); };

	auto indices = base::flat_map<uint64, int>();
	indices.reserve(_icons.size());
	auto index = 0;
	for (const auto &entry : _icons) {
		indices.emplace(entry.setId, index++);
	}

	for (auto &now : icons) {
		if (const auto i = indices.find(now.setId); i != end(indices)) {
			auto &was = _icons[i->second];
			if (now.sticker == was.sticker) {
				now.webm = std::move(was.webm);
				now.lottie = std::move(was.lottie);
				now.lifetime = std::move(was.lifetime);
				now.savedFrame = std::move(was.savedFrame);
			}
		}
	}

	_icons = std::move(icons);
	refreshIconsGeometry(animations);
}

void StickersListFooter::refreshScrollableDimensions() {
	const auto shift = int(base::SafeRound(_iconState.x.current()));
	const auto &last = iconInfo(_icons.size() - 1);
	_iconState.max = std::max(
		last.left + last.width + _iconsRight - width(),
		0);
	if (_iconState.x.current() > _iconState.max) {
		_iconState.x = anim::value(_iconState.max, _iconState.max);
	}
}

void StickersListFooter::refreshIconsGeometry(
		ValidateIconAnimations animations) {
	_selected = _pressed = SpecialOver::None;
	_iconState.x.finish();
	_iconState.selectionX.finish();
	_iconState.selectionWidth.finish();
	_iconState.animationStart = 0;
	_iconState.animation.stop();
	refreshScrollableDimensions();
	updateSelected();
	validateSelectedIcon(_activeByScrollId, animations);
	update();
}

void StickersListFooter::refreshSubiconsGeometry() {
	using Section = Ui::Emoji::Section;
	_subiconState.x.finish();
	_subiconState.selectionX.finish();
	_subiconState.selectionWidth.finish();
	_subiconState.animationStart = 0;
	_subiconState.animation.stop();
	const auto single = st::stickerIconWidth;
	const auto half = single / 2;
	const auto count = int(Section::Symbols) - int(Section::Recent);
	const auto widthMax = count * single;
	const auto widthMin = 4 * single + half;
	const auto collapsedWidth = int(_icons.size()) * single;
	_subiconsWidth = std::clamp(
		width() + single - collapsedWidth,
		widthMin,
		widthMax);
	if (_subiconsWidth < widthMax) {
		_subiconsWidth = ((_subiconsWidth - half) / single) * single + half;
	}
	const auto &last = subiconInfo(int(Section::Symbols));
	_subiconState.max = std::max(
		widthMax - _subiconsWidth,
		0);
	if (_subiconState.x.current() > _subiconState.max) {
		_subiconState.x = anim::value(_subiconState.max, _subiconState.max);
	}
	updateEmojiWidthCallback();
}

bool StickersListFooter::hasOnlyFeaturedSets() const {
	return (_icons.size() == 1)
		&& (_icons[0].setId == Data::Stickers::FeaturedSetId);
}

void StickersListFooter::paintStickerSettingsIcon(Painter &p) const {
	const auto settingsLeft = width() - _iconsRight;
	st::stickersSettings.paint(
		p,
		settingsLeft
			+ (st::stickerIconWidth - st::stickersSettings.width()) / 2,
		_iconsTop + st::emojiCategory.iconPosition.y(),
		width());
}

void StickersListFooter::paintSearchIcon(Painter &p) const {
	const auto searchLeft = _iconsLeft - st::stickerIconWidth;
	st::stickersSearch.paint(
		p,
		searchLeft + (st::stickerIconWidth - st::stickersSearch.width()) / 2,
		_iconsTop + st::emojiCategory.iconPosition.y(),
		width());
}

void StickersListFooter::validateIconLottieAnimation(
		const StickerIcon &icon) {
	icon.ensureMediaCreated();
	if (icon.lottie
		|| !icon.sticker
		|| !HasLottieThumbnail(
			icon.set ? icon.set->flags : Data::StickersSetFlags(),
			icon.thumbnailMedia.get(),
			icon.stickerMedia.get())) {
		return;
	}
	auto player = LottieThumbnail(
		icon.thumbnailMedia.get(),
		icon.stickerMedia.get(),
		StickerLottieSize::StickersFooter,
		QSize(icon.pixw, icon.pixh) * cIntRetinaFactor(),
		_renderer());
	if (!player) {
		return;
	}
	icon.lottie = std::move(player);

	const auto id = icon.setId;
	icon.lottie->updates(
	) | rpl::start_with_next([=] {
		updateSetIcon(id);
	}, icon.lifetime);
}

void StickersListFooter::validateIconWebmAnimation(
		const StickerIcon &icon) {
	icon.ensureMediaCreated();
	if (icon.webm
		|| !icon.sticker
		|| !HasWebmThumbnail(
			icon.set ? icon.set->flags : Data::StickersSetFlags(),
			icon.thumbnailMedia.get(),
			icon.stickerMedia.get())) {
		return;
	}
	const auto id = icon.setId;
	auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification, id);
	};
	icon.webm = WebmThumbnail(
		icon.thumbnailMedia.get(),
		icon.stickerMedia.get(),
		std::move(callback));
}

void StickersListFooter::validateIconAnimation(
		const StickerIcon &icon) {
	validateIconWebmAnimation(icon);
	validateIconLottieAnimation(icon);
}

void StickersListFooter::updateSetIcon(uint64 setId) {
	enumerateVisibleIcons([&](const IconInfo &info) {
		if (_icons[info.index].setId != setId) {
			return;
		}
		updateSetIconAt(info.adjustedLeft);
	});
}

void StickersListFooter::updateSetIconAt(int left) {
	update(left, _iconsTop, st::stickerIconWidth, st::emojiFooterHeight);
}

void StickersListFooter::paintSetIcon(
		Painter &p,
		const IconInfo &info,
		crl::time now,
		bool paused) const {
	const auto &icon = _icons[info.index];
	if (icon.sticker) {
		icon.ensureMediaCreated();
		const_cast<StickersListFooter*>(this)->validateIconAnimation(icon);
		const auto origin = icon.sticker->stickerSetOrigin();
		const auto thumb = icon.thumbnailMedia
			? icon.thumbnailMedia->image()
			: icon.stickerMedia
			? icon.stickerMedia->thumbnail()
			: nullptr;
		const auto x = info.adjustedLeft
			+ (st::stickerIconWidth - icon.pixw) / 2;
		const auto y = _iconsTop + (st::emojiFooterHeight - icon.pixh) / 2;
		if (icon.lottie && icon.lottie->ready()) {
			const auto frame = icon.lottie->frame();
			const auto size = frame.size() / cIntRetinaFactor();
			if (icon.savedFrame.isNull()) {
				icon.savedFrame = QPixmap::fromImage(frame, Qt::ColorOnly);
				icon.savedFrame.setDevicePixelRatio(cRetinaFactor());
			}
			p.drawImage(
				QRect(
					(info.adjustedLeft
						+ (st::stickerIconWidth - size.width()) / 2),
					_iconsTop + (st::emojiFooterHeight - size.height()) / 2,
					size.width(),
					size.height()),
				frame);
			if (!paused) {
				icon.lottie->markFrameShown();
			}
		} else if (icon.webm && icon.webm->started()) {
			const auto frame = icon.webm->current(
				{ .frame = { icon.pixw, icon.pixh }, .keepAlpha = true },
				paused ? 0 : now);
			if (icon.savedFrame.isNull()) {
				icon.savedFrame = frame;
				icon.savedFrame.setDevicePixelRatio(cRetinaFactor());
			}
			p.drawPixmapLeft(x, y, width(), frame);
		} else if (!icon.savedFrame.isNull() || thumb) {
			const auto pixmap = !icon.savedFrame.isNull()
				? icon.savedFrame
				: (!icon.lottie && thumb)
				? thumb->pix(icon.pixw, icon.pixh)
				: QPixmap();
			if (pixmap.isNull()) {
				return;
			} else if (icon.savedFrame.isNull()) {
				icon.savedFrame = pixmap;
			}
			p.drawPixmapLeft(x, y, width(), pixmap);
		}
	} else if (icon.megagroup) {
		const auto size = st::stickerGroupCategorySize;
		icon.megagroup->paintUserpicLeft(
			p,
			icon.megagroupUserpic,
			info.adjustedLeft + (st::stickerIconWidth - size) / 2,
			_iconsTop + (st::emojiFooterHeight - size) / 2,
			width(),
			st::stickerGroupCategorySize);
	} else if (icon.setId == Data::Stickers::PremiumSetId) {
		validatePremiumIcon();
		const auto size = st::stickersPremium.size();
		p.drawImage(
			info.adjustedLeft + (st::stickerIconWidth - size.width()) / 2,
			_iconsTop + (st::emojiFooterHeight - size.height()) / 2,
			_premiumIcon);
	} else {
		using Section = Ui::Emoji::Section;
		const auto sectionIcon = [&](Section section, bool active) {
			const auto icons = std::array{
				&st::emojiRecent,
				&st::emojiRecentActive,
				&st::emojiPeople,
				&st::emojiPeopleActive,
				&st::emojiNature,
				&st::emojiNatureActive,
				&st::emojiFood,
				&st::emojiFoodActive,
				&st::emojiActivity,
				&st::emojiActivityActive,
				&st::emojiTravel,
				&st::emojiTravelActive,
				&st::emojiObjects,
				&st::emojiObjectsActive,
				&st::emojiSymbols,
				&st::emojiSymbolsActive,
			};
			const auto index = int(section) * 2 + (active ? 1 : 0);

			Assert(index >= 0 && index < icons.size());
			return icons[index];
		};
		const auto left = info.adjustedLeft;
		const auto paintOne = [&](int left, const style::icon *icon) {
			icon->paint(
				p,
				left + (st::stickerIconWidth - icon->width()) / 2,
				_iconsTop + (st::emojiFooterHeight - icon->height()) / 2,
				width());
		};
		if (_icons[info.index].setId == AllEmojiSectionSetId()
			&& info.width > st::stickerIconWidth) {
			const auto skip = st::emojiIconSelectSkip;
			p.save();
			p.setClipRect(
				left + skip,
				_iconsTop,
				info.width - 2 * skip,
				st::emojiFooterHeight,
				Qt::IntersectClip);
			enumerateSubicons([&](const IconInfo &info) {
				if (info.visible) {
					paintOne(
						left + info.adjustedLeft,
						sectionIcon(
							Section(int(Section::People) + info.index),
							(_subiconState.selected == info.index)));
				}
				return true;
			});
			p.restore();
		} else {
			paintOne(left, [&] {
				if (icon.setId == Data::Stickers::FeaturedSetId) {
					const auto session = &_controller->session();
					return session->data().stickers().featuredSetsUnreadCount()
						? &st::stickersTrendingUnread
						: &st::stickersTrending;
					//} else if (setId == Stickers::FavedSetId) {
					//	return &st::stickersFaved;
				} else if (icon.setId == AllEmojiSectionSetId()) {
					return &st::emojiPeople;
				} else if (const auto section = SetIdEmojiSection(icon.setId)) {
					return sectionIcon(*section, false);
				}
				return &st::emojiRecent;
			}());
		}
	}
}

} // namespace ChatHelpers
