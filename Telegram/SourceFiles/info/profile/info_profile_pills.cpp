/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_pills.h"

#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "info/profile/info_profile_balance_pill.h"
#include "info/profile/info_profile_music_button.h"
#include "info/profile/info_profile_top_bar.h"
#include "ui/text/format_song_document_name.h"
#include "ui/rect.h"
#include "wallet/wallet_card_view.h"

#include "styles/style_info.h"
#include "styles/style_info_profile_top_bar.h"

namespace Info::Profile {
namespace {

constexpr auto kFadeStart = 0.6;

[[nodiscard]] rpl::producer<DocumentData*> ProfileMusicValue(
		not_null<UserData*> user) {
	const auto savedMusic = &user->owner().savedMusic();
	const auto peerId = user->id;
	return rpl::single(
		peerId
	) | rpl::then(
		savedMusic->changed() | rpl::filter(rpl::mappers::_1 == peerId)
	) | rpl::map([=]() -> DocumentData* {
		const auto &list = savedMusic->list(peerId);
		const auto media = list.empty() ? nullptr : list.front()->media();
		return media ? media->document() : nullptr;
	}) | rpl::distinct_until_changed();
}

} // namespace

PillsRow::PillsRow(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user,
	rpl::producer<std::optional<QColor>> edgeColor,
	Fn<void()> showSavedMusic,
	bool underButtons)
: _parent(parent)
, _balance(std::make_unique<BalancePill>(
	parent,
	user,
	rpl::duplicate(edgeColor)))
, _music(std::make_unique<MusicButton>(parent)) {
	const auto balance = _balance.get();
	balance->setClickedCallback([=] {
		if (balance->actionable()) {
			Wallet::ShowCardView(controller, user, balance);
		}
	});
	const auto music = _music.get();
	music->setClickedCallback([=] {
		if (music->actionable() && showSavedMusic) {
			showSavedMusic();
		}
	});

	std::move(
		edgeColor
	) | rpl::on_next([=](std::optional<QColor> color) {
		music->setOverrideBg(color);
	}, _lifetime);

	ProfileMusicValue(
		user
	) | rpl::on_next([=](DocumentData *document) {
		if (document) {
			music->updateData({ Ui::Text::FormatSongNameFor(document) });
			music->show();
		} else {
			music->hide();
		}
	}, _lifetime);

	// Without the action buttons the row fits the gap below the status.
	if (underButtons) {
		_reserve = rpl::combine(
			balance->shownValue(),
			music->shownValue()
		) | rpl::map([](bool hasBalance, bool hasMusic) {
			return (hasBalance || hasMusic)
				? st::infoProfileTopBarPillsReserve
				: 0;
		});
	}

	rpl::merge(
		balance->naturalWidthValue() | rpl::to_empty,
		balance->shownValue() | rpl::to_empty,
		music->naturalWidthValue() | rpl::to_empty,
		music->shownValue() | rpl::to_empty
	) | rpl::on_next([=] {
		layout();
	}, _lifetime);
}

PillsRow::~PillsRow() = default;

rpl::producer<int> PillsRow::reserveValue() const {
	return _reserve.value();
}

void PillsRow::place(int top, float64 progress) {
	const auto opacity = std::clamp(
		(progress - kFadeStart) / (1. - kFadeStart),
		0.,
		1.);
	_balance->setOpacity(opacity);
	_music->setOpacity(opacity);
	_top = top;
	if (_outerWidth != _parent->width()) {
		layout();
	} else {
		_balance->move(_balance->x(), _top);
		_music->move(_music->x(), _top);
	}
}

rpl::lifetime &PillsRow::lifetime() {
	return _lifetime;
}

void PillsRow::layout() {
	_outerWidth = _parent->width();
	const auto &padding = st::infoProfileTopBarActionButtonsPadding;
	const auto available = _outerWidth - rect::m::sum::h(padding);
	if (available <= 0) {
		return;
	}
	const auto hasBalance = !_balance->isHidden();
	const auto hasMusic = !_music->isHidden();
	const auto gap = (hasBalance && hasMusic)
		? st::infoProfileTopBarPillsGap
		: 0;
	auto balanceWidth = 0;
	if (hasBalance) {
		const auto chrome = hasMusic ? _music->minimumWidth() : 0;
		_balance->resizeToNaturalWidth(
			std::max(available - gap - chrome, 0));
		const auto fitted = _balance->fittedWidth();
		if (fitted < _balance->width()) {
			_balance->resizeToWidth(fitted);
		}
		balanceWidth = _balance->width();
	}
	auto musicWidth = 0;
	if (hasMusic) {
		_music->resizeToNaturalWidth(
			std::max(available - gap - balanceWidth, 0));
		musicWidth = _music->width();
	}
	const auto left = padding.left()
		+ (available - musicWidth - gap - balanceWidth) / 2;
	_music->move(left, _top);
	_balance->move(left + musicWidth + gap, _top);
}

void TopBar::setupPills(
		not_null<Window::SessionController*> controller,
		Fn<void()> showSavedMusic) {
	const auto user = _peer->asUser();
	if (!user
		|| _savedMessages
		|| _topic
		|| !(_source == Source::Profile
			|| (_source == Source::Stories && user->isSelf()))) {
		return;
	}
	_pills = std::make_unique<PillsRow>(
		this,
		controller,
		user,
		_edgeColor.value(),
		std::move(showSavedMusic),
		_hasActions);
	_pills->reserveValue(
	) | rpl::on_next([=](int reserve) {
		applyPillsReserve(reserve);
	}, _pills->lifetime());
}

void TopBar::applyPillsReserve(int reserve) {
	const auto applied = _pillsReserve.current();
	if (applied == reserve) {
		return;
	}
	const auto base = QWidget::maximumHeight() - applied;
	QWidget::setMaximumHeight(base + reserve);
	_cachedGradient = QImage();
	_cachedClipPath = QPainterPath();
	_pillsReserve = reserve;
	updateLabelsPosition();
	update();
}

rpl::producer<int> TopBar::desiredHeightValue() const {
	return _pillsReserve.value() | rpl::map([=] {
		return QWidget::maximumHeight();
	});
}

} // namespace Info::Profile
