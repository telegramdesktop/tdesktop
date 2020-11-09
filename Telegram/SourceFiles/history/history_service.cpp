/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_service.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "layout.h"
#include "history/history.h"
#include "history/view/media/history_view_invoice.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
#include "history/view/history_view_service_message.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_game.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "core/application.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "storage/storage_shared_media.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"

namespace {

constexpr auto kPinnedMessageTextLimit = 16;

} // namespace

void HistoryService::setMessageByAction(const MTPmessageAction &action) {
	auto prepareChatAddUserText = [this](const MTPDmessageActionChatAddUser &action) {
		auto result = PreparedText{};
		auto &users = action.vusers().v;
		if (users.size() == 1) {
			auto u = history()->owner().user(users[0].v);
			if (u == _from) {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_user_joined(tr::now, lt_from, fromLinkText());
			} else {
				result.links.push_back(fromLink());
				result.links.push_back(u->createOpenLink());
				result.text = tr::lng_action_add_user(tr::now, lt_from, fromLinkText(), lt_user, textcmdLink(2, u->name));
			}
		} else if (users.isEmpty()) {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_add_user(tr::now, lt_from, fromLinkText(), lt_user, qsl("somebody"));
		} else {
			result.links.push_back(fromLink());
			for (auto i = 0, l = users.size(); i != l; ++i) {
				auto user = history()->owner().user(users[i].v);
				result.links.push_back(user->createOpenLink());

				auto linkText = textcmdLink(i + 2, user->name);
				if (i == 0) {
					result.text = linkText;
				} else if (i + 1 == l) {
					result.text = tr::lng_action_add_users_and_last(tr::now, lt_accumulated, result.text, lt_user, linkText);
				} else {
					result.text = tr::lng_action_add_users_and_one(tr::now, lt_accumulated, result.text, lt_user, linkText);
				}
			}
			result.text = tr::lng_action_add_users_many(tr::now, lt_from, fromLinkText(), lt_users, result.text);
		}
		return result;
	};

	auto prepareChatJoinedByLink = [this](const MTPDmessageActionChatJoinedByLink &action) {
		auto result = PreparedText{};
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_joined_by_link(tr::now, lt_from, fromLinkText());
		return result;
	};

	auto prepareChatCreate = [this](const MTPDmessageActionChatCreate &action) {
		auto result = PreparedText{};
		result.links.push_back(fromLink());
		result.text = tr::lng_action_created_chat(tr::now, lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle())));
		return result;
	};

	auto prepareChannelCreate = [this](const MTPDmessageActionChannelCreate &action) {
		auto result = PreparedText {};
		if (isPost()) {
			result.text = tr::lng_action_created_channel(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_created_chat(tr::now, lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle())));
		}
		return result;
	};

	auto prepareChatDeletePhoto = [this] {
		auto result = PreparedText{};
		if (isPost()) {
			result.text = tr::lng_action_removed_photo_channel(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_removed_photo(tr::now, lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareChatDeleteUser = [this](const MTPDmessageActionChatDeleteUser &action) {
		auto result = PreparedText{};
		if (peerFromUser(action.vuser_id()) == _from->id) {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_user_left(tr::now, lt_from, fromLinkText());
		} else {
			auto user = history()->owner().user(action.vuser_id().v);
			result.links.push_back(fromLink());
			result.links.push_back(user->createOpenLink());
			result.text = tr::lng_action_kick_user(tr::now, lt_from, fromLinkText(), lt_user, textcmdLink(2, user->name));
		}
		return result;
	};

	auto prepareChatEditPhoto = [this](const MTPDmessageActionChatEditPhoto &action) {
		auto result = PreparedText{};
		if (isPost()) {
			result.text = tr::lng_action_changed_photo_channel(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_changed_photo(tr::now, lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareChatEditTitle = [this](const MTPDmessageActionChatEditTitle &action) {
		auto result = PreparedText{};
		if (isPost()) {
			result.text = tr::lng_action_changed_title_channel(tr::now, lt_title, TextUtilities::Clean(qs(action.vtitle())));
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_changed_title(tr::now, lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle())));
		}
		return result;
	};

	auto prepareScreenshotTaken = [this] {
		auto result = PreparedText{};
		if (out()) {
			result.text = tr::lng_action_you_took_screenshot(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_took_screenshot(tr::now, lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareCustomAction = [&](const MTPDmessageActionCustomAction &action) {
		auto result = PreparedText{};
		result.text = qs(action.vmessage());
		return result;
	};

	auto prepareBotAllowed = [&](const MTPDmessageActionBotAllowed &action) {
		auto result = PreparedText{};
		const auto domain = qs(action.vdomain());
		result.text = tr::lng_action_bot_allowed_from_domain(
			tr::now,
			lt_domain,
			textcmdLink(qstr("http://") + domain, domain));
		return result;
	};

	auto prepareSecureValuesSent = [&](const MTPDmessageActionSecureValuesSent &action) {
		auto result = PreparedText{};
		auto documents = QStringList();
		for (const auto &type : action.vtypes().v) {
			documents.push_back([&] {
				switch (type.type()) {
				case mtpc_secureValueTypePersonalDetails:
					return tr::lng_action_secure_personal_details(tr::now);
				case mtpc_secureValueTypePassport:
				case mtpc_secureValueTypeDriverLicense:
				case mtpc_secureValueTypeIdentityCard:
				case mtpc_secureValueTypeInternalPassport:
					return tr::lng_action_secure_proof_of_identity(tr::now);
				case mtpc_secureValueTypeAddress:
					return tr::lng_action_secure_address(tr::now);
				case mtpc_secureValueTypeUtilityBill:
				case mtpc_secureValueTypeBankStatement:
				case mtpc_secureValueTypeRentalAgreement:
				case mtpc_secureValueTypePassportRegistration:
				case mtpc_secureValueTypeTemporaryRegistration:
					return tr::lng_action_secure_proof_of_address(tr::now);
				case mtpc_secureValueTypePhone:
					return tr::lng_action_secure_phone(tr::now);
				case mtpc_secureValueTypeEmail:
					return tr::lng_action_secure_email(tr::now);
				}
				Unexpected("Type in prepareSecureValuesSent.");
			}());
		};
		result.links.push_back(history()->peer->createOpenLink());
		result.text = tr::lng_action_secure_values_sent(
			tr::now,
			lt_user,
			textcmdLink(1, history()->peer->name),
			lt_documents,
			documents.join(", "));
		return result;
	};

	auto prepareContactSignUp = [this] {
		auto result = PreparedText{};
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_registered(tr::now, lt_from, fromLinkText());
		return result;
	};

	auto prepareProximityReached = [this](const MTPDmessageActionGeoProximityReached &action) {
		auto result = PreparedText{};
		const auto fromId = peerFromMTP(action.vfrom_id());
		const auto fromPeer = history()->owner().peer(fromId);
		const auto toId = peerFromMTP(action.vto_id());
		const auto toPeer = history()->owner().peer(toId);
		const auto selfId = _from->session().userPeerId();
		const auto distanceMeters = action.vdistance().v;
		const auto distance = [&] {
			if (distanceMeters >= 1000) {
				const auto km = (10 * (distanceMeters / 10)) / 1000.;
				return tr::lng_action_proximity_distance_km(
					tr::now,
					lt_count,
					km);
			} else {
				return tr::lng_action_proximity_distance_m(
					tr::now,
					lt_count,
					distanceMeters);
			}
		}();
		result.text = [&] {
			if (fromId == selfId) {
				result.links.push_back(toPeer->createOpenLink());
				return tr::lng_action_you_proximity_reached(
					tr::now,
					lt_distance,
					distance,
					lt_user,
					textcmdLink(1, toPeer->name));
			} else if (toId == selfId) {
				result.links.push_back(fromPeer->createOpenLink());
				return tr::lng_action_proximity_reached_you(
					tr::now,
					lt_from,
					textcmdLink(1, fromPeer->name),
					lt_distance,
					distance);
			} else {
				result.links.push_back(fromPeer->createOpenLink());
				result.links.push_back(toPeer->createOpenLink());
				return tr::lng_action_proximity_reached(
					tr::now,
					lt_from,
					textcmdLink(1, fromPeer->name),
					lt_distance,
					distance,
					lt_user,
					textcmdLink(2, toPeer->name));
			}
		}();
		return result;
	};

	const auto messageText = action.match([&](
		const MTPDmessageActionChatAddUser &data) {
		return prepareChatAddUserText(data);
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		return prepareChatJoinedByLink(data);
	}, [&](const MTPDmessageActionChatCreate &data) {
		return prepareChatCreate(data);
	}, [](const MTPDmessageActionChatMigrateTo &) {
		return PreparedText();
	}, [](const MTPDmessageActionChannelMigrateFrom &) {
		return PreparedText();
	}, [](const MTPDmessageActionHistoryClear &) {
		return PreparedText();
	}, [&](const MTPDmessageActionChannelCreate &data) {
		return prepareChannelCreate(data);
	}, [&](const MTPDmessageActionChatDeletePhoto &) {
		return prepareChatDeletePhoto();
	}, [&](const MTPDmessageActionChatDeleteUser &data) {
		return prepareChatDeleteUser(data);
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		return prepareChatEditPhoto(data);
	}, [&](const MTPDmessageActionChatEditTitle &data) {
		return prepareChatEditTitle(data);
	}, [&](const MTPDmessageActionPinMessage &) {
		return preparePinnedText();
	}, [&](const MTPDmessageActionGameScore &) {
		return prepareGameScoreText();
	}, [&](const MTPDmessageActionPhoneCall &) -> PreparedText {
		Unexpected("PhoneCall type in HistoryService.");
	}, [&](const MTPDmessageActionPaymentSent &) {
		return preparePaymentSentText();
	}, [&](const MTPDmessageActionScreenshotTaken &) {
		return prepareScreenshotTaken();
	}, [&](const MTPDmessageActionCustomAction &data) {
		return prepareCustomAction(data);
	}, [&](const MTPDmessageActionBotAllowed &data) {
		return prepareBotAllowed(data);
	}, [&](const MTPDmessageActionSecureValuesSent &data) {
		return prepareSecureValuesSent(data);
	}, [&](const MTPDmessageActionContactSignUp &data) {
		return prepareContactSignUp();
	}, [&](const MTPDmessageActionGeoProximityReached &data) {
		return prepareProximityReached(data);
	}, [](const MTPDmessageActionPaymentSentMe &) {
		LOG(("API Error: messageActionPaymentSentMe received."));
		return PreparedText{ tr::lng_message_empty(tr::now) };
	}, [](const MTPDmessageActionSecureValuesSentMe &) {
		LOG(("API Error: messageActionSecureValuesSentMe received."));
		return PreparedText{ tr::lng_message_empty(tr::now) };
	}, [](const MTPDmessageActionEmpty &) {
		return PreparedText{ tr::lng_message_empty(tr::now) };
	});

	setServiceText(messageText);

	// Additional information.
	applyAction(action);
}

void HistoryService::applyAction(const MTPMessageAction &action) {
	action.match([&](const MTPDmessageActionChatAddUser &data) {
		if (const auto channel = history()->peer->asMegagroup()) {
			const auto selfUserId = history()->session().userId();
			for (const auto &item : data.vusers().v) {
				if (item.v == selfUserId) {
					channel->mgInfo->joinedMessageFound = true;
					break;
				}
			}
		}
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		if (_from->isSelf()) {
			if (const auto channel = history()->peer->asMegagroup()) {
				channel->mgInfo->joinedMessageFound = true;
			}
		}
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		data.vphoto().match([&](const MTPDphoto &photo) {
			_media = std::make_unique<Data::MediaPhoto>(
				this,
				history()->peer,
				history()->owner().processPhoto(photo));
		}, [](const MTPDphotoEmpty &) {
		});
	}, [&](const MTPDmessageActionChatCreate &) {
		_clientFlags |= MTPDmessage_ClientFlag::f_is_group_essential;
	}, [&](const MTPDmessageActionChannelCreate &) {
		_clientFlags |= MTPDmessage_ClientFlag::f_is_group_essential;
	}, [&](const MTPDmessageActionChatMigrateTo &) {
		_clientFlags |= MTPDmessage_ClientFlag::f_is_group_essential;
	}, [&](const MTPDmessageActionChannelMigrateFrom &) {
		_clientFlags |= MTPDmessage_ClientFlag::f_is_group_essential;
	}, [](const auto &) {
	});
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
		dependent->msg = history()->owner().message(channelId(), dependent->msgId);
		if (dependent->msg) {
			if (dependent->msg->isEmpty()) {
				// Really it is deleted.
				dependent->msg = nullptr;
				force = true;
			} else {
				history()->owner().registerDependentMessage(this, dependent->msg);
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
		Core::App().notifications().checkDelayed();
	}
	return (dependent->msg || !dependent->msgId);
}

HistoryService::PreparedText HistoryService::preparePinnedText() {
	auto result = PreparedText {};
	auto pinned = Get<HistoryServicePinned>();
	if (pinned && pinned->msg) {
		const auto mediaText = [&] {
			using TTL = HistoryServiceSelfDestruct;
			if (const auto media = pinned->msg->media()) {
				return media->pinnedTextSubstring();
			} else if (const auto selfdestruct = pinned->msg->Get<TTL>()) {
				if (selfdestruct->type == TTL::Type::Photo) {
					return tr::lng_action_pinned_media_photo(tr::now);
				} else if (selfdestruct->type == TTL::Type::Video) {
					return tr::lng_action_pinned_media_video(tr::now);
				}
			}
			return QString();
		}();
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
			result.text = tr::lng_action_pinned_message(tr::now, lt_from, fromLinkText(), lt_text, textcmdLink(2, original));
		} else {
			result.text = tr::lng_action_pinned_media(tr::now, lt_from, fromLinkText(), lt_media, textcmdLink(2, mediaText));
		}
	} else if (pinned && pinned->msgId) {
		result.links.push_back(fromLink());
		result.links.push_back(pinned->lnk);
		result.text = tr::lng_action_pinned_media(tr::now, lt_from, fromLinkText(), lt_media, textcmdLink(2, tr::lng_contacts_loading(tr::now)));
	} else {
		result.links.push_back(fromLink());
		result.text = tr::lng_action_pinned_media(tr::now, lt_from, fromLinkText(), lt_media, tr::lng_deleted_message(tr::now));
	}
	return result;
}

HistoryService::PreparedText HistoryService::prepareGameScoreText() {
	auto result = PreparedText {};
	auto gamescore = Get<HistoryServiceGameScore>();

	auto computeGameTitle = [&]() -> QString {
		if (gamescore && gamescore->msg) {
			if (const auto media = gamescore->msg->media()) {
				if (const auto game = media->game()) {
					const auto row = 0;
					const auto column = 0;
					result.links.push_back(
						std::make_shared<ReplyMarkupClickHandler>(
							&history()->owner(),
							row,
							column,
							gamescore->msg->fullId()));
					auto titleText = game->title;
					return textcmdLink(result.links.size(), titleText);
				}
			}
			return tr::lng_deleted_message(tr::now);
		} else if (gamescore && gamescore->msgId) {
			return tr::lng_contacts_loading(tr::now);
		}
		return QString();
	};

	const auto scoreNumber = gamescore ? gamescore->score : 0;
	if (_from->isSelf()) {
		auto gameTitle = computeGameTitle();
		if (gameTitle.isEmpty()) {
			result.text = tr::lng_action_game_you_scored_no_game(
				tr::now,
				lt_count,
				scoreNumber);
		} else {
			result.text = tr::lng_action_game_you_scored(
				tr::now,
				lt_count,
				scoreNumber,
				lt_game,
				gameTitle);
		}
	} else {
		result.links.push_back(fromLink());
		auto gameTitle = computeGameTitle();
		if (gameTitle.isEmpty()) {
			result.text = tr::lng_action_game_score_no_game(
				tr::now,
				lt_count,
				scoreNumber,
				lt_from,
				fromLinkText());
		} else {
			result.text = tr::lng_action_game_score(
				tr::now,
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

	auto invoiceTitle = [&] {
		if (payment && payment->msg) {
			if (const auto media = payment->msg->media()) {
				if (const auto invoice = media->invoice()) {
					return invoice->title;
				}
			}
			return tr::lng_deleted_message(tr::now);
		} else if (payment && payment->msgId) {
			return tr::lng_contacts_loading(tr::now);
		}
		return QString();
	}();

	if (invoiceTitle.isEmpty()) {
		result.text = tr::lng_action_payment_done(tr::now, lt_amount, payment->amount, lt_user, history()->peer->name);
	} else {
		result.text = tr::lng_action_payment_done_for(tr::now, lt_amount, payment->amount, lt_user, history()->peer->name, lt_invoice, invoiceTitle);
	}
	return result;
}

HistoryService::HistoryService(
	not_null<History*> history,
	const MTPDmessage &data,
	MTPDmessage_ClientFlags clientFlags)
: HistoryItem(
		history,
		data.vid().v,
		data.vflags().v,
		clientFlags,
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	createFromMtp(data);
}

HistoryService::HistoryService(
	not_null<History*> history,
	const MTPDmessageService &data,
	MTPDmessage_ClientFlags clientFlags)
: HistoryItem(
		history,
		data.vid().v,
		mtpCastFlags(data.vflags().v),
		clientFlags,
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	createFromMtp(data);
}

HistoryService::HistoryService(
	not_null<History*> history,
	MsgId id,
	MTPDmessage_ClientFlags clientFlags,
	TimeId date,
	const PreparedText &message,
	MTPDmessage::Flags flags,
	PeerId from,
	PhotoData *photo)
: HistoryItem(history, id, flags, clientFlags, date, from) {
	setServiceText(message);
	if (photo) {
		_media = std::make_unique<Data::MediaPhoto>(
			this,
			history->peer,
			photo);
	}
}

bool HistoryService::updateDependencyItem() {
	if (GetDependentData()) {
		return updateDependent(true);
	}
	return HistoryItem::updateDependencyItem();
}

bool HistoryService::needCheck() const {
	return (GetDependentData() != nullptr)
		|| Has<HistoryServiceSelfDestruct>();
}

QString HistoryService::inDialogsText(DrawInDialog way) const {
	return textcmdLink(1, TextUtilities::Clean(notificationText()));
}

QString HistoryService::inReplyText() const {
	const auto result = HistoryService::notificationText();
	const auto text = result.trimmed().startsWith(author()->name)
		? result.trimmed().mid(author()->name.size()).trimmed()
		: result;
	return textcmdLink(1, text);
}

std::unique_ptr<HistoryView::Element> HistoryService::createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing) {
	return delegate->elementCreate(this, replacing);
}

QString HistoryService::fromLinkText() const {
	return textcmdLink(1, _from->name);
}

ClickHandlerPtr HistoryService::fromLink() const {
	return _from->createOpenLink();
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
	_textWidth = -1;
	_textHeight = 0;
}

void HistoryService::markMediaAsReadHook() {
	if (const auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		if (!selfdestruct->destructAt) {
			selfdestruct->destructAt = crl::now() + selfdestruct->timeToLive;
			history()->owner().selfDestructIn(this, selfdestruct->timeToLive);
		}
	}
}

crl::time HistoryService::getSelfDestructIn(crl::time now) {
	if (auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		if (selfdestruct->destructAt > 0) {
			if (selfdestruct->destructAt <= now) {
				auto text = [selfdestruct] {
					switch (selfdestruct->type) {
					case HistoryServiceSelfDestruct::Type::Photo: return tr::lng_ttl_photo_expired(tr::now);
					case HistoryServiceSelfDestruct::Type::Video: return tr::lng_ttl_video_expired(tr::now);
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

void HistoryService::createFromMtp(const MTPDmessage &message) {
	const auto media = message.vmedia();
	Assert(media != nullptr);

	const auto mediaType = media->type();
	switch (mediaType) {
	case mtpc_messageMediaPhoto: {
		if (message.is_media_unread()) {
			const auto &photo = media->c_messageMediaPhoto();
			const auto ttl = photo.vttl_seconds();
			Assert(ttl != nullptr);

			setSelfDestruct(HistoryServiceSelfDestruct::Type::Photo, ttl->v);
			if (out()) {
				setServiceText({ tr::lng_ttl_photo_sent(tr::now) });
			} else {
				auto result = PreparedText();
				result.links.push_back(fromLink());
				result.text = tr::lng_ttl_photo_received(tr::now, lt_from, fromLinkText());
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({ tr::lng_ttl_photo_expired(tr::now) });
		}
	} break;
	case mtpc_messageMediaDocument: {
		if (message.is_media_unread()) {
			const auto &document = media->c_messageMediaDocument();
			const auto ttl = document.vttl_seconds();
			Assert(ttl != nullptr);

			setSelfDestruct(HistoryServiceSelfDestruct::Type::Video, ttl->v);
			if (out()) {
				setServiceText({ tr::lng_ttl_video_sent(tr::now) });
			} else {
				auto result = PreparedText();
				result.links.push_back(fromLink());
				result.text = tr::lng_ttl_video_received(tr::now, lt_from, fromLinkText());
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({ tr::lng_ttl_video_expired(tr::now) });
		}
	} break;

	default: Unexpected("Media type in HistoryService::createFromMtp()");
	}
}

void HistoryService::createFromMtp(const MTPDmessageService &message) {
	if (message.vaction().type() == mtpc_messageActionGameScore) {
		UpdateComponents(HistoryServiceGameScore::Bit());
		Get<HistoryServiceGameScore>()->score = message.vaction().c_messageActionGameScore().vscore().v;
	} else if (message.vaction().type() == mtpc_messageActionPaymentSent) {
		UpdateComponents(HistoryServicePayment::Bit());
		auto amount = message.vaction().c_messageActionPaymentSent().vtotal_amount().v;
		auto currency = qs(message.vaction().c_messageActionPaymentSent().vcurrency());
		Get<HistoryServicePayment>()->amount = Ui::FillAmountAndCurrency(amount, currency);
	}
	if (const auto replyTo = message.vreply_to()) {
		replyTo->match([&](const MTPDmessageReplyHeader &data) {
			const auto peer = data.vreply_to_peer_id()
				? peerFromMTP(*data.vreply_to_peer_id())
				: history()->peer->id;
			if (!peer || peer == history()->peer->id) {
				if (message.vaction().type() == mtpc_messageActionPinMessage) {
					UpdateComponents(HistoryServicePinned::Bit());
				}
				if (const auto dependent = GetDependentData()) {
					dependent->msgId = data.vreply_to_msg_id().v;
					if (!updateDependent()) {
						history()->session().api().requestMessageData(
							history()->peer->asChannel(),
							dependent->msgId,
							HistoryDependentItemCallback(this));
					}
				}
			}
		});
	}
	setMessageByAction(message.vaction());
}

void HistoryService::applyEdition(const MTPDmessageService &message) {
	clearDependency();
	UpdateComponents(0);

	createFromMtp(message);

	if (message.vaction().type() == mtpc_messageActionHistoryClear) {
		removeMedia();
		finishEditionToEmpty();
	} else {
		finishEdition(-1);
	}
}

void HistoryService::removeMedia() {
	if (!_media) return;

	_media.reset();
	_textWidth = -1;
	_textHeight = 0;
	history()->owner().requestItemResize(this);
}

Storage::SharedMediaTypesMask HistoryService::sharedMediaTypes() const {
	if (auto media = this->media()) {
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
	history()->owner().requestItemResize(this);
	if (history()->textCachedFor == this) {
		history()->textCachedFor = nullptr;
	}
	//if (const auto feed = history()->peer->feed()) { // #TODO archive
	//	if (feed->textCachedFor == this) {
	//		feed->textCachedFor = nullptr;
	//		feed->updateChatListEntry();
	//	}
	//}
	history()->session().changes().messageUpdated(
		this,
		Data::MessageUpdate::Flag::DialogRowRepaint);
	history()->owner().updateDependentMessages(this);
}

void HistoryService::clearDependency() {
	if (const auto dependent = GetDependentData()) {
		if (dependent->msg) {
			history()->owner().unregisterDependentMessage(this, dependent->msg);
		}
	}
}

HistoryService::~HistoryService() {
	clearDependency();
	_media.reset();
}

HistoryService::PreparedText GenerateJoinedText(
		not_null<History*> history,
		not_null<UserData*> inviter) {
	if (inviter->id != history->session().userPeerId()) {
		auto result = HistoryService::PreparedText{};
		result.links.push_back(inviter->createOpenLink());
		result.text = (history->isMegagroup()
			? tr::lng_action_add_you_group
			: tr::lng_action_add_you)(
				tr::now,
				lt_from,
				textcmdLink(1, inviter->name));
		return result;
	} else if (history->isMegagroup()) {
		auto self = history->session().user();
		auto result = HistoryService::PreparedText{};
		result.links.push_back(self->createOpenLink());
		result.text = tr::lng_action_user_joined(
			tr::now,
			lt_from,
			textcmdLink(1, self->name));
		return result;
	}
	return { tr::lng_action_you_joined(tr::now) };
}

not_null<HistoryService*> GenerateJoinedMessage(
		not_null<History*> history,
		TimeId inviteDate,
		not_null<UserData*> inviter,
		MTPDmessage::Flags flags) {
	return history->makeServiceMessage(
		history->owner().nextLocalMessageId(),
		MTPDmessage_ClientFlag::f_local_history_entry,
		inviteDate,
		GenerateJoinedText(history, inviter),
		flags);
}
