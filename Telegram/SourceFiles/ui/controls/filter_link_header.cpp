/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/filter_link_header.h"

#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_filter_icons.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Ui {
namespace {

constexpr auto kBodyAnimationPart = 0.90;
constexpr auto kTitleAdditionalScale = 0.05;

class Widget final : public RpWidget {
public:
	Widget(
		not_null<QWidget*> parent,
		FilterLinkHeaderDescriptor &&descriptor);

	void setTitlePosition(int x, int y);

	[[nodiscard]] rpl::producer<not_null<QWheelEvent*>> wheelEvents() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

	[[nodiscard]] QRectF previewRect(
		float64 topProgress,
		float64 sizeProgress) const;
	void refreshTitleText();

	const not_null<FlatLabel*> _about;
	const not_null<IconButton*> _close;
	QMargins _aboutPadding;

	struct {
		float64 top = 0.;
		float64 body = 0.;
		float64 title = 0.;
		float64 scaleTitle = 0.;
	} _progress;

	rpl::variable<int> _badge;
	QImage _preview;
	QRectF _previewRect;

	QString _titleText;
	style::font _titleFont;
	QMargins _titlePadding;
	QPoint _titlePosition;
	QPainterPath _titlePath;

	QString _folderTitle;
	not_null<const style::icon*> _folderIcon;

	int _maxHeight = 0;

	rpl::event_stream<not_null<QWheelEvent*>> _wheelEvents;

};

[[nodiscard]] QImage GeneratePreview(
		const QString &title,
		not_null<const style::icon*> icon,
		int badge) {
	const auto size = st::filterLinkPreview;
	const auto ratio = style::DevicePixelRatio();
	const auto radius = st::filterLinkPreviewRadius;
	const auto full = QSize(size, size) * ratio;
	auto result = QImage(full, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(st::windowBg->c);

	auto p = QPainter(&result);

	const auto column = st::filterLinkPreviewColumn;
	p.fillRect(0, 0, column, size, st::sideBarBg);
	p.fillRect(column, 0, size - column, size, st::emojiPanCategories);

	const auto &st = st::windowFiltersButton;
	const auto skip = st.style.font->spacew;
	const auto available = column - 2 * skip;
	const auto iconWidth = st::foldersAll.width();
	const auto iconHeight = st::foldersAll.height();
	const auto iconLeft = (column - iconWidth) / 2;
	const auto allIconTop = st::filterLinkPreviewAllBottom - iconHeight;
	st::foldersAll.paint(p, iconLeft, allIconTop, size);
	const auto myIconTop = st::filterLinkPreviewMyBottom - iconHeight;
	icon->paint(p, iconLeft, myIconTop, size);

	const auto paintName = [&](const QString &text, int top) {
		auto string = Ui::Text::String(
			st.style,
			text,
			kDefaultTextOptions,
			available);
		string.draw(p, {
			.position = QPoint(
				std::max(
					(column - string.maxWidth()) / 2,
					skip),
				top),
			.outerWidth = available,
			.availableWidth = available,
			.align = style::al_left,
			.elisionLines = 1,
		});
	};
	p.setFont(st.style.font);
	p.setPen(st.textFg);
	paintName(tr::lng_filters_all(tr::now), st::filterLinkPreviewAllTop);
	p.setPen(st.textFgActive);
	paintName(title, st::filterLinkPreviewMyTop);

	auto hq = PainterHighQualityEnabler(p);

	const auto chatSize = st::filterLinkPreviewChatSize;
	const auto chatLeft = size + st::lineWidth - (chatSize / 2);
	const auto paintChat = [&](int top, const style::color &bg) {
		p.setBrush(bg);
		p.drawEllipse(chatLeft, top, chatSize, chatSize);
	};
	const auto chatSkip = st::filterLinkPreviewChatSkip;
	const auto chat1Top = (size - 2 * chatSize - chatSkip) / 2;
	const auto chat2Top = size - chat1Top - chatSize;
	p.setPen(Qt::NoPen);
	paintChat(chat1Top, st::historyPeer4UserpicBg);
	paintChat(chat2Top, st::historyPeer8UserpicBg);

	if (badge > 0) {
		const auto font = st.badgeStyle.font;
		const auto badgeHeight = st.badgeHeight;
		const auto countBadgeWidth = [&](const QString &text) {
			return std::max(
				font->width(text) + 2 * st.badgeSkip,
				badgeHeight);
		};
		const auto defaultBadgeWidth = countBadgeWidth(u"+3"_q);
		const auto badgeText = '+' + QString::number(badge);
		const auto badgeWidth = countBadgeWidth(badgeText);
		const auto defaultBadgeLeft = st::filterLinkPreviewBadgeLeft;
		const auto badgeLeft = defaultBadgeLeft
			+ (defaultBadgeWidth - badgeWidth) / 2;
		const auto badgeTop = st::filterLinkPreviewBadgeTop;

		const auto add = st::lineWidth;
		auto pen = st.textBg->p;
		pen.setWidthF(add * 2.);
		p.setPen(pen);
		p.setBrush(st.badgeBg);
		const auto radius = (badgeHeight / 2) + add;
		const auto rect = QRect(badgeLeft, badgeTop, badgeWidth, badgeHeight)
			+ QMargins(add, add, add, add);
		p.drawRoundedRect(rect, radius, radius);

		p.setPen(st.badgeFg);
		p.setFont(st.badgeStyle.font);
		p.drawText(rect, badgeText, style::al_center);
	}

	auto pen = st::shadowFg->p;
	pen.setWidthF(st::lineWidth * 2.);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(0, 0, size, size, radius, radius);
	p.end();

	return Images::Round(std::move(result), Images::CornersMask(radius));
}

Widget::Widget(
	not_null<QWidget*> parent,
	FilterLinkHeaderDescriptor &&descriptor)
: RpWidget(parent)
, _about(CreateChild<FlatLabel>(
	this,
	rpl::single(descriptor.about.value()),
	st::filterLinkAbout))
, _close(CreateChild<IconButton>(this, st::boxTitleClose))
, _aboutPadding(st::boxRowPadding)
, _badge(std::move(descriptor.badge))
, _titleText(descriptor.title)
, _titleFont(st::boxTitle.style.font)
, _titlePadding(st::filterLinkTitlePadding)
, _folderTitle(descriptor.folderTitle)
, _folderIcon(descriptor.folderIcon) {
	setMinimumHeight(st::boxTitleHeight);
	refreshTitleText();
	setTitlePosition(st::boxTitlePosition.x(), st::boxTitlePosition.y());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_preview = QImage();
	}, lifetime());

	_badge.changes() | rpl::start_with_next([=] {
		_preview = QImage();
		update();
	}, lifetime());
}

void Widget::refreshTitleText() {
	_titlePath = QPainterPath();
	_titlePath.addText(0, _titleFont->ascent, _titleFont, _titleText);
	update();
}

void Widget::setTitlePosition(int x, int y) {
	_titlePosition = { x, y };
}

rpl::producer<not_null<QWheelEvent*>> Widget::wheelEvents() const {
	return _wheelEvents.events();
}

rpl::producer<> Widget::closeRequests() const {
	return _close->clicks() | rpl::to_empty;
}

void Widget::resizeEvent(QResizeEvent *e) {
	const auto &padding = _aboutPadding;
	const auto availableWidth = width() - padding.left() - padding.right();
	if (availableWidth <= 0) {
		return;
	}
	_about->resizeToWidth(availableWidth);

	const auto minHeight = minimumHeight();
	const auto maxHeight = st::filterLinkAboutTop
		+ _about->height()
		+ st::filterLinkAboutBottom;
	if (maxHeight <= minHeight) {
		return;
	} else if (_maxHeight != maxHeight) {
		_maxHeight = maxHeight;
		setMaximumHeight(maxHeight);
	}

	const auto progress = (height() - minHeight)
		/ float64(_maxHeight - minHeight);
	_progress.top = 1. -
		std::clamp(
			(1. - progress) / kBodyAnimationPart,
			0.,
			1.);
	_progress.body = _progress.top;
	_progress.title = 1. - progress;
	_progress.scaleTitle = 1. + kTitleAdditionalScale * progress;

	_previewRect = previewRect(_progress.top, _progress.body);

	const auto titleTop = _previewRect.top()
		+ _previewRect.height()
		+ _titlePadding.top();
	const auto titlePathRect = _titlePath.boundingRect();
	const auto aboutTop = titleTop
		+ titlePathRect.height()
		+ _titlePadding.bottom();
	_about->moveToLeft(_aboutPadding.left(), aboutTop);
	_about->setOpacity(_progress.body);

	_close->moveToRight(0, 0);

	update();
}

QRectF Widget::previewRect(
		float64 topProgress,
		float64 sizeProgress) const {
	const auto size = st::filterLinkPreview * sizeProgress;
	return QRectF(
		(width() - size) / 2.,
		st::filterLinkPreviewTop * topProgress,
		size,
		size);
};

void Widget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.setOpacity(_progress.body);
	if (_progress.top) {
		auto hq = PainterHighQualityEnabler(p);
		if (_preview.isNull()) {
			_preview = GeneratePreview(
				_folderTitle,
				_folderIcon,
				_badge.current());
		}
		p.drawImage(_previewRect, _preview);
	}
	p.resetTransform();

	const auto titlePathRect = _titlePath.boundingRect();

	// Title.
	PainterHighQualityEnabler hq(p);
	p.setOpacity(1.);
	p.setFont(_titleFont);
	p.setPen(st::boxTitleFg);
	const auto fullPreviewRect = previewRect(1., 1.);
	const auto fullTitleTop = fullPreviewRect.top()
		+ fullPreviewRect.height()
		+ _titlePadding.top();
	p.translate(
		anim::interpolate(
			(width() - titlePathRect.width()) / 2,
			_titlePosition.x(),
			_progress.title),
		anim::interpolate(fullTitleTop, _titlePosition.y(), _progress.title));

	p.translate(titlePathRect.center());
	p.scale(_progress.scaleTitle, _progress.scaleTitle);
	p.translate(-titlePathRect.center());
	p.fillPath(_titlePath, st::boxTitleFg);
}

void Widget::wheelEvent(QWheelEvent *e) {
	_wheelEvents.fire(e);
}

} // namespace

[[nodiscard]] FilterLinkHeader MakeFilterLinkHeader(
		not_null<QWidget*> parent,
		FilterLinkHeaderDescriptor &&descriptor) {
	const auto result = CreateChild<Widget>(
		parent.get(),
		std::move(descriptor));
	return {
		.widget = result,
		.wheelEvents = result->wheelEvents(),
		.closeRequests = result->closeRequests(),
	};
}

object_ptr<RoundButton> FilterLinkProcessButton(
		not_null<QWidget*> parent,
		FilterLinkHeaderType type,
		const QString &title,
		rpl::producer<int> badge) {
	const auto st = &st::filterInviteBox.button;
	const auto badgeSt = &st::filterInviteButtonBadgeStyle;
	auto result = object_ptr<RoundButton>(parent, rpl::single(u""_q), *st);

	struct Data {
		QString text;
		QString badge;
	};
	auto data = std::move(
		badge
	) | rpl::map([=](int count) {
		const auto badge = count ? QString::number(count) : QString();
		const auto with = [&](QString badge) {
			return rpl::map([=](QString text) {
				return Data{ text, badge };
			});
		};
		switch (type) {
		case FilterLinkHeaderType::AddingFilter:
			return badge.isEmpty()
				? tr::lng_filters_by_link_add_no() | with(QString())
				: tr::lng_filters_by_link_add_button(
					lt_folder,
					rpl::single(title)
				) | with(badge);
		case FilterLinkHeaderType::AddingChats:
			return badge.isEmpty()
				? tr::lng_filters_by_link_join_no() | with(QString())
				: tr::lng_filters_by_link_and_join_button(
					lt_count,
					rpl::single(float64(count))) | with(badge);
		case FilterLinkHeaderType::AllAdded:
			return tr::lng_box_ok() | with(QString());
		case FilterLinkHeaderType::Removing:
			return badge.isEmpty()
				? tr::lng_filters_by_link_remove_button() | with(QString())
				: tr::lng_filters_by_link_and_quit_button(
					lt_count,
					rpl::single(float64(count))) | with(badge);
		}
		Unexpected("Type in FilterLinkProcessButton.");
	}) | rpl::flatten_latest();

	struct Label : RpWidget {
		using RpWidget::RpWidget;

		Text::String text;
		Text::String badge;
	};
	const auto label = result->lifetime().make_state<Label>(result.data());
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	result->sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto xskip = st->style.font->spacew;
		const auto yskip = xskip / 2;
		label->setGeometry(QRect(QPoint(), size).marginsRemoved(
			{ xskip, yskip, xskip, yskip }));
	}, label->lifetime());
	label->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(label);
		const auto width = label->width();
		const auto hasBadge = !label->badge.isEmpty();
		const auto &badgePadding = st::filterInviteButtonBadgePadding;
		const auto badgeInnerWidth = label->badge.maxWidth();
		const auto badgeInnerHeight = badgeSt->font->height;
		const auto badgeSize = QRect(
			0,
			0,
			badgeInnerWidth,
			badgeInnerHeight
		).marginsAdded(badgePadding).size();
		const auto skip = st::filterInviteButtonBadgeSkip;
		const auto badgeWithSkip = hasBadge ? (skip + badgeSize.width()) : 0;
		const auto full = label->text.maxWidth() + badgeWithSkip;
		const auto use = std::min(full, width);
		const auto left = (width - use) / 2;
		const auto top = st->textTop - label->y();
		const auto available = use - badgeWithSkip;

		p.setPen(st->textFg);
		label->text.drawLeftElided(p, left, top, available + skip, width);
		if (hasBadge) {
			p.setPen(Qt::NoPen);
			p.setBrush(st->textFg);
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = badgeSize.height() / 2;
			const auto badgePosition = QPoint(
				left + available + skip,
				top - badgePadding.top());
			p.drawRoundedRect(
				QRect(badgePosition, badgeSize),
				radius,
				radius);
			p.setPen(st->textBg);
			label->badge.drawLeftElided(
				p,
				badgePosition.x() + badgePadding.left(),
				badgePosition.y() + badgePadding.top(),
				badgeInnerWidth + skip,
				width);
		}
	}, label->lifetime());

	std::move(data) | rpl::start_with_next([=](Data data) {
		label->text.setText(st::filterInviteButtonStyle, data.text);
		label->badge.setText(st::filterInviteButtonBadgeStyle, data.badge);
		label->update();
	}, label->lifetime());

	return result;
}

} // namespace Ui
