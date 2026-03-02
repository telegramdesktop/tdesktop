/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/tag_info_box.h"

#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_tag_control.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "history/view/history_view_message.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "styles/style_boxes.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace {

using HistoryView::BadgeRole;

constexpr auto kTextLinesAlpha = 0.1;

[[nodiscard]] QColor RoleColor(BadgeRole role) {
	return (role == BadgeRole::Creator)
		? st::rankOwnerFg->c
		: (role == BadgeRole::Admin)
		? st::rankAdminFg->c
		: st::rankUserFg->c;
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeRoundColoredLogo(
		not_null<QWidget*> parent,
		BadgeRole role) {
	const auto &icon = st::tagInfoIcon;
	const auto &padding = st::tagInfoIconPadding;
	const auto logoSize = icon.size();
	const auto logoOuter = logoSize.grownBy(padding);
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto logo = result.data();
	logo->resize(logo->width(), logoOuter.height());
	logo->paintRequest() | rpl::on_next([=, &icon] {
		if (logo->width() < logoOuter.width()) {
			return;
		}
		auto p = QPainter(logo);
		auto hq = PainterHighQualityEnabler(p);
		const auto x = (logo->width() - logoOuter.width()) / 2;
		const auto outer = QRect(QPoint(x, 0), logoOuter);
		p.setBrush(RoleColor(role));
		p.setPen(Qt::NoPen);
		p.drawEllipse(outer);
		icon.paintInCenter(p, outer);
	}, logo->lifetime());
	return result;
}

[[nodiscard]] Ui::Text::PaletteDependentEmoji MakeTagPillEmoji(
		const QString &text,
		BadgeRole role) {
	return { .factory = [=] {
		const auto color = RoleColor(role);
		const auto &padding = st::msgTagBadgePadding;
		auto string = Ui::Text::String(st::defaultTextStyle, text);
		const auto textWidth = string.maxWidth();
		const auto isUser = (role == BadgeRole::User);
		const auto contentWidth = padding.left()
			+ textWidth
			+ padding.right();
		const auto pillHeight = padding.top()
			+ st::msgFont->height
			+ padding.bottom();
		const auto imgWidth = isUser
			? textWidth
			: std::max(contentWidth, pillHeight);
		const auto imgHeight = isUser
			? st::msgFont->height
			: pillHeight;
		const auto ratio = style::DevicePixelRatio();

		auto result = QImage(
			QSize(imgWidth, imgHeight) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		result.fill(Qt::transparent);

		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		if (!isUser) {
			auto bgColor = color;
			bgColor.setAlphaF(0.15);
			p.setPen(Qt::NoPen);
			p.setBrush(bgColor);
			p.drawRoundedRect(
				0,
				0,
				imgWidth,
				imgHeight,
				imgHeight / 2.,
				imgHeight / 2.);
		}
		p.setPen(color);
		string.draw(p, {
			.position = QPoint(
				isUser ? 0 : ((imgWidth - textWidth) / 2),
				isUser ? 0 : padding.top()),
			.availableWidth = textWidth,
		});
		p.end();
		return result;
	}, .margin = st::customEmojiTextBadgeMargin };
}

class TagPreviewsWidget final : public Ui::RpWidget {
public:
	TagPreviewsWidget(
		QWidget *parent,
		not_null<Main::Session*> session,
		BadgeRole role);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void paintPreview(
		QPainter &p,
		QRect rect,
		BadgeRole previewRole) const;
	void paintBubbleToImage(
		QRect rect,
		BadgeRole previewRole) const;
	void invalidateCache();

	const std::unique_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<Ui::ChatStyle> _style;
	BadgeRole _role = BadgeRole::User;
	mutable QImage _leftCache;
	mutable QImage _rightCache;

};

TagPreviewsWidget::TagPreviewsWidget(
	QWidget *parent,
	not_null<Main::Session*> session,
	BadgeRole role)
: RpWidget(parent)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(
	session->colorIndicesValue()))
, _role(role) {
	_style->apply(_theme.get());
	resize(width(), st::tagInfoPreviewHeight);

	_theme->repaintBackgroundRequests(
	) | rpl::on_next([=] {
		invalidateCache();
		update();
	}, lifetime());

	style::PaletteChanged() | rpl::on_next([=] {
		invalidateCache();
		update();
	}, lifetime());

	sizeValue() | rpl::skip(1) | rpl::on_next([=] {
		invalidateCache();
	}, lifetime());
}

void TagPreviewsWidget::invalidateCache() {
	_leftCache = QImage();
	_rightCache = QImage();
}

void TagPreviewsWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto gap = st::tagInfoPreviewGap;
	const auto previewWidth = (width() - gap) / 2;
	if (previewWidth <= 0) {
		return;
	}

	const auto leftRect = QRect(0, 0, previewWidth, height());
	const auto rightRect = QRect(
		previewWidth + gap,
		0,
		width() - previewWidth - gap,
		height());

	paintPreview(p, leftRect, BadgeRole::User);

	const auto rightRole = (_role == BadgeRole::User)
		? BadgeRole::Admin
		: _role;
	paintPreview(p, rightRect, rightRole);
}

void TagPreviewsWidget::paintPreview(
		QPainter &p,
		QRect rect,
		BadgeRole previewRole) const {
	const auto previewRadius = st::tagInfoPreviewRadius;

	p.save();
	p.translate(rect.topLeft());

	const auto local = QRect(0, 0, rect.width(), rect.height());
	auto clipPath = QPainterPath();
	clipPath.addRoundedRect(local, previewRadius, previewRadius);
	p.setClipPath(clipPath);

	Window::SectionWidget::PaintBackground(
		p,
		_theme.get(),
		QSize(rect.width(), window()->height()),
		local);

	auto &cache = (previewRole == BadgeRole::User)
		? _leftCache
		: _rightCache;
	if (cache.isNull()) {
		paintBubbleToImage(rect, previewRole);
	}
	p.drawImage(0, 0, cache);

	p.restore();
}

void TagPreviewsWidget::paintBubbleToImage(
		QRect rect,
		BadgeRole previewRole) const {
	const auto ratio = style::DevicePixelRatio();
	auto &cache = (previewRole == BadgeRole::User)
		? _leftCache
		: _rightCache;
	cache = QImage(
		rect.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(ratio);
	cache.fill(Qt::transparent);

	const auto &stm = _style->messageStyle(false, false);
	const auto &padding = st::tagInfoPreviewBubblePadding;
	const auto radius = st::tagInfoPreviewBubbleRadius;
	const auto rightMargin = st::tagInfoPreviewBubbleRightMargin;
	const auto bubbleTop = padding.top();
	const auto bubbleHeight = rect.height()
		- padding.top()
		- padding.bottom();
	const auto bubbleRight = rect.width() - rightMargin;
	const auto bubbleRect = QRect(
		-radius,
		bubbleTop,
		bubbleRight + radius,
		bubbleHeight);

	auto p = QPainter(&cache);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(stm.msgBg);
		p.drawRoundedRect(bubbleRect, radius, radius);
	}

	const auto innerLeft = padding.left();
	const auto innerRight = bubbleRight - padding.right();
	const auto available = innerRight - innerLeft;

	const auto badgeColor = RoleColor(previewRole);
	const auto badgeText = (previewRole == BadgeRole::Creator)
		? tr::lng_tag_info_preview_owner(tr::now)
		: (previewRole == BadgeRole::Admin)
		? tr::lng_tag_info_preview_admin(tr::now)
		: tr::lng_tag_info_preview_member(tr::now);
	auto badgeString = Ui::Text::String(st::defaultTextStyle, badgeText);
	const auto badgeTextWidth = badgeString.maxWidth();
	const auto &badgePadding = st::msgTagBadgePadding;
	const auto badgeContentWidth = badgePadding.left()
		+ badgeTextWidth
		+ badgePadding.right();
	const auto pillHeight = badgePadding.top()
		+ st::msgFont->height
		+ badgePadding.bottom();
	const auto pillWidth = std::max(badgeContentWidth, pillHeight);

	const auto badgeRight = innerRight;
	const auto badgeLeft = badgeRight - pillWidth;
	const auto badgeTop = bubbleTop + st::tagInfoPreviewBadgeTop;

	if (previewRole != BadgeRole::User) {
		auto bgColor = badgeColor;
		bgColor.setAlphaF(0.15);
		const auto pillRect = QRect(
			badgeLeft,
			badgeTop,
			pillWidth,
			pillHeight);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bgColor);
		p.drawRoundedRect(pillRect, pillHeight / 2., pillHeight / 2.);
		p.setPen(badgeColor);
		badgeString.draw(p, {
			.position = QPoint(
				badgeLeft + (pillWidth - badgeTextWidth) / 2,
				badgeTop + badgePadding.top()),
			.availableWidth = badgeTextWidth,
		});
	} else if (badgeTextWidth > 0) {
		p.setPen(st::rankUserFg);
		badgeString.draw(p, {
			.position = QPoint(innerRight - badgeTextWidth, badgeTop),
			.availableWidth = badgeTextWidth,
		});
	}

	p.setFont(st::msgDateFont);
	const auto timeText = u"12:00"_q;
	const auto timeWidth = st::msgDateFont->width(timeText);
	const auto timeX = innerRight - timeWidth;
	const auto timeY = bubbleTop
		+ bubbleHeight
		- padding.bottom()
		+ st::msgDateFont->ascent
		- st::msgDateFont->height;
	p.setPen(stm.msgDateFg);
	p.drawText(timeX, timeY, timeText);

	{
		auto color = stm.historyTextFg->c;
		color.setAlphaF(color.alphaF() * kTextLinesAlpha);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		auto hq = PainterHighQualityEnabler(p);

		const auto lineHeight = st::tagInfoPreviewLineHeight;
		const auto lineSpacing = st::tagInfoPreviewLineSpacing;
		const auto linesTop = badgeTop
			+ pillHeight
			+ lineSpacing;
		const auto lineRadius = lineHeight / 2.0;
		const auto timeAreaLeft = timeX - padding.right();
		const auto fractions = { 1.0, 0.65, 0.65 };
		auto y = double(linesTop);
		auto lineIndex = 0;
		for (const auto fraction : fractions) {
			auto w = available * fraction;
			const auto lineBottom = y + lineHeight;
			if (lineIndex >= 1 && lineBottom > (timeY - lineSpacing)) {
				w = std::min(w, double(timeAreaLeft - innerLeft));
			}
			p.drawRoundedRect(
				QRectF(innerLeft, y, w, lineHeight),
				lineRadius,
				lineRadius);
			y += lineHeight + lineSpacing;
			++lineIndex;
		}
	}

	const auto fadeWidth = st::tagInfoPreviewFadeWidth;
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	auto gradient = QLinearGradient(0, 0, fadeWidth, 0);
	gradient.setStops({
		{ 0., QColor(255, 255, 255, 0) },
		{ 1., QColor(255, 255, 255, 255) },
	});
	p.fillRect(0, 0, fadeWidth, rect.height(), gradient);
	p.end();
}

[[nodiscard]] QString LookupCurrentRank(not_null<PeerData*> peer) {
	const auto selfId = peerToUser(peer->session().user()->id);
	if (const auto channel = peer->asMegagroup()) {
		if (const auto info = channel->mgInfo.get()) {
			const auto it = info->memberRanks.find(selfId);
			if (it != info->memberRanks.end()) {
				return it->second;
			}
		}
	} else if (const auto chat = peer->asChat()) {
		const auto it = chat->memberRanks.find(selfId);
		if (it != chat->memberRanks.end()) {
			return it->second;
		}
	}
	return QString();
}

} // namespace

void TagInfoBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		not_null<PeerData*> author,
		const QString &tagText,
		HistoryView::BadgeRole role) {
	box->setStyle(st::confcallJoinBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	box->addRow(
		MakeRoundColoredLogo(box, role),
		st::boxRowPadding + st::confcallLinkHeaderIconPadding);

	auto title = (role == BadgeRole::Creator)
		? tr::lng_tag_info_title_owner()
		: (role == BadgeRole::Admin)
		? tr::lng_tag_info_title_admin()
		: tr::lng_tag_info_title_user();
	box->addRow(
		object_ptr<Ui::FlatLabel>(box, std::move(title), st::boxTitle),
		st::boxRowPadding + st::confcallLinkTitlePadding,
		style::al_top);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto tagPill = helper.paletteDependent(
		MakeTagPillEmoji(tagText, role));
	const auto authorName = author->shortName();
	const auto groupName = peer->name();
	const auto descText = (role == BadgeRole::Creator)
		? tr::lng_tag_info_text_owner(
			tr::now,
			lt_emoji,
			tagPill,
			lt_author,
			tr::bold(authorName),
			lt_group,
			tr::bold(groupName),
			tr::rich)
		: (role == BadgeRole::Admin)
		? tr::lng_tag_info_text_admin(
			tr::now,
			lt_emoji,
			tagPill,
			lt_author,
			tr::bold(authorName),
			lt_group,
			tr::bold(groupName),
			tr::rich)
		: tr::lng_tag_info_text_user(
			tr::now,
			lt_emoji,
			tagPill,
			lt_author,
			tr::bold(authorName),
			lt_group,
			tr::bold(groupName),
			tr::rich);
	const auto context = helper.context();
	const auto desc = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(descText),
			st::confcallLinkCenteredText,
			st::defaultPopupMenu,
			context),
		st::boxRowPadding,
		style::al_top);
	desc->setTryMakeSimilarLines(true);

	box->addRow(
		object_ptr<TagPreviewsWidget>(
			box,
			&peer->session(),
			role),
		st::boxRowPadding + st::tagInfoPreviewPadding);

	const auto selfUser = peer->session().user();
	const auto selfRole = LookupBadgeRole(peer, selfUser);
	const auto isAdmin = (selfRole != BadgeRole::User);
	const auto canEditSelf = isAdmin
		|| !peer->amRestricted(ChatRestriction::EditRank);

	if (canEditSelf) {
		const auto currentRank = LookupCurrentRank(peer);
		auto buttonText = currentRank.isEmpty()
			? tr::lng_tag_info_add_my_tag()
			: tr::lng_tag_info_edit_my_tag();
		box->addButton(std::move(buttonText), [=] {
			box->closeBox();
			show->show(Box(
				EditCustomRankBox,
				show,
				peer,
				selfUser,
				currentRank,
				true,
				Fn<void(QString)>(nullptr)));
		});
	} else {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_tag_info_admins_only(),
				st::tagInfoAdminsOnlyLabel),
			st::boxRowPadding + st::tagInfoAdminsOnlyPadding,
			style::al_top);
		box->addButton(
			rpl::single(QString()),
			[=] { box->closeBox(); }
		)->setText(rpl::single(Ui::Text::IconEmoji(
			&st::infoStarsUnderstood
		).append(' ').append(
			tr::lng_stars_rating_understood(tr::now))));
	}
}
