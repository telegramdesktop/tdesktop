/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_pull_to_next_channel.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_haptic.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/components/sponsored_messages.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_messages.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_mark_as_read.h"
#include "storage/storage_shared_media.h"
#include "support/support_preload.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

constexpr auto kResetReachedOn = 0.95;
constexpr auto kReverseEase = 1.5;
constexpr auto kReadyDwell = crl::time(120);
constexpr auto kExpandDuration = crl::time(250);
constexpr auto kReleaseShowDuration = crl::time(250);
constexpr auto kReleaseHideDuration = crl::time(250);
constexpr auto kPanelDuration = crl::time(320);
constexpr auto kBounceDuration = crl::time(400);

[[nodiscard]] int PullThreshold() {
	// The pull sides stretch with a soft linear stiffness that reaches
	// a given height in about half the finger travel of the log curve
	// used before, so a doubled threshold keeps a comparable gesture
	// length and avoids accidental triggers.
	return st::historyPullNextThreshold * 2;
}

[[nodiscard]] History *FindInList(
		not_null<Dialogs::MainList*> list,
		not_null<History*> current) {
	for (const auto &row : list->indexed()->all()) {
		const auto history = row->history();
		if (!history || history == current) {
			continue;
		}
		const auto peer = history->peer;
		if (peer->isBroadcast()
			&& (history->unreadCount() > 0)
			&& !history->useTopPromotion()
			&& peer->computeUnavailableReason().isEmpty()) {
			return history;
		}
	}
	return nullptr;
}

[[nodiscard]] History *FindNextUnreadChannel(
		not_null<Window::SessionController*> controller,
		not_null<History*> current) {
	auto &data = controller->session().data();
	const auto filterId = controller->activeChatsFilterCurrent();
	const auto currentList = filterId
		? data.chatsFilters().chatsList(filterId)
		: data.chatsList();
	if (const auto history = FindInList(currentList, current)) {
		return history;
	}
	for (const auto &filter : data.chatsFilters().list()) {
		const auto id = filter.id();
		if (!id || id == filterId) {
			continue;
		}
		if (const auto history = FindInList(
				data.chatsFilters().chatsList(id),
				current)) {
			return history;
		}
	}
	if (filterId) {
		if (const auto history = FindInList(data.chatsList(), current)) {
			return history;
		}
	}
	if (const auto folder = data.folderLoaded(Data::Folder::kId)) {
		if (const auto history = FindInList(folder->chatsList(), current)) {
			return history;
		}
	}
	return nullptr;
}

[[nodiscard]] Data::ForumTopic *FindNextUnreadTopic(
		not_null<Data::ForumTopic*> current) {
	for (const auto &row : current->forum()->topicsList()->indexed()->all()) {
		const auto topic = row->topic();
		if (!topic || topic == current) {
			continue;
		}
		if (MarkAsReadMenu::IsUnreadThread(topic)) {
			return topic;
		}
	}
	return nullptr;
}

[[nodiscard]] Storage::SharedMediaResult TopPinnedSlice(
		not_null<PeerData*> peer) {
	return peer->session().storage().snapshot(Storage::SharedMediaQuery(
		Storage::SharedMediaKey(
			peer->id,
			MsgId(0), // topicRootId
			PeerId(0), // monoforumPeerId
			Storage::SharedMediaType::Pinned,
			ServerMaxMsgId - 1),
		1,
		1));
}

void PreloadPinnedBar(not_null<History*> next) {
	const auto peer = next->peer;
	if (TopPinnedSlice(peer).count.has_value()) {
		return;
	}
	peer->session().api().requestSharedMedia(
		peer,
		MsgId(0), // topicRootId
		PeerId(0), // monoforumPeerId
		Storage::SharedMediaType::Pinned,
		ServerMaxMsgId - 1,
		Data::LoadDirection::Before);
}

[[nodiscard]] bool PinnedBarReady(not_null<History*> next) {
	const auto peer = next->peer;
	const auto slice = TopPinnedSlice(peer);
	if (!slice.count.has_value()) {
		return false;
	} else if (slice.messageIds.empty()) {
		return true;
	}
	const auto id = FullMsgId(peer->id, slice.messageIds.back());
	return (peer->owner().message(id) != nullptr);
}

void PaintStroke(
		QPainter &p,
		const QPainterPath &path,
		const QColor &fg,
		float64 widthScale = 1.) {
	auto pen = QPen(fg);
	pen.setWidthF(st::historyPullNextArrowStroke * widthScale);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

void PaintArrow(QPainter &p, QPointF center, float64 size, const QColor &fg) {
	if (size <= 0.) {
		return;
	}
	const auto progress = size / st::historyPullNextArrowSize;
	const auto scale = style::ConvertFloatScale(progress);
	auto path = QPainterPath();
	path.moveTo(12.5, 4.);
	path.lineTo(12.5, 22.);
	path.moveTo(3.5, 12.);
	path.lineTo(12.5, 3.5);
	path.lineTo(21.5, 12.);
	auto transform = QTransform();
	transform.translate(
		center.x(),
		center.y() - st::historyPullNextArrowRaise);
	transform.scale(scale, scale);
	transform.translate(-12., 8.);
	PaintStroke(p, transform.map(path), fg, progress);
}

void PaintCheck(QPainter &p, QPointF center, float64 size, const QColor &fg) {
	if (size <= 0.) {
		return;
	}
	const auto s = size / 2.4;
	auto path = QPainterPath();
	path.moveTo(center + QPointF(-s, 0.));
	path.lineTo(center + QPointF(-s / 3., s * 0.7));
	path.lineTo(center + QPointF(s, -s * 0.7));
	PaintStroke(p, path, fg);
}

[[nodiscard]] float64 BounceProgress(float64 t) {
	const auto ease = [](float64 x) {
		if (x < 0.5) {
			return 4. * x * x * x;
		}
		const auto u = -2. * x + 2.;
		return 1. - u * u * u / 2.;
	};
	if (t < 0.45) {
		return ease(t / 0.45);
	} else if (t < 0.75) {
		return 1. - ease((t - 0.45) / 0.3) * 1.5;
	}
	return -0.5 + ease((t - 0.75) / 0.25) * 0.5;
}

} // namespace

class PullToNextChannel::Indicator final : public Ui::RpWidget {
public:
	Indicator(
		not_null<QWidget*> parent,
		not_null<const Ui::ChatStyle*> st,
		Fn<bool()> paused);
	~Indicator();

	void setHistoryData(float64 offset, bool ready, History *next);
	void setTopicData(
		float64 offset,
		bool ready,
		Data::ForumTopic *next,
		const QString &completed);
	void hideNow();

private:
	void setProgress(float64 offset, bool ready);
	void clearTopicImage();
	void paintEvent(QPaintEvent *e) override;

	const not_null<const Ui::ChatStyle*> _st;
	const Fn<bool()> _paused;

	Mode _mode = Mode::None;
	float64 _offset = 0.;
	bool _ready = false;
	base::weak_ptr<History> _next;
	base::weak_ptr<Data::ForumTopic> _topic;
	QString _name;
	QString _topicCompleted;
	Ui::PeerUserpicView _userpic;
	std::shared_ptr<Ui::DynamicImage> _topicImage;
	Ui::Animations::Simple _releaseProgress;
	Ui::Animations::Simple _bounce;

};

PullToNextChannel::Indicator::Indicator(
	not_null<QWidget*> parent,
	not_null<const Ui::ChatStyle*> st,
	Fn<bool()> paused)
: RpWidget(parent)
, _st(st)
, _paused(std::move(paused)) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_TranslucentBackground);
	hide();
}

PullToNextChannel::Indicator::~Indicator() {
	clearTopicImage();
}

void PullToNextChannel::Indicator::setHistoryData(
		float64 offset,
		bool ready,
		History *next) {
	if (_mode != Mode::History) {
		clearTopicImage();
		_topic = nullptr;
		_topicCompleted = QString();
		_mode = Mode::History;
	}
	if (_next.get() != next) {
		_next = next;
		_userpic = {};
		_name = next ? next->peer->name() : QString();
		if (next) {
			next->peer->loadUserpic();
		}
	}
	setProgress(offset, ready);
}

void PullToNextChannel::Indicator::setTopicData(
		float64 offset,
		bool ready,
		Data::ForumTopic *next,
		const QString &completed) {
	if (_mode != Mode::Topic
		|| _topic.get() != next
		|| (!next && _topicImage)) {
		clearTopicImage();
		_mode = Mode::Topic;
		_next = nullptr;
		_topic = next;
		_userpic = {};
		_name = next ? next->title() : QString();
		if (next) {
			const auto textFg = [st = _st] {
				return st->msgServiceFg()->c;
			};
			_topicImage = next->iconId()
				? Ui::MakeEmojiThumbnail(
					&next->owner(),
					Data::SerializeCustomEmojiId(next->iconId()),
					_paused,
					textFg)
				: Ui::MakeEmojiThumbnail(
					&next->owner(),
					Data::TopicIconEmojiEntity({
						.title = next->isGeneral()
							? Data::ForumGeneralIconTitle()
							: next->title(),
						.colorId = next->isGeneral()
							? Data::ForumGeneralIconColor(textFg())
							: next->colorId(),
					}),
					_paused,
					textFg);
			_topicImage->subscribeToUpdates(
				crl::guard(this, [=] { update(); }));
		}
	}
	_topicCompleted = completed;
	setProgress(offset, ready);
}

void PullToNextChannel::Indicator::setProgress(
		float64 offset,
		bool ready) {
	_offset = offset;
	if (_ready != ready) {
		const auto from = _releaseProgress.value(_ready ? 1. : 0.);
		_ready = ready;
		_releaseProgress.start(
			[=] { update(); },
			from,
			ready ? 1. : 0.,
			ready ? kReleaseShowDuration : kReleaseHideDuration,
			ready ? anim::easeOutQuint : anim::sineInOut);
		if (ready) {
			_bounce.start(
				[=] { update(); },
				0.,
				1.,
				kBounceDuration,
				anim::linear);
		}
	}
	if (offset <= 0.) {
		hide();
		return;
	} else if (isHidden()) {
		show();
	}
	update();
}

void PullToNextChannel::Indicator::clearTopicImage() {
	if (_topicImage) {
		_topicImage->subscribeToUpdates(nullptr);
		_topicImage = nullptr;
	}
}

void PullToNextChannel::Indicator::hideNow() {
	_offset = 0.;
	_ready = false;
	_releaseProgress.stop();
	_bounce.stop();
	if (_mode == Mode::Topic) {
		clearTopicImage();
		_topic = nullptr;
		_name = QString();
		_topicCompleted = QString();
		_mode = Mode::None;
	}
	hide();
}

void PullToNextChannel::Indicator::paintEvent(QPaintEvent *e) {
	const auto offset = _offset;
	if (offset <= st::lineWidth) {
		return;
	}
	const auto next = _next.get();
	const auto topic = _topic.get();
	if (_mode == Mode::Topic && !topic) {
		clearTopicImage();
		_topic = nullptr;
	}

	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);

	const auto card = float64(st::historyPullNextExpand);
	const auto progress = (card > 0.)
		? std::clamp(offset / card, 0., 1.)
		: 0.;
	const auto release = _releaseProgress.value(_ready ? 1. : 0.);
	const auto alpha = std::clamp(progress, 0., 1.);
	const auto h = float64(height());
	const auto cx = (width() - st::historyScroll.width) / 2.;
	const auto avatar = float64(st::historyPullNextAvatar);
	const auto circleRadius = avatar / 2.;
	const auto bg = _st->msgServiceBg()->c;
	const auto fg = _st->msgServiceFg()->c;
	const auto bounceOffset = -BounceProgress(_bounce.value(1.))
		* st::historyPullNextBounce;

	const auto widthRadius = std::clamp(
		circleRadius * (offset - st::historyPullNextSkip)
			/ (card - st::historyPullNextSkip),
		0.,
		circleRadius);
	const auto widthRadius2 = std::max(
		0.,
		std::min(
			circleRadius * progress,
			offset / 2. - st::historyPullNextSkip * progress));
	const auto size
		= (widthRadius2 * 2. - st::historyPullNextInset) * (1. - release)
		+ avatar * release;

	if (release < 1.) {
		const auto capsuleTop = h - offset;
		const auto capsuleBottom = h
			+ (-st::historyPullNextSkip * (1. - release)
				+ (-offset + avatar) * release);
		auto rect = QRectF(
			cx - widthRadius,
			capsuleTop,
			2. * widthRadius,
			capsuleBottom - capsuleTop);
		auto bgAlpha = alpha;
		if (release > 0.) {
			const auto inset = st::historyPullNextInset * release;
			rect = rect - Margins(inset);
			bgAlpha = alpha * (1. - release);
		}
		if (rect.width() > 0. && rect.height() > 0.) {
			auto capsule = QPainterPath();
			capsule.addRoundedRect(rect, widthRadius, widthRadius);

			p.setOpacity(bgAlpha);
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawPath(capsule);

			const auto arrowCy = h
				+ (-offset
					+ st::historyPullNextArrowSize
					+ st::historyPullNextSkip * (1. - progress)
					- st::historyPullNextRise * release);
			p.save();
			p.setClipRect(rect);
			p.setOpacity(alpha * (1. - release));
			PaintArrow(
				p,
				QPointF(cx, arrowCy),
				st::historyPullNextArrowSize * progress,
				fg);
			p.restore();
		}
	}

	const auto name = (_mode == Mode::History)
		? (next
			? _name
			: tr::lng_pull_no_unread_channels(tr::now))
		: (_mode == Mode::Topic)
		? (topic ? _name : _topicCompleted)
		: QString();
	if (release > 0. && !name.isEmpty()) {
		const auto nameFont = st::historyPullNextNameFont;
		const auto centerWidth = width() - st::historyScroll.width;
		const auto avail = centerWidth - 4 * st::historyPullNextSkip;
		const auto elName = nameFont->elided(name, avail);
		const auto nameW = float64(nameFont->width(elName));
		const auto y = h
			+ (st::historyPullNextNameTop * (1. - release)
				- st::historyPullNextRise * release)
			+ bounceOffset;
		const auto pill = QRectF(
			(centerWidth - nameW) / 2. - st::historyPullNextSkip,
			y - st::historyPullNextPadding,
			nameW + 2 * st::historyPullNextSkip,
			nameFont->height + st::historyPullNextSkip);
		p.setOpacity(alpha * release);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(
			pill,
			st::historyPullNextNameRadius,
			st::historyPullNextNameRadius);
		p.setPen(fg);
		p.setFont(nameFont);
		p.drawText(
			QRectF((centerWidth - nameW) / 2., y, nameW, nameFont->height),
			elName,
			QTextOption(Qt::AlignCenter));
	}

	if (size >= 1.) {
		const auto top = h
			+ ((-st::historyPullNextSkip
					- st::historyPullNextSkip * progress
					- size) * (1. - release)
				+ (-std::min(offset, card) + st::historyPullNextPadding)
					* release)
			+ bounceOffset;
		const auto avRect = QRectF(cx - size / 2., top, size, size);
		p.setOpacity(alpha);
		if (next || topic) {
			const auto count = next
				? next->unreadCount()
				: topic->chatListUnreadState().messages;
			const auto badgeShown = (release > 0.)
				&& (next ? (count > 0) : MarkAsReadMenu::IsUnreadThread(topic));
			const auto font = st::historyPullNextBadgeFont;
			const auto badgeHeight = float64(st::historyPullNextBadge);
			const auto string = (count > 0)
				? ((count > 999) ? u"999+"_q : QString::number(count))
				: QString();
			const auto badgeWidth = badgeShown
				? std::max(
					font->width(string) + badgeHeight - font->height,
					badgeHeight)
				: 0.;
			const auto badge = badgeShown
				? QRectF(
					avRect.x() + avRect.width() - badgeWidth + badgeHeight / 3.,
					avRect.y() - badgeHeight / 4.,
					badgeWidth,
					badgeHeight)
				: QRectF();
			const auto ringWidth = float64(st::historyPullNextBadgeRing);
			const auto layerRect = badgeShown
				? avRect.united(badge + Margins(ringWidth))
				: avRect;

			const auto ratio = style::DevicePixelRatio();
			auto layer = QImage(
				(layerRect.size() * ratio).toSize(),
				QImage::Format_ARGB32_Premultiplied);
			layer.setDevicePixelRatio(ratio);
			layer.fill(Qt::transparent);
			{
				auto q = QPainter(&layer);
				q.setRenderHint(QPainter::Antialiasing);
				q.translate(-layerRect.topLeft());

				q.save();
				q.translate(rect::center(avRect));
				q.scale(size / avatar, size / avatar);
				q.translate(-avatar / 2., -avatar / 2.);
				if (next) {
					next->peer->paintUserpic(
						q,
						_userpic,
						0,
						0,
						int(avatar),
						true);
				} else if (_topicImage) {
					q.drawImage(0, 0, _topicImage->image(int(avatar)));
				}
				q.restore();

				if (badgeShown) {
					q.translate(rect::center(badge));
					q.scale(release, release);
					q.translate(-rect::center(badge));
					q.setPen(Qt::NoPen);
					q.setBrush(st::dialogsUnreadBg->c);
					q.drawRoundedRect(
						badge,
						badgeHeight / 2.,
						badgeHeight / 2.);
					q.setPen(st::dialogsUnreadFg->c);
					q.setFont(font);
					q.drawText(badge, string, QTextOption(Qt::AlignCenter));

					auto pen = QPen(Qt::transparent);
					pen.setWidthF(ringWidth);
					const auto ring = badge + Margins(ringWidth / 2.);
					q.setCompositionMode(QPainter::CompositionMode_Source);
					q.setPen(pen);
					q.setBrush(Qt::NoBrush);
					q.drawRoundedRect(
						ring,
						ring.height() / 2.,
						ring.height() / 2.);
				}
			}
			p.setOpacity(alpha);
			p.drawImage(layerRect.topLeft(), layer);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawEllipse(avRect);
			PaintCheck(p, rect::center(avRect), size / 2., fg);
		}
	}
}

class PullToNextChannel::HintOverlay final : public Ui::RpWidget {
public:
	explicit HintOverlay(not_null<QWidget*> parent);

	void setData(bool visible, bool ready, Mode mode, bool hasCandidate);
	void hideAnimated();
	void hideNow();

private:
	void toggle(bool visible);
	void paintEvent(QPaintEvent *e) override;

	Mode _mode = Mode::None;
	bool _visible = false;
	bool _ready = false;
	bool _has = false;
	Ui::Animations::Simple _panel;
	Ui::Animations::Simple _releaseProgress;

};

PullToNextChannel::HintOverlay::HintOverlay(not_null<QWidget*> parent)
: RpWidget(parent) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();
}

void PullToNextChannel::HintOverlay::setData(
		bool visible,
		bool ready,
		Mode mode,
		bool hasCandidate) {
	_mode = mode;
	_has = hasCandidate;
	toggle(visible && _has);
	if (_ready != ready) {
		const auto from = _releaseProgress.value(_ready ? 1. : 0.);
		_ready = ready;
		_releaseProgress.start(
			[=] { update(); },
			from,
			ready ? 1. : 0.,
			ready ? kReleaseShowDuration : kReleaseHideDuration,
			ready ? anim::easeOutQuint : anim::sineInOut);
	}
	update();
}

void PullToNextChannel::HintOverlay::toggle(bool visible) {
	if (_visible == visible) {
		return;
	}
	const auto from = _panel.value(_visible ? 1. : 0.);
	_visible = visible;
	_panel.start([=] {
		update();
		if (!_panel.animating() && !_visible) {
			hide();
		}
	}, from, visible ? 1. : 0., kPanelDuration, anim::easeOutQuint);
	if (visible && isHidden()) {
		show();
	}
	update();
}

void PullToNextChannel::HintOverlay::hideAnimated() {
	toggle(false);
}

void PullToNextChannel::HintOverlay::hideNow() {
	_mode = Mode::None;
	_visible = false;
	_ready = false;
	_panel.stop();
	_releaseProgress.stop();
	hide();
}

void PullToNextChannel::HintOverlay::paintEvent(QPaintEvent *e) {
	const auto panel = _panel.value(_visible ? 1. : 0.);
	if (panel <= 0.) {
		return;
	}

	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);

	p.setOpacity(panel);
	p.fillRect(rect(), st::windowBg);

	const auto release = _releaseProgress.value(_ready ? 1. : 0.);
	const auto slide = st::historyPullNextHintSlide;
	const auto font = st::historyPullNextHintFont;
	const auto avail = width() - 4 * st::historyPullNextSkip;
	const auto top = (height() - font->height) / 2.;
	p.setFont(font);
	p.setPen(st::windowSubTextFg);
	if (release < 1.) {
		p.setOpacity(panel * (1. - release));
		const auto text = (_mode == Mode::Topic)
			? tr::lng_pull_next_topic(tr::now)
			: tr::lng_pull_next_channel(tr::now);
		const auto pull = font->elided(
			text,
			avail);
		p.drawText(
			QRectF(0., top - slide * release, width(), font->height),
			pull,
			QTextOption(Qt::AlignCenter));
	}
	if (release > 0.) {
		p.setOpacity(panel * release);
		const auto text = (_mode == Mode::Topic)
			? tr::lng_release_next_topic(tr::now)
			: tr::lng_release_next_channel(tr::now);
		const auto rel = font->elided(
			text,
			avail);
		p.drawText(
			QRectF(0., top + slide * (1. - release), width(), font->height),
			rel,
			QTextOption(Qt::AlignCenter));
	}
}

PullToNextChannel::PullToNextChannel(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::ElasticScroll*> scroll,
	not_null<Window::SessionController*> controller,
	Fn<bool()> loadedAtBottom)
: _parent(parent)
, _scroll(scroll)
, _controller(controller)
, _loadedAtBottom(std::move(loadedAtBottom))
, _indicator(base::make_unique_q<Indicator>(
	scroll,
	controller->chatStyle(),
	[=] {
		return controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}))
, _hint(base::make_unique_q<HintOverlay>(parent)) {
	rpl::combine(
		_scroll->positionValue(),
		_scroll->movementValue()
	) | rpl::on_next([=](
			Ui::ElasticScrollPosition position,
			Ui::ElasticScrollMovement movement) {
		handleOverscroll(position, movement);
	}, _lifetime);

	_dwellTimer.setCallback([=] {
		if (_pulling && _pull >= float64(PullThreshold())) {
			_reached = true;
			_peakPull = _pull;
			base::Platform::Haptic();
			startExpand(true);
			pushIndicator();
		}
	});
}

PullToNextChannel::~PullToNextChannel() = default;

void PullToNextChannel::setHistory(History *history) {
	const auto mode = history ? Mode::History : Mode::None;
	if (_mode == mode && _history.get() == history) {
		return;
	}
	reset(anim::type::instant);
	_topic = nullptr;
	_nextTopic = nullptr;
	_history = history;
	_mode = mode;
	updatePullCurve();
}

void PullToNextChannel::setTopic(Data::ForumTopic *topic) {
	const auto mode = topic ? Mode::Topic : Mode::None;
	if (_mode == mode && _topic.get() == topic) {
		return;
	}
	reset(anim::type::instant);
	_history = nullptr;
	_next = nullptr;
	_topic = topic;
	_mode = mode;
	updatePullCurve();
}

void PullToNextChannel::updatePullCurve() {
	_scroll->setOverscrollPullDistances(0, active() ? PullThreshold() : 0);
}

bool PullToNextChannel::active() const {
	switch (_mode) {
	case Mode::History: {
		const auto history = _history.get();
		return Core::App().settings().pullToNextChannel()
			&& history
			&& history->peer->isBroadcast()
			&& atBottom()
			&& !_controller->session().sponsoredMessages().hasUnshownFor(
				history);
	}
	case Mode::Topic:
		return Core::App().settings().pullToNextChannel()
			&& _topic
			&& atBottom();
	case Mode::None:
		return false;
	}
	Unexpected("Mode in PullToNextChannel::active.");
}

bool PullToNextChannel::atBottom() const {
	if (_scroll->scrollTop() < _scroll->scrollTopMax()) {
		return false;
	} else if (_loadedAtBottom) {
		return _loadedAtBottom();
	}
	const auto history = _history.get();
	return history && history->loadedAtBottom();
}

void PullToNextChannel::handleOverscroll(
		Ui::ElasticScrollPosition position,
		Ui::ElasticScrollMovement movement) {
	using Phase = Ui::ElasticScrollMovement;
	updatePullCurve();
	const auto pull = std::max(position.overscroll, 0);
	const auto threshold = PullThreshold();
	if (!_pulling) {
		if (movement != Phase::Progress || pull <= 0 || !active()) {
			return;
		}
		if (_mode == Mode::History) {
			const auto history = _history.get();
			if (!history) {
				return;
			}
			_pulling = true;
			_committed = false;
			_next = FindNextUnreadChannel(_controller, history);
			if (const auto next = _next.get()) {
				if (!next->isReadyFor(ShowAtUnreadMsgId)) {
					[[maybe_unused]] const auto id = Support::SendPreloadRequest(
						next,
						[] {});
				}
				PreloadPinnedBar(next);
			}
		} else if (_mode == Mode::Topic) {
			const auto current = _topic.get();
			if (!current) {
				return;
			}
			const auto forum = current->forum();
			const auto completed = tr::lng_pull_no_unread_topics(
				tr::now,
				lt_group,
				forum->peer()->name());
			const auto next = FindNextUnreadTopic(current);
			if (!next && !forum->topicsList()->loaded()) {
				forum->requestTopics();
				return;
			}
			_pulling = true;
			_committed = false;
			_nextTopic = next;
			_topicCompleted = completed;
		} else {
			return;
		}
	}
	_pull = pull;
	_holding = (movement == Phase::Progress);
	const auto reached = (pull >= threshold);
	if (_reached) {
		// Collapse on a peak-relative reverse, not down to the threshold.
		_peakPull = std::max(_peakPull, float64(pull));
		const auto floor = threshold * kResetReachedOn;
		const auto resetAt = _peakPull - (_peakPull - floor) / kReverseEase;
		if (pull < resetAt) {
			_reached = false;
			_peakPull = 0.;
			startExpand(false);
		}
	} else if (reached) {
		// Arm the dwell on finger-down; a flick releases before it fires.
		if (!_dwellTimer.isActive() && movement == Phase::Progress) {
			_dwellTimer.callOnce(kReadyDwell);
		}
	} else if (pull < threshold * kResetReachedOn) {
		_dwellTimer.cancel();
	}
	pushIndicator();
	if (movement == Phase::Progress) {
		return;
	}
	_dwellTimer.cancel();
	if (!_committed) {
		_committed = true;
		if (_mode == Mode::History) {
			const auto next = _next.get();
			if (_reached
				&& next
				&& (next->unreadCount() > 0)
				&& active()) {
				_pulling = false;
				_jumping = true;
				_scroll->setContentBottomInset(
					int(base::SafeRound(_effective)));
				const auto weak = _next;
				crl::on_main(_parent.get(), [=] { jumpWhenReady(weak, 0); });
				return;
			}
		} else if (_mode == Mode::Topic) {
			const auto current = _topic.get();
			const auto next = _nextTopic.get();
			if (_reached
				&& current
				&& next
				&& (_topic.get() == current)
				&& (_nextTopic.get() == next)
				&& (next != current)
				&& (next->forum() == current->forum())
				&& MarkAsReadMenu::IsUnreadThread(next)
				&& active()) {
				_pulling = false;
				_jumping = true;
				_scroll->setContentBottomInset(
					int(base::SafeRound(_effective)));
				const auto weakCurrent = _topic;
				const auto weakNext = _nextTopic;
				crl::on_main(_parent.get(), [=] {
					jumpToTopic(weakCurrent, weakNext);
				});
				return;
			}
		}
	}
	if (pull <= 0) {
		reset(anim::type::normal);
	}
}

void PullToNextChannel::clearState() {
	_dwellTimer.cancel();
	_pulling = false;
	_holding = false;
	_committed = false;
	_jumping = false;
	_reached = false;
	_peakPull = 0.;
	_pull = 0.;
	_expandTo = false;
	_next = nullptr;
	_nextTopic = nullptr;
	_topicCompleted = QString();
}

void PullToNextChannel::reset(anim::type animated) {
	_expand.stop();
	clearState();
	_scroll->setContentBottomInset(0);
	_indicator->hideNow();
	if (animated == anim::type::instant) {
		_hint->hideNow();
	} else {
		_hint->hideAnimated();
	}
}

void PullToNextChannel::startExpand(bool ready) {
	const auto from = _expand.value(_expandTo ? 1. : 0.);
	_expandTo = ready;
	_expand.start(
		[=] { pushIndicator(); },
		from,
		ready ? 1. : 0.,
		kExpandDuration,
		ready ? anim::easeOutQuint : anim::sineInOut);
}

void PullToNextChannel::pushIndicator() {
	const auto card = float64(st::historyPullNextExpand);
	const auto threshold = float64(PullThreshold());
	const auto expand = _expand.value(_expandTo ? 1. : 0.);
	const auto effective = std::min(
		float64(st::historyPullNextMaxHeight),
		_pull + expand * (card - threshold));
	_effective = effective;
	_scroll->setContentBottomInset(std::max(0, int(base::SafeRound(
		_jumping ? effective : (effective - _pull)))));
	if (_mode == Mode::History) {
		const auto next = _next.get();
		_indicator->setHistoryData(effective, _reached, next);
		_hint->setData(hintVisible(), _reached, _mode, next != nullptr);
	} else if (_mode == Mode::Topic) {
		const auto next = _nextTopic.get();
		_indicator->setTopicData(
			effective,
			_reached,
			next,
			_topicCompleted);
		_hint->setData(hintVisible(), _reached, _mode, next != nullptr);
	}
}

bool PullToNextChannel::hintVisible() const {
	return _holding && (_pull > 0.);
}

void PullToNextChannel::updateGeometry() {
	const auto height = st::historyPullNextMaxHeight;
	_indicator->setGeometry(
		0,
		_scroll->height() - height,
		_scroll->width(),
		height);
	_indicator->raise();

	const auto top = _scroll->y() + _scroll->height();
	const auto bottom = _parent->height();
	if (bottom > top) {
		_hint->setGeometry(_scroll->x(), top, _scroll->width(), bottom - top);
		_hint->raise();
	}
}

void PullToNextChannel::jumpWhenReady(
		base::weak_ptr<History> weak,
		crl::time waited) {
	const auto next = weak.get();
	if (!next) {
		return;
	}
	constexpr auto kInterval = crl::time(100);
	constexpr auto kMaxWait = crl::time(1500);
	constexpr auto kPinnedMaxWait = crl::time(600);
	const auto historyReady = next->isReadyFor(ShowAtUnreadMsgId);
	const auto pinnedReady = (waited >= kPinnedMaxWait) || PinnedBarReady(next);
	if ((historyReady && pinnedReady) || waited >= kMaxWait) {
		jumpTo(next);
		return;
	}
	base::call_delayed(kInterval, _parent.get(), [=] {
		jumpWhenReady(weak, waited + kInterval);
	});
}

void PullToNextChannel::jumpTo(not_null<History*> history) {
	auto params = Window::SectionShow(Window::SectionShow::Way::ClearStack);
	params.slideFromBottom = true;
	_controller->showPeerHistory(history, params);
}

void PullToNextChannel::jumpToTopic(
		base::weak_ptr<Data::ForumTopic> weakCurrent,
		base::weak_ptr<Data::ForumTopic> weakNext) {
	const auto current = weakCurrent.get();
	const auto next = weakNext.get();
	if (!_jumping
		|| _mode != Mode::Topic
		|| !current
		|| !next
		|| (_topic.get() != current)
		|| (_nextTopic.get() != next)
		|| (next == current)
		|| (next->forum() != current->forum())
		|| !MarkAsReadMenu::IsUnreadThread(next)
		|| !active()) {
		reset(anim::type::normal);
		return;
	}
	auto params = Window::SectionShow(Window::SectionShow::Way::ClearStack);
	params.slideFromBottom = true;
	_controller->showTopic(next, ShowAtUnreadMsgId, params);
}

} // namespace HistoryView
