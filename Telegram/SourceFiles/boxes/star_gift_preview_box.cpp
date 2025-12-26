/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_preview_box.h"

#include "base/random.h"
#include "boxes/star_gift_box.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/top_background_gradient.h"
#include "settings/settings_credits_graphics.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

using Tab = Data::GiftAttributeIdType;
using GiftModel = Data::UniqueGiftModel;
using GiftPattern = Data::UniqueGiftPattern;
using GiftBackdrop = Data::UniqueGiftBackdrop;
constexpr auto kSwitchTimeout = 3 * crl::time(1000);
constexpr auto kGiftsPerRow = 3;

struct AttributeDescriptor
	: std::variant<GiftModel, GiftPattern, GiftBackdrop> {

	friend inline bool operator==(
		const AttributeDescriptor &,
		const AttributeDescriptor &) = default;
};

[[nodiscard]] DocumentData *Sticker(const AttributeDescriptor &value) {
	using namespace Data;
	if (const auto pattern = std::get_if<UniqueGiftPattern>(&value)) {
		return pattern->document;
	} else if (const auto model = std::get_if<UniqueGiftModel>(&value)) {
		return model->document;
	}
	return nullptr;
}

struct BackdropPlayers {
	HistoryView::StickerPlayer *now = nullptr;
	HistoryView::StickerPlayer *next = nullptr;
	float64 progress = 0.;
};

struct PatternEmoji {
	std::shared_ptr<Text::CustomEmoji> now;
	std::shared_ptr<Text::CustomEmoji> next;
	float64 progress = 0.;
};

class AttributeDelegate {
public:
	[[nodiscard]] virtual QSize buttonSize() = 0;
	[[nodiscard]] virtual QMargins buttonExtend() const = 0;
	[[nodiscard]] virtual QImage background() = 0;
	[[nodiscard]] virtual QImage cachePatternBackground(
		int width,
		int height,
		QMargins extend) = 0;
	[[nodiscard]] virtual QColor patternColor() = 0;
	[[nodiscard]] virtual BackdropPlayers backdropPlayers() = 0;
	[[nodiscard]] virtual PatternEmoji patternEmoji() = 0;
};

class AttributeButton final : public Ui::AbstractButton {
public:
	AttributeButton(
		QWidget *parent,
		not_null<AttributeDelegate*> delegate,
		const AttributeDescriptor &descriptor);

	void toggleSelected(bool selected, anim::type animated);

	void setDescriptor(const AttributeDescriptor &descriptor);
	void setGeometry(QRect inner, QMargins extend);

private:
	struct PatternCache {
		base::flat_map<float64, QImage> map;
		std::shared_ptr<Text::CustomEmoji> emoji;
	};

	void paintEvent(QPaintEvent *e) override;
	void paintBackground(QPainter &p, const QImage &background);

	void setup();
	void validatePatternCache();
	void setDocument(not_null<DocumentData*> document);

	const not_null<AttributeDelegate*> _delegate;
	QImage _hiddenBgCache;
	AttributeDescriptor _descriptor;
	Ui::Text::String _name;
	Ui::Text::String _percent;
	QImage _backgroundCache;
	PatternCache _patternNow;
	PatternCache _patternNext;
	QImage _patternFrame;
	QColor _patternColor;
	Ui::Animations::Simple _selectedAnimation;
	bool _selected : 1 = false;
	bool _patterned : 1 = false;

	QMargins _extend;

	DocumentData *_document = nullptr;
	DocumentData *_playerDocument = nullptr;
	std::unique_ptr<HistoryView::StickerPlayer> _player;
	rpl::lifetime _mediaLifetime;

};

class Delegate final : public AttributeDelegate {
public:
	explicit Delegate(Fn<void()> fullUpdate);

	Delegate(Delegate &&other) = default;
	~Delegate() = default;

	void update(
		const std::optional<Data::UniqueGift> &now,
		const std::optional<Data::UniqueGift> &next,
		float64 progress);

	[[nodiscard]] QSize buttonSize() override;
	[[nodiscard]] QMargins buttonExtend() const override;
	[[nodiscard]] QImage background() override;
	[[nodiscard]] QImage cachePatternBackground(
		int width,
		int height,
		QMargins extend) override;
	[[nodiscard]] QColor patternColor() override;
	[[nodiscard]] BackdropPlayers backdropPlayers() override;
	[[nodiscard]] PatternEmoji patternEmoji() override;

private:
	struct Sticker {
		DocumentData *document = nullptr;
		DocumentData *playerDocument = nullptr;
		std::unique_ptr<HistoryView::StickerPlayer> player;
		rpl::lifetime mediaLifetime;
	};
	struct Emoji {
		DocumentData *document;
		std::shared_ptr<Text::CustomEmoji> custom;
	};

	Fn<void()> _fullUpdate;
	QSize _single;
	QImage _bg;

	GiftBackdrop _nowBackdrop;
	GiftBackdrop _nextBackdrop;

	Sticker _nowModel;
	Sticker _nextModel;

	Emoji _nowPatternEmoji;
	Emoji _nextPatternEmoji;

	float64 _progress = 0.;

	GiftBackdrop _backdrop;
	QImage _backdropCache;
	bool _backdropDirty = false;

};

struct Selection {
	int model = -1;
	int pattern = -1;
	int backdrop = -1;

	friend inline bool operator==(Selection, Selection) = default;
};

class AttributesList final : public Ui::BoxContentDivider {
public:
	AttributesList(
		QWidget *parent,
		not_null<Delegate*> delegate,
		not_null<const Data::UniqueGiftAttributes*> attributes,
		rpl::producer<Tab> tab,
		Selection initialSelection);

	[[nodiscard]] rpl::producer<Selection> selected() const;

private:
	struct Entry {
		AttributeDescriptor descriptor;
		Selection value;
	};
	struct Entries {
		std::vector<Entry> list;
	};
	struct View {
		std::unique_ptr<AttributeButton> button;
		DocumentData *document = nullptr;
		int index = 0;
	};

	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;

	void fill();
	void refreshAbout();
	void refreshButtons();
	void validateButtons();

	int resizeGetHeight(int width) override;
	void clicked(int index);

	const not_null<Delegate*> _delegate;
	const not_null<const Data::UniqueGiftAttributes*> _attributes;
	rpl::variable<Tab> _tab;
	rpl::variable<Selection> _selected;

	std::unique_ptr<Ui::RpWidget> _about;

	Entries _models;
	Entries _patterns;
	Entries _backdrops;
	not_null<Entries*> _entries;
	not_null<std::vector<Entry>*> _list;

	std::vector<View> _views;
	int _viewsForWidth = 0;
	int _viewsFromRow = 0;
	int _viewsTillRow = 0;

	QSize _singleMin;
	QSize _single;
	int _perRow = 0;
	int _visibleFrom = 0;
	int _visibleTill = 0;

};

void CacheBackdropBackground(
		not_null<GiftBackdrop*> backdrop,
		int width,
		int height,
		QMargins extend,
		QImage &cache,
		bool force = false) {
	const auto outer = QRect(0, 0, width, height);
	const auto inner = outer.marginsRemoved(
		extend
	).translated(-extend.left(), -extend.top());
	const auto ratio = style::DevicePixelRatio();
	if (cache.size() != inner.size() * ratio) {
		cache = QImage(
			inner.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(ratio);
		force = true;
	}
	if (force) {
		cache.fill(Qt::transparent);

		const auto radius = st::giftBoxGiftRadius;
		auto p = QPainter(&cache);
		auto hq = PainterHighQualityEnabler(p);
		auto gradient = QRadialGradient(inner.center(), inner.width() / 2);
		gradient.setStops({
			{ 0., backdrop->centerColor },
			{ 1., backdrop->edgeColor },
		});
		p.setBrush(gradient);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(inner, radius, radius);
	}
}

AttributeButton::AttributeButton(
	QWidget *parent,
	not_null<AttributeDelegate*> delegate,
	const AttributeDescriptor &descriptor)
: AbstractButton(parent)
, _delegate(delegate)
, _descriptor(descriptor) {
	setup();
}

void AttributeButton::setup() {
	v::match(_descriptor, [&](const auto &data) {
		_name.setText(st::uniqueAttributeName, data.name);
		_percent.setText(
			st::uniqueAttributePercent,
			QString::number(data.rarityPermille / 10.) + '%');
	});

	v::match(_descriptor, [&](const GiftBackdrop &data) {
		const auto destroyed = base::take(_player);
		_document = nullptr;
		_playerDocument = nullptr;
		_mediaLifetime.destroy();
		_patternNow = {};
		_patternNext = {};
		_patternFrame = QImage();
	}, [&](const GiftModel &data) {
		setDocument(data.document);
		_patternNow = {};
		_patternNext = {};
		_patternFrame = QImage();
	}, [&](const GiftPattern &data) {
		const auto document = data.document;
		if (_document != document) {
			setDocument(document);
			const auto owner = &document->owner();
			_patternNow.emoji = owner->customEmojiManager().create(
				document,
				[=] { update(); },
				Data::CustomEmojiSizeTag::Large);
			_patternNow.map.clear();
			_patternColor = QColor();
		}
	});
}

void AttributeButton::setDescriptor(const AttributeDescriptor &descriptor) {
	if (_descriptor == descriptor) {
		return;
	}
	_descriptor = descriptor;
	_backgroundCache = QImage();
	setup();
	update();
}

void AttributeButton::setDocument(not_null<DocumentData*> document) {
	if (_document == document) {
		return;
	}
	_document = document;

	const auto media = document->createMediaView();
	media->checkStickerLarge();
	media->goodThumbnailWanted();

	const auto destroyed = base::take(_player);
	_playerDocument = nullptr;
	_mediaLifetime = rpl::single() | rpl::then(
		document->session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return media->loaded();
	}) | rpl::on_next([=] {
		_mediaLifetime.destroy();

		auto result = std::unique_ptr<HistoryView::StickerPlayer>();
		const auto sticker = document->sticker();
		if (sticker->isLottie()) {
			result = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					media.get(),
					ChatHelpers::StickerLottieSize::InlineResults,
					st::uniqueAttributeStickerSize,
					Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			result = std::make_unique<HistoryView::WebmPlayer>(
				media->owner()->location(),
				media->bytes(),
				st::uniqueAttributeStickerSize);
		} else {
			result = std::make_unique<HistoryView::StaticStickerPlayer>(
				media->owner()->location(),
				media->bytes(),
				st::uniqueAttributeStickerSize);
		}
		result->setRepaintCallback([=] { update(); });
		_playerDocument = media->owner();
		_player = std::move(result);
		update();
	});
	if (_playerDocument) {
		_mediaLifetime.destroy();
	}
}

void AttributeButton::setGeometry(QRect inner, QMargins extend) {
	_extend = extend;
	AbstractButton::setGeometry(inner.marginsAdded(extend));
}

void AttributeButton::toggleSelected(bool selected, anim::type animated) {
	if (_selected == selected) {
		if (animated == anim::type::instant) {
			_selectedAnimation.stop();
		}
		return;
	}

	const auto duration = st::defaultRoundCheckbox.duration;
	_selected = selected;
	if (animated == anim::type::instant) {
		_selectedAnimation.stop();
		return;
	}
	_selectedAnimation.start([=] {
		update();
	}, selected ? 0. : 1., selected ? 1. : 0., duration, anim::easeOutCirc);
}

void AttributeButton::paintBackground(
		QPainter &p,
		const QImage &background) {
	const auto removed = QMargins();
	const auto x = removed.left();
	const auto y = removed.top();
	const auto width = this->width() - x - removed.right();
	const auto height = this->height() - y - removed.bottom();
	const auto dpr = int(background.devicePixelRatio());
	const auto bwidth = background.width() / dpr;
	const auto bheight = background.height() / dpr;
	const auto fillRow = [&](int yfrom, int ytill, int bfrom) {
		const auto fill = [&](int xto, int wto, int xfrom, int wfrom = 0) {
			const auto fheight = ytill - yfrom;
			p.drawImage(
				QRect(x + xto, y + yfrom, wto, fheight),
				background,
				QRect(
					QPoint(xfrom, bfrom) * dpr,
					QSize((wfrom ? wfrom : wto), fheight) * dpr));
		};
		if (width < bwidth) {
			const auto xhalf = width / 2;
			fill(0, xhalf, 0);
			fill(xhalf, width - xhalf, bwidth - (width - xhalf));
		} else if (width == bwidth) {
			fill(0, width, 0);
		} else {
			const auto half = bwidth / (2 * dpr);
			fill(0, half, 0);
			fill(width - half, half, bwidth - half);
			fill(half, width - 2 * half, half, 1);
		}
	};
	if (height < bheight) {
		fillRow(0, height / 2, 0);
		fillRow(height / 2, height, bheight - (height - (height / 2)));
	} else {
		fillRow(0, height, 0);
	}

	auto hq = PainterHighQualityEnabler(p);
	const auto progress = _selectedAnimation.value(_selected ? 1. : 0.);
	if (progress < 0.01) {
		return;
	}
	const auto pwidth = progress * st::defaultRoundCheckbox.width;
	p.setPen(QPen(st::defaultRoundCheckbox.bgActive->c, pwidth));
	p.setBrush(Qt::NoBrush);
	const auto rounded = rect().marginsRemoved(_extend);
	const auto phalf = pwidth / 2.;
	const auto extended = QRectF(rounded).marginsRemoved(
		{ phalf, phalf, phalf, phalf });
	const auto xradius = removed.left() + st::giftBoxGiftRadius - phalf;
	const auto yradius = removed.top() + st::giftBoxGiftRadius - phalf;
	p.drawRoundedRect(extended, xradius, yradius);
}

void AttributeButton::validatePatternCache() {
	if (_patternNow.emoji && _patternNow.emoji->ready()) {
		const auto inner = QRect(0, 0,
			this->width() - _extend.left() - _extend.right(),
			this->height() - _extend.top() - _extend.bottom());
		const auto size = inner.size();
		const auto ratio = style::DevicePixelRatio();
		if (_patternFrame.size() != size * ratio) {
			_patternFrame = QImage(
				size * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_patternFrame.setDevicePixelRatio(ratio);
			_patternColor = QColor();
		}
		const auto color = _delegate->patternColor();
		if (_patternColor != color) {
			_patternColor = color;
			_patternNow.map.clear();
			_patternFrame.fill(Qt::transparent);

			auto p = QPainter(&_patternFrame);
			const auto skip = inner.width() / 3;
			Ui::PaintBgPoints(
				p,
				Ui::PatternBgPointsSmall(),
				_patternNow.map,
				_patternNow.emoji.get(),
				color,
				QRect(-skip, 0, inner.width() + 2 * skip, inner.height()));
		}
	} else if (!_patternFrame.isNull()) {
		_patternFrame = QImage();
	}
}

void AttributeButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto model = std::get_if<GiftModel>(&_descriptor);
	const auto pattern = std::get_if<GiftPattern>(&_descriptor);
	const auto backdrop = std::get_if<GiftBackdrop>(&_descriptor);
	const auto position = QPoint(_extend.left(), _extend.top());
	const auto background = _delegate->background();
	const auto width = this->width();
	const auto dpr = int(background.devicePixelRatio());
	paintBackground(p, background);
	if (backdrop) {
		CacheBackdropBackground(
			backdrop,
			width,
			background.height() / dpr,
			_extend,
			_backgroundCache);
		p.drawImage(_extend.left(), _extend.top(), _backgroundCache);

		const auto inner = QRect(
			_extend.left(),
			_extend.top(),
			this->width() - _extend.left() - _extend.right(),
			this->height() - _extend.top() - _extend.bottom());
		const auto skip = inner.width() / 3;
		const auto emoji = _delegate->patternEmoji();
		const auto paintSymbols = [&](
				PatternCache &cache,
				std::shared_ptr<Text::CustomEmoji> emoji,
				float64 shown) {
			if (shown == 0. || !emoji) {
				return;
			} else if (cache.emoji != emoji) {
				cache.map.clear();
				cache.emoji = emoji;
			}
			Ui::PaintBgPoints(
				p,
				Ui::PatternBgPointsSmall(),
				cache.map,
				emoji.get(),
				backdrop->patternColor,
				QRect(-skip, 0, inner.width() + 2 * skip, inner.height()),
				shown);
		};
		p.setClipRect(inner);
		paintSymbols(_patternNow, emoji.now, 1. - emoji.progress);
		paintSymbols(_patternNext, emoji.next, emoji.progress);
		p.setClipping(false);
	} else if (pattern) {
		p.drawImage(
			_extend.left(),
			_extend.top(),
			_delegate->cachePatternBackground(
				width,
				background.height() / dpr,
				_extend));
		validatePatternCache();
		if (!_patternFrame.isNull()) {
			p.drawImage(_extend.left(), _extend.top(), _patternFrame);
		}
	}

	const auto progress = _selectedAnimation.value(_selected ? 1. : 0.);
	if (progress > 0) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::boxBg->p;
		const auto thickness = style::ConvertScaleExact(2.);
		pen.setWidthF(progress * thickness);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto height = background.height() / dpr;
		const auto outer = QRectF(0, 0, width, height);
		const auto shift = progress * thickness * 2;
		const auto extend = QMarginsF(_extend)
			+ QMarginsF(shift, shift, shift, shift);
		const auto radius = st::giftBoxGiftRadius - shift;
		p.drawRoundedRect(outer.marginsRemoved(extend), radius, radius);
	}

	const auto paused = !isOver();
	const auto now = crl::now();
	const auto colored = v::is<GiftPattern>(_descriptor)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0, 0);
	const auto frameSize = st::uniqueAttributeStickerSize;
	const auto paintFrame = [&](HistoryView::StickerPlayer *player) {
		if (!player || !player->ready()) {
			return;
		}
		auto info = player->frame(frameSize, colored, false, now, paused);
		const auto finished = (info.index + 1 == player->framesCount());
		if (!finished || !paused) {
			player->markFrameShown();
		}
		const auto size = info.image.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				(width - size.width()) / 2,
				st::giftBoxSmallStickerTop,
				size.width(),
				size.height()),
			info.image);
	};
	if (backdrop) {
		const auto backdropPlayers = _delegate->backdropPlayers();
		if (backdropPlayers.progress == 0.) {
			paintFrame(backdropPlayers.now);
		} else if (backdropPlayers.progress == 1.) {
			paintFrame(backdropPlayers.next);
		} else {
			p.setOpacity(1. - backdropPlayers.progress);
			paintFrame(backdropPlayers.now);
			p.setOpacity(backdropPlayers.progress);
			paintFrame(backdropPlayers.next);
			p.setOpacity(1.);
		}
	} else {
		paintFrame(_player.get());
	}

	const auto singlew = width - _extend.left() - _extend.right();
	if (model) {
		p.setPen(st::windowBoldFg);
	} else {
		p.setPen(QColor(255, 255, 255));
	}
	_name.draw(p, {
		.position = (position + QPoint(0, st::giftBoxPremiumTextTop)),
		.availableWidth = singlew,
		.align = style::al_top,
	});

	p.setPen(Qt::NoPen);
	p.setBrush(model
		? anim::color(st::windowBgOver, st::windowBgActive, progress)
		: backdrop
		? backdrop->patternColor
		: _delegate->patternColor());
	const auto ppadding = st::uniqueAttributePercentPadding;
	const auto add = int(std::ceil(style::ConvertScaleExact(6.)));
	const auto pradius = std::max(st::giftBoxGiftRadius - add, 1);
	const auto left = position.x()
		+ singlew
		- add
		- ppadding.right()
		- _percent.maxWidth();
	const auto top = position.y() + add + ppadding.top();
	const auto percent = QRect(
		left,
		top,
		_percent.maxWidth(),
		st::uniqueAttributeType.font->height);
	p.drawRoundedRect(percent.marginsAdded(ppadding), pradius, pradius);
	p.setPen(model
		? anim::color(st::windowSubTextFg, st::windowFgActive, progress)
		: QColor(255, 255, 255));
	_percent.draw(p, {
		.position = percent.topLeft(),
	});
}

Delegate::Delegate(Fn<void()> fullUpdate)
: _fullUpdate(std::move(fullUpdate)) {
}

void Delegate::update(
		const std::optional<Data::UniqueGift> &now,
		const std::optional<Data::UniqueGift> &next,
		float64 progress) {
	const auto wasDirty = _backdropDirty;
	const auto badSticker = [&](
			Sticker &model,
			const std::optional<Data::UniqueGift> &gift) {
		return gift && (model.document != gift->model.document);
	};
	const auto enforceSticker = [&](
			Sticker &model,
			const std::optional<Data::UniqueGift> &gift) {
		Expects(gift.has_value());

		const auto document = model.document = gift->model.document;
		const auto media = document->createMediaView();
		media->checkStickerLarge();
		media->goodThumbnailWanted();

		const auto destroyed = base::take(model.player);
		model.playerDocument = nullptr;
		model.mediaLifetime = rpl::single() | rpl::then(
			document->session().downloaderTaskFinished()
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::on_next([=, &model] {
			model.mediaLifetime.destroy();

			auto result = std::unique_ptr<HistoryView::StickerPlayer>();
			const auto sticker = document->sticker();
			if (sticker->isLottie()) {
				result = std::make_unique<HistoryView::LottiePlayer>(
					ChatHelpers::LottiePlayerFromDocument(
						media.get(),
						ChatHelpers::StickerLottieSize::InlineResults,
						st::uniqueAttributeStickerSize,
						Lottie::Quality::High));
			} else if (sticker->isWebm()) {
				result = std::make_unique<HistoryView::WebmPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::uniqueAttributeStickerSize);
			} else {
				result = std::make_unique<HistoryView::StaticStickerPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::uniqueAttributeStickerSize);
			}
			result->setRepaintCallback(_fullUpdate);
			model.playerDocument = media->owner();
			model.player = std::move(result);
			_fullUpdate();
		});
		if (model.playerDocument) {
			model.mediaLifetime.destroy();
		}
	};
	const auto validateSticker = [&](
			Sticker &model,
			const std::optional<Data::UniqueGift> &gift) {
		if (badSticker(model, gift)) {
			enforceSticker(model, gift);
		}
	};
	const auto validatePatternEmoji = [&](
			Emoji &emoji,
			const std::optional<Data::UniqueGift> &gift) {
		if (!gift) {
			emoji = {};
			return;
		}
		const auto document = gift->pattern.document;
		if (emoji.document != document) {
			emoji.document = document;
			emoji.custom = document->owner().customEmojiManager().create(
				document,
				_fullUpdate,
				Data::CustomEmojiSizeTag::Large);
		}
	};

	if (progress == 1.) {
		Assert(next.has_value());
		if (_nextBackdrop != next->backdrop) {
			_nextBackdrop = next->backdrop;
			_backdropDirty = true;
		}
		validateSticker(_nextModel, next);
		validatePatternEmoji(_nextPatternEmoji, next);
		_nowModel = {};
		_nowPatternEmoji = {};
	} else {
		Assert(now.has_value());
		if (_nowBackdrop != now->backdrop) {
			if (_nextBackdrop == now->backdrop) {
				_nowBackdrop = base::take(_nextBackdrop);
			} else {
				_nowBackdrop = now->backdrop;
				_backdropDirty = true;
			}
		}
		if (badSticker(_nowModel, now)) {
			if (_nextModel.document == now->model.document
				&& !_nextModel.mediaLifetime) {
				_nowModel = base::take(_nextModel);
			} else {
				enforceSticker(_nowModel, now);
			}
		}
		validatePatternEmoji(_nowPatternEmoji, now);
		if (progress > 0.) {
			Assert(next.has_value());
			if (_nextBackdrop != next->backdrop) {
				_nextBackdrop = next->backdrop;
				_backdropDirty = true;
			}
			validateSticker(_nextModel, next);
			validatePatternEmoji(_nextPatternEmoji, next);
		} else {
			if (_nextModel.document) {
				_nextModel = Sticker();
			}
			if (_nextPatternEmoji.document) {
				_nextPatternEmoji = Emoji();
			}
		}
	}
	if (_progress != progress) {
		_progress = progress;
		_backdropDirty = true;
	}
	if (!wasDirty && _backdropDirty) {
		_fullUpdate();
	}
}

QSize Delegate::buttonSize() {
	if (!_single.isEmpty()) {
		return _single;
	}
	const auto width = st::boxWideWidth;
	const auto padding = st::giftBoxPadding;
	const auto available = width - padding.left() - padding.right();
	const auto singlew = (available - 2 * st::giftBoxGiftSkip.x())
		/ kGiftsPerRow;
	_single = QSize(singlew, st::giftBoxGiftSmall);
	return _single;
}

QMargins Delegate::buttonExtend() const {
	return st::defaultDropdownMenu.wrap.shadow.extend;
}

QImage Delegate::background() {
	if (!_bg.isNull()) {
		return _bg;
	}
	const auto single = buttonSize();
	const auto extend = buttonExtend();
	const auto bgSize = single.grownBy(extend);
	const auto ratio = style::DevicePixelRatio();
	auto bg = QImage(
		bgSize * ratio,
		QImage::Format_ARGB32_Premultiplied);
	bg.setDevicePixelRatio(ratio);
	bg.fill(Qt::transparent);

	const auto radius = st::giftBoxGiftRadius;
	const auto rect = QRect(QPoint(), bgSize).marginsRemoved(extend);

	{
		auto p = QPainter(&bg);
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(0.3);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowShadowFg);
		p.drawRoundedRect(
			QRectF(rect).translated(0, radius / 12.),
			radius,
			radius);
	}
	bg = bg.scaled(
		(bgSize * ratio) / 2,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	bg = Images::Blur(std::move(bg), true);
	bg = bg.scaled(
		bgSize * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	{
		auto p = QPainter(&bg);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(rect, radius, radius);
	}

	_bg = std::move(bg);
	return _bg;
}

QImage Delegate::cachePatternBackground(
		int width,
		int height,
		QMargins extend) {
	if (_backdropDirty) {
		_backdrop = GiftBackdrop();
		if (_progress == 0.) {
			_backdrop = _nowBackdrop;
		} else if (_progress == 1.) {
			_backdrop = _nextBackdrop;
		} else {
			_backdrop.centerColor = anim::color(
				_nowBackdrop.centerColor,
				_nextBackdrop.centerColor,
				_progress);
			_backdrop.edgeColor = anim::color(
				_nowBackdrop.edgeColor,
				_nextBackdrop.edgeColor,
				_progress);
			_backdrop.patternColor = anim::color(
				_nowBackdrop.patternColor,
				_nextBackdrop.patternColor,
				_progress);
		}
	}
	CacheBackdropBackground(
		&_backdrop,
		width,
		height,
		extend,
		_backdropCache,
		_backdropDirty);

	_backdropDirty = false;
	return _backdropCache;
}

QColor Delegate::patternColor() {
	Expects(!_backdropDirty);

	return _backdrop.patternColor;
}

BackdropPlayers Delegate::backdropPlayers() {
	return {
		.now = _nowModel.player.get(),
		.next = _nextModel.player.get(),
		.progress = _progress,
	};
}

PatternEmoji Delegate::patternEmoji() {
	return {
		.now = _nowPatternEmoji.custom,
		.next = _nextPatternEmoji.custom,
		.progress = _progress,
	};
}

AttributesList::AttributesList(
	QWidget *parent,
	not_null<Delegate*> delegate,
	not_null<const Data::UniqueGiftAttributes*> attributes,
	rpl::producer<Tab> tab,
	Selection initialSelection)
: BoxContentDivider(parent)
, _delegate(delegate)
, _attributes(attributes)
, _tab(std::move(tab))
, _selected(initialSelection)
, _entries(&_models)
, _list(&_entries->list) {
	_singleMin = _delegate->buttonSize();

	fill();

	_tab.value(
	) | rpl::on_next([=](Tab tab) {
		_entries = [&] {
			switch (tab) {
			case Tab::Model: return &_models;
			case Tab::Pattern: return &_patterns;
			case Tab::Backdrop: return &_backdrops;
			}
			Unexpected("Tab in AttributesList.");
		}();
		_list = &_entries->list;
		refreshButtons();
		refreshAbout();
	}, lifetime());

	_selected.value() | rpl::combine_previous() | rpl::on_next([=](
			Selection was,
			Selection now) {
		const auto tab = _tab.current();
		const auto button = [&](int index) {
			const auto i = ranges::find(_views, index, &View::index);
			return (i != end(_views)) ? i->button.get() : nullptr;
		};
		const auto update = [&](int was, int now) {
			if (was != now) {
				if (const auto deselect = button(was)) {
					deselect->toggleSelected(false, anim::type::normal);
				}
				if (const auto select = button(now)) {
					select->toggleSelected(true, anim::type::normal);
				}
			}
		};
		if (tab == Tab::Model) {
			update(was.model, now.model);
		} else if (tab == Tab::Pattern) {
			update(was.pattern, now.pattern);
		} else {
			update(was.backdrop, now.backdrop);
		}
	}, lifetime());
}

auto AttributesList::selected() const -> rpl::producer<Selection> {
	return _selected.value();
}

void AttributesList::fill() {
	const auto addEntries = [&](
			const auto &source,
			Entries &target,
			const auto field) {
		auto value = Selection();
		target.list.clear();
		target.list.reserve(source.size());
		for (const auto &item : source) {
			++(value.*field);
			target.list.emplace_back(Entry{ { item }, { value } });
		}
	};
	addEntries(_attributes->models, _models, &Selection::model);
	addEntries(_attributes->patterns, _patterns, &Selection::pattern);
	addEntries(_attributes->backdrops, _backdrops, &Selection::backdrop);
}

void AttributesList::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleFrom = visibleTop;
	_visibleTill = visibleBottom;
	validateButtons();
}

void AttributesList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.fillRect(rect(), st::boxDividerBg->c);
	paintTop(p);
	paintBottom(p);
}

void AttributesList::refreshButtons() {
	_viewsForWidth = 0;
	_viewsFromRow = 0;
	_viewsTillRow = 0;
	resizeToWidth(width());
	validateButtons();
}

void AttributesList::validateButtons() {
	if (!_perRow || !_about) {
		return;
	}
	const auto padding = st::giftBoxPadding;
	const auto vskip = st::giftListAboutMargin.top()
		+ _about->height()
		+ st::giftListAboutMargin.bottom();
	const auto row = _single.height() + st::giftBoxGiftSkip.y();
	const auto fromRow = std::max(_visibleFrom - vskip, 0) / row;
	const auto tillRow = (_visibleTill - vskip + row - 1) / row;
	Assert(tillRow >= fromRow);
	if (_viewsFromRow == fromRow
		&& _viewsTillRow == tillRow
		&& _viewsForWidth == width()) {
		return;
	}
	_viewsFromRow = fromRow;
	_viewsTillRow = tillRow;
	_viewsForWidth = width();

	const auto available = _viewsForWidth - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	const auto fullw = _perRow * (_single.width() + skipw) - skipw;
	const auto left = padding.left() + (available - fullw) / 2;
	const auto oneh = _single.height() + st::giftBoxGiftSkip.y();
	auto x = left;
	auto y = vskip + fromRow * oneh;
	auto views = std::vector<View>();
	views.reserve((tillRow - fromRow) * _perRow);
	const auto idUsed = [&](DocumentData *document, int column, int row) {
		if (!document) {
			return false;
		}
		for (auto j = row; j != tillRow; ++j) {
			for (auto i = column; i != _perRow; ++i) {
				const auto index = j * _perRow + i;
				if (index >= _list->size()) {
					return false;
				} else if (Sticker((*_list)[index].descriptor) == document) {
					return true;
				}
			}
			column = 0;
		}
		return false;
	};
	const auto selected = [&] {
		const auto values = _selected.current();
		switch (_tab.current()) {
		case Tab::Model: return values.model;
		case Tab::Pattern: return values.pattern;
		case Tab::Backdrop: return values.backdrop;
		}
		Unexpected("Tab in AttributesList::validateButtons.");
	}();
	const auto add = [&](int column, int row) {
		const auto index = row * _perRow + column;
		if (index >= _list->size()) {
			return false;
		}
		const auto &entry = (*_list)[index];
		const auto &descriptor = entry.descriptor;
		const auto document = Sticker(descriptor);
		const auto already = ranges::find_if(_views, [&](const View &view) {
			return view.button && view.document == document;
		});
		if (already != end(_views)) {
			views.push_back(base::take(*already));
			views.back().button->setDescriptor(descriptor);
		} else {
			const auto unused = ranges::find_if(_views, [&](const View &v) {
				return v.button && !idUsed(v.document, column, row);
			});
			if (unused != end(_views)) {
				views.push_back(base::take(*unused));
				views.back().document = document;
				views.back().button->setDescriptor(descriptor);
			} else {
				views.push_back({
					.button = std::make_unique<AttributeButton>(
						this,
						_delegate,
						descriptor),
					.document = document,
				});
				views.back().button->show();
			}
		}
		auto &view = views.back();
		view.index = index;
		view.button->toggleSelected(index == selected, anim::type::instant);
		view.button->setClickedCallback([=] {
			clicked(index);
		});
		return true;
	};
	for (auto j = fromRow; j != tillRow; ++j) {
		for (auto i = 0; i != _perRow; ++i) {
			if (!add(i, j)) {
				break;
			}
			const auto &view = views.back();
			auto pos = QPoint(x, y);

			view.button->setGeometry(
				QRect(pos, _single),
				_delegate->buttonExtend());
			x += _single.width() + skipw;
		}
		x = left;
		y += oneh;
	}

	std::swap(_views, views);
}

void AttributesList::clicked(int index) {
	Expects(index >= 0 && index < _list->size());

	auto now = _selected.current();
	const auto value = (*_list)[index].value;
	const auto check = [&](const auto field) {
		if (value.*field >= 0) {
			now.*field = (now.*field == value.*field) ? -1 : value.*field;
		}
	};
	check(&Selection::model);
	check(&Selection::pattern);
	check(&Selection::backdrop);
	_selected = now;
}

void AttributesList::refreshAbout() {
	auto text = [&] {
		switch (_tab.current()) {
		case Tab::Model: return tr::lng_auction_preview_models;
		case Tab::Pattern: return tr::lng_auction_preview_symbols;
		case Tab::Backdrop: return tr::lng_auction_preview_backdrops;
		}
		Unexpected("Tab in AttributesList::refreshAbout.");
	}()(lt_count_decimal, rpl::single(_list->size() * 1.), tr::rich);
	auto about = std::make_unique<Ui::FlatLabel>(
		this,
		std::move(text),
		st::giftListAbout);
	about->show();
	_about = std::move(about);
	resizeToWidth(width());
}

int AttributesList::resizeGetHeight(int width) {
	const auto padding = st::giftBoxPadding;
	const auto count = int(_list->size());
	const auto available = width - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	_perRow = std::min(
		(available + skipw) / (_singleMin.width() + skipw),
		std::max(count, 1));
	if (!_perRow || !_about) {
		return 0;
	}
	auto result = 0;

	const auto margin = st::giftListAboutMargin;
	_about->resizeToWidth(width - margin.left() - margin.right());
	_about->moveToLeft(margin.left(), result + margin.top());
	result += margin.top() + _about->height() + margin.bottom();

	const auto singlew = std::min(
		((available + skipw) / _perRow) - skipw,
		2 * _singleMin.width());
	Assert(singlew >= _singleMin.width());
	const auto singleh = _singleMin.height();

	_single = QSize(singlew, singleh);
	const auto rows = (count + _perRow - 1) / _perRow;
	const auto skiph = st::giftBoxGiftSkip.y();

	result += rows
		? (padding.bottom() + rows * (singleh + skiph) - skiph)
		: 0;

	return result;
}

} // namespace

void StarGiftPreviewBox(
		not_null<GenericBox*> box,
		const QString &title,
		const Data::UniqueGiftAttributes &attributes,
		Data::GiftAttributeIdType tab,
		std::shared_ptr<Data::UniqueGift> selected) {
	Expects(!attributes.models.empty());
	Expects(!attributes.patterns.empty());
	Expects(!attributes.backdrops.empty());

	box->setStyle(st::uniqueAttributesBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);

	struct State {
		State(
			QString title,
			const Data::UniqueGiftAttributes &attributes,
			Data::GiftAttributeIdType tab,
			std::shared_ptr<Data::UniqueGift> selected)
		: title(title)
		, delegate([=] {
			if (this->tab.current() != Tab::Model && list) {
				list->update();
			}
		})
		, attributes(attributes)
		, tab(tab)
		, gift(make())
		, pushNextTimer([=] { push(); }) {
			apply(selected);
		}
		void apply(std::shared_ptr<Data::UniqueGift> selected) {
			if (!selected) {
				return;
			}
			const auto up = [&](auto &list, const auto &attribute) {
				ranges::stable_partition(list, [&](const auto &existing) {
					return IdFor(attribute) == IdFor(existing);
				});
			};
			up(attributes.models, selected->model);
			up(attributes.patterns, selected->pattern);
			up(attributes.backdrops, selected->backdrop);
			fixed = { 0, 0, 0 };
			paused = true;
			gift = make();
		}

		void randomize() {
			const auto choose = [](const auto &list, auto &indices) {
				if (indices.empty()) {
					ranges::copy(
						ranges::views::ints(0, int(list.size())),
						std::back_inserter(indices));
				}
				const auto which = base::RandomIndex(indices.size());
				const auto index = indices[which];
				indices.erase(begin(indices) + which);
				return index;
			};
			index = {
				.model = choose(attributes.models, models),
				.pattern = choose(attributes.patterns, patterns),
				.backdrop = choose(attributes.backdrops, backdrops),
			};
		}
		[[nodiscard]] UniqueGiftCover make() {
			randomize();

			const auto choose = [](const auto &list, int index, int fixed) {
				return list[(fixed >= 0) ? fixed : index];
			};
			return {
				.values = {
					.title = title,
					.model = choose(
						attributes.models,
						index.model,
						fixed.model),
					.pattern = choose(
						attributes.patterns,
						index.pattern,
						fixed.pattern),
					.backdrop = choose(
						attributes.backdrops,
						index.backdrop,
						fixed.backdrop),
				},
				.force = paused.current(),
			};
		}
		void push() {
			gift = make();
		}

		QString title;
		Delegate delegate;
		Data::UniqueGiftAttributes attributes;
		std::vector<int> models;
		std::vector<int> patterns;
		std::vector<int> backdrops;
		rpl::variable<Tab> tab = Tab::Model;
		Selection index;
		Selection fixed;
		rpl::variable<bool> paused;

		rpl::variable<UniqueGiftCover> gift;

		AttributesList *list = nullptr;

		base::Timer pushNextTimer;
	};

	const auto state = box->lifetime().make_state<State>(
		title,
		attributes,
		tab,
		selected);

	const auto repaintedHook = [=](
			std::optional<Data::UniqueGift> now,
			std::optional<Data::UniqueGift> next,
			float64 progress) {
		state->delegate.update(now, next, progress);
	};

	const auto top = box->setPinnedToTopContent(
		object_ptr<VerticalLayout>(box));
	AddUniqueGiftCover(top, state->gift.value(), {
		.subtitle = rpl::conditional(
			state->paused.value(),
			tr::lng_auction_preview_selected(tr::marked),
			tr::lng_auction_preview_random(tr::marked)),
		.attributesInfo = true,
		.repaintedHook = repaintedHook,
	});

	AddUniqueCloseButton(box, {});

	const auto container = box->verticalLayout();
	state->list = container->add(object_ptr<AttributesList>(
		box,
		&state->delegate,
		&state->attributes,
		state->tab.value(),
		state->fixed));
	state->list->selected(
	) | rpl::on_next([=](Selection value) {
		state->fixed = value;
		state->paused = (value.model >= 0)
			|| (value.pattern >= 0)
			|| (value.backdrop >= 0);
		state->push();
	}, box->lifetime());

	const auto button = box->addButton(rpl::single(u""_q), [] {});
	const auto buttonsParent = button->parentWidget();
	box->clearButtons();

	const auto add = [&](
			tr::phrase<> text,
			const style::RoundButton &st,
			const style::icon &active,
			Tab tab) {
		auto owned = object_ptr<RoundButton>(buttonsParent, text(), st);
		const auto raw = owned.data();

		raw->setTextTransform(RoundButton::TextTransform::NoTransform);
		raw->setClickedCallback([=] {
			state->tab = tab;
		});
		const auto icon = &active;
		state->tab.value() | rpl::on_next([=](Tab now) {
			raw->setTextFgOverride((now == tab)
				? st::defaultActiveButton.textFg->c
				: std::optional<QColor>());
			raw->setBrushOverride((now == tab)
				? st::defaultActiveButton.textBg->c
				: std::optional<QColor>());
			raw->setIconOverride((now == tab) ? icon : nullptr);
		}, raw->lifetime());
		box->addButton(std::move(owned));
	};
	add(
		tr::lng_auction_preview_symbols_button,
		st::uniqueAttributeSymbol,
		st::uniqueAttributeSymbolActive,
		Tab::Pattern);
	add(
		tr::lng_auction_preview_backdrops_button,
		st::uniqueAttributeBackdrop,
		st::uniqueAttributeBackdropActive,
		Tab::Backdrop);
	add(
		tr::lng_auction_preview_models_button,
		st::uniqueAttributeModel,
		st::uniqueAttributeModelActive,
		Tab::Model);

	state->paused.value() | rpl::on_next([=](bool paused) {
		if (paused) {
			state->pushNextTimer.cancel();
		} else {
			state->pushNextTimer.callEach(kSwitchTimeout);
		}
	}, box->lifetime());
}

} // namespace Ui
