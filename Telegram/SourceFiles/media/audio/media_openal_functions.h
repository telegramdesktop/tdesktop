/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <al.h>

namespace OpenAL {

/* Effect object functions */
inline void (AL_APIENTRY *alGenEffects)(ALsizei, ALuint*);
inline void (AL_APIENTRY *alDeleteEffects)(ALsizei, const ALuint*);
inline ALboolean (AL_APIENTRY *alIsEffect)(ALuint);
inline void (AL_APIENTRY *alEffecti)(ALuint, ALenum, ALint);
inline void (AL_APIENTRY *alEffectiv)(ALuint, ALenum, const ALint*);
inline void (AL_APIENTRY *alEffectf)(ALuint, ALenum, ALfloat);
inline void (AL_APIENTRY *alEffectfv)(ALuint, ALenum, const ALfloat*);
inline void (AL_APIENTRY *alGetEffecti)(ALuint, ALenum, ALint*);
inline void (AL_APIENTRY *alGetEffectiv)(ALuint, ALenum, ALint*);
inline void (AL_APIENTRY *alGetEffectf)(ALuint, ALenum, ALfloat*);
inline void (AL_APIENTRY *alGetEffectfv)(ALuint, ALenum, ALfloat*);

/* Filter object functions */
inline void (AL_APIENTRY *alGenFilters)(ALsizei, ALuint*);
inline void (AL_APIENTRY *alDeleteFilters)(ALsizei, const ALuint*);
inline ALboolean (AL_APIENTRY *alIsFilter)(ALuint);
inline void (AL_APIENTRY *alFilteri)(ALuint, ALenum, ALint);
inline void (AL_APIENTRY *alFilteriv)(ALuint, ALenum, const ALint*);
inline void (AL_APIENTRY *alFilterf)(ALuint, ALenum, ALfloat);
inline void (AL_APIENTRY *alFilterfv)(ALuint, ALenum, const ALfloat*);
inline void (AL_APIENTRY *alGetFilteri)(ALuint, ALenum, ALint*);
inline void (AL_APIENTRY *alGetFilteriv)(ALuint, ALenum, ALint*);
inline void (AL_APIENTRY *alGetFilterf)(ALuint, ALenum, ALfloat*);
inline void (AL_APIENTRY *alGetFilterfv)(ALuint, ALenum, ALfloat*);

/* Auxiliary Effect Slot object functions */
inline void (AL_APIENTRY *alGenAuxiliaryEffectSlots)(ALsizei, ALuint*);
inline void (AL_APIENTRY *alDeleteAuxiliaryEffectSlots)(ALsizei, const ALuint*);
inline ALboolean (AL_APIENTRY *alIsAuxiliaryEffectSlot)(ALuint);
inline void (AL_APIENTRY *alAuxiliaryEffectSloti)(ALuint, ALenum, ALint);
inline void (AL_APIENTRY *alAuxiliaryEffectSlotiv)(ALuint, ALenum, const ALint*);
inline void (AL_APIENTRY *alAuxiliaryEffectSlotf)(ALuint, ALenum, ALfloat);
inline void (AL_APIENTRY *alAuxiliaryEffectSlotfv)(ALuint, ALenum, const ALfloat*);
inline void (AL_APIENTRY *alGetAuxiliaryEffectSloti)(ALuint, ALenum, ALint*);
inline void (AL_APIENTRY *alGetAuxiliaryEffectSlotiv)(ALuint, ALenum, ALint*);
inline void (AL_APIENTRY *alGetAuxiliaryEffectSlotf)(ALuint, ALenum, ALfloat*);
inline void (AL_APIENTRY *alGetAuxiliaryEffectSlotfv)(ALuint, ALenum, ALfloat*);

void LoadEFXExtension();
bool HasEFXExtension();

} // namespace OpenAL
