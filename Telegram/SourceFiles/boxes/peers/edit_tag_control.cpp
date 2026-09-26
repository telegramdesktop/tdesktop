/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_tag_control.h"

#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/media/history_view_media_generic.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kRankLimit = 16;
constexpr auto kTextLinesAlpha = 0.1;

using namespace HistoryView;

class TextLinesPart final : public MediaGenericPart {
public:
	TextLinesPart(QMargins margins);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	QMargins _margins;

};

TextLinesPart::TextLinesPart(QMargins margins)
: _margins(margins) {
}

QSize TextLinesPart::countOptimalSize() {
	const auto h = _margins.top()
		+ 4 * st::tagPreviewLineHeight
		+ 3 * st::tagPreviewLineSpacing
		+ _margins.bottom();
	return { st::msgMinWidth, h };
}

QSize TextLinesPart::countCurrentSize(int newWidth) {
	return { newWidth, minHeight() };
}

void TextLinesPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto &stm = context.messageStyle();
	auto color = stm->historyTextFg->c;
	color.setAlphaF(color.alphaF() * kTextLinesAlpha);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	auto hq = PainterHighQualityEnabler(p);

	const auto available = outerWidth - _margins.left() - _margins.right();
	const auto lineHeight = st::tagPreviewLineHeight;
	const auto radius = lineHeight / 2.0;
	const auto fractions = { 1.0, 0.85, 0.65, 0.4 };
	auto y = double(_margins.top());
	for (const auto fraction : fractions) {
		const auto w = available * fraction;
		p.drawRoundedRect(
			QRectF(_margins.left(), y, w, lineHeight),
			radius,
			radius);
		y += lineHeight + st::tagPreviewLineSpacing;
	}
}

class TagPreviewDelegate final : public DefaultElementDelegate {
public:
	TagPreviewDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update);

	void setTagText(const QString &text);
	[[nodiscard]] const QString &tagText() const;

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	Context elementContext() override;
	QString elementAuthorRank(not_null<const Element*> view) override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	QString _tagText;

};

TagPreviewDelegate::TagPreviewDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	Fn<void()> update)
: _parent(parent)
, _pathGradient(MakePathShiftGradient(st, std::move(update))) {
}

void TagPreviewDelegate::setTagText(const QString &text) {
	_tagText = text;
}

const QString &TagPreviewDelegate::tagText() const {
	return _tagText;
}

bool TagPreviewDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto TagPreviewDelegate::elementPathShiftGradient()
-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

Context TagPreviewDelegate::elementContext() {
	return Context::AdminLog;
}

QString TagPreviewDelegate::elementAuthorRank(
		not_null<const Element*> view) {
	return _tagText;
}

} // namespace

HistoryView::BadgeRole LookupBadgeRole(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	if (const auto channel = peer->asMegagroup()) {
		const auto info = channel->mgInfo.get();
		if (info && info->creator == user) {
			return HistoryView::BadgeRole::Creator;
		}
		const auto userId = peerToUser(user->id);
		if (info && info->admins.contains(userId)) {
			return HistoryView::BadgeRole::Admin;
		}
	} else if (const auto chat = peer->asChat()) {
		if (peerToUser(user->id) == chat->creator) {
			return HistoryView::BadgeRole::Creator;
		} else if (chat->admins.contains(user)) {
			return HistoryView::BadgeRole::Admin;
		}
	}
	return HistoryView::BadgeRole::User;
}

class EditTagControl::PreviewWidget final : public Ui::RpWidget {
public:
	PreviewWidget(
		QWidget *parent,
		not_null<Main::Session*> session,
		not_null<UserData*> user,
		const QString &initialText,
		HistoryView::BadgeRole role);
	~PreviewWidget();

	void setTagText(const QString &text);

private:
	void paintEvent(QPaintEvent *e) override;
	void createItem();
	void applyBadge(const QString &text);

	const not_null<History*> _history;
	const not_null<UserData*> _user;
	const HistoryView::BadgeRole _role;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<TagPreviewDelegate> _delegate;
	AdminLog::OwnedItem _item;
	int _topSkip = 0;
	int _bottomSkip = 0;
	Ui::PeerUserpicView _userpic;

};

EditTagControl::PreviewWidget::PreviewWidget(
	QWidget *parent,
	not_null<Main::Session*> session,
	not_null<UserData*> user,
	const QString &initialText,
	HistoryView::BadgeRole role)
: RpWidget(parent)
, _history(session->data().history(PeerData::kServiceNotificationsId))
, _user(user)
, _role(role)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(
	session->colorIndicesValue()))
, _delegate(std::make_unique<TagPreviewDelegate>(
	this,
	_style.get(),
	[=] { update(); }))
, _topSkip(st::msgMargin.bottom() * 2)
, _bottomSkip(st::msgMargin.bottom() + st::msgMargin.top()) {
	_style->apply(_theme.get());
	_delegate->setTagText(initialText);

	_history->owner().viewRepaintRequest(
	) | rpl::on_next([=](Data::RequestViewRepaint data) {
		if (data.view == _item.get()) {
			update();
		}
	}, lifetime());

	createItem();

	widthValue(
	) | rpl::filter([=](int w) {
		return w >= st::msgMinWidth;
	}) | rpl::on_next([=](int w) {
		const auto h = _topSkip
			+ _item->resizeGetHeight(w)
			+ _bottomSkip;
		resize(w, h);
	}, lifetime());

	_history->owner().itemResizeRequest(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		if (_item && item == _item->data() && width() >= st::msgMinWidth) {
			const auto h = _topSkip
				+ _item->resizeGetHeight(width())
				+ _bottomSkip;
			resize(width(), h);
		}
	}, lifetime());
}

EditTagControl::PreviewWidget::~PreviewWidget() {
	_item = {};
}

void EditTagControl::PreviewWidget::createItem() {
	const auto item = _history->addNewLocalMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::HasFromId),
		.from = _user->id,
		.date = base::unixtime::now(),
	}, TextWithEntities(), MTP_messageMediaEmpty());

	auto owned = AdminLog::OwnedItem(_delegate.get(), item);
	owned->overrideMedia(std::make_unique<HistoryView::MediaGeneric>(
		owned.get(),
		[](not_null<HistoryView::MediaGeneric*>,
				Fn<void(std::unique_ptr<HistoryView::MediaGenericPart>)> push) {
			push(std::make_unique<TextLinesPart>(st::msgPadding));
		}));
	_item = std::move(owned);
	applyBadge(_delegate->tagText());
	if (width() >= st::msgMinWidth) {
		const auto h = _topSkip
			+ _item->resizeGetHeight(width())
			+ _bottomSkip;
		resize(width(), h);
	}
}

void EditTagControl::PreviewWidget::applyBadge(const QString &text) {
	if (!_item) {
		return;
	}
	auto badgeText = text;
	if (badgeText.isEmpty()) {
		if (_role == HistoryView::BadgeRole::Admin) {
			badgeText = tr::lng_admin_badge(tr::now);
		} else if (_role == HistoryView::BadgeRole::Creator) {
			badgeText = tr::lng_owner_badge(tr::now);
		}
	}
	_item->overrideRightBadge(badgeText, _role);
}

void EditTagControl::PreviewWidget::setTagText(const QString &text) {
	_delegate->setTagText(text);
	applyBadge(text);
	update();
}

void EditTagControl::PreviewWidget::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto clip = e->rect();
	if (!clip.isEmpty()) {
		p.setClipRect(clip);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(width(), window()->height()),
			clip);
	}

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		rect(),
		e->rect(),
		!window()->isActiveWindow());
	p.translate(0, _topSkip);
	_item->draw(p, context);

	if (_item->displayFromPhoto()) {
		auto userpicBottom = height()
			- _bottomSkip
			- _item->marginBottom()
			- _item->marginTop();
		const auto userpicTop = userpicBottom - st::msgPhotoSize;
		_user->paintUserpicLeft(
			p,
			_userpic,
			st::historyPhotoLeft,
			userpicTop,
			width(),
			st::msgPhotoSize);
	}
}

EditTagControl::EditTagControl(
	QWidget *parent,
	not_null<Main::Session*> session,
	not_null<UserData*> user,
	const QString &currentRank,
	HistoryView::BadgeRole role)
: RpWidget(parent)
, _preview(Ui::CreateChild<PreviewWidget>(
	this,
	session,
	user,
	TextUtilities::RemoveEmoji(currentRank),
	role))
, _field(Ui::CreateChild<Ui::InputField>(
	this,
	st::customBadgeField,
	(role == HistoryView::BadgeRole::Admin
		? tr::lng_admin_badge()
		: role == HistoryView::BadgeRole::Creator
		? tr::lng_owner_badge()
		: tr::lng_rights_edit_admin_rank_name()),
	TextUtilities::RemoveEmoji(currentRank))) {
	_field->setMaxLength(kRankLimit);
	_field->setInstantReplaces(Ui::InstantReplaces::TextOnly());

	_field->changes(
	) | rpl::on_next([=] {
		const auto text = _field->getLastText();
		const auto removed = TextUtilities::RemoveEmoji(text);
		if (removed != text) {
			_field->setText(removed);
		}
		_preview->setTagText(_field->getLastText());
	}, _field->lifetime());

	widthValue(
	) | rpl::on_next([=](int w) {
		_preview->resizeToWidth(w);
		const auto inputMargins = st::boxRowPadding;
		_field->resizeToWidth(w - inputMargins.left() - inputMargins.right());
		_field->moveToLeft(
			inputMargins.left(),
			_preview->height() + st::tagPreviewInputSkip);
		resize(w, _field->y() + _field->height());
	}, lifetime());

	_preview->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		_field->moveToLeft(
			st::boxRowPadding.left(),
			_preview->height() + st::tagPreviewInputSkip);
		resize(width(), _field->y() + _field->height());
	}, lifetime());
}

EditTagControl::~EditTagControl() = default;

QString EditTagControl::currentRank() const {
	return TextUtilities::RemoveEmoji(
		TextUtilities::SingleLine(_field->getLastText().trimmed()));
}

not_null<Ui::InputField*> EditTagControl::field() const {
	return _field;
}
