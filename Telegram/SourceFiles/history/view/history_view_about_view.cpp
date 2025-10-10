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
#include "ui/effects/premium_stars.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "countries/countries_instance.h"
#include "data/business/data_business_common.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_group_call_bar.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_service_box.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_unique_gift.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_reply_markup.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_chat_intro.h"
#include "settings/settings_credits.h" // BuyStarsHandler
#include "settings/settings_premium.h"
#include "ui/chat/chat_style.h"
#include "ui/text/custom_emoji_instance.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h" // GroupCallUserpics
#include "styles/style_credits.h"

namespace HistoryView {
namespace {

constexpr auto kLabelOpacity = 0.85;
constexpr auto kMaxCommonChatsUserpics = 3;

class EmptyChatLockedBox final
	: public ServiceBoxContent
	, public base::has_weak_ptr {
public:
	enum class Type {
		PremiumRequired,
		StarsCharged,
		FreeDirect,
	};

	EmptyChatLockedBox(not_null<Element*> parent, Type type);
	~EmptyChatLockedBox();

	int width() override;
	int top() override;
	QSize size() override;
	TextWithEntities title() override;
	TextWithEntities subtitle() override;
	int buttonSkip() override;
	rpl::producer<QString> button() override;
	std::optional<Ui::Premium::MiniStarsType> buttonMinistars() override;
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
	Settings::BuyStarsHandler _buyStars;
	rpl::variable<bool> _buyStarsLoading;
	Type _type = {};

};

class UserpicsList final : public Ui::DynamicImage {
public:
	UserpicsList(
		std::vector<not_null<PeerData*>> peers,
		const style::GroupCallUserpics &st,
		int countOverride = 0);

	[[nodiscard]] int width() const;

	std::shared_ptr<Ui::DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	struct Subscribed {
		explicit Subscribed(Fn<void()> callback)
		: callback(std::move(callback)) {
		}

		std::vector<HistoryView::UserpicInRow> list;
		bool someNotLoaded = false;
		Fn<void()> callback;
		int paletteVersion = 0;
	};

	const std::vector<not_null<PeerData*>> _peers;
	const style::GroupCallUserpics &_st;
	const int _countOverride = 0;

	QImage _frame;
	std::unique_ptr<Subscribed> _subscribed;

};

class NewBotThreadDottedLine final : public MediaGenericPart {
public:
	explicit NewBotThreadDottedLine(not_null<Element*> parent);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	const not_null<Element*> _parent;

};

class NewBotThreadDownIcon final : public MediaGenericPart {
public:
	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

};

UserpicsList::UserpicsList(
	std::vector<not_null<PeerData*>> peers,
	const style::GroupCallUserpics &st,
	int countOverride)
: _peers(std::move(peers))
, _st(st)
, _countOverride(countOverride) {
}

std::shared_ptr<Ui::DynamicImage> UserpicsList::clone() {
	return std::make_shared<UserpicsList>(_peers, _st);
}

QImage UserpicsList::image(int size) {
	Expects(_subscribed != nullptr);

	const auto regenerate = [&] {
		const auto version = style::PaletteVersion();
		if (_frame.isNull() || _subscribed->paletteVersion != version) {
			_subscribed->paletteVersion = version;
			return true;
		}
		for (auto &entry : _subscribed->list) {
			const auto peer = entry.peer;
			auto &view = entry.view;
			const auto wasView = view.cloud.get();
			if (peer->userpicUniqueKey(view) != entry.uniqueKey
				|| view.cloud.get() != wasView) {
				return true;
			}
		}
		return false;
	}();
	if (regenerate) {
		const auto max = std::max(_countOverride, int(_peers.size()));
		GenerateUserpicsInRow(_frame, _subscribed->list, _st, max);
	}
	return _frame;
}

void UserpicsList::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_subscribed = nullptr;
		return;
	}
	_subscribed = std::make_unique<Subscribed>(std::move(callback));
	for (const auto peer : _peers) {
		_subscribed->list.push_back({ .peer = peer });
	}
}

int UserpicsList::width() const {
	const auto count = std::max(_countOverride, int(_peers.size()));
	if (!count) {
		return 0;
	}
	const auto shifted = count - 1;
	return _st.size + (shifted * (_st.size - _st.shift));
}

void NewBotThreadDownIcon::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	auto color = context.st->msgServiceFg()->c;
	color.setAlphaF(color.alphaF() * kLabelOpacity);
	st::newBotThreadDown.paintInCenter(
		p,
		QRect(0, 0, outerWidth, st::newBotThreadDown.height()),
		color);
}

QSize NewBotThreadDownIcon::countOptimalSize() {
	return st::newBotThreadDown.size();
}

QSize NewBotThreadDownIcon::countCurrentSize(int newWidth) {
	return st::newBotThreadDown.size();
}

NewBotThreadDottedLine::NewBotThreadDottedLine(not_null<Element*> parent)
: _parent(parent) {
}

void NewBotThreadDottedLine::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto skip = st::monoforumBarUserpicSkip;
	auto pen = context.st->msgServiceBg()->p;
	pen.setWidthF(skip);
	pen.setCapStyle(Qt::RoundCap);
	pen.setDashPattern({ 2., 2. });
	p.setPen(pen);
	const auto top = -st::newBotThreadTopSkip / 2;
	p.drawLine(context.viewport.x(), top, context.viewport.width(), top);
}

QSize NewBotThreadDottedLine::countOptimalSize() {
	return { 0, 0 };
}

QSize NewBotThreadDottedLine::countCurrentSize(int newWidth) {
	return { 0, 0 };
}

auto GenerateChatIntro(
	not_null<Element*> parent,
	Element *replacing,
	const Data::ChatIntro &data,
	Fn<void(not_null<DocumentData*>)> helloChosen,
	Fn<void(not_null<DocumentData*>)> sendIntroSticker)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
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
				st::defaultTextStyle,
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
				sendIntroSticker(sticker);
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

auto GenerateNewBotThread(
	not_null<Element*> parent,
	Element *replacing)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
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
				st::defaultTextStyle,
				links));
		};
		const auto title = tr::lng_bot_new_thread_title(tr::now);
		const auto description = tr::lng_bot_new_thread_about(tr::now);
		push(std::make_unique<NewBotThreadDottedLine>(parent));
		pushText(Ui::Text::Bold(title), st::chatIntroTitleMargin);
		pushText({ description }, st::chatIntroMargin);
		push(std::make_unique<NewBotThreadDownIcon>());

		parent->addVerticalMargins(
			st::newBotThreadTopSkip - st::msgServiceMargin.top(),
			st::msgServiceMargin.top());
	};
}

auto GenerateNewPeerInfo(
	not_null<Element*> parent,
	Element *replacing,
	not_null<UserData*> user,
	std::vector<not_null<PeerData*>> commonGroups)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto normalFg = [](const PaintContext &context) {
			return context.st->msgServiceFg()->c;
		};
		const auto fadedFg = [](const PaintContext &context) {
			auto result = context.st->msgServiceFg()->c;
			result.setAlphaF(result.alphaF() * kLabelOpacity);
			return result;
		};
		push(std::make_unique<MediaGenericTextPart>(
			Ui::Text::Bold(user->name()),
			st::newPeerTitleMargin));
		push(std::make_unique<TextPartColored>(
			tr::lng_new_contact_not_contact(tr::now, Ui::Text::WithEntities),
			st::newPeerSubtitleMargin,
			fadedFg));

		auto entries = std::vector<AttributeTable::Entry>();
		const auto country = user->phoneCountryCode();
		if (!country.isEmpty()) {
			const auto &countries = Countries::Instance();
			const auto name = countries.countryNameByISO2(country);
			const auto flag = countries.flagEmojiByISO2(country);
			entries.push_back({
				tr::lng_new_contact_phone_number(tr::now),
				Ui::Text::Bold(flag + QChar(0xA0) + name),
			});
		}
		const auto month = user->registrationMonth();
		const auto year = user->registrationYear();
		if (month && year) {
			entries.push_back({
				tr::lng_new_contact_registration(tr::now),
				Ui::Text::Bold(langMonthOfYearFull(month, year)),
			});
		}

		const auto context = Core::TextContext({
			.session = &parent->history()->session(),
			.repaint = [parent] { parent->repaint(); },
		});
		const auto kUserpicsPrefix = u"userpics-list/"_q;
		if (const auto count = user->commonChatsCount()) {
			const auto url = u"internal:common_groups/"_q
				+ QString::number(user->id.value);
			auto ids = QStringList();
			const auto userpics = std::min(count, kMaxCommonChatsUserpics);
			for (auto i = 0; i != userpics; ++i) {
				ids.push_back(QString::number(i < commonGroups.size()
					? commonGroups[i]->id.value
					: 0));
			}
			auto userpicsData = kUserpicsPrefix + ids.join(',');
			entries.push_back({
				tr::lng_new_contact_common_groups(tr::now),
				Ui::Text::Wrapped(
					tr::lng_new_contact_groups(
						tr::now,
						lt_count,
						count,
						lt_emoji,
						Ui::Text::SingleCustomEmoji(userpicsData),
						lt_arrow,
						Ui::Text::IconEmoji(&st::textMoreIconEmoji),
						Ui::Text::Bold),
					EntityType::CustomUrl,
					url),
			});
		}

		auto copy = context;
		copy.customEmojiFactory = [=, old = copy.customEmojiFactory](
			QStringView data,
			const Ui::Text::MarkedContext &context
		) -> std::unique_ptr<Ui::Text::CustomEmoji> {
			if (!data.startsWith(kUserpicsPrefix)) {
				return old(data, context);
			}
			const auto ids = data.mid(kUserpicsPrefix.size()).split(',');
			auto peers = std::vector<not_null<PeerData*>>();
			for (const auto &id : ids) {
				if (const auto peerId = PeerId(id.toULongLong())) {
					peers.push_back(user->owner().peer(peerId));
				}
			}
			auto image = std::make_shared<UserpicsList>(
				std::move(peers),
				st::newPeerUserpics,
				ids.size());
			const auto size = image->width();
			return std::make_unique<Ui::CustomEmoji::DynamicImageEmoji>(
				data.toString(),
				std::move(image),
				context.repaint,
				st::newPeerUserpicsPadding,
				size);
		};
		push(std::make_unique<AttributeTable>(
			std::move(entries),
			st::newPeerSubtitleMargin,
			fadedFg,
			normalFg,
			copy));

		const auto details = user->botVerifyDetails();
		const auto text = details
			? Data::SingleCustomEmoji(
				details->iconId
			).append(' ').append(details->description)
			: Ui::Text::IconEmoji(
				&st::newPeerNonOfficial
			).append(' ').append(tr::lng_new_contact_not_official(tr::now));
		push(std::make_unique<TextPartColored>(
			text,
			st::newPeerSubtitleMargin,
			fadedFg,
			st::defaultTextStyle,
			base::flat_map<uint16, ClickHandlerPtr>(),
			context));
	};
}

EmptyChatLockedBox::EmptyChatLockedBox(not_null<Element*> parent, Type type)
: _parent(parent)
, _type(type) {
}

EmptyChatLockedBox::~EmptyChatLockedBox() = default;

int EmptyChatLockedBox::width() {
	return (_type == Type::PremiumRequired)
		? st::premiumRequiredWidth
		: st::starsPerMessageWidth;
}

int EmptyChatLockedBox::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize EmptyChatLockedBox::size() {
	return { st::msgServicePhotoWidth, st::msgServicePhotoWidth };
}

TextWithEntities EmptyChatLockedBox::title() {
	return {};
}

int EmptyChatLockedBox::buttonSkip() {
	return st::storyMentionButtonSkip;
}

rpl::producer<QString> EmptyChatLockedBox::button() {
	return (_type == Type::FreeDirect)
		? nullptr
		: (_type == Type::PremiumRequired)
		? tr::lng_send_non_premium_go()
		: tr::lng_send_charges_stars_go();
}

auto EmptyChatLockedBox::buttonMinistars()
-> std::optional<Ui::Premium::MiniStarsType> {
	return Ui::Premium::MiniStarsType::SlowStars;
}

TextWithEntities EmptyChatLockedBox::subtitle() {
	return _parent->data()->notificationText();
}

ClickHandlerPtr EmptyChatLockedBox::createViewLink() {
	_buyStarsLoading = _buyStars.loadingValue();
	const auto handler = [=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (_type == Type::PremiumRequired) {
				Settings::ShowPremium(controller, u"require_premium"_q);
			} else if (!_buyStarsLoading.current()) {
				_buyStars.handler(controller->uiShow())();
			}
		}
	};
	return std::make_shared<LambdaClickHandler>(crl::guard(this, handler));
}

void EmptyChatLockedBox::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	p.setBrush(context.st->msgServiceBg()); // ?
	p.setPen(Qt::NoPen);
	p.drawEllipse(geometry);
	(_type == Type::PremiumRequired
		? st::premiumRequiredIcon
		: st::directMessagesIcon).paintInCenter(p, geometry);
}

void EmptyChatLockedBox::stickerClearLoopPlayed() {
}

std::unique_ptr<StickerPlayer> EmptyChatLockedBox::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

bool EmptyChatLockedBox::hasHeavyPart() {
	return false;
}

void EmptyChatLockedBox::unloadHeavyPart() {
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

bool AboutView::aboveHistory() const {
	return !_history->peer->isBot() || !_history->isForum();
}

bool AboutView::refresh() {
	if (_history->peer->isVerifyCodes()) {
		if (_item) {
			return false;
		}
		setItem(makeAboutVerifyCodes(), nullptr);
		return true;
	}
	const auto user = _history->peer->asUser();
	const auto monoforum = _history->peer->isMonoforum()
		? _history->peer->asChannel()
		: nullptr;
	const auto info = user ? user->botInfo.get() : nullptr;
	if (!info) {
		if (user
			&& !user->isContact()
			&& !user->phoneCountryCode().isEmpty()) {
			if (_item && !_commonGroupsStale) {
				return false;
			}
			loadCommonGroups();
			setItem(makeNewPeerInfo(user), nullptr);
			return true;
		} else if (user && !user->isSelf() && _history->isDisplayedEmpty()) {
			if (_item) {
				return false;
			} else if (user->requiresPremiumToWrite()
				&& !user->session().premium()) {
				setItem(makePremiumRequired(), nullptr);
			} else if (user->isBlocked()) {
				setItem(makeBlocked(), nullptr);
			} else if (user->businessDetails().intro) {
				makeIntro(user);
			} else if (const auto stars = user->starsPerMessageChecked()) {
				setItem(makeStarsPerMessage(stars), nullptr);
			} else {
				makeIntro(user);
			}
			return true;
		} else if (monoforum && _history->isDisplayedEmpty()) {
			if (_item) {
				return false;
			}
			setItem(
				makeStarsPerMessage(monoforum->starsPerMessageChecked()),
				nullptr);
			return true;
		}
		if (_item) {
			setItem({}, nullptr);
			return true;
		}
		_version = 0;
		return false;
	} else if (_history->peer->isForum()) {
		if (_item) {
			return false;
		}
		setItem(makeNewBotThread(), nullptr);
		return true;
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
	const auto sendIntroSticker = [=](not_null<DocumentData*> sticker) {
		_sendIntroSticker.fire_copy(sticker);
	};
	owned->data()->setCustomServiceLink(
		std::make_shared<LambdaClickHandler>(handler));
	owned->overrideMedia(std::make_unique<HistoryView::MediaGeneric>(
		owned.get(),
		GenerateChatIntro(
			owned.get(),
			_item.get(),
			data,
			helloChosen,
			sendIntroSticker),
		HistoryView::MediaGenericDescriptor{
			.maxWidth = st::chatIntroWidth,
			.service = true,
			.hideServiceText = preview || text.isEmpty(),
		}));
	if (!data.sticker && _helloChosen) {
		data.sticker = _helloChosen;
	}
	setItem(std::move(owned), data.sticker);
}

rpl::producer<not_null<DocumentData*>> AboutView::sendIntroSticker() const {
	return _sendIntroSticker.events();
}

rpl::producer<> AboutView::refreshRequests() const {
	return _refreshRequests.events();
}

rpl::lifetime &AboutView::lifetime() {
	return _lifetime;
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

void AboutView::loadCommonGroups() {
	if (_commonGroupsRequested) {
		return;
	}
	_commonGroupsRequested = true;

	const auto user = _history->peer->asUser();
	if (!user) {
		return;
	}

	struct Cached {
		std::vector<not_null<PeerData*>> list;
	};
	struct Session {
		base::flat_map<not_null<UserData*>, Cached> data;
	};
	static auto Map = base::flat_map<not_null<Main::Session*>, Session>();
	const auto session = &_history->session();
	auto i = Map.find(session);
	if (i == end(Map)) {
		i = Map.emplace(session).first;
		session->lifetime().add([session] {
			Map.remove(session);
		});
	}
	auto &cached = i->second.data[user];

	const auto count = user->commonChatsCount();
	if (!count) {
		cached = {};
		return;
	} else while (cached.list.size() > count) {
		cached.list.pop_back();
	}
	_commonGroups = cached.list;
	const auto requestId = _history->session().api().request(
		MTPmessages_GetCommonChats(
			user->inputUser,
			MTP_long(0),
			MTP_int(kMaxCommonChatsUserpics))
	).done([=](const MTPmessages_Chats &result) {
		const auto chats = result.match([](const auto &data) {
			return &data.vchats().v;
		});
		auto &owner = user->session().data();
		auto list = std::vector<not_null<PeerData*>>();
		list.reserve(chats->size());
		for (const auto &chat : *chats) {
			if (const auto peer = owner.processChat(chat)) {
				list.push_back(peer);
				if (list.size() == kMaxCommonChatsUserpics) {
					break;
				}
			}
		}
		if (_commonGroups != list) {
			Map[session].data[user].list = list;
			_commonGroups = std::move(list);
			_commonGroupsStale = true;
			_refreshRequests.fire({});
		}
	}).send();

	_lifetime.add([=] {
		_history->session().api().request(requestId).cancel();
	});
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

AdminLog::OwnedItem AboutView::makeNewPeerInfo(not_null<UserData*> user) {
	_commonGroupsStale = false;

	const auto text = user->name();
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{ { text }});

	auto owned = AdminLog::OwnedItem(_delegate, item);
	owned->overrideMedia(std::make_unique<HistoryView::MediaGeneric>(
		owned.get(),
		GenerateNewPeerInfo(owned.get(), _item.get(), user, _commonGroups),
		HistoryView::MediaGenericDescriptor{
			.maxWidth = st::newPeerWidth,
			.service = true,
			.hideServiceText = true,
		}));
	return owned;
}

AdminLog::OwnedItem AboutView::makeAboutVerifyCodes() {
	return makeAboutSimple(
		tr::lng_verification_codes_about(tr::now, Ui::Text::RichLangValue));
}

AdminLog::OwnedItem AboutView::makeAboutBot(not_null<BotInfo*> info) {
	return makeAboutSimple(
		TextUtilities::ParseEntities(
			info->description,
			Ui::ItemTextBotNoMonoOptions().flags),
		info->document,
		info->photo);
}

AdminLog::OwnedItem AboutView::makeAboutSimple(
		TextWithEntities textWithEntities,
		DocumentData *document,
		PhotoData *photo) {
	const auto make = [&](auto &&...args) {
		return _history->makeMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = (MessageFlag::FakeAboutView
				| MessageFlag::FakeHistoryItem
				| MessageFlag::Local),
			.from = _history->peer->id,
		}, std::forward<decltype(args)>(args)...);
	};
	const auto item = document
		? make(document, textWithEntities)
		: photo
		? make(photo, textWithEntities)
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
		std::make_unique<EmptyChatLockedBox>(
			result.get(),
			EmptyChatLockedBox::Type::PremiumRequired)));
	return result;
}

AdminLog::OwnedItem AboutView::makeStarsPerMessage(int stars) {
	auto name = Ui::Text::Bold(_history->peer->shortName());
	auto cost = Ui::Text::IconEmoji(
		&st::starIconEmoji
	).append(Ui::Text::Bold(Lang::FormatCountDecimal(stars)));
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{ !_history->peer->isMonoforum()
		? tr::lng_send_charges_stars_text(
			tr::now,
			lt_user,
			std::move(name),
			lt_amount,
			std::move(cost),
			Ui::Text::RichLangValue)
		: stars
		? tr::lng_send_charges_stars_channel(
			tr::now,
			lt_channel,
			std::move(name),
			lt_amount,
			std::move(cost),
			Ui::Text::RichLangValue)
		: tr::lng_send_free_channel(
			tr::now,
			lt_channel,
			std::move(name),
			Ui::Text::RichLangValue),
	});
	auto result = AdminLog::OwnedItem(_delegate, item);
	result->overrideMedia(std::make_unique<ServiceBox>(
		result.get(),
		std::make_unique<EmptyChatLockedBox>(
			result.get(),
			(stars
				? EmptyChatLockedBox::Type::StarsCharged
				: EmptyChatLockedBox::Type::FreeDirect))));
	return result;
}

AdminLog::OwnedItem AboutView::makeBlocked() {
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{
		{ tr::lng_chat_intro_default_title(tr::now) }
	});
	return AdminLog::OwnedItem(_delegate, item);
}

AdminLog::OwnedItem AboutView::makeNewBotThread() {
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{
		tr::lng_bot_new_thread_about(tr::now, Ui::Text::RichLangValue)
	});
	auto result = AdminLog::OwnedItem(_delegate, item);
	result->overrideMedia(std::make_unique<MediaGeneric>(
		result.get(),
		GenerateNewBotThread(result.get(), _item.get()),
		HistoryView::MediaGenericDescriptor{
			.maxWidth = st::chatIntroWidth,
			.service = true,
			.hideServiceText = true,
		}));
	return result;
}

} // namespace HistoryView
