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

} // namespace

uint64 EmojiSectionSetId(EmojiSection section) {
	Expects(section >= EmojiSection::Recent
		&& section <= EmojiSection::Symbols);

	return kEmojiSectionSetIdBase + static_cast<uint64>(section);
}

std::optional<EmojiSection> SetIdEmojiSection(uint64 id) {
	const auto base = EmojiSectionSetId(EmojiSection::Recent);
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

StickersListFooter::StickersListFooter(Descriptor &&descriptor)
: InnerFooter(descriptor.parent)
, _controller(descriptor.controller)
, _searchButtonVisible(descriptor.searchButtonVisible)
, _settingsButtonVisible(descriptor.settingsButtonVisible)
, _iconsAnimation([=](crl::time now) {
	return iconsAnimationCallback(now);
}) {
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
	const auto iconsX = qRound(_iconsX.current());
	enumerateIcons([&](const IconInfo &info) {
		auto &icon = _icons[info.index];
		icon.webm = nullptr;
		icon.lottie = nullptr;
		icon.lifetime.destroy();
		icon.stickerMedia = nullptr;
		if (!info.visible) {
			icon.savedFrame = QPixmap();
		}
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
		Fn<void(const IconInfo &)> callback) {
	auto iconsX = qRound(_iconsX.current());
	auto x = _iconsLeft - (iconsX % st::stickerIconWidth);
	auto first = floorclamp(iconsX, st::stickerIconWidth, 0, _icons.size());
	auto last = ceilclamp(
		iconsX + width(),
		st::stickerIconWidth,
		0,
		_icons.size());
	for (auto index = first; index != last; ++index) {
		callback({ .index = index, .left = x, .visible = true });
		x += st::stickerIconWidth;
	}
}

void StickersListFooter::enumerateIcons(
		Fn<void(const IconInfo &)> callback) {
	auto iconsX = qRound(_iconsX.current());
	auto x = _iconsLeft - (iconsX % st::stickerIconWidth);
	auto first = floorclamp(iconsX, st::stickerIconWidth, 0, _icons.size());
	auto last = ceilclamp(
		iconsX + width(),
		st::stickerIconWidth,
		0,
		_icons.size());
	x -= first * st::stickerIconWidth;
	for (auto i = 0, count = int(_icons.size()); i != count; ++i) {
		const auto visible = (i >= first && i < last);
		callback({ .index = i, .left = x, .visible = visible });
		x += st::stickerIconWidth;
	}
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

	auto favedIconIndex = -1;
	auto newSelected = -1;
	for (auto i = 0, l = int(_icons.size()); i != l; ++i) {
		if (_icons[i].setId == setId
			|| (_icons[i].setId == Data::Stickers::FavedSetId
				&& setId == Data::Stickers::RecentSetId)) {
			newSelected = i;
			break;
		} else if (_icons[i].setId == Data::Stickers::FavedSetId) {
			favedIconIndex = i;
		}
	}
	setSelectedIcon(
		(newSelected >= 0
			? newSelected
			: (favedIconIndex >= 0) ? favedIconIndex : 0),
		animations);
}

void StickersListFooter::setSelectedIcon(
		int newSelected,
		ValidateIconAnimations animations) {
	if (_iconSel == newSelected) {
		return;
	}
	_iconSel = newSelected;
	auto iconSelXFinal = _iconSel * st::stickerIconWidth;
	if (animations == ValidateIconAnimations::Full) {
		_iconSelX.start(iconSelXFinal);
	} else {
		_iconSelX = anim::value(iconSelXFinal, iconSelXFinal);
	}
	auto iconsCountForCentering = (2 * _iconSel + 1);
	auto iconsWidthForCentering = iconsCountForCentering
		* st::stickerIconWidth;
	auto iconsXFinal = std::clamp(
		(_iconsLeft + iconsWidthForCentering + _iconsRight - width()) / 2,
		0,
		_iconsMax);
	if (animations == ValidateIconAnimations::None) {
		_iconsX = anim::value(iconsXFinal, iconsXFinal);
		_iconsAnimation.stop();
	} else {
		_iconsX.start(iconsXFinal);
		_iconsStartAnim = crl::now();
		_iconsAnimation.start();
	}
	updateSelected();
	update();
}

void StickersListFooter::processHideFinished() {
	_iconOver = _iconDown = SpecialOver::None;
	_iconsStartAnim = 0;
	_iconsAnimation.stop();
	_iconsX.finish();
	_iconSelX.finish();
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

	const auto now = crl::now();
	const auto paused = _controller->isGifPausedAtLeastFor(
		Window::GifPauseReason::SavedGifs);
	enumerateVisibleIcons([&](const IconInfo &info) {
		paintSetIcon(p, info, now, paused);
	});

	paintSelectionBar(p);
	paintLeftRightFading(p);
}

void StickersListFooter::paintSelectionBar(Painter &p) const {
	auto selxrel = _iconsLeft + qRound(_iconSelX.current());
	auto selx = selxrel - qRound(_iconsX.current());
	if (rtl()) {
		selx = width() - selx - st::stickerIconWidth;
	}
	p.fillRect(
		selx,
		_iconsTop + st::emojiFooterHeight - st::stickerIconPadding,
		st::stickerIconWidth,
		st::stickerIconSel,
		st::stickerIconSelColor);
}

void StickersListFooter::paintLeftRightFading(Painter &p) const {
	auto o_left = std::clamp(
		_iconsX.current() / st::stickerIconLeft.width(),
		0.,
		1.);
	if (o_left > 0) {
		p.setOpacity(o_left);
		st::stickerIconLeft.fill(p, style::rtlrect(_iconsLeft, _iconsTop, st::stickerIconLeft.width(), st::emojiFooterHeight, width()));
		p.setOpacity(1.);
	}
	auto o_right = std::clamp(
		(_iconsMax - _iconsX.current()) / st::stickerIconRight.width(),
		0.,
		1.);
	if (o_right > 0) {
		p.setOpacity(o_right);
		st::stickerIconRight.fill(p, style::rtlrect(width() - _iconsRight - st::stickerIconRight.width(), _iconsTop, st::stickerIconRight.width(), st::emojiFooterHeight, width()));
		p.setOpacity(1.);
	}
}

void StickersListFooter::resizeEvent(QResizeEvent *e) {
	if (_searchField) {
		resizeSearchControls();
	}
	refreshIconsGeometry(ValidateIconAnimations::None);
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

	if (_iconOver == SpecialOver::Settings) {
		_openSettingsRequests.fire({});
	} else if (_iconOver == SpecialOver::Search) {
		toggleSearch(true);
	} else {
		_iconDown = _iconOver;
		_iconsMouseDown = _iconsMousePos;
		_iconsStartX = qRound(_iconsX.current());
	}
}

void StickersListFooter::mouseMoveEvent(QMouseEvent *e) {
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (!_iconsDragging
		&& !_icons.empty()
		&& v::is<int>(_iconDown)) {
		if ((_iconsMousePos - _iconsMouseDown).manhattanLength() >= QApplication::startDragDistance()) {
			_iconsDragging = true;
		}
	}
	if (_iconsDragging) {
		auto newX = std::clamp(
			(rtl() ? -1 : 1) * (_iconsMouseDown.x() - _iconsMousePos.x())
				+ _iconsStartX,
			0,
			_iconsMax);
		if (newX != qRound(_iconsX.current())) {
			_iconsX = anim::value(newX, newX);
			_iconsStartAnim = 0;
			_iconsAnimation.stop();
			update();
		}
	}
}

void StickersListFooter::mouseReleaseEvent(QMouseEvent *e) {
	if (_icons.empty()) {
		return;
	}

	const auto wasDown = std::exchange(_iconDown, SpecialOver::None);

	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	if (_iconsDragging) {
		finishDragging();
		return;
	}

	updateSelected();
	if (wasDown == _iconOver) {
		if (const auto index = std::get_if<int>(&_iconOver)) {
			_iconSelX = anim::value(
				*index * st::stickerIconWidth,
				*index * st::stickerIconWidth);
			_setChosen.fire_copy(_icons[*index].setId);
		}
	}
}

void StickersListFooter::finishDragging() {
	auto newX = std::clamp(
		_iconsStartX + _iconsMouseDown.x() - _iconsMousePos.x(),
		0,
		_iconsMax);
	if (newX != qRound(_iconsX.current())) {
		_iconsX = anim::value(newX, newX);
		_iconsStartAnim = 0;
		_iconsAnimation.stop();
		update();
	}
	_iconsDragging = false;
	updateSelected();
}

bool StickersListFooter::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {
	} else if (e->type() == QEvent::Wheel) {
		if (!_icons.empty()
			&& v::is<int>(_iconOver)
			&& (_iconDown == SpecialOver::None)) {
			scrollByWheelEvent(static_cast<QWheelEvent*>(e));
		}
	}
	return InnerFooter::eventHook(e);
}

void StickersListFooter::scrollByWheelEvent(
		not_null<QWheelEvent*> e) {
	auto horizontal = (e->angleDelta().x() != 0);
	auto vertical = (e->angleDelta().y() != 0);
	if (horizontal) {
		_horizontal = true;
	}
	auto newX = qRound(_iconsX.current());
	if (/*_horizontal && */horizontal) {
		newX = std::clamp(
			newX - (rtl() ? -1 : 1) * (e->pixelDelta().x()
				? e->pixelDelta().x()
				: e->angleDelta().x()),
			0,
			_iconsMax);
	} else if (/*!_horizontal && */vertical) {
		newX = std::clamp(
			newX - (e->pixelDelta().y()
				? e->pixelDelta().y()
				: e->angleDelta().y()),
			0,
			_iconsMax);
	}
	if (newX != qRound(_iconsX.current())) {
		_iconsX = anim::value(newX, newX);
		_iconsStartAnim = 0;
		_iconsAnimation.stop();
		updateSelected();
		update();
	}
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
				return;
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
			updateSetIconAt(info.left);
		});
	} break;

	case Notification::Repaint:
		updateSetIcon(setId);
		break;
	}
}

void StickersListFooter::updateSelected() {
	if (_iconDown != SpecialOver::None) {
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
			x += qRound(_iconsX.current()) - _iconsLeft;
			if (x < _icons.size() * st::stickerIconWidth) {
				newOver = qFloor(x / st::stickerIconWidth);
			}
		}
	}
	if (newOver != _iconOver) {
		if (newOver == SpecialOver::None) {
			setCursor(style::cur_default);
		} else if (_iconOver == SpecialOver::None) {
			setCursor(style::cur_pointer);
		}
		_iconOver = newOver;
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

void StickersListFooter::refreshIconsGeometry(
		ValidateIconAnimations animations) {
	_iconOver = _iconDown = SpecialOver::None;
	_iconsX.finish();
	_iconSelX.finish();
	_iconsStartAnim = 0;
	_iconsAnimation.stop();
	_iconsMax = std::max(
		_iconsLeft + int(_icons.size()) * st::stickerIconWidth + _iconsRight - width(),
		0);
	if (_iconsX.current() > _iconsMax) {
		_iconsX = anim::value(_iconsMax, _iconsMax);
	}
	updateSelected();
	validateSelectedIcon(_activeByScrollId, animations);
	update();
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
		QSize(
			st::stickerIconWidth - 2 * st::stickerIconPadding,
			st::emojiFooterHeight - 2 * st::stickerIconPadding
		) * cIntRetinaFactor(),
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
		updateSetIconAt(info.left);
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
		const auto x = info.left + (st::stickerIconWidth - icon.pixw) / 2;
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
					info.left + (st::stickerIconWidth - size.width()) / 2,
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
			info.left + (st::stickerIconWidth - size) / 2,
			_iconsTop + (st::emojiFooterHeight - size) / 2,
			width(),
			st::stickerGroupCategorySize);
	} else if (icon.setId == Data::Stickers::PremiumSetId) {
		validatePremiumIcon();
		const auto size = st::stickersPremium.size();
		p.drawImage(
			info.left + (st::stickerIconWidth - size.width()) / 2,
			_iconsTop + (st::emojiFooterHeight - size.height()) / 2,
			_premiumIcon);
	} else {
		const auto paintedIcon = [&] {
			if (icon.setId == Data::Stickers::FeaturedSetId) {
				const auto session = &_controller->session();
				return session->data().stickers().featuredSetsUnreadCount()
					? &st::stickersTrendingUnread
					: &st::stickersTrending;
			//} else if (setId == Stickers::FavedSetId) {
			//	return &st::stickersFaved;
			} else if (const auto section = SetIdEmojiSection(icon.setId)) {
				using Section = Ui::Emoji::Section;
				switch (*section) {
				case Section::Recent: return &st::emojiRecent;
				case Section::People: return &st::emojiPeople;
				case Section::Nature: return &st::emojiNature;
				case Section::Food: return &st::emojiFood;
				case Section::Activity: return &st::emojiActivity;
				case Section::Travel: return &st::emojiTravel;
				case Section::Objects: return &st::emojiObjects;
				case Section::Symbols: return &st::emojiSymbols;
				}
				Unexpected("Section in SetIdEmojiSection result.");
			}
			return &st::emojiRecent;
		}();
		paintedIcon->paint(
			p,
			info.left + (st::stickerIconWidth - paintedIcon->width()) / 2,
			_iconsTop + (st::emojiFooterHeight - paintedIcon->height()) / 2,
			width());
	}
}

bool StickersListFooter::iconsAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::stickerIconMove;
	}
	if (_iconsStartAnim) {
		const auto dt = (now - _iconsStartAnim) / float64(st::stickerIconMove);
		if (dt >= 1.) {
			_iconsStartAnim = 0;
			_iconsX.finish();
			_iconSelX.finish();
		} else {
			_iconsX.update(dt, anim::linear);
			_iconSelX.update(dt, anim::linear);
		}
	}

	update();

	return (_iconsStartAnim != 0);
}

} // namespace ChatHelpers
