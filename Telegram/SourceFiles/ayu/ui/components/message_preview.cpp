// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/message_preview.h"

#include "ayu/ayu_settings.h"
#include "ayu/utils/official_resources.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_edition.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_fake_items.h"
#include "main/main_session.h"
#include "styles/style_chat.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/animations.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"

namespace {

[[nodiscard]] QString OfficialChannelLabel() {
	return QString::fromLatin1(TeleForge::OfficialResources::kEntries.front().label);
}

} // namespace

class MessagePreview::PreviewDelegate final
	: public HistoryView::SimpleElementDelegate {
public:
	using HistoryView::SimpleElementDelegate::SimpleElementDelegate;
private:
	HistoryView::Context elementContext() override {
		return HistoryView::Context::ContactPreview;
	}
};

struct MessagePreview::State {
	AdminLog::OwnedItem reply;
	AdminLog::OwnedItem item;
	std::unique_ptr<PreviewDelegate> delegate;
	std::unique_ptr<Ui::ChatStyle> style;
	Ui::Animations::Simple heightAnimation;
	std::unique_ptr<Ui::ChatTheme> theme;
	int currentHeight = 0;
};

MessagePreview::MessagePreview(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _state(lifetime().make_state<State>()) {
	_state->delegate = std::make_unique<PreviewDelegate>(
		controller,
		crl::guard(this, [=] { update(); }));
	_state->style = std::make_unique<Ui::ChatStyle>(
		controller->session().colorIndicesValue());
	_state->style->apply(controller->defaultChatTheme().get());

	const auto history = controller->session().data().history(
		PeerData::kServiceNotificationsId);

	_state->reply = HistoryView::GenerateItem(
		_state->delegate.get(),
		history,
		history->session().userPeerId(),
		FullMsgId(),
		u"Update wehn?"_q);

	const auto previewUser = HistoryView::GenerateUser(
		history,
		OfficialChannelLabel());
	const auto messageItem = history->addNewLocalMessage({
		.id = history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::HasFromId
			| MessageFlag::HasReplyInfo),
		.from = previewUser,
		.replyTo = FullReplyTo{
			.messageId = _state->reply->data()->fullId(),
		},
		.date = base::unixtime::now(),
	}, TextWithEntities{ u"You need to touch some grass."_q },
	MTP_messageMediaEmpty());

	messageItem->setDeleted();

	_state->item = AdminLog::OwnedItem(
		_state->delegate.get(),
		messageItem);

	auto edition = HistoryMessageEdition();
	edition.editDate = base::unixtime::now();
	edition.textWithEntities = TextWithEntities{
		u"You need to touch some grass."_q,
	};
	edition.useSameViews = true;
	edition.useSameForwards = true;
	edition.useSameReplies = true;
	edition.useSameMarkup = true;
	edition.useSameReactions = true;
	edition.useSameSuggest = true;
	messageItem->applyEdition(std::move(edition));
	_state->item.refreshView(_state->delegate.get());

	_state->theme = Window::Theme::DefaultChatThemeOn(lifetime());

	widthValue(
	) | rpl::filter(
		rpl::mappers::_1 >= (st::historyMinimalWidth / 2)
	) | rpl::on_next([=](int w) {
		updateWidgetSize(w);
	}, lifetime());

	rpl::merge(
		AyuSettings::getInstance().replaceBottomInfoWithIconsChanges()
			| rpl::to_empty,
		AyuSettings::getInstance().deletedMarkChanges()
			| rpl::to_empty,
		AyuSettings::getInstance().editedMarkChanges()
			| rpl::to_empty,
		AyuSettings::getInstance().removeMessageTailChanges()
			| rpl::to_empty,
		AyuSettings::getInstance().hideFastShareChanges()
			| rpl::to_empty,
		AyuSettings::getInstance().simpleQuotesAndRepliesChanges()
			| rpl::to_empty,
		AyuSettings::getInstance().semiTransparentDeletedMessagesChanges()
			| rpl::to_empty
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());
}

void MessagePreview::paintEvent(QPaintEvent *e) {
	const auto view = _state->item.get();
	if (!view) {
		return;
	}

	auto p = Painter(this);
	p.setClipRect(e->rect());
	Window::SectionWidget::PaintBackground(
		p,
		_state->theme.get(),
		QSize(width(), window()->height()),
		e->rect());

	auto hq = PainterHighQualityEnabler(p);
	const auto theme = _controller->defaultChatTheme().get();
	auto context = theme->preparePaintContext(
		_state->style.get(),
		rect(),
		rect(),
		rect(),
		_controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer));
	context.outbg = view->hasOutLayout();

	const auto padding = st::settingsForwardPrivacyPadding;
	p.translate(padding / 2, padding + view->marginBottom());
	view->draw(p, context);

	if (!AyuSettings::getInstance().hideFastShare()) {
		const auto size = st::historyFastShareSize;
		const auto g = view->innerGeometry();
		const auto shareLeft = g.x() + g.width()
			+ st::historyFastShareLeft;
		const auto shareTop = g.y() + g.height()
			- st::historyFastShareBottom - size;
		const auto shareRect = QRect(
			shareLeft,
			shareTop,
			size,
			size);
		p.setPen(Qt::NoPen);
		p.setBrush(context.st->msgServiceBg());
		p.drawEllipse(shareRect);
		p.save();
		const auto center = shareRect.center();
		p.translate(center);
		p.scale(-1., 1.);
		p.translate(-center);
		context.st->historyFastShareIcon().paintInCenter(
			p,
			shareRect);
		p.restore();
	}
}

void MessagePreview::updateWidgetSize(int width, bool animate) {
	const auto view = _state->item.get();
	if (!view) {
		return;
	}
	const auto padding = st::settingsForwardPrivacyPadding;
	const auto height = view->resizeGetHeight(width);
	const auto top = view->marginTop();
	const auto bottom = view->marginBottom();
	const auto full = padding + top + height + bottom + padding;
	if (animate && _state->currentHeight > 0
		&& _state->currentHeight != full) {
		_state->heightAnimation.start([=] {
			resize(
				width,
				int(base::SafeRound(
					_state->heightAnimation.value(full))));
		}, _state->currentHeight, full, st::slideWrapDuration);
	} else {
		resize(width, full);
	}
	_state->currentHeight = full;
}

void MessagePreview::refresh() {
	_state->style = std::make_unique<Ui::ChatStyle>(
		_controller->session().colorIndicesValue());
	_state->style->apply(_controller->defaultChatTheme().get());
	_state->item.refreshView(_state->delegate.get());
	if (width() > 0) {
		updateWidgetSize(width(), true);
	}
	update();
}
