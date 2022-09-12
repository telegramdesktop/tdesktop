/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions_tabs.h"

#include "ui/rp_widget.h"
#include "ui/abstract_button.h"
#include "ui/controls/who_reacted_context_action.h"
#include "data/data_message_reaction_id.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

using ::Data::ReactionId;

not_null<Ui::AbstractButton*> CreateTab(
		not_null<QWidget*> parent,
		const style::MultiSelect &st,
		const Ui::Text::CustomEmojiFactory &factory,
		Fn<bool()> paused,
		const ReactionId &reaction,
		Ui::WhoReadType whoReadType,
		int count,
		rpl::producer<bool> selected) {
	struct State {
		std::unique_ptr<Ui::Text::CustomEmoji> custom;
		QImage cache;
		bool selected = false;
	};
	const auto stm = &st.item;
	const auto text = QString("%L1").arg(count);
	const auto font = st::semiboldFont;
	const auto textWidth = font->width(text);
	const auto result = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	const auto width = stm->height
		+ stm->padding.left()
		+ textWidth
		+ stm->padding.right();
	result->resize(width, stm->height);
	const auto state = result->lifetime().make_state<State>();
	std::move(
		selected
	) | rpl::start_with_next([=](bool selected) {
		state->selected = selected;
		state->cache = QImage();
		result->update();
	}, result->lifetime());

	state->custom = reaction.empty()
		? nullptr
		: factory(
			Data::ReactionEntityData(reaction),
			[=] { result->update(); });

	result->paintRequest(
	) | rpl::start_with_next([=] {
		const auto factor = style::DevicePixelRatio();
		const auto height = stm->height;
		const auto skip = st::reactionsTabIconSkip;
		const auto icon = QRect(skip, 0, height, height);
		if (state->cache.isNull()) {
			state->cache = QImage(
				result->size() * factor,
				QImage::Format_ARGB32_Premultiplied);
			state->cache.setDevicePixelRatio(factor);
			state->cache.fill(Qt::transparent);
			auto p = QPainter(&state->cache);

			const auto height = stm->height;
			const auto radius = height / 2;
			p.setPen(Qt::NoPen);
			p.setBrush(state->selected ? stm->textActiveBg : stm->textBg);
			{
				PainterHighQualityEnabler hq(p);
				p.drawRoundedRect(result->rect(), radius, radius);
			}
			const auto skip = st::reactionsTabIconSkip;
			const auto icon = QRect(skip, 0, height, height);
			if (!state->custom) {
				using Type = Ui::WhoReadType;
				(reaction.emoji().isEmpty()
					? (state->selected
						? st::reactionsTabAllSelected
						: st::reactionsTabAll)
					: (whoReadType == Type::Watched
						|| whoReadType == Type::Listened)
					? (state->selected
						? st::reactionsTabPlayedSelected
						: st::reactionsTabPlayed)
					: (state->selected
						? st::reactionsTabChecksSelected
						: st::reactionsTabChecks)).paintInCenter(p, icon);
			}

			const auto textLeft = height + stm->padding.left();
			p.setPen(state->selected ? stm->textActiveFg : stm->textFg);
			p.setFont(font);
			p.drawText(textLeft, stm->padding.top() + font->ascent, text);
		}
		auto p = QPainter(result);
		p.drawImage(0, 0, state->cache);
		if (const auto custom = state->custom.get()) {
			using namespace Ui::Text;
			const auto size = Ui::Emoji::GetSizeNormal() / factor;
			const auto shift = (height - size) / 2;
			const auto skip = (size - AdjustCustomEmojiSize(size)) / 2;
			custom->paint(p, {
				.preview = (state->selected
					? QColor(
						st::activeButtonFg->c.red(),
						st::activeButtonFg->c.green(),
						st::activeButtonFg->c.blue(),
						st::activeButtonFg->c.alpha() / 3)
					: st::windowBgRipple->c),
				.now = crl::now(),
				.position = { icon.x() + shift + skip, shift + skip },
			});
		}
	}, result->lifetime());
	return result;
}

} // namespace

not_null<Tabs*> CreateTabs(
		not_null<QWidget*> parent,
		Ui::Text::CustomEmojiFactory factory,
		Fn<bool()> paused,
		const std::vector<Data::MessageReaction> &items,
		const Data::ReactionId &selected,
		Ui::WhoReadType whoReadType) {
	struct State {
		rpl::variable<ReactionId> selected;
		std::vector<not_null<Ui::AbstractButton*>> tabs;
	};
	const auto result = Ui::CreateChild<Tabs>(parent.get());
	using Entry = std::pair<int, ReactionId>;
	auto tabs = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto st = &st::reactionsTabs;
	const auto state = tabs->lifetime().make_state<State>();
	state->selected = selected;
	const auto append = [&](const ReactionId &reaction, int count) {
		using namespace rpl::mappers;
		const auto tab = CreateTab(
			tabs,
			*st,
			factory,
			paused,
			reaction,
			whoReadType,
			count,
			state->selected.value() | rpl::map(_1 == reaction));
		tab->setClickedCallback([=] {
			state->selected = reaction;
		});
		state->tabs.push_back(tab);
	};
	auto sorted = std::vector<Entry>();
	for (const auto &reaction : items) {
		if (reaction.id.emoji() == u"read"_q) {
			append(reaction.id, reaction.count);
		} else {
			sorted.emplace_back(reaction.count, reaction.id);
		}
	}
	ranges::sort(sorted, std::greater<>(), &Entry::first);
	const auto count = ranges::accumulate(
		sorted,
		0,
		std::plus<>(),
		&Entry::first);
	append(ReactionId(), count);
	for (const auto &[count, reaction] : sorted) {
		append(reaction, count);
	}
	result->move = [=](int x, int y) {
		tabs->moveToLeft(x, y);
	};
	result->resizeToWidth = [=](int width) {
		const auto available = width
			- st->padding.left()
			- st->padding.right();
		if (available <= 0) {
			return;
		}
		auto left = available;
		auto height = st->padding.top();
		for (const auto &tab : state->tabs) {
			if (left > 0 && available - left < tab->width()) {
				left = 0;
				height += tab->height() + st->itemSkip;
			}
			tab->move(
				st->padding.left() + left,
				height - tab->height() - st->itemSkip);
			left += tab->width() + st->itemSkip;
		}
		tabs->resize(width, height - st->itemSkip + st->padding.bottom());
	};
	result->heightValue = [=] {
		using namespace rpl::mappers;
		return tabs->heightValue() | rpl::map(_1 - st::lineWidth);
	};
	result->changes = [=] {
		return state->selected.changes();
	};
	return result;
}

} // namespace HistoryView::Reactions
