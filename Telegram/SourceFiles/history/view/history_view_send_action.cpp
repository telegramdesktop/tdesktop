/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_send_action.h"

#include "data/data_user.h"
#include "data/data_send_action.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history.h"
#include "lang/lang_instance.h" // Instance::supportChoosingStickerReplacement
#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

constexpr auto kStatusShowClientsideTyping = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideRecordVideo = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideUploadVideo = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideRecordVoice = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideUploadVoice = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideRecordRound = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideUploadRound = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideUploadPhoto = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideUploadFile = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideChooseLocation = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideChooseContact = 6 * crl::time(1000);
constexpr auto kStatusShowClientsideChooseSticker = 6 * crl::time(1000);
constexpr auto kStatusShowClientsidePlayGame = 10 * crl::time(1000);
constexpr auto kStatusShowClientsideSpeaking = 6 * crl::time(1000);

} // namespace

SendActionPainter::SendActionPainter(
	not_null<History*> history,
	MsgId rootId)
: _history(history)
, _rootId(rootId)
, _weak(&_history->session())
, _st(st::dialogsTextStyle)
, _sendActionText(st::dialogsTextWidthMin) {
}

void SendActionPainter::setTopic(Data::ForumTopic *topic) {
	_topic = topic;
}

bool SendActionPainter::updateNeedsAnimating(
		not_null<UserData*> user,
		const MTPSendMessageAction &action) {
	using Type = Api::SendProgressType;
	if (action.type() == mtpc_sendMessageCancelAction) {
		clear(user);
		return false;
	}

	const auto now = crl::now();
	const auto emplaceAction = [&](
			Type type,
			crl::time duration,
			int progress = 0) {
		_sendActions.emplace_or_assign(user, type, now + duration, progress);
	};
	action.match([&](const MTPDsendMessageTypingAction &) {
		_typing.emplace_or_assign(user, now + kStatusShowClientsideTyping);
	}, [&](const MTPDsendMessageRecordVideoAction &) {
		emplaceAction(Type::RecordVideo, kStatusShowClientsideRecordVideo);
	}, [&](const MTPDsendMessageRecordAudioAction &) {
		emplaceAction(Type::RecordVoice, kStatusShowClientsideRecordVoice);
	}, [&](const MTPDsendMessageRecordRoundAction &) {
		emplaceAction(Type::RecordRound, kStatusShowClientsideRecordRound);
	}, [&](const MTPDsendMessageGeoLocationAction &) {
		emplaceAction(
			Type::ChooseLocation,
			kStatusShowClientsideChooseLocation);
	}, [&](const MTPDsendMessageChooseContactAction &) {
		emplaceAction(
			Type::ChooseContact,
			kStatusShowClientsideChooseContact);
	}, [&](const MTPDsendMessageUploadVideoAction &data) {
		emplaceAction(
			Type::UploadVideo,
			kStatusShowClientsideUploadVideo,
			data.vprogress().v);
	}, [&](const MTPDsendMessageUploadAudioAction &data) {
		emplaceAction(
			Type::UploadVoice,
			kStatusShowClientsideUploadVoice,
			data.vprogress().v);
	}, [&](const MTPDsendMessageUploadRoundAction &data) {
		emplaceAction(
			Type::UploadRound,
			kStatusShowClientsideUploadRound,
			data.vprogress().v);
	}, [&](const MTPDsendMessageUploadPhotoAction &data) {
		emplaceAction(
			Type::UploadPhoto,
			kStatusShowClientsideUploadPhoto,
			data.vprogress().v);
	}, [&](const MTPDsendMessageUploadDocumentAction &data) {
		emplaceAction(
			Type::UploadFile,
			kStatusShowClientsideUploadFile,
			data.vprogress().v);
	}, [&](const MTPDsendMessageGamePlayAction &) {
		const auto i = _sendActions.find(user);
		if ((i == end(_sendActions))
			|| (i->second.type == Type::PlayGame)
			|| (i->second.until <= now)) {
			emplaceAction(Type::PlayGame, kStatusShowClientsidePlayGame);
		}
	}, [&](const MTPDspeakingInGroupCallAction &) {
		_speaking.emplace_or_assign(
			user,
			now + kStatusShowClientsideSpeaking);
	}, [&](const MTPDsendMessageHistoryImportAction &) {
	}, [&](const MTPDsendMessageChooseStickerAction &) {
		emplaceAction(
			Type::ChooseSticker,
			kStatusShowClientsideChooseSticker);
	}, [&](const MTPDsendMessageEmojiInteraction &) {
		Unexpected("EmojiInteraction here.");
	}, [&](const MTPDsendMessageEmojiInteractionSeen &) {
		// #TODO interaction
	}, [&](const MTPDsendMessageCancelAction &) {
		Unexpected("CancelAction here.");
	});
	return updateNeedsAnimating(now, true);
}

bool SendActionPainter::paint(
		Painter &p,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		style::color color,
		crl::time ms) {
	if (_sendActionAnimation) {
		const auto animationWidth = _sendActionAnimation.width();
		const auto extraAnimationWidth = _animationLeft
			? animationWidth * 2
			: 0;
		const auto left
			= (availableWidth < _animationLeft + extraAnimationWidth)
				? 0
				: _animationLeft;
		_sendActionAnimation.paint(
			p,
			color,
			left + x,
			y + st::normalFont->ascent,
			outerWidth,
			ms);
		// availableWidth should be the same
		// if an animation is in the middle of text.
		if (!left) {
			x += animationWidth;
			availableWidth -= _animationLeft
				? extraAnimationWidth
				: animationWidth;
		}
		p.setPen(color);
		_sendActionText.drawElided(p, x, y, availableWidth);
		return true;
	}
	return false;
}

void SendActionPainter::paintSpeaking(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		style::color color,
		crl::time ms) {
	if (_speakingAnimation) {
		_speakingAnimation.paint(
			p,
			color,
			x,
			y,
			outerWidth,
			ms);
	} else {
		Ui::SendActionAnimation::PaintSpeakingIdle(
			p,
			color,
			x,
			y,
			outerWidth);
	}
}

bool SendActionPainter::updateNeedsAnimating(crl::time now, bool force) {
	if (!_weak) {
		return false;
	}
	auto sendActionChanged = false;
	auto speakingChanged = false;
	for (auto i = begin(_typing); i != end(_typing);) {
		if (now >= i->second) {
			i = _typing.erase(i);
			sendActionChanged = true;
		} else {
			++i;
		}
	}
	for (auto i = begin(_speaking); i != end(_speaking);) {
		if (now >= i->second) {
			i = _speaking.erase(i);
			speakingChanged = true;
		} else {
			++i;
		}
	}
	for (auto i = begin(_sendActions); i != end(_sendActions);) {
		if (now >= i->second.until) {
			i = _sendActions.erase(i);
			sendActionChanged = true;
		} else {
			++i;
		}
	}
	const auto wasSpeakingAnimation = !!_speakingAnimation;
	if (force || sendActionChanged || speakingChanged) {
		QString newTypingString;
		auto animationLeft = 0;
		auto typingCount = _typing.size();
		if (typingCount > 2) {
			newTypingString = tr::lng_many_typing(tr::now, lt_count, typingCount);
		} else if (typingCount > 1) {
			newTypingString = tr::lng_users_typing(
				tr::now,
				lt_user,
				begin(_typing)->first->firstName,
				lt_second_user,
				(end(_typing) - 1)->first->firstName);
		} else if (typingCount) {
			newTypingString = _history->peer->isUser()
				? tr::lng_typing(tr::now)
				: tr::lng_user_typing(
					tr::now,
					lt_user,
					begin(_typing)->first->firstName);
		} else if (!_sendActions.empty()) {
			// Handles all actions except game playing.
			using Type = Api::SendProgressType;
			const auto sendActionString = [](
					Type type,
					const QString &name) -> QString {
				switch (type) {
				case Type::RecordVideo: return name.isEmpty()
					? tr::lng_send_action_record_video({})
					: tr::lng_user_action_record_video({}, lt_user, name);
				case Type::UploadVideo: return name.isEmpty()
					? tr::lng_send_action_upload_video({})
					: tr::lng_user_action_upload_video({}, lt_user, name);
				case Type::RecordVoice: return name.isEmpty()
					? tr::lng_send_action_record_audio({})
					: tr::lng_user_action_record_audio({}, lt_user, name);
				case Type::UploadVoice: return name.isEmpty()
					? tr::lng_send_action_upload_audio({})
					: tr::lng_user_action_upload_audio({}, lt_user, name);
				case Type::RecordRound: return name.isEmpty()
					? tr::lng_send_action_record_round({})
					: tr::lng_user_action_record_round({}, lt_user, name);
				case Type::UploadRound: return name.isEmpty()
					? tr::lng_send_action_upload_round({})
					: tr::lng_user_action_upload_round({}, lt_user, name);
				case Type::UploadPhoto: return name.isEmpty()
					? tr::lng_send_action_upload_photo({})
					: tr::lng_user_action_upload_photo({}, lt_user, name);
				case Type::UploadFile: return name.isEmpty()
					? tr::lng_send_action_upload_file({})
					: tr::lng_user_action_upload_file({}, lt_user, name);
				case Type::ChooseLocation:
				case Type::ChooseContact: return name.isEmpty()
					? tr::lng_typing({})
					: tr::lng_user_typing({}, lt_user, name);
				case Type::ChooseSticker: return name.isEmpty()
					? tr::lng_send_action_choose_sticker({})
					: tr::lng_user_action_choose_sticker({}, lt_user, name);
				default: break;
				};
				return QString();
			};
			for (const auto &[user, action] : _sendActions) {
				const auto isNamed = !_history->peer->isUser();
				newTypingString = sendActionString(
					action.type,
					isNamed ? user->firstName : QString());
				if (!newTypingString.isEmpty()) {
					_sendActionAnimation.start(action.type);

					// Add an animation to the middle of text.
					const auto &lang = Lang::GetInstance();
					if (lang.supportChoosingStickerReplacement()
							&& (action.type == Type::ChooseSticker)) {
						const auto index = newTypingString.size()
							- lang.rightIndexChoosingStickerReplacement(
								isNamed);
						animationLeft = Ui::Text::String(
							_st,
							newTypingString.mid(0, index)).maxWidth();

						if (!_spacesCount) {
							// We have to use QFontMetricsF instead of
							// FontData::spacew for more precise calculation.
							const auto mf = QFontMetricsF(_st.font->f);
							_spacesCount = base::SafeRound(
								_sendActionAnimation.widthNoMargins()
									/ mf.horizontalAdvance(' '));
						}
						newTypingString = newTypingString.replace(
							index,
							Lang::kChoosingStickerReplacement.utf8().size(),
							QString().fill(' ', _spacesCount).constData(),
							_spacesCount);
					}

					break;
				}
			}

			// Everyone in sendActions are playing a game.
			if (newTypingString.isEmpty()) {
				int playingCount = _sendActions.size();
				if (playingCount > 2) {
					newTypingString = tr::lng_many_playing_game(
						tr::now,
						lt_count,
						playingCount);
				} else if (playingCount > 1) {
					newTypingString = tr::lng_users_playing_game(
						tr::now,
						lt_user,
						begin(_sendActions)->first->firstName,
						lt_second_user,
						(end(_sendActions) - 1)->first->firstName);
				} else {
					newTypingString = _history->peer->isUser()
						? tr::lng_playing_game(tr::now)
						: tr::lng_user_playing_game(
							tr::now,
							lt_user,
							begin(_sendActions)->first->firstName);
				}
				_sendActionAnimation.start(Type::PlayGame);
			}
		}
		if (typingCount > 0) {
			_sendActionAnimation.start(Api::SendProgressType::Typing);
		} else if (newTypingString.isEmpty()) {
			_sendActionAnimation.tryToFinish();
		}
		if (_sendActionString != newTypingString) {
			_sendActionString = newTypingString;
			_sendActionText.setText(
				st::dialogsTextStyle,
				_sendActionString,
				Ui::NameTextOptions());
		}
		if (_animationLeft != animationLeft) {
			_animationLeft = animationLeft;
		}
		if (_speaking.empty()) {
			_speakingAnimation.tryToFinish();
		} else {
			_speakingAnimation.start(Api::SendProgressType::Speaking);
		}
	} else if (_speaking.empty() && _speakingAnimation) {
		_speakingAnimation.tryToFinish();
	}
	const auto sendActionResult = !_typing.empty() || !_sendActions.empty();
	const auto speakingResult = !_speaking.empty() || wasSpeakingAnimation;
	if (force
		|| sendActionChanged
		|| (sendActionResult && !anim::Disabled())) {
		const auto height = std::max(
			st::normalFont->height,
			st::dialogsMiniPreviewTop + st::dialogsMiniPreview);
		_history->peer->owner().sendActionManager().updateAnimation({
			_topic ? ((Data::Thread*)_topic) : _history,
			0,
			_sendActionAnimation.width() + _animationLeft,
			height,
			(force || sendActionChanged)
		});
	}
	if (force
		|| speakingChanged
		|| (speakingResult && !anim::Disabled())) {
		_history->peer->owner().sendActionManager().updateSpeakingAnimation({
			_history
		});
	}
	return sendActionResult || speakingResult;
}

void SendActionPainter::clear(not_null<UserData*> from) {
	auto updateAtMs = crl::time(0);
	auto i = _typing.find(from);
	if (i != _typing.cend()) {
		updateAtMs = crl::now();
		i->second = updateAtMs;
	}
	auto j = _sendActions.find(from);
	if (j != _sendActions.cend()) {
		if (!updateAtMs) updateAtMs = crl::now();
		j->second.until = updateAtMs;
	}
	if (updateAtMs) {
		updateNeedsAnimating(updateAtMs, true);
	}
}

} // namespace HistoryView
