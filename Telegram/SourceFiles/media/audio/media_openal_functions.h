/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <alext.h>

namespace OpenAL {

/* Effect object functions */
inline LPALGENEFFECTS alGenEffects;
inline LPALDELETEEFFECTS alDeleteEffects;
inline LPALISEFFECT alIsEffect;
inline LPALEFFECTI alEffecti;
inline LPALEFFECTIV alEffectiv;
inline LPALEFFECTF alEffectf;
inline LPALEFFECTFV alEffectfv;
inline LPALGETEFFECTI alGetEffecti;
inline LPALGETEFFECTIV alGetEffectiv;
inline LPALGETEFFECTF alGetEffectf;
inline LPALGETEFFECTFV alGetEffectfv;

/* Filter object functions */
inline LPALGENFILTERS alGenFilters;
inline LPALDELETEFILTERS alDeleteFilters;
inline LPALISFILTER alIsFilter;
inline LPALFILTERI alFilteri;
inline LPALFILTERIV alFilteriv;
inline LPALFILTERF alFilterf;
inline LPALFILTERFV alFilterfv;
inline LPALGETFILTERI alGetFilteri;
inline LPALGETFILTERIV alGetFilteriv;
inline LPALGETFILTERF alGetFilterf;
inline LPALGETFILTERFV alGetFilterfv;

/* Auxiliary Effect Slot object functions */
inline LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
inline LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
inline LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
inline LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
inline LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
inline LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
inline LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
inline LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
inline LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
inline LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
inline LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

void LoadEFXExtension();
bool HasEFXExtension();

} // namespace OpenAL
