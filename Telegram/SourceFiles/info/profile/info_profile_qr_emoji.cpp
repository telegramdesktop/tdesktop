/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_qr_emoji.h"

#include "base/weak_ptr.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "data/data_document.h"
#include "data/data_message_reaction_id.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/style/style_core_palette.h"
#include "ui/text/text.h"
#include "ui/painter.h"
#include "window/themes/window_themes_embedded.h"
#include "window/window_session_controller.h"

#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

using SizeTag = Data::CustomEmojiManager::SizeTag;

struct PickerState {
	std::vector<Data::ReactionId> initial;
	std::vector<QString> picks;
	base::flat_set<QString> clicked;
	std::vector<DocumentId> provided;
	base::flat_map<DocumentId, Data::ReactionId> docToReaction;
	base::flat_map<Data::ReactionId, DocumentId> reactionToDoc;
	base::flat_map<DocumentId, int> order;
};

struct BackgroundsState final : base::has_weak_ptr {
	Fn<void(QrEmojiBackgrounds)> done;
};

[[nodiscard]] Data::ReactionId ParseKey(QStringView key) {
	const auto id = Data::ReactionFromEntityData(key);
	return id.paid() ? Data::ReactionId() : id;
}

[[nodiscard]] DocumentData *SelectAnimation(
		not_null<Main::Session*> session,
		const Data::ReactionId &id) {
	const auto &all = session->data().reactions().list(
		Data::Reactions::Type::All);
	const auto i = ranges::find(all, id, &Data::Reaction::id);
	return (i != end(all)) ? i->selectAnimation.get() : nullptr;
}

[[nodiscard]] DocumentId PickDocument(
		not_null<Main::Session*> session,
		not_null<const PickerState*> state,
		const Data::ReactionId &id) {
	if (const auto custom = id.custom()) {
		return custom;
	}
	const auto i = state->reactionToDoc.find(id);
	if (i != end(state->reactionToDoc)) {
		return i->second;
	}
	const auto document = SelectAnimation(session, id);
	return document ? document->id : DocumentId();
}

[[nodiscard]] bool ShownOutsideSearch(
		not_null<Main::Session*> session,
		not_null<const PickerState*> state,
		const Data::ReactionId &id) {
	const auto document = PickDocument(session, state, id);
	if (!document) {
		return false;
	} else if (ranges::contains(state->provided, document)) {
		return true;
	}
	const auto sticker = session->data().document(document)->sticker();
	if (!sticker) {
		return false;
	}
	const auto &stickers = session->data().stickers();
	const auto set = sticker->set.id;
	return stickers.emojiSetsOrder().contains(set)
		|| stickers.featuredEmojiSetsOrder().contains(set);
}

void AddInitial(
		not_null<Main::Session*> session,
		not_null<PickerState*> state,
		const std::vector<QString> &keys) {
	for (const auto &key : keys) {
		const auto id = ParseKey(key);
		if (id
			&& !ranges::contains(state->initial, id)
			&& (!state->clicked.contains(key)
				|| !ShownOutsideSearch(session, state, id))) {
			state->initial.push_back(id);
		}
	}
}

[[nodiscard]] std::vector<DocumentId> CollectDocuments(
		not_null<Main::Session*> session,
		bool premium,
		not_null<PickerState*> state) {
	state->docToReaction.clear();
	state->reactionToDoc.clear();
	const auto remember = [&](
			const Data::ReactionId &id,
			DocumentId document) {
		state->docToReaction.emplace(document, id);
		state->reactionToDoc.emplace(id, document);
	};
	const auto possible = Data::LookupPossibleReactions(session);
	auto listed = std::vector<DocumentId>();
	for (const auto &reaction : possible.recent) {
		const auto &id = reaction->id;
		if (!id.paid() && (premium || !id.custom())) {
			const auto document = reaction->selectAnimation->id;
			remember(id, document);
			listed.push_back(document);
		}
	}
	auto result = std::vector<DocumentId>();
	for (const auto &id : state->initial) {
		const auto document = PickDocument(session, state, id);
		if (document
			&& !ranges::contains(listed, document)
			&& !ranges::contains(result, document)) {
			if (!id.custom()) {
				remember(id, document);
			}
			result.push_back(document);
		}
	}
	for (const auto document : listed) {
		if (!ranges::contains(result, document)) {
			result.push_back(document);
		}
	}
	return result;
}

void ApplyPicks(
		not_null<Main::Session*> session,
		not_null<PickerState*> state,
		not_null<ChatHelpers::TabbedSelector*> selector) {
	auto order = base::flat_map<DocumentId, int>();
	auto marked = base::flat_set<DocumentId>();
	auto index = 0;
	for (const auto &key : state->picks) {
		++index;
		const auto id = ParseKey(key);
		const auto document = id
			? PickDocument(session, state, id)
			: DocumentId();
		if (document && marked.emplace(document).second) {
			order.emplace(document, index);
		}
	}
	const auto reordered = (state->order != order);
	state->order = std::move(order);
	selector->setMarkedCustomIds(marked);
	if (reordered) {
		selector->update();
	}
}

[[nodiscard]] QString ChosenKey(
		not_null<const PickerState*> state,
		DocumentId document) {
	const auto i = state->docToReaction.find(document);
	const auto id = (i != end(state->docToReaction))
		? i->second
		: Data::ReactionId{ document };
	return (id.empty() || id.paid())
		? QString()
		: Data::ReactionEntityData(id);
}

void PaintPickBadge(QPainter &p, QRect area, int order) {
	const auto size = st::peerQrEmojiBadgeSize;
	const auto inset = st::peerQrEmojiBadgeInset;
	const auto stroke = st::peerQrEmojiBadgeStroke;
	const auto x = style::RightToLeft()
		? (area.x() + inset)
		: (area.x() + area.width() - inset);
	const auto disc = QRect(
		x - size / 2,
		area.y() + inset - size / 2,
		size,
		size);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::emojiPanBg);
	p.drawEllipse(disc.marginsAdded({ stroke, stroke, stroke, stroke }));
	p.setBrush(st::windowBgActive);
	p.drawEllipse(disc);
	p.setPen(st::windowFgActive);
	p.setFont(st::peerQrEmojiBadgeFont);
	p.drawText(disc, QString::number(order), style::al_center);
}

[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> CreateFieldEmoji(
		not_null<Main::Session*> session,
		QStringView key,
		const Ui::Text::MarkedContext &context) {
	const auto id = ParseKey(key);
	const auto manager = &session->data().customEmojiManager();
	if (!id) {
		return nullptr;
	} else if (const auto custom = id.custom()) {
		return manager->create(custom, context.repaint, SizeTag::Isolated);
	}
	const auto document = SelectAnimation(session, id);
	return document
		? manager->create(
			not_null(document),
			context.repaint,
			SizeTag::Isolated)
		: nullptr;
}

[[nodiscard]] QColor PaletteWindowBg(bool dark) {
	auto palette = std::make_unique<style::palette>();
	Window::Theme::PreparePaletteCallback(dark, {})(*palette);
	return palette->windowBg()->c;
}

} // namespace

QrEmojiPicker CreateQrEmojiPicker(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		const std::vector<QString> &picks) {
	using Selector = ChatHelpers::TabbedSelector;
	const auto session = &controller->session();
	const auto reactions = &session->data().reactions();
	const auto premium = session->premium();
	const auto selector = Ui::CreateChild<Selector>(
		parent.get(),
		ChatHelpers::TabbedSelectorDescriptor{
			.show = controller->uiShow(),
			.st = (premium ? st::peerQrEmojiPan : st::peerQrRecentEmojiPan),
			.level = Window::GifPauseReason::Layer,
			.mode = (premium
				? Selector::Mode::FullReactions
				: Selector::Mode::RecentReactions),
		});
	const auto state = selector->lifetime().make_state<PickerState>();
	state->picks = picks;
	AddInitial(session, state, picks);
	if (premium) {
		selector->setAllowEmojiWithoutPremium(false);
	}
	selector->setRoundRadius(st::peerQrPanelRadius - st::lineWidth);
	selector->setDropDown(true);
	selector->setMarkedCustomPainter([=](
			QPainter &p,
			QRect area,
			DocumentId document) {
		const auto i = state->order.find(document);
		if (i != end(state->order)) {
			p.save();
			PaintPickBadge(p, area, i->second);
			p.restore();
		}
	});
	const auto refresh = [=] {
		auto documents = CollectDocuments(session, premium, state);
		if (state->provided != documents) {
			state->provided = std::move(documents);
			selector->provideRecentEmoji(
				ChatHelpers::DocumentListToRecent(state->provided));
		}
		ApplyPicks(session, state, selector);
	};
	refresh();
	selector->showStarted();
	rpl::merge(
		reactions->topUpdates(),
		reactions->recentUpdates(),
		reactions->defaultUpdates(),
		reactions->favoriteUpdates()
	) | rpl::on_next(refresh, selector->lifetime());

	auto chosen = selector->customEmojiChosen(
	) | rpl::map([=](const ChatHelpers::FileChosen &data) {
		return ChosenKey(state, data.document->id);
	}) | rpl::filter([](const QString &key) {
		return !key.isEmpty();
	}) | rpl::before_next([=](const QString &key) {
		state->clicked.emplace(key);
	});
	return {
		.widget = selector,
		.chosen = std::move(chosen),
		.setPicks = crl::guard(selector, [=](
				const std::vector<QString> &keys) {
			state->picks = keys;
			AddInitial(session, state, keys);
			refresh();
		}),
		.setShown = crl::guard(selector, [=](bool shown) {
			if (shown) {
				selector->afterShown();
			} else {
				selector->beforeHiding();
			}
		}),
	};
}

rpl::producer<Ui::PeerQrEmojiSource> QrEmojiSourceValue(
		not_null<Main::Session*> session) {
	const auto weak = base::make_weak(session);
	const auto factory = [=](
			QStringView key,
			const Ui::Text::MarkedContext &context) {
		const auto strong = weak.get();
		return strong
			? CreateFieldEmoji(strong, key, context)
			: std::unique_ptr<Ui::Text::CustomEmoji>();
	};
	return rpl::single(
		rpl::empty
	) | rpl::then(
		session->data().reactions().defaultUpdates()
	) | rpl::map([=] {
		return Ui::PeerQrEmojiSource{
			.factory = factory,
			.frameSize = (Data::FrameSizeFromTag(SizeTag::Isolated)
				/ style::DevicePixelRatio()),
		};
	});
}

rpl::producer<QrEmojiBackgrounds> QrEmojiBackgroundsValue() {
	return [](auto consumer) {
		auto result = rpl::lifetime();
		const auto state = result.make_state<BackgroundsState>();
		state->done = [=](QrEmojiBackgrounds backgrounds) {
			consumer.put_next(std::move(backgrounds));
		};
		const auto weak = base::make_weak(state);
		crl::async([=] {
			const auto backgrounds = QrEmojiBackgrounds{
				.light = PaletteWindowBg(false),
				.dark = PaletteWindowBg(true),
			};
			crl::on_main(weak, [=] {
				weak->done(backgrounds);
			});
		});
		return result;
	};
}

} // namespace Info::Profile
