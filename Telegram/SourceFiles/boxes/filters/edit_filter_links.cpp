/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_links.h"

#include "apiwrap.h"
#include "boxes/peers/edit_peer_invite_link.h" // InviteLinkQrBox.
#include "boxes/peer_list_box.h"
#include "boxes/premium_limits_box.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/controls/invite_link_label.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <xxhash.h>

namespace {

constexpr auto kMaxLinkTitleLength = 32;

using InviteLinkData = Data::ChatFilterLink;
class LinkRow;

enum class Color {
	Permanent,

	Count,
};

struct InviteLinkAction {
	enum class Type {
		Copy,
		Share,
		Edit,
		Delete,
	};
	QString link;
	Type type = Type::Copy;
};

struct Errors {
	QString status;
	QString toast;
};

[[nodiscard]] std::optional<Errors> ErrorForSharing(
		not_null<History*> history) {
	const auto result = [](const QString &status, const QString &toast) {
		return Errors{ status, toast };
	};
	const auto peer = history->peer;
	if (const auto user = peer->asUser()) {
		return user->isBot()
			? result(
				tr::lng_filters_link_bot_status(tr::now),
				tr::lng_filters_link_bot_error(tr::now))
			: result(
				tr::lng_filters_link_private_status(tr::now),
				tr::lng_filters_link_private_error(tr::now));
	} else if (const auto chat = history->peer->asChat()) {
		if (!chat->canHaveInviteLink()) {
			return result(
				tr::lng_filters_link_noadmin_status(tr::now),
				tr::lng_filters_link_noadmin_group_error(tr::now));
		}
		return std::nullopt;
	} else if (const auto channel = history->peer->asChannel()) {
		if (!channel->canHaveInviteLink()
			&& (!channel->hasUsername() || channel->requestToJoin())) {
			return result(
				tr::lng_filters_link_noadmin_status(tr::now),
				(channel->isMegagroup()
					? tr::lng_filters_link_noadmin_group_error(tr::now)
					: tr::lng_filters_link_noadmin_channel_error(tr::now)));
		}
		return std::nullopt;
	}
	Unexpected("Peer type in ErrorForSharing.");
}

void ShowSaveError(
		not_null<Window::SessionController*> window,
		QString error) {
	const auto session = &window->session();
	if (error == u"CHATLISTS_TOO_MUCH"_q) {
		window->show(Box(ShareableFiltersLimitBox, session));
	} else if (error == u"INVITES_TOO_MUCH"_q) {
		window->show(Box(FilterLinksLimitBox, session));
	} else if (error == u"CHANNELS_TOO_MUCH"_q) {
		window->show(Box(ChannelsLimitBox, session));
	} else if (error == u"USER_CHANNELS_TOO_MUCH"_q) {
		window->showToast(
			{ tr::lng_filters_link_group_admin_error(tr::now) });
	} else {
		window->showToast(error);
	}
}

void ShowEmptyLinkError(not_null<Window::SessionController*> window) {
	ShowSaveError(window, tr::lng_filters_empty(tr::now));
}

void ChatFilterLinkBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		Data::ChatFilterLink data) {
	using namespace rpl::mappers;

	const auto link = data.url;
	box->setTitle(tr::lng_group_invite_edit_title());

	const auto container = box->verticalLayout();
	const auto labelField = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_group_invite_label_header(),
			data.title),
		style::margins(
			st::settingsSubsectionTitlePadding.left(),
			st::settingsSectionSkip,
			st::settingsSubsectionTitlePadding.right(),
			st::settingsSectionSkip * 2));
	labelField->setMaxLength(kMaxLinkTitleLength);
	Settings::AddDivider(container);

	box->setFocusCallback([=] {
		labelField->setFocusFast();
	});

	const auto &saveLabel = link.isEmpty()
		? tr::lng_formatting_link_create
		: tr::lng_settings_save;
	box->addButton(saveLabel(), [=] {
		session->data().chatsFilters().edit(
			data.id,
			data.url,
			labelField->getLastText().trimmed());
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

class LinkRowDelegate {
public:
	virtual void rowUpdateRow(not_null<LinkRow*> row) = 0;
	virtual void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		Color color) = 0;
};

class LinkRow final : public PeerListRow {
public:
	LinkRow(not_null<LinkRowDelegate*> delegate, const InviteLinkData &data);

	void update(const InviteLinkData &data);

	[[nodiscard]] InviteLinkData data() const;

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

private:
	const not_null<LinkRowDelegate*> _delegate;
	InviteLinkData _data;
	QString _status;
	Color _color = Color::Permanent;

};

class ChatRow final : public PeerListRow {
public:
	ChatRow(not_null<PeerData*> peer, const QString &status, bool disabled);

	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

private:
	const bool _disabled = false;
	QImage _disabledFrame;
	InMemoryKey _userpicKey;
	int _paletteVersion = 0;

};

[[nodiscard]] uint64 ComputeRowId(const QString &link) {
	return XXH64(link.data(), link.size() * sizeof(ushort), 0);
}

[[nodiscard]] uint64 ComputeRowId(const InviteLinkData &data) {
	return ComputeRowId(data.url);
}

[[nodiscard]] Color ComputeColor(const InviteLinkData &link) {
	return Color::Permanent;
}

[[nodiscard]] QString ComputeStatus(const InviteLinkData &link) {
	return tr::lng_filters_chats_count(tr::now, lt_count, link.chats.size());
}

LinkRow::LinkRow(
	not_null<LinkRowDelegate*> delegate,
	const InviteLinkData &data)
: PeerListRow(ComputeRowId(data))
, _delegate(delegate)
, _data(data)
, _color(ComputeColor(data)) {
	setCustomStatus(ComputeStatus(data));
}

void LinkRow::update(const InviteLinkData &data) {
	_data = data;
	_color = ComputeColor(data);
	setCustomStatus(ComputeStatus(data));
	refreshName(st::inviteLinkList.item);
	_delegate->rowUpdateRow(this);
}

InviteLinkData LinkRow::data() const {
	return _data;
}

QString LinkRow::generateName() {
	if (!_data.title.isEmpty()) {
		return _data.title;
	}
	auto result = _data.url;
	return result.replace(
		u"https://"_q,
		QString()
	).replace(
		u"t.me/+"_q,
		QString()
	).replace(
		u"t.me/joinchat/"_q,
		QString()
	);
}

QString LinkRow::generateShortName() {
	return generateName();
}

PaintRoundImageCallback LinkRow::generatePaintUserpicCallback(
		bool forceRound) {
	return [=](
			QPainter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		_delegate->rowPaintIcon(p, x, y, size, _color);
	};
}

QSize LinkRow::rightActionSize() const {
	return QSize(
		st::inviteLinkThreeDotsIcon.width(),
		st::inviteLinkThreeDotsIcon.height());
}

QMargins LinkRow::rightActionMargins() const {
	return QMargins(
		0,
		(st::inviteLinkList.item.height - rightActionSize().height()) / 2,
		st::inviteLinkThreeDotsSkip,
		0);
}

void LinkRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	(actionSelected
		? st::inviteLinkThreeDotsIconOver
		: st::inviteLinkThreeDotsIcon).paint(p, x, y, outerWidth);
}

ChatRow::ChatRow(
	not_null<PeerData*> peer,
	const QString &status,
	bool disabled)
: PeerListRow(peer)
, _disabled(disabled) {
	if (!status.isEmpty()) {
		setCustomStatus(status);
	}
}

PaintRoundImageCallback ChatRow::generatePaintUserpicCallback(
		bool forceRound) {
	const auto peer = this->peer();
	const auto saved = peer->isSelf();
	const auto replies = peer->isRepliesChat();
	auto userpic = (saved || replies)
		? Ui::PeerUserpicView()
		: ensureUserpicView();
	auto paint = [=](
			Painter &p,
			int x,
			int y,
			int outerWidth,
			int size) mutable {
		if (forceRound && peer->isForum()) {
			ForceRoundUserpicCallback(peer)(p, x, y, outerWidth, size);
		} else if (saved) {
			Ui::EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, size);
		} else if (replies) {
			Ui::EmptyUserpic::PaintRepliesMessages(p, x, y, outerWidth, size);
		} else {
			peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
		}
	};
	if (!_disabled) {
		return paint;
	}
	return [=](
			Painter &p,
			int x,
			int y,
			int outerWidth,
			int size) mutable {
		const auto wide = size + style::ConvertScale(3);
		const auto full = QSize(wide, wide) * style::DevicePixelRatio();
		auto repaint = false;
		if (_disabledFrame.size() != full) {
			repaint = true;
			_disabledFrame = QImage(
				full,
				QImage::Format_ARGB32_Premultiplied);
			_disabledFrame.setDevicePixelRatio(style::DevicePixelRatio());
		} else {
			repaint = (_paletteVersion != style::PaletteVersion())
				|| (!saved
					&& !replies
					&& (_userpicKey != peer->userpicUniqueKey(userpic)));
		}
		if (repaint) {
			_paletteVersion = style::PaletteVersion();
			_userpicKey = peer->userpicUniqueKey(userpic);

			_disabledFrame.fill(Qt::transparent);
			auto p = Painter(&_disabledFrame);
			paint(p, 0, 0, wide, size);

			auto hq = PainterHighQualityEnabler(p);
			p.setBrush(st::boxBg);
			p.setPen(Qt::NoPen);
			const auto two = style::ConvertScaleExact(2.5);
			const auto half = size / 2.;
			const auto rect = QRectF(half, half, half, half).translated(
				{ two, two });
			p.drawEllipse(rect);

			auto pen = st::windowSubTextFg->p;
			const auto width = style::ConvertScaleExact(1.5);
			const auto dash = 0.55;
			const auto dashWithCaps = dash + 1.;
			pen.setWidthF(width);
			// 11 parts = M_PI * half / ((dashWithCaps + space) * width)
			// 11 = M_PI * half / ((dashWithCaps + space) * width)
			// space = (M_PI * half / (11 * width)) - dashWithCaps
			const auto space = M_PI * half / (11 * width) - dashWithCaps;
			pen.setDashPattern(QVector<qreal>{ dash, space });
			pen.setDashOffset(1.);
			pen.setCapStyle(Qt::RoundCap);
			p.setBrush(Qt::NoBrush);
			p.setPen(pen);
			p.drawEllipse(rect.marginsRemoved({ two, two, two, two }));
		}
		p.drawImage(x, y, _disabledFrame);
	};
}

class LinksController final
	: public PeerListController
	, public LinkRowDelegate
	, public base::has_weak_ptr {
public:
	LinksController(
		not_null<Window::SessionController*> window,
		rpl::producer<std::vector<InviteLinkData>> content,
		Fn<Data::ChatFilter()> currentFilter);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void rowUpdateRow(not_null<LinkRow*> row) override;
	void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		Color color) override;

private:
	void appendRow(const InviteLinkData &data);

	void rebuild(const std::vector<InviteLinkData> &rows);

	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);

	const not_null<Window::SessionController*> _window;
	Fn<Data::ChatFilter()> _currentFilter;
	rpl::variable<std::vector<InviteLinkData>> _rows;
	base::unique_qptr<Ui::PopupMenu> _menu;

	std::array<QImage, int(Color::Count)> _icons;
	rpl::lifetime _lifetime;

};

class LinkController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	LinkController(
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		InviteLinkData data);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void showFinished() override;

	[[nodiscard]] rpl::producer<bool> hasChangesValue() const;
	[[nodiscard]] base::flat_set<not_null<PeerData*>> selected() const;

private:
	void setupAboveWidget();
	void setupBelowWidget();
	void addHeader(not_null<Ui::VerticalLayout*> container);
	void addLinkBlock(not_null<Ui::VerticalLayout*> container);
	void toggleAllSelected(bool select);

	const not_null<Window::SessionController*> _window;
	InviteLinkData _data;

	QString _filterTitle;
	base::flat_set<not_null<History*>> _filterChats;
	base::flat_map<not_null<PeerData*>, QString> _denied;
	rpl::variable<base::flat_set<not_null<PeerData*>>> _selected;
	base::flat_set<not_null<PeerData*>> _initial;

	base::unique_qptr<Ui::PopupMenu> _menu;

	QString _link;

	rpl::variable<bool> _hasChanges = false;

	rpl::event_stream<> _showFinished;

	rpl::lifetime _lifetime;

};

LinkController::LinkController(
	not_null<Window::SessionController*> window,
	const Data::ChatFilter &filter,
	InviteLinkData data)
: _window(window)
, _filterTitle(filter.title())
, _filterChats(filter.always()) {
	_data = std::move(data);
	_link = _data.url;
}

void LinkController::addHeader(not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(
		container.get());
	const auto verticalLayout = container->add(
		object_ptr<Ui::VerticalLayout>(container.get()));

	auto icon = CreateLottieIcon(
		verticalLayout,
		{
			.name = u"cloud_filters"_q,
			.sizeOverride = {
				st::settingsFilterIconSize,
				st::settingsFilterIconSize,
			},
		},
		st::settingsFilterIconPadding);
	_showFinished.events(
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, verticalLayout->lifetime());
	verticalLayout->add(std::move(icon.widget));

	verticalLayout->add(
		object_ptr<Ui::CenterWrap<>>(
			verticalLayout,
			object_ptr<Ui::FlatLabel>(
				verticalLayout,
				(_data.url.isEmpty()
					? tr::lng_filters_link_no_about(Ui::Text::WithEntities)
					: tr::lng_filters_link_share_about(
						lt_folder,
						rpl::single(Ui::Text::Bold(_filterTitle)),
						Ui::Text::WithEntities)),
				st::settingsFilterDividerLabel)),
		st::filterLinkDividerLabelPadding);

	verticalLayout->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());
}

object_ptr<Ui::BoxContent> DeleteLinkBox(
		not_null<Window::SessionController*> window,
		const InviteLinkData &link) {
	const auto sure = [=](Fn<void()> &&close) {
		window->session().data().chatsFilters().destroy(link.id, link.url);
		close();
	};
	return Ui::MakeConfirmBox({
		.text = tr::lng_filters_link_delete_sure(tr::now),
		.confirmed = sure,
		.confirmText = tr::lng_box_delete(tr::now),
	});
}

void LinkController::addLinkBlock(not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto link = _data.url;
	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		CopyInviteLink(delegate()->peerListUiShow(), link);
	});
	const auto shareLink = crl::guard(weak, [=] {
		delegate()->peerListShowBox(
			ShareInviteLinkBox(&_window->session(), link));
	});
	const auto getLinkQr = crl::guard(weak, [=] {
		delegate()->peerListShowBox(
			InviteLinkQrBox(link, tr::lng_filters_link_qr_about()));
	});
	const auto editLink = crl::guard(weak, [=] {
		delegate()->peerListShowBox(
			Box(ChatFilterLinkBox, &_window->session(), _data));
	});
	const auto deleteLink = crl::guard(weak, [=] {
		delegate()->peerListShowBox(DeleteLinkBox(_window, _data));
	});

	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(
			container,
			st::popupMenuWithIcons);
		result->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			copyLink,
			&st::menuIconCopy);
		result->addAction(
			tr::lng_group_invite_context_share(tr::now),
			shareLink,
			&st::menuIconShare);
		result->addAction(
			tr::lng_group_invite_context_qr(tr::now),
			getLinkQr,
			&st::menuIconQrCode);
		result->addAction(
			tr::lng_filters_link_name_it(tr::now),
			editLink,
			&st::menuIconEdit);
		result->addAction(
			tr::lng_group_invite_context_delete(tr::now),
			deleteLink,
			&st::menuIconDelete);
		return result;
	};
	AddSubsectionTitle(
		container,
		tr::lng_filters_link_subtitle(),
		st::filterLinkSubsectionTitlePadding);

	const auto prefix = u"https://"_q;
	const auto label = container->lifetime().make_state<Ui::InviteLinkLabel>(
		container,
		rpl::single(link.startsWith(prefix)
			? link.mid(prefix.size())
			: link),
		createMenu);
	container->add(
		label->take(),
		st::inviteLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());

	AddCopyShareLinkButtons(container, copyLink, shareLink);

	AddSkip(container, st::inviteLinkJoinedRowPadding.bottom() * 2);

	AddSkip(container);

	AddDivider(container);
}

void LinkController::prepare() {
	Expects(!_data.url.isEmpty() || _data.chats.empty());

	for (const auto &history : _data.chats) {
		const auto peer = history->peer;
		auto row = std::make_unique<ChatRow>(
			peer,
			FilterChatStatusText(peer),
			false);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		delegate()->peerListSetRowChecked(raw, true);
		raw->finishCheckedAnimation();
		_initial.emplace(peer);
	}
	for (const auto &history : _filterChats) {
		if (delegate()->peerListFindRow(history->peer->id.value)) {
			continue;
		}
		const auto peer = history->peer;
		const auto error = ErrorForSharing(history);
		auto row = std::make_unique<ChatRow>(
			peer,
			error ? error->status : FilterChatStatusText(peer),
			error.has_value());
		delegate()->peerListAppendRow(std::move(row));
		if (error) {
			_denied.emplace(peer, error->toast);
		} else if (_data.url.isEmpty()) {
			_denied.emplace(peer);
		}
	}
	setupAboveWidget();
	setupBelowWidget();
	delegate()->peerListRefreshRows();
	_selected = _initial;
}

void LinkController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	if (const auto i = _denied.find(peer); i != end(_denied)) {
		if (!i->second.isEmpty()) {
			delegate()->peerListUiShow()->showToast(i->second);
		}
	} else {
		const auto checked = row->checked();
		auto selected = _selected.current();
		delegate()->peerListSetRowChecked(row, !checked);
		if (checked) {
			selected.remove(peer);
		} else {
			selected.emplace(peer);
		}
		const auto has = (_initial != selected);
		_selected = std::move(selected);
		_hasChanges = has;
	}
}

void LinkController::toggleAllSelected(bool select) {
	auto selected = _selected.current();
	if (!select) {
		if (selected.empty()) {
			return;
		}
		for (const auto &peer : selected) {
			const auto row = delegate()->peerListFindRow(peer->id.value);
			Assert(row != nullptr);
			delegate()->peerListSetRowChecked(row, false);
		}
		selected = {};
	} else {
		const auto count = delegate()->peerListFullRowsCount();
		for (auto i = 0; i != count; ++i) {
			const auto row = delegate()->peerListRowAt(i);
			const auto peer = row->peer();
			if (!_denied.contains(peer)) {
				delegate()->peerListSetRowChecked(row, true);
				selected.emplace(peer);
			}
		}
	}
	const auto has = (_initial != selected);
	_selected = std::move(selected);
	_hasChanges = has;
}

void LinkController::showFinished() {
	_showFinished.fire({});
}

void LinkController::setupAboveWidget() {
	using namespace Settings;

	auto wrap = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = wrap.data();

	addHeader(container);
	if (!_data.url.isEmpty()) {
		addLinkBlock(container);
	}

	auto subtitle = _selected.value(
	) | rpl::map([=](const base::flat_set<not_null<PeerData*>> &selected) {
		return _data.url.isEmpty()
			? tr::lng_filters_link_chats_no(tr::now)
			: selected.empty()
			? tr::lng_filters_link_chats_none(tr::now)
			: tr::lng_filters_link_chats(
				tr::now,
				lt_count,
				float64(selected.size()));
	});
	const auto mayBeSelected = delegate()->peerListFullRowsCount()
		- int(_denied.size());
	auto selectedCount = _selected.value(
	) | rpl::map([](const base::flat_set<not_null<PeerData*>> &selected) {
		return int(selected.size());
	});
	AddFilterSubtitleWithToggles(
		container,
		std::move(subtitle),
		mayBeSelected,
		std::move(selectedCount),
		[=](bool select) { toggleAllSelected(select); });

	// Fix label cutting on text change from smaller to longer.
	_selected.changes() | rpl::start_with_next([=] {
		container->resizeToWidth(container->widthNoMargins());
	}, container->lifetime());

	delegate()->peerListSetAboveWidget(std::move(wrap));
}

void LinkController::setupBelowWidget() {
	delegate()->peerListSetBelowWidget(
		object_ptr<Ui::DividerLabel>(
			(QWidget*)nullptr,
			object_ptr<Ui::FlatLabel>(
				(QWidget*)nullptr,
				(_data.url.isEmpty()
					? tr::lng_filters_link_chats_no_about()
					: tr::lng_filters_link_chats_about()),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding));
}

Main::Session &LinkController::session() const {
	return _window->session();
}

rpl::producer<bool> LinkController::hasChangesValue() const {
	return _hasChanges.value();
}

base::flat_set<not_null<PeerData*>> LinkController::selected() const {
	return _selected.current();
}

LinksController::LinksController(
	not_null<Window::SessionController*> window,
	rpl::producer<std::vector<InviteLinkData>> content,
	Fn<Data::ChatFilter()> currentFilter)
: _window(window)
, _currentFilter(std::move(currentFilter))
, _rows(std::move(content)) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &image : _icons) {
			image = QImage();
		}
	}, _lifetime);
}

void LinksController::prepare() {
	_rows.value(
	) | rpl::start_with_next([=](const std::vector<InviteLinkData> &rows) {
		rebuild(rows);
	}, _lifetime);
}

void LinksController::rebuild(const std::vector<InviteLinkData> &rows) {
	auto i = 0;
	auto count = delegate()->peerListFullRowsCount();
	while (i < rows.size()) {
		if (i < count) {
			const auto row = delegate()->peerListRowAt(i);
			static_cast<LinkRow*>(row.get())->update(rows[i]);
		} else {
			appendRow(rows[i]);
		}
		++i;
	}
	while (i < count) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(i));
		--count;
	}
	delegate()->peerListRefreshRows();
}

void LinksController::rowClicked(not_null<PeerListRow*> row) {
	const auto link = static_cast<LinkRow*>(row.get())->data();
	delegate()->peerListShowBox(
		ShowLinkBox(_window, _currentFilter(), link));
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
	const auto real = static_cast<LinkRow*>(row.get());
	const auto data = real->data();
	const auto link = data.url;
	const auto copyLink = [=] {
		CopyInviteLink(delegate()->peerListUiShow(), link);
	};
	const auto shareLink = [=] {
		delegate()->peerListShowBox(
			ShareInviteLinkBox(&_window->session(), link));
	};
	const auto getLinkQr = [=] {
		delegate()->peerListShowBox(
			InviteLinkQrBox(link, tr::lng_filters_link_qr_about()));
	};
	const auto editLink = [=] {
		delegate()->peerListShowBox(
			Box(ChatFilterLinkBox, &_window->session(), data));
	};
	const auto deleteLink = [=] {
		delegate()->peerListShowBox(DeleteLinkBox(_window, data));
	};
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	result->addAction(
		tr::lng_group_invite_context_copy(tr::now),
		copyLink,
		&st::menuIconCopy);
	result->addAction(
		tr::lng_group_invite_context_share(tr::now),
		shareLink,
		&st::menuIconShare);
	result->addAction(
		tr::lng_group_invite_context_qr(tr::now),
		getLinkQr,
		&st::menuIconQrCode);
	result->addAction(
		tr::lng_filters_link_name_it(tr::now),
		editLink,
		&st::menuIconEdit);
	result->addAction(
		tr::lng_group_invite_context_delete(tr::now),
		deleteLink,
		&st::menuIconDelete);
	return result;
}

Main::Session &LinksController::session() const {
	return _window->session();
}

void LinksController::appendRow(const InviteLinkData &data) {
	delegate()->peerListAppendRow(std::make_unique<LinkRow>(this, data));
}

void LinksController::rowUpdateRow(not_null<LinkRow*> row) {
	delegate()->peerListUpdateRow(row);
}

void LinksController::rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		Color color) {
	const auto skip = st::inviteLinkIconSkip;
	const auto inner = size - 2 * skip;
	const auto bg = [&] {
		switch (color) {
		case Color::Permanent: return &st::msgFile1Bg;
		}
		Unexpected("Color in LinksController::rowPaintIcon.");
	}();
	auto &icon = _icons[int(color)];
	if (icon.isNull()) {
		icon = QImage(
			QSize(inner, inner) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		icon.fill(Qt::transparent);
		icon.setDevicePixelRatio(style::DevicePixelRatio());

		auto p = QPainter(&icon);
		p.setPen(Qt::NoPen);
		p.setBrush(*bg);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.drawEllipse(QRect(0, 0, inner, inner));
		}
		st::inviteLinkIcon.paintInCenter(p, { 0, 0, inner, inner });
	}
	p.drawImage(x + skip, y + skip, icon);
}

} // namespace

std::vector<not_null<PeerData*>> CollectFilterLinkChats(
		const Data::ChatFilter &filter) {
	return filter.always() | ranges::views::filter([](
			not_null<History*> history) {
		return !ErrorForSharing(history);
	}) | ranges::views::transform(&History::peer) | ranges::to_vector;
}

bool GoodForExportFilterLink(
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter) {
	using Flag = Data::ChatFilter::Flag;
	const auto listflags = Flag::Chatlist | Flag::HasMyLinks;
	if (!filter.never().empty() || (filter.flags() & ~listflags)) {
		window->showToast(tr::lng_filters_link_cant(tr::now));
		return false;
	}
	return true;
}

void ExportFilterLink(
		FilterId id,
		const std::vector<not_null<PeerData*>> &peers,
		Fn<void(Data::ChatFilterLink)> done,
		Fn<void(QString)> fail) {
	Expects(!peers.empty());

	const auto front = peers.front();
	const auto session = &front->session();
	auto mtpPeers = peers | ranges::views::transform(
		[](not_null<PeerData*> peer) { return MTPInputPeer(peer->input); }
	) | ranges::to<QVector<MTPInputPeer>>();
	session->api().request(MTPchatlists_ExportChatlistInvite(
		MTP_inputChatlistDialogFilter(MTP_int(id)),
		MTP_string(), // title
		MTP_vector<MTPInputPeer>(std::move(mtpPeers))
	)).done([=](const MTPchatlists_ExportedChatlistInvite &result) {
		const auto &data = result.data();
		session->data().chatsFilters().apply(MTP_updateDialogFilter(
			MTP_flags(MTPDupdateDialogFilter::Flag::f_filter),
			MTP_int(id),
			data.vfilter()));
		const auto link = session->data().chatsFilters().add(
			id,
			data.vinvite());
		done(link);
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

void EditLinkChats(
		const Data::ChatFilterLink &link,
		base::flat_set<not_null<PeerData*>> peers,
		Fn<void(QString)> done) {
	Expects(!peers.empty());
	Expects(link.id != 0);
	Expects(!link.url.isEmpty());

	const auto id = link.id;
	const auto front = peers.front();
	const auto session = &front->session();
	auto mtpPeers = peers | ranges::views::transform(
		[](not_null<PeerData*> peer) { return MTPInputPeer(peer->input); }
	) | ranges::to<QVector<MTPInputPeer>>();
	session->api().request(MTPchatlists_EditExportedInvite(
		MTP_flags(MTPchatlists_EditExportedInvite::Flag::f_peers),
		MTP_inputChatlistDialogFilter(MTP_int(link.id)),
		MTP_string(link.url),
		MTPstring(), // title
		MTP_vector<MTPInputPeer>(std::move(mtpPeers))
	)).done([=](const MTPExportedChatlistInvite &result) {
		const auto link = session->data().chatsFilters().add(id, result);
		done(QString());
	}).fail([=](const MTP::Error &error) {
		done(error.type());
	}).send();
}

object_ptr<Ui::BoxContent> ShowLinkBox(
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		const Data::ChatFilterLink &link) {
	auto controller = std::make_unique<LinkController>(window, filter, link);
	controller->setStyleOverrides(&st::inviteLinkChatList);
	const auto raw = controller.get();
	auto initBox = [=](not_null<Ui::BoxContent*> box) {
		box->setTitle(!link.title.isEmpty()
			? rpl::single(link.title)
			: tr::lng_filters_link_title());

		const auto saving = std::make_shared<bool>(false);
		raw->hasChangesValue(
		) | rpl::start_with_next([=](bool has) {
			box->setCloseByOutsideClick(!has);
			box->setCloseByEscape(!has);
			box->clearButtons();
			if (has) {
				box->addButton(tr::lng_settings_save(), [=] {
					if (*saving) {
						return;
					}
					const auto chosen = raw->selected();
					if (chosen.empty()) {
						ShowEmptyLinkError(window);
					} else {
						*saving = true;
						EditLinkChats(link, chosen, crl::guard(box, [=](
								QString error) {
							*saving = false;
							if (error.isEmpty()) {
								box->closeBox();
							} else {
								ShowSaveError(window, error);
							}
						}));
					}
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			} else {
				box->addButton(tr::lng_about_done(), [=] {
					box->closeBox();
				});
			}
		}, box->lifetime());
	};
	return Box<PeerListBox>(std::move(controller), std::move(initBox));
}

QString FilterChatStatusText(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		if (const auto count = chat->count; count > 0) {
			return tr::lng_chat_status_members(tr::now, lt_count, count);
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->membersCountKnown()) {
			return (channel->isBroadcast()
				? tr::lng_chat_status_subscribers
				: tr::lng_chat_status_members)(
					tr::now,
					lt_count,
					channel->membersCount());
		}
	}
	return QString();
}

void SetupFilterLinks(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> window,
		rpl::producer<std::vector<Data::ChatFilterLink>> value,
		Fn<Data::ChatFilter()> currentFilter) {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<PeerListContentDelegateShow>(
		window->uiShow());
	const auto controller = lifetime.make_state<LinksController>(
		window,
		std::move(value),
		std::move(currentFilter));
	controller->setStyleOverrides(&st::inviteLinkList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);
}

void AddFilterSubtitleWithToggles(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		int selectableCount,
		rpl::producer<int> selectedCount,
		Fn<void(bool select)> toggle) {
	using namespace rpl::mappers;

	const auto selectable = (selectableCount > 0);
	auto padding = st::filterLinkSubsectionTitlePadding;
	if (selectable) {
		const auto font = st::boxLinkButton.font;
		padding.setRight(padding.right() + font->spacew + std::max(
			font->width(tr::lng_filters_by_link_select(tr::now)),
			font->width(tr::lng_filters_by_link_deselect(tr::now))));
	}
	const auto title = Settings::AddSubsectionTitle(
		container,
		std::move(text),
		padding);
	if (!selectable) {
		return;
	}
	const auto link = Ui::CreateChild<Ui::LinkButton>(
		container.get(),
		tr::lng_filters_by_link_select(tr::now),
		st::boxLinkButton);
	const auto canSelect = link->lifetime().make_state<rpl::variable<bool>>(
		std::move(selectedCount) | rpl::map(_1 < selectableCount));
	canSelect->value(
	) | rpl::start_with_next([=](bool can) {
		link->setText(can
			? tr::lng_filters_by_link_select(tr::now)
			: tr::lng_filters_by_link_deselect(tr::now));
	}, link->lifetime());
	link->setClickedCallback([=] {
		toggle(canSelect->current());
	});

	rpl::combine(
		container->widthValue(),
		title->topValue(),
		link->widthValue()
	) | rpl::start_with_next([=](int outer, int y, int width) {
		link->move(outer - st::boxRowPadding.right() - width, y);
	}, link->lifetime());
}

std::unique_ptr<PeerListRow> MakeFilterChatRow(
		not_null<PeerData*> peer,
		const QString &status,
		bool disabled) {
	return std::make_unique<ChatRow>(peer, status, disabled);
}
