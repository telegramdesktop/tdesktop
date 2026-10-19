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
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_premium.h"
#include "ui/effects/animations.h"
#include "ui/effects/peer_qr_surface.h"
#include "ui/text/text.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "window/window_session_controller.h"

#include "styles/style_basic.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

using SizeTag = Data::CustomEmojiManager::SizeTag;

constexpr auto kBadgePopDuration = crl::time(110);
constexpr auto kBadgeAlpha = 0.6;
constexpr auto kHoverAlpha = 0.08;
constexpr auto kMarkedAlpha = 0.16;
constexpr auto kFillAlpha = 0.2;
constexpr auto kWeakAlpha = 0.88;
constexpr auto kChosenAlpha = 0.4;
constexpr auto kThumbAlpha = 0.6;
constexpr auto kThumbOverAlpha = 0.8;

struct PickerState {
	std::vector<Data::ReactionId> initial;
	std::vector<QString> picks;
	base::flat_set<QString> clicked;
	std::vector<DocumentId> provided;
	base::flat_map<DocumentId, Data::ReactionId> docToReaction;
	base::flat_map<Data::ReactionId, DocumentId> reactionToDoc;
	base::flat_map<DocumentId, int> order;
	base::flat_map<DocumentId, crl::time> shownAt;
	Ui::Animations::Basic popping;
};

class PicksStrip final : public Ui::RpWidget {
public:
	PicksStrip(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		const style::color &fill,
		const style::color &ink);

	void setPicks(const std::vector<QString> &keys, bool animated);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Slot {
		QString key;
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
		crl::time shownAt = 0;
	};

	const not_null<Main::Session*> _session;
	const style::color &_fill;
	const style::color &_ink;
	std::vector<Slot> _slots;
	Ui::Animations::Simple _width;
	int _frameSize = 0;

};

struct PickerStyle {
	PickerStyle();

	style::owned_color ink;
	style::owned_color hover;
	style::owned_color marked;
	style::owned_color fill;
	style::owned_color weak;
	style::owned_color chosen;
	style::owned_color thumb;
	style::owned_color thumbOver;
	style::EmojiPan pan;
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
		not_null<PickerState*> state) {
	state->docToReaction.clear();
	state->reactionToDoc.clear();
	const auto remember = [&](
			const Data::ReactionId &id,
			DocumentId document) {
		state->docToReaction.emplace(document, id);
		state->reactionToDoc.emplace(id, document);
	};
	const auto premium = session->premium();
	const auto possible = Data::LookupPossibleReactions(session);
	auto listed = std::vector<DocumentId>();
	for (const auto &reaction : possible.recent) {
		const auto &id = reaction->id;
		if (!id.paid()
			&& (premium || (!id.custom() && !reaction->premium))) {
			const auto document = reaction->selectAnimation->id;
			remember(id, document);
			listed.push_back(document);
		}
	}
	auto result = std::vector<DocumentId>();
	const auto addInitial = [&](const Data::ReactionId &id) {
		const auto document = PickDocument(session, state, id);
		if (document
			&& !ranges::contains(listed, document)
			&& !ranges::contains(result, document)) {
			if (!id.custom()) {
				remember(id, document);
			}
			result.push_back(document);
		}
	};
	for (const auto &id : state->initial) {
		addInitial(id);
	}
	if (!premium) {
		for (const auto &key : state->picks) {
			addInitial(ParseKey(key));
		}
	}
	for (const auto document : listed) {
		if (!ranges::contains(result, document)) {
			result.push_back(document);
		}
	}
	return result;
}

template <typename Predicate>
void EraseShownIf(not_null<PickerState*> state, Predicate &&predicate) {
	auto &shownAt = state->shownAt;
	for (auto i = begin(shownAt); i != end(shownAt);) {
		if (predicate(i->first, i->second)) {
			i = shownAt.erase(i);
		} else {
			++i;
		}
	}
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
	EraseShownIf(state, [&](DocumentId document, crl::time) {
		return !state->order.contains(document);
	});
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

[[nodiscard]] bool CanChooseKey(
		not_null<Main::Session*> session,
		not_null<const PickerState*> state,
		const QString &key) {
	if (ranges::contains(state->picks, key) || session->premium()) {
		return true;
	}
	const auto id = ParseKey(key);
	if (id.empty() || id.paid() || id.custom()) {
		return false;
	}
	const auto &all = session->data().reactions().list(
		Data::Reactions::Type::All);
	const auto i = ranges::find(all, id, &Data::Reaction::id);
	return (i != end(all)) && !i->premium;
}

[[nodiscard]] float64 PopProgress(crl::time shownAt, crl::time now) {
	return shownAt
		? std::clamp((now - shownAt) / float64(kBadgePopDuration), 0., 1.)
		: 1.;
}

void PaintPickBadge(QPainter &p, QRect area, int order, float64 shown) {
	if (shown <= 0.) {
		return;
	}
	const auto size = st::peerQrEmojiBadgeSize;
	const auto inset = st::peerQrEmojiBadgeInset;
	const auto x = style::RightToLeft()
		? (area.x() + inset)
		: (area.x() + area.width() - inset);
	const auto scale = anim::easeOutCubic(1., shown);
	const auto disc = QRect(-size / 2, -size / 2, size, size);
	auto hq = PainterHighQualityEnabler(p);
	p.translate(x, area.y() + inset);
	p.scale(scale, scale);
	p.setOpacity(p.opacity() * scale);
	p.setPen(Qt::NoPen);
	p.setBrush(anim::with_alpha(QColor(0, 0, 0), kBadgeAlpha));
	p.drawEllipse(disc);
	p.setPen(QColor(255, 255, 255));
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

[[nodiscard]] std::vector<QString> AddedKeys(
		const std::vector<QString> &was,
		const std::vector<QString> &now) {
	return now | ranges::views::filter([&](const QString &key) {
		return !ranges::contains(was, key);
	}) | ranges::to_vector;
}

void StartPops(
		not_null<Main::Session*> session,
		not_null<PickerState*> state,
		const std::vector<QString> &added) {
	const auto now = crl::now();
	auto started = false;
	for (const auto &key : added) {
		const auto id = ParseKey(key);
		const auto document = id
			? PickDocument(session, state, id)
			: DocumentId();
		if (document && state->order.contains(document)) {
			state->shownAt[document] = now;
			started = true;
		}
	}
	if (started) {
		state->popping.start();
	}
}

[[nodiscard]] int WidthFor(int count) {
	return 2 * st::peerQrEmojiStripPadding
		+ count * st::peerQrEmojiStripSkip;
}

PicksStrip::PicksStrip(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	const style::color &fill,
	const style::color &ink)
: RpWidget(parent)
, _session(session)
, _fill(fill)
, _ink(ink)
, _frameSize(Data::FrameSizeFromTag(SizeTag::Isolated)
	/ style::DevicePixelRatio()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void PicksStrip::setPicks(const std::vector<QString> &keys, bool animated) {
	const auto from = _width.value(WidthFor(int(_slots.size())));
	const auto now = crl::now();
	auto slots = std::vector<Slot>();
	slots.reserve(keys.size());
	for (const auto &key : keys) {
		const auto i = ranges::find(_slots, key, &Slot::key);
		if (i != end(_slots) && i->emoji) {
			slots.push_back(std::move(*i));
			continue;
		}
		slots.push_back({
			.key = key,
			.emoji = CreateFieldEmoji(_session, key, {
				.repaint = [=] { update(); },
			}),
			.shownAt = animated ? now : crl::time(),
		});
	}
	_slots = std::move(slots);
	if (animated) {
		_width.start(
			[=] { update(); },
			from,
			WidthFor(int(_slots.size())),
			kBadgePopDuration);
	} else {
		_width.stop();
	}
	setVisible(!_slots.empty());
	update();
}

void PicksStrip::paintEvent(QPaintEvent *e) {
	if (_slots.empty() || _frameSize <= 0) {
		return;
	}
	auto p = QPainter(this);
	const auto count = int(_slots.size());
	const auto capsuleWidth = _width.value(WidthFor(count));
	const auto capsuleHeight = float64(st::peerQrEmojiStripHeight);
	const auto capsule = QRectF(
		(width() - capsuleWidth) / 2.,
		(height() - capsuleHeight) / 2.,
		capsuleWidth,
		capsuleHeight);
	const auto radius = capsuleHeight / 2.;
	auto path = QPainterPath();
	path.addRoundedRect(capsule, radius, radius);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_fill);
		p.drawPath(path);
	}
	p.setClipPath(path);
	p.setRenderHint(QPainter::SmoothPixmapTransform);

	const auto now = crl::now();
	const auto paused = PowerSaving::On(PowerSaving::kEmojiReactions);
	const auto frame = float64(_frameSize);
	const auto skip = st::peerQrEmojiStripSkip;
	const auto left = capsule.x() + st::peerQrEmojiStripPadding + skip / 2.;
	const auto base = p.transform();
	for (auto i = 0; i != count; ++i) {
		const auto &slot = _slots[i];
		const auto shown = anim::easeOutCubic(
			1.,
			PopProgress(slot.shownAt, now));
		const auto size = st::peerQrEmojiStripSize * shown;
		if (!slot.emoji || size <= 0.) {
			continue;
		}
		auto transform = base;
		transform.translate(left + i * skip, capsule.center().y());
		transform.scale(size / frame, size / frame);
		transform.translate(-frame / 2., -frame / 2.);
		p.setTransform(transform);
		slot.emoji->paint(p, {
			.textColor = _ink->c,
			.now = now,
			.paused = paused,
		});
	}
}

void ApplyPickerColors(
		not_null<PickerStyle*> picker,
		QColor foreground) {
	picker->ink.update(foreground);
	picker->hover.update(anim::with_alpha(foreground, kHoverAlpha));
	picker->marked.update(anim::with_alpha(foreground, kMarkedAlpha));
	picker->fill.update(anim::with_alpha(foreground, kFillAlpha));
	picker->weak.update(anim::with_alpha(foreground, kWeakAlpha));
	picker->chosen.update(anim::with_alpha(foreground, kChosenAlpha));
	picker->thumb.update(anim::with_alpha(foreground, kThumbAlpha));
	picker->thumbOver.update(anim::with_alpha(foreground, kThumbOverAlpha));
}

PickerStyle::PickerStyle()
: ink(QColor(255, 255, 255))
, hover(QColor(255, 255, 255))
, marked(QColor(255, 255, 255))
, fill(QColor(255, 255, 255))
, weak(QColor(255, 255, 255))
, chosen(QColor(255, 255, 255))
, thumb(QColor(255, 255, 255))
, thumbOver(QColor(255, 255, 255))
, pan(st::peerQrEmojiPan) {
	pan.headerFg = ink.color();
	pan.textFg = ink.color();
	pan.tabs.labelFg = ink.color();
	pan.overBg = hover.color();
	pan.markedBg = marked.color();
	pan.categoriesBgOver = marked.color();
	pan.scroll.bg = st::transparent;
	pan.scroll.bgOver = marked.color();
	pan.scroll.barBg = thumb.color();
	pan.scroll.barBgOver = thumbOver.color();
	auto &search = pan.search;
	search.bg = fill.color();
	search.bgActive = chosen.color();
	search.fg = weak.color();
	search.fgActive = ink.color();
	search.field.textFg = ink.color();
	search.field.placeholderFg = weak.color();
	search.field.placeholderFgActive = weak.color();
	search.cancel.crossFg = ink.color();
	search.cancel.crossFgOver = ink.color();
	ApplyPickerColors(this, QColor(255, 255, 255));
}

} // namespace

QrEmojiPicker CreateQrEmojiPicker(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		const std::vector<QString> &picks) {
	using Selector = ChatHelpers::TabbedSelector;
	const auto session = &controller->session();
	const auto reactions = &session->data().reactions();
	const auto show = controller->uiShow();
	const auto pickerStyle = std::make_shared<PickerStyle>();
	const auto selector = Ui::CreateChild<Selector>(
		parent.get(),
		ChatHelpers::TabbedSelectorDescriptor{
			.show = show,
			.st = pickerStyle->pan,
			.level = Window::GifPauseReason::Layer,
			.mode = Selector::Mode::FullReactions,
			.customTextColor = [=] { return pickerStyle->ink.color()->c; },
			.features = { .allCustomEmojiPremium = true },
		});
	// WHY: the selector's children keep references into pan until
	// ~QWidget deletes them; this connection is released only in
	// ~QObject, after that, so pan outlives the whole subtree.
	QObject::connect(selector, &QObject::destroyed, [pickerStyle] {});
	const auto state = selector->lifetime().make_state<PickerState>();
	state->picks = picks;
	AddInitial(session, state, picks);
	selector->setAllowEmojiWithoutPremium(false);
	selector->setRoundRadius(st::emojiPanRadius - st::lineWidth);
	selector->setDropDown(true);
	selector->setMarkedCustomPainter([=](
			QPainter &p,
			QRect area,
			DocumentId document) {
		const auto i = state->order.find(document);
		if (i != end(state->order)) {
			const auto j = state->shownAt.find(document);
			const auto shown = (j != end(state->shownAt))
				? PopProgress(j->second, crl::now())
				: 1.;
			p.save();
			PaintPickBadge(p, area, i->second, shown);
			p.restore();
		}
	});
	state->popping.init([=](crl::time now) {
		selector->update();
		EraseShownIf(state, [&](DocumentId, crl::time at) {
			return (now - at >= kBadgePopDuration);
		});
		return !state->shownAt.empty();
	});
	const auto refresh = [=] {
		auto documents = CollectDocuments(session, state);
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
	Data::AmPremiumValue(
		session
	) | rpl::skip(1) | rpl::on_next(refresh, selector->lifetime());

	const auto strip = Ui::CreateChild<PicksStrip>(
		selector,
		session,
		pickerStyle->fill.color(),
		pickerStyle->ink.color());
	strip->setPicks(state->picks, false);
	strip->raise();
	selector->sizeValue() | rpl::on_next([=](QSize size) {
		strip->setGeometry(
			0,
			(size.height()
				- st::peerQrEmojiStripBottom
				- st::peerQrEmojiStripHeight),
			size.width(),
			st::peerQrEmojiStripHeight);
	}, strip->lifetime());

	auto chosen = selector->customEmojiChosen(
	) | rpl::map([=](const ChatHelpers::FileChosen &data) {
		return ChosenKey(state, data.document->id);
	}) | rpl::filter([](const QString &key) {
		return !key.isEmpty();
	}) | rpl::filter([=](const QString &key) {
		if (CanChooseKey(session, state, key)) {
			return true;
		}
		Settings::ShowPremiumPromoToast(
			show,
			tr::lng_animated_emoji_subscribe(
				tr::now,
				lt_link,
				tr::link(
					tr::bold(tr::lng_animated_emoji_subscribe_link(tr::now))),
				tr::marked),
			u"animated_emoji"_q);
		return false;
	}) | rpl::before_next([=](const QString &key) {
		state->clicked.emplace(key);
	});
	return {
		.widget = selector,
		.chosen = std::move(chosen),
		.setPicks = crl::guard(selector, [=](
				const std::vector<QString> &keys) {
			const auto animated = !selector->isHidden() && !anim::Disabled();
			const auto added = animated
				? AddedKeys(state->picks, keys)
				: std::vector<QString>();
			state->picks = keys;
			AddInitial(session, state, keys);
			refresh();
			strip->setPicks(keys, animated);
			StartPops(session, state, added);
		}),
		.setShown = crl::guard(selector, [=](bool shown) {
			if (shown) {
				refresh();
				selector->afterShown();
			} else {
				selector->beforeHiding();
			}
		}),
		.setColors = crl::guard(selector, [=](
				const Ui::PeerQrColors &colors) {
			ApplyPickerColors(pickerStyle.get(), colors.foreground);
			selector->refreshSearchColors();
			selector->update();
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

} // namespace Info::Profile
