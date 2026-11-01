/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_gifts_panel.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/effects/peer_qr_panel.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/top_background_gradient.h"

#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kColumns = 3;
constexpr auto kWideTileRatio = 1.28;
constexpr auto kTileModelRatio = 0.82;
constexpr auto kPatternBlockRatio = 5. / 3.;
constexpr auto kPatternBlockShift = 1. / 3.;
constexpr auto kRingMinScale = 0.9;
constexpr auto kEmptyTextOpacity = 0.7;

enum class Layout {
	Loading,
	Empty,
	One,
	Two,
	Three,
	Grid,
};

struct Tile {
	PeerQrSwatch swatch;
	std::unique_ptr<Text::CustomEmoji> model;
	std::unique_ptr<Text::CustomEmoji> pattern;
	QImage background;
	base::flat_map<float64, QImage> patternCache;
	bool patterned = false;
	bool hairline = false;
};

[[nodiscard]] Layout ChooseLayout(int count, bool complete) {
	if (!count) {
		return Layout::Empty;
	} else if (!complete || count > kColumns) {
		return Layout::Grid;
	}
	return (count == 1)
		? Layout::One
		: (count == 2)
		? Layout::Two
		: Layout::Three;
}

[[nodiscard]] bool IsWide(Layout layout) {
	return (layout == Layout::One) || (layout == Layout::Two);
}

[[nodiscard]] float64 SquareSize(int width) {
	const auto inner = width - 2 * st::peerQrPanelPadding;
	const auto skip = st::peerQrGiftSkip;
	return std::max((inner - (kColumns - 1) * skip) / float64(kColumns), 0.);
}

[[nodiscard]] QRect SnapRect(QRectF rect) {
	const auto left = int(std::round(rect.x()));
	const auto top = int(std::round(rect.y()));
	return QRect(
		left,
		top,
		int(std::round(rect.x() + rect.width())) - left,
		int(std::round(rect.y() + rect.height())) - top);
}

[[nodiscard]] QImage PrepareBackground(
		const PeerQrAppearance &appearance,
		QSize size) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		size * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		const auto rect = QRectF(QPointF(), QSizeF(size));
		auto gradient = QRadialGradient(
			rect.center(),
			std::max(rect.width(), rect.height()) / 2.);
		gradient.setStops({
			{ 0., appearance.center },
			{ 1., appearance.edge },
		});
		const auto radius = float64(st::peerQrGiftRadius);
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRoundedRect(rect, radius, radius);
	}
	return result;
}

void PaintPattern(
		QImage &background,
		not_null<Text::CustomEmoji*> pattern,
		base::flat_map<float64, QImage> &cache,
		const QColor &color) {
	const auto ratio = background.devicePixelRatio();
	const auto width = int(std::round(background.width() / ratio));
	const auto height = int(std::round(background.height() / ratio));
	const auto block = height * kPatternBlockRatio;
	const auto count = int(std::ceil(width / block));
	const auto radius = float64(st::peerQrGiftRadius);
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(0., 0., width, height), radius, radius);
	auto p = QPainter(&background);
	auto hq = PainterHighQualityEnabler(p);
	p.setClipPath(clip);
	for (auto k = 0; k != count; ++k) {
		const auto left = k * block - height * kPatternBlockShift;
		PaintBgPoints(
			p,
			PatternBgPointsSmall(),
			cache,
			pattern,
			color,
			SnapRect(QRectF(left, 0., block, height)));
	}
}

} // namespace

class PeerQrGiftsPanel::Inner final : public RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<PeerQrGiftsPanel*> panel,
		not_null<PeerQrPanel*> container,
		rpl::producer<QString> emptyAbout);

	[[nodiscard]] bool fitsContent() const;
	[[nodiscard]] int contentHeight(int width) const;

	void setSource(PeerQrGiftSource source);
	void setColors(const PeerQrColors &colors);
	void setGifts(std::vector<PeerQrSwatch> gifts, bool complete);
	void setBrowseShown(bool shown);
	void setSelected(std::optional<PeerQrThemeKey> key, anim::type animated);
	void scrollToSelected();
	void checkOpenHeight();
	void checkLoadMore();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void setupEmpty();
	void layoutEmpty(int width);
	void refreshEmptyShown();
	void startSticker();
	[[nodiscard]] int indexOf(
		const std::optional<PeerQrThemeKey> &key) const;
	[[nodiscard]] QRect tileRect(int index) const;
	[[nodiscard]] int tileAt(QPoint point) const;
	void setOver(int index);
	void ensureArt(Tile &tile);
	void validateBackground(Tile &tile, QSize size);
	void paintTile(
		QPainter &p,
		int index,
		QRect rect,
		crl::time now,
		bool paused);
	void paintRing(QPainter &p, int index, QRect rect) const;
	void releaseFarArt();

	const not_null<PeerQrGiftsPanel*> _panel;
	const not_null<PeerQrPanel*> _container;
	const not_null<RpWidget*> _sticker;
	const not_null<FlatLabel*> _title;
	const not_null<FlatLabel*> _text;
	const not_null<RoundButton*> _browse;
	std::unique_ptr<Lottie::Icon> _icon;
	PeerQrGiftSource _source;
	std::vector<Tile> _tiles;
	std::optional<PeerQrThemeKey> _selected;
	PeerQrSelectionRing _ring;
	Layout _layout = Layout::Loading;
	float64 _size = 0.;
	int _emptyHeight = 0;
	int _openHeightState = -1;
	int _over = -1;
	int _pressed = -1;
	bool _complete = false;
	bool _browseShown = true;

};

PeerQrGiftsPanel::Inner::Inner(
	QWidget *parent,
	not_null<PeerQrGiftsPanel*> panel,
	not_null<PeerQrPanel*> container,
	rpl::producer<QString> emptyAbout)
: RpWidget(parent)
, _panel(panel)
, _container(container)
, _sticker(CreateChild<RpWidget>(this))
, _title(CreateChild<FlatLabel>(
	this,
	tr::lng_qr_box_gifts_empty_title(),
	st::peerQrGiftsEmptyTitle))
, _text(CreateChild<FlatLabel>(
	this,
	std::move(emptyAbout),
	st::peerQrGiftsEmptyText))
, _browse(CreateChild<RoundButton>(
	this,
	tr::lng_qr_box_gifts_browse(),
	st::peerQrGiftsBrowse)) {
	setMouseTracking(true);
	setupEmpty();
}

void PeerQrGiftsPanel::Inner::setupEmpty() {
	const auto size = st::peerQrGiftsEmptySticker;
	_sticker->resize(size, size);
	_sticker->paintRequest() | rpl::on_next([=] {
		if (!_icon) {
			return;
		}
		auto p = QPainter(_sticker);
		_icon->paint(p, 0, 0);
		if (!_icon->animating() && _icon->frameIndex() > 0) {
			startSticker();
		}
	}, _sticker->lifetime());

	_text->setTryMakeSimilarLines(true);
	_browse->setTextTransform(RoundButtonTextTransform::NoTransform);
	_browse->setClickedCallback([=] {
		_panel->_browse.fire({});
	});
	refreshEmptyShown();
}

void PeerQrGiftsPanel::Inner::layoutEmpty(int width) {
	const auto &padding = st::peerQrGiftsEmptyPadding;
	const auto available = width - padding.left() - padding.right();
	auto top = padding.top();
	_sticker->move((width - _sticker->width()) / 2, top);
	top += _sticker->height() + st::peerQrGiftsEmptyTitleSkip;
	_title->resizeToWidth(std::max(available, 0));
	_title->move((width - _title->width()) / 2, top);
	top += _title->height() + st::peerQrGiftsEmptyTextSkip;
	_text->resizeToWidth(std::max(
		std::min(
			width - 2 * st::peerQrPanelPadding,
			st::peerQrGiftsEmptyTextWidth),
		0));
	_text->move((width - _text->width()) / 2, top);
	top += _text->height();
	if (_browseShown) {
		top += st::peerQrGiftsEmptyButtonSkip;
		_browse->move((width - _browse->width()) / 2, top);
		top += _browse->height();
	}
	_emptyHeight = top + padding.bottom();
}

void PeerQrGiftsPanel::Inner::refreshEmptyShown() {
	const auto empty = (_layout == Layout::Empty);
	_sticker->setVisible(empty);
	_title->setVisible(empty);
	_text->setVisible(empty);
	_browse->setVisible(empty && _browseShown);
}

void PeerQrGiftsPanel::Inner::startSticker() {
	if (!_icon) {
		const auto size = st::peerQrGiftsEmptySticker;
		_icon = Lottie::MakeIcon({
			.name = u"dog_empty"_q,
			.sizeOverride = QSize(size, size),
			.limitFps = true,
		});
	}
	_icon->animate([=] {
		_sticker->update();
	}, 0, _icon->framesCount() - 1);
}

bool PeerQrGiftsPanel::Inner::fitsContent() const {
	return (_layout != Layout::Loading) && (_layout != Layout::Grid);
}

int PeerQrGiftsPanel::Inner::contentHeight(int width) const {
	if (_layout == Layout::Loading) {
		return 0;
	} else if (_layout == Layout::Empty) {
		return _emptyHeight;
	}
	const auto size = SquareSize(width);
	const auto rows = (int(_tiles.size()) + kColumns - 1) / kColumns;
	const auto content = IsWide(_layout)
		? (size * kWideTileRatio)
		: (rows * size + (rows - 1) * st::peerQrGiftSkip);
	return int(std::ceil(2 * st::peerQrPanelPadding + content));
}

void PeerQrGiftsPanel::Inner::setSource(PeerQrGiftSource source) {
	_source = std::move(source);
	update();
}

void PeerQrGiftsPanel::Inner::setColors(const PeerQrColors &colors) {
	const auto white = QColor(255, 255, 255);
	_title->setTextColorOverride(colors.foreground);
	_text->setTextColorOverride(
		anim::with_alpha(colors.foreground, kEmptyTextOpacity));
	_browse->setBrushOverride(colors.foreground);
	_browse->setTextFgOverride((colors.foreground == white)
		? colors.ink
		: white);
}

void PeerQrGiftsPanel::Inner::setGifts(
		std::vector<PeerQrSwatch> gifts,
		bool complete) {
	const auto same = (_layout != Layout::Loading)
		&& (_complete == complete)
		&& ranges::equal(
			gifts,
			_tiles,
			ranges::equal_to(),
			ranges::identity(),
			&Tile::swatch);
	if (same) {
		return;
	}
	auto tiles = std::vector<Tile>();
	tiles.reserve(gifts.size());
	for (auto &swatch : gifts) {
		const auto i = ranges::find(
			_tiles,
			swatch.key.gift,
			[](const Tile &tile) { return tile.swatch.key.gift; });
		auto tile = Tile();
		if (i != end(_tiles) && i->swatch == swatch) {
			tile = std::move(*i);
		} else {
			tile.swatch = std::move(swatch);
			const auto edge = tile.swatch.appearance.edge;
			tile.hairline = PeerQrRingNeedsHairline(edge);
		}
		tiles.push_back(std::move(tile));
	}
	_tiles = std::move(tiles);
	_complete = complete;
	const auto layout = ChooseLayout(int(_tiles.size()), complete);
	const auto entered = (layout == Layout::Empty)
		&& (_layout != Layout::Empty);
	_layout = layout;
	refreshEmptyShown();
	if (entered) {
		startSticker();
	}
	_ring.reset(indexOf(_selected));
	_pressed = -1;
	setOver(-1);
	resizeToWidth(width());
	update();
	checkOpenHeight();
	checkLoadMore();
}

void PeerQrGiftsPanel::Inner::setBrowseShown(bool shown) {
	if (_browseShown == shown) {
		return;
	}
	_browseShown = shown;
	refreshEmptyShown();
	resizeToWidth(width());
	checkOpenHeight();
}

void PeerQrGiftsPanel::Inner::setSelected(
		std::optional<PeerQrThemeKey> key,
		anim::type animated) {
	if (_selected == key) {
		return;
	}
	_selected = key;
	_ring.select(indexOf(key), animated, !_container->isHidden(), [=] {
		update();
	});
}

void PeerQrGiftsPanel::Inner::scrollToSelected() {
	const auto index = _ring.selectedIndex();
	if (index < 0) {
		return;
	}
	const auto rect = tileRect(index);
	const auto padding = st::peerQrPanelPadding;
	_panel->_scroll->scrollToY(
		rect.y() - padding,
		rect.y() + rect.height() + padding);
}

void PeerQrGiftsPanel::Inner::checkOpenHeight() {
	const auto state = fitsContent() ? contentHeight(width()) : -1;
	if (_openHeightState != state) {
		_openHeightState = state;
		_panel->_openHeightChanges.fire({});
	}
}

void PeerQrGiftsPanel::Inner::checkLoadMore() {
	if (_layout != Layout::Grid || _complete) {
		return;
	}
	const auto scroll = _panel->_scroll.get();
	const auto pitch = _size + st::peerQrGiftSkip;
	if (scroll->scrollTop() + scroll->height() >= height() - pitch) {
		_panel->_loadMore.fire({});
	}
}

int PeerQrGiftsPanel::Inner::resizeGetHeight(int newWidth) {
	_size = SquareSize(newWidth);
	layoutEmpty(newWidth);
	return contentHeight(newWidth);
}

void PeerQrGiftsPanel::Inner::paintEvent(QPaintEvent *e) {
	if (_tiles.empty()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto clip = e->rect();
	const auto margin = int(std::ceil(_size)) + st::peerQrGiftSkip;
	const auto nearby = clip.marginsAdded({ 0, margin, 0, margin });
	const auto now = crl::now();
	const auto paused = PowerSaving::On(PowerSaving::kStickersPanel)
		|| !_container->phase().shown;
	for (auto i = 0, count = int(_tiles.size()); i != count; ++i) {
		const auto rect = tileRect(i);
		if (!rect.intersects(nearby)) {
			continue;
		}
		ensureArt(_tiles[i]);
		if (rect.intersects(clip)) {
			paintTile(p, i, rect, now, paused);
		}
	}
	releaseFarArt();
}

void PeerQrGiftsPanel::Inner::paintTile(
		QPainter &p,
		int index,
		QRect rect,
		crl::time now,
		bool paused) {
	if (rect.isEmpty()) {
		return;
	}
	auto &tile = _tiles[index];
	validateBackground(tile, rect.size());
	p.drawImage(rect.topLeft(), tile.background);
	if (tile.model) {
		const auto frame = tile.model->width();
		if (frame > 0) {
			const auto box = kTileModelRatio * rect.height();
			const auto scale = box / frame;
			p.save();
			p.translate(QRectF(rect).center() - QPointF(box, box) / 2.);
			p.scale(scale, scale);
			tile.model->paint(p, {
				.textColor = QColor(255, 255, 255),
				.now = now,
				.paused = paused,
			});
			p.restore();
		}
	}
	paintRing(p, index, rect);
}

void PeerQrGiftsPanel::Inner::paintRing(
		QPainter &p,
		int index,
		QRect rect) const {
	_ring.paint(
		p,
		index,
		rect,
		kRingMinScale,
		_tiles[index].hairline,
		float64(st::peerQrGiftRadius));
}

void PeerQrGiftsPanel::Inner::releaseFarArt() {
	const auto scroll = _panel->_scroll.get();
	const auto margin = 2 * int(std::ceil(_size + st::peerQrGiftSkip));
	const auto from = scroll->scrollTop() - margin;
	const auto till = scroll->scrollTop() + scroll->height() + margin;
	for (auto i = 0, count = int(_tiles.size()); i != count; ++i) {
		auto &tile = _tiles[i];
		const auto rect = tileRect(i);
		if (rect.y() + rect.height() < from || rect.y() > till) {
			tile.model = nullptr;
			tile.pattern = nullptr;
			tile.background = QImage();
			tile.patternCache.clear();
			tile.patterned = false;
		}
	}
}

void PeerQrGiftsPanel::Inner::ensureArt(Tile &tile) {
	const auto &look = tile.swatch.appearance.gift;
	const auto repaint = [=] { update(); };
	if (!tile.model && _source.model && !look.model.isEmpty()) {
		tile.model = _source.model(look.model, repaint);
	}
	if (!tile.pattern && _source.pattern && !look.pattern.isEmpty()) {
		tile.pattern = _source.pattern(look.pattern, repaint);
	}
}

void PeerQrGiftsPanel::Inner::validateBackground(Tile &tile, QSize size) {
	const auto ratio = style::DevicePixelRatio();
	if (tile.background.size() != size * ratio
		|| tile.background.devicePixelRatio() != ratio) {
		tile.background = PrepareBackground(tile.swatch.appearance, size);
		tile.patterned = false;
	}
	if (tile.patterned || !tile.pattern || !tile.pattern->ready()) {
		return;
	}
	tile.patterned = true;
	PaintPattern(
		tile.background,
		tile.pattern.get(),
		tile.patternCache,
		tile.swatch.appearance.gift.patternColor);
}

void PeerQrGiftsPanel::Inner::mouseMoveEvent(QMouseEvent *e) {
	setOver(_container->phase().shown ? tileAt(e->pos()) : -1);
}

void PeerQrGiftsPanel::Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && _container->phase().shown) {
		_pressed = tileAt(e->pos());
	}
}

void PeerQrGiftsPanel::Inner::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0
		|| e->button() != Qt::LeftButton
		|| !_container->phase().shown
		|| tileAt(e->pos()) != pressed) {
		return;
	}
	_panel->_picks.fire_copy(_tiles[pressed].swatch);
}

void PeerQrGiftsPanel::Inner::leaveEventHook(QEvent *e) {
	setOver(-1);
}

int PeerQrGiftsPanel::Inner::indexOf(
		const std::optional<PeerQrThemeKey> &key) const {
	if (!key) {
		return -1;
	}
	const auto i = ranges::find(
		_tiles,
		key->gift,
		[](const Tile &tile) { return tile.swatch.key.gift; });
	return (i != end(_tiles)) ? int(i - begin(_tiles)) : -1;
}

QRect PeerQrGiftsPanel::Inner::tileRect(int index) const {
	const auto padding = float64(st::peerQrPanelPadding);
	const auto skip = float64(st::peerQrGiftSkip);
	if (IsWide(_layout)) {
		const auto inner = width() - 2 * padding;
		const auto tile = (_layout == Layout::One)
			? inner
			: ((inner - skip) / 2.);
		return SnapRect(QRectF(
			padding + index * (tile + skip),
			padding,
			tile,
			_size * kWideTileRatio));
	}
	const auto pitch = _size + skip;
	return SnapRect(QRectF(
		padding + (index % kColumns) * pitch,
		padding + (index / kColumns) * pitch,
		_size,
		_size));
}

int PeerQrGiftsPanel::Inner::tileAt(QPoint point) const {
	for (auto i = 0, count = int(_tiles.size()); i != count; ++i) {
		if (tileRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

void PeerQrGiftsPanel::Inner::setOver(int index) {
	if (_over == index) {
		return;
	}
	_over = index;
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
}

PeerQrGiftsPanel::PeerQrGiftsPanel(
	not_null<PeerQrPanel*> panel,
	rpl::producer<QString> emptyAbout)
: RpWidget(panel)
, _scroll(CreateChild<ElasticScroll>(this, st::peerQrPanelScroll))
, _inner(_scroll->setOwnedWidget(object_ptr<Inner>(
	_scroll.get(),
	this,
	panel,
	std::move(emptyAbout))).data()) {
	_scroll->scrollTopValue() | rpl::on_next([=] {
		_inner->checkLoadMore();
	}, lifetime());
}

PeerQrGiftsPanel::~PeerQrGiftsPanel() = default;

int PeerQrGiftsPanel::openHeight(int width) const {
	const auto cap = PeerQrColorsPanel::HeightForWidth(width);
	if (!_inner->fitsContent()) {
		return cap;
	}
	const auto line = st::lineWidth;
	return std::min(_inner->contentHeight(width - 2 * line) + 2 * line, cap);
}

void PeerQrGiftsPanel::setSource(PeerQrGiftSource source) {
	_inner->setSource(std::move(source));
}

void PeerQrGiftsPanel::setColors(const PeerQrColors &colors) {
	_inner->setColors(colors);
}

void PeerQrGiftsPanel::setGifts(
		std::vector<PeerQrSwatch> gifts,
		bool complete) {
	_inner->setGifts(std::move(gifts), complete);
}

void PeerQrGiftsPanel::setBrowseShown(bool shown) {
	_inner->setBrowseShown(shown);
}

void PeerQrGiftsPanel::setSelected(
		std::optional<PeerQrThemeKey> key,
		anim::type animated) {
	_inner->setSelected(key, animated);
}

void PeerQrGiftsPanel::scrollToSelected() {
	_inner->scrollToSelected();
}

rpl::producer<PeerQrSwatch> PeerQrGiftsPanel::picks() const {
	return _picks.events();
}

rpl::producer<> PeerQrGiftsPanel::loadMoreRequests() const {
	return _loadMore.events();
}

rpl::producer<> PeerQrGiftsPanel::browseRequests() const {
	return _browse.events();
}

rpl::producer<> PeerQrGiftsPanel::openHeightChanges() const {
	return _openHeightChanges.events();
}

void PeerQrGiftsPanel::resizeEvent(QResizeEvent *e) {
	_scroll->setGeometry(rect());
	_inner->resizeToWidth(width());
	_inner->checkOpenHeight();
	_inner->checkLoadMore();
}

} // namespace Ui
