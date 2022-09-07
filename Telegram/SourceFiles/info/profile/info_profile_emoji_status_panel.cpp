/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_emoji_status_panel.h"

#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/reactions/history_view_reactions_animation.h"
#include "lang/lang_keys.h"
#include "menu/menu_send.h" // SendMenu::Type.
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/text/format_values.h"
#include "base/unixtime.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_chat.h"

namespace Info::Profile {
namespace {

void PickUntilBox(not_null<Ui::GenericBox*> box, Fn<void(TimeId)> callback) {
	box->setTitle(tr::lng_emoji_status_for_title());

	const auto seconds = Ui::DefaultTimePickerValues();
	const auto phrases = ranges::views::all(
		seconds
	) | ranges::views::transform(Ui::FormatMuteFor) | ranges::to_vector;

	const auto pickerCallback = Ui::TimePickerBox(box, seconds, phrases, 0);

	Ui::ConfirmBox(box, {
		.confirmed = [=] {
			callback(pickerCallback());
			box->closeBox();
		},
		.confirmText = tr::lng_emoji_status_for_submit(),
		.cancelText = tr::lng_cancel(),
	});
}

} // namespace

class EmojiStatusPanel::Animation {
public:
	Animation(
		not_null<Ui::RpWidget*> body,
		not_null<Data::Reactions*> owner,
		HistoryView::Reactions::AnimationArgs &&args,
		Fn<void()> repaint,
		Data::CustomEmojiSizeTag tag);

	[[nodiscard]] not_null<Ui::RpWidget*> layer();
	[[nodiscard]] bool finished() const;

	void repaint();
	bool paintBadgeFrame(not_null<Ui::RpWidget*> widget);

private:
	const int _flySize = 0;
	HistoryView::Reactions::Animation _fly;
	Ui::RpWidget _layer;
	QRect _area;
	bool _areaUpdated = false;
	QPointer<Ui::RpWidget> _target;

};

[[nodiscard]] int ComputeFlySize(Data::CustomEmojiSizeTag tag) {
	using Tag = Data::CustomEmojiSizeTag;
	if (tag == Tag::Normal) {
		return st::reactionInlineImage;
	}
	return int(base::SafeRound(
		(st::reactionInlineImage * Data::FrameSizeFromTag(tag)
			/ float64(Data::FrameSizeFromTag(Tag::Normal)))));
}

EmojiStatusPanel::Animation::Animation(
	not_null<Ui::RpWidget*> body,
	not_null<Data::Reactions*> owner,
	HistoryView::Reactions::AnimationArgs &&args,
	Fn<void()> repaint,
	Data::CustomEmojiSizeTag tag)
: _flySize(ComputeFlySize(tag))
, _fly(
	owner,
	std::move(args),
	std::move(repaint),
	_flySize,
	tag)
, _layer(body) {
	body->sizeValue() | rpl::start_with_next([=](QSize size) {
		_layer.setGeometry(QRect(QPoint(), size));
	}, _layer.lifetime());

	_layer.paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		const auto target = _target.data();
		if (!target || !target->isVisible()) {
			return;
		}
		auto p = QPainter(&_layer);

		const auto rect = Ui::MapFrom(&_layer, target, target->rect());
		const auto skipx = (rect.width() - _flySize) / 2;
		const auto skipy = (rect.height() - _flySize) / 2;
		const auto area = _fly.paintGetArea(
			p,
			QPoint(),
			QRect(
				rect.topLeft() + QPoint(skipx, skipy),
				QSize(_flySize, _flySize)),
			st::infoPeerBadge.premiumFg->c,
			clip,
			crl::now());
		if (_areaUpdated || _area.isEmpty()) {
			_area = area;
		} else {
			_area = _area.united(area);
		}
	}, _layer.lifetime());

	_layer.setAttribute(Qt::WA_TransparentForMouseEvents);
	_layer.show();
}

not_null<Ui::RpWidget*> EmojiStatusPanel::Animation::layer() {
	return &_layer;
}

bool EmojiStatusPanel::Animation::finished() const {
	if (const auto target = _target.data()) {
		return _fly.finished() || !target->isVisible();
	}
	return true;
}

void EmojiStatusPanel::Animation::repaint() {
	if (_area.isEmpty()) {
		_layer.update();
	} else {
		_layer.update(_area);
		_areaUpdated = true;
	}
}

bool EmojiStatusPanel::Animation::paintBadgeFrame(
		not_null<Ui::RpWidget*> widget) {
	_target = widget;
	return !_fly.finished();
}

EmojiStatusPanel::EmojiStatusPanel() = default;

EmojiStatusPanel::~EmojiStatusPanel() = default;

void EmojiStatusPanel::show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> button,
		Data::CustomEmojiSizeTag animationSizeTag) {
	const auto self = controller->session().user();
	const auto &statuses = controller->session().data().emojiStatuses();
	const auto &recent = statuses.list(Data::EmojiStatuses::Type::Recent);
	const auto &other = statuses.list(Data::EmojiStatuses::Type::Default);
	auto list = statuses.list(Data::EmojiStatuses::Type::Colored);
	list.insert(begin(list), 0);
	list.reserve(list.size() + recent.size() + other.size() + 1);
	for (const auto &id : ranges::views::concat(recent, other)) {
		if (!ranges::contains(list, id)) {
			list.push_back(id);
		}
	}
	if (!ranges::contains(list, self->emojiStatusId())) {
		list.push_back(self->emojiStatusId());
	}
	if (!_panel) {
		create(controller);

		_panel->shownValue(
		) | rpl::filter([=] {
			return (_panelButton != nullptr);
		}) | rpl::start_with_next([=](bool shown) {
			if (shown) {
				_panelButton->installEventFilter(_panel.get());
			} else {
				_panelButton->removeEventFilter(_panel.get());
			}
		}, _panel->lifetime());
	}
	if (const auto previous = _panelButton.data()) {
		if (previous != button) {
			previous->removeEventFilter(_panel.get());
		}
	}
	_panelButton = button;
	_animationSizeTag = animationSizeTag;
	_panel->selector()->provideRecentEmoji(list);
	const auto parent = _panel->parentWidget();
	const auto global = button->mapToGlobal(QPoint());
	const auto local = parent->mapFromGlobal(global);
	_panel->moveTopRight(
		local.y() + button->height() - (st::normalFont->height / 2),
		local.x() + button->width() * 3);
	_panel->toggleAnimated();
}

bool EmojiStatusPanel::paintBadgeFrame(not_null<Ui::RpWidget*> widget) {
	if (!_animation) {
		return false;
	} else if (_animation->paintBadgeFrame(widget)) {
		return true;
	}
	InvokeQueued(_animation->layer(), [=] { _animation = nullptr; });
	return false;
}

void EmojiStatusPanel::create(
		not_null<Window::SessionController*> controller) {
	using Selector = ChatHelpers::TabbedSelector;
	const auto body = controller->window().widget()->bodyWidget();
	_panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		body,
		controller,
		object_ptr<Selector>(
			nullptr,
			controller,
			Window::GifPauseReason::Layer,
			ChatHelpers::TabbedSelector::Mode::EmojiStatus));
	_panel->setDropDown(true);
	_panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_panel->hide();
	_panel->selector()->setAllowEmojiWithoutPremium(false);

	struct Chosen {
		DocumentId id = 0;
		TimeId until = 0;
		Ui::MessageSendingAnimationFrom animation;
	};

	_panel->selector()->contextMenuRequested(
	) | rpl::start_with_next([=] {
		_panel->selector()->showMenuWithType(SendMenu::Type::Scheduled);
	}, _panel->lifetime());

	auto statusChosen = _panel->selector()->customEmojiChosen(
	) | rpl::map([=](Selector::FileChosen data) {
		return Chosen{
			.id = data.document->id,
			.until = data.options.scheduled,
			.animation = data.messageSendingFrom,
		};
	});

	auto emojiChosen = _panel->selector()->emojiChosen(
	) | rpl::map([=](Selector::EmojiChosen data) {
		return Chosen{ .animation = data.messageSendingFrom };
	});

	const auto set = [=](Chosen chosen) {
		Expects(chosen.until != Selector::kPickCustomTimeId);

		const auto owner = &controller->session().data();
		startAnimation(owner, body, chosen.id, chosen.animation);
		owner->emojiStatuses().set(chosen.id, chosen.until);
	};

	rpl::merge(
		std::move(statusChosen),
		std::move(emojiChosen)
	) | rpl::start_with_next([=](const Chosen chosen) {
		if (chosen.until == Selector::kPickCustomTimeId) {
			controller->show(Box(PickUntilBox, [=](TimeId seconds) {
				set({ chosen.id, base::unixtime::now() + seconds });
			}));
		} else {
			set(chosen);
			_panel->hideAnimated();
		}
	}, _panel->lifetime());

	_panel->selector()->showPromoForPremiumEmoji();
}

void EmojiStatusPanel::startAnimation(
		not_null<Data::Session*> owner,
		not_null<Ui::RpWidget*> body,
		DocumentId statusId,
		Ui::MessageSendingAnimationFrom from) {
	if (!_panelButton) {
		return;
	}
	auto args = HistoryView::Reactions::AnimationArgs{
		.id = { { statusId } },
		.flyIcon = from.frame,
		.flyFrom = body->mapFromGlobal(from.globalStartGeometry),
	};
	_animation = std::make_unique<Animation>(
		body,
		&owner->reactions(),
		std::move(args),
		[=] { _animation->repaint(); },
		_animationSizeTag);
}

} // namespace Info::Profile
