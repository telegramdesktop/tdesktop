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
#include "lang/lang_keys.h"
#include "menu/menu_send.h" // SendMenu::Type.
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/effects/emoji_fly_animation.h"
#include "ui/text/format_values.h"
#include "base/unixtime.h"
#include "boxes/premium_preview_box.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "styles/style_chat_helpers.h"

namespace Info::Profile {
namespace {

constexpr auto kLimitFirstRow = 8;

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

EmojiStatusPanel::EmojiStatusPanel() = default;

EmojiStatusPanel::~EmojiStatusPanel() = default;

void EmojiStatusPanel::setChooseFilter(Fn<bool(DocumentId)> filter) {
	_chooseFilter = std::move(filter);
}

void EmojiStatusPanel::setChooseCallback(Fn<void(DocumentId)> callback) {
	_chooseCallback = std::move(callback);
}

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
	if (list.size() > kLimitFirstRow) {
		list.erase(begin(list) + kLimitFirstRow, end(list));
	}
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
	) | rpl::map([=](ChatHelpers::FileChosen data) {
		return Chosen{
			.id = data.document->id,
			.until = data.options.scheduled,
			.animation = data.messageSendingFrom,
		};
	});

	auto emojiChosen = _panel->selector()->emojiChosen(
	) | rpl::map([=](ChatHelpers::EmojiChosen data) {
		return Chosen{ .animation = data.messageSendingFrom };
	});

	const auto accept = [=](Chosen chosen) {
		Expects(chosen.until != Selector::kPickCustomTimeId);

		const auto owner = &controller->session().data();
		startAnimation(owner, body, chosen.id, chosen.animation);
		if (_chooseCallback) {
			_chooseCallback(chosen.id);
		} else {
			owner->emojiStatuses().set(chosen.id, chosen.until);
		}
	};

	rpl::merge(
		std::move(statusChosen),
		std::move(emojiChosen)
	) | rpl::filter([=](const Chosen &chosen) {
		return filter(controller, chosen.id);
	}) | rpl::start_with_next([=](const Chosen &chosen) {
		if (chosen.until == Selector::kPickCustomTimeId) {
			_panel->hideAnimated();
			controller->show(Box(PickUntilBox, [=](TimeId seconds) {
				accept({ chosen.id, base::unixtime::now() + seconds });
			}));
		} else {
			accept(chosen);
			_panel->hideAnimated();
		}
	}, _panel->lifetime());
}

bool EmojiStatusPanel::filter(
		not_null<Window::SessionController*> controller,
		DocumentId chosenId) const {
	if (_chooseFilter) {
		return _chooseFilter(chosenId);
	} else if (chosenId && !controller->session().premium()) {
		ShowPremiumPreviewBox(controller, PremiumPreview::EmojiStatus);
		return false;
	}
	return true;
}

void EmojiStatusPanel::startAnimation(
		not_null<Data::Session*> owner,
		not_null<Ui::RpWidget*> body,
		DocumentId statusId,
		Ui::MessageSendingAnimationFrom from) {
	if (!_panelButton || !statusId) {
		return;
	}
	auto args = Ui::ReactionFlyAnimationArgs{
		.id = { { statusId } },
		.flyIcon = from.frame,
		.flyFrom = body->mapFromGlobal(from.globalStartGeometry),
	};
	_animation = std::make_unique<Ui::EmojiFlyAnimation>(
		body,
		&owner->reactions(),
		std::move(args),
		[=] { _animation->repaint(); },
		_animationSizeTag);
}

} // namespace Info::Profile
