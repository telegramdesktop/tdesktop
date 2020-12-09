/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_send_action.h"

#include "data/data_user.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/text/text_options.h"
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
constexpr auto kStatusShowClientsidePlayGame = 10 * crl::time(1000);
constexpr auto kStatusShowClientsideSpeaking = 6 * crl::time(1000);

} // namespace

SendActionPainter::SendActionPainter(not_null<History*> history)
: _history(history)
, _weak(&_history->session())
, _sendActionText(st::dialogsTextWidthMin) {
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
		emplaceAction(Type::ChooseLocation, kStatusShowClientsideChooseLocation);
	}, [&](const MTPDsendMessageChooseContactAction &) {
		emplaceAction(Type::ChooseContact, kStatusShowClientsideChooseContact);
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
		_sendActionAnimation.paint(
			p,
			color,
			x,
			y + st::normalFont->ascent,
			outerWidth,
			ms);
		auto animationWidth = _sendActionAnimation.width();
		x += animationWidth;
		availableWidth -= animationWidth;
		p.setPen(color);
		_sendActionText.drawElided(p, x, y, availableWidth);
		return true;
	}
	return false;
}

void SendActionPainter::paintSpeaking(
		Painter &p,
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
			auto sendActionString = [](Type type, const QString &name) -> QString {
				switch (type) {
				case Type::RecordVideo: return name.isEmpty() ? tr::lng_send_action_record_video(tr::now) : tr::lng_user_action_record_video(tr::now, lt_user, name);
				case Type::UploadVideo: return name.isEmpty() ? tr::lng_send_action_upload_video(tr::now) : tr::lng_user_action_upload_video(tr::now, lt_user, name);
				case Type::RecordVoice: return name.isEmpty() ? tr::lng_send_action_record_audio(tr::now) : tr::lng_user_action_record_audio(tr::now, lt_user, name);
				case Type::UploadVoice: return name.isEmpty() ? tr::lng_send_action_upload_audio(tr::now) : tr::lng_user_action_upload_audio(tr::now, lt_user, name);
				case Type::RecordRound: return name.isEmpty() ? tr::lng_send_action_record_round(tr::now) : tr::lng_user_action_record_round(tr::now, lt_user, name);
				case Type::UploadRound: return name.isEmpty() ? tr::lng_send_action_upload_round(tr::now) : tr::lng_user_action_upload_round(tr::now, lt_user, name);
				case Type::UploadPhoto: return name.isEmpty() ? tr::lng_send_action_upload_photo(tr::now) : tr::lng_user_action_upload_photo(tr::now, lt_user, name);
				case Type::UploadFile: return name.isEmpty() ? tr::lng_send_action_upload_file(tr::now) : tr::lng_user_action_upload_file(tr::now, lt_user, name);
				case Type::ChooseLocation:
				case Type::ChooseContact: return name.isEmpty() ? tr::lng_typing(tr::now) : tr::lng_user_typing(tr::now, lt_user, name);
				default: break;
				};
				return QString();
			};
			for (const auto [user, action] : _sendActions) {
				newTypingString = sendActionString(
					action.type,
					_history->peer->isUser() ? QString() : user->firstName);
				if (!newTypingString.isEmpty()) {
					_sendActionAnimation.start(action.type);
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
		_history->peer->owner().updateSendActionAnimation({
			_history,
			_sendActionAnimation.width(),
			st::normalFont->height,
			(force || sendActionChanged)
		});
	}
	if (force
		|| speakingChanged
		|| (speakingResult && !anim::Disabled())) {
		_history->peer->owner().updateSpeakingAnimation({
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
