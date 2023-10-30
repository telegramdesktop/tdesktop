/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_list_widget.h"

#include "base/unixtime.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/tabbed_search.h"
#include "ui/text/format_values.h"
#include "ui/effects/animations.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/text/custom_emoji_instance.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/premium_graphics.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
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
#include "chat_helpers/emoji_keywords.h"
#include "chat_helpers/stickers_list_widget.h"
#include "chat_helpers/stickers_list_footer.h"
#include "emoji_suggestions_data.h"
#include "emoji_suggestions_helper.h"
#include "main/main_session.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "settings/settings_premium.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace ChatHelpers {
namespace {

constexpr auto kCollapsedRows = 3;
constexpr auto kAppearDuration = 0.3;
constexpr auto kCustomSearchLimit = 256;

using Core::RecentEmojiId;
using Core::RecentEmojiDocument;

} // namespace

class EmojiColorPicker final : public Ui::RpWidget {
public:
	EmojiColorPicker(QWidget *parent, const style::EmojiPan &st);

	void showEmoji(EmojiPtr emoji, bool allLabel = false);

	void clearSelection();
	void handleMouseMove(QPoint globalPos);
	void handleMouseRelease(QPoint globalPos);
	void setSingleSize(QSize size);

	void showAnimated();
	void hideAnimated();
	void hideFast();

	[[nodiscard]] rpl::producer<EmojiChosen> chosen() const;
	[[nodiscard]] rpl::producer<> hidden() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void createAllLabel();
	void animationCallback();
	void updateSize();
	[[nodiscard]] int topColorAllSkip() const;

	void drawVariant(QPainter &p, int variant);

	void updateSelected();
	void setSelected(int newSelected);

	const style::EmojiPan &_st;

	bool _ignoreShow = false;

	QVector<EmojiPtr> _variants;

	int _selected = -1;
	int _pressedSel = -1;
	QPoint _lastMousePos;
	QSize _singleSize;
	QPoint _areaPosition;
	QPoint _innerPosition;
	Ui::RoundRect _backgroundRect;
	Ui::RoundRect _overBg;

	bool _hiding = false;
	QPixmap _cache;
	Ui::Animations::Simple _a_opacity;

	std::unique_ptr<Ui::FlatLabel> _allLabel;

	rpl::event_stream<EmojiChosen> _chosen;
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

EmojiColorPicker::EmojiColorPicker(
	QWidget *parent,
	const style::EmojiPan &st)
: RpWidget(parent)
, _st(st)
, _backgroundRect(st::emojiPanRadius, _st.bg)
, _overBg(st::emojiPanRadius, _st.overBg) {
	setMouseTracking(true);
}

void EmojiColorPicker::showEmoji(EmojiPtr emoji, bool allLabel) {
	if (!emoji || !emoji->hasVariants()) {
		return;
	}
	if (!allLabel) {
		_allLabel = nullptr;
	} else if (!_allLabel) {
		createAllLabel();
	}
	_ignoreShow = false;

	_variants.resize(emoji->variantsCount() + 1);
	for (auto i = 0, size = int(_variants.size()); i != size; ++i) {
		_variants[i] = emoji->variant(i);
	}

	updateSize();

	if (!_cache.isNull()) {
		_cache = QPixmap();
	}
	showAnimated();
}

void EmojiColorPicker::createAllLabel() {
	_allLabel = std::make_unique<Ui::FlatLabel>(
		this,
		tr::lng_emoji_color_all(),
		_st.colorAllLabel);
	_allLabel->show();
	_allLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
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
	if (_allLabel) {
		_allLabel->resizeToWidth(width
			- st::emojiPanMargins.left()
			- st::emojiPanMargins.right()
			- st::emojiPanColorAllPadding.left()
			- st::emojiPanColorAllPadding.right());
		_allLabel->move(
			st::emojiPanMargins.left() + st::emojiPanColorAllPadding.left(),
			st::emojiPanMargins.top() + st::emojiPanColorAllPadding.top());
		height += topColorAllSkip();
	}
	resize(width, height);
	update();
	updateSelected();
}

void EmojiColorPicker::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

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
	Ui::Shadow::paint(p, inner, width(), _st.showAnimation.shadow);
	_backgroundRect.paint(p, inner);

	const auto skip = topColorAllSkip();
	auto x = st::emojiPanMargins.left() + 2 * st::emojiColorsPadding + _singleSize.width();
	if (rtl()) x = width() - x - st::emojiColorsSep;
	p.fillRect(x, st::emojiPanMargins.top() + skip + st::emojiColorsPadding, st::emojiColorsSep, inner.height() - st::emojiColorsPadding * 2 - skip, st::emojiColorsSepColor);

	if (_variants.isEmpty()) {
		return;
	}
	p.translate(0, skip);
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
		_chosen.fire_copy({ .emoji = _variants[_selected] });
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
		if (_allLabel) {
			_allLabel->show();
		}
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

rpl::producer<EmojiChosen> EmojiColorPicker::chosen() const {
	return _chosen.events();
}

rpl::producer<> EmojiColorPicker::hidden() const {
	return _hidden.events();
}

void EmojiColorPicker::hideAnimated() {
	if (_cache.isNull()) {
		if (_allLabel) {
			_allLabel->show();
		}
		_cache = Ui::GrabWidget(this);
		clearSelection();
	}
	_hiding = true;
	if (_allLabel) {
		_allLabel->hide();
	}
	_a_opacity.start([this] { animationCallback(); }, 1., 0., st::emojiPanDuration);
}

void EmojiColorPicker::showAnimated() {
	if (_ignoreShow) return;

	if (!isHidden() && !_hiding) {
		return;
	}
	_hiding = false;
	if (_cache.isNull()) {
		if (_allLabel) {
			_allLabel->show();
		}
		_cache = Ui::GrabWidget(this);
		clearSelection();
	}
	show();
	if (_allLabel) {
		_allLabel->hide();
	}
	_a_opacity.start([this] { animationCallback(); }, 0., 1., st::emojiPanDuration);
}

void EmojiColorPicker::clearSelection() {
	_pressedSel = -1;
	setSelected(-1);
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
}

int EmojiColorPicker::topColorAllSkip() const {
	return _allLabel
		? (st::emojiPanColorAllPadding.top()
			+ _allLabel->height()
			+ st::emojiPanColorAllPadding.bottom())
		: 0;
}

void EmojiColorPicker::updateSelected() {
	auto newSelected = -1;
	auto p = mapFromGlobal(_lastMousePos);
	auto sx = rtl() ? (width() - p.x()) : p.x(), y = p.y() - st::emojiPanMargins.top() - topColorAllSkip() - st::emojiColorsPadding;
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
	const auto skip = topColorAllSkip();
	const auto updateSelectedRect = [&] {
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
			st::emojiPanMargins.top() + st::emojiColorsPadding + skip,
			_singleSize.width(),
			_singleSize.height());
	};
	updateSelectedRect();
	_selected = newSelected;
	updateSelectedRect();
	setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
}

void EmojiColorPicker::drawVariant(QPainter &p, int variant) {
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

		_overBg.paint(p, QRect(tl, st::emojiPanArea));
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
	PauseReason level,
	Mode mode)
: EmojiListWidget(parent, {
	.show = controller->uiShow(),
	.mode = mode,
	.paused = Window::PausedIn(controller, level),
}) {
}

EmojiListWidget::EmojiListWidget(
	QWidget *parent,
	EmojiListDescriptor &&descriptor)
: Inner(
	parent,
	descriptor.st ? *descriptor.st : st::defaultEmojiPan,
	descriptor.show,
	std::move(descriptor.paused))
, _show(std::move(descriptor.show))
, _features(descriptor.features)
, _mode(descriptor.mode)
, _staticCount(_mode == Mode::Full ? kEmojiSectionCount : 1)
, _premiumIcon(_mode == Mode::EmojiStatus
	? std::make_unique<GradientPremiumStar>()
	: nullptr)
, _localSetsManager(
	std::make_unique<LocalStickersManager>(&session()))
, _customRecentFactory(std::move(descriptor.customRecentFactory))
, _customTextColor(std::move(descriptor.customTextColor))
, _overBg(st::emojiPanRadius, st().overBg)
, _collapsedBg(st::emojiPanExpand.height / 2, st().headerFg)
, _picker(this, st())
, _showPickerTimer([=] { showPicker(); }) {
	setMouseTracking(true);
	if (st().bg->c.alpha() > 0) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	if (_mode != Mode::RecentReactions && _mode != Mode::BackgroundEmoji) {
		setupSearch();
	}

	_customSingleSize = Data::FrameSizeFromTag(
		Data::CustomEmojiManager::SizeTag::Large
	) / style::DevicePixelRatio();

	_picker->hide();

	for (auto i = 1; i != _staticCount; ++i) {
		const auto section = static_cast<Section>(i);
		_counts[i] = Ui::Emoji::GetSectionCount(section);
	}

	_picker->chosen(
	) | rpl::start_with_next([=](EmojiChosen data) {
		colorChosen(data);
	}, lifetime());

	_picker->hidden(
	) | rpl::start_with_next([=] {
		pickerHidden();
	}, lifetime());

	session().data().stickers().updated(
		Data::StickersType::Emoji
	) | rpl::start_with_next([=] {
		refreshCustom();
		resizeToWidth(width());
	}, lifetime());

	rpl::combine(
		Data::AmPremiumValue(&session()),
		session().premiumPossibleValue()
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

	if (!descriptor.customRecentList.empty()) {
		fillRecentFrom(descriptor.customRecentList);
	}
}

EmojiListWidget::~EmojiListWidget() {
	base::take(_customEmoji);
}

void EmojiListWidget::setupSearch() {
	const auto session = &_show->session();
	_search = MakeSearch(this, st(), [=](std::vector<QString> &&query) {
		_nextSearchQuery = std::move(query);
		InvokeQueued(this, [=] {
			applyNextSearchQuery();
		});
	}, session, (_mode == Mode::EmojiStatus), _mode == Mode::UserpicBuilder);
}

void EmojiListWidget::applyNextSearchQuery() {
	if (_searchQuery == _nextSearchQuery) {
		return;
	}
	_searchQuery = _nextSearchQuery;
	std::swap(_searchEmoji, _searchEmojiPrevious);
	_searchEmoji.clear();
	const auto finish = [&](bool searching = true) {
		if (!_searchMode && !searching) {
			return;
		}
		const auto modeChanged = (_searchMode != searching);
		clearSelection();
		if (modeChanged) {
			_searchMode = searching;
		}
		if (!searching) {
			_searchResults.clear();
			_searchCustomIds.clear();
		}
		resizeToWidth(width());
		update();
		if (modeChanged) {
			visibleTopBottomUpdated(getVisibleTop(), getVisibleBottom());
		}
		updateSelected();
	};
	if (_searchQuery.empty()) {
		finish(false);
		return;
	}
	const auto guard = gsl::finally([&] { finish(); });
	auto plain = collectPlainSearchResults();
	if (_searchEmoji == _searchEmojiPrevious) {
		return;
	}
	_searchResults.clear();
	_searchCustomIds.clear();
	if (_mode != Mode::Full || session().premium()) {
		appendPremiumSearchResults();
	}
	if (_mode == Mode::Full) {
		for (const auto emoji : plain) {
			_searchResults.push_back({
				.id = { emoji },
			});
		}
	}
}

std::vector<EmojiPtr> EmojiListWidget::collectPlainSearchResults() {
	return SearchEmoji(_searchQuery, _searchEmoji);
}

void EmojiListWidget::appendPremiumSearchResults() {
	const auto test = session().isTestMode();
	auto &owner = session().data();
	const auto checkCustom = [&](EmojiPtr emoji, DocumentId id) {
		return emoji
			&& _searchEmoji.contains(emoji)
			&& (_searchResults.size() < kCustomSearchLimit)
			&& _searchCustomIds.emplace(id).second;
	};
	for (const auto &recent : _recent) {
		if (!recent.custom) {
			continue;
		}
		const auto &idData = recent.id.data;
		const auto id = std::get_if<Core::RecentEmojiDocument>(&idData);
		if (!id || id->test != test) {
			continue;
		}
		const auto sticker = owner.document(id->id)->sticker();
		const auto emoji = sticker
			? Ui::Emoji::Find(sticker->alt)
			: nullptr;
		if (checkCustom(emoji, id->id)) {
			_searchResults.push_back(recent);
		}
	}
	for (const auto &set : _custom) {
		for (const auto &one : set.list) {
			const auto id = one.document->id;
			if (checkCustom(one.emoji, id)) {
				_searchResults.push_back({
					.custom = one.custom,
					.id = { RecentEmojiDocument{ .id = id, .test = test } },
				});
			}
		}
	}
}

void EmojiListWidget::provideRecent(
		const std::vector<DocumentId> &customRecentList) {
	clearSelection();
	fillRecentFrom(customRecentList);
	resizeToWidth(width());
}

void EmojiListWidget::repaintCustom(uint64 setId) {
	if (!_repaintsScheduled.emplace(setId).second) {
		return;
	}
	const auto repaintSearch = (setId == SearchEmojiSectionSetId());
	if (_searchMode) {
		if (repaintSearch) {
			update();
		}
		return;
	}
	const auto repaintRecent = (setId == RecentEmojiSectionSetId());
	enumerateSections([&](const SectionInfo &info) {
		const auto repaint1 = repaintRecent
			&& (info.section == int(Section::Recent));
		const auto repaint2 = !repaint1
			&& (info.section >= _staticCount)
			&& (setId == _custom[info.section - _staticCount].id);
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

rpl::producer<EmojiChosen> EmojiListWidget::chosen() const {
	return _chosen.events();
}

rpl::producer<FileChosen> EmojiListWidget::customChosen() const {
	return _customChosen.events();
}

rpl::producer<> EmojiListWidget::jumpedToPremium() const {
	return _jumpedToPremium.events();
}

rpl::producer<> EmojiListWidget::escapes() const {
	return _search ? _search->escapes() : rpl::never<>();
}

void EmojiListWidget::prepareExpanding() {
	if (_search) {
		_searchExpandCache = _search->grab();
	}
}

void EmojiListWidget::paintExpanding(
		Painter &p,
		QRect clip,
		int finalBottom,
		float64 geometryProgress,
		float64 fullProgress,
		RectPart origin) {
	const auto searchShift = _search
		? anim::interpolate(
			st().padding.top() - _search->height(),
			0,
			geometryProgress)
		: 0;
	const auto shift = clip.topLeft() + QPoint(0, searchShift);
	const auto adjusted = clip.translated(-shift);
	const auto finalHeight = (finalBottom - clip.y());
	if (!_searchExpandCache.isNull()) {
		p.setClipRect(clip);
		p.drawImage(
			clip.x() + st().searchMargin.left(),
			clip.y() + st().searchMargin.top() + searchShift,
			_searchExpandCache);
	}
	p.translate(shift);
	p.setClipRect(adjusted);
	paint(p, ExpandingContext{
		.progress = fullProgress,
		.finalHeight = finalHeight,
		.expanding = true,
	}, adjusted);
	p.translate(-shift);
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
	} else if (info.section < _staticCount) {
		return;
	}
	auto &custom = _custom[info.section - _staticCount];
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
	const auto flag = powerSavingFlag();
	const auto footerPaused = [method = pausedMethod(), flag]() {
		return On(flag) || method();
	};
	auto result = object_ptr<StickersListFooter>(FooterDescriptor{
		.session = &session(),
		.customTextColor = _customTextColor,
		.paused = footerPaused,
		.parent = this,
		.st = &st(),
		.features = { .stickersSettings = false },
		.forceFirstFrame = (_mode == Mode::BackgroundEmoji),
	});
	_footer = result;

	_footer->setChosen(
	) | rpl::start_with_next([=](uint64 setId) {
		showSet(setId);
	}, _footer->lifetime());

	return result;
}

void EmojiListWidget::afterShown() {
	const auto steal = (_mode == Mode::EmojiStatus)
		|| (_mode == Mode::FullReactions)
		|| (_mode == Mode::UserpicBuilder);
	if (_search && steal) {
		_search->stealFocus();
	}
}

void EmojiListWidget::beforeHiding() {
	if (_search) {
		_search->returnFocus();
	}
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
			+ (i == 0 ? _rowsTop : st().header);
		info.rowsBottom = info.rowsTop
			+ (info.rowsCount * _singleSize.height());
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
		return true;
	};
	if (_searchMode) {
		info.section = i;
		info.count = _searchResults.size();
		return next();
	}
	for (; i != _staticCount; ++i) {
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
	return _searchMode ? 1 : (_staticCount + int(_custom.size()));
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
	const auto customSkip = (esize - _customSingleSize) / 2;
	_customPosition = QPoint(customSkip, customSkip);
	_picker->setSingleSize(_singleSize);
}

void EmojiListWidget::setColorAllForceRippled(bool force) {
	_colorAllRippleForced = force;
	if (_colorAllRippleForced) {
		_colorAllRippleForcedLifetime = style::PaletteChanged(
		) | rpl::filter([=] {
			return _colorAllRipple != nullptr;
		}) | rpl::start_with_next([=] {
			_colorAllRipple->forceRepaint();
		});
		if (!_colorAllRipple) {
			_colorAllRipple = createButtonRipple(int(Section::People));
		}
		if (_colorAllRipple->empty()) {
			_colorAllRipple->addFading();
		} else {
			_colorAllRipple->lastUnstop();
		}
	} else {
		if (_colorAllRipple) {
			_colorAllRipple->lastStop();
		}
		_colorAllRippleForcedLifetime.destroy();
	}
}

int EmojiListWidget::countDesiredHeight(int newWidth) {
	const auto fullWidth = st().margin.left()
		+ newWidth
		+ st().margin.right();
	const auto padding = st().padding;
	const auto innerWidth = fullWidth - padding.left() - padding.right();
	_columnCount = std::max(innerWidth / st().desiredSize, 1);
	const auto singleWidth = innerWidth / _columnCount;
	_rowsTop = _search ? _search->height() : padding.top();
	_rowsLeft = padding.left()
		+ (innerWidth - _columnCount * singleWidth) / 2
		- st().margin.left();
	setSingleSize({ singleWidth, singleWidth - 2 * st().verticalSizeSub });

	const auto countResult = [this](int minimalLastHeight) {
		const auto info = sectionInfo(sectionsCount() - 1);
		return info.top
			+ qMax(info.rowsBottom - info.top, minimalLastHeight);
	};
	const auto minimalHeight = this->minimalHeight();
	const auto minimalLastHeight = std::max(
		minimalHeight - padding.bottom(),
		0);
	return qMax(
		minimalHeight,
		countResult(minimalLastHeight) + padding.bottom());
}

int EmojiListWidget::defaultMinimalHeight() const {
	return Inner::defaultMinimalHeight();
}

void EmojiListWidget::ensureLoaded(int section) {
	Expects(section >= 0 && section < sectionsCount());

	if (section == int(Section::Recent)) {
		if (_recent.empty()) {
			fillRecent();
		}
		return;
	} else if (section >= _staticCount || !_emoji[section].empty()) {
		return;
	}
	_emoji[section] = Ui::Emoji::GetSection(static_cast<Section>(section));
	_counts[section] = _emoji[section].size();

	const auto &settings = Core::App().settings();
	for (auto &emoji : _emoji[section]) {
		emoji = settings.lookupEmojiVariant(emoji);
	}
}

void EmojiListWidget::fillRecent() {
	if (_mode != Mode::Full) {
		return;
	}
	_recent.clear();
	_recentCustomIds.clear();

	const auto &list = Core::App().settings().recentEmoji();
	_recent.reserve(std::min(int(list.size()), Core::kRecentEmojiLimit) + 1);
	const auto test = session().isTestMode();
	for (const auto &one : list) {
		const auto document = std::get_if<RecentEmojiDocument>(&one.id.data);
		if (document && document->test != test) {
			continue;
		}
		_recent.push_back({
			.custom = resolveCustomRecent(one.id),
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

void EmojiListWidget::fillRecentFrom(const std::vector<DocumentId> &list) {
	const auto test = session().isTestMode();
	_recent.clear();
	_recent.reserve(list.size());
	for (const auto &id : list) {
		if (!id && _mode == Mode::EmojiStatus) {
			const auto star = QString::fromUtf8("\xe2\xad\x90\xef\xb8\x8f");
			_recent.push_back({ .id = { Ui::Emoji::Find(star) } });
		} else if (!id && _mode == Mode::BackgroundEmoji) {
			const auto fakeId = DocumentId(5246772116543512028ULL);
			const auto no = QString::fromUtf8("\xe2\x9b\x94\xef\xb8\x8f");
			_recent.push_back({
				.custom = resolveCustomRecent(fakeId),
				.id = { Ui::Emoji::Find(no) },
			});
			_recentCustomIds.emplace(fakeId);
		} else {
			_recent.push_back({
				.custom = resolveCustomRecent(id),
				.id = { RecentEmojiDocument{ .id = id, .test = test } },
			});
			_recentCustomIds.emplace(id);
		}
	}
}

base::unique_qptr<Ui::PopupMenu> EmojiListWidget::fillContextMenu(
		SendMenu::Type type) {
	if (v::is_null(_selected)) {
		return nullptr;
	}
	const auto over = std::get_if<OverEmoji>(&_selected);
	if (!over) {
		return nullptr;
	}
	const auto section = over->section;
	const auto index = over->index;
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		(_mode == Mode::Full
			? st::popupMenuWithIcons
			: st::defaultPopupMenu));
	if (_mode == Mode::Full) {
		fillRecentMenu(menu, section, index);
	} else if (_mode == Mode::EmojiStatus) {
		fillEmojiStatusMenu(menu, section, index);
	}
	if (menu->empty()) {
		return nullptr;
	}
	return menu;
}

void EmojiListWidget::fillRecentMenu(
		not_null<Ui::PopupMenu*> menu,
		int section,
		int index) {
	if (section != int(Section::Recent)) {
		return;
	}
	const auto addAction = Ui::Menu::CreateAddActionCallback(menu);
	const auto over = OverEmoji{ section, index };
	const auto emoji = lookupOverEmoji(&over);
	const auto custom = lookupCustomEmoji(index, section);
	if (custom && custom->sticker()) {
		const auto sticker = custom->sticker();
		const auto emoji = sticker->alt;
		const auto setId = sticker->set.id;
		if (!emoji.isEmpty()) {
			auto data = TextForMimeData{ emoji, { emoji } };
			data.rich.entities.push_back({
				EntityType::CustomEmoji,
				0,
				int(emoji.size()),
				Data::SerializeCustomEmojiId(custom)
			});
			addAction(tr::lng_emoji_copy(tr::now), [=] {
				TextUtilities::SetClipboardText(data);
			}, &st::menuIconCopy);
		}
		if (setId && _features.openStickerSets) {
			addAction(
				tr::lng_emoji_view_pack(tr::now),
				crl::guard(this, [=] { displaySet(setId); }),
				&st::menuIconShowAll);
		}
	} else if (emoji) {
		addAction(tr::lng_emoji_copy(tr::now), [=] {
			const auto text = emoji->text();
			TextUtilities::SetClipboardText({ text, { text } });
		}, &st::menuIconCopy);
	}
	auto id = RecentEmojiId{ emoji };
	if (custom) {
		id.data = RecentEmojiDocument{
			.id = custom->id,
			.test = custom->session().isTestMode(),
		};
	}
	addAction(tr::lng_emoji_remove_recent(tr::now), crl::guard(this, [=] {
		Core::App().settings().hideRecentEmoji(id);
		refreshRecent();
	}), &st::menuIconCancel);

	menu->addSeparator(&st().expandedSeparator);

	const auto resetRecent = [=] {
		const auto sure = [=](Fn<void()> &&close) {
			Core::App().settings().resetRecentEmoji();
			refreshRecent();
			close();
		};
		checkHideWithBox(Ui::MakeConfirmBox({
			.text = tr::lng_emoji_reset_recent_sure(),
			.confirmed = crl::guard(this, sure),
			.confirmText = tr::lng_emoji_reset_recent_button(tr::now),
			.labelStyle = &st().boxLabel,
			}));
	};
	addAction({
		.text = tr::lng_emoji_reset_recent(tr::now),
		.handler = crl::guard(this, resetRecent),
		.icon = &st::menuIconRestoreAttention,
		.isAttention = true,
	});
}

void EmojiListWidget::fillEmojiStatusMenu(
		not_null<Ui::PopupMenu*> menu,
		int section,
		int index) {
	const auto chosen = lookupCustomEmoji(index, section);
	if (!chosen) {
		return;
	}
	const auto selectWith = [=](TimeId scheduled) {
		selectCustom(
			lookupChosen(chosen, nullptr, { .scheduled = scheduled }));
	};
	for (const auto &value : { 3600, 3600 * 8, 3600 * 24, 3600 * 24 * 7 }) {
		const auto text = tr::lng_emoji_status_menu_duration_any(
			tr::now,
			lt_duration,
			Ui::FormatMuteFor(value));
		menu->addAction(text, crl::guard(this, [=] {
			selectWith(base::unixtime::now() + value);
		}));
	}
	menu->addAction(
		tr::lng_manage_messages_ttl_after_custom(tr::now),
		crl::guard(this, [=] { selectWith(
			TabbedSelector::kPickCustomTimeId); }));
}

void EmojiListWidget::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto clip = e ? e->rect() : rect();

	_repaintsScheduled.clear();
	if (_grabbingChosen) {
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(clip, Qt::transparent);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	} else if (st().bg->c.alpha() > 0) {
		p.fillRect(clip, st().bg);
	}
	if (!_searchExpandCache.isNull()) {
		_searchExpandCache = QImage();
	}

	paint(p, {}, clip);
}

void EmojiListWidget::validateEmojiPaintContext(
		const ExpandingContext &context) {
	auto value = Ui::Text::CustomEmojiPaintContext{
		.textColor = (_customTextColor
			? _customTextColor()
			: (_mode == Mode::EmojiStatus)
			? anim::color(
				st::stickerPanPremium1,
				st::stickerPanPremium2,
				0.5)
			: st().textFg->c),
		.size = QSize(_customSingleSize, _customSingleSize),
		.now = crl::now(),
		.scale = context.progress,
		.paused = On(powerSavingFlag()) || paused(),
		.scaled = context.expanding,
		.internal = { .forceFirstFrame = (_mode == Mode::BackgroundEmoji) },
	};
	if (!_emojiPaintContext) {
		_emojiPaintContext = std::make_unique<
			Ui::Text::CustomEmojiPaintContext
		>(std::move(value));
	} else {
		*_emojiPaintContext = std::move(value);
	}
}

void EmojiListWidget::paint(
		Painter &p,
		ExpandingContext context,
		QRect clip) {
	validateEmojiPaintContext(context);

	auto fromColumn = floorclamp(
		clip.x() - _rowsLeft,
		_singleSize.width(),
		0,
		_columnCount);
	auto toColumn = ceilclamp(
		clip.x() + clip.width() - _rowsLeft,
		_singleSize.width(),
		0,
		_columnCount);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = _columnCount - fromColumn;
		toColumn = _columnCount - toColumn;
	}
	const auto expandProgress = context.progress;
	auto selectedButton = std::get_if<OverButton>(!v::is_null(_pressed)
		? &_pressed
		: &_selected);
	if (_searchResults.empty() && _searchMode) {
		paintEmptySearchResults(p);
	}
	enumerateSections([&](const SectionInfo &info) {
		if (clip.top() >= info.rowsBottom) {
			return true;
		} else if (clip.top() + clip.height() <= info.top) {
			return false;
		}
		const auto buttonSelected = selectedButton
			? (selectedButton->section == info.section)
			: false;
		const auto titleLeft = (info.premiumRequired
			? st().headerLockedLeft
			: st().headerLeft) - st().margin.left();
		const auto widthForTitle = emojiRight()
			- titleLeft
			- paintButtonGetWidth(p, info, buttonSelected, clip);
		if (info.section > 0 && clip.top() < info.rowsTop) {
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st().headerFg);
			auto titleText = (info.section < _staticCount)
				? ChatHelpers::EmojiCategoryTitle(info.section)(tr::now)
				: _custom[info.section - _staticCount].title;
			auto titleWidth = st::emojiPanHeaderFont->width(titleText);
			if (titleWidth > widthForTitle) {
				titleText = st::emojiPanHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::emojiPanHeaderFont->width(titleText);
			}
			const auto top = info.top + st().headerTop;
			if (info.premiumRequired) {
				st::emojiPremiumRequired.paint(
					p,
					st().headerLockLeft - st().margin.left(),
					top,
					width());
			}
			const auto textBaseline = top + st::emojiPanHeaderFont->ascent;
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st().headerFg);
			p.drawText(titleLeft, textBaseline, titleText);
		}
		if (clip.top() + clip.height() > info.rowsTop) {
			ensureLoaded(info.section);
			auto fromRow = floorclamp(
				clip.y() - info.rowsTop,
				_singleSize.height(),
				0,
				info.rowsCount);
			auto toRow = ceilclamp(
				clip.y() + clip.height() - info.rowsTop,
				_singleSize.height(),
				0,
				info.rowsCount);
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
					const auto position = QPoint(
						_rowsLeft + j * _singleSize.width(),
						info.rowsTop + i * _singleSize.height()
					);
					const auto w = position + _areaPosition;
					if (context.expanding) {
						const auto y = (position.y() - _rowsTop);
						const auto x = (position.x() - _rowsLeft);
						const auto sum = y
							+ std::max(std::min(y, width()) - x, 0);
						const auto maxSum = context.finalHeight
							+ std::min(context.finalHeight, width());
						const auto started = (sum / float64(maxSum))
							- kAppearDuration;
						context.progress = (expandProgress <= started)
							? 0.
							: (expandProgress >= started + kAppearDuration)
							? 1.
							: ((expandProgress - started) / kAppearDuration);
					}
					if (info.collapsed
						&& index + 1 == _columnCount * kCollapsedRows) {
						drawCollapsedBadge(p, w - _areaPosition, info.count);
						continue;
					}
					if (!_grabbingChosen
						&& selected
						&& st().overBg->c.alpha() > 0) {
						auto tl = w;
						if (rtl()) {
							tl.setX(width() - tl.x() - st::emojiPanArea.width());
						}
						_overBg.paint(p, QRect(tl, st::emojiPanArea));
					}
					if (_searchMode) {
						drawRecent(p, context, w, _searchResults[index]);
					} else if (info.section == int(Section::Recent)) {
						drawRecent(p, context, w, _recent[index]);
					} else if (info.section < _staticCount) {
						drawEmoji(p, context, w, _emoji[info.section][index]);
					} else {
						const auto set = info.section - _staticCount;
						drawCustom(p, context, w, set, index);
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
	p.setPen(this->st().bg);
	p.setFont(st.font);
	p.drawText(
		buttonx + (buttonw - textWidth) / 2,
		(buttony + st.textTop + st.font->ascent),
		text);
}

void EmojiListWidget::drawRecent(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		const RecentOne &recent) {
	_recentPainted = true;
	if (const auto custom = recent.custom) {
		_emojiPaintContext->scale = context.progress;
		_emojiPaintContext->position = position
			+ _innerPosition
			+ _customPosition;
		custom->paint(p, *_emojiPaintContext);
	} else if (const auto emoji = std::get_if<EmojiPtr>(&recent.id.data)) {
		if (_mode == Mode::EmojiStatus) {
			position += QPoint(
				(_singleSize.width() - st::emojiStatusDefault.width()) / 2,
				(_singleSize.height() - st::emojiStatusDefault.height()) / 2
			) - _areaPosition;
			p.drawImage(position, _premiumIcon->image());
		} else {
			drawEmoji(p, context, position, *emoji);
		}
	} else {
		Unexpected("Empty custom emoji in EmojiListWidget::drawRecent.");
	}
}

void EmojiListWidget::drawEmoji(
		QPainter &p,
		const ExpandingContext &context,
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
		const ExpandingContext &context,
		QPoint position,
		int set,
		int index) {
	auto &custom = _custom[set];
	custom.painted = true;
	auto &entry = custom.list[index];
	_emojiPaintContext->scale = context.progress;
	_emojiPaintContext->position = position
		+ _innerPosition
		+ _customPosition;
	entry.custom->paint(p, *_emojiPaintContext);
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

DocumentData *EmojiListWidget::lookupCustomEmoji(
		int index,
		int section) const {
	if (_searchMode) {
		if (index < _searchResults.size()) {
			const auto document = std::get_if<RecentEmojiDocument>(
				&_searchResults[index].id.data);
			if (document) {
				return session().data().document(document->id);
			}
		}
		return nullptr;
	} else if (section == int(Section::Recent) && index < _recent.size()) {
		const auto document = std::get_if<RecentEmojiDocument>(
			&_recent[index].id.data);
		if (document) {
			return session().data().document(document->id);
		}
	} else if (section >= _staticCount
		&& index < _custom[section - _staticCount].list.size()) {
		auto &set = _custom[section - _staticCount];
		return set.list[index].document;
	}
	return nullptr;
}

EmojiPtr EmojiListWidget::lookupOverEmoji(const OverEmoji *over) const {
	const auto section = over ? over->section : -1;
	const auto index = over ? over->index : -1;
	return _searchMode
		? ((index < _searchResults.size()
			&& v::is<EmojiPtr>(_searchResults[index].id.data))
			? v::get<EmojiPtr>(_searchResults[index].id.data)
			: nullptr)
		: (section == int(Section::Recent)
			&& index < _recent.size()
			&& v::is<EmojiPtr>(_recent[index].id.data))
		? v::get<EmojiPtr>(_recent[index].id.data)
		: (section > int(Section::Recent)
			&& section < _staticCount
			&& index < _emoji[section].size())
		? _emoji[section][index]
		: nullptr;
}

EmojiChosen EmojiListWidget::lookupChosen(
		EmojiPtr emoji,
		not_null<const OverEmoji*> over) {
	const auto rect = emojiRect(over->section, over->index);
	const auto size = st::emojiStatusDefault.size();
	const auto icon = QRect(
		rect.x() + (_singleSize.width() - size.width()) / 2,
		rect.y() + (_singleSize.height() - size.height()) / 2,
		rect.width(),
		rect.height());
	return {
		.emoji = emoji,
		.messageSendingFrom = {
			.type = Ui::MessageSendingAnimationFrom::Type::Emoji,
			.globalStartGeometry = mapToGlobal(icon),
		},
	};
}

FileChosen EmojiListWidget::lookupChosen(
		not_null<DocumentData*> custom,
		const OverEmoji *over,
		Api::SendOptions options) {
	_grabbingChosen = true;
	const auto guard = gsl::finally([&] { _grabbingChosen = false; });
	const auto rect = over ? emojiRect(over->section, over->index) : QRect();
	const auto emoji = over ? QRect(
		rect.topLeft() + _areaPosition + _innerPosition + _customPosition,
		QSize(_customSingleSize, _customSingleSize)
	) : QRect();

	return {
		.document = custom,
		.options = options,
		.messageSendingFrom = {
			.type = Ui::MessageSendingAnimationFrom::Type::Emoji,
			.globalStartGeometry = over ? mapToGlobal(emoji) : QRect(),
			.frame = over ? Ui::GrabWidgetToImage(this, emoji) : QImage(),
		},
	};
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
			if (!Core::App().settings().hasChosenEmojiVariant(emoji)) {
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
			if (emoji
				&& emoji->hasVariants()
				&& Core::App().settings().hasChosenEmojiVariant(emoji)) {
				_picker->hideAnimated();
				_pickerSelected = v::null;
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
		if (section >= _staticCount
			&& sectionInfo(section).collapsed
			&& index + 1 == _columnCount * kCollapsedRows) {
			_custom[section - _staticCount].expanded = true;
			resizeToWidth(width());
			update();
			return;
		} else if (const auto emoji = lookupOverEmoji(over)) {
			if (emoji->hasVariants() && !_picker->isHidden()) {
				return;
			}
			selectEmoji(lookupChosen(emoji, over));
		} else if (const auto custom = lookupCustomEmoji(index, section)) {
			selectCustom(lookupChosen(custom, over));
		}
	} else if (const auto set = std::get_if<OverSet>(&pressed)) {
		Assert(set->section >= _staticCount
			&& set->section < _staticCount + _custom.size());
		displaySet(_custom[set->section - _staticCount].id);
	} else if (auto button = std::get_if<OverButton>(&pressed)) {
		Assert(hasButton(button->section));
		const auto id = hasColorButton(button->section)
			? 0
			: _custom[button->section - _staticCount].id;
		const auto usage = ChatHelpers::WindowUsage::PremiumPromo;
		if (hasColorButton(button->section)) {
			_pickerSelected = pressed;
			showPicker();
		} else if (hasRemoveButton(button->section)) {
			removeSet(id);
		} else if (hasAddButton(button->section)) {
			_localSetsManager->install(id);
		} else if (const auto resolved = _show->resolveWindow(usage)) {
			_jumpedToPremium.fire({});
			switch (_mode) {
			case Mode::Full:
			case Mode::UserpicBuilder:
				Settings::ShowPremium(resolved, u"animated_emoji"_q);
				break;
			case Mode::FullReactions:
			case Mode::RecentReactions:
				Settings::ShowPremium(resolved, u"infinite_reactions"_q);
				break;
			case Mode::EmojiStatus:
				Settings::ShowPremium(resolved, u"emoji_status"_q);
				break;
			case Mode::TopicIcon:
				Settings::ShowPremium(resolved, u"forum_topic_icon"_q);
				break;
			case Mode::BackgroundEmoji:
				Settings::ShowPremium(resolved, u"name_color"_q);
				break;
			}
		}
	}
}

void EmojiListWidget::displaySet(uint64 setId) {
	const auto &sets = session().data().stickers().sets();
	auto it = sets.find(setId);
	if (it != sets.cend()) {
		checkHideWithBox(Box<StickerSetBox>(_show, it->second.get()));
	}
}

void EmojiListWidget::removeSet(uint64 setId) {
	const auto &labelSt = st().boxLabel;
	if (auto box = MakeConfirmRemoveSetBox(&session(), labelSt, setId)) {
		checkHideWithBox(std::move(box));
	}
}

void EmojiListWidget::selectEmoji(EmojiChosen data) {
	Core::App().settings().incrementRecentEmoji({ data.emoji });
	_chosen.fire(std::move(data));
}

void EmojiListWidget::selectCustom(FileChosen data) {
	const auto document = data.document;
	const auto skip = (document->isPremiumEmoji() && !session().premium());
	if (!skip && _mode == Mode::Full) {
		auto &settings = Core::App().settings();
		settings.incrementRecentEmoji({ RecentEmojiDocument{
			document->id,
			document->session().isTestMode(),
		} });
	}
	_customChosen.fire(std::move(data));
}

void EmojiListWidget::showPicker() {
	if (v::is_null(_pickerSelected)) {
		return;
	}
	const auto showAt = [&](float64 xCoef, int y, int height) {
		y -= _picker->height() - st::emojiPanRadius + getVisibleTop();
		if (y < st().header) {
			y += _picker->height() + height;
		}
		auto xmax = width() - _picker->width();
		if (rtl()) xCoef = 1. - xCoef;
		_picker->move(qRound(xmax * xCoef), y);

		disableScroll(true);
	};
	if (const auto button = std::get_if<OverButton>(&_pickerSelected)) {
		const auto hand = QString::fromUtf8("\xF0\x9F\x91\x8B");
		const auto emoji = Ui::Emoji::Find(hand);
		Assert(emoji != nullptr && emoji->hasVariants());
		_picker->showEmoji(emoji, true);
		setColorAllForceRippled(true);
		const auto rect = buttonRect(button->section);
		showAt(1., rect.y(), rect.height() - 2 * st::emojiPanRadius);
	} else if (const auto over = std::get_if<OverEmoji>(&_pickerSelected)) {
		const auto emoji = lookupOverEmoji(over);
		if (emoji && emoji->hasVariants()) {
			_picker->showEmoji(emoji);

			const auto coef = float64(over->index % _columnCount)
				/ float64(_columnCount - 1);
			const auto h = _singleSize.height() - 2 * st::emojiPanRadius;
			showAt(coef, emojiRect(over->section, over->index).y(), h);
		}
	}
}

void EmojiListWidget::pickerHidden() {
	_pickerSelected = v::null;
	update();
	disableScroll(false);
	setColorAllForceRippled(false);

	_lastMousePos = QCursor::pos();
	updateSelected();
}

bool EmojiListWidget::hasColorButton(int index) const {
	return (_staticCount > int(Section::People))
		&& (index == int(Section::People));
}

QRect EmojiListWidget::colorButtonRect(int index) const {
	return colorButtonRect(sectionInfo(index));
}

QRect EmojiListWidget::colorButtonRect(const SectionInfo &info) const {
	if (_mode != Mode::Full) {
		return QRect();
	}
	const auto &colorSt = st().colorAll;
	const auto buttonw = colorSt.rippleAreaPosition.x()
		+ colorSt.rippleAreaSize;
	const auto buttonh = colorSt.height;
	const auto buttonx = emojiRight() - st::emojiPanColorAllSkip - buttonw;
	const auto buttony = info.top + st::emojiPanRemoveTop;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

bool EmojiListWidget::hasRemoveButton(int index) const {
	if (index < _staticCount
		|| index >= _staticCount + _custom.size()) {
		return false;
	}
	const auto &set = _custom[index - _staticCount];
	return set.canRemove && !set.premiumRequired;
}

QRect EmojiListWidget::removeButtonRect(int index) const {
	return removeButtonRect(sectionInfo(index));
}

QRect EmojiListWidget::removeButtonRect(const SectionInfo &info) const {
	if (_mode != Mode::Full) {
		return QRect();
	}
	const auto &removeSt = st().removeSet;
	const auto buttonw = removeSt.rippleAreaPosition.x()
		+ removeSt.rippleAreaSize;
	const auto buttonh = removeSt.height;
	const auto buttonx = emojiRight() - st::emojiPanRemoveSkip - buttonw;
	const auto buttony = info.top + st::emojiPanRemoveTop;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

bool EmojiListWidget::hasAddButton(int index) const {
	if (index < _staticCount
		|| index >= _staticCount + _custom.size()) {
		return false;
	}
	const auto &set = _custom[index - _staticCount];
	return !set.canRemove && !set.premiumRequired;
}

QRect EmojiListWidget::addButtonRect(int index) const {
	return buttonRect(sectionInfo(index), _add);
}

bool EmojiListWidget::hasUnlockButton(int index) const {
	if (index < _staticCount
		|| index >= _staticCount + _custom.size()) {
		return false;
	}
	const auto &set = _custom[index - _staticCount];
	return set.premiumRequired;
}

QRect EmojiListWidget::unlockButtonRect(int index) const {
	Expects(index >= _staticCount
		&& index < _staticCount + _custom.size());

	return buttonRect(sectionInfo(index), rightButton(index));
}

bool EmojiListWidget::hasButton(int index) const {
	if (hasColorButton(index)
		|| (index >= _staticCount
			&& index < _staticCount + _custom.size())) {
		return true;
	}
	return false;
}

QRect EmojiListWidget::buttonRect(int index) const {
	return hasColorButton(index)
		? colorButtonRect(index)
		: hasRemoveButton(index)
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
	Expects(index >= _staticCount
		&& index < _staticCount + _custom.size());
	return hasAddButton(index)
		? _add
		: _custom[index - _staticCount].canRemove
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

void EmojiListWidget::colorChosen(EmojiChosen data) {
	Expects(data.emoji != nullptr && data.emoji->hasVariants());

	const auto emoji = data.emoji;
	auto &settings = Core::App().settings();
	if (const auto button = std::get_if<OverButton>(&_pickerSelected)) {
		settings.saveAllEmojiVariants(emoji);
		for (auto section = int(Section::People)
			; section < _staticCount
			; ++section) {
			for (auto &emoji : _emoji[section]) {
				emoji = settings.lookupEmojiVariant(emoji);
			}
		}
		update();
	} else {
		settings.saveEmojiVariant(emoji);

		const auto over = std::get_if<OverEmoji>(&_pickerSelected);
		if (over
			&& over->section > int(Section::Recent)
			&& over->section < _staticCount
			&& over->index < _emoji[over->section].size()) {
			_emoji[over->section][over->index] = emoji;
			rtlupdate(emojiRect(over->section, over->index));
		}
		selectEmoji(data);
	}
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
	if (_mode != Mode::Full) {
		return {};
	}
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
	if (_mode != Mode::Full) {
		return;
	}
	clearSelection();
	fillRecent();
	resizeToWidth(width());
	update();
}

void EmojiListWidget::refreshCustom() {
	if (_mode == Mode::RecentReactions) {
		return;
	}
	auto old = base::take(_custom);
	const auto session = &this->session();
	const auto premiumPossible = session->premiumPossible();
	const auto premiumMayBeBought = premiumPossible
		&& !session->premium()
		&& !_allowWithoutPremium;
	const auto owner = &session->data();
	const auto &sets = owner->stickers().sets();
	const auto push = [&](uint64 setId, bool installed) {
		auto it = sets.find(setId);
		if (it == sets.cend()
			|| it->second->stickers.isEmpty()
			|| (_mode == Mode::BackgroundEmoji
				&& !it->second->textColor())) {
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
			if (const auto sticker = document->sticker()) {
				set.push_back({
					.custom = resolveCustomEmoji(document, setId),
					.document = document,
					.emoji = Ui::Emoji::Find(sticker->alt),
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
		if (_searchCustomIds.contains(documentId)) {
			repaintCustom(SearchEmojiSectionSetId());
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

Ui::Text::CustomEmoji *EmojiListWidget::resolveCustomRecent(
		RecentEmojiId customId) {
	const auto &data = customId.data;
	if (const auto document = std::get_if<RecentEmojiDocument>(&data)) {
		return resolveCustomRecent(document->id);
	} else if (const auto emoji = std::get_if<EmojiPtr>(&data)) {
		return nullptr;
	}
	Unexpected("Custom recent emoji id.");
}

not_null<Ui::Text::CustomEmoji*> EmojiListWidget::resolveCustomRecent(
		DocumentId documentId) {
	const auto i = _customRecent.find(documentId);
	if (i != end(_customRecent)) {
		return i->second.get();
	}
	const auto j = _customEmoji.find(documentId);
	if (j != end(_customEmoji)) {
		return j->second.emoji.get();
	}
	auto repaint = repaintCallback(documentId, RecentEmojiSectionSetId());
	if (_customRecentFactory) {
		return _customRecent.emplace(
			documentId,
			_customRecentFactory(documentId, std::move(repaint))
		).first->second.get();
	}
	auto custom = session().data().customEmojiManager().create(
		documentId,
		std::move(repaint),
		Data::CustomEmojiManager::SizeTag::Large);
	return _customEmoji.emplace(
		documentId,
		CustomEmojiInstance{ .emoji = std::move(custom), .recentOnly = true }
	).first->second.emoji.get();
}

std::vector<StickerIcon> EmojiListWidget::fillIcons() {
	auto result = std::vector<StickerIcon>();
	result.reserve(2 + _custom.size());

	result.emplace_back(RecentEmojiSectionSetId());
	if (_mode != Mode::Full) {
	} else if (_custom.empty()) {
		using Section = Ui::Emoji::Section;
		for (auto i = int(Section::People); i <= int(Section::Symbols); ++i) {
			result.emplace_back(EmojiSectionSetId(Section(i)));
		}
	} else {
		result.emplace_back(AllEmojiSectionSetId());
	}
	const auto esize = StickersListFooter::IconFrameSize();
	for (const auto &custom : _custom) {
		const auto set = custom.set;
		result.emplace_back(set, custom.thumbnailDocument, esize, esize);
	}
	return result;
}

int EmojiListWidget::paintButtonGetWidth(
		QPainter &p,
		const SectionInfo &info,
		bool selected,
		QRect clip) const {
	if (!hasButton(info.section)) {
		return 0;
	}
	auto &ripple = (info.section >= _staticCount)
		? _custom[info.section - _staticCount].ripple
		: _colorAllRipple;
	const auto colorAll = hasColorButton(info.section);
	if (colorAll || hasRemoveButton(info.section)) {
		const auto rect = colorAll
			? colorButtonRect(info)
			: removeButtonRect(info);
		if (rect.isEmpty()) {
			return 0;
		} else if (rect.intersects(clip)) {
			const auto &bst = colorAll ? st().colorAll : st().removeSet;
			if (colorAll && _colorAllRippleForced) {
				selected = true;
			}
			if (ripple) {
				ripple->paint(
					p,
					rect.x() + bst.rippleAreaPosition.x(),
					rect.y() + bst.rippleAreaPosition.y(),
					width());
				if (ripple->empty()) {
					ripple.reset();
				}
			}
			const auto &icon = selected ? bst.iconOver : bst.icon;
			icon.paint(
				p,
				(rect.topLeft()
					+ QPoint(
						rect.width() - icon.width(),
						rect.height() - icon.height()) / 2),
				width());
		}
		return emojiRight() - rect.x();
	}
	const auto canAdd = hasAddButton(info.section);
	const auto &button = rightButton(info.section);
	const auto rect = buttonRect(info, button);
	p.drawImage(rect.topLeft(), selected ? button.backOver : button.back);
	if (ripple) {
		const auto color = QColor(0, 0, 0, 36);
		ripple->paint(p, rect.x(), rect.y(), width(), &color);
		if (ripple->empty()) {
			ripple.reset();
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
	return emojiRight() - rect.x();
}

void EmojiListWidget::paintEmptySearchResults(Painter &p) {
	Inner::paintEmptySearchResults(
		p,
		st::emojiEmpty,
		tr::lng_emoji_nothing_found(tr::now));
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
		} else if (_features.openStickerSets
			&& section >= _staticCount
			&& _mode == Mode::Full) {
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
		Assert(hasColorButton(button->section)
			|| (button->section >= _staticCount
				&& button->section < _staticCount + _custom.size()));
		auto &ripple = (button->section >= _staticCount)
			? _custom[button->section - _staticCount].ripple
			: _colorAllRipple;
		if (ripple) {
			ripple->lastStop();
		}
	}
	_pressed = newPressed;
	if (auto button = std::get_if<OverButton>(&_pressed)) {
		Assert(hasColorButton(button->section)
			|| (button->section >= _staticCount
				&& button->section < _staticCount + _custom.size()));
		auto &ripple = (button->section >= _staticCount)
			? _custom[button->section - _staticCount].ripple
			: _colorAllRipple;
		if (!ripple) {
			ripple = createButtonRipple(button->section);
		}
		ripple->add(mapFromGlobal(QCursor::pos()) - buttonRippleTopLeft(button->section));
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
	Expects(hasButton(section));

	const auto colorAll = hasColorButton(section);
	const auto remove = hasRemoveButton(section);
	const auto &staticSt = colorAll ? st().colorAll : st().removeSet;
	const auto &st = (colorAll || remove)
		? staticSt.ripple
		: st::emojiPanButton.ripple;
	auto mask = (colorAll || remove)
		? Ui::RippleAnimation::EllipseMask(QSize(
			staticSt.rippleAreaSize,
			staticSt.rippleAreaSize))
		: rightButton(section).rippleMask;
	return std::make_unique<Ui::RippleAnimation>(
		st,
		std::move(mask),
		[this, section] { rtlupdate(buttonRect(section)); });
}

QPoint EmojiListWidget::buttonRippleTopLeft(int section) const {
	Expects(hasButton(section));

	return myrtlrect(buttonRect(section)).topLeft()
		+ (hasColorButton(section)
			? st().colorAll.rippleAreaPosition
			: hasRemoveButton(section)
			? st().removeSet.rippleAreaPosition
			: QPoint());
}

PowerSaving::Flag EmojiListWidget::powerSavingFlag() const {
	const auto reactions = (_mode == Mode::FullReactions)
		|| (_mode == Mode::RecentReactions);
	return reactions
		? PowerSaving::kEmojiReactions
		: PowerSaving::kEmojiPanel;
}

void EmojiListWidget::refreshEmoji() {
	refreshRecent();
	refreshCustom();
}

void EmojiListWidget::showSet(uint64 setId) {
	clearSelection();
	if (_search && _searchMode) {
		_search->cancel();
		applyNextSearchQuery();
	}

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
	Expects(_searchMode
		|| section < _staticCount
		|| (section - _staticCount) < _custom.size());

	return _searchMode
		? SearchEmojiSectionSetId()
		: (section < _staticCount)
		? EmojiSectionSetId(static_cast<Section>(section))
		: _custom[section - _staticCount].id;
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
