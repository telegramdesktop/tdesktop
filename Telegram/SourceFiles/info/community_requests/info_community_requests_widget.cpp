/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/community_requests/info_community_requests_widget.h"

#include "api/api_communities.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/manage_community_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "ui/arc_angles.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/click_handler.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/animations.h"
#include "ui/effects/numbers_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace {

constexpr auto kPerPage = 100;
constexpr auto kAcceptButton = 1;
constexpr auto kRejectButton = 2;
constexpr auto kUserLink = 3;
constexpr auto kHiddenPill = 4;
constexpr auto kUndoToastDuration = crl::time(3000);
constexpr auto kPendingRowOpacity = 0.4;

[[nodiscard]] object_ptr<Ui::RpWidget> MakeUserpicToastIcon(
		not_null<PeerData*> peer,
		int size) {
	auto result = object_ptr<Ui::RpWidget>((QWidget*)nullptr);
	const auto raw = result.data();
	raw->resize(size, size);
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);

	struct State {
		std::shared_ptr<Ui::DynamicImage> image;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->image = Ui::MakeUserpicThumbnail(peer);
	state->image->subscribeToUpdates([=] { raw->update(); });

	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		p.drawImage(QRect(0, 0, size, size), state->image->image(size));
	}, raw->lifetime());

	return result;
}

[[nodiscard]] not_null<Ui::AbstractButton*> MakeUndoButton(
		not_null<QWidget*> parent,
		int width,
		const QString &text,
		rpl::producer<crl::time> finish,
		crl::time total,
		Fn<void()> click,
		Fn<void()> timeout) {
	const auto result = Ui::CreateChild<Ui::AbstractButton>(parent);
	result->setClickedCallback(std::move(click));

	struct State {
		explicit State(not_null<QWidget*> button)
		: countdown(
			st::toastUndoFont,
			[=] { button->update(); }) {
		}

		Ui::NumbersAnimation countdown;
		crl::time finish = 0;
		int secondsLeft = 0;
		Ui::Animations::Basic animation;
		Fn<void()> update;
		base::Timer timer;
	};
	const auto state = result->lifetime().make_state<State>(result);
	const auto updateLeft = [=] {
		const auto now = crl::now();
		const auto left = state->finish - now;
		if (left > 0) {
			const auto seconds = int((left + 999) / 1000);
			if (state->secondsLeft != seconds) {
				state->secondsLeft = seconds;
				state->countdown.setText(QString::number(seconds), seconds);
			}
			state->timer.callOnce((left % 1000) + 1);
		} else {
			state->animation.stop();
			state->timer.cancel();
			timeout();
		}
	};
	state->update = [=] {
		if (anim::Disabled()) {
			state->animation.stop();
		} else {
			if (!state->animation.animating()) {
				state->animation.start();
			}
			state->timer.cancel();
		}
		updateLeft();
		result->update();
	};

	result->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(result);

		const auto font = st::historyPremiumViewSet.style.font;
		const auto top = (result->height() - font->height) / 2;
		auto pen = st::historyPremiumViewSet.textFg->p;
		p.setPen(pen);
		p.setFont(font);
		p.drawText(0, top + font->ascent, text);

		const auto inner = QRect(
			width - st::toastUndoSkip - st::toastUndoDiameter,
			(result->height() - st::toastUndoDiameter) / 2,
			st::toastUndoDiameter,
			st::toastUndoDiameter);
		state->countdown.paint(
			p,
			inner.x() + (inner.width() - state->countdown.countWidth()) / 2,
			inner.y() + (inner.height() - st::toastUndoFont->height) / 2,
			width);

		const auto progress = (state->finish - crl::now()) / float64(total);
		const auto len = int(base::SafeRound(arc::kFullLength * progress));
		if (len > 0) {
			const auto from = arc::kFullLength / 4;
			auto hq = PainterHighQualityEnabler(p);
			pen.setWidthF(st::toastUndoStroke);
			p.setPen(pen);
			p.drawArc(inner, from, len);
		}
	}, result->lifetime());
	result->resize(width, st::historyPremiumViewSet.height);

	std::move(finish) | rpl::on_next([=](crl::time value) {
		state->finish = value;
		state->update();
	}, result->lifetime());
	state->animation.init(state->update);
	state->timer.setCallback(state->update);
	state->update();

	result->show();
	return result;
}

class Row;

class RowDelegate {
public:
	[[nodiscard]] virtual QSize rowAcceptButtonSize() = 0;
	[[nodiscard]] virtual QSize rowRejectButtonSize() = 0;
	virtual void rowUpdateRow(not_null<Row*> row) = 0;
	virtual void rowPaintAccept(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) = 0;
	virtual void rowPaintReject(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) = 0;
};

void MessageGroupOwnerBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> chat,
		UserData *owner,
		not_null<Window::SessionNavigation*> navigation) {
	box->setStyle(st::futureOwnerBox);
	box->setWidth(st::boxWideWidth);

	const auto content = box->verticalLayout();

	auto userpic = object_ptr<Ui::UserpicButton>(
		box,
		chat,
		st::defaultUserpicButton);
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	box->addRow(
		std::move(userpic),
		st::boxRowPadding + st::createBotUserpicPadding,
		style::al_top);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			chat->name(),
			st::boostCenteredTitle),
		st::boxRowPadding,
		style::al_top);

	const auto bot = chat->isUser();
	if (!bot) {
		Ui::AddSkip(content, st::boostTextSkip);
		const auto channel = chat->asChannel();
		const auto count = (channel && channel->membersCountKnown())
			? channel->membersCount()
			: 0;
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_chat_status_members(tr::now, lt_count, count),
				st::showOrLabel),
			st::boxRowPadding,
			style::al_top);
	}

	Ui::AddSkip(content, st::boostTextSkip);
	auto info = Ui::Text::IconEmoji(&st::requestHiddenIcon)
		.append(' ')
		.append((bot
			? tr::lng_community_request_invite_only_bot
			: tr::lng_community_request_invite_only)(tr::now));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(std::move(info)),
			st::boostText),
		st::boxRowPadding,
		style::al_top
	)->setTryMakeSimilarLines(true);

	Ui::AddSkip(content, st::messageOwnerInfoSkip);
	const auto message = content->add(
		object_ptr<Ui::RoundButton>(
			content,
			(bot
				? tr::lng_community_request_message_owner_bot()
				: tr::lng_community_request_message_owner()),
			st::defaultActiveButton),
		st::boxRowPadding,
		style::al_justify);
	message->setClickedCallback([=] {
		box->closeBox();
		if (owner) {
			navigation->showPeerHistory(
				owner,
				Window::SectionShow::Way::ClearStack,
				ShowAtUnreadMsgId);
		}
	});
	Ui::AddSkip(content);
	const auto cancel = content->add(
		object_ptr<Ui::RoundButton>(
			content,
			tr::lng_cancel(),
			st::defaultLightButton),
		st::boxRowPadding,
		style::al_justify);
	cancel->setClickedCallback([=] {
		box->closeBox();
	});
	Ui::AddSkip(content);

	for (const auto &b : { message, cancel }) {
		b->setFullRadius(true);
	}
}

class Row final : public PeerListRow {
public:
	Row(
		not_null<RowDelegate*> delegate,
		const Api::CommunityPeerRequest &request);

	[[nodiscard]] bool pending() const {
		return _pending;
	}
	void setPending(bool pending);

	[[nodiscard]] UserData *requestedBy() const {
		return _requestedBy;
	}
	[[nodiscard]] bool hiddenChat() const {
		return _hiddenChat;
	}

	float64 opacity() override;

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;

	int elementsCount() const override;
	QRect elementGeometry(int element, int outerWidth) const override;
	bool elementDisabled(int element) const override;
	bool elementOnlySelect(int element) const override;
	void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) override;
	void elementsStopLastRipple() override;
	void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) override;

private:
	struct Pills {
		QRect members;
		QRect hidden;
		int clusterWidth = 0;
	};
	[[nodiscard]] int statusTextLeft() const;
	[[nodiscard]] int pillHeight() const;
	[[nodiscard]] int pillTop() const;
	[[nodiscard]] int clusterWidth() const;
	[[nodiscard]] Pills computePills(int outerWidth) const;
	void paintPill(Painter &p, QRect pill) const;

	const not_null<RowDelegate*> _delegate;
	UserData *_requestedBy = nullptr;
	Ui::Text::String _suggest;
	Ui::Text::String _members;
	Ui::Text::String _hidden;
	bool _hiddenChat = false;
	int _userWidth = 0;
	std::unique_ptr<Ui::RippleAnimation> _acceptRipple;
	std::unique_ptr<Ui::RippleAnimation> _rejectRipple;
	bool _pending = false;
	std::shared_ptr<Ui::DynamicImage> _suggestUserpic;
	std::shared_ptr<Ui::DynamicImage> _chatUserpic;
	QImage _composedCache;
	int _composedCacheSize = 0;

};

Row::Row(
	not_null<RowDelegate*> delegate,
	const Api::CommunityPeerRequest &request)
: PeerListRow(request.peer)
, _delegate(delegate)
, _requestedBy(request.requestedBy) {
	const auto user = request.requestedBy
		? request.requestedBy->shortName()
		: QString();
	_userWidth = st::requestSuggestStyle.font->width(user);
	auto suggest = request.peer->isUser()
		? tr::lng_community_request_suggested_bot(
			tr::now,
			lt_user,
			tr::link(user),
			tr::marked)
		: request.peer->isBroadcast()
		? tr::lng_community_request_suggested_channel(
			tr::now,
			lt_user,
			tr::link(user),
			tr::marked)
		: tr::lng_community_request_suggested_group(
			tr::now,
			lt_user,
			tr::link(user),
			tr::marked);
	_suggest.setMarkedText(
		st::requestSuggestStyle,
		suggest,
		Ui::NameTextOptions());

	if (!request.peer->isUser()) {
		const auto channel = request.peer->asChannel();
		const auto count = (channel && channel->membersCountKnown())
			? channel->membersCount()
			: 0;
		auto members = Ui::Text::IconEmoji(&st::requestMembersIcon)
			.append(Lang::FormatCountToShort(count).string);
		_members.setMarkedText(
			st::requestMembersStyle,
			members,
			Ui::NameTextOptions());
	}

	_hiddenChat = !request.visible;
	if (!request.visible) {
		_hidden.setMarkedText(
			st::requestMembersStyle,
			Ui::Text::IconEmoji(&st::requestHiddenIcon),
			Ui::NameTextOptions());
	}

	_chatUserpic = Ui::MakeUserpicThumbnail(request.peer);
	if (request.requestedBy) {
		_suggestUserpic = Ui::MakeUserpicThumbnail(request.requestedBy);
	}
	const auto invalidate = [this] {
		_composedCache = QImage();
		_delegate->rowUpdateRow(this);
	};
	_chatUserpic->subscribeToUpdates(invalidate);
	if (_suggestUserpic) {
		_suggestUserpic->subscribeToUpdates(invalidate);
	}
}

PaintRoundImageCallback Row::generatePaintUserpicCallback(bool forceRound) {
	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		if (_composedCache.isNull() || _composedCacheSize != size) {
			_composedCacheSize = size;
			const auto badge = st::requestSuggestBadgeSize;
			const auto border = st::requestSuggestBadgeBorder;
			const auto &skip = st::requestSuggestBadgeSkip;
			const auto bx = size - skip.x() - badge;
			const auto by = size - skip.y() - badge;
			// The badge may extend past the userpic edge (negative skip);
			// grow the canvas so it overhangs into the row instead of clipping.
			const auto overRight = _suggestUserpic
				? std::max(bx + badge - size, 0)
				: 0;
			const auto overBottom = _suggestUserpic
				? std::max(by + badge - size, 0)
				: 0;
			const auto ratio = style::DevicePixelRatio();
			const auto full = QSize(size + overRight, size + overBottom);
			_composedCache = QImage(
				full * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_composedCache.setDevicePixelRatio(ratio);
			_composedCache.fill(Qt::transparent);
			auto q = QPainter(&_composedCache);
			q.drawImage(QRect(0, 0, size, size), _chatUserpic->image(size));
			if (_suggestUserpic) {
				auto hq = PainterHighQualityEnabler(q);
				const auto cut = QRectF(
					bx - border,
					by - border,
					badge + 2 * border,
					badge + 2 * border);

				q.setCompositionMode(QPainter::CompositionMode_Source);
				auto pen = QPen(Qt::transparent);
				pen.setWidthF(border);
				q.setPen(pen);
				q.setBrush(Qt::transparent);
				q.drawEllipse(cut);

				q.setCompositionMode(QPainter::CompositionMode_SourceOver);
				q.drawImage(
					QRectF(bx, by, badge, badge),
					_suggestUserpic->image(badge));
			}
		}
		const auto ratio = style::DevicePixelRatio();
		p.drawImage(
			QRect(
				x,
				y,
				_composedCache.width() / ratio,
				_composedCache.height() / ratio),
			_composedCache);
	};
}

int Row::statusTextLeft() const {
	return st::requestSuggestPosition.x();
}

int Row::pillHeight() const {
	const auto &padding = st::requestMembersPadding;
	return padding.top()
		+ st::requestMembersStyle.font->height
		+ padding.bottom();
}

int Row::pillTop() const {
	return st::requestMembersTop - st::requestMembersPadding.top();
}

int Row::clusterWidth() const {
	const auto &padding = st::requestMembersPadding;
	auto result = 0;
	if (!_members.isEmpty()) {
		result += padding.left() + _members.maxWidth() + padding.right();
	}
	if (!_hidden.isEmpty()) {
		if (result > 0) {
			result += st::requestPillsSkip;
		}
		result += padding.left() + _hidden.maxWidth() + padding.right();
	}
	return result;
}

Row::Pills Row::computePills(int outerWidth) const {
	const auto &padding = st::requestMembersPadding;
	const auto top = pillTop();
	const auto height = pillHeight();
	auto result = Pills();
	result.clusterWidth = clusterWidth();
	auto right = outerWidth - st::requestMembersRightSkip;
	if (!_members.isEmpty()) {
		const auto width = padding.left()
			+ _members.maxWidth()
			+ padding.right();
		result.members = QRect(right - width, top, width, height);
		right -= width + st::requestPillsSkip;
	}
	if (!_hidden.isEmpty()) {
		const auto width = padding.left()
			+ _hidden.maxWidth()
			+ padding.right();
		result.hidden = QRect(right - width, top, width, height);
	}
	return result;
}

QSize Row::rightActionSize() const {
	const auto width = clusterWidth();
	return width ? QSize(width, pillHeight()) : QSize();
}

QMargins Row::rightActionMargins() const {
	return QMargins(0, pillTop(), st::requestMembersRightSkip, 0);
}

void Row::paintPill(Painter &p, QRect pill) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = pill.height() / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(pill, radius, radius);
}

void Row::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	const auto now = crl::now();
	const auto grey = selected ? st.statusFgOver : st.statusFg;

	p.setPen(grey);
	_suggest.draw(p, {
		.position = QPoint(statusTextLeft(), st::requestSuggestPosition.y()),
		.availableWidth = availableWidth,
		.palette = &st::defaultTextPalette,
		.now = now,
		.elisionLines = 1,
	});

	const auto pills = computePills(outerWidth);
	const auto &padding = st::requestMembersPadding;
	if (!_members.isEmpty()) {
		paintPill(p, pills.members);
		p.setPen(grey);
		_members.draw(p, {
			.position = QPoint(
				pills.members.x() + padding.left(),
				st::requestMembersTop),
			.availableWidth = _members.maxWidth(),
			.palette = &st::defaultTextPalette,
			.now = now,
			.elisionLines = 1,
		});
	}
	if (!_hidden.isEmpty()) {
		paintPill(p, pills.hidden);
		p.setPen(grey);
		_hidden.draw(p, {
			.position = QPoint(
				pills.hidden.x() + padding.left(),
				st::requestMembersTop),
			.availableWidth = _hidden.maxWidth(),
			.palette = &st::defaultTextPalette,
			.now = now,
			.elisionLines = 1,
		});
	}
}

void Row::setPending(bool pending) {
	if (_pending == pending) {
		return;
	}
	_pending = pending;
	if (_pending) {
		elementsStopLastRipple();
	}
	_delegate->rowUpdateRow(this);
}

float64 Row::opacity() {
	return _pending ? kPendingRowOpacity : 1.;
}

int Row::elementsCount() const {
	return !_hidden.isEmpty()
		? kHiddenPill
		: _requestedBy
		? kUserLink
		: kRejectButton;
}

QRect Row::elementGeometry(int element, int outerWidth) const {
	switch (element) {
	case kAcceptButton: {
		const auto size = _delegate->rowAcceptButtonSize();
		return QRect(st::communityRequestAcceptPosition, size);
	} break;
	case kRejectButton: {
		const auto accept = _delegate->rowAcceptButtonSize();
		const auto size = _delegate->rowRejectButtonSize();
		return QRect(
			(st::communityRequestAcceptPosition
				+ QPoint(accept.width() + st::requestButtonsSkip, 0)),
			size);
	} break;
	case kUserLink: {
		return QRect(
			statusTextLeft(),
			st::requestSuggestPosition.y(),
			_userWidth,
			st::requestSuggestStyle.font->height);
	} break;
	case kHiddenPill: {
		return _hidden.isEmpty()
			? QRect()
			: computePills(outerWidth).hidden;
	} break;
	}
	return QRect();
}

bool Row::elementDisabled(int element) const {
	return (element != kUserLink) && (element != kHiddenPill) && _pending;
}

bool Row::elementOnlySelect(int element) const {
	return true;
}

void Row::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	const auto pointer = (element == kAcceptButton)
		? &_acceptRipple
		: (element == kRejectButton)
		? &_rejectRipple
		: nullptr;
	if (!pointer) {
		return;
	}
	auto &ripple = *pointer;
	if (!ripple) {
		auto mask = Ui::RippleAnimation::RoundRectMask(
			(element == kAcceptButton
				? _delegate->rowAcceptButtonSize()
				: _delegate->rowRejectButtonSize()),
			st::requestsAcceptButton.height / 2);
		ripple = std::make_unique<Ui::RippleAnimation>(
			(element == kAcceptButton
				? st::requestsAcceptButton.ripple
				: st::requestsRejectButton.ripple),
			std::move(mask),
			std::move(updateCallback));
	}
	ripple->add(point);
}

void Row::elementsStopLastRipple() {
	if (_acceptRipple) {
		_acceptRipple->lastStop();
	}
	if (_rejectRipple) {
		_rejectRipple->lastStop();
	}
}

void Row::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	if (_pending) {
		return;
	}
	const auto accept = elementGeometry(kAcceptButton, outerWidth);
	const auto reject = elementGeometry(kRejectButton, outerWidth);

	const auto over = [&](int element) {
		return (selectedElement == element);
	};
	_delegate->rowPaintAccept(
		p,
		accept,
		_acceptRipple,
		outerWidth,
		over(kAcceptButton));
	_delegate->rowPaintReject(
		p,
		reject,
		_rejectRipple,
		outerWidth,
		over(kRejectButton));
}

struct PendingAction {
	enum class Stage {
		Pending,
		Performed,
		Undone,
	};
	not_null<Row*> row;
	not_null<PeerData*> peer;
	bool reject = false;
	Stage stage = Stage::Pending;
};

class Controller final
	: public PeerListController
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	Controller(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> community);

	Main::Session &session() const override;
	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowElementClicked(
		not_null<PeerListRow*> row,
		int element) override;
	void rowElementHovered(
		not_null<PeerListRow*> row,
		int element,
		QRect elementRect) override;

	void setToastParent(not_null<QWidget*> parent) {
		_toastParent = parent;
	}
	void setTooltipParent(not_null<Ui::RpWidget*> parent) {
		_tooltipParent = parent;
	}
	void setListWidget(not_null<Ui::RpWidget*> list) {
		_listWidget = list;
	}
	void flushPendingOnClose();

	QSize rowAcceptButtonSize() override;
	QSize rowRejectButtonSize() override;
	void rowUpdateRow(not_null<Row*> row) override;
	void rowPaintAccept(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) override;
	void rowPaintReject(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) override;

private:
	void paintButton(
		Painter &p,
		QRect geometry,
		const style::RoundButton &st,
		const Ui::RoundRect &rect,
		const Ui::RoundRect &rectOver,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		const QString &text,
		int textWidth,
		int outerWidth,
		bool over);
	void startUndoable(not_null<PeerListRow*> row, bool reject);
	void performNow(const std::shared_ptr<PendingAction> &action);
	void hideCurrentToast();

	const not_null<Window::SessionNavigation*> _navigation;
	const not_null<ChannelData*> _community;
	QPointer<QWidget> _toastParent;
	QPointer<Ui::RpWidget> _tooltipParent;
	QPointer<Ui::RpWidget> _listWidget;
	base::unique_qptr<Ui::ImportantTooltip> _hiddenTooltip;
	base::weak_ptr<Ui::Toast::Instance> _toast;
	std::vector<std::shared_ptr<PendingAction>> _pending;
	std::shared_ptr<PendingAction> _current;

	QString _offset;
	bool _allLoaded = false;
	bool _loading = false;

	Ui::RoundRect _acceptRect;
	Ui::RoundRect _acceptRectOver;
	Ui::RoundRect _rejectRect;
	Ui::RoundRect _rejectRectOver;
	QString _acceptText;
	QString _rejectText;
	int _acceptTextWidth = 0;
	int _rejectTextWidth = 0;

};

Controller::Controller(
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> community)
: _navigation(navigation)
, _community(community)
, _acceptRect(
	st::requestsAcceptButton.height / 2,
	st::requestsAcceptButton.textBg)
, _acceptRectOver(
	st::requestsAcceptButton.height / 2,
	st::requestsAcceptButton.textBgOver)
, _rejectRect(
	st::requestsAcceptButton.height / 2,
	st::requestsRejectButton.textBg)
, _rejectRectOver(
	st::requestsAcceptButton.height / 2,
	st::requestsRejectButton.textBgOver)
, _acceptText(tr::lng_community_request_add(tr::now))
, _rejectText(tr::lng_community_request_decline(tr::now))
, _acceptTextWidth(st::requestsAcceptButton.style.font->width(_acceptText))
, _rejectTextWidth(st::requestsRejectButton.style.font->width(_rejectText)) {
	setStyleOverrides(&st::communityRequestsBoxList);
}

Main::Session &Controller::session() const {
	return _community->session();
}

void Controller::prepare() {
	delegate()->peerListSetTitle(tr::lng_community_requests_title());
	loadMoreRows();
}

void Controller::loadMoreRows() {
	if (_allLoaded || _loading) {
		return;
	}
	_loading = true;
	session().api().communities().requestPeerLinkRequests(
		_community,
		_offset,
		kPerPage,
		crl::guard(this, [=](Api::CommunityPeerRequestsSlice slice) {
			_loading = false;
			for (const auto &request : slice.list) {
				if (!delegate()->peerListFindRow(request.peer->id.value)) {
					delegate()->peerListAppendRow(
						std::make_unique<Row>(this, request));
				}
			}
			delegate()->peerListRefreshRows();
			_offset = slice.nextOffset;
			if (_offset.isEmpty()) {
				_allLoaded = true;
			}
		}));
}

void Controller::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	const auto raw = static_cast<Row*>(row.get());
	if (raw->pending()) {
		return;
	}
	if (raw->hiddenChat()) {
		_navigation->uiShow()->showBox(Box(
			MessageGroupOwnerBox,
			raw->peer(),
			raw->requestedBy(),
			_navigation));
		return;
	}
	const auto peer = row->peer();
	if (const auto window = _navigation->parentController()) {
		window->showPeer(peer);
	}
}

void Controller::rowElementClicked(
		not_null<PeerListRow*> row,
		int element) {
	if (element == kAcceptButton) {
		startUndoable(row, false);
	} else if (element == kRejectButton) {
		startUndoable(row, true);
	} else if (element == kUserLink) {
		const auto raw = static_cast<Row*>(row.get());
		if (const auto user = raw->requestedBy()) {
			_navigation->uiShow()->showBox(
				PrepareShortInfoBox(user, _navigation));
		}
	}
}

void Controller::rowElementHovered(
		not_null<PeerListRow*> row,
		int element,
		QRect elementRect) {
	if (element != kHiddenPill
		|| elementRect.isEmpty()
		|| !_tooltipParent
		|| !_listWidget) {
		if (_hiddenTooltip) {
			_hiddenTooltip->toggleFast(false);
			_hiddenTooltip = nullptr;
		}
		return;
	}
	const auto parent = _tooltipParent.data();
	_hiddenTooltip.reset(Ui::CreateChild<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			tr::lng_community_request_only_members(tr::marked),
			st::boxWideWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip));
	_hiddenTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	_hiddenTooltip->toggleFast(false);
	const auto anchor = Ui::MapFrom(parent, _listWidget.data(), elementRect);
	_hiddenTooltip->pointAt(anchor, RectPart::Top | RectPart::Center);
	_hiddenTooltip->raise();
	_hiddenTooltip->toggleAnimated(true);
}

void Controller::startUndoable(not_null<PeerListRow*> row, bool reject) {
	hideCurrentToast();

	const auto raw = static_cast<Row*>(row.get());
	const auto peer = row->peer();
	raw->setPending(true);

	const auto action = std::make_shared<PendingAction>(PendingAction{
		.row = raw,
		.peer = peer,
		.reject = reject,
	});
	_pending.push_back(action);
	_current = action;

	if (!_toastParent) {
		performNow(action);
		return;
	}

	auto text = (reject
		? tr::lng_community_request_declined_toast_undo
		: tr::lng_community_request_added_toast_undo)(
			tr::now,
			lt_chat,
			tr::bold(peer->name()),
			tr::rich);

	const auto &st = st::historyPremiumToast;
	const auto size = st.style.font->height * 2;
	const auto undoText = tr::lng_paid_react_undo(tr::now);
	const auto undoFont = st::historyPremiumViewSet.style.font;
	const auto rightSkip = undoFont->width(undoText)
		+ st::toastUndoSpace
		+ st::toastUndoDiameter
		+ st::toastUndoSkip
		- st.padding.right();
	const auto total = kUndoToastDuration;
	const auto finish = crl::now() + total;

	_toast = Ui::Toast::Show(_toastParent.data(), Ui::Toast::Config{
		.text = std::move(text),
		.iconContent = MakeUserpicToastIcon(peer, size),
		.padding = rpl::single(QMargins(0, 0, rightSkip, 0)),
		.st = &st,
		.attach = RectPart::Bottom,
		.acceptinput = true,
		.infinite = true,
	});
	const auto strong = _toast.get();
	if (!strong) {
		performNow(action);
		return;
	}
	const auto widget = strong->widget();
	widget->lifetime().add(crl::guard(this, [=] {
		performNow(action);
	}));

	const auto hideToast = [weak = _toast] {
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
	};
	const auto undo = [=] {
		if (action->stage != PendingAction::Stage::Pending) {
			return;
		}
		action->stage = PendingAction::Stage::Undone;
		action->row->setPending(false);
		if (const auto strong = _toast.get()) {
			strong->hideAnimated();
		}
		if (_current == action) {
			_current = nullptr;
		}
		_pending.erase(
			ranges::remove(_pending, action),
			end(_pending));
	};
	const auto button = MakeUndoButton(
		widget.get(),
		rightSkip + st.padding.right(),
		undoText,
		rpl::single(finish),
		total,
		undo,
		hideToast);
	rpl::combine(
		widget->sizeValue(),
		button->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize inner) {
		button->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
	}, widget->lifetime());
}

void Controller::performNow(const std::shared_ptr<PendingAction> &action) {
	if (action->stage != PendingAction::Stage::Pending) {
		return;
	}
	action->stage = PendingAction::Stage::Performed;
	if (_current == action) {
		_current = nullptr;
	}
	_pending.erase(ranges::remove(_pending, action), end(_pending));
	const auto peer = action->peer;
	const auto id = peer->id.value;
	const auto reject = action->reject;
	session().api().communities().togglePeerLinkRequestApproval(
		_community,
		peer,
		reject,
		crl::guard(this, [=] {
			if (const auto row = delegate()->peerListFindRow(id)) {
				delegate()->peerListRemoveRow(row);
				delegate()->peerListRefreshRows();
			}
		}),
		crl::guard(this, [=](const QString &error) {
			const auto show = delegate()->peerListUiShow();
			if (error == Api::kCommunityPeersTooMuch.utf16()) {
				show->showToast(Api::CommunityPeersLimitToast(peer));
			} else {
				show->showToast(error);
			}
		}));
}

void Controller::hideCurrentToast() {
	// Perform the outgoing action explicitly instead of relying on the
	// fading toast's destruction callback, which may never fire if the
	// box is closed before the fade finishes.
	if (const auto action = base::take(_current)) {
		performNow(action);
	}
	if (const auto strong = base::take(_toast).get()) {
		strong->hideAnimated();
	}
}

void Controller::flushPendingOnClose() {
	_current = nullptr;
	for (const auto &action : base::take(_pending)) {
		performNow(action);
	}
	if (const auto strong = base::take(_toast).get()) {
		strong->hideAnimated();
	}
}

QSize Controller::rowAcceptButtonSize() {
	const auto &st = st::requestsAcceptButton;
	return {
		(st.width <= 0) ? (_acceptTextWidth - st.width) : st.width,
		st.height,
	};
}

QSize Controller::rowRejectButtonSize() {
	const auto &st = st::requestsRejectButton;
	return {
		(st.width <= 0) ? (_rejectTextWidth - st.width) : st.width,
		st.height,
	};
}

void Controller::rowPaintAccept(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) {
	paintButton(
		p,
		geometry,
		st::requestsAcceptButton,
		_acceptRect,
		_acceptRectOver,
		ripple,
		_acceptText,
		_acceptTextWidth,
		outerWidth,
		over);
}

void Controller::rowPaintReject(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) {
	paintButton(
		p,
		geometry,
		st::requestsRejectButton,
		_rejectRect,
		_rejectRectOver,
		ripple,
		_rejectText,
		_rejectTextWidth,
		outerWidth,
		over);
}

void Controller::paintButton(
		Painter &p,
		QRect geometry,
		const style::RoundButton &st,
		const Ui::RoundRect &rect,
		const Ui::RoundRect &rectOver,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		const QString &text,
		int textWidth,
		int outerWidth,
		bool over) {
	rect.paint(p, geometry);
	if (over) {
		rectOver.paint(p, geometry);
	}
	if (ripple) {
		ripple->paint(p, geometry.x(), geometry.y(), outerWidth);
		if (ripple->empty()) {
			ripple = nullptr;
		}
	}
	p.setFont(st.style.font);
	p.setPen(over ? st.textFgOver : st.textFg);
	p.drawTextLeft(
		geometry.x() + (geometry.width() - textWidth) / 2,
		geometry.y() + st.textTop,
		outerWidth,
		text);
}

} // namespace

namespace Info::CommunityRequests {

class InnerWidget final
	: public Ui::VerticalLayout
	, private PeerListContentDelegate {
public:
	InnerWidget(
		QWidget *parent,
		not_null<::Info::Controller*> controller,
		not_null<::Controller*> listController,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	// PeerListContentDelegate interface.
	void peerListSetTitle(rpl::producer<QString> title) override {
	}
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override {
	}
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override {
		return false;
	}
	int peerListSelectedRowsCount() override {
		return 0;
	}
	void peerListScrollToTop() override {
	}
	void peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) override {
		Unexpected("Item selection in Info::CommunityRequests.");
	}
	void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) override {
		Unexpected("Item selection in Info::CommunityRequests.");
	}
	void peerListFinishSelectedRowsBunch() override {
	}
	void peerListSetDescription(
			object_ptr<Ui::FlatLabel> description) override {
		description.destroy();
	}
	std::shared_ptr<Main::SessionShow> peerListUiShow() override {
		return _show;
	}

	const std::shared_ptr<Main::SessionShow> _show;
	const not_null<::Info::Controller*> _controller;
	const not_null<::Controller*> _listController;
	const not_null<PeerData*> _peer;
	PeerListContent *_list = nullptr;

};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<::Info::Controller*> controller,
	not_null<::Controller*> listController,
	not_null<PeerData*> peer)
: Ui::VerticalLayout(parent)
, _show(controller->uiShow())
, _controller(controller)
, _listController(listController)
, _peer(peer) {
	const auto community = peer->asChannel();
	Assert(community != nullptr);

	const auto divider = Ui::AddDividerText(
		this,
		tr::lng_community_requests_about(
			lt_link,
			tr::lng_community_requests_about_link() | rpl::map(tr::link),
			tr::marked));
	divider->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		ShowManageCommunityBox(_controller->parentController(), community);
	}));

	Ui::AddSubsectionTitle(
		this,
		tr::lng_community_requests_count(
			lt_count,
			Info::Profile::PendingRequestsCountValue(
				community
			) | rpl::map([](int count) {
				return float64(std::max(count, 1));
			})),
		st::communityRequestsTitlePadding);

	_list = add(object_ptr<PeerListContent>(this, _listController));
	setContent(_list);
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));
	_listController->setListWidget(_list);
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(peer, nullptr, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::CommunityRequests);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, peer());
	result->setInternalState(geometry, this);
	return result;
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: ContentWidget(parent, controller)
, _listController(std::make_unique<::Controller>(
	controller,
	peer->asChannel())) {
	const auto raw = static_cast<::Controller*>(_listController.get());
	raw->setToastParent(this);
	raw->setTooltipParent(this);

	_inner = setInnerWidget(
		object_ptr<InnerWidget>(this, controller, raw, peer));

	_bottom = setupBottomBar();
}

Widget::~Widget() {
	static_cast<::Controller*>(_listController.get())->flushPendingOnClose();
}

std::unique_ptr<Ui::RpWidget> Widget::setupBottomBar() {
	auto result = std::make_unique<Ui::VerticalLayout>(this);
	const auto wrap = result.get();

	const auto row = wrap->add(
		object_ptr<Ui::RpWidget>(wrap),
		st::requestsSectionBottomMargin);
	const auto declineAll = Ui::CreateChild<Ui::RoundButton>(
		row,
		tr::lng_community_requests_decline_all(),
		st::requestsDeclineAllButton);
	const auto addAll = Ui::CreateChild<Ui::RoundButton>(
		row,
		tr::lng_community_requests_add_all(),
		st::requestsAddAllButton);
	declineAll->setClickedCallback([=] { processAll(true); });
	addAll->setClickedCallback([=] { processAll(false); });
	declineAll->setFullRadius(true);
	addAll->setFullRadius(true);

	row->resize(row->width(), st::requestsAddAllButton.height);
	rpl::combine(
		row->widthValue(),
		declineAll->widthValue(),
		addAll->widthValue()
	) | rpl::on_next([=](int width, int, int) {
		declineAll->moveToLeft(0, 0, width);
		addAll->moveToRight(0, 0, width);
	}, row->lifetime());

	widthValue() | rpl::on_next([=](int width) {
		wrap->resizeToWidth(width);
	}, wrap->lifetime());

	rpl::combine(
		wrap->heightValue(),
		heightValue()
	) | rpl::on_next([=] {
		updateBottomBarGeometry();
	}, wrap->lifetime());

	return result;
}

void Widget::updateBottomBarGeometry() {
	if (!_bottom) {
		return;
	}
	const auto height = _bottom->height();
	const auto fullHeight = this->height();
	setScrollBottomSkip(height - st::boxRadius);
	_bottom->move(0, fullHeight - height + st::boxRadius);
}

void Widget::processAll(bool reject) {
	const auto raw = static_cast<::Controller*>(_listController.get());
	const auto community = peer()->asChannel();
	const auto controller = this->controller();
	const auto count = std::max(community->pendingRequestsCount(), 1);
	const auto sure = crl::guard(this, [=](Fn<void()> &&close) {
		close();
		raw->flushPendingOnClose();
		community->session().api().communities(
		).toggleAllPeerLinkRequestApproval(
			community,
			reject,
			crl::guard(this, [=] {
				const auto show = controller->uiShow();
				controller->showBackFromStack();
				show->showToast(reject
					? tr::lng_community_request_declined_toast(
						tr::now,
						lt_count,
						count)
					: tr::lng_community_request_added_toast(
						tr::now,
						lt_count,
						count));
			}),
			crl::guard(this, [=](const QString &error) {
				const auto show = controller->uiShow();
				if (error == Api::kCommunityPeersTooMuch.utf16()) {
					show->showToast(tr::lng_community_peers_limit(
						tr::now,
						lt_count,
						Api::CommunityPeersLimit(&community->session())));
				} else {
					show->showToast(error);
				}
			}));
	});
	controller->uiShow()->showBox(Ui::MakeConfirmBox({
		.text = (reject
			? tr::lng_community_requests_decline_all_sure(
				tr::now,
				lt_count,
				count)
			: tr::lng_community_requests_add_all_sure(
				tr::now,
				lt_count,
				count)),
		.confirmed = sure,
		.confirmText = (reject
			? tr::lng_community_request_decline()
			: tr::lng_community_request_add()),
		.confirmStyle = (reject
			? &st::attentionBoxButton
			: nullptr),
		.title = (reject
			? tr::lng_community_requests_decline_all_title()
			: tr::lng_community_requests_add_all_title()),
	}));
}

rpl::producer<QString> Widget::title() {
	return tr::lng_community_requests_title();
}

void Widget::showFinished() {
	ContentWidget::showFinished();
	updateBottomBarGeometry();
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto my = dynamic_cast<Memento*>(memento.get())) {
		if (my->peer() == peer()) {
			restoreState(my);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(peer());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
}

void Widget::restoreState(not_null<Memento*> memento) {
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::CommunityRequests
