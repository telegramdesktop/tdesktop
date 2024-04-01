/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_about_view.h"

#include "api/api_premium.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/random.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/click_handler_types.h"
#include "data/business/data_business_common.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_service_box.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_reply_markup.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_chat_intro.h"
#include "settings/settings_premium.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

class PremiumRequiredBox final : public ServiceBoxContent {
public:
	explicit PremiumRequiredBox(not_null<Element*> parent);
	~PremiumRequiredBox();

	int width() override;
	int top() override;
	QSize size() override;
	QString title() override;
	TextWithEntities subtitle() override;
	int buttonSkip() override;
	rpl::producer<QString> button() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) override;
	ClickHandlerPtr createViewLink() override;

	bool hideServiceText() override {
		return true;
	}

	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

private:
	const not_null<Element*> _parent;

};

auto GenerateChatIntro(
	not_null<Element*> parent,
	Element *replacing,
	const Data::ChatIntro &data,
	Fn<void(not_null<DocumentData*>)> helloChosen)
-> Fn<void(Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			if (text.empty()) {
				return;
			}
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				links));
		};
		const auto title = data.customPhrases()
			? data.title
			: tr::lng_chat_intro_default_title(tr::now);
		const auto description = data.customPhrases()
			? data.description
			: tr::lng_chat_intro_default_message(tr::now);
		pushText(Ui::Text::Bold(title), st::chatIntroTitleMargin);
		pushText({ description }, title.isEmpty()
			? st::chatIntroTitleMargin
			: st::chatIntroMargin);
		const auto sticker = [=] {
			using Tag = ChatHelpers::StickerLottieSize;
			auto sticker = data.sticker;
			if (!sticker) {
				const auto api = &parent->history()->session().api();
				const auto &list = api->premium().helloStickers();
				if (!list.empty()) {
					sticker = list[base::RandomIndex(list.size())];
					if (helloChosen) {
						helloChosen(sticker);
					}
				}
			}
			const auto send = [=] {
				Api::SendExistingDocument(Api::MessageToSend(
					Api::SendAction(parent->history())
				), sticker);
			};
			return StickerInBubblePart::Data{
				.sticker = sticker,
				.size = st::chatIntroStickerSize,
				.cacheTag = Tag::ChatIntroHelloSticker,
				.link = std::make_shared<LambdaClickHandler>(send),
			};
		};
		push(std::make_unique<StickerInBubblePart>(
			parent,
			replacing,
			sticker,
			st::chatIntroStickerPadding));
	};
}

PremiumRequiredBox::PremiumRequiredBox(not_null<Element*> parent)
: _parent(parent) {
}

PremiumRequiredBox::~PremiumRequiredBox() = default;

int PremiumRequiredBox::width() {
	return st::premiumRequiredWidth;
}

int PremiumRequiredBox::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize PremiumRequiredBox::size() {
	return { st::msgServicePhotoWidth, st::msgServicePhotoWidth };
}

QString PremiumRequiredBox::title() {
	return QString();
}

int PremiumRequiredBox::buttonSkip() {
	return st::storyMentionButtonSkip;
}

rpl::producer<QString> PremiumRequiredBox::button() {
	return tr::lng_send_non_premium_go();
}

TextWithEntities PremiumRequiredBox::subtitle() {
	return _parent->data()->notificationText();
}

ClickHandlerPtr PremiumRequiredBox::createViewLink() {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			Settings::ShowPremium(controller, u"require_premium"_q);
		}
	});
}

void PremiumRequiredBox::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	p.setBrush(context.st->msgServiceBg()); // ?
	p.setPen(Qt::NoPen);
	p.drawEllipse(geometry);
	st::premiumRequiredIcon.paintInCenter(p, geometry);
}

void PremiumRequiredBox::stickerClearLoopPlayed() {
}

std::unique_ptr<StickerPlayer> PremiumRequiredBox::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

bool PremiumRequiredBox::hasHeavyPart() {
	return false;
}

void PremiumRequiredBox::unloadHeavyPart() {
}

} // namespace

AboutView::AboutView(
	not_null<History*> history,
	not_null<ElementDelegate*> delegate)
: _history(history)
, _delegate(delegate) {
}

AboutView::~AboutView() {
	setItem({}, nullptr);
}

not_null<History*> AboutView::history() const {
	return _history;
}

Element *AboutView::view() const {
	return _item.get();
}

HistoryItem *AboutView::item() const {
	if (const auto element = view()) {
		return element->data();
	}
	return nullptr;
}

bool AboutView::refresh() {
	const auto user = _history->peer->asUser();
	const auto info = user ? user->botInfo.get() : nullptr;
	if (!info) {
		if (user && !user->isSelf() && _history->isDisplayedEmpty()) {
			if (_item) {
				return false;
			} else if (user->meRequiresPremiumToWrite()
				&& !user->session().premium()) {
				setItem(makePremiumRequired(), nullptr);
			} else {
				makeIntro(user);
			}
			return true;
		}
		if (_item) {
			setItem({}, nullptr);
			return true;
		}
		_version = 0;
		return false;
	}
	const auto version = info->descriptionVersion;
	if (_version == version) {
		return false;
	}
	_version = version;
	setItem(makeAboutBot(info), nullptr);
	return true;
}

void AboutView::makeIntro(not_null<UserData*> user) {
	make(user->businessDetails().intro);
}

void AboutView::make(Data::ChatIntro data, bool preview) {
	const auto text = data
		? tr::lng_action_set_chat_intro(
			tr::now,
			lt_from,
			_history->peer->name())
		: QString();
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{ { text }});

	if (data.sticker) {
		_helloChosen = nullptr;
	} else if (_helloChosen) {
		data.sticker = _helloChosen;
	}

	auto owned = AdminLog::OwnedItem(_delegate, item);
	const auto helloChosen = [=](not_null<DocumentData*> sticker) {
		setHelloChosen(sticker);
	};
	const auto handler = [=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (controller->session().premium()) {
				controller->showSettings(Settings::ChatIntroId());
			} else {
				ShowPremiumPreviewBox(
					controller->uiShow(),
					PremiumFeature::ChatIntro);
			}
		}
	};
	owned->overrideMedia(std::make_unique<HistoryView::MediaGeneric>(
		owned.get(),
		GenerateChatIntro(owned.get(), _item.get(), data, helloChosen),
		HistoryView::MediaGenericDescriptor{
			.maxWidth = st::chatIntroWidth,
			.serviceLink = std::make_shared<LambdaClickHandler>(handler),
			.service = true,
			.hideServiceText = preview || text.isEmpty(),
		}));
	if (!data.sticker && _helloChosen) {
		data.sticker = _helloChosen;
	}
	setItem(std::move(owned), data.sticker);
}

void AboutView::toggleStickerRegistered(bool registered) {
	if (const auto item = _item ? _item->data().get() : nullptr) {
		if (_sticker) {
			const auto owner = &item->history()->owner();
			if (registered) {
				owner->registerDocumentItem(_sticker, item);
			} else {
				owner->unregisterDocumentItem(_sticker, item);
			}
		}
	}
	if (!registered) {
		_sticker = nullptr;
	}
}

void AboutView::setHelloChosen(not_null<DocumentData*> sticker) {
	_helloChosen = sticker;
	toggleStickerRegistered(false);
	_sticker = sticker;
	toggleStickerRegistered(true);
}

void AboutView::setItem(AdminLog::OwnedItem item, DocumentData *sticker) {
	toggleStickerRegistered(false);
	_item = std::move(item);
	_sticker = sticker;
	toggleStickerRegistered(true);
}

AdminLog::OwnedItem AboutView::makeAboutBot(not_null<BotInfo*> info) {
	const auto textWithEntities = TextUtilities::ParseEntities(
		info->description,
		Ui::ItemTextBotNoMonoOptions().flags);
	const auto make = [&](auto &&...args) {
		return _history->makeMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = (MessageFlag::FakeAboutView
				| MessageFlag::FakeHistoryItem
				| MessageFlag::Local),
			.from = _history->peer->id,
		}, std::forward<decltype(args)>(args)...);
	};
	const auto item = info->document
		? make(info->document, textWithEntities)
		: info->photo
		? make(info->photo, textWithEntities)
		: make(textWithEntities, MTP_messageMediaEmpty());
	return AdminLog::OwnedItem(_delegate, item);
}

AdminLog::OwnedItem AboutView::makePremiumRequired() {
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{ tr::lng_send_non_premium_text(
		tr::now,
		lt_user,
		Ui::Text::Bold(_history->peer->shortName()),
		Ui::Text::RichLangValue),
	});
	auto result = AdminLog::OwnedItem(_delegate, item);
	result->overrideMedia(std::make_unique<ServiceBox>(
		result.get(),
		std::make_unique<PremiumRequiredBox>(result.get())));
	return result;
}

} // namespace HistoryView
