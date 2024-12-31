/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/choose_filter_box.h"

#include "apiwrap.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h" // primaryWindow
#include "core/ui_integration.h"
#include "data/data_chat_filters.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "ui/filter_icons.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h" // Ui::Text::Bold
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace {

[[nodiscard]] QImage Icon(const Data::ChatFilter &f) {
	constexpr auto kScale = 0.75;
	const auto icon = Ui::LookupFilterIcon(Ui::ComputeFilterIcon(f)).normal;
	const auto originalWidth = icon->width();
	const auto originalHeight = icon->height();

	const auto scaledWidth = int(originalWidth * kScale);
	const auto scaledHeight = int(originalHeight * kScale);

	auto image = QImage(
		scaledWidth * style::DevicePixelRatio(),
		scaledHeight * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);

	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);

		const auto x = int((scaledWidth - originalWidth * kScale) / 2);
		const auto y = int((scaledHeight - originalHeight * kScale) / 2);

		p.scale(kScale, kScale);
		icon->paint(p, x, y, scaledWidth, st::dialogsUnreadBgMuted->c);
		if (const auto color = f.colorIndex()) {
			p.resetTransform();
			const auto circleSize = scaledWidth / 3.;
			const auto r = QRectF(
				x + scaledWidth - circleSize,
				y + scaledHeight - circleSize - circleSize / 3.,
				circleSize,
				circleSize);
			p.setPen(Qt::NoPen);
			p.setCompositionMode(QPainter::CompositionMode_Clear);
			p.setBrush(Qt::transparent);
			p.drawEllipse(r + Margins(st::lineWidth * 1.5));
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			p.setBrush(Ui::EmptyUserpic::UserpicColor(*color).color2);
			p.drawEllipse(r);
		}
	}

	return image;
}

class FilterAction : public Ui::Menu::Action {
public:
	using Ui::Menu::Action::Action;

	void setIcon(QImage &&image) {
		_icon = std::move(image);
	}

protected:
	void paintEvent(QPaintEvent *event) override {
		Ui::Menu::Action::paintEvent(event);
		if (!_icon.isNull()) {
			const auto size = _icon.size() / style::DevicePixelRatio();
			auto p = QPainter(this);
			p.drawImage(
				width()
					- size.width()
					- st::menuWithIcons.itemPadding.right(),
				(height() - size.height()) / 2,
				_icon);
		}
	}

private:
	QImage _icon;

};

Data::ChatFilter ChangedFilter(
		const Data::ChatFilter &filter,
		not_null<History*> history,
		bool add) {
	auto always = base::duplicate(filter.always());
	auto never = base::duplicate(filter.never());
	if (add) {
		never.remove(history);
	} else {
		always.remove(history);
	}
	const auto result = Data::ChatFilter(
		filter.id(),
		filter.title(),
		filter.iconEmoji(),
		filter.colorIndex(),
		filter.flags(),
		std::move(always),
		filter.pinned(),
		std::move(never));
	const auto in = result.contains(history);
	if (in == add) {
		return result;
	}
	always = base::duplicate(result.always());
	never = base::duplicate(result.never());
	if (add) {
		always.insert(history);
	} else {
		never.insert(history);
	}
	return Data::ChatFilter(
		filter.id(),
		filter.title(),
		filter.iconEmoji(),
		filter.colorIndex(),
		filter.flags(),
		std::move(always),
		filter.pinned(),
		std::move(never));
}

void ChangeFilterById(
		FilterId filterId,
		not_null<History*> history,
		bool add) {
	Expects(filterId != 0);

	const auto list = history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (i != end(list)) {
		const auto was = *i;
		const auto filter = ChangedFilter(was, history, add);
		history->owner().chatsFilters().set(filter);
		history->session().api().request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
			MTP_int(filter.id()),
			filter.tl()
		)).done([=, chat = history->peer->name(), name = filter.title()] {
			const auto account = not_null(&history->session().account());
			if (const auto controller = Core::App().windowFor(account)) {
				const auto isStatic = name.isStatic;
				const auto textContext = [=](not_null<QWidget*> widget) {
					return Core::MarkedTextContext{
						.session = &history->session(),
						.customEmojiRepaint = [=] { widget->update(); },
						.customEmojiLoopLimit = isStatic ? -1 : 0,
					};
				};
				controller->showToast({
					.text = (add
						? tr::lng_filters_toast_add
						: tr::lng_filters_toast_remove)(
							tr::now,
							lt_chat,
							Ui::Text::Bold(chat),
							lt_folder,
							Ui::Text::Wrapped(name.text, EntityType::Bold),
							Ui::Text::WithEntities),
					.textContext = textContext,
				});
			}
		}).fail([=](const MTP::Error &error) {
			LOG(("API Error: failed to %1 a dialog to a folder. %2")
				.arg(add ? u"add"_q : u"remove"_q)
				.arg(error.type()));
			// Revert filter on fail.
			history->owner().chatsFilters().set(was);
		}).send();
	}
}

} // namespace

ChooseFilterValidator::ChooseFilterValidator(not_null<History*> history)
: _history(history) {
}

bool ChooseFilterValidator::canAdd() const {
	for (const auto &filter : _history->owner().chatsFilters().list()) {
		if (filter.id() && !filter.contains(_history)) {
			return true;
		}
	}
	return false;
}

bool ChooseFilterValidator::canRemove(FilterId filterId) const {
	Expects(filterId != 0);

	const auto list = _history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (i != end(list)) {
		return Data::CanRemoveFromChatFilter(*i, _history);
	}
	return false;
}

ChooseFilterValidator::LimitData ChooseFilterValidator::limitReached(
		FilterId filterId,
		bool always) const {
	Expects(filterId != 0);

	const auto list = _history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	const auto limit = _history->owner().pinnedChatsLimit(filterId);
	const auto &chatsList = always ? i->always() : i->never();
	return {
		.reached = (i != end(list))
			&& !ranges::contains(chatsList, _history)
			&& (chatsList.size() >= limit),
		.count = int(chatsList.size()),
	};
}

void ChooseFilterValidator::add(FilterId filterId) const {
	ChangeFilterById(filterId, _history, true);
}

void ChooseFilterValidator::remove(FilterId filterId) const {
	ChangeFilterById(filterId, _history, false);
}

void FillChooseFilterMenu(
		not_null<Window::SessionController*> controller,
		not_null<Ui::PopupMenu*> menu,
		not_null<History*> history) {
	const auto weak = base::make_weak(controller);
	const auto validator = ChooseFilterValidator(history);
	const auto &list = history->owner().chatsFilters().list();
	const auto showColors = history->owner().chatsFilters().tagsEnabled();
	for (const auto &filter : list) {
		const auto id = filter.id();
		if (!id) {
			continue;
		}

		auto callback = [=] {
			const auto toAdd = !filter.contains(history);
			const auto r = validator.limitReached(id, toAdd);
			if (r.reached) {
				controller->show(Box(
					FilterChatsLimitBox,
					&controller->session(),
					r.count,
					toAdd));
				return;
			} else if (toAdd ? validator.canAdd() : validator.canRemove(id)) {
				if (toAdd) {
					validator.add(id);
				} else {
					validator.remove(id);
				}
			}
		};

		const auto contains = filter.contains(history);
		const auto title = filter.title();
		auto item = base::make_unique_q<FilterAction>(
			menu.get(),
			st::foldersMenu,
			Ui::Menu::CreateAction(
				menu.get(),
				Ui::Text::FixAmpersandInAction(title.text.text),
				std::move(callback)),
			contains ? &st::mediaPlayerMenuCheck : nullptr,
			contains ? &st::mediaPlayerMenuCheck : nullptr);
		const auto context = Core::MarkedTextContext{
			.session = &history->session(),
			.customEmojiRepaint = [raw = item.get()] { raw->update(); },
			.customEmojiLoopLimit = title.isStatic ? -1 : 0,
		};
		item->setMarkedText(title.text, QString(), context);

		item->setIcon(Icon(showColors ? filter : filter.withColorIndex({})));
		const auto action = menu->addAction(std::move(item));
		action->setEnabled(contains
			? validator.canRemove(id)
			: validator.canAdd());
	}

	const auto limit = [session = &controller->session()] {
		return Data::PremiumLimits(session).dialogFiltersCurrent();
	};
	if ((list.size() - 1) < limit()) {
		menu->addAction(tr::lng_filters_create(tr::now), [=] {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			const auto session = &strong->session();
			const auto count = session->data().chatsFilters().list().size();
			if ((count - 1) >= limit()) {
				return;
			}
			auto filter =
				Data::ChatFilter({}, {}, {}, {}, {}, { history }, {}, {});
			const auto send = [=](const Data::ChatFilter &filter) {
				session->api().request(MTPmessages_UpdateDialogFilter(
					MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
					MTP_int(count),
					filter.tl()
				)).done([=] {
					session->data().chatsFilters().reload();
				}).send();
			};
			strong->uiShow()->show(
				Box(EditFilterBox, strong, std::move(filter), send, nullptr));
		}, &st::menuIconShowInFolder);
	}

	history->owner().chatsFilters().changed(
	) | rpl::start_with_next([=] {
		menu->hideMenu();
	}, menu->lifetime());
}
