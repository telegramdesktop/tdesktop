/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_poll.h"

#include "core/click_handler_types.h"
#include "core/ui_integration.h" // TextContext
#include "data/data_cloud_file.h"
#include "data/data_location.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_reaction_preview.h"
#include "history/view/history_view_text_helper.h"
#include "history/view/media/menu/history_view_poll_menu.h"
#include "calls/calls_instance.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/image/image.h"
#include "ui/item_text_options.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "history/view/media/history_view_location.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/history_view_group_call_bar.h"
#include "data/data_media_types.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_file_origin.h"
#include "data/data_poll.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "base/crc32hash.h"
#include "base/unixtime.h"
#include "base/timer.h"
#include "base/qt/qt_key_modifiers.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_polls.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_polls.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"


namespace HistoryView {
namespace {

constexpr auto kShowRecentVotersCount = 3;
constexpr auto kRotateSegments = 8;
constexpr auto kRotateAmplitude = 3.;
constexpr auto kScaleSegments = 2;
constexpr auto kScaleAmplitude = 0.03;
constexpr auto kRollDuration = crl::time(400);

[[nodiscard]] int PollAnswerMediaSize() {
	return st::historyPollRadio.diameter * 2;
}

[[nodiscard]] int PollAnswerMediaSkip() {
	return st::historyPollPercentSkip * 2;
}

enum class PollThumbnailKind {
	None,
	Photo,
	Document,
	Audio,
	Emoji,
	Geo,
};

struct PercentCounterItem {
	int index = 0;
	int percent = 0;
	int remainder = 0;

	inline bool operator==(const PercentCounterItem &o) const {
		return remainder == o.remainder && percent == o.percent;
	}

	inline bool operator<(const PercentCounterItem &other) const {
		if (remainder > other.remainder) {
			return true;
		} else if (remainder < other.remainder) {
			return false;
		}
		return percent < other.percent;
	}
};

void AdjustPercentCount(gsl::span<PercentCounterItem> items, int left) {
	ranges::sort(items, std::less<>());
	for (auto i = 0, count = int(items.size()); i != count;) {
		const auto &item = items[i];
		auto j = i + 1;
		for (; j != count; ++j) {
			if (items[j].percent != item.percent
				|| items[j].remainder != item.remainder) {
				break;
			}
		}
		if (!items[i].remainder) {
			// If this item has correct value in 'percent' we don't want
			// to increment it to an incorrect one. This fixes a case with
			// four items with three votes for three different items.
			break;
		}
		const auto equal = j - i;
		if (equal <= left) {
			left -= equal;
			for (; i != j; ++i) {
				++items[i].percent;
			}
		} else {
			i = j;
		}
	}
}

void CountNicePercent(
		gsl::span<const int> votes,
		int total,
		gsl::span<int> result) {
	Expects(result.size() >= votes.size());
	Expects(votes.size() <= PollData::kMaxOptions);

	const auto count = size_type(votes.size());
	PercentCounterItem ItemsStorage[PollData::kMaxOptions];
	const auto items = gsl::make_span(ItemsStorage).subspan(0, count);
	auto left = 100;
	auto &&zipped = ranges::views::zip(
		votes,
		items,
		ranges::views::ints(0, int(items.size())));
	for (auto &&[votes, item, index] : zipped) {
		item.index = index;
		item.percent = (votes * 100) / total;
		item.remainder = (votes * 100) - (item.percent * total);
		left -= item.percent;
	}
	if (left > 0 && left <= count) {
		AdjustPercentCount(items, left);
	}
	for (const auto &item : items) {
		result[item.index] = item.percent;
	}
}

[[nodiscard]] uint32 HashPollShuffleValue(
		UserId userId,
		PollId pollId,
		const QByteArray &option) {
	auto hash = QByteArray::number(quint64(userId.bare))
		+ option
		+ QByteArray::number(quint64(pollId));
	return uint32(base::crc32(hash.constData(), hash.size()));
}

struct PollThumbnailData {
	std::shared_ptr<Ui::DynamicImage> thumbnail;
	ClickHandlerPtr handler;
	PollThumbnailKind kind = PollThumbnailKind::None;
	bool rounded = false;
	bool isVideo = false;
	uint64 id = 0;
};

[[nodiscard]] PollThumbnailData MakePollThumbnail(
		not_null<PollData*> poll,
		const PollAnswer &answer,
		Window::SessionController::MessageContext messageContext,
		Fn<bool()> paused = nullptr);

[[nodiscard]] PollThumbnailData MakePollThumbnail(
		not_null<PollData*> poll,
		const PollMedia &media,
		Window::SessionController::MessageContext messageContext,
		Fn<bool()> paused = nullptr) {
	auto result = PollThumbnailData();
	if (!media) {
		return result;
	}
	if (media.photo) {
		result.id = uint64(media.photo->id);
		result.thumbnail = Ui::MakePhotoThumbnailCenterCrop(
			media.photo,
			messageContext.id);
		result.rounded = true;
		result.kind = PollThumbnailKind::Photo;
	} else if (media.document) {
		result.id = uint64(media.document->id);
		if (media.document->sticker()) {
			result.thumbnail = Ui::MakeEmojiThumbnail(
				&poll->owner(),
				Data::SerializeCustomEmojiId(media.document),
				paused);
			result.kind = PollThumbnailKind::Emoji;
		} else if (media.document->isSong()
			|| media.document->isVoiceMessage()) {
			result.thumbnail = Ui::MakeDocumentFilePreviewThumbnail(
				media.document,
				messageContext.id);
			result.kind = PollThumbnailKind::Audio;
		} else {
			result.thumbnail = Ui::MakeDocumentThumbnailCenterCrop(
				media.document,
				messageContext.id);
			result.rounded = true;
			result.kind = PollThumbnailKind::Document;
			result.isVideo = media.document->isVideoFile()
				|| media.document->isVideoMessage()
				|| media.document->isAnimation();
		}
	} else if (media.geo) {
		result.id = uint64(media.geo->hash());
		const auto cloudImage = poll->owner().location(*media.geo);
		result.thumbnail = Ui::MakeGeoThumbnailWithPin(
			cloudImage,
			&poll->session(),
			Data::FileOrigin());
		result.rounded = true;
		result.kind = PollThumbnailKind::Geo;
	}
	if (result.kind == PollThumbnailKind::Photo && result.id) {
		const auto photo = media.photo;
		const auto session = &poll->session();
		result.handler = std::make_shared<LambdaClickHandler>(
			[=](ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				const auto controller = my.sessionWindow.get();
				if (!controller || (&controller->session() != session)) {
					return;
				}
				controller->openPhoto(photo, messageContext);
			});
	} else if ((result.kind == PollThumbnailKind::Document
		|| result.kind == PollThumbnailKind::Audio) && result.id) {
		const auto document = media.document;
		const auto session = &poll->session();
		result.handler = std::make_shared<LambdaClickHandler>(
			[=](ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				const auto controller = my.sessionWindow.get();
				if (!controller || (&controller->session() != session)) {
					return;
				}
				controller->openDocument(document, true, messageContext);
			});
	} else if (result.kind == PollThumbnailKind::Geo && media.geo) {
		const auto point = *media.geo;
		const auto session = &poll->session();
		result.handler = std::make_shared<LambdaClickHandler>(
			[=](ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				const auto controller = my.sessionWindow.get();
				if (!controller || (&controller->session() != session)) {
					return;
				}
				HistoryView::ShowPollGeoPreview(controller, point);
			});
	}
	return result;
}

PollThumbnailData MakePollThumbnail(
		not_null<PollData*> poll,
		const PollAnswer &answer,
		Window::SessionController::MessageContext messageContext,
		Fn<bool()> paused) {
	auto result
		= MakePollThumbnail(poll, answer.media, messageContext, paused);
	if (result.kind == PollThumbnailKind::Emoji && result.id) {
		const auto documentId = DocumentId(result.id);
		const auto option = answer.option;
		const auto session = &poll->session();
		result.handler = std::make_shared<LambdaClickHandler>(
			[=](ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				const auto controller = my.sessionWindow.get();
				if (!controller || (&controller->session() != session)) {
					return;
				}
				const auto document = poll->owner().document(documentId);
				const auto itemId = messageContext.id;
				ShowStickerPreview(
					controller,
					itemId,
					document,
					[=](not_null<Ui::DropdownMenu*> menu) {
						FillPollAnswerMenu(
							menu,
							poll,
							option,
							document,
							itemId,
							controller);
					});
			});
	}
	if (result.handler) {
		result.handler->setProperty(
			kPollOptionProperty,
			answer.option);
	}
	return result;
}

} // namespace

struct Poll::AnswerAnimation {
	anim::value percent;
	anim::value filling;
	anim::value opacity;
	bool chosen = false;
	bool correct = false;
};

struct Poll::AnswersAnimation {
	std::vector<AnswerAnimation> data;
	Ui::Animations::Simple progress;
};

struct Poll::SendingAnimation {
	template <typename Callback>
	SendingAnimation(
		const QByteArray &option,
		Callback &&callback);

	QByteArray option;
	Ui::InfiniteRadialAnimation animation;
};

struct Poll::Answer {
	Answer();

	void fillData(
		not_null<PollData*> poll,
		const PollAnswer &original,
		Ui::Text::MarkedContext context);
	void fillMedia(
		not_null<PollData*> poll,
		const PollAnswer &original,
		Window::SessionController::MessageContext messageContext,
		Fn<void()> repaint,
		Fn<bool()> paused);

	Ui::Text::String text;
	QByteArray option;
	int votes = 0;
	int votesPercent = 0;
	int votesPercentWidth = 0;
	float64 filling = 0.;
	QString votesPercentString;
	QString votesCountString;
	int votesCountWidth = 0;
	bool chosen = false;
	bool correct = false;
	bool selected = false;
	ClickHandlerPtr handler;
	ClickHandlerPtr mediaHandler;
	Ui::Animations::Simple selectedAnimation;
	std::shared_ptr<Ui::DynamicImage> thumbnail;
	bool thumbnailRounded = false;
	bool thumbnailIsVideo = false;
	PollThumbnailKind thumbnailKind = PollThumbnailKind::None;
	uint64 thumbnailId = 0;
	mutable std::unique_ptr<Ui::RippleAnimation> ripple;
	std::vector<UserpicInRow> recentVoters;
	mutable QImage recentVotersImage;
};

struct Poll::AttachedMedia {
	std::shared_ptr<Ui::DynamicImage> thumbnail;
	ClickHandlerPtr handler;
	PhotoData *photo = nullptr;
	std::shared_ptr<Data::PhotoMedia> photoMedia;
	QSize photoSize;
	PollThumbnailKind kind = PollThumbnailKind::None;
	bool rounded = false;
	uint64 id = 0;
};

struct Poll::SolutionMedia {
	PollThumbnailKind kind = PollThumbnailKind::None;
	uint64 id = 0;
};

struct Poll::RecentVoter {
	not_null<PeerData*> peer;
	mutable Ui::PeerUserpicView userpic;
};

struct Poll::Part {
	explicit Part(not_null<Poll*> owner) : _owner(owner) {}
	virtual ~Part() = default;

	[[nodiscard]] virtual int countHeight(int innerWidth) const = 0;
	virtual void draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const = 0;
	[[nodiscard]] virtual TextState textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const = 0;
	virtual void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {}
	virtual void clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {}
	[[nodiscard]] virtual bool hasHeavyPart() const { return false; }
	virtual void unloadHeavyPart() {}
	[[nodiscard]] virtual uint16 selectionLength() const { return 0; }

protected:
	const not_null<Poll*> _owner;
};

struct Poll::Footer : public Poll::Part {
	explicit Footer(not_null<Poll*> owner);

	int countHeight(int innerWidth) const override;
	void draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const override;
	TextState textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	void updateTotalVotes();

	Ui::Text::String _totalVotesLabel;
	Ui::Text::String _adminVotesLabel;
	Ui::Text::String _adminBackVoteLabel;
	ClickHandlerPtr _showResultsLink;
	ClickHandlerPtr _sendVotesLink;
	ClickHandlerPtr _adminVotesLink;
	ClickHandlerPtr _adminBackVoteLink;
	ClickHandlerPtr _saveOptionLink;
	mutable std::unique_ptr<Ui::RippleAnimation> _linkRipple;
	mutable int _linkRippleShift = 0;
	mutable base::Timer _closeTimer;

private:
	[[nodiscard]] int topSkip() const;
	[[nodiscard]] int textTop() const;
	[[nodiscard]] bool hasTimerLine(int innerWidth) const;
	[[nodiscard]] bool hasCloseDate() const;
	[[nodiscard]] QString closeTimerText() const;
	[[nodiscard]] bool timerFooterMultiline(int paintw) const;
	[[nodiscard]] bool centeredOverlapsInfo(
		int textWidth,
		int innerWidth) const;
	[[nodiscard]] int bottomLineWidth(int innerWidth) const;
	[[nodiscard]] int dateInfoPadding(int innerWidth) const;
	void toggleLinkRipple(bool pressed);
};

int Poll::Footer::topSkip() const {
	return _owner->canAddOption()
		? 0
		: st::historyPollTotalVotesSkip;
}

int Poll::Footer::textTop() const {
	return topSkip()
		+ st::msgPadding.bottom()
		+ st::historyPollBottomButtonTop;
}

bool Poll::Footer::hasCloseDate() const {
	return _owner->_poll->closeDate > 0
		&& !(_owner->_flags & PollData::Flag::Closed);
}

bool Poll::Footer::hasTimerLine(int innerWidth) const {
	if (_owner->inlineFooter() || _owner->showVotersCount()) {
		return timerFooterMultiline(innerWidth);
	}
	return hasCloseDate();
}

int Poll::Footer::countHeight(int innerWidth) const {
	const auto inline_ = _owner->inlineFooter();
	const auto top = topSkip();
	const auto buttonSkip = inline_
		? 0
		: st::historyPollBottomButtonSkip;
	const auto timerLine = hasTimerLine(innerWidth);
	return top
		+ buttonSkip
		+ st::msgDateFont->height
		+ (timerLine ? st::msgDateFont->height : 0)
		+ dateInfoPadding(innerWidth)
		+ st::msgPadding.bottom();
}

void Poll::Footer::draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const {
	const auto stm = context.messageStyle();
	const auto inline_ = _owner->inlineFooter();

	if (inline_) {
		const auto top = st::msgPadding.bottom();
		p.setPen(stm->msgDateFg);
		const auto timerText = closeTimerText();
		if (timerText.isEmpty()) {
			const auto labelWidth = _totalVotesLabel.maxWidth();
			const auto labelLeft = left + (innerWidth - labelWidth) / 2;
			_totalVotesLabel.drawLeftElided(
				p,
				labelLeft,
				top,
				labelWidth,
				outerWidth);
		} else if (timerFooterMultiline(innerWidth)) {
			const auto labelWidth = _totalVotesLabel.maxWidth();
			const auto labelLeft = left + (innerWidth - labelWidth) / 2;
			_totalVotesLabel.drawLeftElided(
				p,
				labelLeft,
				top,
				labelWidth,
				outerWidth);
			p.setFont(st::msgDateFont);
			const auto timerw = st::msgDateFont->width(timerText);
			p.drawTextLeft(
				left + (innerWidth - timerw) / 2,
				top + st::msgDateFont->height,
				outerWidth,
				timerText,
				timerw);
		} else {
			p.setFont(st::msgDateFont);
			const auto sep = QString::fromUtf8(" \xC2\xB7 ");
			const auto full = _totalVotesLabel.toString()
				+ sep
				+ timerText;
			const auto fullw = st::msgDateFont->width(full);
			p.drawTextLeft(
				left + (innerWidth - fullw) / 2,
				top,
				outerWidth,
				full,
				fullw);
		}
		return;
	}

	const auto stringtop = textTop();

	if (_linkRipple) {
		const auto rippleTop = topSkip();
		p.setOpacity(st::historyPollRippleOpacity);
		_linkRipple->paint(
			p,
			left - st::msgPadding.left() - _linkRippleShift,
			rippleTop,
			outerWidth,
			&stm->msgWaveformInactive->c);
		if (_linkRipple->empty()) {
			_linkRipple.reset();
		}
		p.setOpacity(1.);
	}
	if (_owner->_addOptionActive) {
		p.setFont(st::semiboldFont);
		p.setPen(stm->msgFileThumbLinkFg);
		const auto text = tr::lng_polls_add_option_save(tr::now);
		const auto textw = st::semiboldFont->width(text);
		p.drawTextLeft(
			left + (innerWidth - textw) / 2,
			stringtop,
			outerWidth,
			text,
			textw);
		return;
	}
	if (_owner->isAuthorNotVoted()
		&& !_owner->_adminShowResults
		&& !_owner->canSendVotes()) {
		if (_owner->_totalVotes > 0) {
			p.setPen(stm->msgFileThumbLinkFg);
			const auto labelWidth = _adminVotesLabel.maxWidth();
			_adminVotesLabel.drawLeft(
				p,
				left + (innerWidth - labelWidth) / 2,
				stringtop,
				labelWidth,
				outerWidth);
		} else {
			p.setPen(stm->msgDateFg);
			const auto textw = _totalVotesLabel.maxWidth();
			_totalVotesLabel.drawLeft(
				p,
				left + (innerWidth - textw) / 2,
				stringtop,
				textw,
				outerWidth);
		}
	} else if (_owner->_adminShowResults && _owner->isAuthorNotVoted()) {
		p.setPen(stm->msgFileThumbLinkFg);
		const auto backw = _adminBackVoteLabel.maxWidth();
		_adminBackVoteLabel.drawLeft(
			p,
			left + (innerWidth - backw) / 2,
			stringtop,
			backw,
			outerWidth);
	} else if (_owner->showVotersCount()) {
		_linkRipple.reset();
		p.setPen(stm->msgDateFg);
		const auto timerText = closeTimerText();
		if (timerText.isEmpty()) {
			const auto labelWidth = _totalVotesLabel.maxWidth();
			const auto labelLeft = left + (innerWidth - labelWidth) / 2;
			_totalVotesLabel.draw(
				p,
				labelLeft,
				stringtop,
				labelWidth,
				style::al_top);
		} else if (timerFooterMultiline(innerWidth)) {
			const auto labelWidth = _totalVotesLabel.maxWidth();
			const auto labelLeft = left + (innerWidth - labelWidth) / 2;
			_totalVotesLabel.draw(
				p,
				labelLeft,
				stringtop,
				labelWidth,
				style::al_top);
			p.setFont(st::msgDateFont);
			const auto timerw = st::msgDateFont->width(timerText);
			p.drawTextLeft(
				left + (innerWidth - timerw) / 2,
				stringtop + st::msgDateFont->height,
				outerWidth,
				timerText,
				timerw);
		} else {
			p.setFont(st::msgDateFont);
			const auto sep = QString::fromUtf8(" \xC2\xB7 ");
			const auto full = _totalVotesLabel.toString()
				+ sep
				+ timerText;
			const auto fullw = st::msgDateFont->width(full);
			p.drawTextLeft(
				left + (innerWidth - fullw) / 2,
				stringtop,
				outerWidth,
				full,
				fullw);
		}
	} else {
		const auto votedPublic = _owner->_voted
			&& (_owner->_flags & PollData::Flag::PublicVotes);
		const auto link = (_owner->showVotes() || votedPublic)
			? _showResultsLink
			: _owner->canSendVotes()
			? _sendVotesLink
			: nullptr;
		p.setFont(st::semiboldFont);
		p.setPen(link ? stm->msgFileThumbLinkFg : stm->msgDateFg);
		const auto string = (_owner->showVotes() || votedPublic)
			? ((_owner->_flags & PollData::Flag::PublicVotes)
				? tr::lng_polls_view_votes(
					tr::now,
					lt_count,
					_owner->_totalVotes)
				: tr::lng_polls_view_results(tr::now))
			: tr::lng_polls_submit_votes(tr::now);
		const auto stringw = st::semiboldFont->width(string);
		p.drawTextLeft(
			left + (innerWidth - stringw) / 2,
			stringtop,
			outerWidth,
			string,
			stringw);
		const auto timerText = closeTimerText();
		if (!timerText.isEmpty()) {
			p.setFont(st::msgDateFont);
			p.setPen(stm->msgDateFg);
			const auto timerw = st::msgDateFont->width(timerText);
			p.drawTextLeft(
				left + (innerWidth - timerw) / 2,
				stringtop + st::semiboldFont->height,
				outerWidth,
				timerText,
				timerw);
		}
	}
}

TextState Poll::Footer::textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const {
	TextState result;
	if (_owner->inlineFooter()) {
		return result;
	}
	const auto top = topSkip();
	const auto h = countHeight(innerWidth);
	if (point.y() < top || point.y() >= h) {
		return result;
	}
	_owner->_lastLinkPoint = point;
	if (_owner->_addOptionActive) {
		result.link = _saveOptionLink;
	} else if (_owner->isAuthorNotVoted()
		&& !_owner->_adminShowResults
		&& !_owner->canSendVotes()) {
		if (_owner->_totalVotes > 0) {
			result.link = _adminVotesLink;
		}
	} else if (_owner->_adminShowResults && _owner->isAuthorNotVoted()) {
		result.link = _adminBackVoteLink;
	} else if (!_owner->showVotersCount()) {
		const auto votedPublic = _owner->_voted
			&& (_owner->_flags & PollData::Flag::PublicVotes);
		result.link = (_owner->showVotes() || votedPublic)
			? _showResultsLink
			: _owner->canSendVotes()
			? _sendVotesLink
			: nullptr;
	}
	return result;
}

void Poll::Footer::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (handler == _sendVotesLink
		|| handler == _showResultsLink
		|| handler == _adminVotesLink
		|| handler == _adminBackVoteLink
		|| handler == _saveOptionLink) {
		toggleLinkRipple(pressed);
	}
}

void Poll::Footer::toggleLinkRipple(bool pressed) {
	if (pressed) {
		const auto outerWidth = _owner->width();
		const auto h = countHeight(
			outerWidth - st::msgPadding.left() - st::msgPadding.right());
		const auto rippleTop = topSkip();
		const auto linkHeight = h - rippleTop;
		if (!_linkRipple) {
			auto mask = _owner->isRoundedInBubbleBottom()
				? static_cast<Message*>(_owner->_parent.get())
					->bottomRippleMask(linkHeight)
				: BottomRippleMask{
					Ui::RippleAnimation::RectMask(
						{ outerWidth, linkHeight }),
				};
			_linkRipple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask.image),
				[owner = _owner] { owner->repaint(); });
			_linkRippleShift = mask.shift;
		}
		_linkRipple->add(
			_owner->_lastLinkPoint
				+ QPoint(_linkRippleShift, -rippleTop));
	} else if (_linkRipple) {
		_linkRipple->lastStop();
	}
}

void Poll::Footer::updateTotalVotes() {
	if (_owner->_totalVotes == _owner->_poll->totalVoters
		&& !_totalVotesLabel.isEmpty()) {
		return;
	}
	_owner->_totalVotes = _owner->_poll->totalVoters;
	const auto quiz = _owner->_poll->quiz();
	const auto string = !_owner->_totalVotes
		? (quiz
			? tr::lng_polls_answers_none
			: tr::lng_polls_votes_none)(tr::now)
		: (quiz
			? tr::lng_polls_answers_count
			: tr::lng_polls_votes_count)(
				tr::now,
				lt_count_short,
				_owner->_totalVotes);
	_totalVotesLabel.setText(st::msgDateTextStyle, string);
	_adminVotesLabel.setMarkedText(
		st::semiboldTextStyle,
		tr::lng_polls_admin_votes(
			tr::now,
			lt_count,
			_owner->_totalVotes,
			lt_arrow,
			Ui::Text::IconEmoji(&st::textMoreIconEmoji),
			tr::marked));
}

struct Poll::AddOption : public Poll::Part {
	explicit AddOption(not_null<Poll*> owner);

	int countHeight(int innerWidth) const override;
	void draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const override;
	TextState textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	ClickHandlerPtr _addOptionLink;
	mutable std::unique_ptr<Ui::RippleAnimation> _addOptionRipple;

private:
	[[nodiscard]] int rowHeight() const;
	void toggleRipple(bool pressed);
};

Poll::AddOption::AddOption(not_null<Poll*> owner)
: Part(owner)
, _addOptionLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		owner.get(),
		[=](ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto delegate = my.elementDelegate
				? my.elementDelegate()
				: nullptr) {
				const auto innerWidth = owner->width()
					- st::msgPadding.left()
					- st::msgPadding.right();
				delegate->elementShowAddPollOption(
					owner->_parent,
					owner->_poll,
					owner->_parent->data()->fullId(),
					owner->addOptionRect(innerWidth));
			}
		}))) {
}

int Poll::AddOption::rowHeight() const {
	return st::historyPollAnswerPaddingNoMedia.top()
		+ st::msgDateFont->height
		+ st::historyPollAnswerPaddingNoMedia.bottom();
}

int Poll::AddOption::countHeight(int innerWidth) const {
	return _owner->canAddOption() ? rowHeight() : 0;
}

void Poll::AddOption::draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const {
	if (!_owner->canAddOption() || _owner->_addOptionActive) {
		return;
	}
	const auto stm = context.messageStyle();
	const auto &padding = st::historyPollAnswerPaddingNoMedia;
	const auto textTop = padding.top();

	if (_addOptionRipple) {
		p.setOpacity(st::historyPollRippleOpacity);
		_addOptionRipple->paint(
			p,
			left - st::msgPadding.left(),
			0,
			outerWidth,
			&stm->msgWaveformInactive->c);
		if (_addOptionRipple->empty()) {
			_addOptionRipple.reset();
		}
		p.setOpacity(1.);
	}

	const auto color = stm->msgDateFg->c;
	const auto &icon = st::pollBoxOutlinePollAddIcon;
	const auto &radio = st::historyPollRadio;
	const auto iconLeft = left
		+ (radio.diameter - icon.width()) / 2;
	const auto iconTop = textTop
		+ (st::msgDateFont->height - icon.height()) / 2;
	icon.paint(p, iconLeft, iconTop, outerWidth, color);

	p.setFont(st::normalFont);
	p.setPen(stm->msgDateFg);
	const auto text = tr::lng_polls_add_option(tr::now);
	const auto textw = st::normalFont->width(text);
	p.drawTextLeft(
		left + st::historyPollAnswerPadding.left(),
		textTop,
		outerWidth,
		text,
		textw);
}

TextState Poll::AddOption::textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const {
	TextState result;
	if (!_owner->canAddOption()) {
		return result;
	}
	const auto h = rowHeight();
	if (point.y() >= 0 && point.y() < h) {
		_owner->_lastLinkPoint = point;
		result.link = _addOptionLink;
	}
	return result;
}

void Poll::AddOption::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (handler == _addOptionLink) {
		toggleRipple(pressed);
	}
}

void Poll::AddOption::toggleRipple(bool pressed) {
	if (pressed) {
		const auto outerWidth = _owner->width();
		const auto h = rowHeight();
		if (!_addOptionRipple) {
			auto mask = Ui::RippleAnimation::RectMask(
				QSize(outerWidth, h));
			_addOptionRipple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[owner = _owner] { owner->repaint(); });
		}
		_addOptionRipple->add(_owner->_lastLinkPoint);
	} else if (_addOptionRipple) {
		_addOptionRipple->lastStop();
	}
}

struct Poll::Header : public Poll::Part {
	explicit Header(not_null<Poll*> owner)
	: Part(owner)
	, _description(st::msgMinWidth / 2)
	, _question(st::msgMinWidth / 2)
	, _attachedMedia(std::make_unique<AttachedMedia>()) {
	}

	int countHeight(int innerWidth) const override;
	void draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const override;
	TextState textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const override;
	void clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;
	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;
	uint16 selectionLength() const override;

	void updateDescription();
	void updateAttachedMedia();
	[[nodiscard]] int countTopContentSkip(int pollWidth = 0) const;
	[[nodiscard]] int countTopMediaHeight(int pollWidth = 0) const;
	[[nodiscard]] int countAttachHeight(int pollWidth = 0) const;
	[[nodiscard]] QRect countTopMediaRect(int top) const;
	[[nodiscard]] Ui::BubbleRounding topMediaRounding() const;
	void validateTopMediaCache(QSize size) const;
	[[nodiscard]] int countDescriptionHeight(int innerWidth) const;
	[[nodiscard]] int countQuestionTop(
		int innerWidth,
		int pollWidth = 0) const;
	[[nodiscard]] uint16 solutionSelectionLength() const;
	[[nodiscard]] TextSelection toSolutionSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection fromSolutionSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection toQuestionSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection fromQuestionSelection(
		TextSelection selection) const;
	void updateRecentVoters();
	void paintRecentVoters(
		Painter &p,
		int left,
		int top,
		const PaintContext &context) const;
	void paintShowSolution(
		Painter &p,
		int right,
		int top,
		const PaintContext &context) const;
	void showSolution() const;
	void solutionToggled(
		bool solutionShown,
		anim::type animated = anim::type::normal) const;
	[[nodiscard]] bool canShowSolution() const;
	[[nodiscard]] bool inShowSolution(
		QPoint point,
		int right,
		int top) const;
	void updateSolutionText();
	void updateSolutionMedia();
	[[nodiscard]] int countSolutionBlockHeight(int innerWidth) const;
	[[nodiscard]] int countSolutionMediaHeight(int mediaWidth) const;
	void paintSolutionBlock(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const;

	Ui::Text::String _description;
	Ui::Text::String _question;
	Ui::Text::String _subtitle;
	std::unique_ptr<AttachedMedia> _attachedMedia;
	std::unique_ptr<Media> _attachedMediaAttach;
	mutable QImage _attachedMediaCache;
	mutable Ui::BubbleRounding _attachedMediaCacheRounding;
	std::vector<RecentVoter> _recentVoters;
	QImage _recentVotersImage;
	mutable ClickHandlerPtr _showSolutionLink;
	Ui::Text::String _solutionText;
	mutable ClickHandlerPtr _closeSolutionLink;
	std::unique_ptr<SolutionMedia> _solutionMedia;
	std::unique_ptr<Media> _solutionAttach;
	mutable Ui::Animations::Simple _solutionButtonAnimation;
	mutable bool _solutionShown = false;
	mutable bool _solutionButtonVisible = false;
	mutable QImage _userpicCircleCache;
};

int Poll::Header::countHeight(int innerWidth) const {
	const auto pollWidth = innerWidth
		+ st::msgPadding.left()
		+ st::msgPadding.right();
	return countQuestionTop(innerWidth, pollWidth)
		+ _question.countHeight(innerWidth)
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip;
}

void Poll::Header::draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const {
	const auto stm = context.messageStyle();
	auto tshift = countTopContentSkip();

	if (const auto mediaHeight = countTopMediaHeight()) {
		if (_attachedMediaAttach) {
			const auto sideSkip = st::historyPollMediaSideSkip;
			_attachedMediaAttach->setBubbleRounding(
				topMediaRounding());
			p.translate(sideSkip, tshift);
			_attachedMediaAttach->draw(
				p,
				context.translated(-sideSkip, -tshift)
					.withSelection(TextSelection()));
			p.translate(-sideSkip, -tshift);
		} else {
			const auto target = countTopMediaRect(tshift);
			p.setPen(Qt::NoPen);
			p.setBrush(stm->msgFileBg);
			PainterHighQualityEnabler hq(p);
			if (_attachedMedia->kind
					== PollThumbnailKind::Emoji) {
				p.drawRoundedRect(
					target,
					st::roundRadiusLarge,
					st::roundRadiusLarge);
				const auto image
					= _attachedMedia->thumbnail->image(
						std::max(target.width(), target.height()));
				if (!image.isNull()) {
					const auto source = QRectF(
						QPointF(),
						QSizeF(image.size()));
					const auto kx = target.width() / source.width();
					const auto ky = target.height() / source.height();
					const auto scale = std::min(kx, ky);
					const auto imageSize = QSizeF(
						source.width() * scale,
						source.height() * scale);
					const auto geometry = QRectF(
						target.x()
							+ (target.width()
								- imageSize.width()) / 2.,
						target.y()
							+ (target.height()
								- imageSize.height()) / 2.,
						imageSize.width(),
						imageSize.height());
					p.save();
					auto path = QPainterPath();
					path.addRoundedRect(
						target,
						st::roundRadiusLarge,
						st::roundRadiusLarge);
					p.setClipPath(path);
					p.drawImage(geometry, image, source);
					p.restore();
				}
			} else {
				validateTopMediaCache(target.size());
				if (!_attachedMediaCache.isNull()) {
					p.drawImage(target.topLeft(),
						_attachedMediaCache);
				}
			}
		}
		tshift += mediaHeight + st::historyPollMediaSkip;
	}

	if (const auto descriptionHeight
			= countDescriptionHeight(innerWidth)) {
		p.setPen(stm->historyTextFg);
		_owner->_parent->prepareCustomEmojiPaint(
			p, context, _description);
		_description.draw(p, {
			.position = { left, tshift },
			.outerWidth = outerWidth,
			.availableWidth = innerWidth,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused,
			.pausedSpoiler = context.paused,
			.selection = context.selection,
			.useFullWidth = true,
		});
		tshift += descriptionHeight + st::historyPollDescriptionSkip;
	}

	if (const auto solutionHeight
			= countSolutionBlockHeight(innerWidth)) {
		paintSolutionBlock(
			p, left, tshift, innerWidth, context);
		tshift += solutionHeight + st::historyPollExplanationSkip;
	}

	p.setPen(stm->historyTextFg);
	_owner->_parent->prepareCustomEmojiPaint(p, context, _question);
	_question.draw(p, {
		.position = { left, tshift },
		.outerWidth = outerWidth,
		.availableWidth = innerWidth,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused,
		.pausedSpoiler = context.paused,
		.selection = toQuestionSelection(context.selection),
	});
	tshift += _question.countHeight(innerWidth)
		+ st::historyPollSubtitleSkip;

	p.setPen(stm->msgDateFg);
	_subtitle.drawLeftElided(
		p, left, tshift, innerWidth, outerWidth);
	paintRecentVoters(
		p, left + _subtitle.maxWidth(), tshift, context);
	paintShowSolution(
		p, left + innerWidth, tshift, context);
}

TextState Poll::Header::textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const {
	TextState result(_owner->_parent);
	auto tshift = countTopContentSkip();

	if (const auto mediaHeight = countTopMediaHeight()) {
		if (_attachedMediaAttach) {
			const auto sideSkip = st::historyPollMediaSideSkip;
			if (QRect(
					sideSkip,
					tshift,
					_attachedMediaAttach->width(),
					mediaHeight).contains(point)) {
				result = _attachedMediaAttach->textState(
					point - QPoint(sideSkip, tshift),
					request);
				result.symbol = 0;
				return result;
			}
		} else if (_attachedMedia
			&& _attachedMedia->handler
			&& QRect(0, tshift, outerWidth, mediaHeight)
				.contains(point)) {
			result.link = _attachedMedia->handler;
			return result;
		}
		tshift += mediaHeight + st::historyPollMediaSkip;
	}

	auto symbolAdd = uint16(0);
	if (const auto descriptionHeight
			= countDescriptionHeight(innerWidth)) {
		if (QRect(left, tshift, innerWidth, descriptionHeight)
				.contains(point)) {
			result = TextState(
				_owner->_parent,
				_description.getStateLeft(
					point - QPoint(left, tshift),
					innerWidth,
					outerWidth,
					request.forText()));
			return result;
		}
		if (point.y() >= tshift + descriptionHeight) {
			symbolAdd += _description.length();
		}
		tshift += descriptionHeight + st::historyPollDescriptionSkip;
	}

	if (const auto solutionHeight
			= countSolutionBlockHeight(innerWidth)) {
		if (QRect(left, tshift, innerWidth, solutionHeight)
				.contains(point)) {
			const auto &qst = st::historyPagePreview;
			const auto innerLeft = left + qst.padding.left();
			const auto innerRight = left
				+ innerWidth
				- qst.padding.right();
			const auto closeArea
				= st::historyPollExplanationCloseSize;
			const auto closeLeft = innerRight - closeArea;
			const auto closeTop = tshift + qst.padding.top();
			if (QRect(
				closeLeft,
				closeTop,
				closeArea,
				st::semiboldFont->height).contains(point)) {
				result.link = _closeSolutionLink;
				return result;
			}
			const auto textTop = tshift
				+ qst.padding.top()
				+ st::semiboldFont->height
				+ st::historyPollExplanationTitleSkip;
			const auto textWidth = innerRight - innerLeft;
			const auto textHeight
				= _solutionText.countHeight(textWidth);
			if (QRect(
				innerLeft,
				textTop,
				textWidth,
				textHeight).contains(point)) {
				result = TextState(
					_owner->_parent,
					_solutionText.getStateLeft(
						point - QPoint(innerLeft, textTop),
						textWidth,
						outerWidth,
						request.forText()));
				result.symbol += symbolAdd;
				return result;
			}
			if (_solutionAttach) {
				if (const auto mh
						= countSolutionMediaHeight(
							textWidth)) {
					const auto mediaTop = textTop
						+ textHeight
						+ st::historyPollExplanationMediaSkip;
					const auto isDocument = _solutionMedia
						&& (_solutionMedia->kind
								== PollThumbnailKind::Document
							|| _solutionMedia->kind
								== PollThumbnailKind::Audio);
					const auto isThumbed = isDocument
						&& _owner->_poll->solutionMedia.document
						&& _owner->_poll->solutionMedia.document
							->hasThumbnail()
						&& !_owner->_poll->solutionMedia.document
							->isSong();
					const auto &fileSt = isThumbed
						? st::msgFileThumbLayout
						: st::msgFileLayout;
					const auto shift = isDocument
						? fileSt.padding.left()
						: 0;
					const auto mediaLeft = innerLeft - shift;
					if (QRect(
							mediaLeft,
							mediaTop,
							_solutionAttach->width(),
							mh).contains(point)) {
						result = _solutionAttach->textState(
							point - QPoint(mediaLeft, mediaTop),
							request);
						result.symbol = 0;
					}
				}
			}
			return result;
		}
		if (point.y() >= tshift + solutionHeight) {
			symbolAdd += _solutionText.length();
		}
		tshift += solutionHeight + st::historyPollExplanationSkip;
	}

	const auto questionH = _question.countHeight(innerWidth);
	if (QRect(left, tshift, innerWidth, questionH).contains(point)) {
		result = TextState(
			_owner->_parent,
			_question.getState(
				point - QPoint(left, tshift),
				innerWidth,
				request.forText()));
		result.symbol += symbolAdd;
		return result;
	}
	if (point.y() >= tshift + questionH) {
		symbolAdd += _question.length();
	}
	tshift += questionH + st::historyPollSubtitleSkip;
	if (inShowSolution(
			point, left + innerWidth, tshift)) {
		result.link = _showSolutionLink;
		return result;
	}

	return result;
}

void Poll::Header::clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {
	if (_attachedMediaAttach) {
		_attachedMediaAttach->clickHandlerActiveChanged(
			handler, active);
	}
}

void Poll::Header::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (_attachedMediaAttach) {
		_attachedMediaAttach->clickHandlerPressedChanged(
			handler, pressed);
	}
	if (_solutionAttach) {
		_solutionAttach->clickHandlerPressedChanged(
			handler, pressed);
	}
}

bool Poll::Header::hasHeavyPart() const {
	for (const auto &recent : _recentVoters) {
		if (!recent.userpic.null()) {
			return true;
		}
	}
	return false;
}

void Poll::Header::unloadHeavyPart() {
	for (auto &recent : _recentVoters) {
		recent.userpic = {};
	}
}

uint16 Poll::Header::selectionLength() const {
	return _owner->fullSelectionLength();
}

struct Poll::Options : public Poll::Part {
	using Part::Part;

	int countHeight(int innerWidth) const override;
	void draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const override;
	TextState textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;
	bool hasHeavyPart() const override;

	void checkSendingAnimation() const;
	void unloadHeavyPart() override;

	[[nodiscard]] int countAnswerContentWidth(
		const Answer &answer,
		int innerWidth) const;
	[[nodiscard]] int countVotesExtraHeight(
		const Answer &answer,
		int textWidth) const;
	[[nodiscard]] int countAnswerHeight(
		const Answer &answer,
		int innerWidth) const;
	void resetAnswersAnimation() const;
	void radialAnimationCallback() const;
	int paintAnswer(
		Painter &p,
		const Answer &answer,
		const AnswerAnimation *animation,
		int left,
		int top,
		int width,
		int outerWidth,
		const PaintContext &context) const;
	void paintRadio(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		const PaintContext &context) const;
	void paintPercent(
		Painter &p,
		const QString &percent,
		int percentWidth,
		int left,
		int top,
		int topPadding,
		int outerWidth,
		const PaintContext &context) const;
	void paintFilling(
		Painter &p,
		bool chosen,
		bool correct,
		float64 filling,
		int left,
		int top,
		int topPadding,
		int width,
		int contentWidth,
		int contentHeight,
		const PaintContext &context) const;
	[[nodiscard]] bool checkAnimationStart() const;
	[[nodiscard]] bool answerVotesChanged() const;
	void saveStateInAnimation() const;
	void startAnswersAnimation() const;
	void toggleRipple(Answer &answer, bool pressed);
	void toggleMultiOption(const QByteArray &option);
	void sendMultiOptions();
	void showResults();
	void showAnswerVotesTooltip(const QByteArray &option);
	void checkQuizAnswered();
	[[nodiscard]] ClickHandlerPtr createAnswerClickHandler(
		const Answer &answer);
	void updateAnswers();
	void updateAnswerVotes();
	void updateAnswerVotesFromOriginal(
		Answer &answer,
		const PollAnswer &original,
		int percent,
		int maxVotes,
		bool showPercent);

	std::vector<Answer> _answers;
	mutable std::unique_ptr<AnswersAnimation> _answersAnimation;
	mutable std::unique_ptr<SendingAnimation> _sendingAnimation;
	mutable QImage _fillingIconCache;
	bool _anyAnswerHasMedia = false;
	bool _hasSelected = false;
	bool _votedFromHere = false;
};

int Poll::Options::countHeight(int innerWidth) const {
	auto result = ranges::accumulate(ranges::views::all(
		_answers
	) | ranges::views::transform([&](const Answer &answer) {
		return countAnswerHeight(answer, innerWidth);
	}), 0);
	if (_owner->canAddOption()) {
		result += (st::historyPollChoiceRight.height()
			- st::historyPollFillingHeight) / 2;
	}
	return result;
}

void Poll::Options::draw(
		Painter &p,
		int left,
		int innerWidth,
		int outerWidth,
		const PaintContext &context) const {
	checkSendingAnimation();

	const auto progress = _answersAnimation
		? _answersAnimation->progress.value(1.)
		: 1.;
	if (progress == 1.) {
		resetAnswersAnimation();
	}

	auto tshift = 0;
	auto &&answers = ranges::views::zip(
		_answers,
		ranges::views::ints(0, int(_answers.size())));
	for (const auto &[answer, index] : answers) {
		const auto animation = _answersAnimation
			? &_answersAnimation->data[index]
			: nullptr;
		if (animation) {
			animation->percent.update(progress, anim::linear);
			animation->filling.update(
				progress,
				_owner->showVotes()
					? anim::easeOutCirc
					: anim::linear);
			animation->opacity.update(progress, anim::linear);
		}
		const auto height = paintAnswer(
			p,
			answer,
			animation,
			left,
			tshift,
			innerWidth,
			outerWidth,
			context);
		tshift += height;
	}
}

TextState Poll::Options::textState(
		QPoint point,
		int left,
		int innerWidth,
		int outerWidth,
		StateRequest request) const {
	TextState result;
	const auto can = _owner->canVote();
	const auto show = _owner->showVotes();

	auto tshift = 0;
	for (const auto &answer : _answers) {
		const auto height = countAnswerHeight(answer, innerWidth);
		if (point.y() >= tshift && point.y() < tshift + height) {
			const auto media = answer.thumbnail
				? PollAnswerMediaSize()
				: 0;
			if (media
				&& answer.mediaHandler
				&& QRect(
					left + innerWidth
						- st::historyPollAnswerPadding.right()
						- media,
					tshift + (answer.thumbnail
						? st::historyPollAnswerPadding
						: st::historyPollAnswerPaddingNoMedia).top(),
					media,
					media).contains(point)) {
				result.link = answer.mediaHandler;
			} else {
				const auto &answerPadding = answer.thumbnail
					? st::historyPollAnswerPadding
					: st::historyPollAnswerPaddingNoMedia;
				const auto aleft = left
					+ st::historyPollAnswerPadding.left();
				const auto atop = tshift + answerPadding.top();
				const auto textWidth = countAnswerContentWidth(
					answer,
					innerWidth);
				const auto textState = answer.text.getStateLeft(
					point - QPoint(aleft, atop),
					textWidth,
					outerWidth,
					request.forText());
				if (textState.link) {
					result.link = textState.link;
				} else {
					if (can) {
						_owner->_lastLinkPoint = point;
					}
					result.link = answer.handler;
				}
			}
			if (!can && show) {
				result.customTooltip = true;
				using Flag = Ui::Text::StateRequest::Flag;
				if (request.flags & Flag::LookupCustomTooltip) {
					const auto quiz = _owner->_poll->quiz();
					result.customTooltipText = answer.votes
						? (quiz
							? tr::lng_polls_answers_count
							: tr::lng_polls_votes_count)(
								tr::now,
								lt_count_decimal,
								answer.votes)
						: (quiz
							? tr::lng_polls_answers_none
							: tr::lng_polls_votes_none)(tr::now);
				}
			}
			return result;
		}
		tshift += height;
	}
	return result;
}

void Poll::Options::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	const auto i = ranges::find(
		_answers,
		handler,
		&Answer::handler);
	if (i != end(_answers)) {
		if (_owner->canVote()) {
			toggleRipple(*i, pressed);
		}
	}
}

bool Poll::Options::hasHeavyPart() const {
	for (const auto &answer : _answers) {
		if (!answer.recentVotersImage.isNull()) {
			return true;
		}
	}
	return false;
}

void Poll::Options::unloadHeavyPart() {
	for (auto &answer : _answers) {
		for (auto &recent : answer.recentVoters) {
			recent.view = {};
		}
		answer.recentVotersImage = QImage();
	}
}

template <typename Callback>
Poll::SendingAnimation::SendingAnimation(
	const QByteArray &option,
	Callback &&callback)
: option(option)
, animation(
	std::forward<Callback>(callback),
	st::historyPollRadialAnimation) {
}

Poll::Answer::Answer() : text(st::msgMinWidth / 2) {
}

void Poll::Answer::fillData(
		not_null<PollData*> poll,
		const PollAnswer &original,
		Ui::Text::MarkedContext context) {
	chosen = original.chosen;
	correct = poll->quiz() ? original.correct : chosen;
	if (!text.isEmpty() && text.toTextWithEntities() == original.text) {
		return;
	}
	text.setMarkedText(
		st::historyPollAnswerStyle,
		original.text,
		Ui::WebpageTextTitleOptions(),
		context);
}

void Poll::Answer::fillMedia(
		not_null<PollData*> poll,
		const PollAnswer &original,
		Window::SessionController::MessageContext messageContext,
		Fn<void()> repaint,
		Fn<bool()> paused) {
	const auto updated = MakePollThumbnail(
		poll,
		original,
		messageContext,
		paused);
	const auto same = (updated.kind == thumbnailKind)
		&& (updated.id == thumbnailId)
		&& (updated.rounded == thumbnailRounded);
	if (same) {
		return;
	}
	if (thumbnail) {
		thumbnail->subscribeToUpdates(nullptr);
	}
	thumbnail = updated.thumbnail;
	mediaHandler = updated.handler;
	thumbnailRounded = updated.rounded;
	thumbnailIsVideo = updated.isVideo;
	thumbnailKind = updated.kind;
	thumbnailId = updated.id;
	if (thumbnail) {
		thumbnail->subscribeToUpdates(std::move(repaint));
	}
}

Poll::Footer::Footer(not_null<Poll*> owner)
: Part(owner)
, _showResultsLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		owner,
		[=] { owner->_optionsPart->showResults(); })))
, _sendVotesLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		owner,
		[=] { owner->_optionsPart->sendMultiOptions(); })))
, _adminVotesLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		owner,
		[=] {
			if (owner->_flags & PollData::Flag::PublicVotes) {
				owner->_optionsPart->showResults();
			} else {
				owner->_optionsPart->saveStateInAnimation();
				owner->_adminShowResults = true;
				owner->_optionsPart->updateAnswerVotes();
				owner->_optionsPart->startAnswersAnimation();
			}
		})))
, _adminBackVoteLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		owner,
		[=] {
			owner->_optionsPart->saveStateInAnimation();
			owner->_adminShowResults = false;
			owner->_optionsPart->updateAnswerVotes();
			owner->_optionsPart->startAnswersAnimation();
		})))
, _saveOptionLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		owner,
		[=](ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto delegate = my.elementDelegate
				? my.elementDelegate()
				: nullptr) {
				delegate->elementSubmitAddPollOption(
					owner->_parent->data()->fullId());
			}
		})))
, _closeTimer([=] { owner->repaint(); }) {
}

Poll::Poll(
	not_null<Element*> parent,
	not_null<PollData*> poll,
	const TextWithEntities &consumed)
: Media(parent)
, _poll(poll)
{
	_headerPart = std::make_unique<Header>(this);
	_optionsPart = std::make_unique<Options>(this);
	_addOptionPart = std::make_unique<AddOption>(this);
	_footerPart = std::make_unique<Footer>(this);
	if (!consumed.text.isEmpty()) {
		_headerPart->updateDescription();
	}
	history()->owner().registerPollView(_poll, _parent);
}

QSize Poll::countOptimalSize() {
	updateTexts();

	const auto paddings = st::msgPadding.left() + st::msgPadding.right();

	auto maxWidth = st::msgFileMinWidth;
	accumulate_max(maxWidth, paddings + _headerPart->_description.maxWidth());
	accumulate_max(maxWidth, paddings + _headerPart->_question.maxWidth());
	for (const auto &answer : _optionsPart->_answers) {
		const auto media = answer.thumbnail
			? (PollAnswerMediaSize() + PollAnswerMediaSkip())
			: 0;
		accumulate_max(
			maxWidth,
			paddings
			+ st::historyPollAnswerPadding.left()
			+ answer.text.maxWidth()
			+ media
			+ st::historyPollAnswerPadding.right());
	}

	const auto innerWidth = maxWidth - paddings;
	auto minHeight = _headerPart->countHeight(innerWidth)
		+ _optionsPart->countHeight(innerWidth)
		+ _addOptionPart->countHeight(innerWidth)
		+ _footerPart->countHeight(innerWidth);
	return { maxWidth, minHeight };
}

bool Poll::showVotes() const {
	if (_adminShowResults) {
		return true;
	}
	if (_flags & PollData::Flag::HideResultsUntilClose) {
		return (_flags & PollData::Flag::Closed) || _parent->data()->out();
	}
	return _voted || (_flags & PollData::Flag::Closed);
}

bool Poll::isAuthorNotVoted() const {
	return _parent->data()->out()
		&& !_voted
		&& !(_flags & PollData::Flag::Closed);
}

bool Poll::canVote() const {
	return !showVotes() && !_voted && _parent->data()->isRegular();
}

bool Poll::canSendVotes() const {
	return canVote() && _optionsPart->_hasSelected;
}

bool Poll::showVotersCount() const {
	if (_voted && !showVotes()) {
		return !(_flags & PollData::Flag::PublicVotes);
	}
	return showVotes()
		? (!_totalVotes || !(_flags & PollData::Flag::PublicVotes))
		: !(_flags & PollData::Flag::MultiChoice);
}

bool Poll::inlineFooter() const {
	return !(_flags
		& (PollData::Flag::PublicVotes | PollData::Flag::MultiChoice));
}

bool Poll::canAddOption() const {
	return (_flags & PollData::Flag::OpenAnswers)
		&& !(_flags & PollData::Flag::Closed)
		&& !_parent->data()->Has<HistoryMessageForwarded>()
		&& (int(_poll->answers.size())
			< _poll->session().appConfig().pollOptionsLimit());
}

QRect Poll::addOptionRect(int innerWidth) const {
	const auto answersHeight = ranges::accumulate(ranges::views::all(
		_optionsPart->_answers
	) | ranges::views::transform([&](const Answer &answer) {
		return _optionsPart->countAnswerHeight(answer, innerWidth);
	}), 0);
	const auto top = _headerPart->countQuestionTop(innerWidth)
		+ _headerPart->_question.countHeight(innerWidth)
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip
		+ answersHeight
		+ (st::historyPollChoiceRight.height()
			- st::historyPollFillingHeight) / 2;
	return QRect(
		st::msgPadding.left(),
		top,
		innerWidth,
		_addOptionPart->countHeight(innerWidth));
}

void Poll::setAddOptionActive(bool active) {
	if (_addOptionActive != active) {
		_addOptionActive = active;
		history()->owner().requestViewResize(_parent);
	}
}

int Poll::Options::countAnswerContentWidth(
		const Answer &answer,
		int innerWidth) const {
	const auto answerWidth = innerWidth
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();
	const auto mediaWidth = answer.thumbnail
		? (PollAnswerMediaSize() + PollAnswerMediaSkip())
		: 0;
	return std::max(1, answerWidth - mediaWidth);
}

int Poll::Options::countVotesExtraHeight(
		const Answer &answer,
		int textWidth) const {
	if (answer.votesCountString.isEmpty()
		&& answer.recentVoters.empty()) {
		return 0;
	}
	const auto lineWidths = answer.text.countLineWidths(textWidth);
	if (lineWidths.empty()) {
		return 0;
	}
	const auto voterCount = int(answer.recentVoters.size());
	const auto &ust = st::historyPollAnswerUserpics;
	const auto userpicsWidth = voterCount
		? (ust.size + (voterCount - 1) * (ust.size - ust.shift))
		: 0;
	const auto userpicsExtra = userpicsWidth
		? (st::historyPollAnswerUserpicsSkip + userpicsWidth)
		: 0;
	const auto votesWidth = answer.votesCountWidth
		+ userpicsExtra
		+ st::historyPollFillingRight
		+ st::historyPollPercentSkip;
	const auto lastLineWidth = lineWidths.back();
	if (lastLineWidth + votesWidth <= textWidth) {
		return 0;
	}
	return st::normalFont->height;
}

int Poll::Options::countAnswerHeight(
		const Answer &answer,
		int innerWidth) const {
	const auto media = answer.thumbnail ? PollAnswerMediaSize() : 0;
	const auto textWidth = countAnswerContentWidth(answer, innerWidth);
	const auto &padding = answer.thumbnail
		? st::historyPollAnswerPadding
		: st::historyPollAnswerPaddingNoMedia;
	const auto textHeight = answer.text.countHeight(textWidth);
	const auto multiline = (textHeight
		> st::historyPollPercentFont->height);
	const auto votesExtra = _owner->showVotes()
		? countVotesExtraHeight(answer, textWidth)
		: 0;
	const auto fillingWithChoice = (_owner->showVotes()
		&& (media || multiline || votesExtra))
		? (std::max(textHeight, st::historyPollPercentFont->height)
			+ votesExtra
			+ st::historyPollFillingTop
			+ (st::historyPollFillingHeight
				+ st::historyPollChoiceRight.height()) / 2)
		: 0;
	return padding.top()
		+ std::max({
			textHeight,
			media,
			fillingWithChoice,
		})
		+ padding.bottom();
}

QSize Poll::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	const auto innerWidth = newWidth
		- st::msgPadding.left()
		- st::msgPadding.right();

	auto newHeight = _headerPart->countHeight(innerWidth)
		+ _optionsPart->countHeight(innerWidth)
		+ _addOptionPart->countHeight(innerWidth)
		+ _footerPart->countHeight(innerWidth);
	return { newWidth, newHeight };
}

void Poll::updateTexts() {
	const auto current = _poll->owner().poll(_poll->id);
	if (_poll != current) {
		_poll = current;
		_pollVersion = 0;
	}
	if (_pollVersion == _poll->version) {
		return;
	}
	const auto first = !_pollVersion;
	_pollVersion = _poll->version;

	const auto willStartAnimation = _optionsPart->checkAnimationStart();
	const auto voted = _voted;

	_headerPart->updateDescription();
	if (_headerPart->_question.toTextWithEntities() != _poll->question) {
		auto options = Ui::WebpageTextTitleOptions();
		options.maxw = options.maxh = 0;
		_headerPart->_question.setMarkedText(
			st::historyPollQuestionStyle,
			_poll->question,
			options,
			Core::TextContext({
				.session = &_poll->session(),
				.repaint = [=] {
					if (!_parent->delegate()->elementAnimationsPaused()) {
						repaint();
					}
				},
				.customEmojiLoopLimit = 2,
			}));
	}
	if (_flags != _poll->flags() || _headerPart->_subtitle.isEmpty()) {
		using Flag = PollData::Flag;
		_flags = _poll->flags();
		_headerPart->_subtitle.setText(
			st::msgDateTextStyle,
			((_flags & Flag::Closed)
				? tr::lng_polls_closed(tr::now)
				: (_flags & Flag::Quiz)
				? ((_flags & Flag::PublicVotes)
					? tr::lng_polls_public_quiz(tr::now)
					: tr::lng_polls_anonymous_quiz(tr::now))
				: ((_flags & Flag::PublicVotes)
					? tr::lng_polls_public(tr::now)
					: tr::lng_polls_anonymous(tr::now))));
	}
	if (_footerPart->_adminBackVoteLabel.isEmpty()) {
		_footerPart->_adminBackVoteLabel.setMarkedText(
			st::semiboldTextStyle,
			tr::lng_polls_admin_back_vote(
				tr::now,
				lt_arrow,
				Ui::Text::IconEmoji(&st::textBackIconEmoji),
				tr::marked));
	}
	_headerPart->updateRecentVoters();
	_optionsPart->updateAnswers();
	_headerPart->updateAttachedMedia();
	_headerPart->updateSolutionText();
	_headerPart->updateSolutionMedia();
	updateVotes();

	if (willStartAnimation) {
		_optionsPart->startAnswersAnimation();
		if (!voted) {
			_optionsPart->checkQuizAnswered();
		}
	}
	_headerPart->solutionToggled(
		_headerPart->_solutionShown,
		first ? anim::type::instant : anim::type::normal);
}

void Poll::Header::updateDescription() {
	const auto media = _owner->_parent->data()->media();
	const auto consumed = media
		? media->consumedMessageText()
		: TextWithEntities();
	if (consumed.text.isEmpty()) {
		_description = Ui::Text::String(st::msgMinWidth / 2);
		return;
	}
	if (_description.toTextWithEntities() == consumed) {
		return;
	}
	const auto context = Core::TextContext({
		.session = &_owner->_poll->session(),
		.repaint = [=] {
			if (!_owner->_parent->delegate()->elementAnimationsPaused()) {
				_owner->_parent->customEmojiRepaint();
			}
		},
		.customEmojiLoopLimit = 2,
	});
	_description.setMarkedText(
		st::webPageDescriptionStyle,
		consumed,
		Ui::ItemTextOptions(_owner->_parent->data()),
		context);
	InitElementTextPart(_owner->_parent, _description);
}

void Poll::Header::updateSolutionText() {
	if (_owner->_poll->solution.text.isEmpty()) {
		_solutionText = Ui::Text::String();
		return;
	}
	if (_solutionText.toTextWithEntities() == _owner->_poll->solution) {
		return;
	}
	_solutionText = Ui::Text::String(st::msgMinWidth);
	_solutionText.setMarkedText(
		st::webPageDescriptionStyle,
		_owner->_poll->solution,
		Ui::ItemTextOptions(_owner->_parent->data()),
		Core::TextContext({
			.session = &_owner->_poll->session(),
			.repaint = [=] {
				if (!_owner->_parent->delegate()->elementAnimationsPaused()) {
					_owner->repaint();
				}
			},
		}));
	InitElementTextPart(_owner->_parent, _solutionText);
}

void Poll::Header::updateSolutionMedia() {
	const auto item = _owner->_parent->data();
	const auto messageContext = Window::SessionController::MessageContext{
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	};
	const auto paused = [=] {
		return _owner->_parent->delegate()->elementAnimationsPaused();
	};
	const auto updated = MakePollThumbnail(
		_owner->_poll,
		_owner->_poll->solutionMedia,
		messageContext,
		paused);
	if (!updated.thumbnail) {
		_solutionMedia = nullptr;
		_solutionAttach = nullptr;
		return;
	}
	if (_solutionMedia
		&& _solutionMedia->kind == updated.kind
		&& _solutionMedia->id == updated.id) {
		return;
	}
	if (!_solutionMedia) {
		_solutionMedia = std::make_unique<SolutionMedia>();
	}
	_solutionMedia->kind = updated.kind;
	_solutionMedia->id = updated.id;
	auto photo = (PhotoData*)(nullptr);
	auto document = (DocumentData*)(nullptr);
	if (updated.kind == PollThumbnailKind::Photo && updated.id) {
		photo = _owner->_poll->owner().photo(PhotoId(updated.id));
	} else if (updated.kind == PollThumbnailKind::Document && updated.id) {
		document = _owner->_poll->owner().document(DocumentId(updated.id));
	} else if (updated.kind == PollThumbnailKind::Audio && updated.id) {
		document = _owner->_poll->owner().document(DocumentId(updated.id));
	} else if (updated.kind == PollThumbnailKind::Geo
		&& _owner->_poll->solutionMedia.geo) {
		const auto &point = *_owner->_poll->solutionMedia.geo;
		const auto cloudImage = _owner->_poll->owner().location(point);
		_solutionAttach = std::make_unique<Location>(
			_owner->_parent,
			cloudImage,
			point,
			QString(),
			QString());
		return;
	}
	_solutionAttach = (photo || document)
		? CreateAttach(_owner->_parent, document, photo)
		: nullptr;
}

void Poll::Header::updateAttachedMedia() {
	const auto item = _owner->_parent->data();
	const auto messageContext = Window::SessionController::MessageContext{
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	};
	const auto paused = [=] {
		return _owner->_parent->delegate()->elementAnimationsPaused();
	};
	const auto updated = MakePollThumbnail(
		_owner->_poll,
		_owner->_poll->attachedMedia,
		messageContext,
		paused);
	const auto same = (_attachedMedia->kind == updated.kind)
		&& (_attachedMedia->id == updated.id)
		&& (_attachedMedia->rounded == updated.rounded);
	if (same) {
		return;
	}
	if (_attachedMedia->thumbnail) {
		_attachedMedia->thumbnail->subscribeToUpdates(nullptr);
	}
	_attachedMediaCache = QImage();
	_attachedMedia->thumbnail = updated.thumbnail;
	_attachedMedia->handler = updated.handler;
	_attachedMedia->kind = updated.kind;
	_attachedMedia->rounded = updated.rounded;
	_attachedMedia->id = updated.id;
	_attachedMedia->photo = nullptr;
	_attachedMedia->photoMedia = nullptr;
	_attachedMedia->photoSize = QSize();
	if (updated.kind == PollThumbnailKind::Photo && updated.id) {
		const auto photo = _owner->_poll->owner().photo(PhotoId(updated.id));
		_attachedMedia->photo = photo;
		_attachedMedia->photoMedia = photo->createMediaView();
		_attachedMedia->photoMedia->wanted(
			Data::PhotoSize::Large,
			_owner->_parent->data()->fullId());
		if (const auto size = photo->size(Data::PhotoSize::Large)) {
			_attachedMedia->photoSize = *size;
		} else if (const auto s = photo->size(Data::PhotoSize::Thumbnail)) {
			_attachedMedia->photoSize = *s;
		}
	}
	if ((updated.kind == PollThumbnailKind::Document
		|| updated.kind == PollThumbnailKind::Audio) && updated.id) {
		const auto document = _owner->_poll->owner().document(
			DocumentId(updated.id));
		_attachedMediaAttach = CreateAttach(
			_owner->_parent,
			document,
			nullptr);
	} else {
		_attachedMediaAttach = nullptr;
	}
	if (_attachedMedia->thumbnail) {
		_attachedMedia->thumbnail->subscribeToUpdates(
			crl::guard(_owner, [=] {
				if (!_owner->_parent->delegate()->elementAnimationsPaused()) {
					_attachedMediaCache = QImage();
					_owner->repaint();
				}
			}));
	}
}

int Poll::Header::countTopContentSkip(int pollWidth) const {
	return countTopMediaHeight(pollWidth)
		? st::historyPollMediaTopSkip
		: _owner->isBubbleTop()
		? st::historyPollQuestionTop
		: (st::historyPollQuestionTop - st::msgFileTopMinus);
}

int Poll::Header::countTopMediaHeight(int pollWidth) const {
	if (!_attachedMedia || !_attachedMedia->thumbnail) {
		return 0;
	}
	if (_attachedMediaAttach) {
		return countAttachHeight(pollWidth);
	}
	if (_attachedMedia->kind == PollThumbnailKind::Photo
		&& !_attachedMedia->photoSize.isEmpty()) {
		const auto w = pollWidth > 0 ? pollWidth : _owner->width();
		const auto sideSkip = st::historyPollMediaSideSkip;
		const auto availableWidth = w - 2 * sideSkip;
		const auto &original = _attachedMedia->photoSize;
		return std::max(
			1,
			int(original.height() * availableWidth / original.width()));
	}
	return st::historyPollMediaHeight;
}

int Poll::Header::countAttachHeight(int pollWidth) const {
	if (!_attachedMediaAttach) {
		return 0;
	}
	_attachedMediaAttach->initDimensions();
	const auto w = pollWidth > 0 ? pollWidth : _owner->width();
	const auto sideSkip = st::historyPollMediaSideSkip;
	const auto innerWidth = w - 2 * sideSkip;
	return _attachedMediaAttach->resizeGetHeight(
		std::max(1, innerWidth));
}

QRect Poll::Header::countTopMediaRect(int top) const {
	const auto sideSkip = st::historyPollMediaSideSkip;
	const auto mediaHeight = countTopMediaHeight();
	return mediaHeight
		? QRect(
			sideSkip,
			top,
			std::max(1, _owner->width() - 2 * sideSkip),
			mediaHeight)
		: QRect();
}

Ui::BubbleRounding Poll::Header::topMediaRounding() const {
	using Corner = Ui::BubbleCornerRounding;
	auto result = _owner->adjustedBubbleRounding(
		RectPart::BottomLeft | RectPart::BottomRight);
	const auto normalize = [](Corner value) {
		return (value == Corner::Large)
			? Corner::Large
			: (value == Corner::None)
			? Corner::None
			: Corner::Small;
	};
	result.topLeft = normalize(result.topLeft);
	result.topRight = normalize(result.topRight);
	result.bottomLeft = Corner::Small;
	result.bottomRight = Corner::Small;
	return result;
}

void Poll::Header::validateTopMediaCache(QSize size) const {
	if (!_attachedMedia || !_attachedMedia->thumbnail || size.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto rounding = topMediaRounding();
	if ((_attachedMediaCache.size() == (size * ratio))
		&& (_attachedMediaCacheRounding == rounding)) {
		return;
	}
	auto source = QImage();
	if (_attachedMedia->photoMedia) {
		if (const auto image
			= _attachedMedia->photoMedia->image(Data::PhotoSize::Large)) {
			source = image->original();
		} else if (const auto image
			= _attachedMedia->photoMedia->image(
				Data::PhotoSize::Thumbnail)) {
			source = image->original();
		} else if (const auto image
			= _attachedMedia->photoMedia->thumbnailInline()) {
			source = image->original();
		}
	}
	if (source.isNull()) {
		source = _attachedMedia->thumbnail->image(
			std::max(size.width(), size.height()) * ratio);
	}
	if (source.isNull()) {
		return;
	}
	const auto sw = source.width();
	const auto sh = source.height();
	const auto tw = size.width() * ratio;
	const auto th = size.height() * ratio;
	if (sw * th != sh * tw) {
		const auto cropW = std::min(sw, sh * tw / th);
		const auto cropH = std::min(sh, sw * th / tw);
		source = source.copy(
			(sw - cropW) / 2,
			(sh - cropH) / 2,
			cropW,
			cropH);
	}
	auto prepared = Images::Prepare(
		source,
		size * ratio,
		{ .outer = size });
	prepared = Images::Round(
		std::move(prepared),
		MediaRoundingMask(rounding));
	prepared.setDevicePixelRatio(ratio);
	_attachedMediaCache = std::move(prepared);
	_attachedMediaCacheRounding = rounding;
}

int Poll::Header::countDescriptionHeight(int innerWidth) const {
	return _description.isEmpty() ? 0 : _description.countHeight(innerWidth);
}

int Poll::Header::countSolutionMediaHeight(int mediaWidth) const {
	if (!_solutionAttach) {
		return 0;
	}
	_solutionAttach->initDimensions();
	return _solutionAttach->resizeGetHeight(mediaWidth);
}

int Poll::Header::countSolutionBlockHeight(int innerWidth) const {
	if (!_solutionShown || !canShowSolution()) {
		return 0;
	}
	const auto &qst = st::historyPagePreview;
	const auto textWidth = innerWidth
		- qst.padding.left()
		- qst.padding.right();
	auto height = qst.padding.top();
	height += st::semiboldFont->height;
	height += st::historyPollExplanationTitleSkip;
	height += _solutionText.countHeight(textWidth);
	if (const auto mediaHeight = countSolutionMediaHeight(textWidth)) {
		height += st::historyPollExplanationMediaSkip + mediaHeight;
	}
	height += qst.padding.bottom();
	return height;
}

int Poll::Header::countQuestionTop(int innerWidth, int pollWidth) const {
	auto result = countTopContentSkip(pollWidth);
	if (const auto mediaHeight = countTopMediaHeight(pollWidth)) {
		result += mediaHeight + st::historyPollMediaSkip;
	}
	if (const auto descriptionHeight = countDescriptionHeight(innerWidth)) {
		result += descriptionHeight + st::historyPollDescriptionSkip;
	}
	if (const auto solutionHeight = countSolutionBlockHeight(innerWidth)) {
		result += solutionHeight + st::historyPollExplanationSkip;
	}
	return result;
}

uint16 Poll::Header::solutionSelectionLength() const {
	return (_solutionShown && canShowSolution())
		? _solutionText.length()
		: uint16(0);
}

TextSelection Poll::Header::toSolutionSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(selection, _description);
}

TextSelection Poll::Header::fromSolutionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(selection, _description);
}

TextSelection Poll::Header::toQuestionSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(
		selection,
		uint16(_description.length() + solutionSelectionLength()));
}

TextSelection Poll::Header::fromQuestionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(
		selection,
		uint16(_description.length() + solutionSelectionLength()));
}

void Poll::Options::checkQuizAnswered() {
	if (!_owner->_voted
		|| !_votedFromHere
		|| !_owner->_poll->quiz()
		|| anim::Disabled()) {
		return;
	}
	const auto i = ranges::find(_answers, true, &Answer::chosen);
	if (i == end(_answers)) {
		return;
	}
	if (i->correct) {
		_owner->_fireworksAnimation = std::make_unique<Ui::FireworksAnimation>(
			[=] { _owner->repaint(); });
	} else {
		_owner->_wrongAnswerAnimation.start(
			[=] { _owner->repaint(); },
			0.,
			1.,
			kRollDuration,
			anim::linear);
		_owner->_headerPart->showSolution();
	}
}

void Poll::Header::showSolution() const {
	if (!_owner->_poll->solution.text.isEmpty()) {
		solutionToggled(true);
	}
}

void Poll::Header::solutionToggled(
		bool solutionShown,
		anim::type animated) const {
	_solutionShown = solutionShown;
	const auto visible = canShowSolution() && !_solutionShown;
	if (_solutionButtonVisible == visible) {
		if (animated == anim::type::instant
			&& _solutionButtonAnimation.animating()) {
			_solutionButtonAnimation.stop();
			_owner->repaint();
		}
		return;
	}
	_solutionButtonVisible = visible;
	if (animated == anim::type::instant) {
		_solutionButtonAnimation.stop();
	} else {
		_solutionButtonAnimation.start(
			[=] { _owner->repaint(); },
			visible ? 0. : 1.,
			visible ? 1. : 0.,
			st::fadeWrapDuration);
	}
	_owner->history()->owner().requestViewResize(_owner->_parent);
}

void Poll::Header::updateRecentVoters() {
	auto &&sliced = ranges::views::all(
		_owner->_poll->recentVoters
	) | ranges::views::take(kShowRecentVotersCount);
	const auto changed = !ranges::equal(
		_recentVoters,
		sliced,
		ranges::equal_to(),
		&RecentVoter::peer);
	if (changed) {
		auto updated = ranges::views::all(
			sliced
		) | ranges::views::transform([](not_null<PeerData*> peer) {
			return RecentVoter{ peer };
		}) | ranges::to_vector;
		const auto has = _owner->hasHeavyPart();
		if (has) {
			for (auto &voter : updated) {
				const auto i = ranges::find(
					_recentVoters,
					voter.peer,
					&RecentVoter::peer);
				if (i != end(_recentVoters)) {
					voter.userpic = std::move(i->userpic);
				}
			}
		}
		_recentVoters = std::move(updated);
		if (has && !_owner->hasHeavyPart()) {
			_owner->_parent->checkHeavyPart();
		}
	}
}

void Poll::Options::updateAnswers() {
	const auto context = Core::TextContext({
		.session = &_owner->_poll->session(),
		.repaint = [=] {
			if (!_owner->_parent->delegate()->elementAnimationsPaused()) {
				_owner->repaint();
			}
		},
		.customEmojiLoopLimit = 2,
	});
	const auto repaintThumbnail = crl::guard(_owner, [=] {
		if (!_owner->_parent->delegate()->elementAnimationsPaused()) {
			_owner->repaint();
		}
	});
	const auto paused = [=] {
		return _owner->_parent->delegate()->elementAnimationsPaused();
	};
	const auto item = _owner->_parent->data();
	const auto messageContext = Window::SessionController::MessageContext{
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	};
	auto options = ranges::views::all(
		_owner->_poll->answers
	) | ranges::views::transform(&PollAnswer::option) | ranges::to_vector;
	if ((_owner->_flags & PollData::Flag::ShuffleAnswers)
		&& !(_owner->_flags & PollData::Flag::Creator)) {
		const auto userId = _owner->_poll->session().userId();
		const auto pollId = _owner->_poll->id;
		ranges::sort(options, [&](const QByteArray &a, const QByteArray &b) {
			const auto hashA = HashPollShuffleValue(userId, pollId, a);
			const auto hashB = HashPollShuffleValue(userId, pollId, b);
			return (hashA == hashB) ? (a < b) : (hashA < hashB);
		});
	}
	const auto changed = (_answers.size() != options.size())
		|| !ranges::equal(
			_answers,
			options,
			ranges::equal_to(),
			&Answer::option);
	if (!changed) {
		for (auto &answer : _answers) {
			const auto i = ranges::find(
				_owner->_poll->answers,
				answer.option,
				&PollAnswer::option);
			Assert(i != end(_owner->_poll->answers));
			answer.fillData(_owner->_poll, *i, context);
			answer.fillMedia(
				_owner->_poll,
				*i,
				messageContext,
				repaintThumbnail,
				paused);
		}
		_anyAnswerHasMedia = ranges::any_of(_answers, [](const Answer &a) {
			return a.thumbnail != nullptr;
		});
		return;
	}
	_answers = ranges::views::all(options) | ranges::views::transform([&](
			const QByteArray &option) {
		auto result = Answer();
		result.option = option;
		const auto i = ranges::find(
			_owner->_poll->answers,
			option,
			&PollAnswer::option);
		Assert(i != end(_owner->_poll->answers));
		result.fillData(_owner->_poll, *i, context);
		result.fillMedia(
			_owner->_poll,
			*i,
			messageContext,
			repaintThumbnail,
			paused);
		return result;
	}) | ranges::to_vector;

	if ((_owner->_flags & PollData::Flag::ShuffleAnswers)
		&& !(_owner->_flags & PollData::Flag::Creator)) {
		const auto visitorId = _owner->_poll->session().userId();
		const auto pollId = _owner->_poll->id;
		ranges::sort(_answers, [&](const Answer &a, const Answer &b) {
			return HashPollShuffleValue(visitorId, pollId, a.option)
				< HashPollShuffleValue(visitorId, pollId, b.option);
		});
	}

	for (auto &answer : _answers) {
		answer.handler = createAnswerClickHandler(answer);
	}
	_anyAnswerHasMedia = ranges::any_of(_answers, [](const Answer &a) {
		return a.thumbnail != nullptr;
	});

	resetAnswersAnimation();
}

ClickHandlerPtr Poll::Options::createAnswerClickHandler(
		const Answer &answer) {
	const auto option = answer.option;
	auto result = ClickHandlerPtr();
	if (_owner->_flags & PollData::Flag::MultiChoice) {
		result = std::make_shared<LambdaClickHandler>(crl::guard(_owner, [=] {
			if (Logs::DebugEnabled() && base::IsCtrlPressed()) {
				TextUtilities::SetClipboardText(
					TextForMimeData::Simple(_owner->_poll->debugString()));
				return;
			}
			if (_owner->canVote()) {
				_owner->_optionsPart->toggleMultiOption(option);
			} else if (_owner->showVotes()) {
				_owner->_optionsPart->showAnswerVotesTooltip(option);
			}
		}));
	} else {
		result = std::make_shared<LambdaClickHandler>(crl::guard(_owner, [=] {
			if (Logs::DebugEnabled() && base::IsCtrlPressed()) {
				TextUtilities::SetClipboardText(
					TextForMimeData::Simple(_owner->_poll->debugString()));
				return;
			}
			if (_owner->canVote()) {
				_owner->_optionsPart->_votedFromHere = true;
				_owner->history()->session().api().polls().sendVotes(
					_owner->_parent->data()->fullId(),
					{ option });
			} else if (_owner->showVotes()) {
				_owner->_optionsPart->showAnswerVotesTooltip(option);
			}
		}));
	}
	result->setProperty(kPollOptionProperty, option);
	return result;
}

void Poll::Options::toggleMultiOption(const QByteArray &option) {
	const auto i = ranges::find(
		_answers,
		option,
		&Answer::option);
	if (i != end(_answers)) {
		const auto selected = i->selected;
		i->selected = !selected;
		i->selectedAnimation.start(
			[=] { _owner->repaint(); },
			selected ? 1. : 0.,
			selected ? 0. : 1.,
			st::defaultCheck.duration);
		if (selected) {
			const auto j = ranges::find(
				_answers,
				true,
				&Answer::selected);
			_hasSelected = (j != end(_answers));
		} else {
			_hasSelected = true;
		}
		_owner->repaint();
	}
}

void Poll::Options::sendMultiOptions() {
	auto chosen = _answers | ranges::views::filter(
		&Answer::selected
	) | ranges::views::transform(
		&Answer::option
	) | ranges::to_vector;
	if (!chosen.empty()) {
		_votedFromHere = true;
		_owner->history()->session().api().polls().sendVotes(
			_owner->_parent->data()->fullId(),
			std::move(chosen));
	}
}

void Poll::Options::showAnswerVotesTooltip(const QByteArray &option) {
	const auto answer = _owner->_poll->answerByOption(option);
	if (!answer) {
		return;
	}
	const auto quiz = _owner->_poll->quiz();
	const auto text = answer->votes
		? (quiz
			? tr::lng_polls_answers_count
			: tr::lng_polls_votes_count)(
				tr::now,
				lt_count_decimal,
				answer->votes)
		: (quiz
			? tr::lng_polls_answers_none
			: tr::lng_polls_votes_none)(tr::now);
	_owner->_parent->delegate()->elementShowTooltip({ text }, [] {});
}

void Poll::Options::showResults() {
	_owner->_parent->delegate()->elementShowPollResults(
		_owner->_poll,
		_owner->_parent->data()->fullId());
}

void Poll::updateVotes() {
	const auto voted = _poll->voted();
	if (_voted != voted) {
		_voted = voted;
		if (_voted) {
			for (auto &answer : _optionsPart->_answers) {
				answer.selected = false;
			}
			if (_optionsPart->_votedFromHere
				&& (_flags & PollData::Flag::HideResultsUntilClose)
				&& !(_flags & PollData::Flag::Closed)) {
				Ui::Toast::Show({
					.text = tr::lng_polls_results_after_close(
						tr::now,
						tr::marked),
					.iconLottie = u"toast_hide_results"_q,
					.iconLottieSize = st::pollToastIconSize,
					.duration = crl::time(3000),
				});
			}
		} else {
			_optionsPart->_votedFromHere = false;
			_optionsPart->_hasSelected = false;
		}
	}
	_optionsPart->updateAnswerVotes();
	_footerPart->updateTotalVotes();
}

void Poll::Options::checkSendingAnimation() const {
	const auto &sending = _owner->_poll->sendingVotes;
	const auto sendingRadial = (sending.size() == 1)
		&& !(_owner->_flags & PollData::Flag::MultiChoice);
	if (sendingRadial == (_sendingAnimation != nullptr)) {
		if (_sendingAnimation) {
			_sendingAnimation->option = sending.front();
		}
		return;
	}
	if (!sendingRadial) {
		if (!_answersAnimation) {
			_sendingAnimation = nullptr;
		}
		return;
	}
	_sendingAnimation = std::make_unique<SendingAnimation>(
		sending.front(),
		[=] { radialAnimationCallback(); });
	_sendingAnimation->animation.start();
}

void Poll::Options::updateAnswerVotesFromOriginal(
		Answer &answer,
		const PollAnswer &original,
		int percent,
		int maxVotes,
		bool showPercent) {
	if (!_owner->showVotes() || !showPercent) {
		answer.votesPercent = 0;
		answer.votesPercentString.clear();
		answer.votesPercentWidth = 0;
	} else if (answer.votesPercentString.isEmpty()
		|| answer.votesPercent != percent) {
		answer.votesPercent = percent;
		answer.votesPercentString = QString::number(percent) + '%';
		answer.votesPercentWidth = st::historyPollPercentFont->width(
			answer.votesPercentString);
	}
	answer.chosen = original.chosen;
	answer.votes = original.votes;
	answer.filling = answer.votes / float64(maxVotes);
	if (_owner->showVotes() && answer.votes) {
		answer.votesCountString = Lang::FormatCountDecimal(answer.votes);
		answer.votesCountWidth = st::normalFont->width(
			answer.votesCountString);
	} else {
		answer.votesCountString.clear();
		answer.votesCountWidth = 0;
	}
	const auto changed = !ranges::equal(
		answer.recentVoters,
		original.recentVoters,
		ranges::equal_to(),
		&UserpicInRow::peer);
	if (changed) {
		auto updated = std::vector<UserpicInRow>();
		updated.reserve(original.recentVoters.size());
		for (const auto &peer : original.recentVoters) {
			const auto i = ranges::find(
				answer.recentVoters,
				peer,
				&UserpicInRow::peer);
			if (i != end(answer.recentVoters)) {
				updated.push_back(std::move(*i));
			} else {
				updated.push_back({ .peer = peer });
			}
		}
		answer.recentVoters = std::move(updated);
		answer.recentVotersImage = QImage();
	}
}

void Poll::Options::updateAnswerVotes() {
	if (_owner->_poll->answers.size() != _answers.size()
		|| _owner->_poll->answers.empty()) {
		return;
	}
	const auto totalVotes = _owner->_poll->totalVoters;
	const auto showPercent = (totalVotes > 0)
		&& ranges::all_of(_owner->_poll->answers, [=](const PollAnswer &a) {
			return a.votes <= totalVotes;
		});
	const auto maxVotes = std::max(1, ranges::max_element(
		_owner->_poll->answers,
		ranges::less(),
		&PollAnswer::votes)->votes);

	constexpr auto kMaxCount = PollData::kMaxOptions;
	const auto count = size_type(_owner->_poll->answers.size());
	Assert(count <= kMaxCount);
	int PercentsStorage[kMaxCount] = { 0 };
	int VotesStorage[kMaxCount] = { 0 };

	ranges::copy(
		ranges::views::all(
			_owner->_poll->answers
		) | ranges::views::transform(&PollAnswer::votes),
		ranges::begin(VotesStorage));

	if (showPercent) {
		CountNicePercent(
			gsl::make_span(VotesStorage).subspan(0, count),
			totalVotes,
			gsl::make_span(PercentsStorage).subspan(0, count));
	}

	for (auto &answer : _answers) {
		const auto i = ranges::find(
			_owner->_poll->answers,
			answer.option,
			&PollAnswer::option);
		Assert(i != end(_owner->_poll->answers));
		const auto index = int(i - begin(_owner->_poll->answers));
		updateAnswerVotesFromOriginal(
			answer,
			*i,
			PercentsStorage[index],
			maxVotes,
			showPercent);
	}
}

void Poll::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	if (_poll->checkResultsReload(context.now)) {
		history()->session().api().polls().reloadResults(_parent->data());
	}

	const auto padding = st::msgPadding;
	paintw -= padding.left() + padding.right();

	const Part *parts[] = {
		_headerPart.get(),
		_optionsPart.get(),
		_addOptionPart.get(),
		_footerPart.get(),
	};
	auto tshift = 0;
	for (const auto part : parts) {
		const auto h = part->countHeight(paintw);
		if (h > 0) {
			const auto saved = p.transform();
			p.translate(0, tshift);
			part->draw(p, padding.left(), paintw, width(), context);
			p.setTransform(saved);
		}
		tshift += h;
	}
}

void Poll::Options::resetAnswersAnimation() const {
	_answersAnimation = nullptr;
	if (_owner->_poll->sendingVotes.size() != 1
		|| (_owner->_flags & PollData::Flag::MultiChoice)) {
		_sendingAnimation = nullptr;
	}
}

void Poll::Options::radialAnimationCallback() const {
	if (!anim::Disabled()) {
		_owner->repaint();
	}
}

void Poll::Header::paintRecentVoters(
		Painter &p,
		int left,
		int top,
		const PaintContext &context) const {
	const auto count = int(_recentVoters.size());
	if (!count) {
		return;
	}
	auto x = left
		+ st::historyPollRecentVotersSkip
		+ (count - 1) * st::historyPollRecentVoterSkip;
	auto y = top;
	const auto size = st::historyPollRecentVoterSize;
	const auto stm = context.messageStyle();
	auto pen = stm->msgBg->p;
	pen.setWidth(st::lineWidth);

	auto created = false;
	for (const auto &recent : ranges::views::reverse(_recentVoters)) {
		const auto was = !recent.userpic.null();
		recent.peer->paintUserpic(p, recent.userpic, x, y, size);
		if (!was && !recent.userpic.null()) {
			created = true;
		}
		const auto paintContent = [&](QPainter &p) {
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(x, y, size, size);
		};
		if (_owner->usesBubblePattern(context)) {
			const auto add = st::lineWidth * 2;
			const auto target = QRect(x, y, size, size).marginsAdded(
				{ add, add, add, add });
			Ui::PaintPatternBubblePart(
				p,
				context.viewport,
				context.bubblesPattern->pixmap,
				target,
				paintContent,
				_userpicCircleCache);
		} else {
			paintContent(p);
		}
		x -= st::historyPollRecentVoterSkip;
	}
	if (created) {
		_owner->history()->owner().registerHeavyViewPart(_owner->_parent);
	}
}

void Poll::Header::paintShowSolution(
		Painter &p,
		int right,
		int top,
		const PaintContext &context) const {
	const auto shown = _solutionButtonAnimation.value(
		_solutionButtonVisible ? 1. : 0.);
	if (!shown) {
		return;
	}
	if (!_showSolutionLink) {
		_showSolutionLink = std::make_shared<LambdaClickHandler>(
			crl::guard(_owner, [=] { _owner->_headerPart->showSolution(); }));
	}
	const auto stm = context.messageStyle();
	const auto &icon = stm->historyQuizExplain;
	const auto x = right - icon.width();
	const auto y = top + (st::normalFont->height - icon.height()) / 2;
	if (shown == 1.) {
		icon.paint(p, x, y, _owner->width());
	} else {
		p.save();
		p.translate(x + icon.width() / 2, y + icon.height() / 2);
		p.scale(shown, shown);
		p.setOpacity(shown);
		icon.paint(p, -icon.width() / 2, -icon.height() / 2, _owner->width());
		p.restore();
	}
}

void Poll::Header::paintSolutionBlock(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const {
	if (!_solutionShown || !canShowSolution()) {
		return;
	}
	if (!_closeSolutionLink) {
		_closeSolutionLink = std::make_shared<LambdaClickHandler>(
			crl::guard(
				_owner,
				[=] { _owner->_headerPart->solutionToggled(false); }));
	}

	const auto &qst = st::historyPagePreview;
	const auto blockHeight = countSolutionBlockHeight(paintw);
	const auto outer = QRect(left, top, paintw, blockHeight);

	const auto stm = context.messageStyle();
	const auto view = _owner->_parent;
	const auto selected = context.selected();
	const auto colorIndex = view->contentColorIndex();
	const auto &chatSt = *context.st;
	const auto colorPattern = chatSt.colorPatternIndex(colorIndex);
	const auto useColorIndex = !context.outbg;
	const auto cache = useColorIndex
		? chatSt.coloredReplyCache(selected, colorIndex).get()
		: stm->replyCache[colorPattern].get();

	Ui::Text::ValidateQuotePaintCache(*cache, qst);
	Ui::Text::FillQuotePaint(p, outer, *cache, qst);

	const auto innerLeft = left + qst.padding.left();
	const auto innerRight = left + paintw - qst.padding.right();
	const auto textWidth = innerRight - innerLeft;
	auto yshift = top + qst.padding.top();

	p.setPen(cache->outlines[0]);
	p.setFont(st::semiboldFont);
	const auto closeArea = st::historyPollExplanationCloseSize;
	p.drawTextLeft(
		innerLeft,
		yshift,
		_owner->width(),
		tr::lng_polls_solution_title(tr::now),
		textWidth - closeArea);

	{
		const auto iconSize = st::historyPollExplanationCloseIconSize;
		const auto centerX = innerRight - closeArea / 2;
		const auto centerY = yshift + st::semiboldFont->height / 2;
		const auto half = iconSize / 2;
		auto pen = QPen(cache->outlines[0]);
		pen.setWidthF(st::historyPollExplanationCloseStroke * 1.);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setRenderHint(QPainter::Antialiasing);
		p.drawLine(
			centerX - half, centerY - half,
			centerX + half, centerY + half);
		p.drawLine(
			centerX + half, centerY - half,
			centerX - half, centerY + half);
		p.setRenderHint(QPainter::Antialiasing, false);
	}

	yshift += st::semiboldFont->height + st::historyPollExplanationTitleSkip;

	p.setPen(stm->historyTextFg);
	_owner->_parent->prepareCustomEmojiPaint(p, context, _solutionText);
	_solutionText.draw(p, {
		.position = { innerLeft, yshift },
		.outerWidth = _owner->width(),
		.availableWidth = textWidth,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused,
		.pausedSpoiler = context.paused,
		.selection = toSolutionSelection(context.selection),
	});

	if (countSolutionMediaHeight(textWidth)) {
		yshift += _solutionText.countHeight(textWidth)
			+ st::historyPollExplanationMediaSkip;
		const auto isDocument = _solutionMedia
			&& (_solutionMedia->kind == PollThumbnailKind::Document
				|| _solutionMedia->kind == PollThumbnailKind::Audio);
		const auto isThumbed = isDocument
			&& _owner->_poll->solutionMedia.document
			&& _owner->_poll->solutionMedia.document->hasThumbnail()
			&& !_owner->_poll->solutionMedia.document->isSong();
		const auto &fileSt = isThumbed
			? st::msgFileThumbLayout
			: st::msgFileLayout;
		const auto shift = isDocument ? fileSt.padding.left() : 0;
		const auto attachLeft = rtl()
			? (_owner->width() - innerLeft + shift - _solutionAttach->width())
			: (innerLeft - shift);
		p.translate(attachLeft, yshift);
		_solutionAttach->draw(
			p,
			context.translated(-attachLeft, -yshift)
				.withSelection(TextSelection()));
		p.translate(-attachLeft, -yshift);
	}
}

int Poll::Options::paintAnswer(
		Painter &p,
		const Answer &answer,
		const AnswerAnimation *animation,
		int left,
		int top,
		int width,
		int outerWidth,
		const PaintContext &context) const {
	const auto height = countAnswerHeight(answer, width);
	if (!context.highlight.pollOption.isEmpty()
		&& context.highlight.pollOption == answer.option
		&& context.highlight.collapsion > 0.) {
		const auto hlTextWidth = countAnswerContentWidth(answer, width);
		const auto hlTextHeight = answer.text.countHeight(hlTextWidth);
		const auto hlMultiline = (hlTextHeight
			> st::historyPollPercentFont->height);
		const auto hlVotesExtra = countVotesExtraHeight(
			answer,
			hlTextWidth);
		const auto fillingExtra = (_owner->showVotes()
			&& !answer.thumbnail
			&& !hlMultiline
			&& !hlVotesExtra)
			? (st::historyPollChoiceRight.height() / 2)
			: 0;
		const auto absoluteTop = top
			+ _owner->_headerPart->countHeight(width);
		const auto to = context.highlightInterpolateTo;
		const auto toProgress = (1. - context.highlight.collapsion);
		if (toProgress >= 1.) {
			context.highlightPathCache->addRect(to);
		} else if (toProgress <= 0.) {
			context.highlightPathCache->addRect(
				0,
				absoluteTop + fillingExtra,
				_owner->width(),
				height + fillingExtra);
		} else {
			const auto lerp = [=](int from, int to) {
				return from + (to - from) * toProgress;
			};
			context.highlightPathCache->addRect(
				lerp(0, to.x()),
				lerp(absoluteTop, to.y()) + fillingExtra,
				lerp(_owner->width(), to.width()),
				lerp(height + fillingExtra, to.height()));
		}
	}
	const auto stm = context.messageStyle();
	const auto &answerPadding = answer.thumbnail
		? st::historyPollAnswerPadding
		: st::historyPollAnswerPaddingNoMedia;
	const auto aleft = left + st::historyPollAnswerPadding.left();
	const auto awidth = width
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();
	const auto media = answer.thumbnail ? PollAnswerMediaSize() : 0;
	const auto textWidth = countAnswerContentWidth(answer, width);
	const auto textContentHeight = answer.text.countHeight(textWidth);
	const auto multilineAnswer = (textContentHeight
		> st::historyPollPercentFont->height);
	const auto votesExtraHeight = countVotesExtraHeight(
		answer,
		textWidth);
	const auto fillingContentHeight = (multilineAnswer || votesExtraHeight)
		? (textContentHeight + votesExtraHeight)
		: textContentHeight;
	const auto anyMediaWidth = _anyAnswerHasMedia
		? (PollAnswerMediaSize() + PollAnswerMediaSkip())
		: 0;
	const auto barContentWidth = std::max(1, awidth - anyMediaWidth);

	if (answer.ripple) {
		p.setOpacity(st::historyPollRippleOpacity);
		answer.ripple->paint(
			p,
			left - st::msgPadding.left(),
			top,
			outerWidth,
			&stm->msgWaveformInactive->c);
		if (answer.ripple->empty()) {
			answer.ripple.reset();
		}
		p.setOpacity(1.);
	}

	const auto paintVotesCount = [&](float64 opacity = 1.) {
		if (answer.votesCountString.isEmpty()) {
			return;
		}
		if (!answer.recentVoters.empty()) {
			if (NeedRegenerateUserpics(
					answer.recentVotersImage,
					answer.recentVoters)) {
				const auto was = !answer.recentVotersImage.isNull();
				GenerateUserpicsInRow(
					answer.recentVotersImage,
					answer.recentVoters,
					st::historyPollAnswerUserpics);
				if (!was) {
					_owner->history()->owner().registerHeavyViewPart(
						_owner->_parent);
				}
			}
		}
		const auto userpicsWidth = answer.recentVotersImage.isNull()
			? 0
			: (answer.recentVotersImage.width()
				/ style::DevicePixelRatio());
		const auto userpicsExtra = userpicsWidth
			? (st::historyPollAnswerUserpicsSkip + userpicsWidth)
			: 0;
		const auto rightEdge = aleft + awidth
			- anyMediaWidth
			- st::historyPollFillingRight;
		const auto countX = rightEdge
			- answer.votesCountWidth
			- userpicsExtra;
		const auto atop = top + answerPadding.top()
			+ ((multilineAnswer || votesExtraHeight)
				? (textContentHeight
					- (votesExtraHeight ? 0 : st::normalFont->height))
				: 0);
		p.setOpacity(opacity);
		p.setFont(st::normalFont);
		p.setPen(stm->msgDateFg);
		p.drawTextLeft(
			countX,
			atop,
			outerWidth,
			answer.votesCountString,
			answer.votesCountWidth);
		if (userpicsWidth) {
			const auto userpicsX = rightEdge - userpicsWidth;
			const auto userpicsY = atop
				+ (st::normalFont->height
					- st::historyPollAnswerUserpics.size) / 2;
			p.drawImage(userpicsX, userpicsY, answer.recentVotersImage);
		}
	};

	if (animation) {
		const auto opacity = animation->opacity.current();
		if (opacity < 1.) {
			p.setOpacity(1. - opacity);
			paintRadio(p, answer, left, top, context);
		}
		if (opacity > 0.) {
			const auto percent = QString::number(
				int(base::SafeRound(animation->percent.current()))) + '%';
			const auto percentWidth = st::historyPollPercentFont->width(
				percent);
			p.setOpacity(opacity);
			paintPercent(
				p,
				percent,
				percentWidth,
				left,
				top,
				answerPadding.top(),
				outerWidth,
				context);
			paintVotesCount(opacity);
			p.setOpacity(sqrt(opacity));
			paintFilling(
				p,
				animation->chosen,
				animation->correct,
				animation->filling.current(),
				left,
				top,
				answerPadding.top(),
				width,
				barContentWidth,
				fillingContentHeight,
				context);
			p.setOpacity(1.);
		}
	} else if (!_owner->showVotes()) {
		paintRadio(p, answer, left, top, context);
	} else {
		paintPercent(
			p,
			answer.votesPercentString,
			answer.votesPercentWidth,
			left,
			top,
			answerPadding.top(),
			outerWidth,
			context);
		paintVotesCount();
		paintFilling(
			p,
			answer.chosen,
			answer.correct,
			answer.filling,
			left,
			top,
			answerPadding.top(),
			width,
			barContentWidth,
			fillingContentHeight,
			context);
	}

	top += answerPadding.top();
	if (answer.thumbnail) {
		const auto target = QRect(
			aleft + awidth - media,
			top,
			media,
			media);
		if (!target.isEmpty()) {
			const auto image = answer.thumbnail->image(media);
			if (!image.isNull()) {
				const auto source = QRectF(QPointF(), QSizeF(image.size()));
				const auto kx = target.width() / source.width();
				const auto ky = target.height() / source.height();
				const auto scale = std::max(kx, ky);
				const auto size = QSizeF(
					source.width() * scale,
					source.height() * scale);
				const auto geometry = QRectF(
					target.x() + (target.width() - size.width()) / 2.,
					target.y() + (target.height() - size.height()) / 2.,
					size.width(),
					size.height());
				p.save();
				if (answer.thumbnailRounded) {
					auto path = QPainterPath();
					path.addRoundedRect(
						target,
						st::roundRadiusSmall,
						st::roundRadiusSmall);
					p.setClipPath(path);
				}
				p.drawImage(geometry, image, source);
				p.restore();
				if (answer.thumbnailIsVideo) {
					st::dialogsMiniPlay.paintInCenter(p, target);
				}
			}
		}
	}
	p.setPen(stm->historyTextFg);
	answer.text.draw(p, {
		.position = { aleft, top },
		.outerWidth = outerWidth,
		.availableWidth = textWidth,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused,
		.pausedSpoiler = context.paused,
	});

	return height;
}

void Poll::Options::paintRadio(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		const PaintContext &context) const {
	const auto &answerPadding = answer.thumbnail
		? st::historyPollAnswerPadding
		: st::historyPollAnswerPaddingNoMedia;
	top += answerPadding.top();

	const auto stm = context.messageStyle();

	PainterHighQualityEnabler hq(p);
	const auto &radio = st::historyPollRadio;
	const auto over = ClickHandler::showAsActive(answer.handler);
	const auto &regular = stm->msgDateFg;

	const auto chosen = answer.chosen;
	const auto checkmark = chosen
		? 1.
		: answer.selectedAnimation.value(answer.selected ? 1. : 0.);

	const auto o = p.opacity();
	if (checkmark < 1.) {
		p.setBrush(Qt::NoBrush);
		p.setOpacity(o
			* (over
				? st::historyPollRadioOpacityOver
				: st::historyPollRadioOpacity));
	}

	const auto multiChoice = (_owner->_flags & PollData::Flag::MultiChoice);
	const auto rect = QRectF(left, top, radio.diameter, radio.diameter)
		- Margins(radio.thickness / 2.);
	const auto radius = st::historyPollCheckboxRadius;
	if (_sendingAnimation && _sendingAnimation->option == answer.option) {
		const auto &active = stm->msgServiceFg;
		if (anim::Disabled()) {
			anim::DrawStaticLoading(p, rect, radio.thickness, active);
		} else {
			const auto state = _sendingAnimation->animation.computeState();
			auto pen = anim::pen(regular, active, state.shown);
			pen.setWidth(radio.thickness);
			pen.setCapStyle(Qt::RoundCap);
			p.setPen(pen);
			if (multiChoice) {
				p.drawRoundedRect(rect, radius, radius);
			} else {
				p.drawArc(
					rect,
					state.arcFrom,
					state.arcLength);
			}
		}
	} else {
		if (checkmark < 1.) {
			auto pen = regular->p;
			pen.setWidth(radio.thickness);
			p.setPen(pen);
			if (multiChoice) {
				p.drawRoundedRect(rect, radius, radius);
			} else {
				p.drawEllipse(rect);
			}
		}
		if (checkmark > 0.) {
			const auto removeFull = (radio.diameter / 2 - radio.thickness);
			const auto removeNow = removeFull * (1. - checkmark);
			const auto color = stm->msgFileThumbLinkFg;
			auto pen = color->p;
			pen.setWidth(radio.thickness);
			p.setPen(pen);
			p.setBrush(color);
			const auto inner = rect - Margins(removeNow);
			if (multiChoice) {
				p.drawRoundedRect(inner, radius, radius);
			} else {
				p.drawEllipse(inner);
			}
			const auto &icon = stm->historyPollChosen;
			icon.paint(
				p,
				left + (radio.diameter - icon.width()) / 2,
				top + (radio.diameter - icon.height()) / 2,
				_owner->width());
		}
	}

	p.setOpacity(o);
}

void Poll::Options::paintPercent(
		Painter &p,
		const QString &percent,
		int percentWidth,
		int left,
		int top,
		int topPadding,
		int outerWidth,
		const PaintContext &context) const {
	const auto stm = context.messageStyle();
	const auto aleft = left + st::historyPollAnswerPadding.left();

	top += topPadding;

	p.setFont(st::historyPollPercentFont);
	p.setPen(stm->historyTextFg);
	const auto pleft = aleft - percentWidth - st::historyPollPercentSkip;
	p.drawTextLeft(
		pleft,
		top + st::historyPollPercentTop,
		outerWidth,
		percent,
		percentWidth);
}

void Poll::Options::paintFilling(
		Painter &p,
		bool chosen,
		bool correct,
		float64 filling,
		int left,
		int top,
		int topPadding,
		int width,
		int contentWidth,
		int contentHeight,
		const PaintContext &context) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto aleft = left + st::historyPollAnswerPadding.left();

	top += topPadding;

	const auto thickness = st::historyPollFillingHeight;
	const auto max = contentWidth - st::historyPollFillingRight;
	const auto size
		= anim::interpolate(st::historyPollFillingMin, max, filling);
	const auto radius = st::historyPollFillingRadius;
	const auto ftop = top
		+ std::max(st::historyPollPercentFont->height, contentHeight)
		+ st::historyPollFillingTop;

	enum class Style {
		Incorrect,
		Correct,
		Default,
	};
	const auto style = [&] {
		if (chosen && !correct) {
			return Style::Incorrect;
		} else if (chosen
			&& correct
			&& _owner->_poll->quiz()
			&& !context.outbg) {
			return Style::Correct;
		} else {
			return Style::Default;
		}
	}();
	auto barleft = aleft;
	auto barwidth = size;
	const auto &color = (style == Style::Incorrect)
		? st->boxTextFgError()
		: (style == Style::Correct)
		? st->boxTextFgGood()
		: stm->msgFileBg;
	p.setPen(Qt::NoPen);
	PainterHighQualityEnabler hq(p);
	{
		p.setBrush(anim::with_alpha(color->c, st::historyPollFillingBgOpacity));
		p.drawRoundedRect(barleft, ftop, max, thickness, radius, radius);
	}
	p.setBrush(color);
	if (chosen || correct) {
		const auto &icon = (style == Style::Incorrect)
			? st->historyPollChoiceWrong()
			: (style == Style::Correct)
			? st->historyPollChoiceRight()
			: stm->historyPollChoiceRight;
		const auto cleft = aleft - st::historyPollPercentSkip - icon.width();
		const auto ctop = ftop - (icon.height() - thickness) / 2;
		if (_owner->_flags & PollData::Flag::MultiChoice) {
			p.drawRoundedRect(
				cleft,
				ctop,
				icon.width(),
				icon.height(),
				st::historyPollCheckboxRadius,
				st::historyPollCheckboxRadius);
		} else {
			p.drawEllipse(cleft, ctop, icon.width(), icon.height());
		}

		const auto paintContent = [&](QPainter &p) {
			icon.paint(p, cleft, ctop, width);
		};
		if (style == Style::Default && _owner->usesBubblePattern(context)) {
			const auto add = st::lineWidth * 2;
			const auto target = QRect(
				cleft,
				ctop,
				icon.width(),
				icon.height()
			).marginsAdded({ add, add, add, add });
			Ui::PaintPatternBubblePart(
				p,
				context.viewport,
				context.bubblesPattern->pixmap,
				target,
				paintContent,
				_fillingIconCache);
		} else {
			paintContent(p);
		}
		//barleft += icon.width() - radius;
		//barwidth -= icon.width() - radius;
	}
	if (barwidth > 0) {
		p.drawRoundedRect(barleft, ftop, barwidth, thickness, radius, radius);
	}
}

bool Poll::Options::answerVotesChanged() const {
	if (_owner->_poll->answers.size() != _answers.size()
		|| _owner->_poll->answers.empty()) {
		return false;
	}
	for (const auto &answer : _answers) {
		const auto i = ranges::find(
			_owner->_poll->answers,
			answer.option,
			&PollAnswer::option);
		if (i == end(_owner->_poll->answers)) {
			return false;
		} else if (answer.votes != i->votes) {
			return true;
		}
	}
	return false;
}

void Poll::Options::saveStateInAnimation() const {
	if (_answersAnimation) {
		return;
	}
	const auto show = _owner->showVotes();
	_answersAnimation = std::make_unique<AnswersAnimation>();
	_answersAnimation->data.reserve(_answers.size());
	const auto convert = [&](const Answer &answer) {
		auto result = AnswerAnimation();
		result.percent = show ? float64(answer.votesPercent) : 0.;
		result.filling = show ? answer.filling : 0.;
		result.opacity = show ? 1. : 0.;
		result.chosen = answer.chosen;
		result.correct = answer.correct;
		return result;
	};
	ranges::transform(
		_answers,
		ranges::back_inserter(_answersAnimation->data),
		convert);
}

bool Poll::Options::checkAnimationStart() const {
	if (_owner->_poll->answers.size() != _answers.size()) {
		// Skip initial changes.
		return false;
	}
	const auto result = _owner->showVotes()
		!= (_owner->_poll->voted() || _owner->_poll->closed())
		|| answerVotesChanged();
	if (result) {
		saveStateInAnimation();
	}
	return result;
}

void Poll::Options::startAnswersAnimation() const {
	if (!_answersAnimation) {
		return;
	}

	const auto show = _owner->showVotes();
	auto &&both = ranges::views::zip(_answers, _answersAnimation->data);
	for (auto &&[answer, data] : both) {
		data.percent.start(show ? float64(answer.votesPercent) : 0.);
		data.filling.start(show ? answer.filling : 0.);
		data.opacity.start(show ? 1. : 0.);
		data.chosen = data.chosen || answer.chosen;
		data.correct = data.correct || answer.correct;
	}
	_answersAnimation->progress.start(
		[=] { _owner->repaint(); },
		0.,
		1.,
		st::historyPollDuration);
}

TextSelection Poll::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	const auto descLen = _headerPart->_description.length();
	const auto solLen = _headerPart->solutionSelectionLength();
	const auto descSolLen = uint16(descLen + solLen);

	if (descLen == 0 && solLen == 0) {
		return _headerPart->_question.adjustSelection(selection, type);
	}
	if (selection.to <= descLen && descLen > 0) {
		return _headerPart->_description.adjustSelection(selection, type);
	}
	if (solLen > 0
		&& selection.from >= descLen
		&& selection.to <= descSolLen) {
		const auto adjusted = _headerPart->_solutionText.adjustSelection(
			_headerPart->toSolutionSelection(selection),
			type);
		return _headerPart->fromSolutionSelection(adjusted);
	}
	const auto questionSelection = _headerPart->_question.adjustSelection(
		_headerPart->toQuestionSelection(selection),
		type);
	if (selection.from >= descSolLen) {
		return _headerPart->fromQuestionSelection(questionSelection);
	}
	const auto from = (selection.from < descLen && descLen > 0)
		? _headerPart->_description.adjustSelection(selection, type).from
		: (solLen > 0 && selection.from < descSolLen)
		? _headerPart->fromSolutionSelection(
			_headerPart->_solutionText.adjustSelection(
				_headerPart->toSolutionSelection(selection),
				type)).from
		: _headerPart->fromQuestionSelection(questionSelection).from;
	const auto to = (selection.to <= descSolLen && solLen > 0)
		? _headerPart->fromSolutionSelection(
			_headerPart->_solutionText.adjustSelection(
				_headerPart->toSolutionSelection(selection),
				type)).to
		: _headerPart->fromQuestionSelection(questionSelection).to;
	return { from, to };
}

uint16 Poll::fullSelectionLength() const {
	return _headerPart->_description.length()
		+ _headerPart->solutionSelectionLength()
		+ _headerPart->_question.length();
}

TextForMimeData Poll::selectedText(TextSelection selection) const {
	auto description = _headerPart->_description.toTextForMimeData(selection);
	auto solution = _headerPart->_solutionText.toTextForMimeData(
		_headerPart->toSolutionSelection(selection));
	auto question = _headerPart->_question.toTextForMimeData(
		_headerPart->toQuestionSelection(selection));
	auto result = TextForMimeData();
	const auto append = [&](TextForMimeData &&part) {
		if (part.empty()) {
			return;
		}
		if (!result.empty()) {
			result.append('\n');
		}
		result.append(std::move(part));
	};
	append(std::move(description));
	append(std::move(solution));
	append(std::move(question));
	return result;
}

TextState Poll::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (!_poll->sendingVotes.empty()) {
		return result;
	}

	const auto padding = st::msgPadding;
	auto paintw = width() - padding.left() - padding.right();

	const Part *parts[] = {
		_headerPart.get(),
		_optionsPart.get(),
		_addOptionPart.get(),
		_footerPart.get(),
	};
	auto tshift = 0;
	auto symbolAdd = uint16(0);
	for (const auto part : parts) {
		const auto h = part->countHeight(paintw);
		if (h > 0) {
			auto partResult = part->textState(
				point - QPoint(0, tshift),
				padding.left(),
				paintw,
				width(),
				request);
			if (partResult.link) {
				partResult.itemId = result.itemId;
				partResult.symbol += symbolAdd;
				return partResult;
			}
			if (point.y() >= tshift && point.y() < tshift + h) {
				partResult.itemId = result.itemId;
				partResult.symbol += symbolAdd;
				return partResult;
			}
		}
		symbolAdd += part->selectionLength();
		tshift += h;
	}
	return result;
}

void Poll::parentTextUpdated() {
	_headerPart->updateDescription();
	history()->owner().requestViewResize(_parent);
}

auto Poll::bubbleRoll() const -> BubbleRoll {
	const auto value = _wrongAnswerAnimation.value(1.);
	_wrongAnswerAnimated = (value < 1.);
	if (!_wrongAnswerAnimated) {
		return BubbleRoll();
	}
	const auto progress = [](float64 full) {
		const auto lower = std::floor(full);
		const auto shift = (full - lower);
		switch (int(lower) % 4) {
		case 0: return -shift;
		case 1: return (shift - 1.);
		case 2: return shift;
		case 3: return (1. - shift);
		}
		Unexpected("Value in Poll::getBubbleRollDegrees.");
	};
	return {
		.rotate = progress(value * kRotateSegments) * kRotateAmplitude,
		.scale = 1. + progress(value * kScaleSegments) * kScaleAmplitude
	};
}

QMargins Poll::bubbleRollRepaintMargins() const {
	if (!_wrongAnswerAnimated) {
		return QMargins();
	}
	static const auto kAdd = int(std::ceil(
		st::msgMaxWidth * std::sin(kRotateAmplitude * M_PI / 180.)));
	return QMargins(kAdd, kAdd, kAdd, kAdd);
}

void Poll::paintBubbleFireworks(
		Painter &p,
		const QRect &bubble,
		crl::time ms) const {
	if (!_fireworksAnimation || _fireworksAnimation->paint(p, bubble)) {
		return;
	}
	_fireworksAnimation = nullptr;
}

void Poll::clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {
	_headerPart->clickHandlerActiveChanged(handler, active);
}

void Poll::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (!handler) return;

	_headerPart->clickHandlerPressedChanged(handler, pressed);
	_optionsPart->clickHandlerPressedChanged(handler, pressed);
	_addOptionPart->clickHandlerPressedChanged(handler, pressed);
	_footerPart->clickHandlerPressedChanged(handler, pressed);
}

void Poll::hideSpoilers() {
	if (_headerPart->_description.hasSpoilers()) {
		_headerPart->_description.setSpoilerRevealed(
			false,
			anim::type::instant);
	}
	if (_headerPart->_solutionText.hasSpoilers()) {
		_headerPart->_solutionText.setSpoilerRevealed(
			false,
			anim::type::instant);
	}
}

void Poll::unloadHeavyPart() {
	_headerPart->unloadHeavyPart();
	_optionsPart->unloadHeavyPart();
}

bool Poll::hasHeavyPart() const {
	return _headerPart->hasHeavyPart()
		|| _optionsPart->hasHeavyPart();
}

void Poll::Options::toggleRipple(Answer &answer, bool pressed) {
	if (pressed) {
		const auto outerWidth = _owner->width();
		const auto innerWidth = outerWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		if (!answer.ripple) {
			auto mask = Ui::RippleAnimation::RectMask(QSize(
				outerWidth,
				countAnswerHeight(answer, innerWidth)));
			answer.ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { _owner->repaint(); });
		}
		// _owner->_lastLinkPoint is Options-local, compute answer's
		// position within Options (sum of heights above it).
		auto answerTop = 0;
		for (const auto &a : _answers) {
			if (&a == &answer) {
				break;
			}
			answerTop += countAnswerHeight(a, innerWidth);
		}
		answer.ripple->add(_owner->_lastLinkPoint - QPoint(0, answerTop));
	} else if (answer.ripple) {
		answer.ripple->lastStop();
	}
}

bool Poll::Header::canShowSolution() const {
	return _owner->showVotes() && !_owner->_poll->solution.text.isEmpty();
}

bool Poll::Header::inShowSolution(
		QPoint point,
		int right,
		int top) const {
	if (!canShowSolution() || !_solutionButtonVisible) {
		return false;
	}
	const auto &icon = st::historyQuizExplainIn;
	const auto x = right - icon.width();
	const auto y = top + (st::normalFont->height - icon.height()) / 2;
	return QRect(x, y, icon.width(), icon.height()).contains(point);
}

QString Poll::Footer::closeTimerText() const {
	if (_owner->_poll->closeDate <= 0
		|| (_owner->_flags & PollData::Flag::Closed)) {
		_closeTimer.cancel();
		return {};
	}
	const auto left = _owner->_poll->closeDate - base::unixtime::now();
	if (left <= 0) {
		_closeTimer.cancel();
		return {};
	}
	if (!_closeTimer.isActive()) {
		_closeTimer.callEach(1000);
	}
	const auto hideResults = (_owner->_flags
		& PollData::Flag::HideResultsUntilClose);
	if (left >= 86400) {
		const auto days = (left + 86399) / 86400;
		return hideResults
			? tr::lng_polls_results_in_days(tr::now, lt_count, days)
			: tr::lng_polls_ends_in_days(tr::now, lt_count, days);
	}
	const auto timer = Ui::FormatDurationText(left);
	return hideResults
		? tr::lng_polls_results_in_time(tr::now, lt_time, timer)
		: tr::lng_polls_ends_in_time(tr::now, lt_time, timer);
}

bool Poll::Footer::timerFooterMultiline(int paintw) const {
	const auto timerText = closeTimerText();
	if (timerText.isEmpty()) {
		return false;
	}
	if (_owner->_voted && !_owner->showVotes()) {
		return true;
	}
	const auto sep = QString::fromUtf8(" \xC2\xB7 ");
	const auto full = _totalVotesLabel.toString()
		+ sep
		+ timerText;
	return centeredOverlapsInfo(st::msgDateFont->width(full), paintw);
}

bool Poll::Footer::centeredOverlapsInfo(
		int textWidth,
		int innerWidth) const {
	const auto skipw = _owner->_parent->skipBlockWidth();
	return (innerWidth + textWidth) / 2 > innerWidth - skipw;
}

int Poll::Footer::bottomLineWidth(int innerWidth) const {
	const auto inline_ = _owner->inlineFooter();
	const auto timerText = closeTimerText();
	const auto timerLine = hasTimerLine(innerWidth);

	if (inline_ || _owner->showVotersCount()) {
		if (timerText.isEmpty()) {
			return _totalVotesLabel.maxWidth();
		} else if (timerLine) {
			return st::msgDateFont->width(timerText);
		}
		// Single-line timer — timerFooterMultiline already handles.
		return 0;
	}

	if (_owner->_addOptionActive) {
		return timerLine
			? 0
			: st::semiboldFont->width(
				tr::lng_polls_add_option_save(tr::now));
	} else if (_owner->isAuthorNotVoted()
		&& !_owner->_adminShowResults
		&& !_owner->canSendVotes()) {
		return timerLine
			? 0
			: (_owner->_totalVotes > 0)
			? _adminVotesLabel.maxWidth()
			: _totalVotesLabel.maxWidth();
	} else if (_owner->_adminShowResults
		&& _owner->isAuthorNotVoted()) {
		return timerLine ? 0 : _adminBackVoteLabel.maxWidth();
	}

	if (timerLine) {
		return st::msgDateFont->width(timerText);
	}
	const auto votedPublic = _owner->_voted
		&& (_owner->_flags & PollData::Flag::PublicVotes);
	const auto string = (_owner->showVotes() || votedPublic)
		? ((_owner->_flags & PollData::Flag::PublicVotes)
			? tr::lng_polls_view_votes(
				tr::now,
				lt_count,
				_owner->_totalVotes)
			: tr::lng_polls_view_results(tr::now))
		: tr::lng_polls_submit_votes(tr::now);
	return st::semiboldFont->width(string);
}

int Poll::Footer::dateInfoPadding(int innerWidth) const {
	const auto w = bottomLineWidth(innerWidth);
	return (w > 0 && centeredOverlapsInfo(w, innerWidth))
		? st::msgDateFont->height
		: 0;
}

Poll::~Poll() {
	history()->owner().unregisterPollView(_poll, _parent);
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

} // namespace HistoryView
