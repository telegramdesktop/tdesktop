/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_list_widget.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "chat_helpers/stickers.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "dialogs/dialogs_layout.h"
#include "boxes/sticker_set_box.h"
#include "boxes/stickers_box.h"
#include "boxes/confirm_box.h"
#include "auth_session.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace ChatHelpers {
namespace {

constexpr auto kInlineItemsMaxPerRow = 5;
constexpr auto kSearchRequestDelay = 400;
constexpr auto kRecentDisplayLimit = 20;

bool SetInMyList(MTPDstickerSet::Flags flags) {
	return (flags & MTPDstickerSet::Flag::f_installed_date)
		&& !(flags & MTPDstickerSet::Flag::f_archived);
}

} // namespace

struct StickerIcon {
	StickerIcon(uint64 setId) : setId(setId) {
	}
	StickerIcon(uint64 setId, DocumentData *sticker, int pixw, int pixh)
	: setId(setId)
	, sticker(sticker)
	, pixw(pixw)
	, pixh(pixh) {
	}
	uint64 setId = 0;
	DocumentData *sticker = nullptr;
	ChannelData *megagroup = nullptr;
	int pixw = 0;
	int pixh = 0;

};

class StickersListWidget::Footer : public TabbedSelector::InnerFooter, private base::Subscriber {
public:
	Footer(not_null<StickersListWidget*> parent);

	void preloadImages();
	void validateSelectedIcon(uint64 setId, ValidateIconAnimations animations);
	void refreshIcons(ValidateIconAnimations animations);
	bool hasOnlyFeaturedSets() const;

	void leaveToChildEvent(QEvent *e, QWidget *child) override;

	void stealFocus();
	void returnFocus();
	void setLoading(bool loading);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	bool event(QEvent *e) override;

	void processHideFinished() override;

private:
	enum class SpecialOver {
		None,
		Search,
		Settings,
	};
	using OverState = base::variant<SpecialOver, int>;

	template <typename Callback>
	void enumerateVisibleIcons(Callback callback);

	void step_icons(TimeMs ms, bool timer);
	void setSelectedIcon(
		int newSelected,
		ValidateIconAnimations animations);

	void updateSelected();
	void finishDragging();
	void paintStickerSettingsIcon(Painter &p) const;
	void paintSearchIcon(Painter &p) const;
	void paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const;
	void paintSetIcon(Painter &p, const StickerIcon &icon, int x) const;
	void paintSelectionBar(Painter &p) const;
	void paintLeftRightFading(Painter &p) const;

	void initSearch();
	void toggleSearch(bool visible);
	void resizeSearchControls();
	void scrollByWheelEvent(not_null<QWheelEvent*> e);

	not_null<StickersListWidget*> _pan;

	static constexpr auto kVisibleIconsCount = 8;

	QList<StickerIcon> _icons;
	OverState _iconOver = SpecialOver::None;
	int _iconSel = 0;
	OverState _iconDown = SpecialOver::None;
	bool _iconsDragging = false;
	BasicAnimation _a_icons;
	QPoint _iconsMousePos, _iconsMouseDown;
	int _iconsLeft = 0;
	int _iconsRight = 0;
	int _iconsTop = 0;
	int _iconsStartX = 0;
	int _iconsMax = 0;
	anim::value _iconsX;
	anim::value _iconSelX;
	TimeMs _iconsStartAnim = 0;

	bool _horizontal = false;

	bool _searchShown = false;
	object_ptr<Ui::InputField> _searchField = { nullptr };
	object_ptr<Ui::CrossButton> _searchCancel = { nullptr };
	QPointer<QWidget> _focusTakenFrom;

};

StickersListWidget::Set::Set(
	uint64 id,
	MTPDstickerSet::Flags flags,
	const QString &title,
	const QString &shortName,
	bool externalLayout,
	int count,
	const Stickers::Pack &pack)
: id(id)
, flags(flags)
, title(title)
, shortName(shortName)
, pack(pack)
, externalLayout(externalLayout)
, count(count) {
}

StickersListWidget::Set::Set(Set &&other) = default;
StickersListWidget::Set &StickersListWidget::Set::operator=(
	Set &&other) = default;
StickersListWidget::Set::~Set() = default;

StickersListWidget::Footer::Footer(not_null<StickersListWidget*> parent) : InnerFooter(parent)
, _pan(parent)
, _a_icons(animation(this, &Footer::step_icons)) {
	setMouseTracking(true);

	_iconsLeft = _iconsRight = st::emojiCategorySkip + st::stickerIconWidth;

	subscribe(Auth().downloaderTaskFinished(), [this] {
		update();
	});
}

void StickersListWidget::Footer::initSearch() {
	_searchField.create(
		this,
		st::gifsSearchField,
		langFactory(lng_stickers_search_sets));
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
	connect(_searchField, &Ui::InputField::submitted, this, [this](bool ctrlShiftEnter) {
		_pan->sendSearchRequest();
	});
	connect(_searchField, &Ui::InputField::cancelled, this, cancelSearch);
	connect(_searchField, &Ui::InputField::changed, this, [this] {
		_pan->searchForSets(_searchField->getLastText());
	});
	_searchCancel->setClickedCallback(cancelSearch);

	resizeSearchControls();
}

void StickersListWidget::Footer::toggleSearch(bool visible) {
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

void StickersListWidget::Footer::stealFocus() {
	if (_searchField) {
		if (!_focusTakenFrom) {
			_focusTakenFrom = QApplication::focusWidget();
		}
		_searchField->setFocus();
	}
}

void StickersListWidget::Footer::returnFocus() {
	if (_searchField && _focusTakenFrom) {
		if (_searchField->hasFocus()) {
			_focusTakenFrom->setFocus();
		}
		_focusTakenFrom = nullptr;
	}
}

template <typename Callback>
void StickersListWidget::Footer::enumerateVisibleIcons(Callback callback) {
	auto iconsX = qRound(_iconsX.current());
	auto index = iconsX / st::stickerIconWidth;
	auto x = _iconsLeft - (iconsX % st::stickerIconWidth);
	auto first = floorclamp(iconsX, st::stickerIconWidth, 0, _icons.size());
	auto last = ceilclamp(iconsX + width(), st::stickerIconWidth, 0, _icons.size());
	for (auto index = first; index != last; ++index) {
		callback(_icons[index], x);
		x += st::stickerIconWidth;
	}
}

void StickersListWidget::Footer::preloadImages() {
	enumerateVisibleIcons([this](const StickerIcon &icon, int x) {
		if (auto sticker = icon.sticker) {
			sticker->thumb->load();
		}
	});
}

void StickersListWidget::Footer::validateSelectedIcon(
		uint64 setId,
		ValidateIconAnimations animations) {
	auto newSelected = 0;
	for (auto i = 0, l = _icons.size(); i != l; ++i) {
		if (_icons[i].setId == setId) {
			newSelected = i;
			break;
		}
	}
	setSelectedIcon(newSelected, animations);
}

void StickersListWidget::Footer::setSelectedIcon(
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
	auto iconsXFinal = snap(
		(_iconsLeft + iconsWidthForCentering + _iconsRight - width()) / 2,
		0,
		_iconsMax);
	if (animations == ValidateIconAnimations::None) {
		_iconsX = anim::value(iconsXFinal, iconsXFinal);
		_a_icons.stop();
	} else {
		_iconsX.start(iconsXFinal);
		_iconsStartAnim = getms();
		_a_icons.start();
	}
	updateSelected();
	update();
}

void StickersListWidget::Footer::processHideFinished() {
	_iconOver = _iconDown = SpecialOver::None;
	_iconsStartAnim = 0;
	_a_icons.stop();
	_iconsX.finish();
	_iconSelX.finish();
	_horizontal = false;
}

void StickersListWidget::Footer::leaveToChildEvent(QEvent *e, QWidget *child) {
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void StickersListWidget::Footer::setLoading(bool loading) {
	if (_searchCancel) {
		_searchCancel->setLoadingAnimation(loading);
	}
}

void StickersListWidget::Footer::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintSearchIcon(p);

	if (_icons.isEmpty() || _searchShown) {
		return;
	}

	if (!hasOnlyFeaturedSets()) {
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

	enumerateVisibleIcons([&](const StickerIcon &icon, int x) {
		paintSetIcon(p, icon, x);
	});

	paintSelectionBar(p);
	paintLeftRightFading(p);
}

void StickersListWidget::Footer::paintSelectionBar(Painter &p) const {
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

void StickersListWidget::Footer::paintLeftRightFading(Painter &p) const {
	auto o_left = snap(_iconsX.current() / st::stickerIconLeft.width(), 0., 1.);
	if (o_left > 0) {
		p.setOpacity(o_left);
		st::stickerIconLeft.fill(p, rtlrect(_iconsLeft, _iconsTop, st::stickerIconLeft.width(), st::emojiFooterHeight, width()));
		p.setOpacity(1.);
	}
	auto o_right = snap((_iconsMax - _iconsX.current()) / st::stickerIconRight.width(), 0., 1.);
	if (o_right > 0) {
		p.setOpacity(o_right);
		st::stickerIconRight.fill(p, rtlrect(width() - _iconsRight - st::stickerIconRight.width(), _iconsTop, st::stickerIconRight.width(), st::emojiFooterHeight, width()));
		p.setOpacity(1.);
	}
}

void StickersListWidget::Footer::resizeEvent(QResizeEvent *e) {
	if (_searchField) {
		resizeSearchControls();
	}
}

void StickersListWidget::Footer::resizeSearchControls() {
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

void StickersListWidget::Footer::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (_iconOver == SpecialOver::Settings) {
		Ui::show(Box<StickersBox>(hasOnlyFeaturedSets()
			? StickersBox::Section::Featured
			: StickersBox::Section::Installed));
	} else if (_iconOver == SpecialOver::Search) {
		toggleSearch(true);
	} else {
		_iconDown = _iconOver;
		_iconsMouseDown = _iconsMousePos;
		_iconsStartX = qRound(_iconsX.current());
	}
}

void StickersListWidget::Footer::mouseMoveEvent(QMouseEvent *e) {
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (!_iconsDragging
		&& !_icons.isEmpty()
		&& base::get_if<int>(&_iconDown) != nullptr) {
		if ((_iconsMousePos - _iconsMouseDown).manhattanLength() >= QApplication::startDragDistance()) {
			_iconsDragging = true;
		}
	}
	if (_iconsDragging) {
		auto newX = snap(_iconsStartX + (rtl() ? -1 : 1) * (_iconsMouseDown.x() - _iconsMousePos.x()), 0, _iconsMax);
		if (newX != qRound(_iconsX.current())) {
			_iconsX = anim::value(newX, newX);
			_iconsStartAnim = 0;
			_a_icons.stop();
			update();
		}
	}
}

void StickersListWidget::Footer::mouseReleaseEvent(QMouseEvent *e) {
	if (_icons.isEmpty()) {
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
		if (const auto index = base::get_if<int>(&_iconOver)) {
			_iconSelX = anim::value(
				*index * st::stickerIconWidth,
				*index * st::stickerIconWidth);
			_pan->showStickerSet(_icons[*index].setId);
		}
	}
}

void StickersListWidget::Footer::finishDragging() {
	auto newX = snap(_iconsStartX + _iconsMouseDown.x() - _iconsMousePos.x(), 0, _iconsMax);
	if (newX != qRound(_iconsX.current())) {
		_iconsX = anim::value(newX, newX);
		_iconsStartAnim = 0;
		_a_icons.stop();
		update();
	}
	_iconsDragging = false;
	updateSelected();
}

bool StickersListWidget::Footer::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {
	} else if (e->type() == QEvent::Wheel) {
		if (!_icons.isEmpty()
			&& (base::get_if<int>(&_iconOver) != nullptr)
			&& (_iconDown == SpecialOver::None)) {
			scrollByWheelEvent(static_cast<QWheelEvent*>(e));
		}
	}
	return InnerFooter::event(e);
}

void StickersListWidget::Footer::scrollByWheelEvent(
		not_null<QWheelEvent*> e) {
	auto horizontal = (e->angleDelta().x() != 0 || e->orientation() == Qt::Horizontal);
	auto vertical = (e->angleDelta().y() != 0 || e->orientation() == Qt::Vertical);
	if (horizontal) {
		_horizontal = true;
	}
	auto newX = qRound(_iconsX.current());
	if (/*_horizontal && */horizontal) {
		newX = snap(newX - (rtl() ? -1 : 1) * (e->pixelDelta().x() ? e->pixelDelta().x() : e->angleDelta().x()), 0, _iconsMax);
	} else if (/*!_horizontal && */vertical) {
		newX = snap(newX - (e->pixelDelta().y() ? e->pixelDelta().y() : e->angleDelta().y()), 0, _iconsMax);
	}
	if (newX != qRound(_iconsX.current())) {
		_iconsX = anim::value(newX, newX);
		_iconsStartAnim = 0;
		_a_icons.stop();
		updateSelected();
		update();
	}
}

void StickersListWidget::Footer::updateSelected() {
	if (_iconDown >= 0) {
		return;
	}

	auto p = mapFromGlobal(_iconsMousePos);
	auto x = p.x(), y = p.y();
	if (rtl()) x = width() - x;
	const auto settingsLeft = width() - _iconsRight;
	const auto searchLeft = _iconsLeft - st::stickerIconWidth;
	auto newOver = OverState(SpecialOver::None);
	if (x >= searchLeft
		&& x < searchLeft + st::stickerIconWidth
		&& y >= _iconsTop
		&& y < _iconsTop + st::emojiFooterHeight) {
		newOver = SpecialOver::Search;
	} else if (x >= settingsLeft
		&& x < settingsLeft + st::stickerIconWidth
		&& y >= _iconsTop
		&& y < _iconsTop + st::emojiFooterHeight) {
		if (!_icons.isEmpty() && !hasOnlyFeaturedSets()) {
			newOver = SpecialOver::Settings;
		}
	} else if (!_icons.isEmpty()) {
		if (y >= _iconsTop
			&& y < _iconsTop + st::emojiFooterHeight
			&& x >= _iconsLeft
			&& x < width() - _iconsRight
			&& x < _icons.size() * st::stickerIconWidth) {
			x += qRound(_iconsX.current()) - _iconsLeft;
			newOver = qFloor(x / st::stickerIconWidth);
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

void StickersListWidget::Footer::refreshIcons(
		ValidateIconAnimations animations) {
	_iconOver = SpecialOver::None;
	_pan->fillIcons(_icons);
	_iconsX.finish();
	_iconSelX.finish();
	_iconsStartAnim = 0;
	_a_icons.stop();
	_iconsMax = std::max(
		_iconsLeft + int(_icons.size()) * st::stickerIconWidth + _iconsRight - width(),
		0);
	if (_iconsX.current() > _iconsMax) {
		_iconsX = anim::value(_iconsMax, _iconsMax);
	}
	updateSelected();
	_pan->validateSelectedIcon(animations);
	update();
}

bool StickersListWidget::Footer::hasOnlyFeaturedSets() const {
	return (_icons.size() == 1) && (_icons[0].setId == Stickers::FeaturedSetId);
}

void StickersListWidget::Footer::paintStickerSettingsIcon(Painter &p) const {
	const auto settingsLeft = width() - _iconsRight;
	st::stickersSettings.paint(p, settingsLeft + (st::stickerIconWidth - st::stickersSettings.width()) / 2, _iconsTop + st::emojiCategory.iconPosition.y(), width());
}

void StickersListWidget::Footer::paintSearchIcon(Painter &p) const {
	const auto searchLeft = _iconsLeft - st::stickerIconWidth;
	st::stickersSearch.paint(p, searchLeft + (st::stickerIconWidth - st::stickersSearch.width()) / 2, _iconsTop + st::emojiCategory.iconPosition.y(), width());
}

void StickersListWidget::Footer::paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const {
	if (auto unread = Auth().data().featuredStickerSetsUnreadCount()) {
		Dialogs::Layout::UnreadBadgeStyle unreadSt;
		unreadSt.sizeId = Dialogs::Layout::UnreadBadgeInStickersPanel;
		unreadSt.size = st::stickersSettingsUnreadSize;
		int unreadRight = iconLeft + st::stickerIconWidth - st::stickersSettingsUnreadPosition.x();
		if (rtl()) unreadRight = width() - unreadRight;
		int unreadTop = _iconsTop + st::stickersSettingsUnreadPosition.y();
		Dialogs::Layout::paintUnreadCount(p, QString::number(unread), unreadRight, unreadTop, unreadSt);
	}
}

void StickersListWidget::Footer::paintSetIcon(
		Painter &p,
		const StickerIcon &icon,
		int x) const {
	if (icon.sticker) {
		icon.sticker->thumb->load();
		auto pix = icon.sticker->thumb->pix(icon.pixw, icon.pixh);

		p.drawPixmapLeft(x + (st::stickerIconWidth - icon.pixw) / 2, _iconsTop + (st::emojiFooterHeight - icon.pixh) / 2, width(), pix);
	} else if (icon.megagroup) {
		icon.megagroup->paintUserpicLeft(p, x + (st::stickerIconWidth - st::stickerGroupCategorySize) / 2, _iconsTop + (st::emojiFooterHeight - st::stickerGroupCategorySize) / 2, width(), st::stickerGroupCategorySize);
	} else {
		auto getSpecialSetIcon = [](uint64 setId) {
			if (setId == Stickers::FeaturedSetId) {
				return &st::stickersTrending;
			//} else if (setId == Stickers::FavedSetId) {
			//	return &st::stickersFaved;
			}
			return &st::emojiRecent;
		};
		auto paintedIcon = getSpecialSetIcon(icon.setId);
		paintedIcon->paint(p, x + (st::stickerIconWidth - paintedIcon->width()) / 2, _iconsTop + st::emojiCategory.iconPosition.y(), width());
		if (icon.setId == Stickers::FeaturedSetId) {
			paintFeaturedStickerSetsBadge(p, x);
		}
	}
}

void StickersListWidget::Footer::step_icons(TimeMs ms, bool timer) {
	if (_iconsStartAnim) {
		auto dt = (ms - _iconsStartAnim) / float64(st::stickerIconMove);
		if (dt >= 1) {
			_iconsStartAnim = 0;
			_iconsX.finish();
			_iconSelX.finish();
		} else {
			_iconsX.update(dt, anim::linear);
			_iconSelX.update(dt, anim::linear);
		}
	}

	if (timer) update();

	if (!_iconsStartAnim) {
		_a_icons.stop();
	}
}

StickersListWidget::StickersListWidget(QWidget *parent, not_null<Window::Controller*> controller) : Inner(parent, controller)
, _section(Section::Stickers)
, _megagroupSetAbout(st::columnMinimalWidthThird - st::emojiScroll.width - st::emojiPanHeaderLeft)
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _settings(this, lang(lng_stickers_you_have))
, _searchRequestTimer([=] { sendSearchRequest(); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	connect(_settings, SIGNAL(clicked()), this, SLOT(onSettings()));

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));

	subscribe(Auth().downloaderTaskFinished(), [this] {
		update();
		readVisibleSets();
	});
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::ChannelStickersChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _megagroupSet) {
			refreshStickers();
		}
	}));
}

object_ptr<TabbedSelector::InnerFooter> StickersListWidget::createFooter() {
	Expects(_footer == nullptr);
	auto result = object_ptr<Footer>(this);
	_footer = result;
	return std::move(result);
}

void StickersListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto top = getVisibleTop();
	Inner::visibleTopBottomUpdated(visibleTop, visibleBottom);
	if (_section == Section::Featured) {
		readVisibleSets();
	}
	validateSelectedIcon(ValidateIconAnimations::Full);
}

void StickersListWidget::readVisibleSets() {
	auto itemsVisibleTop = getVisibleTop();
	auto itemsVisibleBottom = getVisibleBottom();
	auto rowHeight = featuredRowHeight();
	int rowFrom = floorclamp(itemsVisibleTop, rowHeight, 0, _featuredSets.size());
	int rowTo = ceilclamp(itemsVisibleBottom, rowHeight, 0, _featuredSets.size());
	for (int i = rowFrom; i < rowTo; ++i) {
		auto &set = _featuredSets[i];
		if (!(set.flags & MTPDstickerSet_ClientFlag::f_unread)) {
			continue;
		}
		if (i * rowHeight < itemsVisibleTop || (i + 1) * rowHeight > itemsVisibleBottom) {
			continue;
		}
		int count = qMin(set.pack.size(), _columnCount);
		int loaded = 0;
		for (int j = 0; j < count; ++j) {
			if (set.pack[j]->thumb->loaded() || set.pack[j]->loaded()) {
				++loaded;
			}
		}
		if (loaded == count) {
			Auth().api().readFeaturedSetDelayed(set.id);
		}
	}
}

int StickersListWidget::featuredRowHeight() const {
	return st::stickersTrendingHeader
		+ _singleSize.height()
		+ st::stickersTrendingSkip;
}

template <typename Callback>
bool StickersListWidget::enumerateSections(Callback callback) const {
	auto info = SectionInfo();
	const auto &sets = shownSets();
	for (auto i = 0; i != sets.size(); ++i) {
		auto &set = sets[i];
		info.section = i;
		info.count = set.pack.size();
		const auto titleSkip = set.externalLayout
			? st::stickersTrendingHeader
			: setHasTitle(set)
			? st::emojiPanHeader
			: st::stickerPanPadding;
		info.rowsTop = info.top + titleSkip;
		if (set.externalLayout) {
			info.rowsCount = 1;
			info.rowsBottom = info.top + featuredRowHeight();
		} else if (set.id == Stickers::MegagroupSetId && !info.count) {
			info.rowsCount = 0;
			info.rowsBottom = info.rowsTop + _megagroupSetButtonRect.y() + _megagroupSetButtonRect.height() + st::stickerGroupCategoryAddMargin.bottom();
		} else {
			info.rowsCount = (info.count / _columnCount) + ((info.count % _columnCount) ? 1 : 0);
			info.rowsBottom = info.rowsTop + info.rowsCount * _singleSize.height();
		}
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
	}
	return true;
}

StickersListWidget::SectionInfo StickersListWidget::sectionInfo(int section) const {
	Expects(section >= 0 && section < shownSets().size());
	auto result = SectionInfo();
	enumerateSections([searchForSection = section, &result](const SectionInfo &info) {
		if (info.section == searchForSection) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

StickersListWidget::SectionInfo StickersListWidget::sectionInfoByOffset(int yOffset) const {
	auto result = SectionInfo();
	enumerateSections([this, &result, yOffset](const SectionInfo &info) {
		if (yOffset < info.rowsBottom || info.section == shownSets().size() - 1) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

int StickersListWidget::countDesiredHeight(int newWidth) {
	if (newWidth <= st::stickerPanWidthMin) {
		return 0;
	}
	auto availableWidth = newWidth - (st::stickerPanPadding - st::buttonRadius);
	auto columnCount = availableWidth / st::stickerPanWidthMin;
	auto singleWidth = availableWidth / columnCount;
	auto fullWidth = (st::buttonRadius + newWidth + st::emojiScroll.width);
	auto rowsRight = (fullWidth - columnCount * singleWidth) / 2;
	accumulate_max(rowsRight, st::emojiScroll.width);
	_rowsLeft = fullWidth
		- columnCount * singleWidth
		- rowsRight
		- st::buttonRadius;
	_singleSize = QSize(singleWidth, singleWidth);
	setColumnCount(columnCount);

	auto visibleHeight = minimalHeight();
	auto minimalHeight = (visibleHeight - st::stickerPanPadding);
	auto countResult = [this](int minimalLastHeight) {
		const auto &sets = shownSets();
		if (sets.empty()) {
			return 0;
		}
		const auto info = sectionInfo(sets.size() - 1);
		return info.top
			+ qMax(info.rowsBottom - info.top, minimalLastHeight);
	};
	const auto minimalLastHeight = (_section == Section::Stickers)
		? minimalHeight
		: 0;
	return qMax(minimalHeight, countResult(minimalLastHeight))
		+ st::stickerPanPadding;
}

void StickersListWidget::installedLocally(uint64 setId) {
	_installedLocallySets.insert(setId);
}

void StickersListWidget::notInstalledLocally(uint64 setId) {
	_installedLocallySets.remove(setId);
}

void StickersListWidget::clearInstalledLocally() {
	if (!_installedLocallySets.empty()) {
		_installedLocallySets.clear();
		refreshStickers();
	}
}

void StickersListWidget::sendSearchRequest() {
	if (_searchRequestId || _searchNextQuery.isEmpty()) {
		return;
	}

	_searchRequestTimer.cancel();
	_searchQuery = _searchNextQuery;

	auto it = _searchCache.find(_searchQuery);
	if (it != _searchCache.cend()) {
		_footer->setLoading(false);
		return;
	}

	_footer->setLoading(true);
	const auto hash = int32(0);
	_searchRequestId = request(MTPmessages_SearchStickerSets(
		MTP_flags(0),
		MTP_string(_searchQuery),
		MTP_int(hash)
	)).done([=](const MTPmessages_FoundStickerSets &result) {
		searchResultsDone(result);
	}).fail([this](const RPCError &error) {
		// show error?
		_footer->setLoading(false);
		_searchRequestId = 0;
	}).handleAllErrors().send();
}

void StickersListWidget::searchForSets(const QString &query) {
	const auto cleaned = query.trimmed();
	if (cleaned.isEmpty()) {
		cancelSetsSearch();
		return;
	}

	if (_searchQuery != cleaned) {
		_footer->setLoading(false);
		if (const auto requestId = base::take(_searchRequestId)) {
			request(requestId).cancel();
		}
		if (_searchCache.find(cleaned) != _searchCache.cend()) {
			_searchRequestTimer.cancel();
			_searchQuery = _searchNextQuery = cleaned;
		} else {
			_searchNextQuery = cleaned;
			_searchRequestTimer.callOnce(kSearchRequestDelay);
		}
		showSearchResults();
	}
}

void StickersListWidget::cancelSetsSearch() {
	_footer->setLoading(false);
	if (const auto requestId = base::take(_searchRequestId)) {
		request(requestId).cancel();
	}
	_searchRequestTimer.cancel();
	_searchQuery = _searchNextQuery = QString();
	_searchCache.clear();
	refreshSearchRows(nullptr);
}

void StickersListWidget::showSearchResults() {
	refreshSearchRows();
	scrollToY(0);
}

void StickersListWidget::refreshSearchRows() {
	auto it = _searchCache.find(_searchQuery);
	auto sets = (it != end(_searchCache))
		? &it->second
		: nullptr;
	refreshSearchRows(sets);
}

void StickersListWidget::refreshSearchRows(
		const std::vector<Stickers::Set*> *cloudSets) {
	clearSelection();

	_searchSets.clear();
	fillLocalSearchRows(_searchNextQuery);

	if (!cloudSets && _searchSets.empty()) {
		showStickerSet(!_mySets.empty()
			? _mySets[0].id
			: Stickers::FeaturedSetId);
		return;
	}

	if (_section != Section::Search) {
		_section = Section::Search;
	}
	if (cloudSets) {
		fillCloudSearchRows(*cloudSets);
	}
	if (_footer) {
		_footer->refreshIcons(ValidateIconAnimations::Scroll);
	}

	_lastMousePosition = QCursor::pos();

	resizeToWidth(width());
	updateSelected();
}

void StickersListWidget::fillLocalSearchRows(const QString &query) {
	const auto searchWordsList = TextUtilities::PrepareSearchWords(query);
	if (searchWordsList.isEmpty()) {
		return;
	}
	auto searchWordInTitle = [](
			const QStringList &titleWords,
			const QString &searchWord) {
		for (const auto &titleWord : titleWords) {
			if (titleWord.startsWith(searchWord)) {
				return true;
			}
		}
		return false;
	};
	auto allSearchWordsInTitle = [&](
			const QStringList &titleWords) {
		for (const auto &searchWord : searchWordsList) {
			if (!searchWordInTitle(titleWords, searchWord)) {
				return false;
			}
		}
		return true;
	};

	const auto &sets = Auth().data().stickerSets();
	for (const auto &[setId, titleWords] : _searchIndex) {
		if (allSearchWordsInTitle(titleWords)) {
			if (const auto it = sets.find(setId); it != sets.end()) {
				addSearchRow(&*it);
			}
		}
	}
}

void StickersListWidget::fillCloudSearchRows(
		const std::vector<Stickers::Set*> &sets) {
	for (const auto set : sets) {
		addSearchRow(set);
	}
}

void StickersListWidget::addSearchRow(not_null<const Stickers::Set*> set) {
	_searchSets.push_back(Set(
		set->id,
		set->flags,
		set->title,
		set->shortName,
		!SetInMyList(set->flags),
		set->count,
		set->stickers.empty() ? set->covers : set->stickers));
}

auto StickersListWidget::shownSets() const -> const std::vector<Set> & {
	switch (_section) {
	case Section::Featured: return _featuredSets;
	case Section::Search: return _searchSets;
	case Section::Stickers: return _mySets;
	}
	Unexpected("Section in StickersListWidget.");
}

auto StickersListWidget::shownSets() -> std::vector<Set> & {
	switch (_section) {
	case Section::Featured: return _featuredSets;
	case Section::Search: return _searchSets;
	case Section::Stickers: return _mySets;
	}
	Unexpected("Section in StickersListWidget.");
}

void StickersListWidget::searchResultsDone(
		const MTPmessages_FoundStickerSets &result) {
	_footer->setLoading(false);
	_searchRequestId = 0;

	if (result.type() == mtpc_messages_foundStickerSetsNotModified) {
		LOG(("API Error: "
			"messages.foundStickerSetsNotModified not expected."));
		return;
	}

	Assert(result.type() == mtpc_messages_foundStickerSets);

	auto it = _searchCache.find(_searchQuery);
	if (it == _searchCache.cend()) {
		it = _searchCache.emplace(
			_searchQuery,
			std::vector<Stickers::Set*>()).first;
	}
	auto &d = result.c_messages_foundStickerSets();
	for_const (const auto &stickerSet, d.vsets.v) {
		const MTPDstickerSet *setData = nullptr;
		Stickers::Pack covers;
		switch (stickerSet.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = stickerSet.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = stickerSet.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
			for (const auto &cover : d.vcovers.v) {
				const auto document = Auth().data().document(cover);
				if (document->sticker()) {
					covers.push_back(document);
				}
			}
		} break;
		}
		if (!setData) continue;

		if (const auto set = Stickers::FeedSet(*setData)) {
			if (!covers.empty()) {
				set->covers = covers;
			}
			if (set->stickers.empty() && set->covers.empty()) {
				continue;
			}
			it->second.push_back(set);
		}
	}
	showSearchResults();
}

int StickersListWidget::stickersLeft() const {
	return _rowsLeft;
}

QRect StickersListWidget::stickerRect(int section, int sel) {
	auto info = sectionInfo(section);
	if (sel >= shownSets()[section].pack.size()) {
		sel -= shownSets()[section].pack.size();
	}
	auto countTillItem = (sel - (sel % _columnCount));
	auto rowsToSkip = (countTillItem / _columnCount) + ((countTillItem % _columnCount) ? 1 : 0);
	auto x = stickersLeft() + ((sel % _columnCount) * _singleSize.width());
	auto y = info.rowsTop + rowsToSkip * _singleSize.height();
	return QRect(QPoint(x, y), _singleSize);
}

void StickersListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);

	paintStickers(p, clip);
}

void StickersListWidget::paintStickers(Painter &p, QRect clip) {
	auto fromColumn = floorclamp(clip.x() - stickersLeft(), _singleSize.width(), 0, _columnCount);
	auto toColumn = ceilclamp(clip.x() + clip.width() - stickersLeft(), _singleSize.width(), 0, _columnCount);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = _columnCount - fromColumn;
		toColumn = _columnCount - toColumn;
	}

	auto ms = getms();
	auto &sets = shownSets();
	auto selectedSticker = base::get_if<OverSticker>(&_selected);
	auto selectedButton = base::get_if<OverButton>(_pressed ? &_pressed : &_selected);

	if (sets.empty() && _section == Section::Search) {
		paintEmptySearchResults(p);
	}
	enumerateSections([&](const SectionInfo &info) {
		if (clip.top() >= info.rowsBottom) {
			return true;
		} else if (clip.top() + clip.height() <= info.top) {
			return false;
		}
		auto &set = sets[info.section];
		if (set.externalLayout) {
			const auto size = (set.flags
				& MTPDstickerSet_ClientFlag::f_not_loaded)
				? set.count
				: int(set.pack.size());

			auto widthForTitle = stickersRight() - (st::emojiPanHeaderLeft - st::buttonRadius);
			if (featuredHasAddButton(info.section)) {
				auto add = featuredAddRect(info.section);
				auto selected = selectedButton ? (selectedButton->section == info.section) : false;
				auto &textBg = selected ? st::stickersTrendingAdd.textBgOver : st::stickersTrendingAdd.textBg;

				App::roundRect(p, myrtlrect(add), textBg, ImageRoundRadius::Small);
				if (set.ripple) {
					set.ripple->paint(p, add.x(), add.y(), width(), ms);
					if (set.ripple->empty()) {
						set.ripple.reset();
					}
				}
				p.setFont(st::stickersTrendingAdd.font);
				p.setPen(selected ? st::stickersTrendingAdd.textFgOver : st::stickersTrendingAdd.textFg);
				p.drawTextLeft(add.x() - (st::stickersTrendingAdd.width / 2), add.y() + st::stickersTrendingAdd.textTop, width(), _addText, _addWidth);

				widthForTitle -= add.width() - (st::stickersTrendingAdd.width / 2);
			} else {
				auto add = featuredAddRect(info.section);
				int checkx = add.left() + (add.width() - st::stickersFeaturedInstalled.width()) / 2;
				int checky = add.top() + (add.height() - st::stickersFeaturedInstalled.height()) / 2;
				st::stickersFeaturedInstalled.paint(p, QPoint(checkx, checky), width());
			}
			if (set.flags & MTPDstickerSet_ClientFlag::f_unread) {
				widthForTitle -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
			}

			auto titleText = set.title;
			auto titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			if (titleWidth > widthForTitle) {
				titleText = st::stickersTrendingHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			}
			p.setFont(st::stickersTrendingHeaderFont);
			p.setPen(st::stickersTrendingHeaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, info.top + st::stickersTrendingHeaderTop, width(), titleText, titleWidth);

			if (set.flags & MTPDstickerSet_ClientFlag::f_unread) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::stickersFeaturedUnreadBg);

				{
					PainterHighQualityEnabler hq(p);
					p.drawEllipse(rtlrect(st::emojiPanHeaderLeft - st::buttonRadius + titleWidth + st::stickersFeaturedUnreadSkip, info.top + st::stickersTrendingHeaderTop + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
				}
			}

			auto statusText = (size > 0) ? lng_stickers_count(lt_count, size) : lang(lng_contacts_loading);
			p.setFont(st::stickersTrendingSubheaderFont);
			p.setPen(st::stickersTrendingSubheaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, info.top + st::stickersTrendingSubheaderTop, width(), statusText);

			if (info.rowsTop >= clip.y() + clip.height()) {
				return true;
			}

			for (int j = fromColumn; j < toColumn; ++j) {
				int index = j;
				if (index >= size) break;

				auto selected = selectedSticker ? (selectedSticker->section == info.section && selectedSticker->index == index) : false;
				auto deleteSelected = false;
				paintSticker(p, set, info.rowsTop, index, selected, deleteSelected);
			}
			return true;
		}
		if (setHasTitle(set) && clip.top() < info.rowsTop) {
			auto titleText = set.title;
			auto titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			auto widthForTitle = stickersRight() - (st::emojiPanHeaderLeft - st::buttonRadius);
			if (hasRemoveButton(info.section)) {
				auto remove = removeButtonRect(info.section);
				auto selected = selectedButton ? (selectedButton->section == info.section) : false;
				if (set.ripple) {
					set.ripple->paint(p, remove.x() + st::stickerPanRemoveSet.rippleAreaPosition.x(), remove.y() + st::stickerPanRemoveSet.rippleAreaPosition.y(), width(), ms);
					if (set.ripple->empty()) {
						set.ripple.reset();
					}
				}
				(selected ? st::stickerPanRemoveSet.iconOver : st::stickerPanRemoveSet.icon).paint(p, remove.topLeft() + st::stickerPanRemoveSet.iconPosition, width());

				widthForTitle -= remove.width();
			}
			if (titleWidth > widthForTitle) {
				titleText = st::stickersTrendingHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			}
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			p.drawTextLeft(st::emojiPanHeaderLeft - st::buttonRadius, info.top + st::emojiPanHeaderTop, width(), titleText, titleWidth);
		}
		if (clip.top() + clip.height() > info.rowsTop) {
			if (set.id == Stickers::MegagroupSetId && set.pack.empty()) {
				auto buttonSelected = (base::get_if<OverGroupAdd>(&_selected) != nullptr);
				paintMegagroupEmptySet(p, info.rowsTop, buttonSelected, ms);
			} else {
				auto special = (set.flags & MTPDstickerSet::Flag::f_official) != 0;
				auto fromRow = floorclamp(clip.y() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
				auto toRow = ceilclamp(clip.y() + clip.height() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
				for (int i = fromRow; i < toRow; ++i) {
					for (int j = fromColumn; j < toColumn; ++j) {
						int index = i * _columnCount + j;
						if (index >= info.count) break;

						auto selected = selectedSticker ? (selectedSticker->section == info.section && selectedSticker->index == index) : false;
						auto deleteSelected = selected && selectedSticker->overDelete;
						paintSticker(p, set, info.rowsTop, index, selected, deleteSelected);
					}
				}
			}
		}
		return true;
	});
}

void StickersListWidget::paintEmptySearchResults(Painter &p) {
	const auto iconLeft = (width() - st::stickersEmpty.width()) / 2;
	const auto iconTop = (height() / 3) - (st::stickersEmpty.height() / 2);
	st::stickersEmpty.paint(p, iconLeft, iconTop, width());

	const auto text = lang(lng_stickers_nothing_found);
	const auto textWidth = st::normalFont->width(text);
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawTextLeft(
		(width() - textWidth) / 2,
		iconTop + st::stickersEmpty.height() - st::normalFont->height,
		width(),
		text,
		textWidth);
}

int StickersListWidget::megagroupSetInfoLeft() const {
	return st::emojiPanHeaderLeft - st::buttonRadius;
}

void StickersListWidget::paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected, TimeMs ms) {
	auto infoLeft = megagroupSetInfoLeft();
	_megagroupSetAbout.drawLeft(p, infoLeft, y, width() - infoLeft, width());

	auto &textBg = buttonSelected
		? st::stickerGroupCategoryAdd.textBgOver
		: st::stickerGroupCategoryAdd.textBg;

	auto button = _megagroupSetButtonRect.translated(0, y);
	App::roundRect(p, myrtlrect(button), textBg, ImageRoundRadius::Small);
	if (_megagroupSetButtonRipple) {
		_megagroupSetButtonRipple->paint(p, button.x(), button.y(), width(), ms);
		if (_megagroupSetButtonRipple->empty()) {
			_megagroupSetButtonRipple.reset();
		}
	}
	p.setFont(st::stickerGroupCategoryAdd.font);
	p.setPen(buttonSelected ? st::stickerGroupCategoryAdd.textFgOver : st::stickerGroupCategoryAdd.textFg);
	p.drawTextLeft(button.x() - (st::stickerGroupCategoryAdd.width / 2), button.y() + st::stickerGroupCategoryAdd.textTop, width(), _megagroupSetButtonText, _megagroupSetButtonTextWidth);
}

void StickersListWidget::paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected) {
	auto sticker = set.pack[index];
	if (!sticker->sticker()) return;

	int row = (index / _columnCount), col = (index % _columnCount);

	auto pos = QPoint(stickersLeft() + col * _singleSize.width(), y + row * _singleSize.height());
	if (selected) {
		auto tl = pos;
		if (rtl()) tl.setX(width() - tl.x() - _singleSize.width());
		App::roundRect(p, QRect(tl, _singleSize), st::emojiPanHover, StickerHoverCorners);
	}

	auto goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
	if (goodThumb) {
		sticker->thumb->load();
	} else {
		sticker->checkSticker();
	}

	auto coef = qMin((_singleSize.width() - st::buttonRadius * 2) / float64(sticker->dimensions.width()), (_singleSize.height() - st::buttonRadius * 2) / float64(sticker->dimensions.height()));
	if (coef > 1) coef = 1;
	auto w = qMax(qRound(coef * sticker->dimensions.width()), 1);
	auto h = qMax(qRound(coef * sticker->dimensions.height()), 1);
	auto ppos = pos + QPoint((_singleSize.width() - w) / 2, (_singleSize.height() - h) / 2);
	auto paintImage = [&](ImagePtr image) {
		if (image->loaded()) {
			p.drawPixmapLeft(
				ppos,
				width(),
				image->pixSingle(w, h, w, h, ImageRoundRadius::None));
		}
	};
	if (goodThumb) {
		paintImage(sticker->thumb);
	} else if (!sticker->sticker()->img->isNull()) {
		paintImage(sticker->sticker()->img);
	}

	if (selected && stickerHasDeleteButton(set, index)) {
		auto xPos = pos + QPoint(_singleSize.width() - st::stickerPanDeleteIconBg.width(), 0);
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityBgOver : st::stickerPanDeleteOpacityBg);
		st::stickerPanDeleteIconBg.paint(p, xPos, width());
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityFgOver : st::stickerPanDeleteOpacityFg);
		st::stickerPanDeleteIconFg.paint(p, xPos, width());
		p.setOpacity(1.);
	}
}

int StickersListWidget::stickersRight() const {
	return stickersLeft() + (_columnCount * _singleSize.width());
}

bool StickersListWidget::featuredHasAddButton(int index) const {
	if (index < 0
		|| index >= shownSets().size()
		|| !shownSets()[index].externalLayout) {
		return false;
	}
	const auto flags = shownSets()[index].flags;
	return !SetInMyList(flags);
}

QRect StickersListWidget::featuredAddRect(int index) const {
	auto addw = _addWidth - st::stickersTrendingAdd.width;
	auto addh = st::stickersTrendingAdd.height;
	auto addx = stickersRight() - addw;
	auto addy = sectionInfo(index).top + st::stickersTrendingAddTop;
	return QRect(addx, addy, addw, addh);
}

bool StickersListWidget::hasRemoveButton(int index) const {
	if (index < 0 || index >= shownSets().size()) {
		return false;
	}
	auto &set = shownSets()[index];
	if (set.externalLayout) {
		return false;
	}
	auto flags = set.flags;
	if (!(flags & MTPDstickerSet_ClientFlag::f_special)) {
		return true;
	}
	if (set.id == Stickers::MegagroupSetId) {
		Assert(_megagroupSet != nullptr);
		if (index + 1 != shownSets().size()) {
			return true;
		}
		return !set.pack.empty() && _megagroupSet->canEditStickers();
	}
	return false;
}

QRect StickersListWidget::removeButtonRect(int index) const {
	auto buttonw = st::stickerPanRemoveSet.width;
	auto buttonh = st::stickerPanRemoveSet.height;
	auto buttonx = stickersRight() - buttonw;
	auto buttony = sectionInfo(index).top + (st::emojiPanHeader - buttonh) / 2;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

void StickersListWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePosition = e->globalPos();
	updateSelected();

	setPressed(_selected);
	ClickHandler::pressed();
	_previewTimer.start(QApplication::startDragTime());
}

void StickersListWidget::setPressed(OverState newPressed) {
	if (auto button = base::get_if<OverButton>(&_pressed)) {
		auto &sets = shownSets();
		Assert(button->section >= 0 && button->section < sets.size());
		auto &set = sets[button->section];
		if (set.ripple) {
			set.ripple->lastStop();
		}
	} else if (base::get_if<OverGroupAdd>(&_pressed)) {
		if (_megagroupSetButtonRipple) {
			_megagroupSetButtonRipple->lastStop();
		}
	}
	_pressed = newPressed;
	if (auto button = base::get_if<OverButton>(&_pressed)) {
		auto &sets = shownSets();
		Assert(button->section >= 0 && button->section < sets.size());
		auto &set = sets[button->section];
		if (!set.ripple) {
			set.ripple = createButtonRipple(button->section);
		}
		set.ripple->add(mapFromGlobal(QCursor::pos()) - buttonRippleTopLeft(button->section));
	} else if (base::get_if<OverGroupAdd>(&_pressed)) {
		if (!_megagroupSetButtonRipple) {
			auto maskSize = _megagroupSetButtonRect.size();
			auto mask = Ui::RippleAnimation::roundRectMask(maskSize, st::buttonRadius);
			_megagroupSetButtonRipple = std::make_unique<Ui::RippleAnimation>(st::stickerGroupCategoryAdd.ripple, std::move(mask), [this] {
				rtlupdate(megagroupSetButtonRectFinal());
			});
		}
		_megagroupSetButtonRipple->add(mapFromGlobal(QCursor::pos()) - myrtlrect(megagroupSetButtonRectFinal()).topLeft());
	}
}

QRect StickersListWidget::megagroupSetButtonRectFinal() const {
	auto result = QRect();
	if (_section == Section::Stickers) {
		enumerateSections([this, &result](const SectionInfo &info) {
			if (shownSets()[info.section].id == Stickers::MegagroupSetId) {
				result = _megagroupSetButtonRect.translated(0, info.rowsTop);
				return false;
			}
			return true;
		});
	}
	return result;
}

std::unique_ptr<Ui::RippleAnimation> StickersListWidget::createButtonRipple(int section) {
	Expects(section >= 0 && section < shownSets().size());

	if (shownSets()[section].externalLayout) {
		auto maskSize = QSize(_addWidth - st::stickersTrendingAdd.width, st::stickersTrendingAdd.height);
		auto mask = Ui::RippleAnimation::roundRectMask(maskSize, st::buttonRadius);
		return std::make_unique<Ui::RippleAnimation>(
			st::stickersTrendingAdd.ripple,
			std::move(mask),
			[this, section] { rtlupdate(featuredAddRect(section)); });
	}
	auto maskSize = QSize(st::stickerPanRemoveSet.rippleAreaSize, st::stickerPanRemoveSet.rippleAreaSize);
	auto mask = Ui::RippleAnimation::ellipseMask(maskSize);
	return std::make_unique<Ui::RippleAnimation>(
		st::stickerPanRemoveSet.ripple,
		std::move(mask),
		[this, section] { rtlupdate(removeButtonRect(section)); });
}

QPoint StickersListWidget::buttonRippleTopLeft(int section) const {
	Expects(section >= 0 && section < shownSets().size());

	if (shownSets()[section].externalLayout) {
		return myrtlrect(featuredAddRect(section)).topLeft();
	}
	return myrtlrect(removeButtonRect(section)).topLeft() + st::stickerPanRemoveSet.rippleAreaPosition;
}

void StickersListWidget::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.stop();

	auto pressed = _pressed;
	setPressed(base::none);
	if (pressed != _selected) {
		update();
	}

	auto activated = ClickHandler::unpressed();
	if (_previewShown) {
		_previewShown = false;
		return;
	}

	_lastMousePosition = e->globalPos();
	updateSelected();

	auto &sets = shownSets();
	if (pressed && pressed == _selected) {
		if (auto sticker = base::get_if<OverSticker>(&pressed)) {
			Assert(sticker->section >= 0 && sticker->section < sets.size());
			auto &set = sets[sticker->section];
			Assert(sticker->index >= 0 && sticker->index < set.pack.size());
			if (stickerHasDeleteButton(set, sticker->index) && sticker->overDelete) {
				if (set.id == Stickers::RecentSetId) {
					removeRecentSticker(sticker->section, sticker->index);
				} else if (set.id == Stickers::FavedSetId) {
					removeFavedSticker(sticker->section, sticker->index);
				} else {
					Unexpected("Single sticker delete click.");
				}
				return;
			}
			emit selected(set.pack[sticker->index]);
		} else if (auto set = base::get_if<OverSet>(&pressed)) {
			Assert(set->section >= 0 && set->section < sets.size());
			displaySet(sets[set->section].id);
		} else if (auto button = base::get_if<OverButton>(&pressed)) {
			Assert(button->section >= 0 && button->section < sets.size());
			if (sets[button->section].externalLayout) {
				installSet(sets[button->section].id);
			} else if (sets[button->section].id == Stickers::MegagroupSetId) {
				auto removeLocally = sets[button->section].pack.empty()
					|| !_megagroupSet->canEditStickers();
				removeMegagroupSet(removeLocally);
			} else {
				removeSet(sets[button->section].id);
			}
		} else if (base::get_if<OverGroupAdd>(&pressed)) {
			Ui::show(Box<StickersBox>(_megagroupSet));
		}
	}
}

void StickersListWidget::validateSelectedIcon(
		ValidateIconAnimations animations) {
	if (_footer) {
		_footer->validateSelectedIcon(
			currentSet(getVisibleTop()),
			animations);
	}
}

void StickersListWidget::removeRecentSticker(int section, int index) {
	if ((_section != Section::Stickers)
		|| (section >= int(_mySets.size()))
		|| (_mySets[section].id != Stickers::RecentSetId)) {
		return;
	}

	clearSelection();
	bool refresh = false;
	auto sticker = _mySets[section].pack[index];
	auto &recent = Stickers::GetRecentPack();
	for (int32 i = 0, l = recent.size(); i < l; ++i) {
		if (recent.at(i).first == sticker) {
			recent.removeAt(i);
			Local::writeUserSettings();
			refresh = true;
			break;
		}
	}
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(Stickers::CustomSetId);
	if (it != sets.cend()) {
		for (int i = 0, l = it->stickers.size(); i < l; ++i) {
			if (it->stickers.at(i) == sticker) {
				it->stickers.removeAt(i);
				if (it->stickers.isEmpty()) {
					sets.erase(it);
				}
				Local::writeInstalledStickers();
				refresh = true;
				break;
			}
		}
	}
	if (refresh) {
		refreshRecentStickers();
		updateSelected();
		update();
	}
}

void StickersListWidget::removeFavedSticker(int section, int index) {
	if ((_section != Section::Stickers)
		|| (section >= int(_mySets.size()))
		|| (_mySets[section].id != Stickers::FavedSetId)) {
		return;
	}

	clearSelection();
	auto sticker = _mySets[section].pack[index];
	Stickers::SetFaved(sticker, false);
	auto unfave = true;
	MTP::send(MTPmessages_FaveSticker(sticker->mtpInput(), MTP_bool(unfave)));
}

void StickersListWidget::setColumnCount(int count) {
	Expects(count > 0);

	if (_columnCount != count) {
		_columnCount = count;
		refreshFooterIcons();
	}
}

void StickersListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	updateSelected();
}

void StickersListWidget::resizeEvent(QResizeEvent *e) {
	_settings->moveToLeft(
		(width() - _settings->width()) / 2,
		height() / 3);
	if (!_megagroupSetAbout.isEmpty()) {
		refreshMegagroupSetGeometry();
	}
}

void StickersListWidget::leaveEventHook(QEvent *e) {
	clearSelection();
}

void StickersListWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void StickersListWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePosition = QCursor::pos();
	updateSelected();
}

void StickersListWidget::clearSelection() {
	setPressed(base::none);
	setSelected(base::none);
	update();
}

TabbedSelector::InnerFooter *StickersListWidget::getFooter() const {
	return _footer;
}

void StickersListWidget::processHideFinished() {
	clearSelection();
}

void StickersListWidget::processPanelHideFinished() {
	clearInstalledLocally();

	// Preserve panel state through visibility toggles.
	//// Reset to the recent stickers section.
	//if (_section == Section::Featured && (!_footer || !_footer->hasOnlyFeaturedSets())) {
	//	_section = Section::Stickers;
	//	validateSelectedIcon(ValidateIconAnimations::None);
	//}
}

void StickersListWidget::refreshStickers() {
	clearSelection();

	_mySets.clear();
	_favedStickersMap.clear();
	_mySets.reserve(Auth().data().stickerSetsOrder().size() + 3);

	refreshFavedStickers();
	refreshRecentStickers(false);
	refreshMegagroupStickers(GroupStickersPlace::Visible);
	for_const (auto setId, Auth().data().stickerSetsOrder()) {
		const auto externalLayout = false;
		appendSet(_mySets, setId, externalLayout, AppendSkip::Archived);
	}
	refreshMegagroupStickers(GroupStickersPlace::Hidden);

	_featuredSets.clear();
	_featuredSets.reserve(Auth().data().featuredStickerSetsOrder().size());

	for_const (auto setId, Auth().data().featuredStickerSetsOrder()) {
		const auto externalLayout = true;
		appendSet(_featuredSets, setId, externalLayout, AppendSkip::Installed);
	}

	refreshSearchSets();

	resizeToWidth(width());

	if (_footer) {
		refreshFooterIcons();
	}
	refreshSettingsVisibility();

	_lastMousePosition = QCursor::pos();
	updateSelected();
	update();
}

void StickersListWidget::refreshSearchSets() {
	refreshSearchIndex();

	const auto &sets = Auth().data().stickerSets();
	for (auto &set : _searchSets) {
		if (const auto it = sets.find(set.id); it != sets.end()) {
			set.flags = it->flags;
			if (!it->stickers.empty()) {
				set.pack = it->stickers;
			}
			if (!SetInMyList(set.flags)) {
				_installedLocallySets.remove(set.id);
				set.externalLayout = true;
			}
		}
	}
}

void StickersListWidget::refreshSearchIndex() {
	_searchIndex.clear();
	for (const auto &set : _mySets) {
		const auto list = TextUtilities::PrepareSearchWords(set.title + ' ' + set.shortName);
		_searchIndex.emplace_back(set.id, list);
	}
}

void StickersListWidget::refreshSettingsVisibility() {
	const auto visible = (_section == Section::Stickers) && _mySets.empty();
	_settings->setVisible(visible);
}

void StickersListWidget::refreshFooterIcons() {
	_footer->refreshIcons(ValidateIconAnimations::None);
	if (_footer->hasOnlyFeaturedSets() && _section != Section::Featured) {
		showStickerSet(Stickers::FeaturedSetId);
	}
}

void StickersListWidget::preloadImages() {
	auto &sets = shownSets();
	for (int i = 0, l = sets.size(), k = 0; i < l; ++i) {
		int count = sets[i].pack.size();
		if (sets[i].externalLayout) {
			accumulate_min(count, _columnCount);
		}
		for (int j = 0; j != count; ++j) {
			if (++k > _columnCount * (_columnCount + 1)) break;

			auto sticker = sets[i].pack.at(j);
			if (!sticker || !sticker->sticker()) continue;

			bool goodThumb = !sticker->thumb->isNull()
				&& ((sticker->thumb->width() >= 128)
					|| (sticker->thumb->height() >= 128));
			if (goodThumb) {
				sticker->thumb->load();
			} else {
				sticker->automaticLoad(0);
			}
		}
		if (k > _columnCount * (_columnCount + 1)) break;
	}
	if (_footer) {
		_footer->preloadImages();
	}
}

uint64 StickersListWidget::currentSet(int yOffset) const {
	if (_section == Section::Featured) {
		return Stickers::FeaturedSetId;
	}
	const auto &sets = shownSets();
	return sets.empty()
		? Stickers::RecentSetId
		: sets[sectionInfoByOffset(yOffset).section].id;
}

void StickersListWidget::appendSet(
		std::vector<Set> &to,
		uint64 setId,
		bool externalLayout,
		AppendSkip skip) {
	auto &sets = Auth().data().stickerSets();
	auto it = sets.constFind(setId);
	if (it == sets.cend() || it->stickers.isEmpty()) {
		return;
	}
	if ((skip == AppendSkip::Archived)
		&& (it->flags & MTPDstickerSet::Flag::f_archived)) {
		return;
	}
	if ((skip == AppendSkip::Installed)
		&& (it->flags & MTPDstickerSet::Flag::f_installed_date)
		&& !(it->flags & MTPDstickerSet::Flag::f_archived)) {
		if (!_installedLocallySets.contains(setId)) {
			return;
		}
	}

	to.push_back(Set(
		it->id,
		it->flags,
		it->title,
		it->shortName,
		externalLayout,
		it->count,
		it->stickers));
}

void StickersListWidget::refreshRecent() {
	if (_section == Section::Stickers) {
		refreshRecentStickers();
	}
	if (_footer && _footer->hasOnlyFeaturedSets() && _section != Section::Featured) {
		showStickerSet(Stickers::FeaturedSetId);
	}
}

Stickers::Pack StickersListWidget::collectRecentStickers() {
	_custom.clear();
	auto result = Stickers::Pack();

	auto &sets = Auth().data().stickerSets();
	auto &recent = Stickers::GetRecentPack();
	auto customIt = sets.constFind(Stickers::CustomSetId);
	auto cloudIt = sets.constFind(Stickers::CloudRecentSetId);
	const auto customCount = (customIt != sets.cend())
		? customIt->stickers.size()
		: 0;
	const auto cloudCount = (cloudIt != sets.cend())
		? cloudIt->stickers.size()
		: 0;
	result.reserve(cloudCount + recent.size() + customCount);
	_custom.reserve(cloudCount + recent.size() + customCount);

	auto add = [&](not_null<DocumentData*> document, bool custom) {
		if (result.size() >= kRecentDisplayLimit) {
			return;
		}
		const auto index = result.indexOf(document);
		if (index >= 0) {
			if (index >= cloudCount && custom) {
				// Mark stickers from local recent as custom.
				_custom[index] = true;
			}
		} else if (!_favedStickersMap.contains(document)) {
			result.push_back(document);
			_custom.push_back(custom);
		}
	};

	if (cloudCount > 0) {
		for_const (auto document, cloudIt->stickers) {
			add(document, false);
		}
	}
	for_const (auto &recentSticker, recent) {
		add(recentSticker.first, false);
	}
	if (customCount > 0) {
		for_const (auto document, customIt->stickers) {
			add(document, true);
		}
	}
	return result;
}

void StickersListWidget::refreshRecentStickers(bool performResize) {
	clearSelection();

	auto recentPack = collectRecentStickers();
	auto recentIt = std::find_if(_mySets.begin(), _mySets.end(), [](auto &set) {
		return set.id == Stickers::RecentSetId;
	});
	if (!recentPack.empty()) {
		if (recentIt == _mySets.end()) {
			const auto shortName = QString();
			const auto externalLayout = false;
			_mySets.push_back(Set(
				Stickers::RecentSetId,
				(MTPDstickerSet::Flag::f_official
					| MTPDstickerSet_ClientFlag::f_special),
				lang(lng_recent_stickers),
				shortName,
				externalLayout,
				recentPack.size(),
				recentPack));
		} else {
			recentIt->pack = recentPack;
		}
	} else if (recentIt != _mySets.end()) {
		_mySets.erase(recentIt);
	}

	if (performResize && (_section == Section::Stickers || _section == Section::Featured)) {
		resizeToWidth(width());
		updateSelected();
	}
}

void StickersListWidget::refreshFavedStickers() {
	clearSelection();
	auto &sets = Auth().data().stickerSets();
	auto it = sets.constFind(Stickers::FavedSetId);
	if (it == sets.cend() || it->stickers.isEmpty()) {
		return;
	}
	const auto externalLayout = false;
	const auto shortName = QString();
	_mySets.push_back(Set(
		Stickers::FavedSetId,
		(MTPDstickerSet::Flag::f_official
			| MTPDstickerSet_ClientFlag::f_special),
		Lang::Hard::FavedSetTitle(),
		shortName,
		externalLayout,
		it->count,
		it->stickers));
	_favedStickersMap = base::flat_set<not_null<DocumentData*>> { it->stickers.begin(), it->stickers.end() };
}

void StickersListWidget::refreshMegagroupStickers(GroupStickersPlace place) {
	if (!_megagroupSet) {
		return;
	}
	auto canEdit = _megagroupSet->canEditStickers();
	auto isShownHere = [place](bool hidden) {
		return (hidden == (place == GroupStickersPlace::Hidden));
	};
	if (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetEmpty) {
		if (canEdit) {
			auto hidden = Auth().settings().isGroupStickersSectionHidden(_megagroupSet->id);
			if (isShownHere(hidden)) {
				const auto shortName = QString();
				const auto externalLayout = false;
				const auto count = 0;
				_mySets.push_back(Set(
					Stickers::MegagroupSetId,
					MTPDstickerSet_ClientFlag::f_special | 0,
					lang(lng_group_stickers),
					QString(),
					externalLayout,
					count));
			}
		}
		return;
	}
	auto hidden = Auth().settings().isGroupStickersSectionHidden(_megagroupSet->id);
	auto removeHiddenForGroup = [this, &hidden] {
		if (hidden) {
			Auth().settings().removeGroupStickersSectionHidden(_megagroupSet->id);
			Local::writeUserSettings();
			hidden = false;
		}
	};
	if (canEdit && hidden) {
		removeHiddenForGroup();
	}
	if (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetID) {
		auto &set = _megagroupSet->mgInfo->stickerSet.c_inputStickerSetID();
		auto &sets = Auth().data().stickerSets();
		auto it = sets.constFind(set.vid.v);
		if (it != sets.cend()) {
			auto isInstalled = (it->flags & MTPDstickerSet::Flag::f_installed_date)
				&& !(it->flags & MTPDstickerSet::Flag::f_archived);
			if (isInstalled && !canEdit) {
				removeHiddenForGroup();
			} else if (isShownHere(hidden)) {
				const auto shortName = QString();
				const auto externalLayout = false;
				_mySets.push_back(Set(
					Stickers::MegagroupSetId,
					MTPDstickerSet_ClientFlag::f_special | 0,
					lang(lng_group_stickers),
					shortName,
					externalLayout,
					it->count,
					it->stickers));
			}
			return;
		}
	}
	if (!isShownHere(hidden)) {
		return;
	}
	request(MTPmessages_GetStickerSet(_megagroupSet->mgInfo->stickerSet)).done([this](const MTPmessages_StickerSet &result) {
		if (auto set = Stickers::FeedSetFull(result)) {
			refreshStickers();
		}
	}).send();
}

void StickersListWidget::fillIcons(QList<StickerIcon> &icons) {
	icons.clear();
	icons.reserve(_mySets.size() + 1);
	if (Auth().data().featuredStickerSetsUnreadCount()
		&& !_featuredSets.empty()) {
		icons.push_back(StickerIcon(Stickers::FeaturedSetId));
	}

	auto i = 0;
	if (i != _mySets.size() && _mySets[i].id == Stickers::FavedSetId) {
		++i;
		icons.push_back(StickerIcon(Stickers::FavedSetId));
	}
	if (i != _mySets.size() && _mySets[i].id == Stickers::RecentSetId) {
		++i;
		if (!icons.empty() && icons.back().setId != Stickers::FavedSetId) {
			icons.push_back(StickerIcon(Stickers::RecentSetId));
		}
	}
	for (auto l = _mySets.size(); i != l; ++i) {
		if (_mySets[i].id == Stickers::MegagroupSetId) {
			icons.push_back(StickerIcon(Stickers::MegagroupSetId));
			icons.back().megagroup = _megagroupSet;
			continue;
		}
		auto s = _mySets[i].pack[0];
		auto availw = st::stickerIconWidth - 2 * st::stickerIconPadding, availh = st::emojiFooterHeight - 2 * st::stickerIconPadding;
		auto thumbw = s->thumb->width(), thumbh = s->thumb->height(), pixw = 1, pixh = 1;
		if (availw * thumbh > availh * thumbw) {
			pixh = availh;
			pixw = (pixh * thumbw) / thumbh;
		} else {
			pixw = availw;
			pixh = thumbw ? ((pixw * thumbh) / thumbw) : 1;
		}
		if (pixw < 1) pixw = 1;
		if (pixh < 1) pixh = 1;
		icons.push_back(StickerIcon(_mySets[i].id, s, pixw, pixh));
	}

	if (!Auth().data().featuredStickerSetsUnreadCount()
		&& !_featuredSets.empty()) {
		icons.push_back(StickerIcon(Stickers::FeaturedSetId));
	}
}

bool StickersListWidget::preventAutoHide() {
	return _removingSetId != 0 || _displayingSetId != 0;
}

void StickersListWidget::updateSelected() {
	if (_pressed && !_previewShown) {
		return;
	}

	auto newSelected = OverState { base::none };
	auto p = mapFromGlobal(_lastMousePosition);
	if (!rect().contains(p)
		|| p.y() < getVisibleTop() || p.y() >= getVisibleBottom()
		|| !isVisible()) {
		clearSelection();
		return;
	}
	auto &sets = shownSets();
	auto sx = (rtl() ? width() - p.x() : p.x()) - stickersLeft();
	if (!shownSets().empty()) {
		auto info = sectionInfoByOffset(p.y());
		auto section = info.section;
		if (p.y() >= info.top && p.y() < info.rowsTop) {
			if (hasRemoveButton(section) && myrtlrect(removeButtonRect(section)).contains(p.x(), p.y())) {
				newSelected = OverButton { section };
			} else if (featuredHasAddButton(section) && myrtlrect(featuredAddRect(section)).contains(p.x(), p.y())) {
				newSelected = OverButton{ section };
			} else if (!(sets[section].flags & MTPDstickerSet_ClientFlag::f_special)) {
				newSelected = OverSet { section };
			} else if (sets[section].id == Stickers::MegagroupSetId
					&& (_megagroupSet->canEditStickers() || !sets[section].pack.empty())) {
				newSelected = OverSet { section };
			}
		} else if (p.y() >= info.rowsTop && p.y() < info.rowsBottom && sx >= 0) {
			auto yOffset = p.y() - info.rowsTop;
			auto &set = sets[section];
			if (set.id == Stickers::MegagroupSetId && set.pack.empty()) {
				if (_megagroupSetButtonRect.contains(stickersLeft() + sx, yOffset)) {
					newSelected = OverGroupAdd {};
				}
			} else {
				auto special = ((set.flags & MTPDstickerSet::Flag::f_official) != 0);
				auto rowIndex = qFloor(yOffset / _singleSize.height());
				auto columnIndex = qFloor(sx / _singleSize.width());
				auto index = rowIndex * _columnCount + columnIndex;
				if (index >= 0 && index < set.pack.size()) {
					auto overDelete = false;
					if (stickerHasDeleteButton(set, index)) {
						auto inx = sx - (columnIndex * _singleSize.width());
						auto iny = yOffset - (rowIndex * _singleSize.height());
						if (inx >= _singleSize.width() - st::stickerPanDeleteIconBg.width() && iny < st::stickerPanDeleteIconBg.height()) {
							overDelete = true;
						}
					}
					newSelected = OverSticker { section, index, overDelete };
				}
			}
		}
	}

	setSelected(newSelected);
}

bool StickersListWidget::setHasTitle(const Set &set) const {
	if (set.id == Stickers::FavedSetId) {
		return false;
	} else if (set.id == Stickers::RecentSetId) {
		return !_mySets.empty() && _mySets[0].id == Stickers::FavedSetId;
	}
	return true;
}

bool StickersListWidget::stickerHasDeleteButton(const Set &set, int index) const {
	if (set.id == Stickers::RecentSetId) {
		Assert(index >= 0 && index < _custom.size());
		return _custom[index];
	}
	return (set.id == Stickers::FavedSetId);
}

void StickersListWidget::setSelected(OverState newSelected) {
	if (_selected != newSelected) {
		setCursor(newSelected ? style::cur_pointer : style::cur_default);

		auto &sets = shownSets();
		auto updateSelected = [&]() {
			if (auto sticker = base::get_if<OverSticker>(&_selected)) {
				rtlupdate(stickerRect(sticker->section, sticker->index));
			} else if (auto button = base::get_if<OverButton>(&_selected)) {
				if (button->section >= 0
					&& button->section < sets.size()
					&& sets[button->section].externalLayout) {
					rtlupdate(featuredAddRect(button->section));
				} else {
					rtlupdate(removeButtonRect(button->section));
				}
			} else if (base::get_if<OverGroupAdd>(&_selected)) {
				rtlupdate(megagroupSetButtonRectFinal());
			}
		};
		updateSelected();
		_selected = newSelected;
		updateSelected();

		if (_previewShown && _pressed != _selected) {
			if (auto sticker = base::get_if<OverSticker>(&_selected)) {
				_pressed = _selected;
				Assert(sticker->section >= 0 && sticker->section < sets.size());
				auto &set = sets[sticker->section];
				Assert(sticker->index >= 0 && sticker->index < set.pack.size());
				Ui::showMediaPreview(set.pack[sticker->index]);
			}
		}
	}
}

void StickersListWidget::onSettings() {
	Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
}

void StickersListWidget::onPreview() {
	if (auto sticker = base::get_if<OverSticker>(&_pressed)) {
		auto &sets = shownSets();
		Assert(sticker->section >= 0 && sticker->section < sets.size());
		auto &set = sets[sticker->section];
		Assert(sticker->index >= 0 && sticker->index < set.pack.size());
		Ui::showMediaPreview(set.pack[sticker->index]);
		_previewShown = true;
	}
}

void StickersListWidget::showStickerSet(uint64 setId) {
	clearSelection();

	if (setId == Stickers::FeaturedSetId) {
		if (_section != Section::Featured) {
			_section = Section::Featured;

			refreshRecentStickers(true);
			refreshSettingsVisibility();
			if (_footer) {
				_footer->refreshIcons(ValidateIconAnimations::Scroll);
			}
			update();
		}

		emit scrollToY(0);
		emit scrollUpdated();
		return;
	}

	auto needRefresh = (_section != Section::Stickers);
	if (needRefresh) {
		_section = Section::Stickers;
		refreshRecentStickers(true);
		refreshSettingsVisibility();
	}

	auto y = 0;
	enumerateSections([this, setId, &y](const SectionInfo &info) {
		if (shownSets()[info.section].id == setId) {
			y = info.top;
			return false;
		}
		return true;
	});
	emit scrollToY(y);
	emit scrollUpdated();

	if (needRefresh && _footer) {
		_footer->refreshIcons(ValidateIconAnimations::Scroll);
	}

	_lastMousePosition = QCursor::pos();

	update();
}

void StickersListWidget::refreshMegagroupSetGeometry() {
	auto left = megagroupSetInfoLeft();
	auto availableWidth = (width() - left);
	auto top = _megagroupSetAbout.countHeight(availableWidth) + st::stickerGroupCategoryAddMargin.top();
	_megagroupSetButtonTextWidth = st::stickerGroupCategoryAdd.font->width(_megagroupSetButtonText);
	auto buttonWidth = _megagroupSetButtonTextWidth - st::stickerGroupCategoryAdd.width;
	_megagroupSetButtonRect = QRect(left, top, buttonWidth, st::stickerGroupCategoryAdd.height);
}

void StickersListWidget::showMegagroupSet(ChannelData *megagroup) {
	Expects(!megagroup || megagroup->isMegagroup());

	if (_megagroupSet != megagroup) {
		_megagroupSet = megagroup;

		if (_megagroupSetAbout.isEmpty()) {
			_megagroupSetAbout.setText(
				st::stickerGroupCategoryAbout,
				lang(lng_group_stickers_description));
			_megagroupSetButtonText = lang(lng_group_stickers_add).toUpper();
			refreshMegagroupSetGeometry();
		}
		_megagroupSetButtonRipple.reset();

		refreshStickers();
	}
}

void StickersListWidget::afterShown() {
	if (_footer) {
		_footer->stealFocus();
	}
}

void StickersListWidget::beforeHiding() {
	if (_footer) {
		_footer->returnFocus();
	}
}

void StickersListWidget::displaySet(uint64 setId) {
	if (setId == Stickers::MegagroupSetId) {
		if (_megagroupSet->canEditStickers()) {
			_displayingSetId = setId;
			auto box = Ui::show(Box<StickersBox>(_megagroupSet));
			connect(box, &QObject::destroyed, this, [this] {
				_displayingSetId = 0;
				emit checkForHide();
			});
			return;
		} else if (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetID) {
			setId = _megagroupSet->mgInfo->stickerSet.c_inputStickerSetID().vid.v;
		} else {
			return;
		}
	}
	auto &sets = Auth().data().stickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		_displayingSetId = setId;
		auto box = Ui::show(
			Box<StickerSetBox>(Stickers::inputSetId(*it)),
			LayerOption::KeepOther);
		connect(box, &QObject::destroyed, this, [this] {
			_displayingSetId = 0;
			emit checkForHide();
		});
	}
}

void StickersListWidget::installSet(uint64 setId) {
	auto &sets = Auth().data().stickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		const auto input = Stickers::inputSetId(*it);
		if ((it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)
			|| it->stickers.empty()) {
			request(MTPmessages_GetStickerSet(
				input
			)).done([=](const MTPmessages_StickerSet &result) {
				Stickers::FeedSetFull(result);
				sendInstallRequest(setId, input);
			}).send();
		} else {
			sendInstallRequest(setId, input);
		}
	}
}

void StickersListWidget::sendInstallRequest(
		uint64 setId,
		const MTPInputStickerSet &input) {
	request(MTPmessages_InstallStickerSet(
		input,
		MTP_bool(false)
	)).done([=](const MTPmessages_StickerSetInstallResult &result) {
		if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
			Stickers::ApplyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
		}
	}).fail([=](const RPCError &error) {
		notInstalledLocally(setId);
		Stickers::UndoInstallLocally(setId);
	}).send();

	installedLocally(setId);
	Stickers::InstallLocally(setId);
}

void StickersListWidget::removeMegagroupSet(bool locally) {
	if (locally) {
		Auth().settings().setGroupStickersSectionHidden(_megagroupSet->id);
		Local::writeUserSettings();
		refreshStickers();
		return;
	}
	_removingSetId = Stickers::MegagroupSetId;
	Ui::show(Box<ConfirmBox>(lang(lng_stickers_remove_group_set), base::lambda_guarded(this, [this, group = _megagroupSet] {
		Expects(group->mgInfo != nullptr);
		if (group->mgInfo->stickerSet.type() != mtpc_inputStickerSetEmpty) {
			Auth().api().setGroupStickerSet(group, MTP_inputStickerSetEmpty());
		}
		Ui::hideLayer();
		_removingSetId = 0;
		emit checkForHide();
	}), base::lambda_guarded(this, [this] {
		_removingSetId = 0;
		emit checkForHide();
	})));
}

void StickersListWidget::removeSet(uint64 setId) {
	auto &sets = Auth().data().stickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		_removingSetId = it->id;
		auto text = lng_stickers_remove_pack(lt_sticker_pack, it->title);
		Ui::show(Box<ConfirmBox>(text, lang(lng_stickers_remove_pack_confirm), base::lambda_guarded(this, [this] {
			Ui::hideLayer();
			auto &sets = Auth().data().stickerSetsRef();
			auto it = sets.find(_removingSetId);
			if (it != sets.cend()) {
				if (it->id && it->access) {
					request(MTPmessages_UninstallStickerSet(MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)))).send();
				} else if (!it->shortName.isEmpty()) {
					request(MTPmessages_UninstallStickerSet(MTP_inputStickerSetShortName(MTP_string(it->shortName)))).send();
				}
				auto writeRecent = false;
				auto &recent = Stickers::GetRecentPack();
				for (auto i = recent.begin(); i != recent.cend();) {
					if (it->stickers.indexOf(i->first) >= 0) {
						i = recent.erase(i);
						writeRecent = true;
					} else {
						++i;
					}
				}
				it->flags &= ~MTPDstickerSet::Flag::f_installed_date;
				it->installDate = TimeId(0);
				//
				// Set can be in search results.
				//
				//if (!(it->flags & MTPDstickerSet_ClientFlag::f_featured)
				//	&& !(it->flags & MTPDstickerSet_ClientFlag::f_special)) {
				//	sets.erase(it);
				//}
				int removeIndex = Auth().data().stickerSetsOrder().indexOf(_removingSetId);
				if (removeIndex >= 0) Auth().data().stickerSetsOrderRef().removeAt(removeIndex);
				refreshStickers();
				Local::writeInstalledStickers();
				if (writeRecent) Local::writeUserSettings();
			}
			_removingSetId = 0;
			emit checkForHide();
		}), base::lambda_guarded(this, [this] {
			_removingSetId = 0;
			emit checkForHide();
		})));
	}
}

StickersListWidget::~StickersListWidget() = default;

} // namespace ChatHelpers
