/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_chat_links.h"

#include "api/api_chat_links.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "boxes/peers/edit_peer_invite_link.h"
#include "boxes/peers/edit_peer_invite_links.h"
#include "boxes/premium_preview_box.h"
#include "boxes/peer_list_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "core/core_settings.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/emoji_button.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

constexpr auto kChangesDebounceTimeout = crl::time(1000);

using ChatLinkData = Api::ChatLink;

class ChatLinks final : public BusinessSection<ChatLinks> {
public:
	ChatLinks(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~ChatLinks();

	[[nodiscard]] rpl::producer<QString> title() override;

	const Ui::RoundRect *bottomSkipRounding() const override {
		return &_bottomSkipRounding;
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);

	Ui::RoundRect _bottomSkipRounding;

};

struct ChatLinkAction {
	enum class Type {
		Copy,
		Share,
		Rename,
		Delete,
	};
	QString link;
	Type type = Type::Copy;
};

class Row;

class RowDelegate {
public:
	virtual not_null<Main::Session*> rowSession() = 0;
	virtual void rowUpdateRow(not_null<Row*> row) = 0;
	virtual void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size) = 0;
};

class Row final : public PeerListRow {
public:
	Row(not_null<RowDelegate*> delegate, const ChatLinkData &data);

	void update(const ChatLinkData &data);

	[[nodiscard]] ChatLinkData data() const;

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;
	bool rightActionDisabled() const override {
		return true;
	}

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;

private:
	void updateStatus(const ChatLinkData &data);

	const not_null<RowDelegate*> _delegate;
	ChatLinkData _data;
	Ui::Text::String _status;
	Ui::Text::String _clicks;

};

[[nodiscard]] uint64 ComputeRowId(const ChatLinkData &data) {
	return UniqueRowIdFromString(data.link);
}

[[nodiscard]] QString ComputeClicks(const ChatLinkData &link) {
	return link.clicks
		? tr::lng_chat_links_clicks(tr::now, lt_count, link.clicks)
		: tr::lng_chat_links_no_clicks(tr::now);
}

Row::Row(not_null<RowDelegate*> delegate, const ChatLinkData &data)
: PeerListRow(ComputeRowId(data))
, _delegate(delegate)
, _data(data) {
	setCustomStatus(QString());
	updateStatus(data);
}

void Row::updateStatus(const ChatLinkData &data) {
	const auto context = Core::MarkedTextContext{
		.session = _delegate->rowSession(),
		.customEmojiRepaint = [=] { _delegate->rowUpdateRow(this); },
	};
	_status.setMarkedText(
		st::messageTextStyle,
		data.message,
		kMarkupTextOptions,
		context);
	_clicks.setText(st::messageTextStyle, ComputeClicks(data));
}

void Row::update(const ChatLinkData &data) {
	_data = data;
	updateStatus(data);
	refreshName(st::inviteLinkList.item);
	_delegate->rowUpdateRow(this);
}

ChatLinkData Row::data() const {
	return _data;
}

QString Row::generateName() {
	if (!_data.title.isEmpty()) {
		return _data.title;
	}
	auto result = _data.link;
	return result.replace(
		u"https://"_q,
		QString()
	);
}

QString Row::generateShortName() {
	return generateName();
}

PaintRoundImageCallback Row::generatePaintUserpicCallback(bool forceRound) {
	return [=](
			QPainter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		_delegate->rowPaintIcon(p, x, y, size);
	};
}

QSize Row::rightActionSize() const {
	return QSize(
		_clicks.maxWidth(),
		st::inviteLinkThreeDotsIcon.height());
}

QMargins Row::rightActionMargins() const {
	return QMargins(
		0,
		(st::inviteLinkList.item.height - rightActionSize().height()) / 2,
		st::inviteLinkThreeDotsSkip,
		0);
}

void Row::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	p.setPen(selected ? st::windowSubTextFgOver : st::windowSubTextFg);
	_clicks.draw(p, x, y, outerWidth);
}

void Row::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	p.setPen(selected ? st.statusFgOver : st.statusFg);
	_status.draw(p, {
		.position = { x, y },
		.outerWidth = outerWidth,
		.availableWidth = availableWidth,
		.palette = &st::defaultTextPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = crl::now(),
		.elisionLines = 1,
	});
}

class LinksController final
	: public PeerListController
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	explicit LinksController(not_null<Window::SessionController*> window);

	[[nodiscard]] rpl::producer<int> fullCountValue() const {
		return _count.value();
	}

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	not_null<Main::Session*> rowSession() override;
	void rowUpdateRow(not_null<Row*> row) override;
	void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size) override;

private:
	void appendRow(const ChatLinkData &data);
	void prependRow(const ChatLinkData &data);
	void updateRow(const ChatLinkData &data);
	bool removeRow(const QString &link);

	void showRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow);

	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);

	const not_null<Window::SessionController*> _window;
	const not_null<Main::Session*> _session;
	rpl::variable<int> _count;
	base::unique_qptr<Ui::PopupMenu> _menu;

	QImage _icon;
	rpl::lifetime _lifetime;

};

struct LinksList {
	not_null<Ui::RpWidget*> widget;
	not_null<LinksController*> controller;
};

LinksList AddLinksList(
		not_null<Window::SessionController*> window,
		not_null<Ui::VerticalLayout*> container) {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<PeerListContentDelegateShow>(
		window->uiShow());
	const auto controller = lifetime.make_state<LinksController>(window);
	controller->setStyleOverrides(&st::inviteLinkList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	return { content, controller };
}

void EditChatLinkBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		ChatLinkData data,
		Fn<void(ChatLinkData, Fn<void()> close)> submit) {
	box->setTitle(data.link.isEmpty()
		? tr::lng_chat_link_new_title()
		: tr::lng_chat_link_edit_title());

	box->setWidth(st::boxWideWidth);

	Ui::AddDividerText(
		box->verticalLayout(),
		tr::lng_chat_link_description());

	const auto peer = controller->session().user();
	const auto outer = box->getDelegate()->outerContainer();
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box.get(),
			st::settingsChatLinkField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_chat_link_placeholder()));
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	Ui::AddDivider(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	const auto title = box->addRow(object_ptr<Ui::InputField>(
		box.get(),
		st::defaultInputField,
		tr::lng_chat_link_name(),
		data.title));

	const auto emojiToggle = Ui::CreateChild<Ui::EmojiButton>(
		field->parentWidget(),
		st::defaultComposeFiles.emoji);

	using Selector = ChatHelpers::TabbedSelector;
	auto &lifetime = box->lifetime();
	const auto emojiPanel = lifetime.make_state<ChatHelpers::TabbedPanel>(
		outer,
		controller,
		object_ptr<Selector>(
			nullptr,
			controller->uiShow(),
			Window::GifPauseReason::Layer,
			Selector::Mode::EmojiOnly));
	emojiPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	emojiPanel->hide();
	emojiPanel->selector()->setCurrentPeer(peer);
	emojiPanel->selector()->emojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
	}, field->lifetime());
	emojiPanel->selector()->customEmojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		Data::InsertCustomEmoji(field, data.document);
	}, field->lifetime());

	emojiToggle->installEventFilter(emojiPanel);
	emojiToggle->addClickHandler([=] {
		emojiPanel->toggleAnimated();
	});

	const auto allow = [](not_null<DocumentData*>) { return true; };
	InitMessageFieldHandlers(
		controller,
		field,
		Window::GifPauseReason::Layer,
		allow);
	Ui::Emoji::SuggestionsController::Init(
		outer,
		field,
		&controller->session(),
		{ .suggestCustomEmoji = true, .allowCustomWithoutPremium = allow });

	field->setSubmitSettings(Core::App().settings().sendSubmitWay());
	field->setMaxHeight(st::defaultComposeFiles.caption.heightMax);

	const auto save = [=] {
		auto copy = data;
		copy.title = title->getLastText().trimmed();
		auto textWithTags = field->getTextWithAppliedMarkdown();
		copy.message = TextWithEntities{
			textWithTags.text,
			TextUtilities::ConvertTextTagsToEntities(textWithTags.tags)
		};
		submit(copy, crl::guard(box, [=] {
			box->closeBox();
		}));
	};
	const auto updateEmojiPanelGeometry = [=] {
		const auto parent = emojiPanel->parentWidget();
		const auto global = emojiToggle->mapToGlobal({ 0, 0 });
		const auto local = parent->mapFromGlobal(global);
		emojiPanel->moveBottomRight(
			local.y(),
			local.x() + emojiToggle->width() * 3);
	};
	const auto filterCallback = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Move || type == QEvent::Resize) {
			// updateEmojiPanelGeometry uses not only container geometry, but
			// also container children geometries that will be updated later.
			crl::on_main(emojiPanel, updateEmojiPanelGeometry);
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(emojiPanel, outer, filterCallback);

	field->submits(
	) | rpl::start_with_next([=] {
		title->setFocus();
	}, field->lifetime());
	field->cancelled(
	) | rpl::start_with_next([=] {
		box->closeBox();
	}, field->lifetime());

	title->submits(
	) | rpl::start_with_next(save, title->lifetime());

	rpl::combine(
		box->sizeValue(),
		field->geometryValue()
	) | rpl::start_with_next([=](QSize outer, QRect inner) {
		emojiToggle->moveToLeft(
			inner.x() + inner.width() - emojiToggle->width(),
			inner.y() + st::settingsChatLinkEmojiTop);
		emojiToggle->update();
		crl::on_main(emojiPanel, updateEmojiPanelGeometry);
	}, emojiToggle->lifetime());

	const auto initial = TextWithTags{
		data.message.text,
		TextUtilities::ConvertEntitiesToTextTags(data.message.entities)
	};
	field->setTextWithTags(initial, Ui::InputField::HistoryAction::Clear);
	auto cursor = field->textCursor();
	cursor.movePosition(QTextCursor::End);
	field->setTextCursor(cursor);

	const auto checkChangedTimer = lifetime.make_state<base::Timer>([=] {
		if (field->getTextWithAppliedMarkdown() == initial) {
			box->setCloseByOutsideClick(true);
		}
	});
	field->changes(
	) | rpl::start_with_next([=] {
		checkChangedTimer->callOnce(kChangesDebounceTimeout);
		box->setCloseByOutsideClick(false);
	}, field->lifetime());

	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void EditChatLink(
		not_null<Window::SessionController*> window,
		not_null<Main::Session*> session,
		ChatLinkData data) {
	const auto submitting = std::make_shared<bool>();
	const auto submit = [=](ChatLinkData data, Fn<void()> close) {
		if (std::exchange(*submitting, true)) {
			return;
		}
		const auto done = crl::guard(window, [=](const auto&) {
			window->showToast(tr::lng_chat_link_saved(tr::now));
			close();
		});
		session->api().chatLinks().edit(
			data.link,
			data.title,
			data.message,
			done);
	};
	window->show(Box(
		EditChatLinkBox,
		window,
		data,
		crl::guard(window, submit)));
}

LinksController::LinksController(
	not_null<Window::SessionController*> window)
: _window(window)
, _session(&window->session()) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_icon = QImage();
	}, _lifetime);

	_session->api().chatLinks().updates(
	) | rpl::start_with_next([=](const Api::ChatLinkUpdate &update) {
		if (!update.now) {
			if (removeRow(update.was)) {
				delegate()->peerListRefreshRows();
			}
		} else if (update.was.isEmpty()) {
			prependRow(*update.now);
			delegate()->peerListRefreshRows();
		} else {
			updateRow(*update.now);
		}
	}, _lifetime);
}

void LinksController::prepare() {
	auto &&list = _session->api().chatLinks().list()
		| ranges::views::reverse;
	for (const auto &link : list) {
		appendRow(link);
	}
	delegate()->peerListRefreshRows();
}

void LinksController::rowClicked(not_null<PeerListRow*> row) {
	showRowMenu(row, true);
}

void LinksController::showRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow) {
	delegate()->peerListShowRowMenu(row, highlightRow);
}

void LinksController::rowRightActionClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, true);
}

base::unique_qptr<Ui::PopupMenu> LinksController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = createRowContextMenu(parent, row);

	if (result) {
		// First clear _menu value, so that we don't check row positions yet.
		base::take(_menu);

		// Here unique_qptr is used like a shared pointer, where
		// not the last destroyed pointer destroys the object, but the first.
		_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
	}

	return result;
}

base::unique_qptr<Ui::PopupMenu> LinksController::createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto real = static_cast<Row*>(row.get());
	const auto data = real->data();
	const auto link = data.link;
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	result->addAction(tr::lng_group_invite_context_copy(tr::now), [=] {
		QGuiApplication::clipboard()->setText(link);
		delegate()->peerListUiShow()->showToast(
			tr::lng_chat_link_copied(tr::now));
	}, &st::menuIconCopy);
	result->addAction(tr::lng_group_invite_context_share(tr::now), [=] {
		delegate()->peerListUiShow()->showBox(ShareInviteLinkBox(
			_session,
			link,
			tr::lng_chat_link_copied(tr::now)));
	}, &st::menuIconShare);
	result->addAction(tr::lng_group_invite_context_qr(tr::now), [=] {
		delegate()->peerListUiShow()->showBox(InviteLinkQrBox(
			link,
			tr::lng_chat_link_qr_title(),
			tr::lng_chat_link_qr_about()));
	}, &st::menuIconQrCode);
	result->addAction(tr::lng_group_invite_context_edit(tr::now), [=] {
		EditChatLink(_window, _session, data);
	}, &st::menuIconEdit);
	result->addAction(tr::lng_group_invite_context_delete(tr::now), [=] {
		const auto sure = [=](Fn<void()> &&close) {
			_window->session().api().chatLinks().destroy(link, close);
		};
		_window->show(Ui::MakeConfirmBox({
			.text = tr::lng_chat_link_delete_sure(tr::now),
			.confirmed = sure,
			.confirmText = tr::lng_box_delete(tr::now),
		}));
	}, &st::menuIconDelete);
	return result;
}

Main::Session &LinksController::session() const {
	return *_session;
}

void LinksController::appendRow(const ChatLinkData &data) {
	delegate()->peerListAppendRow(std::make_unique<Row>(this, data));
	_count = _count.current() + 1;
}

void LinksController::prependRow(const ChatLinkData &data) {
	delegate()->peerListPrependRow(std::make_unique<Row>(this, data));
	_count = _count.current() + 1;
}

void LinksController::updateRow(const ChatLinkData &data) {
	if (const auto row = delegate()->peerListFindRow(ComputeRowId(data))) {
		const auto real = static_cast<Row*>(row);
		real->update(data);
		delegate()->peerListUpdateRow(row);
	}
}

bool LinksController::removeRow(const QString &link) {
	const auto id = UniqueRowIdFromString(link);
	if (const auto row = delegate()->peerListFindRow(id)) {
		delegate()->peerListRemoveRow(row);
		_count = std::max(_count.current() - 1, 0);
		return true;
	}
	return false;
}

not_null<Main::Session*> LinksController::rowSession() {
	return _session;
}

void LinksController::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void LinksController::rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size) {
	const auto skip = st::inviteLinkIconSkip;
	const auto inner = size - 2 * skip;
	const auto bg = &st::msgFile1Bg;
	if (_icon.isNull()) {
		_icon = QImage(
			QSize(inner, inner) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		_icon.fill(Qt::transparent);
		_icon.setDevicePixelRatio(style::DevicePixelRatio());

		auto p = QPainter(&_icon);
		p.setPen(Qt::NoPen);
		p.setBrush(*bg);
		{
			auto hq = PainterHighQualityEnabler(p);
			auto rect = QRect(0, 0, inner, inner);
			p.drawEllipse(rect);
		}
		st::inviteLinkIcon.paintInCenter(p, { 0, 0, inner, inner });
	}
	p.drawImage(x + skip, y + skip, _icon);
}

ChatLinks::ChatLinks(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

ChatLinks::~ChatLinks() = default;

rpl::producer<QString> ChatLinks::title() {
	return tr::lng_chat_links_title();
}

void ChatLinks::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddDividerTextWithLottie(content, {
		.lottie = u"chat_link"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes() | rpl::take(1),
		.about = tr::lng_chat_links_about(Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	Ui::AddSkip(content);

	const auto limit = controller->session().appConfig().get<int>(
		u"business_chat_links_limit"_q,
		100);
	const auto add = content->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			content,
			MakeCreateLinkButton(
				content,
				tr::lng_chat_links_create_link()))
	)->setDuration(0);

	const auto list = AddLinksList(controller, content);
	add->toggleOn(list.controller->fullCountValue() | rpl::map(_1 < limit));
	add->finishAnimating();

	add->entity()->setClickedCallback([=] {
		if (!controller->session().premium()) {
			ShowPremiumPreviewToBuy(
				controller,
				PremiumFeature::ChatLinks);
			return;
		}
		const auto submitting = std::make_shared<bool>();
		const auto submit = [=](ChatLinkData data, Fn<void()> close) {
			if (std::exchange(*submitting, true)) {
				return;
			}
			const auto done = [=](const auto&) {
				controller->showToast(tr::lng_chat_link_saved(tr::now));
				close();
			};
			controller->session().api().chatLinks().create(
				data.title,
				data.message,
				done);
		};
		controller->show(Box(
			EditChatLinkBox,
			controller,
			ChatLinkData(),
			crl::guard(this, submit)));
	});

	Ui::AddSkip(content);

	const auto self = controller->session().user();
	const auto username = self->username();
	const auto make = [&](std::vector<QString> links) {
		Expects(!links.empty());

		for (auto &link : links) {
			link = controller->session().createInternalLink(link);
		}
		return (links.size() > 1)
			? tr::lng_chat_links_footer_both(
				tr::now,
				lt_username,
				Ui::Text::Link(links[0], "https://" + links[0]),
				lt_link,
				Ui::Text::Link(links[1], "https://" + links[1]),
				Ui::Text::WithEntities)
			: Ui::Text::Link(links[0], "https://" + links[0]);
	};
	auto links = !username.isEmpty()
		? make({ username, '+' + self->phone() })
		: make({ '+' + self->phone() });
	auto label = object_ptr<Ui::FlatLabel>(
		content,
		tr::lng_chat_links_footer(
			lt_links,
			rpl::single(std::move(links)),
			Ui::Text::WithEntities),
		st::boxDividerLabel);
	label->setClickHandlerFilter([=](ClickHandlerPtr handler, auto) {
		QGuiApplication::clipboard()->setText(handler->url());
		controller->showToast(tr::lng_chat_link_copied(tr::now));
		return false;
	});
	content->add(object_ptr<Ui::DividerLabel>(
		content,
		std::move(label),
		st::settingsChatbotsBottomTextMargin,
		RectPart::Top));

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type ChatLinksId() {
	return ChatLinks::Id();
}

} // namespace Settings
