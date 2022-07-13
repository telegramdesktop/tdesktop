/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_list_widget.h"

#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/text/custom_emoji_instance.h"
#include "ui/effects/ripple_animation.h"
#include "ui/emoji_config.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "boxes/sticker_set_box.h"
#include "lang/lang_keys.h"
#include "layout/layout_position.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "chat_helpers/stickers_list_widget.h"
#include "chat_helpers/stickers_list_footer.h"
#include "emoji_suggestions_data.h"
#include "emoji_suggestions_helper.h"
#include "main/main_session.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "window/window_session_controller.h"
#include "facades.h"
#include "styles/style_chat_helpers.h"

namespace ChatHelpers {

class EmojiColorPicker : public Ui::RpWidget {
public:
	EmojiColorPicker(QWidget *parent);

	void showEmoji(EmojiPtr emoji);

	void clearSelection();
	void handleMouseMove(QPoint globalPos);
	void handleMouseRelease(QPoint globalPos);
	void setSingleSize(QSize size);

	void showAnimated();
	void hideAnimated();
	void hideFast();

	rpl::producer<EmojiPtr> chosen() const;
	rpl::producer<> hidden() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void animationCallback();
	void updateSize();

	void drawVariant(Painter &p, int variant);

	void updateSelected();
	void setSelected(int newSelected);

	bool _ignoreShow = false;

	QVector<EmojiPtr> _variants;

	int _selected = -1;
	int _pressedSel = -1;
	QPoint _lastMousePos;
	QSize _singleSize;

	bool _hiding = false;
	QPixmap _cache;
	Ui::Animations::Simple _a_opacity;

	rpl::event_stream<EmojiPtr> _chosen;
	rpl::event_stream<> _hidden;

};

#if 0
class EmojiListWidget::Footer : public TabbedSelector::InnerFooter {
public:
	Footer(not_null<EmojiListWidget*> parent);

	void setCurrentSectionIcon(Section section);

protected:
	void processPanelHideFinished() override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void setActiveSection(Section section);

	not_null<EmojiListWidget*> _pan;
	std::array<object_ptr<Ui::IconButton>, kEmojiSectionCount> _sections;

};
#endif

struct EmojiListWidget::CustomInstance {
	CustomInstance(
		std::unique_ptr<Ui::CustomEmoji::Loader> loader,
		Fn<void(
			not_null<Ui::CustomEmoji::Instance*>,
			Ui::CustomEmoji::RepaintRequest)> repaintLater,
		Fn<void()> repaint);

	Ui::CustomEmoji::Instance emoji;
	Ui::CustomEmoji::Object object;
};

EmojiListWidget::CustomInstance::CustomInstance(
	std::unique_ptr<Ui::CustomEmoji::Loader> loader,
	Fn<void(
		not_null<Ui::CustomEmoji::Instance*>,
		Ui::CustomEmoji::RepaintRequest)> repaintLater,
	Fn<void()> repaint)
: emoji(
	Ui::CustomEmoji::Loading(std::move(loader), Ui::CustomEmoji::Preview()),
	std::move(repaintLater))
, object(&emoji, std::move(repaint)) {
}

#if 0
EmojiListWidget::Footer::Footer(not_null<EmojiListWidget*> parent)
: InnerFooter(parent)
, _pan(parent)
, _sections { {
	object_ptr<Ui::IconButton>(this, st::emojiCategoryRecent),
	object_ptr<Ui::IconButton>(this, st::emojiCategoryPeople),
	object_ptr<Ui::IconButton>(this, st::emojiCategoryNature),
	object_ptr<Ui::IconButton>(this, st::emojiCategoryFood),
	object_ptr<Ui::IconButton>(this, st::emojiCategoryActivity),
	object_ptr<Ui::IconButton>(this, st::emojiCategoryTravel),
	object_ptr<Ui::IconButton>(this, st::emojiCategoryObjects),
	object_ptr<Ui::IconButton>(this, st::emojiCategorySymbols),
} } {
	for (auto i = 0; i != _sections.size(); ++i) {
		auto value = static_cast<Section>(i);
		_sections[i]->setClickedCallback([=] {
			setActiveSection(value);
		});
	}
	setCurrentSectionIcon(Section::Recent);
}

void EmojiListWidget::Footer::resizeEvent(QResizeEvent *e) {
	auto availableWidth = (width() - st::emojiCategorySkip * 2);
	auto buttonWidth = availableWidth / _sections.size();
	auto buttonsWidth = buttonWidth * _sections.size();
	auto left = (width() - buttonsWidth) / 2;
	for (auto &button : _sections) {
		button->resizeToWidth(buttonWidth);
		button->moveToLeft(left, 0);
		left += button->width();
	}
}

void EmojiListWidget::Footer::processPanelHideFinished() {
	// Preserve panel state through visibility toggles.
	//setCurrentSectionIcon(Section::Recent);
}

void EmojiListWidget::Footer::setCurrentSectionIcon(Section section) {
	std::array<const style::icon *, kEmojiSectionCount> overrides = { {
		&st::emojiRecentActive,
		&st::emojiPeopleActive,
		&st::emojiNatureActive,
		&st::emojiFoodActive,
		&st::emojiActivityActive,
		&st::emojiTravelActive,
		&st::emojiObjectsActive,
		&st::emojiSymbolsActive,
	} };
	for (auto i = 0; i != _sections.size(); ++i) {
		_sections[i]->setIconOverride((section == static_cast<Section>(i)) ? overrides[i] : nullptr);
	}
}

void EmojiListWidget::Footer::setActiveSection(Ui::Emoji::Section section) {
	_pan->showEmojiSection(section);
}
#endif

EmojiColorPicker::EmojiColorPicker(QWidget *parent)
: RpWidget(parent) {
	setMouseTracking(true);
}

void EmojiColorPicker::showEmoji(EmojiPtr emoji) {
	if (!emoji || !emoji->hasVariants()) {
		return;
	}
	_ignoreShow = false;

	_variants.resize(emoji->variantsCount() + 1);
	for (auto i = 0, size = int(_variants.size()); i != size; ++i) {
		_variants[i] = emoji->variant(i);
	}

	updateSize();

	if (!_cache.isNull()) _cache = QPixmap();
	showAnimated();
}

void EmojiColorPicker::updateSize() {
	auto width = st::emojiPanMargins.left()
		+ _singleSize.width() * _variants.size()
		+ (_variants.size() - 2) * st::emojiColorsPadding
		+ st::emojiColorsSep
		+ st::emojiPanMargins.right();
	auto height = st::emojiPanMargins.top()
		+ 2 * st::emojiColorsPadding
		+ _singleSize.height()
		+ st::emojiPanMargins.bottom();
	resize(width, height);
	update();
	updateSelected();
}

void EmojiColorPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto opacity = _a_opacity.value(_hiding ? 0. : 1.);
	if (opacity < 1.) {
		if (opacity > 0.) {
			p.setOpacity(opacity);
		} else {
			return;
		}
	}
	if (e->rect() != rect()) {
		p.setClipRect(e->rect());
	}

	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	if (!_cache.isNull()) {
		p.drawPixmap(0, 0, _cache);
		return;
	}
	Ui::Shadow::paint(p, inner, width(), st::defaultRoundShadow);
	Ui::FillRoundRect(p, inner, st::boxBg, Ui::BoxCorners);

	auto x = st::emojiPanMargins.left() + 2 * st::emojiColorsPadding + _singleSize.width();
	if (rtl()) x = width() - x - st::emojiColorsSep;
	p.fillRect(x, st::emojiPanMargins.top() + st::emojiColorsPadding, st::emojiColorsSep, inner.height() - st::emojiColorsPadding * 2, st::emojiColorsSepColor);

	if (_variants.isEmpty()) return;
	for (auto i = 0, count = int(_variants.size()); i != count; ++i) {
		drawVariant(p, i);
	}
}

void EmojiColorPicker::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();
	_pressedSel = _selected;
}

void EmojiColorPicker::mouseReleaseEvent(QMouseEvent *e) {
	handleMouseRelease(e->globalPos());
}

void EmojiColorPicker::handleMouseRelease(QPoint globalPos) {
	_lastMousePos = globalPos;
	int32 pressed = _pressedSel;
	_pressedSel = -1;

	updateSelected();
	if (_selected >= 0 && (pressed < 0 || _selected == pressed)) {
		_chosen.fire_copy(_variants[_selected]);
	}
	_ignoreShow = true;
	hideAnimated();
}

void EmojiColorPicker::setSingleSize(QSize size) {
	_singleSize = size;
	updateSize();
}

void EmojiColorPicker::handleMouseMove(QPoint globalPos) {
	_lastMousePos = globalPos;
	updateSelected();
}

void EmojiColorPicker::mouseMoveEvent(QMouseEvent *e) {
	handleMouseMove(e->globalPos());
}

void EmojiColorPicker::animationCallback() {
	update();
	if (!_a_opacity.animating()) {
		_cache = QPixmap();
		if (_hiding) {
			hide();
			_hidden.fire({});
		} else {
			_lastMousePos = QCursor::pos();
			updateSelected();
		}
	}
}

void EmojiColorPicker::hideFast() {
	clearSelection();
	_a_opacity.stop();
	_cache = QPixmap();
	hide();
	_hidden.fire({});
}

rpl::producer<EmojiPtr> EmojiColorPicker::chosen() const {
	return _chosen.events();
}

rpl::producer<> EmojiColorPicker::hidden() const {
	return _hidden.events();
}

void EmojiColorPicker::hideAnimated() {
	if (_cache.isNull()) {
		_cache = Ui::GrabWidget(this);
		clearSelection();
	}
	_hiding = true;
	_a_opacity.start([this] { animationCallback(); }, 1., 0., st::emojiPanDuration);
}

void EmojiColorPicker::showAnimated() {
	if (_ignoreShow) return;

	if (!isHidden() && !_hiding) {
		return;
	}
	_hiding = false;
	if (_cache.isNull()) {
		_cache = Ui::GrabWidget(this);
		clearSelection();
	}
	show();
	_a_opacity.start([this] { animationCallback(); }, 0., 1., st::emojiPanDuration);
}

void EmojiColorPicker::clearSelection() {
	_pressedSel = -1;
	setSelected(-1);
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
}

void EmojiColorPicker::updateSelected() {
	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);
	auto sx = rtl() ? (width() - p.x()) : p.x(), y = p.y() - st::emojiPanMargins.top() - st::emojiColorsPadding;
	if (y >= 0 && y < _singleSize.height()) {
		auto x = sx - st::emojiPanMargins.left() - st::emojiColorsPadding;
		if (x >= 0 && x < _singleSize.width()) {
			newSelected = 0;
		} else {
			x -= _singleSize.width() + 2 * st::emojiColorsPadding + st::emojiColorsSep;
			if (x >= 0 && x < _singleSize.width() * (_variants.size() - 1)) {
				newSelected = (x / _singleSize.width()) + 1;
			}
		}
	}

	setSelected(newSelected);
}

void EmojiColorPicker::setSelected(int newSelected) {
	if (_selected == newSelected) {
		return;
	}
	auto updateSelectedRect = [this] {
		if (_selected < 0) return;
		auto addedSkip = (_selected > 0)
			? (2 * st::emojiColorsPadding + st::emojiColorsSep)
			: 0;
		auto left = st::emojiPanMargins.left()
			+ st::emojiColorsPadding
			+ _selected * _singleSize.width()
			+ addedSkip;
		rtlupdate(
			left,
			st::emojiPanMargins.top() + st::emojiColorsPadding,
			_singleSize.width(),
			_singleSize.height());
	};
	updateSelectedRect();
	_selected = newSelected;
	updateSelectedRect();
	setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
}

void EmojiColorPicker::drawVariant(Painter &p, int variant) {
	QPoint w(st::emojiPanMargins.left() + st::emojiColorsPadding + variant * _singleSize.width() + (variant ? 2 * st::emojiColorsPadding + st::emojiColorsSep : 0), st::emojiPanMargins.top() + st::emojiColorsPadding);
	if (variant == _selected) {
		QPoint tl(w);
		if (rtl()) tl.setX(width() - tl.x() - _singleSize.width());
		Ui::FillRoundRect(p, QRect(tl, _singleSize), st::emojiPanHover, Ui::StickerHoverCorners);
	}
	const auto esize = Ui::Emoji::GetSizeLarge();
	Ui::Emoji::Draw(
		p,
		_variants[variant],
		esize,
		w.x() + (_singleSize.width() - (esize / cIntRetinaFactor())) / 2,
		w.y() + (_singleSize.height() - (esize / cIntRetinaFactor())) / 2);
}

EmojiListWidget::EmojiListWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Inner(parent, controller)
, _picker(this)
, _showPickerTimer([=] { showPicker(); })
, _repaintTimer([=] { invokeRepaints(); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_picker->hide();

	_esize = Ui::Emoji::GetSizeLarge();

	for (auto i = 0; i != kEmojiSectionCount; ++i) {
		const auto section = static_cast<Section>(i);
		_counts[i] = (section == Section::Recent)
			? int(Core::App().settings().recentEmoji().size())
			: Ui::Emoji::GetSectionCount(section);
	}

	_picker->chosen(
	) | rpl::start_with_next([=](EmojiPtr emoji) {
		colorChosen(emoji);
	}, lifetime());

	_picker->hidden(
	) | rpl::start_with_next([=] {
		pickerHidden();
	}, lifetime());

	controller->session().data().stickers().updated(
	) | rpl::start_with_next([=] {
		refreshCustom();
		resizeToWidth(width());
	}, lifetime());
}

EmojiListWidget::~EmojiListWidget() {
	base::take(_instances);
	base::take(_repaints);
}

void EmojiListWidget::repaintLater(
		uint64 setId,
		Ui::CustomEmoji::RepaintRequest request) {
	if (_instances.empty()) {
		return;
	}
	auto &repaint = _repaints[request.duration];
	if (repaint.when < request.when) {
		repaint.when = request.when;
	}
	repaint.ids.emplace(setId);
	scheduleRepaintTimer();
}

void EmojiListWidget::scheduleRepaintTimer() {
	if (_repaintTimerScheduled) {
		return;
	}
	_repaintTimerScheduled = true;
	Ui::PostponeCall(this, [=] {
		_repaintTimerScheduled = false;

		auto next = crl::time();
		for (const auto &[duration, bunch] : _repaints) {
			if (!next || next > bunch.when) {
				next = bunch.when;
			}
		}
		if (next && (!_repaintNext || _repaintNext > next)) {
			const auto now = crl::now();
			if (now >= next) {
				_repaintNext = 0;
				_repaintTimer.cancel();
				invokeRepaints();
			} else {
				_repaintNext = next;
				_repaintTimer.callOnce(next - now);
			}
		}
	});
}

void EmojiListWidget::invokeRepaints() {
	_repaintNext = 0;
	auto ids = base::flat_set<uint64>();
	const auto now = crl::now();
	for (auto i = begin(_repaints); i != end(_repaints);) {
		if (i->second.when > now) {
			++i;
			continue;
		}
		if (ids.empty()) {
			ids = std::move(i->second.ids);
		} else {
			for (const auto id : i->second.ids) {
				ids.emplace(id);
			}
		}
		i = _repaints.erase(i);
	}
	repaintCustom([&](uint64 id) { return ids.contains(id); });
	scheduleRepaintTimer();
}

template <typename CheckId>
void EmojiListWidget::repaintCustom(CheckId checkId) {
	enumerateSections([&](const SectionInfo &info) {
		if (info.section >= kEmojiSectionCount
			&& checkId(_custom[info.section - kEmojiSectionCount].id)) {
			update(
				0,
				info.rowsTop,
				width(),
				info.rowsBottom - info.rowsTop);
		}
		return true;
	});
}

rpl::producer<EmojiPtr> EmojiListWidget::chosen() const {
	return _chosen.events();
}

auto EmojiListWidget::customChosen() const
-> rpl::producer<TabbedSelector::FileChosen> {
	return _customChosen.events();
}

void EmojiListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	Inner::visibleTopBottomUpdated(visibleTop, visibleBottom);
	if (_footer) {
		_footer->validateSelectedIcon(
			currentSet(visibleTop),
			ValidateIconAnimations::Full);
	}
	unloadNotSeenCustom(visibleTop, visibleBottom);
}

void EmojiListWidget::unloadNotSeenCustom(
		int visibleTop,
		int visibleBottom) {
	enumerateSections([&](const SectionInfo &info) {
		if (info.section < kEmojiSectionCount
			|| (info.rowsBottom > visibleTop
				&& info.rowsTop < visibleBottom)) {
			return true;
		}
		auto &custom = _custom[info.section - kEmojiSectionCount];
		if (!custom.painted) {
			return true;
		}
		custom.painted = false;
		for (const auto &single : custom.list) {
			single.instance->object.unload();
		}
		return true;
	});
}

object_ptr<TabbedSelector::InnerFooter> EmojiListWidget::createFooter() {
	Expects(_footer == nullptr);

	using FooterDescriptor = StickersListFooter::Descriptor;
	auto result = object_ptr<StickersListFooter>(FooterDescriptor{
		.controller = controller(),
		.parent = this,
	});
	_footer = result;

	_footer->setChosen(
	) | rpl::start_with_next([=](uint64 setId) {
		showSet(setId);
	}, _footer->lifetime());

	return result;
}

template <typename Callback>
bool EmojiListWidget::enumerateSections(Callback callback) const {
	Expects(_columnCount > 0);

	auto i = 0;
	auto info = SectionInfo();
	const auto next = [&] {
		info.rowsCount = (info.count + _columnCount - 1) / _columnCount;
		info.rowsTop = info.top + (i == 0 ? st::emojiPanPadding : st::emojiPanHeader);
		info.rowsBottom = info.rowsTop + info.rowsCount * _singleSize.height();
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
		return true;
	};
	for (; i != kEmojiSectionCount; ++i) {
		info.section = i;
		info.count = _counts[i];
		if (!next()) {
			return false;
		}
	}
	for (auto &section : _custom) {
		info.section = i++;
		info.count = int(section.list.size());
		if (!next()) {
			return false;
		}
	}
	return true;
}

EmojiListWidget::SectionInfo EmojiListWidget::sectionInfo(int section) const {
	Expects(section >= 0 && section < sectionsCount());

	auto result = SectionInfo();
	enumerateSections([&](const SectionInfo &info) {
		if (info.section == section) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

EmojiListWidget::SectionInfo EmojiListWidget::sectionInfoByOffset(
		int yOffset) const {
	auto result = SectionInfo();
	const auto count = sectionsCount();
	enumerateSections([&result, count, yOffset](const SectionInfo &info) {
		if (yOffset < info.rowsBottom || info.section == count - 1) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

int EmojiListWidget::sectionsCount() const {
	return kEmojiSectionCount + int(_custom.size());
}

int EmojiListWidget::countDesiredHeight(int newWidth) {
	const auto fullWidth = st::roundRadiusSmall
		+ newWidth
		+ st::emojiScroll.width;
	_columnCount = std::max(
		(fullWidth - st::emojiPadding * 2) / st::emojiPanDesiredSize,
		1);

	_rowsLeft = fullWidth / (_columnCount * 4 + 2);
	auto rowsRight = std::max(_rowsLeft, st::emojiScroll.width);
	auto singleWidth = (fullWidth - _rowsLeft - rowsRight)
		/ _columnCount;
	_rowsLeft -= st::roundRadiusSmall;
	_singleSize = QSize(singleWidth, singleWidth - 4 * st::lineWidth);
	_picker->setSingleSize(_singleSize);

	auto visibleHeight = minimalHeight();
	auto minimalHeight = (visibleHeight - st::stickerPanPadding);
	auto countResult = [this](int minimalLastHeight) {
		const auto info = sectionInfo(sectionsCount() - 1);
		return info.top
			+ qMax(info.rowsBottom - info.top, minimalLastHeight);
	};
	const auto minimalLastHeight = minimalHeight;
	return qMax(minimalHeight, countResult(minimalLastHeight))
		+ st::emojiPanPadding;
}

void EmojiListWidget::ensureLoaded(int section) {
	Expects(section >= 0 && section < sectionsCount());

	if (section >= kEmojiSectionCount || !_emoji[section].empty()) {
		return;
	}
	_emoji[section] = (static_cast<Section>(section) == Section::Recent)
		? Core::App().settings().recentEmojiSection()
		: Ui::Emoji::GetSection(static_cast<Section>(section));
	_counts[section] = _emoji[section].size();
	if (static_cast<Section>(section) == Section::Recent) {
		return;
	}
	const auto &variants = Core::App().settings().emojiVariants();
	for (auto &emoji : _emoji[section]) {
		if (emoji->hasVariants()) {
			const auto j = variants.find(emoji->nonColoredId());
			if (j != end(variants)) {
				emoji = emoji->variant(j->second);
			}
		}
	}
}

void EmojiListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::emojiPanBg);

	auto fromColumn = floorclamp(r.x() - _rowsLeft, _singleSize.width(), 0, _columnCount);
	auto toColumn = ceilclamp(r.x() + r.width() - _rowsLeft, _singleSize.width(), 0, _columnCount);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = _columnCount - fromColumn;
		toColumn = _columnCount - toColumn;
	}

	const auto paused = controller()->isGifPausedAtLeastFor(
		Window::GifPauseReason::SavedGifs);
	const auto now = crl::now();
	auto selectedButton = std::get_if<OverButton>(!v::is_null(_pressed)
		? &_pressed
		: &_selected);
	enumerateSections([&](const SectionInfo &info) {
		if (r.top() >= info.rowsBottom) {
			return true;
		} else if (r.top() + r.height() <= info.top) {
			return false;
		}
		if (info.section > 0 && r.top() < info.rowsTop) {
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			auto titleText = (info.section < kEmojiSectionCount)
				? ChatHelpers::EmojiCategoryTitle(info.section)(tr::now)
				: _custom[info.section - kEmojiSectionCount].title;
			auto titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			auto widthForTitle = emojiRight() - (st::emojiPanHeaderLeft - st::roundRadiusSmall);
			if (hasRemoveButton(info.section)) {
				auto &custom = _custom[info.section - kEmojiSectionCount];
				auto remove = removeButtonRect(info.section);
				auto selected = selectedButton ? (selectedButton->section == info.section) : false;
				if (custom.ripple) {
					custom.ripple->paint(p, remove.x() + st::stickerPanRemoveSet.rippleAreaPosition.x(), remove.y() + st::stickerPanRemoveSet.rippleAreaPosition.y(), width());
					if (custom.ripple->empty()) {
						custom.ripple.reset();
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
			p.drawTextLeft(st::emojiPanHeaderLeft - st::roundRadiusSmall, info.top + st::emojiPanHeaderTop, width(), titleText, titleWidth);
		}
		if (r.top() + r.height() > info.rowsTop) {
			ensureLoaded(info.section);
			auto fromRow = floorclamp(r.y() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
			auto toRow = ceilclamp(r.y() + r.height() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
			for (auto i = fromRow; i < toRow; ++i) {
				for (auto j = fromColumn; j < toColumn; ++j) {
					auto index = i * _columnCount + j;
					if (index >= info.count) break;

					const auto state = OverEmoji{
						.section = info.section,
						.index = index,
					};
					const auto selected = (state == _selected)
						|| (!_picker->isHidden()
							&& state == _pickerSelected);

					auto w = QPoint(_rowsLeft + j * _singleSize.width(), info.rowsTop + i * _singleSize.height());
					if (selected) {
						auto tl = w;
						if (rtl()) tl.setX(width() - tl.x() - _singleSize.width());
						Ui::FillRoundRect(p, QRect(tl, _singleSize), st::emojiPanHover, Ui::StickerHoverCorners);
					}
					if (info.section < kEmojiSectionCount) {
						drawEmoji(p, w, info.section, index);
					} else {
						const auto set = info.section - kEmojiSectionCount;
						drawCustom(p, w, now, paused, set, index);
					}
				}
			}
		}
		return true;
	});
}

void EmojiListWidget::drawEmoji(
		QPainter &p,
		QPoint position,
		int section,
		int index) {
	const auto size = (_esize / cIntRetinaFactor());
	Ui::Emoji::Draw(
		p,
		_emoji[section][index],
		_esize,
		position.x() + (_singleSize.width() - size) / 2,
		position.y() + (_singleSize.height() - size) / 2);
}

void EmojiListWidget::drawCustom(
		QPainter &p,
		QPoint position,
		crl::time now,
		bool paused,
		int set,
		int index) {
	const auto size = (_esize / cIntRetinaFactor());
	_custom[set].painted = true;
	_custom[set].list[index].instance->object.paint(
		p,
		position.x() + (_singleSize.width() - size) / 2,
		position.y() + (_singleSize.height() - size) / 2,
		now,
		st::windowBgRipple->c,
		paused);
}

bool EmojiListWidget::checkPickerHide() {
	if (!_picker->isHidden() && !v::is_null(_pickerSelected)) {
		_picker->hideAnimated();
		_pickerSelected = v::null;
		updateSelected();
		return true;
	}
	return false;
}

void EmojiListWidget::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (checkPickerHide() || e->button() != Qt::LeftButton) {
		return;
	}
	setPressed(_selected);
	if (const auto over = std::get_if<OverEmoji>(&_selected)) {
		if (over->section < kEmojiSectionCount
			&& over->index < _emoji[over->section].size()
			&& _emoji[over->section][over->index]->hasVariants()) {
			_pickerSelected = _selected;
			setCursor(style::cur_default);
			const auto &variants = Core::App().settings().emojiVariants();
			if (!variants.contains(_emoji[over->section][over->index]->nonColoredId())) {
				showPicker();
			} else {
				_showPickerTimer.callOnce(500);
			}
		}
	}
}

void EmojiListWidget::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	setPressed(v::null);
	_lastMousePos = e->globalPos();
	if (!_picker->isHidden()) {
		if (_picker->rect().contains(_picker->mapFromGlobal(_lastMousePos))) {
			return _picker->handleMouseRelease(QCursor::pos());
		} else if (const auto over = std::get_if<OverEmoji>(&_pickerSelected)) {
			const auto section = over->section;
			const auto index = over->index;
			if (section < kEmojiSectionCount
				&& index < _emoji[section].size()
				&& _emoji[section][index]->hasVariants()) {
				const auto &variants = Core::App().settings().emojiVariants();
				if (variants.contains(_emoji[section][index]->nonColoredId())) {
					_picker->hideAnimated();
					_pickerSelected = v::null;
				}
			}
		}
	}
	updateSelected();

	if (_showPickerTimer.isActive()) {
		_showPickerTimer.cancel();
		_pickerSelected = v::null;
		_picker->hide();
	}

	if (v::is_null(_selected) || _selected != pressed) {
		return;
	}

	if (const auto over = std::get_if<OverEmoji>(&_selected)) {
		const auto section = over->section;
		const auto index = over->index;
		if (section < kEmojiSectionCount && index < _emoji[section].size()) {
			const auto emoji = _emoji[section][index];
			if (emoji->hasVariants() && !_picker->isHidden()) {
				return;
			}
			selectEmoji(emoji);
		} else if (section >= kEmojiSectionCount
			&& index < _custom[section - kEmojiSectionCount].list.size()) {
			auto &set = _custom[section - kEmojiSectionCount];
			selectCustom(set.list[index].document);
		}
	} else if (const auto set = std::get_if<OverSet>(&pressed)) {
		Assert(set->section >= kEmojiSectionCount
			&& set->section < kEmojiSectionCount + _custom.size());
		displaySet(_custom[set->section - kEmojiSectionCount].id);
	} else if (auto button = std::get_if<OverButton>(&pressed)) {
		Assert(button->section >= kEmojiSectionCount
			&& button->section < kEmojiSectionCount + _custom.size());
		removeSet(_custom[button->section - kEmojiSectionCount].id);
	}
}

void EmojiListWidget::displaySet(uint64 setId) {
	const auto &sets = session().data().stickers().sets();
	auto it = sets.find(setId);
	if (it != sets.cend()) {
		checkHideWithBox(controller()->show(
			Box<StickerSetBox>(controller(), it->second->identifier()),
			Ui::LayerOption::KeepOther).data());
	}
}

void EmojiListWidget::removeSet(uint64 setId) {
	if (auto box = MakeConfirmRemoveSetBox(&session(), setId)) {
		checkHideWithBox(controller()->show(
			std::move(box),
			Ui::LayerOption::KeepOther));
	}
}

void EmojiListWidget::selectEmoji(EmojiPtr emoji) {
	Core::App().settings().incrementRecentEmoji(emoji);
	_chosen.fire_copy(emoji);
}

void EmojiListWidget::selectCustom(not_null<DocumentData*> document) {
	_customChosen.fire({ .document = document });
}

void EmojiListWidget::showPicker() {
	if (v::is_null(_pickerSelected)) {
		return;
	}

	const auto over = std::get_if<OverEmoji>(&_pickerSelected);
	const auto section = over ? over->section : -1;
	if (section >= 0
		&& section < kEmojiSectionCount
		&& over->index < _emoji[section].size()
		&& _emoji[section][over->index]->hasVariants()) {
		_picker->showEmoji(_emoji[section][over->index]);

		auto y = emojiRect(section, over->index).y();
		y -= _picker->height() - st::roundRadiusSmall + getVisibleTop();
		if (y < st::emojiPanHeader) {
			y += _picker->height() - st::roundRadiusSmall + _singleSize.height() - st::roundRadiusSmall;
		}
		auto xmax = width() - _picker->width();
		auto coef = float64(over->index % _columnCount) / float64(_columnCount - 1);
		if (rtl()) coef = 1. - coef;
		_picker->move(qRound(xmax * coef), y);

		disableScroll(true);
	}
}

void EmojiListWidget::pickerHidden() {
	_pickerSelected = v::null;
	update();
	disableScroll(false);

	_lastMousePos = QCursor::pos();
	updateSelected();
}

bool EmojiListWidget::hasRemoveButton(int index) const {
	if (index < kEmojiSectionCount
		|| index >= kEmojiSectionCount + _custom.size()) {
		return false;
	}
	return true;
}

QRect EmojiListWidget::removeButtonRect(int index) const {
	auto buttonw = st::stickerPanRemoveSet.width;
	auto buttonh = st::stickerPanRemoveSet.height;
	auto buttonx = emojiRight() - buttonw;
	auto buttony = sectionInfo(index).top + (st::emojiPanHeader - buttonh) / 2;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

int EmojiListWidget::emojiRight() const {
	return emojiLeft() + (_columnCount * _singleSize.width());
}

int EmojiListWidget::emojiLeft() const {
	return _rowsLeft;
}

QRect EmojiListWidget::emojiRect(int section, int index) const {
	Expects(_columnCount > 0);

	const auto info = sectionInfo(section);
	const auto countTillItem = (index - (index % _columnCount));
	const auto rowsToSkip = (countTillItem / _columnCount)
		+ ((countTillItem % _columnCount) ? 1 : 0);
	const auto x = _rowsLeft + ((index % _columnCount) * _singleSize.width());
	const auto y = info.rowsTop + rowsToSkip * _singleSize.height();
	return QRect(x, y, _singleSize.width(), _singleSize.height());
}

void EmojiListWidget::colorChosen(EmojiPtr emoji) {
	if (emoji->hasVariants()) {
		Core::App().settings().saveEmojiVariant(emoji);
	}
	const auto over = std::get_if<OverEmoji>(&_pickerSelected);
	if (over && over->section >= 0 && over->section < kEmojiSectionCount) {
		_emoji[over->section][over->index] = emoji;
		rtlupdate(emojiRect(over->section, over->index));
	}
	selectEmoji(emoji);
	_picker->hideAnimated();
}

void EmojiListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	if (!_picker->isHidden()) {
		if (_picker->rect().contains(_picker->mapFromGlobal(_lastMousePos))) {
			return _picker->handleMouseMove(QCursor::pos());
		} else {
			_picker->clearSelection();
		}
	}
	updateSelected();
}

void EmojiListWidget::leaveEventHook(QEvent *e) {
	clearSelection();
}

void EmojiListWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void EmojiListWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void EmojiListWidget::clearSelection() {
	setPressed(v::null);
	setSelected(v::null);
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
}

uint64 EmojiListWidget::currentSet(int yOffset) const {
	return sectionSetId(sectionInfoByOffset(yOffset).section);
}

QString EmojiListWidget::tooltipText() const {
	const auto &replacements = Ui::Emoji::internal::GetAllReplacements();
	const auto over = std::get_if<OverEmoji>(&_selected);
	const auto section = over ? over->section : -1;
	const auto index = over ? over->index : -1;
	if (section >= 0
		&& section < kEmojiSectionCount
		&& index < _emoji[section].size()) {
		const auto emoji = _emoji[section][index]->original();
		const auto text = emoji->text();
		// find the replacement belonging to the emoji
		const auto it = ranges::find_if(replacements, [&](const auto &one) {
			return text == Ui::Emoji::QStringFromUTF16(one.emoji);
		});
		if (it != replacements.end()) {
			return Ui::Emoji::QStringFromUTF16(it->replacement);
		}
	}
	return {};
}

QPoint EmojiListWidget::tooltipPos() const {
	return _lastMousePos;
}

bool EmojiListWidget::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

TabbedSelector::InnerFooter *EmojiListWidget::getFooter() const {
	return _footer;
}

void EmojiListWidget::processHideFinished() {
	if (!_picker->isHidden()) {
		_picker->hideFast();
		_pickerSelected = v::null;
	}
	clearSelection();
}

void EmojiListWidget::refreshRecent() {
	clearSelection();
	_emoji[0] = Core::App().settings().recentEmojiSection();
	_counts[0] = _emoji[0].size();
	resizeToWidth(width());
}

void EmojiListWidget::refreshCustom() {
	auto searchFromIndex = 0;
	auto old = base::take(_custom);
	const auto owner = &controller()->session().data();
	const auto &order = owner->stickers().emojiSetsOrder();
	const auto &sets = owner->stickers().sets();
	for (const auto setId : order) {
		auto it = sets.find(setId);
		if (it == sets.cend() || it->second->stickers.isEmpty()) {
			continue;
		}
		const auto &list = it->second->stickers;
		const auto i = ranges::find(old, setId, &CustomSet::id);
		if (i != end(old)) {
			const auto valid = [&] {
				const auto count = int(list.size());
				if (i->list.size() != count) {
					return false;
				}
				for (auto k = 0; k != count; ++k) {
					if (i->list[k].document != list[k]) {
						return false;
					}
				}
				return true;
			}();
			if (valid) {
				i->thumbnailDocument = it->second->lookupThumbnailDocument();
				_custom.push_back(std::move(*i));
				continue;
			}
		}
		auto set = std::vector<CustomOne>();
		set.reserve(list.size());
		for (const auto document : list) {
			if (document->sticker()) {
				auto i = _instances.find(document->id);
				if (i == end(_instances)) {
					auto loader = owner->customEmojiManager().createLoader(
						document,
						Data::CustomEmojiManager::SizeTag::Large);
					const auto repaintDelayed = [=](
							not_null<Ui::CustomEmoji::Instance*> instance,
							Ui::CustomEmoji::RepaintRequest request) {
						repaintLater(setId, request);
					};
					const auto repaintNow = [=] {
						repaintCustom([&](uint64 id) {
							return id == setId;
						});
					};
					i = _instances.emplace(
						document->id,
						std::make_unique<CustomInstance>(
							std::move(loader),
							std::move(repaintDelayed),
							std::move(repaintNow))).first;
				}
				set.push_back({
					.instance = i->second.get(),
					.document = document,
				});
			}
		}
		_custom.push_back({
			.id = setId,
			.set = it->second.get(),
			.thumbnailDocument = it->second->lookupThumbnailDocument(),
			.title = it->second->title,
			.list = std::move(set),
		});
	}
	_footer->refreshIcons(
		fillIcons(),
		nullptr,
		ValidateIconAnimations::None);
}

std::vector<StickerIcon> EmojiListWidget::fillIcons() {
	auto result = std::vector<StickerIcon>();
	result.reserve(2 + _custom.size());

	result.emplace_back(RecentEmojiSectionSetId());
	if (_custom.empty()) {
		using Section = Ui::Emoji::Section;
		for (auto i = int(Section::People); i <= int(Section::Symbols); ++i) {
			result.emplace_back(EmojiSectionSetId(Section(i)));
		}
	} else {
		result.emplace_back(AllEmojiSectionSetId());
	}
	for (const auto &custom : _custom) {
		const auto set = custom.set;
		const auto s = custom.thumbnailDocument;
		const auto availw = st::stickerIconWidth - 2 * st::stickerIconPadding;
		const auto availh = st::emojiFooterHeight - 2 * st::stickerIconPadding;
		const auto size = set->hasThumbnail()
			? QSize(
				set->thumbnailLocation().width(),
				set->thumbnailLocation().height())
			: s->hasThumbnail()
			? QSize(
				s->thumbnailLocation().width(),
				s->thumbnailLocation().height())
			: QSize();
		auto thumbw = size.width(), thumbh = size.height(), pixw = 1, pixh = 1;
		if (availw * thumbh > availh * thumbw) {
			pixh = availh;
			pixw = (pixh * thumbw) / thumbh;
		} else {
			pixw = availw;
			pixh = thumbw ? ((pixw * thumbh) / thumbw) : 1;
		}
		if (pixw < 1) pixw = 1;
		if (pixh < 1) pixh = 1;
		result.emplace_back(set, s, pixw, pixh);
	}
	return result;
}

bool EmojiListWidget::eventHook(QEvent *e) {
	if (e->type() == QEvent::ParentChange) {
		if (_picker->parentWidget() != parentWidget()) {
			_picker->setParent(parentWidget());
		}
		_picker->raise();
	}
	return Inner::eventHook(e);
}

void EmojiListWidget::updateSelected() {
	if (!v::is_null(_pressed) || !v::is_null(_pickerSelected)) {
		return;
	}

	auto newSelected = OverState{ v::null };
	auto p = mapFromGlobal(_lastMousePos);
	auto info = sectionInfoByOffset(p.y());
	auto section = info.section;
	if (p.y() >= info.top && p.y() < info.rowsTop) {
		if (hasRemoveButton(section) && myrtlrect(removeButtonRect(section)).contains(p.x(), p.y())) {
			newSelected = OverButton{ section };
		} else {
			newSelected = OverSet{ section };
		}
	} else if (p.y() >= info.rowsTop && p.y() < info.rowsBottom) {
		auto sx = (rtl() ? width() - p.x() : p.x()) - _rowsLeft;
		if (sx >= 0 && sx < _columnCount * _singleSize.width()) {
			const auto index = qFloor((p.y() - info.rowsTop) / _singleSize.height()) * _columnCount + qFloor(sx / _singleSize.width());
			if (index < info.count) {
				newSelected = OverEmoji{ .section = section, .index = index };
			}
		}
	}
	setSelected(newSelected);
}

void EmojiListWidget::setSelected(OverState newSelected) {
	if (_selected == newSelected) {
		return;
	}
	setCursor(!v::is_null(newSelected)
		? style::cur_pointer
		: style::cur_default);

	const auto updateSelected = [&] {
		if (const auto sticker = std::get_if<OverEmoji>(&_selected)) {
			rtlupdate(emojiRect(sticker->section, sticker->index));
		} else if (const auto button = std::get_if<OverButton>(&_selected)) {
			rtlupdate(removeButtonRect(button->section));
		}
	};
	updateSelected();
	_selected = newSelected;
	updateSelected();

	const auto hasSelection = !v::is_null(_selected);
	if (hasSelection && Core::App().settings().suggestEmoji()) {
		Ui::Tooltip::Show(1000, this);
	}

	setCursor(hasSelection ? style::cur_pointer : style::cur_default);
	if (hasSelection && !_picker->isHidden()) {
		if (_selected != _pickerSelected) {
			_picker->hideAnimated();
		} else {
			_picker->showAnimated();
		}
	}
}

void EmojiListWidget::setPressed(OverState newPressed) {
	if (auto button = std::get_if<OverButton>(&_pressed)) {
		Assert(button->section >= kEmojiSectionCount
			&& button->section < kEmojiSectionCount + _custom.size());
		auto &set = _custom[button->section - kEmojiSectionCount];
		if (set.ripple) {
			set.ripple->lastStop();
		}
	}
	_pressed = newPressed;
	if (auto button = std::get_if<OverButton>(&_pressed)) {
		Assert(button->section >= kEmojiSectionCount
			&& button->section < kEmojiSectionCount + _custom.size());
		auto &set = _custom[button->section - kEmojiSectionCount];
		if (!set.ripple) {
			set.ripple = createButtonRipple(button->section);
		}
		set.ripple->add(mapFromGlobal(QCursor::pos()) - buttonRippleTopLeft(button->section));
	}
}

std::unique_ptr<Ui::RippleAnimation> EmojiListWidget::createButtonRipple(
		int section) {
	Expects(section >= kEmojiSectionCount
		&& section < kEmojiSectionCount + _custom.size());

	const auto set = section - kEmojiSectionCount;
	auto maskSize = QSize(
		st::stickerPanRemoveSet.rippleAreaSize,
		st::stickerPanRemoveSet.rippleAreaSize);
	auto mask = Ui::RippleAnimation::ellipseMask(maskSize);
	return std::make_unique<Ui::RippleAnimation>(
		st::stickerPanRemoveSet.ripple,
		std::move(mask),
		[this, section] { rtlupdate(removeButtonRect(section)); });
}

QPoint EmojiListWidget::buttonRippleTopLeft(int section) const {
	Expects(section >= kEmojiSectionCount
		&& section < kEmojiSectionCount + _custom.size());

	return myrtlrect(removeButtonRect(section)).topLeft()
		+ st::stickerPanRemoveSet.rippleAreaPosition;
}

void EmojiListWidget::showEmojiSection(Section section) {
	showSet(EmojiSectionSetId(section));
}

void EmojiListWidget::refreshEmoji() {
	refreshRecent();
	refreshCustom();
}

void EmojiListWidget::showSet(uint64 setId) {
	clearSelection();

	refreshEmoji();

	auto y = 0;
	enumerateSections([&](const SectionInfo &info) {
		if (setId == sectionSetId(info.section)) {
			y = info.top;
			return false;
		}
		return true;
	});
	scrollTo(y);

	_lastMousePos = QCursor::pos();

	update();
}

uint64 EmojiListWidget::sectionSetId(int section) const {
	Expects(section < kEmojiSectionCount
		|| (section - kEmojiSectionCount) < _custom.size());

	return (section < kEmojiSectionCount)
		? EmojiSectionSetId(static_cast<Section>(section))
		: _custom[section - kEmojiSectionCount].id;

}

tr::phrase<> EmojiCategoryTitle(int index) {
	switch (index) {
	case 1: return tr::lng_emoji_category1;
	case 2: return tr::lng_emoji_category2;
	case 3: return tr::lng_emoji_category3;
	case 4: return tr::lng_emoji_category4;
	case 5: return tr::lng_emoji_category5;
	case 6: return tr::lng_emoji_category6;
	case 7: return tr::lng_emoji_category7;
	}
	Unexpected("Index in CategoryTitle.");
}

} // namespace ChatHelpers
