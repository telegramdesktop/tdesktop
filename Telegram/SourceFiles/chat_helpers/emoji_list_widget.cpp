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
#include "ui/effects/premium_graphics.h"
#include "ui/emoji_config.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "boxes/sticker_set_box.h"
#include "lang/lang_keys.h"
#include "layout/layout_position.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_peer_values.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "chat_helpers/stickers_list_widget.h"
#include "chat_helpers/stickers_list_footer.h"
#include "emoji_suggestions_data.h"
#include "emoji_suggestions_helper.h"
#include "main/main_session.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "settings/settings_premium.h"
#include "window/window_session_controller.h"
#include "facades.h"
#include "styles/style_chat_helpers.h"

namespace ChatHelpers {
namespace {

constexpr auto kCollapsedRows = 3;

using Core::RecentEmojiId;
using Core::RecentEmojiDocument;

} // namespace

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
	QPoint _areaPosition;
	QPoint _innerPosition;

	bool _hiding = false;
	QPixmap _cache;
	Ui::Animations::Simple _a_opacity;

	rpl::event_stream<EmojiPtr> _chosen;
	rpl::event_stream<> _hidden;

};

struct EmojiListWidget::CustomEmojiInstance {
	std::unique_ptr<Ui::Text::CustomEmoji> emoji;
	bool recentOnly = false;
};

struct EmojiListWidget::RecentOne {
	Ui::Text::CustomEmoji *custom = nullptr;
	RecentEmojiId id;
};

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
	const auto area = st::emojiPanArea;
	_singleSize = size;
	_areaPosition = QPoint(
		(_singleSize.width() - area.width()) / 2,
		(_singleSize.height() - area.height()) / 2);
	const auto esize = Ui::Emoji::GetSizeLarge() / style::DevicePixelRatio();
	_innerPosition = QPoint(
		(area.width() - esize) / 2,
		(area.height() - esize) / 2);
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
	const auto w = QPoint(
		st::emojiPanMargins.left(),
		st::emojiPanMargins.top()
	) + QPoint(
		(st::emojiColorsPadding
			+ variant * _singleSize.width()
			+ (variant
				? (2 * st::emojiColorsPadding + st::emojiColorsSep)
				: 0)),
		st::emojiColorsPadding
	) + _areaPosition;
	if (variant == _selected) {
		QPoint tl(w);
		if (rtl()) tl.setX(width() - tl.x() - st::emojiPanArea.width());

		Ui::FillRoundRect(
			p,
			QRect(tl, st::emojiPanArea),
			st::emojiPanHover,
			Ui::EmojiHoverCorners);
	}
	Ui::Emoji::Draw(
		p,
		_variants[variant],
		Ui::Emoji::GetSizeLarge(),
		w.x() + _innerPosition.x(),
		w.y() + _innerPosition.y());
}

EmojiListWidget::EmojiListWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Window::GifPauseReason level)
: Inner(parent, controller, level)
, _localSetsManager(
	std::make_unique<LocalStickersManager>(&controller->session()))
, _collapsedBg(st::emojiPanExpand.height / 2, st::emojiPanHeaderFg)
, _picker(this)
, _showPickerTimer([=] { showPicker(); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_picker->hide();

	for (auto i = 1; i != kEmojiSectionCount; ++i) {
		const auto section = static_cast<Section>(i);
		_counts[i] = Ui::Emoji::GetSectionCount(section);
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
		Data::StickersType::Emoji
	) | rpl::start_with_next([=] {
		refreshCustom();
		resizeToWidth(width());
	}, lifetime());

	rpl::combine(
		Data::AmPremiumValue(&controller->session()),
		controller->session().premiumPossibleValue()
	) | rpl::skip(1) | rpl::start_with_next([=] {
		refreshCustom();
		resizeToWidth(width());
	}, lifetime());

	rpl::single(
		rpl::empty
	) | rpl::then(
		style::PaletteChanged()
	) | rpl::start_with_next([=] {
		initButton(_add, tr::lng_stickers_featured_add(tr::now), false);
		initButton(_unlock, tr::lng_emoji_featured_unlock(tr::now), true);
		initButton(_restore, tr::lng_emoji_premium_restore(tr::now), true);
	}, lifetime());
}

EmojiListWidget::~EmojiListWidget() {
	base::take(_customEmoji);
}

void EmojiListWidget::repaintCustom(uint64 setId) {
	if (!_repaintsScheduled.emplace(setId).second) {
		return;
	}
	const auto repaintRecent = (setId == RecentEmojiSectionSetId());
	enumerateSections([&](const SectionInfo &info) {
		const auto repaint1 = repaintRecent
			&& (info.section == int(Section::Recent));
		const auto repaint2 = !repaint1
			&& (info.section >= kEmojiSectionCount)
			&& (setId == _custom[info.section - kEmojiSectionCount].id);
		if (repaint1 || repaint2) {
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

auto EmojiListWidget::premiumChosen() const
-> rpl::producer<not_null<DocumentData*>> {
	return _premiumChosen.events();
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
		if (info.rowsBottom <= visibleTop || info.rowsTop >= visibleBottom) {
			unloadCustomIn(info);
		}
		return true;
	});
}

void EmojiListWidget::unloadAllCustom() {
	enumerateSections([&](const SectionInfo &info) {
		unloadCustomIn(info);
		return true;
	});
}

void EmojiListWidget::unloadCustomIn(const SectionInfo &info) {
	if (!info.section && _recentPainted) {
		_recentPainted = false;
		for (const auto &single : _recent) {
			if (const auto custom = single.custom) {
				custom->unload();
			}
		}
		return;
	} else if (info.section < kEmojiSectionCount) {
		return;
	}
	auto &custom = _custom[info.section - kEmojiSectionCount];
	if (!custom.painted) {
		return;
	}
	custom.painted = false;
	for (const auto &single : custom.list) {
		single.custom->unload();
	}
}

object_ptr<TabbedSelector::InnerFooter> EmojiListWidget::createFooter() {
	Expects(_footer == nullptr);

	using FooterDescriptor = StickersListFooter::Descriptor;
	auto result = object_ptr<StickersListFooter>(FooterDescriptor{
		.controller = controller(),
		.level = level(),
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
		info.rowsCount = info.collapsed
			? kCollapsedRows
			: (info.count + _columnCount - 1) / _columnCount;
		info.rowsTop = info.top
			+ (i == 0 ? st::emojiPanPadding : st::emojiPanHeader);
		info.rowsBottom = info.rowsTop
			+ (info.rowsCount * _singleSize.height())
			+ st::roundRadiusSmall;
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
		return true;
	};
	for (; i != kEmojiSectionCount; ++i) {
		info.section = i;
		info.count = i ? _counts[i] : _recent.size();
		if (!next()) {
			return false;
		}
	}
	for (auto &section : _custom) {
		info.section = i++;
		info.premiumRequired = section.premiumRequired;
		info.count = int(section.list.size());
		info.collapsed = !section.expanded
			&& (!section.canRemove || section.premiumRequired)
			&& (info.count > _columnCount * kCollapsedRows);
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

void EmojiListWidget::setSingleSize(QSize size) {
	const auto area = st::emojiPanArea;
	_singleSize = size;
	_areaPosition = QPoint(
		(_singleSize.width() - area.width()) / 2,
		(_singleSize.height() - area.height()) / 2);
	const auto esize = Ui::Emoji::GetSizeLarge() / style::DevicePixelRatio();
	_innerPosition = QPoint(
		(area.width() - esize) / 2,
		(area.height() - esize) / 2);
	_picker->setSingleSize(_singleSize);
}

int EmojiListWidget::countDesiredHeight(int newWidth) {
	const auto left = st::emojiPanLeft;
	const auto right = st::emojiPanRight;
	const auto fullWidth = st::roundRadiusSmall
		+ newWidth
		+ st::emojiScroll.width;
	const auto innerWidth = fullWidth - left - right;
	_columnCount = std::max(innerWidth / st::emojiPanDesiredSize, 1);
	const auto singleWidth = innerWidth / _columnCount;
	_rowsLeft = left
		+ (innerWidth - _columnCount * singleWidth) / 2
		- st::roundRadiusSmall;
	setSingleSize({ singleWidth, singleWidth - 4 * st::lineWidth });

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

	if (section == int(Section::Recent)) {
		if (_recent.empty()) {
			fillRecent();
		}
		return;
	} else if (section >= kEmojiSectionCount || !_emoji[section].empty()) {
		return;
	}
	_emoji[section] = Ui::Emoji::GetSection(static_cast<Section>(section));
	_counts[section] = _emoji[section].size();

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

void EmojiListWidget::fillRecent() {
	_recent.clear();
	_recentCustomIds.clear();

	const auto &list = Core::App().settings().recentEmoji();
	_recent.reserve(std::min(int(list.size()), Core::kRecentEmojiLimit));
	const auto test = controller()->session().isTestMode();
	for (const auto &one : list) {
		const auto document = std::get_if<RecentEmojiDocument>(&one.id.data);
		if (document && document->test != test) {
			continue;
		}
		_recent.push_back({
			.custom = resolveCustomEmoji(one.id),
			.id = one.id,
		});
		if (document) {
			_recentCustomIds.emplace(document->id);
		}
		if (_recent.size() >= Core::kRecentEmojiLimit) {
			break;
		}
	}
}

void EmojiListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	_repaintsScheduled.clear();

	const auto r = e ? e->rect() : rect();
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

	const auto paused = controller()->isGifPausedAtLeastFor(level());
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
		const auto buttonSelected = selectedButton
			? (selectedButton->section == info.section)
			: false;
		const auto widthForTitle = emojiRight()
			- (st::emojiPanHeaderLeft - st::roundRadiusSmall)
			- paintButtonGetWidth(p, info, buttonSelected, r);
		if (info.section > 0 && r.top() < info.rowsTop) {
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			auto titleText = (info.section < kEmojiSectionCount)
				? ChatHelpers::EmojiCategoryTitle(info.section)(tr::now)
				: _custom[info.section - kEmojiSectionCount].title;
			auto titleWidth = st::emojiPanHeaderFont->width(titleText);
			if (titleWidth > widthForTitle) {
				titleText = st::emojiPanHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::emojiPanHeaderFont->width(titleText);
			}
			const auto left = (info.premiumRequired
				? st::emojiPanHeaderLockedLeft
				: st::emojiPanHeaderLeft) - st::roundRadiusSmall;
			const auto top = info.top + st::emojiPanHeaderTop;
			if (info.premiumRequired) {
				st::emojiPremiumRequired.paint(
					p,
					st::emojiPanHeaderLockLeft - st::roundRadiusSmall,
					top,
					width());
			}
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			p.drawTextLeft(left, top, width(), titleText, titleWidth);
		}
		if (r.top() + r.height() > info.rowsTop) {
			ensureLoaded(info.section);
			auto fromRow = floorclamp(r.y() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
			auto toRow = ceilclamp(r.y() + r.height() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
			for (auto i = fromRow; i < toRow; ++i) {
				for (auto j = fromColumn; j < toColumn; ++j) {
					const auto index = i * _columnCount + j;
					if (index >= info.count) {
						break;
					}

					const auto state = OverEmoji{
						.section = info.section,
						.index = index,
					};
					const auto selected = (state == _selected)
						|| (!_picker->isHidden()
							&& state == _pickerSelected);
					auto w = QPoint(
						_rowsLeft + j * _singleSize.width(),
						info.rowsTop + i * _singleSize.height()
					) + _areaPosition;
					if (info.collapsed
						&& index + 1 == _columnCount * kCollapsedRows) {
						drawCollapsedBadge(p, w - _areaPosition, info.count);
						continue;
					}
					if (selected) {
						auto tl = w;
						if (rtl()) {
							tl.setX(width() - tl.x() - st::emojiPanArea.width());
						}
						Ui::FillRoundRect(
							p,
							QRect(tl, st::emojiPanArea),
							st::emojiPanHover,
							Ui::EmojiHoverCorners);
					}
					if (info.section == int(Section::Recent)) {
						drawRecent(p, w, now, paused, index);
					} else if (info.section < kEmojiSectionCount) {
						drawEmoji(p, w, _emoji[info.section][index]);
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

void EmojiListWidget::drawCollapsedBadge(
		QPainter &p,
		QPoint position,
		int count) {
	const auto &st = st::emojiPanExpand;
	const auto text = u"+%1"_q.arg(count - _columnCount * kCollapsedRows + 1);
	const auto textWidth = st.font->width(text);
	const auto buttonw = std::max(textWidth - st.width, st.height);
	const auto buttonh = st.height;
	const auto buttonx = position.x() + (_singleSize.width() - buttonw) / 2;
	const auto buttony = position.y() + (_singleSize.height() - buttonh) / 2;
	_collapsedBg.paint(p, QRect(buttonx, buttony, buttonw, buttonh));
	p.setPen(st::emojiPanBg);
	p.setFont(st.font);
	p.drawText(
		buttonx + (buttonw - textWidth) / 2,
		(buttony + st.textTop + st.font->ascent),
		text);
}

void EmojiListWidget::drawRecent(
		QPainter &p,
		QPoint position,
		crl::time now,
		bool paused,
		int index) {
	_recentPainted = true;
	if (const auto emoji = std::get_if<EmojiPtr>(&_recent[index].id.data)) {
		drawEmoji(p, position, *emoji);
	} else {
		Assert(_recent[index].custom != nullptr);
		position += _innerPosition;
		_recent[index].custom->paint(
			p,
			position.x(),
			position.y(),
			now,
			st::windowBgRipple->c,
			paused);
	}
}

void EmojiListWidget::drawEmoji(
		QPainter &p,
		QPoint position,
		EmojiPtr emoji) {
	position += _innerPosition;
	Ui::Emoji::Draw(
		p,
		emoji,
		Ui::Emoji::GetSizeLarge(),
		position.x(),
		position.y());
}

void EmojiListWidget::drawCustom(
		QPainter &p,
		QPoint position,
		crl::time now,
		bool paused,
		int set,
		int index) {
	position += _innerPosition;
	_custom[set].painted = true;
	_custom[set].list[index].custom->paint(
		p,
		position.x(),
		position.y(),
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

EmojiPtr EmojiListWidget::lookupOverEmoji(const OverEmoji *over) const {
	const auto section = over ? over->section : -1;
	const auto index = over ? over->index : -1;
	return (section == int(Section::Recent)
		&& index < _recent.size()
		&& v::is<EmojiPtr>(_recent[index].id.data))
		? v::get<EmojiPtr>(_recent[index].id.data)
		: (section > int(Section::Recent)
			&& section < kEmojiSectionCount
			&& index < _emoji[section].size())
		? _emoji[section][index]
		: nullptr;
}

void EmojiListWidget::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (checkPickerHide() || e->button() != Qt::LeftButton) {
		return;
	}
	setPressed(_selected);
	if (const auto over = std::get_if<OverEmoji>(&_selected)) {
		const auto emoji = lookupOverEmoji(over);
		if (emoji && emoji->hasVariants()) {
			_pickerSelected = _selected;
			setCursor(style::cur_default);
			const auto &variants = Core::App().settings().emojiVariants();
			if (!variants.contains(emoji->nonColoredId())) {
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
			const auto emoji = lookupOverEmoji(over);
			if (emoji && emoji->hasVariants()) {
				const auto &variants = Core::App().settings().emojiVariants();
				if (variants.contains(emoji->nonColoredId())) {
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
		if (section >= kEmojiSectionCount
			&& sectionInfo(section).collapsed
			&& index + 1 == _columnCount * kCollapsedRows) {
			_custom[section - kEmojiSectionCount].expanded = true;
			resizeToWidth(width());
			update();
			return;
		} else if (const auto emoji = lookupOverEmoji(over)) {
			if (emoji->hasVariants() && !_picker->isHidden()) {
				return;
			}
			selectEmoji(emoji);
		} else if (section == int(Section::Recent)
			&& index < _recent.size()) {
			const auto document = std::get_if<RecentEmojiDocument>(
				&_recent[index].id.data);
			const auto custom = document
				? session().data().document(document->id).get()
				: nullptr;
			if (custom && custom->sticker()) {
				selectCustom(custom);
			}
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
		const auto id = _custom[button->section - kEmojiSectionCount].id;
		if (hasRemoveButton(button->section)) {
			removeSet(id);
		} else if (hasAddButton(button->section)) {
			_localSetsManager->install(id);
		} else {
			Settings::ShowPremium(controller(), u"animated_emoji"_q);
		}
	}
}

void EmojiListWidget::displaySet(uint64 setId) {
	const auto &sets = session().data().stickers().sets();
	auto it = sets.find(setId);
	if (it != sets.cend()) {
		checkHideWithBox(controller()->show(
			Box<StickerSetBox>(controller(), it->second.get()),
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
	Core::App().settings().incrementRecentEmoji({ emoji });
	_chosen.fire_copy(emoji);
}

void EmojiListWidget::selectCustom(not_null<DocumentData*> document) {
	if (document->isPremiumEmoji()
		&& !document->session().premium()
		&& !_allowWithoutPremium) {
		_premiumChosen.fire_copy(document);
		return;
	}
	Core::App().settings().incrementRecentEmoji({ RecentEmojiDocument{
		document->id,
		document->session().isTestMode(),
	} });
	_customChosen.fire({ .document = document });
}

void EmojiListWidget::showPicker() {
	if (v::is_null(_pickerSelected)) {
		return;
	}

	const auto over = std::get_if<OverEmoji>(&_pickerSelected);
	const auto emoji = lookupOverEmoji(over);
	if (emoji && emoji->hasVariants()) {
		_picker->showEmoji(emoji);

		auto y = emojiRect(over->section, over->index).y();
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
	const auto &set = _custom[index - kEmojiSectionCount];
	return set.canRemove && !set.premiumRequired;
}

QRect EmojiListWidget::removeButtonRect(int index) const {
	return removeButtonRect(sectionInfo(index));
}

QRect EmojiListWidget::removeButtonRect(const SectionInfo &info) const {
	const auto buttonw = st::stickerPanRemoveSet.rippleAreaPosition.x()
		+ st::stickerPanRemoveSet.rippleAreaSize;
	const auto buttonh = st::stickerPanRemoveSet.height;
	const auto buttonx = emojiRight() - st::emojiPanRemoveSkip - buttonw;
	const auto buttony = info.top + (st::emojiPanHeader - buttonh) / 2;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

bool EmojiListWidget::hasAddButton(int index) const {
	if (index < kEmojiSectionCount
		|| index >= kEmojiSectionCount + _custom.size()) {
		return false;
	}
	const auto &set = _custom[index - kEmojiSectionCount];
	return !set.canRemove && !set.premiumRequired;
}

QRect EmojiListWidget::addButtonRect(int index) const {
	return buttonRect(sectionInfo(index), _add);
}

bool EmojiListWidget::hasUnlockButton(int index) const {
	if (index < kEmojiSectionCount
		|| index >= kEmojiSectionCount + _custom.size()) {
		return false;
	}
	const auto &set = _custom[index - kEmojiSectionCount];
	return set.premiumRequired;
}

QRect EmojiListWidget::unlockButtonRect(int index) const {
	Expects(index >= kEmojiSectionCount
		&& index < kEmojiSectionCount + _custom.size());

	return buttonRect(sectionInfo(index), rightButton(index));
}

bool EmojiListWidget::hasButton(int index) const {
	if (index < kEmojiSectionCount
		|| index >= kEmojiSectionCount + _custom.size()) {
		return false;
	}
	return true;
}

QRect EmojiListWidget::buttonRect(int index) const {
	return hasRemoveButton(index)
		? removeButtonRect(index)
		: hasAddButton(index)
		? addButtonRect(index)
		: unlockButtonRect(index);
}

QRect EmojiListWidget::buttonRect(
		const SectionInfo &info,
		const RightButton &button) const {
	const auto buttonw = button.textWidth - st::emojiPanButton.width;
	const auto buttonh = st::emojiPanButton.height;
	const auto buttonx = emojiRight() - buttonw - st::emojiPanButtonRight;
	const auto buttony = info.top + st::emojiPanButtonTop;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

auto EmojiListWidget::rightButton(int index) const -> const RightButton & {
	Expects(index >= kEmojiSectionCount
		&& index < kEmojiSectionCount + _custom.size());
	return hasAddButton(index)
		? _add
		: _custom[index - kEmojiSectionCount].canRemove
		? _restore
		: _unlock;
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
	if (over
		&& over->section > int(Section::Recent)
		&& over->section < kEmojiSectionCount
		&& over->index < _emoji[over->section].size()) {
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

void EmojiListWidget::setAllowWithoutPremium(bool allow) {
	if (_allowWithoutPremium == allow) {
		return;
	}
	_allowWithoutPremium = allow;
	refreshCustom();
	resizeToWidth(width());
}

QString EmojiListWidget::tooltipText() const {
	const auto &replacements = Ui::Emoji::internal::GetAllReplacements();
	const auto over = std::get_if<OverEmoji>(&_selected);
	if (const auto emoji = lookupOverEmoji(over)) {
		const auto text = emoji->original()->text();
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
	unloadAllCustom();
	clearSelection();
}

void EmojiListWidget::processPanelHideFinished() {
	unloadAllCustom();
	if (_localSetsManager->clearInstalledLocally()) {
		refreshCustom();
	}
}

void EmojiListWidget::refreshRecent() {
	clearSelection();
	fillRecent();
	resizeToWidth(width());
}

void EmojiListWidget::refreshCustom() {
	auto old = base::take(_custom);
	const auto session = &controller()->session();
	const auto premiumPossible = session->premiumPossible();
	const auto premiumMayBeBought = premiumPossible
		&& !session->premium()
		&& !_allowWithoutPremium;
	const auto owner = &session->data();
	const auto &sets = owner->stickers().sets();
	const auto push = [&](uint64 setId, bool installed) {
		auto it = sets.find(setId);
		if (it == sets.cend() || it->second->stickers.isEmpty()) {
			return;
		}
		const auto canRemove = !!(it->second->flags
			& Data::StickersSetFlag::Installed);
		const auto sortAsInstalled = canRemove
			&& (!(it->second->flags & Data::StickersSetFlag::Featured)
				|| !_localSetsManager->isInstalledLocally(setId));
		if (sortAsInstalled != installed) {
			return;
		}
		auto premium = false;
		const auto &list = it->second->stickers;
		const auto i = ranges::find(old, setId, &CustomSet::id);
		if (i != end(old)) {
			const auto valid = [&] {
				const auto count = int(list.size());
				if (i->list.size() != count) {
					return false;
				}
				for (auto k = 0; k != count; ++k) {
					if (!premium && list[k]->isPremiumEmoji()) {
						premium = true;
					}
					if (i->list[k].document != list[k]) {
						return false;
					}
				}
				return true;
			}();
			if (premium && !premiumPossible) {
				return;
			} else if (valid) {
				i->thumbnailDocument = it->second->lookupThumbnailDocument();
				const auto premiumRequired = premium && premiumMayBeBought;
				if (i->canRemove != canRemove
					|| i->premiumRequired != premiumRequired) {
					i->canRemove = canRemove;
					i->premiumRequired = premiumRequired;
					i->ripple.reset();
				}
				if (i->canRemove && !i->premiumRequired) {
					i->expanded = false;
				}
				_custom.push_back(std::move(*i));
				return;
			}
		}
		auto set = std::vector<CustomOne>();
		set.reserve(list.size());
		for (const auto document : list) {
			if (document->sticker()) {
				set.push_back({
					.custom = resolveCustomEmoji(document, setId),
					.document = document,
				});
				if (!premium && document->isPremiumEmoji()) {
					premium = true;
				}
			}
		}
		if (premium && !premiumPossible) {
			return;
		}
		_custom.push_back({
			.id = setId,
			.set = it->second.get(),
			.thumbnailDocument = it->second->lookupThumbnailDocument(),
			.title = it->second->title,
			.list = std::move(set),
			.canRemove = canRemove,
			.premiumRequired = premium && premiumMayBeBought,
		});
	};
	for (const auto setId : owner->stickers().emojiSetsOrder()) {
		push(setId, true);
	}
	for (const auto setId : owner->stickers().featuredEmojiSetsOrder()) {
		push(setId, false);
	}

	_footer->refreshIcons(
		fillIcons(),
		currentSet(getVisibleTop()),
		nullptr,
		ValidateIconAnimations::None);
	update();
}

Fn<void()> EmojiListWidget::repaintCallback(
		DocumentId documentId,
		uint64 setId) {
	return [=] {
		repaintCustom(setId);
		if (_recentCustomIds.contains(documentId)) {
			repaintCustom(RecentEmojiSectionSetId());
		}
	};
}

not_null<Ui::Text::CustomEmoji*> EmojiListWidget::resolveCustomEmoji(
		not_null<DocumentData*> document,
		uint64 setId) {
	Expects(document->sticker() != nullptr);

	const auto documentId = document->id;
	const auto i = _customEmoji.find(documentId);
	const auto recentOnly = (i != end(_customEmoji)) && i->second.recentOnly;
	if (i != end(_customEmoji) && !recentOnly) {
		return i->second.emoji.get();
	}
	auto instance = document->owner().customEmojiManager().create(
		document,
		repaintCallback(documentId, setId),
		Data::CustomEmojiManager::SizeTag::Large);
	if (recentOnly) {
		for (auto &recent : _recent) {
			if (recent.custom && recent.custom == i->second.emoji.get()) {
				recent.custom = instance.get();
			}
		}
		i->second.emoji = std::move(instance);
		i->second.recentOnly = false;
		return i->second.emoji.get();
	}
	return _customEmoji.emplace(
		documentId,
		CustomEmojiInstance{ .emoji = std::move(instance) }
	).first->second.emoji.get();
}

Ui::Text::CustomEmoji *EmojiListWidget::resolveCustomEmoji(
		RecentEmojiId customId) {
	const auto &data = customId.data;
	if (const auto document = std::get_if<RecentEmojiDocument>(&data)) {
		return resolveCustomEmoji(document->id);
	} else if (const auto emoji = std::get_if<EmojiPtr>(&data)) {
		return nullptr;
	}
	Unexpected("Custom recent emoji id.");
}

not_null<Ui::Text::CustomEmoji*> EmojiListWidget::resolveCustomEmoji(
		DocumentId documentId) {
	const auto i = _customEmoji.find(documentId);
	if (i != end(_customEmoji)) {
		return i->second.emoji.get();
	}
	return _customEmoji.emplace(
		documentId,
		CustomEmojiInstance{
			.emoji = session().data().customEmojiManager().create(
				documentId,
				repaintCallback(documentId, RecentEmojiSectionSetId()),
				Data::CustomEmojiManager::SizeTag::Large),
			.recentOnly = true,
		}
	).first->second.emoji.get();
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
		const auto availw = st::stickerIconWidth - 2 * st::emojiIconPadding;
		const auto availh = st::emojiFooterHeight - 2 * st::emojiIconPadding;
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

int EmojiListWidget::paintButtonGetWidth(
		QPainter &p,
		const SectionInfo &info,
		bool selected,
		QRect clip) const {
	if (info.section < kEmojiSectionCount
		|| info.section >= kEmojiSectionCount + _custom.size()) {
		return 0;
	}
	auto &custom = _custom[info.section - kEmojiSectionCount];
	if (hasRemoveButton(info.section)) {
		const auto remove = removeButtonRect(info);
		if (remove.intersects(clip)) {
			if (custom.ripple) {
				custom.ripple->paint(
					p,
					remove.x() + st::stickerPanRemoveSet.rippleAreaPosition.x(),
					remove.y() + st::stickerPanRemoveSet.rippleAreaPosition.y(),
					width());
				if (custom.ripple->empty()) {
					custom.ripple.reset();
				}
			}
			(selected
				? st::stickerPanRemoveSet.iconOver
				: st::stickerPanRemoveSet.icon).paint(
					p,
					remove.topLeft() + st::stickerPanRemoveSet.iconPosition,
					width());
		}
		return remove.width();
	}
	const auto canAdd = hasAddButton(info.section);
	const auto &button = rightButton(info.section);
	const auto rect = buttonRect(info, button);
	p.drawImage(rect.topLeft(), selected ? button.backOver : button.back);
	if (custom.ripple) {
		const auto ripple = QColor(0, 0, 0, 36);
		custom.ripple->paint(p, rect.x(), rect.y(), width(), &ripple);
		if (custom.ripple->empty()) {
			custom.ripple.reset();
		}
	}
	p.setPen(!canAdd
		? st::premiumButtonFg
		: selected
		? st::emojiPanButton.textFgOver
		: st::emojiPanButton.textFg);
	p.setFont(st::emojiPanButton.font);
	p.drawText(
		rect.x() - (st::emojiPanButton.width / 2),
		(rect.y()
			+ st::emojiPanButton.textTop
			+ st::emojiPanButton.font->ascent),
		button.text);
	return rect.width();
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
		if (hasButton(section)
			&& myrtlrect(buttonRect(section)).contains(p.x(), p.y())) {
			newSelected = OverButton{ section };
		} else if (section >= kEmojiSectionCount) {
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
			rtlupdate(buttonRect(button->section));
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

void EmojiListWidget::initButton(
		RightButton &button,
		const QString &text,
		bool gradient) {
	button.text = text;
	button.textWidth = st::emojiPanButton.font->width(text);
	const auto width = button.textWidth - st::emojiPanButton.width;
	const auto height = st::emojiPanButton.height;
	const auto factor = style::DevicePixelRatio();
	auto prepare = [&](QColor bg, QBrush fg) {
		auto image = QImage(
			QSize(width, height) * factor,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(factor);
		image.fill(Qt::transparent);
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(fg);
		const auto radius = height / 2.;
		p.drawRoundedRect(QRect(0, 0, width, height), radius, radius);
		p.end();
		return image;
	};
	button.back = prepare(Qt::transparent, [&]() -> QBrush {
		if (gradient) {
			auto result = QLinearGradient(QPointF(0, 0), QPointF(width, 0));
			result.setStops(Ui::Premium::GiftGradientStops());
			return result;
		}
		return st::emojiPanButton.textBg;
	}());
	button.backOver = gradient
		? button.back
		: prepare(Qt::transparent, st::emojiPanButton.textBgOver);
	button.rippleMask = prepare(Qt::black, Qt::white);
}

std::unique_ptr<Ui::RippleAnimation> EmojiListWidget::createButtonRipple(
		int section) {
	Expects(section >= kEmojiSectionCount
		&& section < kEmojiSectionCount + _custom.size());

	const auto remove = hasRemoveButton(section);
	const auto &st = remove
		? st::stickerPanRemoveSet.ripple
		: st::emojiPanButton.ripple;
	auto mask = remove
		? Ui::RippleAnimation::ellipseMask(QSize(
			st::stickerPanRemoveSet.rippleAreaSize,
			st::stickerPanRemoveSet.rippleAreaSize))
		: rightButton(section).rippleMask;
	return std::make_unique<Ui::RippleAnimation>(
		st,
		std::move(mask),
		[this, section] { rtlupdate(buttonRect(section)); });
}

QPoint EmojiListWidget::buttonRippleTopLeft(int section) const {
	Expects(section >= kEmojiSectionCount
		&& section < kEmojiSectionCount + _custom.size());

	return myrtlrect(buttonRect(section)).topLeft()
		+ (hasRemoveButton(section)
			? st::stickerPanRemoveSet.rippleAreaPosition
			: QPoint());
}

void EmojiListWidget::refreshEmoji() {
	refreshRecent();
	refreshCustom();
}

void EmojiListWidget::showSet(uint64 setId) {
	clearSelection();

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
