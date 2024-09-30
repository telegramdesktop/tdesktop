/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/field_autocomplete.h"

#include "data/business/data_shortcut_messages.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "menu/menu_send.h" // SendMenu::FillSendMenu
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/message_field.h" // PrepareMentionTag.
#include "chat_helpers/tabbed_selector.h" // ChatHelpers::FileChosen.
#include "mainwindow.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "media/clip/media_clip_reader.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/text/text_options.h"
#include "ui/image/image.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "base/qt/qt_common_adapters.h"
#include "boxes/sticker_set_box.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace ChatHelpers {
namespace {

[[nodiscard]] QString PrimaryUsername(not_null<UserData*> user) {
	const auto &usernames = user->usernames();
	return usernames.empty() ? user->username() : usernames.front();
}

template <typename T, typename U>
inline int indexOfInFirstN(const T &v, const U &elem, int last) {
	for (auto b = v.cbegin(), i = b, e = b + std::max(int(v.size()), last)
		; i != e
		; ++i) {
		if (i->user == elem) {
			return (i - b);
		}
	}
	return -1;
}

} // namespace

class FieldAutocomplete::Inner final : public Ui::RpWidget {
public:
	struct ScrollTo {
		int top;
		int bottom;
	};

	Inner(
		std::shared_ptr<Show> show,
		const style::EmojiPan &st,
		not_null<FieldAutocomplete*> parent,
		not_null<MentionRows*> mrows,
		not_null<HashtagRows*> hrows,
		not_null<BotCommandRows*> brows,
		not_null<StickerRows*> srows);

	void clearSel(bool hidden = false);
	bool moveSel(int key);
	bool chooseSelected(FieldAutocomplete::ChooseMethod method) const;
	bool chooseAtIndex(
		FieldAutocomplete::ChooseMethod method,
		int index,
		Api::SendOptions options = {}) const;

	void setRecentInlineBotsInRows(int32 bots);
	void setSendMenuDetails(Fn<SendMenu::Details()> &&callback);
	void rowsUpdated();

	rpl::producer<FieldAutocomplete::MentionChosen> mentionChosen() const;
	rpl::producer<FieldAutocomplete::HashtagChosen> hashtagChosen() const;
	rpl::producer<FieldAutocomplete::BotCommandChosen>
		botCommandChosen() const;
	rpl::producer<FieldAutocomplete::StickerChosen> stickerChosen() const;
	rpl::producer<ScrollTo> scrollToRequested() const;

	void onParentGeometryChanged();

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	QRect selectedRect(int index) const;
	void updateSelectedRow();
	void setSel(int sel, bool scroll = false);
	void showPreview();
	void selectByMouse(QPoint global);

	QSize stickerBoundingBox() const;
	void setupLottie(StickerSuggestion &suggestion);
	void setupWebm(StickerSuggestion &suggestion);
	void repaintSticker(not_null<DocumentData*> document);
	void repaintStickerAtIndex(int index);
	std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();
	void clipCallback(
		Media::Clip::Notification notification,
		not_null<DocumentData*> document);

	const std::shared_ptr<Show> _show;
	const not_null<Main::Session*> _session;
	const style::EmojiPan &_st;
	const not_null<FieldAutocomplete*> _parent;
	const not_null<MentionRows*> _mrows;
	const not_null<HashtagRows*> _hrows;
	const not_null<BotCommandRows*> _brows;
	const not_null<StickerRows*> _srows;
	Ui::RoundRect _overBg;
	rpl::lifetime _stickersLifetime;
	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;
	base::unique_qptr<Ui::PopupMenu> _menu;
	int _stickersPerRow = 1;
	int _recentInlineBotsInRows = 0;
	int _sel = -1;
	int _down = -1;
	std::optional<QPoint> _lastMousePosition;
	bool _mouseSelection = false;

	bool _overDelete = false;

	bool _previewShown = false;

	bool _adjustShadowLeft = false;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	StickerPremiumMark _premiumMark;

	Fn<SendMenu::Details()> _sendMenuDetails;

	rpl::event_stream<FieldAutocomplete::MentionChosen> _mentionChosen;
	rpl::event_stream<FieldAutocomplete::HashtagChosen> _hashtagChosen;
	rpl::event_stream<FieldAutocomplete::BotCommandChosen> _botCommandChosen;
	rpl::event_stream<FieldAutocomplete::StickerChosen> _stickerChosen;
	rpl::event_stream<ScrollTo> _scrollToRequested;

	base::Timer _previewTimer;

};

struct FieldAutocomplete::StickerSuggestion {
	not_null<DocumentData*> document;
	std::shared_ptr<Data::DocumentMedia> documentMedia;
	std::unique_ptr<Lottie::SinglePlayer> lottie;
	Media::Clip::ReaderPointer webm;
	QImage premiumLock;
};

struct FieldAutocomplete::MentionRow {
	not_null<UserData*> user;
	Ui::Text::String name;
	Ui::PeerUserpicView userpic;
};

struct FieldAutocomplete::BotCommandRow {
	not_null<UserData*> user;
	QString command;
	QString description;
	Ui::PeerUserpicView userpic;
	Ui::Text::String descriptionText;
};

FieldAutocomplete::FieldAutocomplete(
	QWidget *parent,
	std::shared_ptr<Show> show,
	const style::EmojiPan *stOverride)
: RpWidget(parent)
, _show(std::move(show))
, _session(&_show->session())
, _st(stOverride ? *stOverride : st::defaultEmojiPan)
, _scroll(this) {
	hide();

	_scroll->setGeometry(rect());

	_inner = _scroll->setOwnedWidget(
		object_ptr<Inner>(
			_show,
			_st,
			this,
			&_mrows,
			&_hrows,
			&_brows,
			&_srows));
	_inner->setGeometry(rect());

	_inner->scrollToRequested(
	) | rpl::start_with_next([=](Inner::ScrollTo data) {
		_scroll->scrollToY(data.top, data.bottom);
	}, lifetime());

	_scroll->show();
	_inner->show();

	hide();

	_scroll->geometryChanged(
	) | rpl::start_with_next(crl::guard(_inner, [=] {
		_inner->onParentGeometryChanged();
	}), lifetime());
}

std::shared_ptr<Show> FieldAutocomplete::uiShow() const {
	return _show;
}

void FieldAutocomplete::requestRefresh() {
	_refreshRequests.fire({});
}

rpl::producer<> FieldAutocomplete::refreshRequests() const {
	return _refreshRequests.events();
}

void FieldAutocomplete::requestStickersUpdate() {
	_stickersUpdateRequests.fire({});
}

rpl::producer<> FieldAutocomplete::stickersUpdateRequests() const {
	return _stickersUpdateRequests.events();
}

auto FieldAutocomplete::mentionChosen() const
-> rpl::producer<FieldAutocomplete::MentionChosen> {
	return _inner->mentionChosen();
}

auto FieldAutocomplete::hashtagChosen() const
-> rpl::producer<FieldAutocomplete::HashtagChosen> {
	return _inner->hashtagChosen();
}

auto FieldAutocomplete::botCommandChosen() const
-> rpl::producer<FieldAutocomplete::BotCommandChosen> {
	return _inner->botCommandChosen();
}

auto FieldAutocomplete::stickerChosen() const
-> rpl::producer<FieldAutocomplete::StickerChosen> {
	return _inner->stickerChosen();
}

auto FieldAutocomplete::choosingProcesses() const
-> rpl::producer<FieldAutocomplete::Type> {
	return _scroll->scrollTopChanges(
	) | rpl::filter([](int top) {
		return top != 0;
	}) | rpl::map([=] {
		return !_mrows.empty()
			? Type::Mentions
			: !_hrows.empty()
			? Type::Hashtags
			: !_brows.empty()
			? Type::BotCommands
			: !_srows.empty()
			? Type::Stickers
			: _type;
	});
}

FieldAutocomplete::~FieldAutocomplete() = default;

void FieldAutocomplete::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto opacity = _a_opacity.value(_hiding ? 0. : 1.);
	if (opacity < 1.) {
		if (opacity > 0.) {
			p.setOpacity(opacity);
			p.drawPixmap(0, 0, _cache);
		} else if (_hiding) {

		}
		return;
	}

	p.fillRect(rect(), _st.bg);
}

void FieldAutocomplete::showFiltered(
		not_null<PeerData*> peer,
		QString query,
		bool addInlineBots) {
	_chat = peer->asChat();
	_user = peer->asUser();
	_channel = peer->asChannel();
	if (query.isEmpty()) {
		_type = Type::Mentions;
		rowsUpdated(
			MentionRows(),
			HashtagRows(),
			BotCommandRows(),
			base::take(_srows),
			false);
		return;
	}

	_emoji = nullptr;

	query = query.toLower();
	auto type = Type::Stickers;
	auto plainQuery = QStringView(query);
	switch (query.at(0).unicode()) {
	case '@':
		type = Type::Mentions;
		plainQuery = base::StringViewMid(query, 1);
		break;
	case '#':
		type = Type::Hashtags;
		plainQuery = base::StringViewMid(query, 1);
		break;
	case '/':
		type = Type::BotCommands;
		plainQuery = base::StringViewMid(query, 1);
		break;
	}
	const auto resetScroll = (_type != type || _filter != plainQuery);
	if (resetScroll) {
		_type = type;
		_filter = TextUtilities::RemoveAccents(plainQuery.toString());
	}
	_addInlineBots = addInlineBots;

	updateFiltered(resetScroll);
}

void FieldAutocomplete::showStickers(EmojiPtr emoji) {
	const auto resetScroll = (_emoji != emoji);
	if (resetScroll || emoji) {
		_emoji = emoji;
		_type = Type::Stickers;
	} else if (!emoji) {
		rowsUpdated(
			base::take(_mrows),
			base::take(_hrows),
			base::take(_brows),
			StickerRows(),
			false);
		return;
	}

	_chat = nullptr;
	_user = nullptr;
	_channel = nullptr;

	updateFiltered(resetScroll);
}

EmojiPtr FieldAutocomplete::stickersEmoji() const {
	return _emoji;
}

bool FieldAutocomplete::clearFilteredBotCommands() {
	if (_brows.empty()) {
		return false;
	}
	_brows.clear();
	return true;
}

FieldAutocomplete::StickerRows FieldAutocomplete::getStickerSuggestions() {
	const auto data = &_session->data().stickers();
	const auto list = data->getListByEmoji({ _emoji }, _stickersSeed);
	auto result = ranges::views::all(
		list
	) | ranges::views::transform([](not_null<DocumentData*> sticker) {
		return StickerSuggestion{
			sticker,
			sticker->createMediaView()
		};
	}) | ranges::to_vector;
	for (auto &suggestion : _srows) {
		if (!suggestion.lottie && !suggestion.webm) {
			continue;
		}
		const auto i = ranges::find(
			result,
			suggestion.document,
			&StickerSuggestion::document);
		if (i != end(result)) {
			i->lottie = std::move(suggestion.lottie);
			i->webm = std::move(suggestion.webm);
		}
	}
	return result;
}

void FieldAutocomplete::updateFiltered(bool resetScroll) {
	int32 now = base::unixtime::now(), recentInlineBots = 0;
	MentionRows mrows;
	HashtagRows hrows;
	BotCommandRows brows;
	StickerRows srows;
	if (_emoji) {
		srows = getStickerSuggestions();
	} else if (_type == Type::Mentions) {
		int maxListSize = _addInlineBots ? cRecentInlineBots().size() : 0;
		if (_chat) {
			maxListSize += (_chat->participants.empty() ? _chat->lastAuthors.size() : _chat->participants.size());
		} else if (_channel && _channel->isMegagroup()) {
			if (!_channel->canViewMembers()) {
				maxListSize += _channel->mgInfo->admins.size();
			} else if (!_channel->lastParticipantsRequestNeeded()) {
				maxListSize += _channel->mgInfo->lastParticipants.size();
			}
		}
		if (maxListSize) {
			mrows.reserve(maxListSize);
		}

		auto filterNotPassedByUsername = [this](UserData *user) -> bool {
			if (PrimaryUsername(user).startsWith(_filter, Qt::CaseInsensitive)) {
				const auto exactUsername
					= (PrimaryUsername(user).size() == _filter.size());
				return exactUsername;
			}
			return true;
		};
		auto filterNotPassedByName = [&](UserData *user) -> bool {
			for (const auto &nameWord : user->nameWords()) {
				if (nameWord.startsWith(_filter, Qt::CaseInsensitive)) {
					const auto exactUsername = PrimaryUsername(user).compare(
						_filter,
						Qt::CaseInsensitive) == 0;
					return exactUsername;
				}
			}
			return filterNotPassedByUsername(user);
		};

		bool listAllSuggestions = _filter.isEmpty();
		if (_addInlineBots) {
			for (const auto user : cRecentInlineBots()) {
				if (user->isInaccessible()
					|| (!listAllSuggestions
						&& filterNotPassedByUsername(user))) {
					continue;
				}
				mrows.push_back({ user });
				++recentInlineBots;
			}
		}
		if (_chat) {
			auto sorted = base::flat_multi_map<TimeId, not_null<UserData*>>();
			const auto byOnline = [&](not_null<UserData*> user) {
				return Data::SortByOnlineValue(user, now);
			};
			mrows.reserve(mrows.size() + (_chat->participants.empty() ? _chat->lastAuthors.size() : _chat->participants.size()));
			if (_chat->noParticipantInfo()) {
				_chat->session().api().requestFullPeer(_chat);
			} else if (!_chat->participants.empty()) {
				for (const auto &user : _chat->participants) {
					if (user->isInaccessible()) continue;
					if (!listAllSuggestions && filterNotPassedByName(user)) continue;
					if (indexOfInFirstN(mrows, user, recentInlineBots) >= 0) continue;
					sorted.emplace(byOnline(user), user);
				}
			}
			for (const auto user : _chat->lastAuthors) {
				if (user->isInaccessible()) continue;
				if (!listAllSuggestions && filterNotPassedByName(user)) continue;
				if (indexOfInFirstN(mrows, user, recentInlineBots) >= 0) continue;
				mrows.push_back({ user });
				sorted.remove(byOnline(user), user);
			}
			for (auto i = sorted.cend(), b = sorted.cbegin(); i != b;) {
				--i;
				mrows.push_back({ i->second });
			}
		} else if (_channel && _channel->isMegagroup()) {
			if (!_channel->canViewMembers()) {
				if (!_channel->mgInfo->adminsLoaded) {
					_channel->session().api().chatParticipants().requestAdmins(_channel);
				} else {
					mrows.reserve(mrows.size() + _channel->mgInfo->admins.size());
					for (const auto &[userId, rank] : _channel->mgInfo->admins) {
						if (const auto user = _channel->owner().userLoaded(userId)) {
							if (user->isInaccessible()) continue;
							if (!listAllSuggestions && filterNotPassedByName(user)) continue;
							if (indexOfInFirstN(mrows, user, recentInlineBots) >= 0) continue;
							mrows.push_back({ user });
						}
					}
				}
			} else if (_channel->lastParticipantsRequestNeeded()) {
				_channel->session().api().chatParticipants().requestLast(
					_channel);
			} else {
				mrows.reserve(mrows.size() + _channel->mgInfo->lastParticipants.size());
				for (const auto user : _channel->mgInfo->lastParticipants) {
					if (user->isInaccessible()) continue;
					if (!listAllSuggestions && filterNotPassedByName(user)) continue;
					if (indexOfInFirstN(mrows, user, recentInlineBots) >= 0) continue;
					mrows.push_back({ user });
				}
			}
		}
	} else if (_type == Type::Hashtags) {
		bool listAllSuggestions = _filter.isEmpty();
		auto &recent(cRecentWriteHashtags());
		hrows.reserve(recent.size());
		for (const auto &item : recent) {
			const auto &tag = item.first;
			if (!listAllSuggestions
				&& (tag.size() == _filter.size()
					|| !TextUtilities::RemoveAccents(tag).startsWith(
						_filter,
						Qt::CaseInsensitive))) {
				continue;
			}
			hrows.push_back(tag);
		}
	} else if (_type == Type::BotCommands) {
		bool listAllSuggestions = _filter.isEmpty();
		bool hasUsername = _filter.indexOf('@') > 0;
		base::flat_map<
			not_null<UserData*>,
			not_null<const std::vector<Data::BotCommand>*>> bots;
		int32 cnt = 0;
		if (_chat) {
			if (_chat->noParticipantInfo()) {
				_chat->session().api().requestFullPeer(_chat);
			} else if (!_chat->participants.empty()) {
				const auto &commands = _chat->botCommands();
				for (const auto &user : _chat->participants) {
					if (!user->isBot()) {
						continue;
					}
					const auto i = commands.find(peerToUser(user->id));
					if (i != end(commands)) {
						bots.emplace(user, &i->second);
						cnt += i->second.size();
					}
				}
			}
		} else if (_user && _user->isBot()) {
			if (!_user->botInfo->inited) {
				_user->session().api().requestFullPeer(_user);
			}
			cnt = _user->botInfo->commands.size();
			bots.emplace(_user, &_user->botInfo->commands);
		} else if (_channel && _channel->isMegagroup()) {
			if (_channel->mgInfo->bots.empty()) {
				if (!_channel->mgInfo->botStatus) {
					_channel->session().api().chatParticipants().requestBots(
						_channel);
				}
			} else {
				const auto &commands = _channel->mgInfo->botCommands();
				for (const auto &user : _channel->mgInfo->bots) {
					if (!user->isBot()) {
						continue;
					}
					const auto i = commands.find(peerToUser(user->id));
					if (i != end(commands)) {
						bots.emplace(user, &i->second);
						cnt += i->second.size();
					}
				}
			}
		}
		if (cnt) {
			const auto make = [&](
					not_null<UserData*> user,
					const Data::BotCommand &command) {
				return BotCommandRow{
					user,
					command.command,
					command.description,
					user->activeUserpicView()
				};
			};
			brows.reserve(cnt);
			int32 botStatus = _chat ? _chat->botStatus : ((_channel && _channel->isMegagroup()) ? _channel->mgInfo->botStatus : -1);
			if (_chat) {
				for (const auto &user : _chat->lastAuthors) {
					if (!user->isBot()) {
						continue;
					}
					const auto i = bots.find(user);
					if (i == end(bots)) {
						continue;
					}
					for (const auto &command : *i->second) {
						if (!listAllSuggestions) {
							auto toFilter = (hasUsername || botStatus == 0 || botStatus == 2)
								? command.command + '@' + PrimaryUsername(user)
								: command.command;
							if (!toFilter.startsWith(_filter, Qt::CaseInsensitive)/* || toFilter.size() == _filter.size()*/) {
								continue;
							}
						}
						brows.push_back(make(user, command));
					}
					bots.erase(i);
				}
			}
			if (!bots.empty()) {
				for (auto i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
					const auto user = i->first;
					for (const auto &command : *i->second) {
						if (!listAllSuggestions) {
							const auto toFilter = (hasUsername
									|| botStatus == 0
									|| botStatus == 2)
								? command.command + '@' + PrimaryUsername(user)
								: command.command;
							if (!toFilter.startsWith(_filter, Qt::CaseInsensitive)/* || toFilter.size() == _filter.size()*/) continue;
						}
						brows.push_back(make(user, command));
					}
				}
			}
		}
		const auto shortcuts = (_user && !_user->isBot())
			? _user->owner().shortcutMessages().shortcuts().list
			: base::flat_map<BusinessShortcutId, Data::Shortcut>();
		if (!hasUsername && brows.empty() && !shortcuts.empty()) {
			const auto self = _user->session().user();
			for (const auto &[id, shortcut] : shortcuts) {
				if (shortcut.count < 1) {
					continue;
				} else if (!listAllSuggestions) {
					if (!shortcut.name.startsWith(_filter, Qt::CaseInsensitive)) {
						continue;
					}
				}
				brows.push_back(BotCommandRow{
					self,
					shortcut.name,
					tr::lng_forum_messages(tr::now, lt_count, shortcut.count),
					self->activeUserpicView()
				});
			}
			if (!brows.empty()) {
				brows.insert(begin(brows), BotCommandRow{ self }); // Edit.
			}
		}
	}
	rowsUpdated(
		std::move(mrows),
		std::move(hrows),
		std::move(brows),
		std::move(srows),
		resetScroll);
	_inner->setRecentInlineBotsInRows(recentInlineBots);
}

void FieldAutocomplete::rowsUpdated(
		MentionRows &&mrows,
		HashtagRows &&hrows,
		BotCommandRows &&brows,
		StickerRows &&srows,
		bool resetScroll) {
	if (mrows.empty() && hrows.empty() && brows.empty() && srows.empty()) {
		if (!isHidden()) {
			hideAnimated();
		}
		_scroll->scrollToY(0);
		_mrows.clear();
		_hrows.clear();
		_brows.clear();
		_srows.clear();
	} else {
		_mrows = std::move(mrows);
		_hrows = std::move(hrows);
		_brows = std::move(brows);
		_srows = std::move(srows);

		bool hidden = _hiding || isHidden();
		if (hidden) {
			show();
			_scroll->show();
		}
		recount(resetScroll);
		update();
		if (hidden) {
			hide();
			showAnimated();
		}
	}
	_inner->rowsUpdated();
}

void FieldAutocomplete::setBoundings(QRect boundings) {
	_boundings = boundings;
	recount();
}

void FieldAutocomplete::recount(bool resetScroll) {
	int32 h = 0, oldst = _scroll->scrollTop(), st = oldst, maxh = 4.5 * st::mentionHeight;
	if (!_srows.empty()) {
		int32 stickersPerRow = qMax(1, int32(_boundings.width() - 2 * st::stickerPanPadding) / int32(st::stickerPanSize.width()));
		int32 rows = rowscount(_srows.size(), stickersPerRow);
		h = st::stickerPanPadding + rows * st::stickerPanSize.height();
	} else if (!_mrows.empty()) {
		h = _mrows.size() * st::mentionHeight;
	} else if (!_hrows.empty()) {
		h = _hrows.size() * st::mentionHeight;
	} else if (!_brows.empty()) {
		h = _brows.size() * st::mentionHeight;
	}
	h += _st.autocompleteBottomSkip;

	if (_inner->width() != _boundings.width() || _inner->height() != h) {
		_inner->resize(_boundings.width(), h);
	}
	if (h > _boundings.height()) h = _boundings.height();
	if (h > maxh) h = maxh;
	if (width() != _boundings.width() || height() != h) {
		setGeometry(
			_boundings.x(),
			_boundings.y() + _boundings.height() - h,
			_boundings.width(),
			h);
		_scroll->resize(_boundings.width(), h);
	} else if (x() != _boundings.x()
		|| y() != _boundings.y() + _boundings.height() - h) {
		move(_boundings.x(), _boundings.y() + _boundings.height() - h);
	}
	if (resetScroll) st = 0;
	if (st != oldst) _scroll->scrollToY(st);
	if (resetScroll) _inner->clearSel();
}

void FieldAutocomplete::hideFast() {
	_a_opacity.stop();
	hideFinish();
}

void FieldAutocomplete::hideAnimated() {
	if (isHidden() || _hiding) {
		return;
	}

	if (_cache.isNull()) {
		_scroll->show();
		_cache = Ui::GrabWidget(this);
	}
	_scroll->hide();
	_hiding = true;
	_a_opacity.start([this] { animationCallback(); }, 1., 0., st::emojiPanDuration);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void FieldAutocomplete::hideFinish() {
	hide();
	_hiding = false;
	_filter = u"-"_q;
	_inner->clearSel(true);
}

void FieldAutocomplete::showAnimated() {
	if (!isHidden() && !_hiding) {
		return;
	}
	if (_cache.isNull()) {
		_stickersSeed = base::RandomValue<uint64>();
		_scroll->show();
		_cache = Ui::GrabWidget(this);
	}
	_scroll->hide();
	_hiding = false;
	show();
	_a_opacity.start([this] { animationCallback(); }, 0., 1., st::emojiPanDuration);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void FieldAutocomplete::animationCallback() {
	update();
	if (!_a_opacity.animating()) {
		_cache = QPixmap();
		setAttribute(Qt::WA_OpaquePaintEvent);
		if (_hiding) {
			hideFinish();
		} else {
			_scroll->show();
			_inner->clearSel();
		}
	}
}

const QString &FieldAutocomplete::filter() const {
	return _filter;
}

ChatData *FieldAutocomplete::chat() const {
	return _chat;
}

ChannelData *FieldAutocomplete::channel() const {
	return _channel;
}

UserData *FieldAutocomplete::user() const {
	return _user;
}

int32 FieldAutocomplete::innerTop() {
	return _scroll->scrollTop();
}

int32 FieldAutocomplete::innerBottom() {
	return _scroll->scrollTop() + _scroll->height();
}

bool FieldAutocomplete::chooseSelected(ChooseMethod method) const {
	return _inner->chooseSelected(method);
}

void FieldAutocomplete::setSendMenuDetails(
		Fn<SendMenu::Details()> &&callback) {
	_inner->setSendMenuDetails(std::move(callback));
}

bool FieldAutocomplete::eventFilter(QObject *obj, QEvent *e) {
	auto hidden = isHidden();
	auto moderate = Core::App().settings().moderateModeEnabled();
	if (hidden && !moderate) return QWidget::eventFilter(obj, e);

	if (e->type() == QEvent::KeyPress) {
		QKeyEvent *ev = static_cast<QKeyEvent*>(e);
		if (!(ev->modifiers() & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier))) {
			const auto key = ev->key();
			if (!hidden) {
				if (key == Qt::Key_Up || key == Qt::Key_Down || (!_srows.empty() && (key == Qt::Key_Left || key == Qt::Key_Right))) {
					return _inner->moveSel(key);
				} else if (key == Qt::Key_Enter || key == Qt::Key_Return) {
					return _inner->chooseSelected(ChooseMethod::ByEnter);
				}
			}
			if (moderate
				&& ((key >= Qt::Key_1 && key <= Qt::Key_9)
					|| key == Qt::Key_Q
					|| key == Qt::Key_W)) {

				return _moderateKeyActivateCallback
					? _moderateKeyActivateCallback(key)
					: false;
			}
		}
	}
	return QWidget::eventFilter(obj, e);
}

FieldAutocomplete::Inner::Inner(
	std::shared_ptr<Show> show,
	const style::EmojiPan &st,
	not_null<FieldAutocomplete*> parent,
	not_null<MentionRows*> mrows,
	not_null<HashtagRows*> hrows,
	not_null<BotCommandRows*> brows,
	not_null<StickerRows*> srows)
: _show(std::move(show))
, _session(&_show->session())
, _st(st)
, _parent(parent)
, _mrows(mrows)
, _hrows(hrows)
, _brows(brows)
, _srows(srows)
, _overBg(st::roundRadiusSmall, _st.overBg)
, _pathGradient(std::make_unique<Ui::PathShiftGradient>(
	_st.pathBg,
	_st.pathFg,
	[=] { update(); }))
, _premiumMark(_session, st::stickersPremiumLock)
, _previewTimer([=] { showPreview(); }) {
	_session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	_show->adjustShadowLeft(
	) | rpl::start_with_next([=](bool adjust) {
		_adjustShadowLeft = adjust;
		update();
	}, lifetime());
}

void FieldAutocomplete::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect());
	if (r != rect()) p.setClipRect(r);

	auto mentionleft = 2 * st::mentionPadding.left() + st::mentionPhotoSize;
	auto mentionwidth = width()
		- mentionleft
		- 2 * st::mentionPadding.right();
	auto htagleft = st::historyAttach.width
		+ st::historyComposeField.textMargins.left()
		- st::lineWidth;
	auto htagwidth = width()
		- st::mentionPadding.right()
		- htagleft
		- st::defaultScrollArea.width;

	if (!_srows->empty()) {
		_pathGradient->startFrame(
			0,
			width(),
			std::min(st::msgMaxWidth / 2, width() / 2));

		const auto now = crl::now();
		int32 rows = rowscount(_srows->size(), _stickersPerRow);
		int32 fromrow = floorclamp(r.y() - st::stickerPanPadding, st::stickerPanSize.height(), 0, rows);
		int32 torow = ceilclamp(r.y() + r.height() - st::stickerPanPadding, st::stickerPanSize.height(), 0, rows);
		int32 fromcol = floorclamp(r.x() - st::stickerPanPadding, st::stickerPanSize.width(), 0, _stickersPerRow);
		int32 tocol = ceilclamp(r.x() + r.width() - st::stickerPanPadding, st::stickerPanSize.width(), 0, _stickersPerRow);
		for (int32 row = fromrow; row < torow; ++row) {
			for (int32 col = fromcol; col < tocol; ++col) {
				int32 index = row * _stickersPerRow + col;
				if (index >= _srows->size()) break;

				auto &sticker = (*_srows)[index];
				const auto document = sticker.document;
				const auto &media = sticker.documentMedia;
				const auto info = document->sticker();
				if (!info) continue;

				if (media->loaded()) {
					if (info->isLottie() && !sticker.lottie) {
						setupLottie(sticker);
					} else if (info->isWebm() && !sticker.webm) {
						setupWebm(sticker);
					}
				}

				QPoint pos(st::stickerPanPadding + col * st::stickerPanSize.width(), st::stickerPanPadding + row * st::stickerPanSize.height());
				if (_sel == index) {
					QPoint tl(pos);
					if (rtl()) tl.setX(width() - tl.x() - st::stickerPanSize.width());
					_overBg.paint(p, QRect(tl, st::stickerPanSize));
				}

				media->checkStickerSmall();
				const auto paused = _show->paused(
					PauseReason::TabbedPanel);
				const auto size = ComputeStickerSize(
					document,
					stickerBoundingBox());
				const auto ppos = pos + QPoint(
					(st::stickerPanSize.width() - size.width()) / 2,
					(st::stickerPanSize.height() - size.height()) / 2);
				auto lottieFrame = QImage();
				if (sticker.lottie && sticker.lottie->ready()) {
					lottieFrame = sticker.lottie->frame();
					p.drawImage(
						QRect(
							ppos,
							lottieFrame.size() / style::DevicePixelRatio()),
						lottieFrame);
					if (!paused) {
						sticker.lottie->markFrameShown();
					}
				} else if (sticker.webm && sticker.webm->started()) {
					p.drawImage(ppos, sticker.webm->current({
						.frame = size,
						.keepAlpha = true,
					}, paused ? 0 : now));
				} else if (const auto image = media->getStickerSmall()) {
					p.drawPixmapLeft(ppos, width(), image->pix(size));
				} else {
					PaintStickerThumbnailPath(
						p,
						media.get(),
						QRect(ppos, size),
						_pathGradient.get());
				}

				if (document->isPremiumSticker()) {
					_premiumMark.paint(
						p,
						lottieFrame,
						sticker.premiumLock,
						pos,
						st::stickerPanSize,
						width());
				}
			}
		}
	} else {
		int32 from = qFloor(e->rect().top() / st::mentionHeight), to = qFloor(e->rect().bottom() / st::mentionHeight) + 1;
		int32 last = !_mrows->empty()
			? _mrows->size()
			: !_hrows->empty()
			? _hrows->size()
			: _brows->size();
		auto filter = _parent->filter();
		bool hasUsername = filter.indexOf('@') > 0;
		int filterSize = filter.size();
		bool filterIsEmpty = filter.isEmpty();
		for (int32 i = from; i < to; ++i) {
			if (i >= last) break;

			bool selected = (i == _sel);
			if (selected) {
				p.fillRect(0, i * st::mentionHeight, width(), st::mentionHeight, st::mentionBgOver);
				int skip = (st::mentionHeight - st::smallCloseIconOver.height()) / 2;
				if (!_hrows->empty() || (!_mrows->empty() && i < _recentInlineBotsInRows)) {
					st::smallCloseIconOver.paint(p, QPoint(width() - st::smallCloseIconOver.width() - skip, i * st::mentionHeight + skip), width());
				}
			}
			if (!_mrows->empty()) {
				auto &row = _mrows->at(i);
				const auto user = row.user;
				auto first = (!filterIsEmpty
						&& PrimaryUsername(user).startsWith(
							filter,
							Qt::CaseInsensitive))
					? ('@' + PrimaryUsername(user).mid(0, filterSize))
					: QString();
				auto second = first.isEmpty()
					? (PrimaryUsername(user).isEmpty()
						? QString()
						: ('@' + PrimaryUsername(user)))
					: PrimaryUsername(user).mid(filterSize);
				auto firstwidth = st::mentionFont->width(first);
				auto secondwidth = st::mentionFont->width(second);
				auto unamewidth = firstwidth + secondwidth;
				if (row.name.isEmpty()) {
					row.name.setText(st::msgNameStyle, user->name(), Ui::NameTextOptions());
				}
				auto namewidth = row.name.maxWidth();
				if (mentionwidth < unamewidth + namewidth) {
					namewidth = (mentionwidth * namewidth) / (namewidth + unamewidth);
					unamewidth = mentionwidth - namewidth;
					if (firstwidth < unamewidth + st::mentionFont->elidew) {
						if (firstwidth < unamewidth) {
							first = st::mentionFont->elided(first, unamewidth);
						} else if (!second.isEmpty()) {
							first = st::mentionFont->elided(first + second, unamewidth);
							second = QString();
						}
					} else {
						second = st::mentionFont->elided(second, unamewidth - firstwidth);
					}
				}
				user->loadUserpic();
				user->paintUserpicLeft(p, row.userpic, st::mentionPadding.left(), i * st::mentionHeight + st::mentionPadding.top(), width(), st::mentionPhotoSize);

				p.setPen(selected ? st::mentionNameFgOver : st::mentionNameFg);
				row.name.drawElided(p, 2 * st::mentionPadding.left() + st::mentionPhotoSize, i * st::mentionHeight + st::mentionTop, namewidth);

				p.setFont(st::mentionFont);
				p.setPen(selected ? st::mentionFgOverActive : st::mentionFgActive);
				p.drawText(mentionleft + namewidth + st::mentionPadding.right(), i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, first);
				if (!second.isEmpty()) {
					p.setPen(selected ? st::mentionFgOver : st::mentionFg);
					p.drawText(mentionleft + namewidth + st::mentionPadding.right() + firstwidth, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, second);
				}
			} else if (!_hrows->empty()) {
				QString hrow = _hrows->at(i);
				QString first = filterIsEmpty ? QString() : ('#' + hrow.mid(0, filterSize));
				QString second = filterIsEmpty ? ('#' + hrow) : hrow.mid(filterSize);
				int32 firstwidth = st::mentionFont->width(first), secondwidth = st::mentionFont->width(second);
				if (htagwidth < firstwidth + secondwidth) {
					if (htagwidth < firstwidth + st::mentionFont->elidew) {
						first = st::mentionFont->elided(first + second, htagwidth);
						second = QString();
					} else {
						second = st::mentionFont->elided(second, htagwidth - firstwidth);
					}
				}

				p.setFont(st::mentionFont);
				if (!first.isEmpty()) {
					p.setPen((selected ? st::mentionFgOverActive : st::mentionFgActive)->p);
					p.drawText(htagleft, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, first);
				}
				if (!second.isEmpty()) {
					p.setPen((selected ? st::mentionFgOver : st::mentionFg)->p);
					p.drawText(htagleft + firstwidth, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, second);
				}
			} else {
				auto &row = _brows->at(i);
				const auto user = row.user;
				if (user->isSelf() && row.command.isEmpty()) {
					p.setPen(st::windowActiveTextFg);
					p.setFont(st::semiboldFont);
					p.drawText(
						QRect(0, i * st::mentionHeight, width(), st::mentionHeight),
						tr::lng_replies_edit_button(tr::now),
						style::al_center);
					continue;
				}

				auto toHighlight = row.command;
				int32 botStatus = _parent->chat() ? _parent->chat()->botStatus : ((_parent->channel() && _parent->channel()->isMegagroup()) ? _parent->channel()->mgInfo->botStatus : -1);
				if (hasUsername || botStatus == 0 || botStatus == 2) {
					toHighlight += '@' + PrimaryUsername(user);
				}
				user->loadUserpic();
				user->paintUserpicLeft(p, row.userpic, st::mentionPadding.left(), i * st::mentionHeight + st::mentionPadding.top(), width(), st::mentionPhotoSize);

				auto commandText = '/' + toHighlight;

				p.setPen(selected ? st::mentionNameFgOver : st::mentionNameFg);
				p.setFont(st::semiboldFont);
				p.drawText(2 * st::mentionPadding.left() + st::mentionPhotoSize, i * st::mentionHeight + st::mentionTop + st::semiboldFont->ascent, commandText);

				auto commandTextWidth = st::semiboldFont->width(commandText);
				auto addleft = commandTextWidth + st::mentionPadding.left();
				auto widthleft = mentionwidth - addleft;

				if (!row.description.isEmpty()
					&& row.descriptionText.isEmpty()) {
					row.descriptionText.setText(
						st::defaultTextStyle,
						row.description,
						Ui::NameTextOptions());
				}
				if (widthleft > st::mentionFont->elidew && !row.descriptionText.isEmpty()) {
					p.setPen((selected ? st::mentionFgOver : st::mentionFg)->p);
					row.descriptionText.drawElided(p, mentionleft + addleft, i * st::mentionHeight + st::mentionTop, widthleft);
				}
			}
		}
		p.fillRect(
			_adjustShadowLeft ? st::lineWidth : 0,
			_parent->innerBottom() - st::lineWidth,
			width() - (_adjustShadowLeft ? st::lineWidth : 0),
			st::lineWidth,
			st::shadowFg);
	}
	p.fillRect(
		_adjustShadowLeft ? st::lineWidth : 0,
		_parent->innerTop(),
		width() - (_adjustShadowLeft ? st::lineWidth : 0),
		st::lineWidth,
		st::shadowFg);
}

void FieldAutocomplete::Inner::resizeEvent(QResizeEvent *e) {
	_stickersPerRow = qMax(1, int32(width() - 2 * st::stickerPanPadding) / int32(st::stickerPanSize.width()));
}

void FieldAutocomplete::Inner::mouseMoveEvent(QMouseEvent *e) {
	const auto globalPosition = e->globalPos();
	if (!_lastMousePosition) {
		_lastMousePosition = globalPosition;
		return;
	} else if (!_mouseSelection
		&& *_lastMousePosition == globalPosition) {
		return;
	}
	selectByMouse(globalPosition);
}

void FieldAutocomplete::Inner::clearSel(bool hidden) {
	_overDelete = false;
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	setSel((_mrows->empty() && _brows->empty() && _hrows->empty())
		? -1
		: (_brows->size() > 1
			&& _brows->front().user->isSelf()
			&& _brows->front().command.isEmpty())
		? 1
		: 0);
	if (hidden) {
		_down = -1;
		_previewShown = false;
	}
}

bool FieldAutocomplete::Inner::moveSel(int key) {
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;

	int32 maxSel = !_mrows->empty()
		? _mrows->size()
		: !_hrows->empty()
		? _hrows->size()
		: !_brows->empty()
		? _brows->size()
		: _srows->size();
	int32 direction = (key == Qt::Key_Up) ? -1 : (key == Qt::Key_Down ? 1 : 0);
	if (!_srows->empty()) {
		if (key == Qt::Key_Left) {
			direction = -1;
		} else if (key == Qt::Key_Right) {
			direction = 1;
		} else {
			direction *= _stickersPerRow;
		}
	}
	if (_sel >= maxSel || _sel < 0) {
		if (direction < -1) {
			setSel(((maxSel - 1) / _stickersPerRow) * _stickersPerRow, true);
		} else if (direction < 0) {
			setSel(maxSel - 1, true);
		} else {
			setSel(0, true);
		}
		return (_sel >= 0 && _sel < maxSel);
	}
	setSel((_sel + direction >= maxSel || _sel + direction < 0) ? -1 : (_sel + direction), true);
	return true;
}

bool FieldAutocomplete::Inner::chooseSelected(
		FieldAutocomplete::ChooseMethod method) const {
	return chooseAtIndex(method, _sel);
}

bool FieldAutocomplete::Inner::chooseAtIndex(
		FieldAutocomplete::ChooseMethod method,
		int index,
		Api::SendOptions options) const {
	if (index < 0 || (method == ChooseMethod::ByEnter && _mouseSelection)) {
		return false;
	}
	if (!_srows->empty()) {
		if (index < _srows->size()) {
			const auto document = (*_srows)[index].document;

			const auto from = [&]() -> Ui::MessageSendingAnimationFrom {
				if (options.scheduled) {
					return {};
				}
				const auto bounding = selectedRect(index);
				auto contentRect = QRect(
					QPoint(),
					ComputeStickerSize(
						document,
						stickerBoundingBox()));
				contentRect.moveCenter(bounding.center());
				return {
					Ui::MessageSendingAnimationFrom::Type::Sticker,
					_show->session().data().nextLocalMessageId(),
					mapToGlobal(std::move(contentRect)),
				};
			};

			_stickerChosen.fire({ document, options, from() });
			return true;
		}
	} else if (!_mrows->empty()) {
		if (index < _mrows->size()) {
			const auto user = _mrows->at(index).user;
			_mentionChosen.fire({ user, PrimaryUsername(user), method });
			return true;
		}
	} else if (!_hrows->empty()) {
		if (index < _hrows->size()) {
			_hashtagChosen.fire({ '#' + _hrows->at(index), method });
			return true;
		}
	} else if (!_brows->empty()) {
		if (index < _brows->size()) {
			const auto user = _brows->at(index).user;
			const auto &command = _brows->at(index).command;
			const auto botStatus = _parent->chat()
				? _parent->chat()->botStatus
				: ((_parent->channel() && _parent->channel()->isMegagroup())
					? _parent->channel()->mgInfo->botStatus
					: -1);

			const auto insertUsername = (botStatus == 0
				|| botStatus == 2
				|| _parent->filter().indexOf('@') > 0);
			const auto commandString = QString("/%1%2").arg(
				command,
				insertUsername ? ('@' + PrimaryUsername(user)) : QString());
			_botCommandChosen.fire({ user, commandString, method });
			return true;
		}
	}
	return false;
}

void FieldAutocomplete::Inner::setRecentInlineBotsInRows(int32 bots) {
	_recentInlineBotsInRows = bots;
}

void FieldAutocomplete::Inner::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());
	if (e->button() == Qt::LeftButton) {
		if (_overDelete && _sel >= 0 && _sel < (_mrows->empty() ? _hrows->size() : _recentInlineBotsInRows)) {
			bool removed = false;
			if (_mrows->empty()) {
				QString toRemove = _hrows->at(_sel);
				RecentHashtagPack &recent(cRefRecentWriteHashtags());
				for (RecentHashtagPack::iterator i = recent.begin(); i != recent.cend();) {
					if (i->first == toRemove) {
						i = recent.erase(i);
						removed = true;
					} else {
						++i;
					}
				}
			} else {
				UserData *toRemove = _mrows->at(_sel).user;
				RecentInlineBots &recent(cRefRecentInlineBots());
				int32 index = recent.indexOf(toRemove);
				if (index >= 0) {
					recent.remove(index);
					removed = true;
				}
			}
			if (removed) {
				_show->session().local().writeRecentHashtagsAndBots();
			}
			_parent->updateFiltered();

			selectByMouse(e->globalPos());
		} else if (_srows->empty()) {
			chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
		} else {
			_down = _sel;
			_previewTimer.callOnce(QApplication::startDragTime());
		}
	}
}

void FieldAutocomplete::Inner::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.cancel();

	int32 pressed = _down;
	_down = -1;

	selectByMouse(e->globalPos());

	if (_previewShown) {
		_previewShown = false;
		return;
	}

	if (_sel < 0 || _sel != pressed || _srows->empty()) return;

	chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
}

void FieldAutocomplete::Inner::contextMenuEvent(QContextMenuEvent *e) {
	if (_sel < 0 || _srows->empty() || _down >= 0) {
		return;
	}
	const auto index = _sel;
	const auto details = _sendMenuDetails
		? _sendMenuDetails()
		: SendMenu::Details();
	const auto method = FieldAutocomplete::ChooseMethod::ByClick;
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);

	const auto send = crl::guard(this, [=](Api::SendOptions options) {
		chooseAtIndex(method, index, options);
	});
	SendMenu::FillSendMenu(
		_menu,
		_show,
		details,
		SendMenu::DefaultCallback(_show, send));
	if (!_menu->empty()) {
		_menu->popup(QCursor::pos());
	}
}

void FieldAutocomplete::Inner::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void FieldAutocomplete::Inner::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	if (_mouseSelection) {
		setSel(-1);
		_mouseSelection = false;
		_lastMousePosition = std::nullopt;
	}
}

QRect FieldAutocomplete::Inner::selectedRect(int index) const {
	if (index < 0) {
		return QRect();
	}
	if (_srows->empty()) {
		return { 0, index * st::mentionHeight, width(), st::mentionHeight };
	} else {
		const auto row = int(index / _stickersPerRow);
		const auto col = int(index % _stickersPerRow);
		return {
			st::stickerPanPadding + col * st::stickerPanSize.width(),
			st::stickerPanPadding + row * st::stickerPanSize.height(),
			st::stickerPanSize.width(),
			st::stickerPanSize.height()
		};
	}
}

void FieldAutocomplete::Inner::updateSelectedRow() {
	const auto rect = selectedRect(_sel);
	if (rect.isValid()) {
		update(rect);
	}
}

void FieldAutocomplete::Inner::setSel(int sel, bool scroll) {
	updateSelectedRow();
	_sel = sel;
	updateSelectedRow();

	if (scroll && _sel >= 0) {
		if (_srows->empty()) {
			_scrollToRequested.fire({
				_sel * st::mentionHeight,
				(_sel + 1) * st::mentionHeight });
		} else {
			int32 row = _sel / _stickersPerRow;
			const auto padding = st::stickerPanPadding;
			_scrollToRequested.fire({
				(row ? padding : 0) + row * st::stickerPanSize.height(),
				(padding
					+ (row + 1) * st::stickerPanSize.height()
					+ _st.autocompleteBottomSkip) });
		}
	}
}

void FieldAutocomplete::Inner::rowsUpdated() {
	if (_srows->empty()) {
		_stickersLifetime.destroy();
	}
}

auto FieldAutocomplete::Inner::getLottieRenderer()
-> std::shared_ptr<Lottie::FrameRenderer> {
	if (auto result = _lottieRenderer.lock()) {
		return result;
	}
	auto result = Lottie::MakeFrameRenderer();
	_lottieRenderer = result;
	return result;
}

void FieldAutocomplete::Inner::setupLottie(StickerSuggestion &suggestion) {
	const auto document = suggestion.document;
	suggestion.lottie = LottiePlayerFromDocument(
		suggestion.documentMedia.get(),
		StickerLottieSize::InlineResults,
		stickerBoundingBox() * style::DevicePixelRatio(),
		Lottie::Quality::Default,
		getLottieRenderer());

	suggestion.lottie->updates(
	) | rpl::start_with_next([=] {
		repaintSticker(document);
	}, _stickersLifetime);
}

void FieldAutocomplete::Inner::setupWebm(StickerSuggestion &suggestion) {
	const auto document = suggestion.document;
	auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification, document);
	};
	suggestion.webm = Media::Clip::MakeReader(
		suggestion.documentMedia->owner()->location(),
		suggestion.documentMedia->bytes(),
		std::move(callback));
}

QSize FieldAutocomplete::Inner::stickerBoundingBox() const {
	return QSize(
		st::stickerPanSize.width() - st::roundRadiusSmall * 2,
		st::stickerPanSize.height() - st::roundRadiusSmall * 2);
}

void FieldAutocomplete::Inner::repaintSticker(
		not_null<DocumentData*> document) {
	const auto i = ranges::find(
		*_srows,
		document,
		&StickerSuggestion::document);
	if (i == end(*_srows)) {
		return;
	}
	repaintStickerAtIndex(i - begin(*_srows));
}

void FieldAutocomplete::Inner::repaintStickerAtIndex(int index) {
	const auto row = (index / _stickersPerRow);
	const auto col = (index % _stickersPerRow);
	update(
		st::stickerPanPadding + col * st::stickerPanSize.width(),
		st::stickerPanPadding + row * st::stickerPanSize.height(),
		st::stickerPanSize.width(),
		st::stickerPanSize.height());
}

void FieldAutocomplete::Inner::clipCallback(
		Media::Clip::Notification notification,
		not_null<DocumentData*> document) {
	const auto i = ranges::find(
		*_srows,
		document,
		&StickerSuggestion::document);
	if (i == end(*_srows)) {
		return;
	}
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		if (!i->webm) {
			break;
		} else if (i->webm->state() == State::Error) {
			i->webm.setBad();
		} else if (i->webm->ready() && !i->webm->started()) {
			const auto size = ComputeStickerSize(
				i->document,
				stickerBoundingBox());
			i->webm->start({ .frame = size, .keepAlpha = true });
		}
	} break;

	case Notification::Repaint: break;
	}
	repaintStickerAtIndex(i - begin(*_srows));
}

void FieldAutocomplete::Inner::selectByMouse(QPoint globalPosition) {
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	const auto mouse = mapFromGlobal(globalPosition);

	if (_down >= 0 && !_previewShown) {
		return;
	}

	int32 sel = -1, maxSel = 0;
	if (!_srows->empty()) {
		int32 row = (mouse.y() >= st::stickerPanPadding) ? ((mouse.y() - st::stickerPanPadding) / st::stickerPanSize.height()) : -1;
		int32 col = (mouse.x() >= st::stickerPanPadding) ? ((mouse.x() - st::stickerPanPadding) / st::stickerPanSize.width()) : -1;
		if (row >= 0 && col >= 0) {
			sel = row * _stickersPerRow + col;
		}
		maxSel = _srows->size();
		_overDelete = false;
	} else {
		sel = mouse.y() / int32(st::mentionHeight);
		maxSel = !_mrows->empty()
			? _mrows->size()
			: !_hrows->empty()
			? _hrows->size()
			: _brows->size();
		_overDelete = (!_hrows->empty() || (!_mrows->empty() && sel < _recentInlineBotsInRows)) ? (mouse.x() >= width() - st::mentionHeight) : false;
	}
	if (sel < 0 || sel >= maxSel) {
		sel = -1;
	}
	if (sel != _sel) {
		setSel(sel);
		if (_down >= 0 && _sel >= 0 && _down != _sel) {
			_down = _sel;
			showPreview();
		}
	}
}

void FieldAutocomplete::Inner::onParentGeometryChanged() {
	const auto globalPosition = QCursor::pos();
	if (rect().contains(mapFromGlobal(globalPosition))) {
		setMouseTracking(true);
		if (_mouseSelection) {
			selectByMouse(globalPosition);
		}
	}
}

void FieldAutocomplete::Inner::showPreview() {
	if (_down >= 0 && _down < _srows->size()) {
		const auto document = (*_srows)[_down].document;
		_show->showMediaPreview(document->stickerSetOrigin(), document);
		_previewShown = true;
	}
}

void FieldAutocomplete::Inner::setSendMenuDetails(
		Fn<SendMenu::Details()> &&callback) {
	_sendMenuDetails = std::move(callback);
}

auto FieldAutocomplete::Inner::mentionChosen() const
-> rpl::producer<FieldAutocomplete::MentionChosen> {
	return _mentionChosen.events();
}

auto FieldAutocomplete::Inner::hashtagChosen() const
-> rpl::producer<FieldAutocomplete::HashtagChosen> {
	return _hashtagChosen.events();
}

auto FieldAutocomplete::Inner::botCommandChosen() const
-> rpl::producer<FieldAutocomplete::BotCommandChosen> {
	return _botCommandChosen.events();
}

auto FieldAutocomplete::Inner::stickerChosen() const
-> rpl::producer<FieldAutocomplete::StickerChosen> {
	return _stickerChosen.events();
}

auto FieldAutocomplete::Inner::scrollToRequested() const
-> rpl::producer<ScrollTo> {
	return _scrollToRequested.events();
}

void InitFieldAutocomplete(
		std::unique_ptr<FieldAutocomplete> &autocomplete,
		FieldAutocompleteDescriptor &&descriptor) {
	Expects(!autocomplete);

	autocomplete = std::make_unique<FieldAutocomplete>(
		descriptor.parent,
		descriptor.show,
		descriptor.stOverride);
	const auto raw = autocomplete.get();
	const auto field = descriptor.field;

	field->rawTextEdit()->installEventFilter(raw);
	field->customTab(true);

	raw->mentionChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::MentionChosen data) {
		const auto user = data.user;
		if (data.mention.isEmpty()) {
			field->insertTag(
				user->firstName.isEmpty() ? user->name() : user->firstName,
				PrepareMentionTag(user));
		} else {
			field->insertTag('@' + data.mention);
		}
	}, raw->lifetime());

	const auto sendCommand = descriptor.sendBotCommand;
	const auto setText = descriptor.setText;

	raw->hashtagChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::HashtagChosen data) {
		field->insertTag(data.hashtag);
	}, raw->lifetime());

	const auto peer = descriptor.peer;
	const auto features = descriptor.features;
	const auto processShortcut = descriptor.processShortcut;
	const auto shortcutMessages = (processShortcut != nullptr)
		? &peer->owner().shortcutMessages()
		: nullptr;
	raw->botCommandChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::BotCommandChosen data) {
		if (!features().autocompleteCommands) {
			return;
		}
		using Method = FieldAutocompleteChooseMethod;
		const auto byTab = (data.method == Method::ByTab);
		const auto shortcut = data.user->isSelf();

		// Send bot command at once, if it was not inserted by pressing Tab.
		if (byTab && data.command.size() > 1) {
			field->insertTag(data.command);
		} else if (!shortcut) {
			sendCommand(data.command);
			setText(
				field->getTextWithTagsPart(field->textCursor().position()));
		} else if (processShortcut) {
			processShortcut(data.command.mid(1));
		}
	}, raw->lifetime());

	raw->setModerateKeyActivateCallback(std::move(descriptor.moderateKeyActivateCallback));

	if (const auto stickerChoosing = descriptor.stickerChoosing) {
		raw->choosingProcesses(
		) | rpl::start_with_next([=](FieldAutocomplete::Type type) {
			if (type == FieldAutocomplete::Type::Stickers) {
				stickerChoosing();
			}
		}, raw->lifetime());
	}
	if (const auto chosen = descriptor.stickerChosen) {
		raw->stickerChosen(
		) | rpl::start_with_next(chosen, raw->lifetime());
	}

	field->tabbed(
	) | rpl::start_with_next([=] {
		if (!raw->isHidden()) {
			raw->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
		}
	}, raw->lifetime());

	const auto check = [=] {
		auto parsed = ParseMentionHashtagBotCommandQuery(field, features());
		if (parsed.query.isEmpty()) {
		} else if (parsed.query[0] == '#'
			&& cRecentWriteHashtags().isEmpty()
			&& cRecentSearchHashtags().isEmpty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (parsed.query[0] == '@'
			&& cRecentInlineBots().isEmpty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (parsed.query[0] == '/'
			&& peer->isUser()
			&& !peer->asUser()->isBot()
			&& (!shortcutMessages
				|| shortcutMessages->shortcuts().list.empty())) {
			parsed = {};
		}
		raw->showFiltered(peer, parsed.query, parsed.fromStart);
	};

	const auto updateStickersByEmoji = [=] {
		const auto errorForStickers = Data::RestrictionError(
			peer,
			ChatRestriction::SendStickers);
		if (features().suggestStickersByEmoji && !errorForStickers) {
			const auto &text = field->getTextWithTags().text;
			auto length = 0;
			if (const auto emoji = Ui::Emoji::Find(text, &length)) {
				if (text.size() <= length) {
					raw->showStickers(emoji);
					return;
				}
			}
		}
		raw->showStickers(nullptr);
	};

	raw->refreshRequests(
	) | rpl::start_with_next(check, raw->lifetime());

	raw->stickersUpdateRequests(
	) | rpl::start_with_next(updateStickersByEmoji, raw->lifetime());

	peer->owner().botCommandsChanges(
	) | rpl::filter([=](not_null<PeerData*> changed) {
		return (peer == changed);
	}) | rpl::start_with_next([=] {
		if (raw->clearFilteredBotCommands()) {
			check();
		}
	}, raw->lifetime());

	peer->owner().stickers().updated(
		Data::StickersType::Stickers
	) | rpl::start_with_next(updateStickersByEmoji, raw->lifetime());

	QObject::connect(
		field->rawTextEdit(),
		&QTextEdit::cursorPositionChanged,
		raw,
		check,
		Qt::QueuedConnection);

	field->changes() | rpl::start_with_next(
		updateStickersByEmoji,
		raw->lifetime());

	peer->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Rights
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer == peer);
	}) | rpl::start_with_next(updateStickersByEmoji, raw->lifetime());

	if (shortcutMessages) {
		shortcutMessages->shortcutsChanged(
		) | rpl::start_with_next(check, raw->lifetime());
	}

	raw->setSendMenuDetails(std::move(descriptor.sendMenuDetails));
	raw->hideFast();
}

} // namespace ChatHelpers
