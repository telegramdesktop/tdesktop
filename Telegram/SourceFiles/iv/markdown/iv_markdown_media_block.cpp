/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_media_block.h"

#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "lang/lang_keys.h"

#include "rpl/lifetime.h"

#include "styles/palette.h"
#include "styles/style_iv.h"

#include <algorithm>
#include <utility>

namespace Iv::Markdown {
namespace {

constexpr auto kIvMarkedTextOptions = TextParseOptions{
	TextParseMultiline,
	0,
	0,
	Qt::LayoutDirectionAuto,
};

[[nodiscard]] const style::Markdown &PaintStyle(
		const MarkdownArticlePaintContext &context,
		const style::Markdown &st) {
	return context.paintMarkdownStyle(st);
}

[[nodiscard]] MarkdownArticlePaintContext ClippedContext(
		const MarkdownArticlePaintContext &context,
		QRect clip) {
	auto result = context;
	result.clip = clip;
	return result;
}

void SetPlainTextLeaf(
		Ui::Text::String *leaf,
		const style::TextStyle &textStyle,
		const QString &text,
		int width) {
	*leaf = Ui::Text::String(TextMinResizeWidth(width));
	leaf->setMarkedText(
		textStyle,
		TextWithEntities::Simple(text),
		kIvMarkedTextOptions);
}

[[nodiscard]] int LeafHeight(
		const Ui::Text::String &leaf,
		const style::TextStyle &textStyle,
		int width) {
	return std::max(
		leaf.countHeight(width),
		TextLineHeight(textStyle));
}

[[nodiscard]] int LeafFirstLineBaseline(
		const Ui::Text::String &leaf,
		const QRect &textRect,
		const style::TextStyle &textStyle) {
	const auto lines = leaf.countLinesGeometry(textRect.width());
	return textRect.y() + (lines.empty()
		? TextLineBaseline(textStyle)
		: lines.front().baseline);
}

void PaintTextLeaf(
		Painter &p,
		const Ui::Text::String &leaf,
		const MarkdownArticlePaintContext &context,
		QRect rect,
		int width,
		style::align align = style::al_left) {
	const auto availableWidth = std::max(width, 1);
	leaf.draw(p, {
		.position = rect.topLeft(),
		.availableWidth = availableWidth,
		.geometry = TextGeometry(availableWidth),
		.align = align,
		.clip = context.clip,
		.palette = &p.textPalette(),
		.pre = context.caches.pre,
		.blockquote = context.caches.blockquote,
		.colors = context.caches.colors,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
	});
}

void PaintCardSurface(
		Painter &p,
		QRect rect,
		int border,
		const style::color &borderFg,
		const style::color &bg,
		int radius) {
	if (rect.isEmpty()) {
		return;
	}
	if (border <= 0) {
		if (radius > 0) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(bg->c);
			p.drawRoundedRect(rect, radius, radius);
		} else {
			p.fillRect(rect, bg->c);
		}
		return;
	}
	const auto half = border / 2.;
	const auto inner = QRectF(rect).marginsRemoved({
		half,
		half,
		half,
		half,
	});
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(borderFg->c, border));
	p.setBrush(bg->c);
	p.drawRoundedRect(inner, radius, radius);
}

[[nodiscard]] QString ChannelCopyText(const PreparedChannelBlockData &channel) {
	return channel.title;
}

class ChannelMediaBlock final : public MediaBlock {
public:
	ChannelMediaBlock(
		const PreparedChannelBlockData &prepared,
		std::shared_ptr<MediaRuntime> mediaRuntime);

	[[nodiscard]] uint64 stableId() const override;

	[[nodiscard]] int resizeGetHeight(int width) override;

	void setGeometry(QRect geometry) override;

	[[nodiscard]] QRect geometry() const override;

	[[nodiscard]] int firstLineBaseline() const override;

	void paint(
			Painter &p,
			const MarkdownArticlePaintContext &context) const override;

	[[nodiscard]] ClickHandlerPtr linkAt(QPoint point) const override;

	[[nodiscard]] MediaActivation activationAt(QPoint point) const override;

	[[nodiscard]] MediaBlockSelectionData selectionData() const override;

private:
	void resolveChannel();

	void rebuildLayout(int width);

	void applyGeometry();

	void handleJoinedChange();

	const uint64 _stableId = 0;
	const uint64 _channelId = 0;
	const QString _titleText;
	const QString _copyText;
	const QString _username;
	const std::shared_ptr<MediaRuntime> _mediaRuntime;
	std::shared_ptr<ChannelRuntime> _channelRuntime;
	MediaActivation _openActivation;
	MediaActivation _joinActivation;
	QRect _geometry;
	Ui::Text::String _titleLeaf;
	QString _actionText;
	ClickHandlerPtr _joinLink;
	QRect _titleRect;
	QRect _actionRect;
	rpl::lifetime _joinedChangesLifetime;
	int _layoutWidth = 1;
	int _height = 1;
	int _titleWidth = 1;
	int _actionWidth = 0;
	int _actionOuterWidth = 0;
	int _actionOuterHeight = 0;
	int _textHeight = 0;
	int _cardContentHeight = 0;
	int _firstLineBaseline = 0;
	bool _joinVisible = false;
	bool _channelResolved = false;
};

ChannelMediaBlock::ChannelMediaBlock(
	const PreparedChannelBlockData &prepared,
	std::shared_ptr<MediaRuntime> mediaRuntime)
: _stableId(prepared.id.value)
, _channelId(prepared.channelId)
, _titleText(prepared.title)
, _copyText(ChannelCopyText(prepared))
, _username(prepared.username)
, _mediaRuntime(std::move(mediaRuntime)) {
	resolveChannel();
	if (_mediaRuntime) {
		_mediaRuntime->channelJoinedChanges() | rpl::on_next([=](uint64 id) {
			if (id == _channelId) {
				handleJoinedChange();
			}
		}, _joinedChangesLifetime);
	}
}

uint64 ChannelMediaBlock::stableId() const {
	return _stableId;
}

int ChannelMediaBlock::resizeGetHeight(int width) {
	rebuildLayout(width);
	return _height;
}

void ChannelMediaBlock::setGeometry(QRect geometry) {
	if (_layoutWidth != std::max(geometry.width(), 1)) {
		rebuildLayout(geometry.width());
	}
	_geometry = QRect(
		geometry.topLeft(),
		QSize(_layoutWidth, _height));
	applyGeometry();
}

QRect ChannelMediaBlock::geometry() const {
	return _geometry;
}

int ChannelMediaBlock::firstLineBaseline() const {
	return _firstLineBaseline;
}

void ChannelMediaBlock::paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) const {
	const auto visible = context.clip.intersected(_geometry);
	if (visible.isEmpty()) {
		return;
	}
	const auto visibleContext = ClippedContext(context, visible);
	const auto &layout = layoutStyle().channel;
	const auto &paint = PaintStyle(context, layoutStyle()).channel;
	const auto &buttonLayout = layout.button;
	const auto &buttonPaint = paint.button;
	p.save();
	p.setClipRect(visible);
	PaintCardSurface(
		p,
		_geometry,
		layout.border,
		paint.borderFg,
		paint.bg,
		layout.radius);
	p.setPen(paint.titleFg->c);
	PaintTextLeaf(
		p,
		_titleLeaf,
		visibleContext,
		_titleRect,
		_titleWidth);
	if (_joinVisible && !_actionRect.isEmpty()) {
		const auto innerRect = _actionRect.marginsRemoved(buttonLayout.padding);
		PaintCardSurface(
			p,
			_actionRect,
			buttonLayout.border,
			buttonPaint.borderFg,
			buttonPaint.bg,
			buttonLayout.radius);
		p.setPen(buttonPaint.textFg->c);
		p.setFont((ClickHandler::showAsActive(_joinLink)
			|| ClickHandler::showAsPressed(_joinLink))
			? buttonLayout.textStyle.font->underline()
			: buttonLayout.textStyle.font);
		p.drawText(innerRect, Qt::AlignCenter, _actionText);
	}
	p.restore();
}

ClickHandlerPtr ChannelMediaBlock::linkAt(QPoint point) const {
	if (_joinVisible
		&& _joinLink
		&& !_actionRect.isEmpty()
		&& _actionRect.contains(point)) {
		return _joinLink;
	}
	return nullptr;
}

MediaActivation ChannelMediaBlock::activationAt(QPoint point) const {
	if (!_geometry.contains(point)) {
		return {};
	}
	if (_joinVisible && !_actionRect.isEmpty() && _actionRect.contains(point)) {
		return _joinActivation;
	}
	return _openActivation;
}

MediaBlockSelectionData ChannelMediaBlock::selectionData() const {
	return {
		.copyText = _copyText,
	};
}

void ChannelMediaBlock::resolveChannel() {
	if (_channelResolved) {
		return;
	}
	_channelResolved = true;
	if (_mediaRuntime) {
		_channelRuntime = _mediaRuntime->resolveChannel(_channelId, _username);
	}
	_openActivation = {};
	_joinActivation = {};
	_joinLink = nullptr;
	if (_channelRuntime) {
		_openActivation.kind = MediaActivationKind::OpenChannel;
		_openActivation.channel = _channelRuntime;
	}
}

void ChannelMediaBlock::rebuildLayout(int width) {
	resolveChannel();
	const auto &card = layoutStyle().channel;
	const auto &padding = card.padding;
	const auto &button = card.button;
	const auto &buttonPadding = button.padding;
	const auto &titleStyle = card.titleStyle;
	const auto &actionStyle = button.textStyle;
	_layoutWidth = std::max(width, 1);
	_joinVisible = _channelRuntime && _channelRuntime->joinVisible();
	if (_joinVisible && _channelRuntime) {
		_joinActivation.kind = MediaActivationKind::JoinChannel;
		_joinActivation.channel = _channelRuntime;
		if (!_joinLink) {
			_joinLink = std::make_shared<LambdaClickHandler>(
				[runtime = _channelRuntime](ClickContext context) {
					runtime->join(context.button);
				});
		}
	} else {
		_joinActivation = {};
		_joinLink = nullptr;
	}

	auto actionTextHeight = 0;
	auto actionOuterWidth = 0;
	auto actionOuterHeight = 0;
	if (_joinVisible) {
		_actionText = tr::lng_iv_join_channel(tr::now);
		_actionWidth = std::max(actionStyle.font->width(_actionText), 1);
		actionTextHeight = TextLineHeight(actionStyle);
		actionOuterWidth = _actionWidth
			+ buttonPadding.left()
			+ buttonPadding.right();
		actionOuterHeight = actionTextHeight
			+ buttonPadding.top()
			+ buttonPadding.bottom();
	} else {
		_actionText = QString();
		_actionWidth = 0;
	}

	_titleWidth = std::max(
		_layoutWidth
			- padding.left()
			- (_joinVisible
				? (actionOuterWidth + card.buttonSkip)
				: padding.right()),
		1);
	SetPlainTextLeaf(
		&_titleLeaf,
		titleStyle,
		_titleText,
		_titleWidth);
	const auto titleHeight = LeafHeight(
		_titleLeaf,
		titleStyle,
		_titleWidth);

	_textHeight = titleHeight;
	_actionOuterWidth = actionOuterWidth;
	_actionOuterHeight = actionOuterHeight;
	_cardContentHeight = std::max(_textHeight, _actionOuterHeight);
	_height = padding.top() + _cardContentHeight + padding.bottom();
}

void ChannelMediaBlock::applyGeometry() {
	const auto &card = layoutStyle().channel;
	const auto &padding = card.padding;
	const auto &titleStyle = card.titleStyle;
	const auto contentLeft = _geometry.x() + padding.left();
	const auto textTop = _geometry.y() + padding.top()
		+ std::max((_cardContentHeight - _textHeight) / 2, 0);
	const auto titleHeight = LeafHeight(
		_titleLeaf,
		titleStyle,
		_titleWidth);
	_titleRect = QRect(
		contentLeft,
		textTop,
		_titleWidth,
		titleHeight);
	_firstLineBaseline = LeafFirstLineBaseline(
		_titleLeaf,
		_titleRect,
		titleStyle);
	if (_joinVisible && _actionOuterWidth > 0 && _actionOuterHeight > 0) {
		_actionRect = QRect(
			_geometry.x() + _layoutWidth - _actionOuterWidth,
			_geometry.y(),
			_actionOuterWidth,
			_height);
	} else {
		_actionRect = QRect();
	}
}

void ChannelMediaBlock::handleJoinedChange() {
	if (_geometry.width() <= 0 && _layoutWidth <= 0) {
		_channelResolved = false;
		resolveChannel();
		return;
	}
	const auto previousGeometry = _geometry;
	const auto previousTitleRect = _titleRect;
	const auto previousActionRect = _actionRect;
	const auto previousHeight = _height;
	const auto previousJoinVisible = _joinVisible;
	_channelResolved = false;
	resolveChannel();
	rebuildLayout((_geometry.width() > 0) ? _geometry.width() : _layoutWidth);
	if (_geometry.width() > 0) {
		_geometry = QRect(
			previousGeometry.topLeft(),
			QSize(_layoutWidth, _height));
		applyGeometry();
	}
	if (_height != previousHeight) {
		requestRelayout(QRect());
	} else if (_joinVisible != previousJoinVisible
		|| _titleRect != previousTitleRect
		|| _actionRect != previousActionRect) {
		requestRepaint(previousGeometry);
	}
}

} // namespace

MediaBlock::~MediaBlock() = default;

bool MediaBlock::alive() const {
	return true;
}

void MediaBlock::setHost(MediaBlockHost *host) {
	if (_host == host) {
		return;
	}
	_host = host;
	hostUpdated();
}

MediaBlockHost *MediaBlock::host() const {
	return _host;
}

bool MediaBlock::hasHeavyPart() const {
	return false;
}

void MediaBlock::unloadHeavyPart() {
}

void MediaBlock::hideSpoilers() {
}

void MediaBlock::setLayoutStyle(const style::Markdown &st) {
	if (_st == &st) {
		return;
	}
	_st = &st;
	layoutStyleUpdated();
}

const style::Markdown &MediaBlock::layoutStyle() const {
	return *_st;
}

void MediaBlock::setMediaPixelScale(double scale) {
	_mediaPixelScale = scale;
	mediaPixelScaleUpdated();
}

double MediaBlock::mediaPixelScale() const {
	return _mediaPixelScale;
}

void MediaBlock::requestRepaint(QRect articleRect) const {
	if (_host) {
		_host->requestRepaint(articleRect);
	}
}

void MediaBlock::requestRelayout(QRect articleRect) const {
	if (_host) {
		_host->requestRelayout(articleRect);
	}
}

void MediaBlock::layoutStyleUpdated() {
}

void MediaBlock::mediaPixelScaleUpdated() {
}

void MediaBlock::hostUpdated() {
}

std::shared_ptr<MediaBlock> CreatePhotoMediaBlock(
		const PreparedPhotoBlockData &prepared,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st) {
	if (mediaRuntime
		&& prepared.viewerOpen
		&& prepared.urlOverride.isEmpty()) {
		mediaRuntime->registerPhoto(prepared.photoId, prepared.caption);
		if (const auto hosted = mediaRuntime->hostedMediaBlockFactory()) {
			if (const auto block = hosted->createPhoto(prepared)) {
				block->setLayoutStyle(st);
				return block;
			}
		}
	}
	return nullptr;
}

std::shared_ptr<MediaBlock> CreateVideoMediaBlock(
		const PreparedVideoBlockData &prepared,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st) {
	if (mediaRuntime) {
		mediaRuntime->registerDocument(prepared.media.id, prepared.caption);
		if (const auto hosted = mediaRuntime->hostedMediaBlockFactory()) {
			if (const auto block = hosted->createVideo(prepared)) {
				block->setLayoutStyle(st);
				return block;
			}
		}
	}
	return nullptr;
}

std::shared_ptr<MediaBlock> CreateAudioMediaBlock(
		const PreparedAudioBlockData &prepared,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st) {
	if (mediaRuntime) {
		if (const auto hosted = mediaRuntime->hostedMediaBlockFactory()) {
			if (const auto block = hosted->createAudio(prepared)) {
				block->setLayoutStyle(st);
				return block;
			}
		}
	}
	return nullptr;
}

std::shared_ptr<MediaBlock> CreateMapMediaBlock(
		const PreparedMapBlockData &prepared,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st) {
	if (mediaRuntime) {
		if (const auto hosted = mediaRuntime->hostedMediaBlockFactory()) {
			if (const auto block = hosted->createMap(prepared)) {
				block->setLayoutStyle(st);
				return block;
			}
		}
	}
	return nullptr;
}

std::shared_ptr<MediaBlock> CreateChannelMediaBlock(
		const PreparedChannelBlockData &prepared,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st) {
	auto result = std::make_shared<ChannelMediaBlock>(prepared, mediaRuntime);
	result->setLayoutStyle(st);
	return result;
}

std::shared_ptr<MediaBlock> CreateGroupedMediaBlock(
		const PreparedGroupedMediaBlockData &prepared,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st) {
	if (mediaRuntime) {
		for (const auto &item : prepared.items) {
			switch (item.media.kind) {
			case PreparedMediaItemKind::Photo:
				mediaRuntime->registerPhoto(
					item.media.id,
					prepared.caption);
				break;
			case PreparedMediaItemKind::Document:
				mediaRuntime->registerDocument(
					item.media.id,
					prepared.caption);
				break;
			}
		}
		if (const auto hosted = mediaRuntime->hostedMediaBlockFactory()) {
			if (const auto block = hosted->createGroupedMedia(prepared)) {
				block->setLayoutStyle(st);
				return block;
			}
		}
	}
	return nullptr;
}

} // namespace Iv::Markdown
