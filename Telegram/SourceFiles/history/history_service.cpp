/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_service.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
#include "auth_session.h"
#include "window/notifications_manager.h"
#include "storage/storage_shared_media.h"
#include "ui/text_options.h"

namespace {

constexpr auto kPinnedMessageTextLimit = 16;

} // namespace

void HistoryService::setMessageByAction(const MTPmessageAction &action) {
	auto prepareChatAddUserText = [this](const MTPDmessageActionChatAddUser &action) {
		auto result = PreparedText {};
		auto &users = action.vusers.v;
		if (users.size() == 1) {
			auto u = App::user(peerFromUser(users[0]));
			if (u == _from) {
				result.links.push_back(fromLink());
				result.text = lng_action_user_joined(lt_from, fromLinkText());
			} else {
				result.links.push_back(fromLink());
				result.links.push_back(u->createOpenLink());
				result.text = lng_action_add_user(lt_from, fromLinkText(), lt_user, textcmdLink(2, u->name));
			}
		} else if (users.isEmpty()) {
			result.links.push_back(fromLink());
			result.text = lng_action_add_user(lt_from, fromLinkText(), lt_user, "somebody");
		} else {
			result.links.push_back(fromLink());
			for (auto i = 0, l = users.size(); i != l; ++i) {
				auto user = App::user(peerFromUser(users[i]));
				result.links.push_back(user->createOpenLink());

				auto linkText = textcmdLink(i + 2, user->name);
				if (i == 0) {
					result.text = linkText;
				} else if (i + 1 == l) {
					result.text = lng_action_add_users_and_last(lt_accumulated, result.text, lt_user, linkText);
				} else {
					result.text = lng_action_add_users_and_one(lt_accumulated, result.text, lt_user, linkText);
				}
			}
			result.text = lng_action_add_users_many(lt_from, fromLinkText(), lt_users, result.text);
		}
		return result;
	};

	auto prepareChatJoinedByLink = [this](const MTPDmessageActionChatJoinedByLink &action) {
		auto result = PreparedText {};
		result.links.push_back(fromLink());
		result.text = lng_action_user_joined_by_link(lt_from, fromLinkText());
		return result;
	};

	auto prepareChatCreate = [this](const MTPDmessageActionChatCreate &action) {
		auto result = PreparedText {};
		result.links.push_back(fromLink());
		result.text = lng_action_created_chat(lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle)));
		return result;
	};

	auto prepareChannelCreate = [this](const MTPDmessageActionChannelCreate &action) {
		auto result = PreparedText {};
		if (isPost()) {
			result.text = lang(lng_action_created_channel);
		} else {
			result.links.push_back(fromLink());
			result.text = lng_action_created_chat(lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle)));
		}
		return result;
	};

	auto prepareChatDeletePhoto = [this] {
		auto result = PreparedText {};
		if (isPost()) {
			result.text = lang(lng_action_removed_photo_channel);
		} else {
			result.links.push_back(fromLink());
			result.text = lng_action_removed_photo(lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareChatDeleteUser = [this](const MTPDmessageActionChatDeleteUser &action) {
		auto result = PreparedText {};
		if (peerFromUser(action.vuser_id) == _from->id) {
			result.links.push_back(fromLink());
			result.text = lng_action_user_left(lt_from, fromLinkText());
		} else {
			auto user = App::user(peerFromUser(action.vuser_id));
			result.links.push_back(fromLink());
			result.links.push_back(user->createOpenLink());
			result.text = lng_action_kick_user(lt_from, fromLinkText(), lt_user, textcmdLink(2, user->name));
		}
		return result;
	};

	auto prepareChatEditPhoto = [this](const MTPDmessageActionChatEditPhoto &action) {
		auto result = PreparedText {};
		if (isPost()) {
			result.text = lang(lng_action_changed_photo_channel);
		} else {
			result.links.push_back(fromLink());
			result.text = lng_action_changed_photo(lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareChatEditTitle = [this](const MTPDmessageActionChatEditTitle &action) {
		auto result = PreparedText {};
		if (isPost()) {
			result.text = lng_action_changed_title_channel(lt_title, TextUtilities::Clean(qs(action.vtitle)));
		} else {
			result.links.push_back(fromLink());
			result.text = lng_action_changed_title(lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle)));
		}
		return result;
	};

	auto prepareScreenshotTaken = [this] {
		auto result = PreparedText {};
		if (out()) {
			result.text = lang(lng_action_you_took_screenshot);
		} else {
			result.links.push_back(fromLink());
			result.text = lng_action_took_screenshot(lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareCustomAction = [&](const MTPDmessageActionCustomAction &action) {
		auto result = PreparedText {};
		result.text = qs(action.vmessage);
		return result;
	};

	auto messageText = PreparedText {};

	switch (action.type()) {
	case mtpc_messageActionChatAddUser: messageText = prepareChatAddUserText(action.c_messageActionChatAddUser()); break;
	case mtpc_messageActionChatJoinedByLink: messageText = prepareChatJoinedByLink(action.c_messageActionChatJoinedByLink()); break;
	case mtpc_messageActionChatCreate: messageText = prepareChatCreate(action.c_messageActionChatCreate()); break;
	case mtpc_messageActionChannelCreate: messageText = prepareChannelCreate(action.c_messageActionChannelCreate()); break;
	case mtpc_messageActionHistoryClear: break; // Leave empty text.
	case mtpc_messageActionChatDeletePhoto: messageText = prepareChatDeletePhoto(); break;
	case mtpc_messageActionChatDeleteUser: messageText = prepareChatDeleteUser(action.c_messageActionChatDeleteUser()); break;
	case mtpc_messageActionChatEditPhoto: messageText = prepareChatEditPhoto(action.c_messageActionChatEditPhoto()); break;
	case mtpc_messageActionChatEditTitle: messageText = prepareChatEditTitle(action.c_messageActionChatEditTitle()); break;
	case mtpc_messageActionChatMigrateTo: messageText.text = lang(lng_action_group_migrate); break;
	case mtpc_messageActionChannelMigrateFrom: messageText.text = lang(lng_action_group_migrate); break;
	case mtpc_messageActionPinMessage: messageText = preparePinnedText(); break;
	case mtpc_messageActionGameScore: messageText = prepareGameScoreText(); break;
	case mtpc_messageActionPhoneCall: Unexpected("PhoneCall type in HistoryService.");
	case mtpc_messageActionPaymentSent: messageText = preparePaymentSentText(); break;
	case mtpc_messageActionScreenshotTaken: messageText = prepareScreenshotTaken(); break;
	case mtpc_messageActionCustomAction: messageText = prepareCustomAction(action.c_messageActionCustomAction()); break;
	default: messageText.text = lang(lng_message_empty); break;
	}

	setServiceText(messageText);

	// Additional information.
	switch (action.type()) {
	case mtpc_messageActionChatAddUser: {
		if (auto channel = history()->peer->asMegagroup()) {
			auto &users = action.c_messageActionChatAddUser().vusers;
			for_const (auto &item, users.v) {
				if (item.v == Auth().userId()) {
					channel->mgInfo->joinedMessageFound = true;
					break;
				}
			}
		}
	} break;

	case mtpc_messageActionChatJoinedByLink: {
		if (_from->isSelf() && history()->peer->isMegagroup()) {
			history()->peer->asChannel()->mgInfo->joinedMessageFound = true;
		}
	} break;

	case mtpc_messageActionChatEditPhoto: {
		auto &photo = action.c_messageActionChatEditPhoto().vphoto;
		if (photo.type() == mtpc_photo) {
			_media = std::make_unique<HistoryPhoto>(this, history()->peer, photo.c_photo(), st::msgServicePhotoWidth);
		}
	} break;

	case mtpc_messageActionChatMigrateTo:
	case mtpc_messageActionChannelMigrateFrom: {
		_flags |= MTPDmessage_ClientFlag::f_is_group_migrate;
	} break;
	}
}

void HistoryService::setSelfDestruct(HistoryServiceSelfDestruct::Type type, int ttlSeconds) {
	UpdateComponents(HistoryServiceSelfDestruct::Bit());
	auto selfdestruct = Get<HistoryServiceSelfDestruct>();
	selfdestruct->timeToLive = ttlSeconds * 1000LL;
	selfdestruct->type = type;
}

bool HistoryService::updateDependent(bool force) {
	auto dependent = GetDependentData();
	Assert(dependent != nullptr);

	if (!force) {
		if (!dependent->msgId || dependent->msg) {
			return true;
		}
	}

	if (!dependent->lnk) {
		dependent->lnk = goToMessageClickHandler(history()->peer, dependent->msgId);
	}
	auto gotDependencyItem = false;
	if (!dependent->msg) {
		dependent->msg = App::histItemById(channelId(), dependent->msgId);
		if (dependent->msg) {
			if (dependent->msg->isEmpty()) {
				// Really it is deleted.
				dependent->msg = nullptr;
				force = true;
			} else {
				App::historyRegDependency(this, dependent->msg);
				gotDependencyItem = true;
			}
		}
	}
	if (dependent->msg) {
		updateDependentText();
	} else if (force) {
		if (dependent->msgId > 0) {
			dependent->msgId = 0;
			gotDependencyItem = true;
		}
		updateDependentText();
	}
	if (force && gotDependencyItem) {
		Auth().notifications().checkDelayed();
	}
	return (dependent->msg || !dependent->msgId);
}

HistoryService::PreparedText HistoryService::preparePinnedText() {
	auto result = PreparedText {};
	auto pinned = Get<HistoryServicePinned>();
	if (pinned && pinned->msg) {
		auto mediaText = ([pinned]() -> QString {
			auto media = pinned->msg->getMedia();
			switch (media ? media->type() : MediaTypeCount) {
			case MediaTypePhoto: return lang(lng_action_pinned_media_photo);
			case MediaTypeVideo: return lang(lng_action_pinned_media_video);
			case MediaTypeGrouped: return lang(media->getPhoto()
				? lng_action_pinned_media_photo
				: lng_action_pinned_media_video);
			case MediaTypeContact: return lang(lng_action_pinned_media_contact);
			case MediaTypeFile: return lang(lng_action_pinned_media_file);
			case MediaTypeGif: {
				if (auto document = media->getDocument()) {
					if (document->isVideoMessage()) {
						return lang(lng_action_pinned_media_video_message);
					}
				}
				return lang(lng_action_pinned_media_gif);
			} break;
			case MediaTypeSticker: {
				auto emoji = static_cast<HistorySticker*>(media)->emoji();
				if (emoji.isEmpty()) {
					return lang(lng_action_pinned_media_sticker);
				}
				return lng_action_pinned_media_emoji_sticker(lt_emoji, emoji);
			} break;
			case MediaTypeLocation: return lang(lng_action_pinned_media_location);
			case MediaTypeMusicFile: return lang(lng_action_pinned_media_audio);
			case MediaTypeVoiceFile: return lang(lng_action_pinned_media_voice);
			case MediaTypeGame: {
				auto title = static_cast<HistoryGame*>(media)->game()->title;
				return lng_action_pinned_media_game(lt_game, title);
			} break;
			}
			return QString();
		})();

		result.links.push_back(fromLink());
		result.links.push_back(pinned->lnk);
		if (mediaText.isEmpty()) {
			auto original = pinned->msg->originalText().text;
			auto cutAt = 0;
			auto limit = kPinnedMessageTextLimit;
			auto size = original.size();
			for (; limit != 0;) {
				--limit;
				if (cutAt >= size) break;
				if (original.at(cutAt).isLowSurrogate() && cutAt + 1 < size && original.at(cutAt + 1).isHighSurrogate()) {
					cutAt += 2;
				} else {
					++cutAt;
				}
			}
			if (!limit && cutAt + 5 < size) {
				original = original.mid(0, cutAt) + qstr("...");
			}
			result.text = lng_action_pinned_message(lt_from, fromLinkText(), lt_text, textcmdLink(2, original));
		} else {
			result.text = lng_action_pinned_media(lt_from, fromLinkText(), lt_media, textcmdLink(2, mediaText));
		}
	} else if (pinned && pinned->msgId) {
		result.links.push_back(fromLink());
		result.links.push_back(pinned->lnk);
		result.text = lng_action_pinned_media(lt_from, fromLinkText(), lt_media, textcmdLink(2, lang(lng_contacts_loading)));
	} else {
		result.links.push_back(fromLink());
		result.text = lng_action_pinned_media(lt_from, fromLinkText(), lt_media, lang(lng_deleted_message));
	}
	return result;
}

HistoryService::PreparedText HistoryService::prepareGameScoreText() {
	auto result = PreparedText {};
	auto gamescore = Get<HistoryServiceGameScore>();

	auto computeGameTitle = [gamescore, &result]() -> QString {
		if (gamescore && gamescore->msg) {
			if (const auto media = gamescore->msg->getMedia()) {
				if (media->type() == MediaTypeGame) {
					const auto row = 0;
					const auto column = 0;
					result.links.push_back(
						std::make_shared<ReplyMarkupClickHandler>(
							row,
							column,
							gamescore->msg->fullId()));
					auto titleText = static_cast<HistoryGame*>(media)->game()->title;
					return textcmdLink(result.links.size(), titleText);
				}
			}
			return lang(lng_deleted_message);
		} else if (gamescore && gamescore->msgId) {
			return lang(lng_contacts_loading);
		}
		return QString();
	};

	const auto scoreNumber = gamescore ? gamescore->score : 0;
	if (_from->isSelf()) {
		auto gameTitle = computeGameTitle();
		if (gameTitle.isEmpty()) {
			result.text = lng_action_game_you_scored_no_game(
				lt_count,
				scoreNumber);
		} else {
			result.text = lng_action_game_you_scored(
				lt_count,
				scoreNumber,
				lt_game,
				gameTitle);
		}
	} else {
		result.links.push_back(fromLink());
		auto gameTitle = computeGameTitle();
		if (gameTitle.isEmpty()) {
			result.text = lng_action_game_score_no_game(
				lt_count,
				scoreNumber,
				lt_from,
				fromLinkText());
		} else {
			result.text = lng_action_game_score(
				lt_count,
				scoreNumber,
				lt_from,
				fromLinkText(),
				lt_game,
				gameTitle);
		}
	}
	return result;
}

HistoryService::PreparedText HistoryService::preparePaymentSentText() {
	auto result = PreparedText {};
	auto payment = Get<HistoryServicePayment>();

	auto invoiceTitle = ([payment]() -> QString {
		if (payment && payment->msg) {
			if (auto media = payment->msg->getMedia()) {
				if (media->type() == MediaTypeInvoice) {
					return static_cast<HistoryInvoice*>(media)->getTitle();
				}
			}
			return lang(lng_deleted_message);
		} else if (payment && payment->msgId) {
			return lang(lng_contacts_loading);
		}
		return QString();
	})();

	if (invoiceTitle.isEmpty()) {
		result.text = lng_action_payment_done(lt_amount, payment->amount, lt_user, history()->peer->name);
	} else {
		result.text = lng_action_payment_done_for(lt_amount, payment->amount, lt_user, history()->peer->name, lt_invoice, invoiceTitle);
	}
	return result;
}

HistoryService::HistoryService(not_null<History*> history, const MTPDmessage &message) :
	HistoryItem(history, message.vid.v, message.vflags.v, ::date(message.vdate), message.has_from_id() ? message.vfrom_id.v : 0) {
	createFromMtp(message);
}

HistoryService::HistoryService(not_null<History*> history, const MTPDmessageService &message) :
	HistoryItem(history, message.vid.v, mtpCastFlags(message.vflags.v), ::date(message.vdate), message.has_from_id() ? message.vfrom_id.v : 0) {
	createFromMtp(message);
}

HistoryService::HistoryService(not_null<History*> history, MsgId msgId, QDateTime date, const PreparedText &message, MTPDmessage::Flags flags, int32 from, PhotoData *photo) :
	HistoryItem(history, msgId, flags, date, from) {
	setServiceText(message);
	if (photo) {
		_media = std::make_unique<HistoryPhoto>(this, history->peer, photo, st::msgServicePhotoWidth);
	}
}

void HistoryService::initDimensions() {
	_maxw = _text.maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	_minh = _text.minHeight();
	if (_media) {
		_media->initDimensions();
	}
}

bool HistoryService::updateDependencyItem() {
	if (GetDependentData()) {
		return updateDependent(true);
	}
	return HistoryItem::updateDependencyItem();
}

QRect HistoryService::countGeometry() const {
	auto result = QRect(0, 0, width(), _height);
	if (Adaptive::ChatWide()) {
		result.setWidth(qMin(result.width(), st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	return result.marginsRemoved(st::msgServiceMargin);
}

TextWithEntities HistoryService::selectedText(TextSelection selection) const {
	return _text.originalTextWithEntities((selection == FullSelection) ? AllTextSelection : selection);
}

QString HistoryService::inDialogsText(DrawInDialog way) const {
	return textcmdLink(1, TextUtilities::Clean(notificationText()));
}

QString HistoryService::inReplyText() const {
	QString result = HistoryService::notificationText();
	return result.trimmed().startsWith(author()->name) ? result.trimmed().mid(author()->name.size()).trimmed() : result;
}

void HistoryService::setServiceText(const PreparedText &prepared) {
	_text.setText(
		st::serviceTextStyle,
		prepared.text,
		Ui::ItemTextServiceOptions());
	auto linkIndex = 0;
	for_const (auto &link, prepared.links) {
		// Link indices start with 1.
		_text.setLink(++linkIndex, link);
	}

	setPendingInitDimensions();
	_textWidth = -1;
	_textHeight = 0;
}

void HistoryService::draw(Painter &p, QRect clip, TextSelection selection, TimeMs ms) const {
	auto height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom();
	auto dateh = 0;
	auto unreadbarh = 0;
	if (auto date = Get<HistoryMessageDate>()) {
		dateh = date->height();
		p.translate(0, dateh);
		clip.translate(0, -dateh);
		height -= dateh;
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		unreadbarh = unreadbar->height();
		if (clip.intersects(QRect(0, 0, width(), unreadbarh))) {
			unreadbar->paint(p, 0, width());
		}
		p.translate(0, unreadbarh);
		clip.translate(0, -unreadbarh);
		height -= unreadbarh;
	}

	HistoryLayout::PaintContext context(ms, clip, selection);
	HistoryLayout::ServiceMessagePainter::paint(p, this, context, height);

	if (auto skiph = dateh + unreadbarh) {
		p.translate(0, -skiph);
	}
}

int HistoryService::resizeContentGetHeight() {
	_height = displayedDateHeight();
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		_height += unreadbar->height();
	}

	if (_text.isEmpty()) {
		_textHeight = 0;
	} else {
		auto contentWidth = width();
		if (Adaptive::ChatWide()) {
			accumulate_min(contentWidth, st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left());
		}
		contentWidth -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
		if (contentWidth < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) {
			contentWidth = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;
		}

		auto nwidth = qMax(contentWidth - st::msgServicePadding.left() - st::msgServicePadding.right(), 0);
		if (nwidth != _textWidth) {
			_textWidth = nwidth;
			_textHeight = _text.countHeight(nwidth);
		}
		if (contentWidth >= _maxw) {
			_height += _minh;
		} else {
			_height += _textHeight;
		}
		_height += st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom();
		if (_media) {
			_height += st::msgServiceMargin.top() + _media->resizeGetHeight(_media->currentWidth());
		}
	}

	return _height;
}

void HistoryService::markMediaAsReadHook() {
	if (auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		if (!selfdestruct->destructAt) {
			selfdestruct->destructAt = getms(true) + selfdestruct->timeToLive;
			App::histories().selfDestructIn(this, selfdestruct->timeToLive);
		}
	}
}

TimeMs HistoryService::getSelfDestructIn(TimeMs now) {
	if (auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		if (selfdestruct->destructAt > 0) {
			if (selfdestruct->destructAt <= now) {
				auto text = [selfdestruct] {
					switch (selfdestruct->type) {
					case HistoryServiceSelfDestruct::Type::Photo: return lang(lng_ttl_photo_expired);
					case HistoryServiceSelfDestruct::Type::Video: return lang(lng_ttl_video_expired);
					}
					Unexpected("Type in HistoryServiceSelfDestruct::Type");
				};
				setServiceText({ text() });
				return 0;
			}
			return selfdestruct->destructAt - now;
		}
	}
	return 0;
}

bool HistoryService::hasPoint(QPoint point) const {
	auto g = countGeometry();
	if (g.width() < 1) {
		return false;
	}

	if (auto dateh = displayedDateHeight()) {
		g.setTop(g.top() + dateh);
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		g.setTop(g.top() + unreadbar->height());
	}
	if (_media) {
		g.setHeight(g.height() - (st::msgServiceMargin.top() + _media->height()));
	}
	return g.contains(point);
}

HistoryTextState HistoryService::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(this);

	auto g = countGeometry();
	if (g.width() < 1) {
		return result;
	}

	if (auto dateh = displayedDateHeight()) {
		point.setY(point.y() - dateh);
		g.setHeight(g.height() - dateh);
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		auto unreadbarh = unreadbar->height();
		point.setY(point.y() - unreadbarh);
		g.setHeight(g.height() - unreadbarh);
	}

	if (_media) {
		g.setHeight(g.height() - (st::msgServiceMargin.top() + _media->height()));
	}
	auto trect = g.marginsAdded(-st::msgServicePadding);
	if (trect.contains(point)) {
		auto textRequest = request.forText();
		textRequest.align = style::al_center;
		result = HistoryTextState(this, _text.getState(
			point - trect.topLeft(),
			trect.width(),
			textRequest));
		if (auto gamescore = Get<HistoryServiceGameScore>()) {
			if (!result.link && result.cursor == HistoryInTextCursorState && g.contains(point)) {
				result.link = gamescore->lnk;
			}
		} else if (auto payment = Get<HistoryServicePayment>()) {
			if (!result.link && result.cursor == HistoryInTextCursorState && g.contains(point)) {
				result.link = payment->lnk;
			}
		}
	} else if (_media) {
		result = _media->getState(point - QPoint(st::msgServiceMargin.left() + (g.width() - _media->maxWidth()) / 2, st::msgServiceMargin.top() + g.height() + st::msgServiceMargin.top()), request);
	}
	return result;
}

void HistoryService::createFromMtp(const MTPDmessage &message) {
	auto mediaType = message.vmedia.type();
	switch (mediaType) {
	case mtpc_messageMediaPhoto: {
		if (message.is_media_unread()) {
			auto &photo = message.vmedia.c_messageMediaPhoto();
			Assert(photo.has_ttl_seconds());
			setSelfDestruct(HistoryServiceSelfDestruct::Type::Photo, photo.vttl_seconds.v);
			if (out()) {
				setServiceText({ lang(lng_ttl_photo_sent) });
			} else {
				auto result = PreparedText();
				result.links.push_back(fromLink());
				result.text = lng_ttl_photo_received(lt_from, fromLinkText());
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({ lang(lng_ttl_photo_expired) });
		}
	} break;
	case mtpc_messageMediaDocument: {
		if (message.is_media_unread()) {
			auto &document = message.vmedia.c_messageMediaDocument();
			Assert(document.has_ttl_seconds());
			setSelfDestruct(HistoryServiceSelfDestruct::Type::Video, document.vttl_seconds.v);
			if (out()) {
				setServiceText({ lang(lng_ttl_video_sent) });
			} else {
				auto result = PreparedText();
				result.links.push_back(fromLink());
				result.text = lng_ttl_video_received(lt_from, fromLinkText());
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({ lang(lng_ttl_video_expired) });
		}
	} break;

	default: Unexpected("Media type in HistoryService::createFromMtp()");
	}
}

void HistoryService::createFromMtp(const MTPDmessageService &message) {
	if (message.vaction.type() == mtpc_messageActionGameScore) {
		UpdateComponents(HistoryServiceGameScore::Bit());
		Get<HistoryServiceGameScore>()->score = message.vaction.c_messageActionGameScore().vscore.v;
	} else if (message.vaction.type() == mtpc_messageActionPaymentSent) {
		UpdateComponents(HistoryServicePayment::Bit());
		auto amount = message.vaction.c_messageActionPaymentSent().vtotal_amount.v;
		auto currency = qs(message.vaction.c_messageActionPaymentSent().vcurrency);
		Get<HistoryServicePayment>()->amount = HistoryInvoice::fillAmountAndCurrency(amount, currency);
	}
	if (message.has_reply_to_msg_id()) {
		if (message.vaction.type() == mtpc_messageActionPinMessage) {
			UpdateComponents(HistoryServicePinned::Bit());
		}
		if (auto dependent = GetDependentData()) {
			dependent->msgId = message.vreply_to_msg_id.v;
			if (!updateDependent()) {
				Auth().api().requestMessageData(
					history()->peer->asChannel(),
					dependent->msgId,
					HistoryDependentItemCallback(fullId()));
			}
		}
	}
	setMessageByAction(message.vaction);
}

void HistoryService::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_media) _media->clickHandlerActiveChanged(p, active);
	HistoryItem::clickHandlerActiveChanged(p, active);
}

void HistoryService::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_media) _media->clickHandlerPressedChanged(p, pressed);
	HistoryItem::clickHandlerPressedChanged(p, pressed);
}

void HistoryService::applyEdition(const MTPDmessageService &message) {
	clearDependency();
	UpdateComponents(0);

	createFromMtp(message);

	if (message.vaction.type() == mtpc_messageActionHistoryClear) {
		removeMedia();
		finishEditionToEmpty();
	} else {
		finishEdition(-1);
	}
}

void HistoryService::removeMedia() {
	if (!_media) return;

	bool mediaWasDisplayed = _media->isDisplayed();
	_media.reset();
	if (mediaWasDisplayed) {
		_textWidth = -1;
		_textHeight = 0;
	}
}

Storage::SharedMediaTypesMask HistoryService::sharedMediaTypes() const {
	if (auto media = getMedia()) {
		return media->sharedMediaTypes();
	}
	return {};
}

void HistoryService::updateDependentText() {
	auto text = PreparedText {};
	if (Has<HistoryServicePinned>()) {
		text = preparePinnedText();
	} else if (Has<HistoryServiceGameScore>()) {
		text = prepareGameScoreText();
	} else if (Has<HistoryServicePayment>()) {
		text = preparePaymentSentText();
	} else {
		return;
	}

	setServiceText(text);
	if (history()->textCachedFor == this) {
		history()->textCachedFor = nullptr;
	}
	if (App::main()) {
		App::main()->dlgUpdated(history()->peer, id);
	}
	App::historyUpdateDependent(this);
}

void HistoryService::clearDependency() {
	if (auto dependent = GetDependentData()) {
		if (dependent->msg) {
			App::historyUnregDependency(this, dependent->msg);
		}
	}
}

HistoryService::~HistoryService() {
	clearDependency();
	_media.reset();
}

HistoryJoined::HistoryJoined(not_null<History*> history, const QDateTime &inviteDate, not_null<UserData*> inviter, MTPDmessage::Flags flags)
	: HistoryService(history, clientMsgId(), inviteDate, GenerateText(history, inviter), flags) {
}

HistoryJoined::PreparedText HistoryJoined::GenerateText(not_null<History*> history, not_null<UserData*> inviter) {
	if (inviter->id == Auth().userPeerId()) {
		if (history->isMegagroup()) {
			auto self = App::user(Auth().userPeerId());
			auto result = PreparedText {};
			result.links.push_back(self->createOpenLink());
			result.text = lng_action_user_joined(lt_from, textcmdLink(1, self->name));
			return result;
		}
		return { lang(lng_action_you_joined) };
	}
	auto result = PreparedText {};
	result.links.push_back(inviter->createOpenLink());
	result.text = (history->isMegagroup() ? lng_action_add_you_group : lng_action_add_you)(lt_from, textcmdLink(1, inviter->name));
	return result;
}
