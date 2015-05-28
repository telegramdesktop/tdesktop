/***************************************************************************
 *   Copyright (C) 2012~2012 by CSSlayer                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <fcitx-config/hotkey.h>
#include <QTextCodec>
#include <QDebug>
#include <ctype.h>
#include "qtkeytrans.h"
#include "qtkeytransdata.h"

#define _ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define _ARRAY_END(a) (a + _ARRAY_SIZE(a))

void qEventToSym(int key, const QString& text, Qt::KeyboardModifiers mod, int& outsym, unsigned int& outstate) {
    int sym = 0;
    int state = 0;
    do {
        if (text.length() <= 0)
            break;
        int uni = text[0].unicode();
        int* result = qBinaryFind(unicodeHasKey, _ARRAY_END(unicodeHasKey), uni);
        if (result != _ARRAY_END(unicodeHasKey)) {
            sym = *result + 0x1000000;
            break;
        }

        Unicode2Key* keyMap = qBinaryFind(unicodeKeyMap, _ARRAY_END(unicodeKeyMap), uni);
        if (keyMap != _ARRAY_END(unicodeKeyMap)) {
            sym = keyMap->key;
            break;
        }
    } while(0);

    do {
        if (sym)
            break;

        QtCode2Key* result = NULL;
        if (mod & Qt::KeypadModifier) {
            result = qBinaryFind(keyPadQtCodeToKey, _ARRAY_END(keyPadQtCodeToKey), key);
            if (result == _ARRAY_END(keyPadQtCodeToKey))
                result = NULL;
        }
        else {
            if (text.isNull()) {
                result = qBinaryFind(qtCodeToKeyBackup, _ARRAY_END(qtCodeToKeyBackup), key);
                if (result == _ARRAY_END(qtCodeToKeyBackup))
                    result = NULL;
            }
            if (!result) {
                result = qBinaryFind(qtCodeToKey, _ARRAY_END(qtCodeToKey), key);

                if (result == _ARRAY_END(qtCodeToKey))
                    result = NULL;
            }

            if (!result) {
                result = qBinaryFind(keyPadQtCodeToKey, _ARRAY_END(keyPadQtCodeToKey), key);
                if (result == _ARRAY_END(keyPadQtCodeToKey))
                    result = NULL;
            }
        }

        if (result)
            sym = result->key;

    } while (0);

    state = FcitxKeyState_None;

    if (mod & Qt::CTRL)
        state |= FcitxKeyState_Ctrl;

    if (mod & Qt::ALT)
        state |= FcitxKeyState_Alt;

    if (mod & Qt::SHIFT)
        state |= FcitxKeyState_Shift;

    if (mod & Qt::META)
        state |= FcitxKeyState_Super;

    outsym = sym;
    outstate= state;
}

// the next lines are taken on 10/2009 from X.org (X11/XF86keysym.h), defining some special
// multimedia keys. They are included here as not every system has them.
#define XF86FcitxKey_MonBrightnessUp     0x1008FF02
#define XF86FcitxKey_MonBrightnessDown   0x1008FF03
#define XF86FcitxKey_KbdLightOnOff       0x1008FF04
#define XF86FcitxKey_KbdBrightnessUp     0x1008FF05
#define XF86FcitxKey_KbdBrightnessDown   0x1008FF06
#define XF86FcitxKey_Standby             0x1008FF10
#define XF86FcitxKey_AudioLowerVolume    0x1008FF11
#define XF86FcitxKey_AudioMute           0x1008FF12
#define XF86FcitxKey_AudioRaiseVolume    0x1008FF13
#define XF86FcitxKey_AudioPlay           0x1008FF14
#define XF86FcitxKey_AudioStop           0x1008FF15
#define XF86FcitxKey_AudioPrev           0x1008FF16
#define XF86FcitxKey_AudioNext           0x1008FF17
#define XF86FcitxKey_HomePage            0x1008FF18
#define XF86FcitxKey_Mail                0x1008FF19
#define XF86FcitxKey_Start               0x1008FF1A
#define XF86FcitxKey_Search              0x1008FF1B
#define XF86FcitxKey_AudioRecord         0x1008FF1C
#define XF86FcitxKey_Calculator          0x1008FF1D
#define XF86FcitxKey_Memo                0x1008FF1E
#define XF86FcitxKey_ToDoList            0x1008FF1F
#define XF86FcitxKey_Calendar            0x1008FF20
#define XF86FcitxKey_PowerDown           0x1008FF21
#define XF86FcitxKey_ContrastAdjust      0x1008FF22
#define XF86FcitxKey_Back                0x1008FF26
#define XF86FcitxKey_Forward             0x1008FF27
#define XF86FcitxKey_Stop                0x1008FF28
#define XF86FcitxKey_Refresh             0x1008FF29
#define XF86FcitxKey_PowerOff            0x1008FF2A
#define XF86FcitxKey_WakeUp              0x1008FF2B
#define XF86FcitxKey_Eject               0x1008FF2C
#define XF86FcitxKey_ScreenSaver         0x1008FF2D
#define XF86FcitxKey_WWW                 0x1008FF2E
#define XF86FcitxKey_Sleep               0x1008FF2F
#define XF86FcitxKey_Favorites           0x1008FF30
#define XF86FcitxKey_AudioPause          0x1008FF31
#define XF86FcitxKey_AudioMedia          0x1008FF32
#define XF86FcitxKey_MyComputer          0x1008FF33
#define XF86FcitxKey_LightBulb           0x1008FF35
#define XF86FcitxKey_Shop                0x1008FF36
#define XF86FcitxKey_History             0x1008FF37
#define XF86FcitxKey_OpenURL             0x1008FF38
#define XF86FcitxKey_AddFavorite         0x1008FF39
#define XF86FcitxKey_HotLinks            0x1008FF3A
#define XF86FcitxKey_BrightnessAdjust    0x1008FF3B
#define XF86FcitxKey_Finance             0x1008FF3C
#define XF86FcitxKey_Community           0x1008FF3D
#define XF86FcitxKey_AudioRewind         0x1008FF3E
#define XF86FcitxKey_BackForward         0x1008FF3F
#define XF86FcitxKey_Launch0             0x1008FF40
#define XF86FcitxKey_Launch1             0x1008FF41
#define XF86FcitxKey_Launch2             0x1008FF42
#define XF86FcitxKey_Launch3             0x1008FF43
#define XF86FcitxKey_Launch4             0x1008FF44
#define XF86FcitxKey_Launch5             0x1008FF45
#define XF86FcitxKey_Launch6             0x1008FF46
#define XF86FcitxKey_Launch7             0x1008FF47
#define XF86FcitxKey_Launch8             0x1008FF48
#define XF86FcitxKey_Launch9             0x1008FF49
#define XF86FcitxKey_LaunchA             0x1008FF4A
#define XF86FcitxKey_LaunchB             0x1008FF4B
#define XF86FcitxKey_LaunchC             0x1008FF4C
#define XF86FcitxKey_LaunchD             0x1008FF4D
#define XF86FcitxKey_LaunchE             0x1008FF4E
#define XF86FcitxKey_LaunchF             0x1008FF4F
#define XF86FcitxKey_ApplicationLeft     0x1008FF50
#define XF86FcitxKey_ApplicationRight    0x1008FF51
#define XF86FcitxKey_Book                0x1008FF52
#define XF86FcitxKey_CD                  0x1008FF53
#define XF86FcitxKey_Calculater          0x1008FF54
#define XF86FcitxKey_Clear               0x1008FF55
#define XF86FcitxKey_ClearGrab           0x1008FE21
#define XF86FcitxKey_Close               0x1008FF56
#define XF86FcitxKey_Copy                0x1008FF57
#define XF86FcitxKey_Cut                 0x1008FF58
#define XF86FcitxKey_Display             0x1008FF59
#define XF86FcitxKey_DOS                 0x1008FF5A
#define XF86FcitxKey_Documents           0x1008FF5B
#define XF86FcitxKey_Excel               0x1008FF5C
#define XF86FcitxKey_Explorer            0x1008FF5D
#define XF86FcitxKey_Game                0x1008FF5E
#define XF86FcitxKey_Go                  0x1008FF5F
#define XF86FcitxKey_iTouch              0x1008FF60
#define XF86FcitxKey_LogOff              0x1008FF61
#define XF86FcitxKey_Market              0x1008FF62
#define XF86FcitxKey_Meeting             0x1008FF63
#define XF86FcitxKey_MenuKB              0x1008FF65
#define XF86FcitxKey_MenuPB              0x1008FF66
#define XF86FcitxKey_MySites             0x1008FF67
#define XF86FcitxKey_News                0x1008FF69
#define XF86FcitxKey_OfficeHome          0x1008FF6A
#define XF86FcitxKey_Option              0x1008FF6C
#define XF86FcitxKey_Paste               0x1008FF6D
#define XF86FcitxKey_Phone               0x1008FF6E
#define XF86FcitxKey_Reply               0x1008FF72
#define XF86FcitxKey_Reload              0x1008FF73
#define XF86FcitxKey_RotateWindows       0x1008FF74
#define XF86FcitxKey_RotationPB          0x1008FF75
#define XF86FcitxKey_RotationKB          0x1008FF76
#define XF86FcitxKey_Save                0x1008FF77
#define XF86FcitxKey_Send                0x1008FF7B
#define XF86FcitxKey_Spell               0x1008FF7C
#define XF86FcitxKey_SplitScreen         0x1008FF7D
#define XF86FcitxKey_Support             0x1008FF7E
#define XF86FcitxKey_TaskPane            0x1008FF7F
#define XF86FcitxKey_Terminal            0x1008FF80
#define XF86FcitxKey_Tools               0x1008FF81
#define XF86FcitxKey_Travel              0x1008FF82
#define XF86FcitxKey_Video               0x1008FF87
#define XF86FcitxKey_Word                0x1008FF89
#define XF86FcitxKey_Xfer                0x1008FF8A
#define XF86FcitxKey_ZoomIn              0x1008FF8B
#define XF86FcitxKey_ZoomOut             0x1008FF8C
#define XF86FcitxKey_Away                0x1008FF8D
#define XF86FcitxKey_Messenger           0x1008FF8E
#define XF86FcitxKey_WebCam              0x1008FF8F
#define XF86FcitxKey_MailForward         0x1008FF90
#define XF86FcitxKey_Pictures            0x1008FF91
#define XF86FcitxKey_Music               0x1008FF92
#define XF86FcitxKey_Battery             0x1008FF93
#define XF86FcitxKey_Bluetooth           0x1008FF94
#define XF86FcitxKey_WLAN                0x1008FF95
#define XF86FcitxKey_UWB                 0x1008FF96
#define XF86FcitxKey_AudioForward        0x1008FF97
#define XF86FcitxKey_AudioRepeat         0x1008FF98
#define XF86FcitxKey_AudioRandomPlay     0x1008FF99
#define XF86FcitxKey_Subtitle            0x1008FF9A
#define XF86FcitxKey_AudioCycleTrack     0x1008FF9B
#define XF86FcitxKey_Time                0x1008FF9F
#define XF86FcitxKey_Select              0x1008FFA0
#define XF86FcitxKey_View                0x1008FFA1
#define XF86FcitxKey_TopMenu             0x1008FFA2
#define XF86FcitxKey_Suspend             0x1008FFA7
#define XF86FcitxKey_Hibernate           0x1008FFA8
#define XF86FcitxKey_TouchpadToggle      0x1008FFA9
#define XF86FcitxKey_TouchpadOn          0x1008FFB0
#define XF86FcitxKey_TouchpadOff         0x1008FFB1


// end of XF86keysyms.h

QT_BEGIN_NAMESPACE

// keyboard mapping table
static const unsigned int KeyTbl[] = {

    // misc keys

    FcitxKey_Escape,                  Qt::Key_Escape,
    FcitxKey_Tab,                     Qt::Key_Tab,
    FcitxKey_ISO_Left_Tab,            Qt::Key_Backtab,
    FcitxKey_BackSpace,               Qt::Key_Backspace,
    FcitxKey_Return,                  Qt::Key_Return,
    FcitxKey_Insert,                  Qt::Key_Insert,
    FcitxKey_Delete,                  Qt::Key_Delete,
    FcitxKey_Clear,                   Qt::Key_Delete,
    FcitxKey_Pause,                   Qt::Key_Pause,
    FcitxKey_Print,                   Qt::Key_Print,
    0x1005FF60,                 Qt::Key_SysReq,         // hardcoded Sun SysReq
    0x1007ff00,                 Qt::Key_SysReq,         // hardcoded X386 SysReq

    // cursor movement

    FcitxKey_Home,                    Qt::Key_Home,
    FcitxKey_End,                     Qt::Key_End,
    FcitxKey_Left,                    Qt::Key_Left,
    FcitxKey_Up,                      Qt::Key_Up,
    FcitxKey_Right,                   Qt::Key_Right,
    FcitxKey_Down,                    Qt::Key_Down,
    FcitxKey_Prior,                   Qt::Key_PageUp,
    FcitxKey_Next,                    Qt::Key_PageDown,

    // modifiers

    FcitxKey_Shift_L,                 Qt::Key_Shift,
    FcitxKey_Shift_R,                 Qt::Key_Shift,
    FcitxKey_Shift_Lock,              Qt::Key_Shift,
    FcitxKey_Control_L,               Qt::Key_Control,
    FcitxKey_Control_R,               Qt::Key_Control,
    FcitxKey_Meta_L,                  Qt::Key_Meta,
    FcitxKey_Meta_R,                  Qt::Key_Meta,
    FcitxKey_Alt_L,                   Qt::Key_Alt,
    FcitxKey_Alt_R,                   Qt::Key_Alt,
    FcitxKey_Caps_Lock,               Qt::Key_CapsLock,
    FcitxKey_Num_Lock,                Qt::Key_NumLock,
    FcitxKey_Scroll_Lock,             Qt::Key_ScrollLock,
    FcitxKey_Super_L,                 Qt::Key_Super_L,
    FcitxKey_Super_R,                 Qt::Key_Super_R,
    FcitxKey_Menu,                    Qt::Key_Menu,
    FcitxKey_Hyper_L,                 Qt::Key_Hyper_L,
    FcitxKey_Hyper_R,                 Qt::Key_Hyper_R,
    FcitxKey_Help,                    Qt::Key_Help,
    0x1000FF74,                 Qt::Key_Backtab,        // hardcoded HP backtab
    0x1005FF10,                 Qt::Key_F11,            // hardcoded Sun F36 (labeled F11)
    0x1005FF11,                 Qt::Key_F12,            // hardcoded Sun F37 (labeled F12)

    // numeric and function keypad keys

    FcitxKey_KP_Space,                Qt::Key_Space,
    FcitxKey_KP_Tab,                  Qt::Key_Tab,
    FcitxKey_KP_Enter,                Qt::Key_Enter,
    //FcitxKey_KP_F1,                 Qt::Key_F1,
    //FcitxKey_KP_F2,                 Qt::Key_F2,
    //FcitxKey_KP_F3,                 Qt::Key_F3,
    //FcitxKey_KP_F4,                 Qt::Key_F4,
    FcitxKey_KP_Home,                 Qt::Key_Home,
    FcitxKey_KP_Left,                 Qt::Key_Left,
    FcitxKey_KP_Up,                   Qt::Key_Up,
    FcitxKey_KP_Right,                Qt::Key_Right,
    FcitxKey_KP_Down,                 Qt::Key_Down,
    FcitxKey_KP_Prior,                Qt::Key_PageUp,
    FcitxKey_KP_Next,                 Qt::Key_PageDown,
    FcitxKey_KP_End,                  Qt::Key_End,
    FcitxKey_KP_Begin,                Qt::Key_Clear,
    FcitxKey_KP_Insert,               Qt::Key_Insert,
    FcitxKey_KP_Delete,               Qt::Key_Delete,
    FcitxKey_KP_Equal,                Qt::Key_Equal,
    FcitxKey_KP_Multiply,             Qt::Key_Asterisk,
    FcitxKey_KP_Add,                  Qt::Key_Plus,
    FcitxKey_KP_Separator,            Qt::Key_Comma,
    FcitxKey_KP_Subtract,             Qt::Key_Minus,
    FcitxKey_KP_Decimal,              Qt::Key_Period,
    FcitxKey_KP_Divide,               Qt::Key_Slash,

    // International input method support keys

    // International & multi-key character composition
    FcitxKey_ISO_Level3_Shift,        Qt::Key_AltGr,
    FcitxKey_Multi_key,       Qt::Key_Multi_key,
    FcitxKey_Codeinput,       Qt::Key_Codeinput,
    FcitxKey_SingleCandidate,     Qt::Key_SingleCandidate,
    FcitxKey_MultipleCandidate,   Qt::Key_MultipleCandidate,
    FcitxKey_PreviousCandidate,   Qt::Key_PreviousCandidate,

    // Misc Functions
    FcitxKey_Mode_switch,     Qt::Key_Mode_switch,
    FcitxKey_script_switch,       Qt::Key_Mode_switch,

    // Japanese keyboard support
    FcitxKey_Kanji,           Qt::Key_Kanji,
    FcitxKey_Muhenkan,        Qt::Key_Muhenkan,
    //FcitxKey_Henkan_Mode,       Qt::Key_Henkan_Mode,
    FcitxKey_Henkan_Mode,     Qt::Key_Henkan,
    FcitxKey_Henkan,          Qt::Key_Henkan,
    FcitxKey_Romaji,          Qt::Key_Romaji,
    FcitxKey_Hiragana,        Qt::Key_Hiragana,
    FcitxKey_Katakana,        Qt::Key_Katakana,
    FcitxKey_Hiragana_Katakana,   Qt::Key_Hiragana_Katakana,
    FcitxKey_Zenkaku,         Qt::Key_Zenkaku,
    FcitxKey_Hankaku,         Qt::Key_Hankaku,
    FcitxKey_Zenkaku_Hankaku,     Qt::Key_Zenkaku_Hankaku,
    FcitxKey_Touroku,         Qt::Key_Touroku,
    FcitxKey_Massyo,          Qt::Key_Massyo,
    FcitxKey_Kana_Lock,       Qt::Key_Kana_Lock,
    FcitxKey_Kana_Shift,      Qt::Key_Kana_Shift,
    FcitxKey_Eisu_Shift,      Qt::Key_Eisu_Shift,
    FcitxKey_Eisu_toggle,     Qt::Key_Eisu_toggle,
    //FcitxKey_Kanji_Bangou,      Qt::Key_Kanji_Bangou,
    //FcitxKey_Zen_Koho,      Qt::Key_Zen_Koho,
    //FcitxKey_Mae_Koho,      Qt::Key_Mae_Koho,
    FcitxKey_Kanji_Bangou,        Qt::Key_Codeinput,
    FcitxKey_Zen_Koho,        Qt::Key_MultipleCandidate,
    FcitxKey_Mae_Koho,        Qt::Key_PreviousCandidate,

#ifdef FcitxKey_KOREAN
    // Korean keyboard support
    FcitxKey_Hangul,          Qt::Key_Hangul,
    FcitxKey_Hangul_Start,        Qt::Key_Hangul_Start,
    FcitxKey_Hangul_End,      Qt::Key_Hangul_End,
    FcitxKey_Hangul_Hanja,        Qt::Key_Hangul_Hanja,
    FcitxKey_Hangul_Jamo,     Qt::Key_Hangul_Jamo,
    FcitxKey_Hangul_Romaja,       Qt::Key_Hangul_Romaja,
    //FcitxKey_Hangul_Codeinput,  Qt::Key_Hangul_Codeinput,
    FcitxKey_Hangul_Codeinput,    Qt::Key_Codeinput,
    FcitxKey_Hangul_Jeonja,       Qt::Key_Hangul_Jeonja,
    FcitxKey_Hangul_Banja,        Qt::Key_Hangul_Banja,
    FcitxKey_Hangul_PreHanja,     Qt::Key_Hangul_PreHanja,
    FcitxKey_Hangul_PostHanja,    Qt::Key_Hangul_PostHanja,
    //FcitxKey_Hangul_SingleCandidate,Qt::Key_Hangul_SingleCandidate,
    //FcitxKey_Hangul_MultipleCandidate,Qt::Key_Hangul_MultipleCandidate,
    //FcitxKey_Hangul_PreviousCandidate,Qt::Key_Hangul_PreviousCandidate,
    FcitxKey_Hangul_SingleCandidate,  Qt::Key_SingleCandidate,
    FcitxKey_Hangul_MultipleCandidate,Qt::Key_MultipleCandidate,
    FcitxKey_Hangul_PreviousCandidate,Qt::Key_PreviousCandidate,
    FcitxKey_Hangul_Special,      Qt::Key_Hangul_Special,
    //FcitxKey_Hangul_switch,     Qt::Key_Hangul_switch,
    FcitxKey_Hangul_switch,       Qt::Key_Mode_switch,
#endif  // FcitxKey_KOREAN

    // dead keys
    FcitxKey_dead_grave,              Qt::Key_Dead_Grave,
    FcitxKey_dead_acute,              Qt::Key_Dead_Acute,
    FcitxKey_dead_circumflex,         Qt::Key_Dead_Circumflex,
    FcitxKey_dead_tilde,              Qt::Key_Dead_Tilde,
    FcitxKey_dead_macron,             Qt::Key_Dead_Macron,
    FcitxKey_dead_breve,              Qt::Key_Dead_Breve,
    FcitxKey_dead_abovedot,           Qt::Key_Dead_Abovedot,
    FcitxKey_dead_diaeresis,          Qt::Key_Dead_Diaeresis,
    FcitxKey_dead_abovering,          Qt::Key_Dead_Abovering,
    FcitxKey_dead_doubleacute,        Qt::Key_Dead_Doubleacute,
    FcitxKey_dead_caron,              Qt::Key_Dead_Caron,
    FcitxKey_dead_cedilla,            Qt::Key_Dead_Cedilla,
    FcitxKey_dead_ogonek,             Qt::Key_Dead_Ogonek,
    FcitxKey_dead_iota,               Qt::Key_Dead_Iota,
    FcitxKey_dead_voiced_sound,       Qt::Key_Dead_Voiced_Sound,
    FcitxKey_dead_semivoiced_sound,   Qt::Key_Dead_Semivoiced_Sound,
    FcitxKey_dead_belowdot,           Qt::Key_Dead_Belowdot,
    FcitxKey_dead_hook,               Qt::Key_Dead_Hook,
    FcitxKey_dead_horn,               Qt::Key_Dead_Horn,

    // Special keys from X.org - This include multimedia keys,
    // wireless/bluetooth/uwb keys, special launcher keys, etc.
    XF86FcitxKey_Back,                Qt::Key_Back,
    XF86FcitxKey_Forward,             Qt::Key_Forward,
    XF86FcitxKey_Stop,                Qt::Key_Stop,
    XF86FcitxKey_Refresh,             Qt::Key_Refresh,
    XF86FcitxKey_Favorites,           Qt::Key_Favorites,
    XF86FcitxKey_AudioMedia,          Qt::Key_LaunchMedia,
    XF86FcitxKey_OpenURL,             Qt::Key_OpenUrl,
    XF86FcitxKey_HomePage,            Qt::Key_HomePage,
    XF86FcitxKey_Search,              Qt::Key_Search,
    XF86FcitxKey_AudioLowerVolume,    Qt::Key_VolumeDown,
    XF86FcitxKey_AudioMute,           Qt::Key_VolumeMute,
    XF86FcitxKey_AudioRaiseVolume,    Qt::Key_VolumeUp,
    XF86FcitxKey_AudioPlay,           Qt::Key_MediaPlay,
    XF86FcitxKey_AudioStop,           Qt::Key_MediaStop,
    XF86FcitxKey_AudioPrev,           Qt::Key_MediaPrevious,
    XF86FcitxKey_AudioNext,           Qt::Key_MediaNext,
    XF86FcitxKey_AudioRecord,         Qt::Key_MediaRecord,
    XF86FcitxKey_Mail,                Qt::Key_LaunchMail,
    XF86FcitxKey_MyComputer,          Qt::Key_Launch0,  // ### Qt 6: remap properly
    XF86FcitxKey_Calculator,          Qt::Key_Launch1,
    XF86FcitxKey_Memo,                Qt::Key_Memo,
    XF86FcitxKey_ToDoList,            Qt::Key_ToDoList,
    XF86FcitxKey_Calendar,            Qt::Key_Calendar,
    XF86FcitxKey_PowerDown,           Qt::Key_PowerDown,
    XF86FcitxKey_ContrastAdjust,      Qt::Key_ContrastAdjust,
    XF86FcitxKey_Standby,             Qt::Key_Standby,
    XF86FcitxKey_MonBrightnessUp,     Qt::Key_MonBrightnessUp,
    XF86FcitxKey_MonBrightnessDown,   Qt::Key_MonBrightnessDown,
    XF86FcitxKey_KbdLightOnOff,       Qt::Key_KeyboardLightOnOff,
    XF86FcitxKey_KbdBrightnessUp,     Qt::Key_KeyboardBrightnessUp,
    XF86FcitxKey_KbdBrightnessDown,   Qt::Key_KeyboardBrightnessDown,
    XF86FcitxKey_PowerOff,            Qt::Key_PowerOff,
    XF86FcitxKey_WakeUp,              Qt::Key_WakeUp,
    XF86FcitxKey_Eject,               Qt::Key_Eject,
    XF86FcitxKey_ScreenSaver,         Qt::Key_ScreenSaver,
    XF86FcitxKey_WWW,                 Qt::Key_WWW,
    XF86FcitxKey_Sleep,               Qt::Key_Sleep,
    XF86FcitxKey_LightBulb,           Qt::Key_LightBulb,
    XF86FcitxKey_Shop,                Qt::Key_Shop,
    XF86FcitxKey_History,             Qt::Key_History,
    XF86FcitxKey_AddFavorite,         Qt::Key_AddFavorite,
    XF86FcitxKey_HotLinks,            Qt::Key_HotLinks,
    XF86FcitxKey_BrightnessAdjust,    Qt::Key_BrightnessAdjust,
    XF86FcitxKey_Finance,             Qt::Key_Finance,
    XF86FcitxKey_Community,           Qt::Key_Community,
    XF86FcitxKey_AudioRewind,         Qt::Key_AudioRewind,
    XF86FcitxKey_BackForward,         Qt::Key_BackForward,
    XF86FcitxKey_ApplicationLeft,     Qt::Key_ApplicationLeft,
    XF86FcitxKey_ApplicationRight,    Qt::Key_ApplicationRight,
    XF86FcitxKey_Book,                Qt::Key_Book,
    XF86FcitxKey_CD,                  Qt::Key_CD,
    XF86FcitxKey_Calculater,          Qt::Key_Calculator,
    XF86FcitxKey_Clear,               Qt::Key_Clear,
    XF86FcitxKey_ClearGrab,           Qt::Key_ClearGrab,
    XF86FcitxKey_Close,               Qt::Key_Close,
    XF86FcitxKey_Copy,                Qt::Key_Copy,
    XF86FcitxKey_Cut,                 Qt::Key_Cut,
    XF86FcitxKey_Display,             Qt::Key_Display,
    XF86FcitxKey_DOS,                 Qt::Key_DOS,
    XF86FcitxKey_Documents,           Qt::Key_Documents,
    XF86FcitxKey_Excel,               Qt::Key_Excel,
    XF86FcitxKey_Explorer,            Qt::Key_Explorer,
    XF86FcitxKey_Game,                Qt::Key_Game,
    XF86FcitxKey_Go,                  Qt::Key_Go,
    XF86FcitxKey_iTouch,              Qt::Key_iTouch,
    XF86FcitxKey_LogOff,              Qt::Key_LogOff,
    XF86FcitxKey_Market,              Qt::Key_Market,
    XF86FcitxKey_Meeting,             Qt::Key_Meeting,
    XF86FcitxKey_MenuKB,              Qt::Key_MenuKB,
    XF86FcitxKey_MenuPB,              Qt::Key_MenuPB,
    XF86FcitxKey_MySites,             Qt::Key_MySites,
    XF86FcitxKey_News,                Qt::Key_News,
    XF86FcitxKey_OfficeHome,          Qt::Key_OfficeHome,
    XF86FcitxKey_Option,              Qt::Key_Option,
    XF86FcitxKey_Paste,               Qt::Key_Paste,
    XF86FcitxKey_Phone,               Qt::Key_Phone,
    XF86FcitxKey_Reply,               Qt::Key_Reply,
    XF86FcitxKey_Reload,              Qt::Key_Reload,
    XF86FcitxKey_RotateWindows,       Qt::Key_RotateWindows,
    XF86FcitxKey_RotationPB,          Qt::Key_RotationPB,
    XF86FcitxKey_RotationKB,          Qt::Key_RotationKB,
    XF86FcitxKey_Save,                Qt::Key_Save,
    XF86FcitxKey_Send,                Qt::Key_Send,
    XF86FcitxKey_Spell,               Qt::Key_Spell,
    XF86FcitxKey_SplitScreen,         Qt::Key_SplitScreen,
    XF86FcitxKey_Support,             Qt::Key_Support,
    XF86FcitxKey_TaskPane,            Qt::Key_TaskPane,
    XF86FcitxKey_Terminal,            Qt::Key_Terminal,
    XF86FcitxKey_Tools,               Qt::Key_Tools,
    XF86FcitxKey_Travel,              Qt::Key_Travel,
    XF86FcitxKey_Video,               Qt::Key_Video,
    XF86FcitxKey_Word,                Qt::Key_Word,
    XF86FcitxKey_Xfer,                Qt::Key_Xfer,
    XF86FcitxKey_ZoomIn,              Qt::Key_ZoomIn,
    XF86FcitxKey_ZoomOut,             Qt::Key_ZoomOut,
    XF86FcitxKey_Away,                Qt::Key_Away,
    XF86FcitxKey_Messenger,           Qt::Key_Messenger,
    XF86FcitxKey_WebCam,              Qt::Key_WebCam,
    XF86FcitxKey_MailForward,         Qt::Key_MailForward,
    XF86FcitxKey_Pictures,            Qt::Key_Pictures,
    XF86FcitxKey_Music,               Qt::Key_Music,
    XF86FcitxKey_Battery,             Qt::Key_Battery,
    XF86FcitxKey_Bluetooth,           Qt::Key_Bluetooth,
    XF86FcitxKey_WLAN,                Qt::Key_WLAN,
    XF86FcitxKey_UWB,                 Qt::Key_UWB,
    XF86FcitxKey_AudioForward,        Qt::Key_AudioForward,
    XF86FcitxKey_AudioRepeat,         Qt::Key_AudioRepeat,
    XF86FcitxKey_AudioRandomPlay,     Qt::Key_AudioRandomPlay,
    XF86FcitxKey_Subtitle,            Qt::Key_Subtitle,
    XF86FcitxKey_AudioCycleTrack,     Qt::Key_AudioCycleTrack,
    XF86FcitxKey_Time,                Qt::Key_Time,
    XF86FcitxKey_Select,              Qt::Key_Select,
    XF86FcitxKey_View,                Qt::Key_View,
    XF86FcitxKey_TopMenu,             Qt::Key_TopMenu,
    XF86FcitxKey_Bluetooth,           Qt::Key_Bluetooth,
    XF86FcitxKey_Suspend,             Qt::Key_Suspend,
    XF86FcitxKey_Hibernate,           Qt::Key_Hibernate,
    XF86FcitxKey_Launch0,             Qt::Key_Launch2, // ### Qt 6: remap properly
    XF86FcitxKey_Launch1,             Qt::Key_Launch3,
    XF86FcitxKey_Launch2,             Qt::Key_Launch4,
    XF86FcitxKey_Launch3,             Qt::Key_Launch5,
    XF86FcitxKey_Launch4,             Qt::Key_Launch6,
    XF86FcitxKey_Launch5,             Qt::Key_Launch7,
    XF86FcitxKey_Launch6,             Qt::Key_Launch8,
    XF86FcitxKey_Launch7,             Qt::Key_Launch9,
    XF86FcitxKey_Launch8,             Qt::Key_LaunchA,
    XF86FcitxKey_Launch9,             Qt::Key_LaunchB,
    XF86FcitxKey_LaunchA,             Qt::Key_LaunchC,
    XF86FcitxKey_LaunchB,             Qt::Key_LaunchD,
    XF86FcitxKey_LaunchC,             Qt::Key_LaunchE,
    XF86FcitxKey_LaunchD,             Qt::Key_LaunchF,
    XF86FcitxKey_LaunchE,             Qt::Key_LaunchG,
    XF86FcitxKey_LaunchF,             Qt::Key_LaunchH,

    0,                          0
};

static const unsigned short katakanaKeysymsToUnicode[] = {
    0x0000, 0x3002, 0x300C, 0x300D, 0x3001, 0x30FB, 0x30F2, 0x30A1,
    0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30C3,
    0x30FC, 0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD,
    0x30AF, 0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD,
    0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC,
    0x30CD, 0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE,
    0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9,
    0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3, 0x309B, 0x309C
};

static const unsigned short cyrillicKeysymsToUnicode[] = {
    0x0000, 0x0452, 0x0453, 0x0451, 0x0454, 0x0455, 0x0456, 0x0457,
    0x0458, 0x0459, 0x045a, 0x045b, 0x045c, 0x0000, 0x045e, 0x045f,
    0x2116, 0x0402, 0x0403, 0x0401, 0x0404, 0x0405, 0x0406, 0x0407,
    0x0408, 0x0409, 0x040a, 0x040b, 0x040c, 0x0000, 0x040e, 0x040f,
    0x044e, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433,
    0x0445, 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e,
    0x043f, 0x044f, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432,
    0x044c, 0x044b, 0x0437, 0x0448, 0x044d, 0x0449, 0x0447, 0x044a,
    0x042e, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
    0x0425, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e,
    0x041f, 0x042f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412,
    0x042c, 0x042b, 0x0417, 0x0428, 0x042d, 0x0429, 0x0427, 0x042a
};

static const unsigned short greekKeysymsToUnicode[] = {
    0x0000, 0x0386, 0x0388, 0x0389, 0x038a, 0x03aa, 0x0000, 0x038c,
    0x038e, 0x03ab, 0x0000, 0x038f, 0x0000, 0x0000, 0x0385, 0x2015,
    0x0000, 0x03ac, 0x03ad, 0x03ae, 0x03af, 0x03ca, 0x0390, 0x03cc,
    0x03cd, 0x03cb, 0x03b0, 0x03ce, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397,
    0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x039e, 0x039f,
    0x03a0, 0x03a1, 0x03a3, 0x0000, 0x03a4, 0x03a5, 0x03a6, 0x03a7,
    0x03a8, 0x03a9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x03b1, 0x03b2, 0x03b3, 0x03b4, 0x03b5, 0x03b6, 0x03b7,
    0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03be, 0x03bf,
    0x03c0, 0x03c1, 0x03c3, 0x03c2, 0x03c4, 0x03c5, 0x03c6, 0x03c7,
    0x03c8, 0x03c9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

static const unsigned short technicalKeysymsToUnicode[] = {
    0x0000, 0x23B7, 0x250C, 0x2500, 0x2320, 0x2321, 0x2502, 0x23A1,
    0x23A3, 0x23A4, 0x23A6, 0x239B, 0x239D, 0x239E, 0x23A0, 0x23A8,
    0x23AC, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x2264, 0x2260, 0x2265, 0x222B,
    0x2234, 0x221D, 0x221E, 0x0000, 0x0000, 0x2207, 0x0000, 0x0000,
    0x223C, 0x2243, 0x0000, 0x0000, 0x0000, 0x21D4, 0x21D2, 0x2261,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x221A, 0x0000,
    0x0000, 0x0000, 0x2282, 0x2283, 0x2229, 0x222A, 0x2227, 0x2228,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2202,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0192, 0x0000,
    0x0000, 0x0000, 0x0000, 0x2190, 0x2191, 0x2192, 0x2193, 0x0000
};

static const unsigned short specialKeysymsToUnicode[] = {
    0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x0000, 0x0000,
    0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA,
    0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C,
    0x2502, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

static const unsigned short publishingKeysymsToUnicode[] = {
    0x0000, 0x2003, 0x2002, 0x2004, 0x2005, 0x2007, 0x2008, 0x2009,
    0x200a, 0x2014, 0x2013, 0x0000, 0x0000, 0x0000, 0x2026, 0x2025,
    0x2153, 0x2154, 0x2155, 0x2156, 0x2157, 0x2158, 0x2159, 0x215a,
    0x2105, 0x0000, 0x0000, 0x2012, 0x2329, 0x0000, 0x232a, 0x0000,
    0x0000, 0x0000, 0x0000, 0x215b, 0x215c, 0x215d, 0x215e, 0x0000,
    0x0000, 0x2122, 0x2613, 0x0000, 0x25c1, 0x25b7, 0x25cb, 0x25af,
    0x2018, 0x2019, 0x201c, 0x201d, 0x211e, 0x0000, 0x2032, 0x2033,
    0x0000, 0x271d, 0x0000, 0x25ac, 0x25c0, 0x25b6, 0x25cf, 0x25ae,
    0x25e6, 0x25ab, 0x25ad, 0x25b3, 0x25bd, 0x2606, 0x2022, 0x25aa,
    0x25b2, 0x25bc, 0x261c, 0x261e, 0x2663, 0x2666, 0x2665, 0x0000,
    0x2720, 0x2020, 0x2021, 0x2713, 0x2717, 0x266f, 0x266d, 0x2642,
    0x2640, 0x260e, 0x2315, 0x2117, 0x2038, 0x201a, 0x201e, 0x0000
};

static const unsigned short aplKeysymsToUnicode[] = {
    0x0000, 0x0000, 0x0000, 0x003c, 0x0000, 0x0000, 0x003e, 0x0000,
    0x2228, 0x2227, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x00af, 0x0000, 0x22a5, 0x2229, 0x230a, 0x0000, 0x005f, 0x0000,
    0x0000, 0x0000, 0x2218, 0x0000, 0x2395, 0x0000, 0x22a4, 0x25cb,
    0x0000, 0x0000, 0x0000, 0x2308, 0x0000, 0x0000, 0x222a, 0x0000,
    0x2283, 0x0000, 0x2282, 0x0000, 0x22a2, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x22a3, 0x0000, 0x0000, 0x0000
};

static const unsigned short koreanKeysymsToUnicode[] = {
    0x0000, 0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137,
    0x3138, 0x3139, 0x313a, 0x313b, 0x313c, 0x313d, 0x313e, 0x313f,
    0x3140, 0x3141, 0x3142, 0x3143, 0x3144, 0x3145, 0x3146, 0x3147,
    0x3148, 0x3149, 0x314a, 0x314b, 0x314c, 0x314d, 0x314e, 0x314f,
    0x3150, 0x3151, 0x3152, 0x3153, 0x3154, 0x3155, 0x3156, 0x3157,
    0x3158, 0x3159, 0x315a, 0x315b, 0x315c, 0x315d, 0x315e, 0x315f,
    0x3160, 0x3161, 0x3162, 0x3163, 0x11a8, 0x11a9, 0x11aa, 0x11ab,
    0x11ac, 0x11ad, 0x11ae, 0x11af, 0x11b0, 0x11b1, 0x11b2, 0x11b3,
    0x11b4, 0x11b5, 0x11b6, 0x11b7, 0x11b8, 0x11b9, 0x11ba, 0x11bb,
    0x11bc, 0x11bd, 0x11be, 0x11bf, 0x11c0, 0x11c1, 0x11c2, 0x316d,
    0x3171, 0x3178, 0x317f, 0x3181, 0x3184, 0x3186, 0x318d, 0x318e,
    0x11eb, 0x11f0, 0x11f9, 0x0000, 0x0000, 0x0000, 0x0000, 0x20a9
};

static QChar keysymToUnicode(unsigned char byte3, unsigned char byte4)
{
    switch (byte3) {
    case 0x04:
        // katakana
        if (byte4 > 0xa0 && byte4 < 0xe0)
            return QChar(katakanaKeysymsToUnicode[byte4 - 0xa0]);
        else if (byte4 == 0x7e)
            return QChar(0x203e); // Overline
        break;
    case 0x06:
        // russian, use lookup table
        if (byte4 > 0xa0)
            return QChar(cyrillicKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x07:
        // greek
        if (byte4 > 0xa0)
            return QChar(greekKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x08:
        // technical
        if (byte4 > 0xa0)
            return QChar(technicalKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x09:
        // special
        if (byte4 >= 0xe0)
            return QChar(specialKeysymsToUnicode[byte4 - 0xe0]);
        break;
    case 0x0a:
        // publishing
        if (byte4 > 0xa0)
            return QChar(publishingKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x0b:
        // APL
        if (byte4 > 0xa0)
            return QChar(aplKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x0e:
        // Korean
        if (byte4 > 0xa0)
            return QChar(koreanKeysymsToUnicode[byte4 - 0xa0]);
        break;
    default:
        break;
    }
    return QChar(0x0);
}

int translateKeySym(uint key)
{
    int code = -1;
    int i = 0;                                // any other keys
    while (KeyTbl[i]) {
        if (key == KeyTbl[i]) {
            code = (int)KeyTbl[i+1];
            break;
        }
        i += 2;
    }
    return code;
}

QString translateKeySym(int keysym, uint xmodifiers,
                        int &code, Qt::KeyboardModifiers &modifiers,
                        QByteArray &chars, int &count)
{
    // all keysyms smaller than 0xff00 are actally keys that can be mapped to unicode chars

    QTextCodec *mapper = QTextCodec::codecForLocale();
    QChar converted;

    if (/*count == 0 &&*/ keysym < 0xff00) {
        unsigned char byte3 = (unsigned char)(keysym >> 8);
        int mib = -1;
        switch(byte3) {
        case 0: // Latin 1
        case 1: // Latin 2
        case 2: //latin 3
        case 3: // latin4
            mib = byte3 + 4;
            break;
        case 5: // arabic
            mib = 82;
            break;
        case 12: // Hebrew
            mib = 85;
            break;
        case 13: // Thai
            mib = 2259;
            break;
        case 4: // kana
        case 6: // cyrillic
        case 7: // greek
        case 8: // technical, no mapping here at the moment
        case 9: // Special
        case 10: // Publishing
        case 11: // APL
        case 14: // Korean, no mapping
            mib = -1; // manual conversion
            mapper= 0;
            converted = keysymToUnicode(byte3, keysym & 0xff);
            break;
        case 0x20:
            // currency symbols
            if (keysym >= 0x20a0 && keysym <= 0x20ac) {
                mib = -1; // manual conversion
                mapper = 0;
                converted = (uint)keysym;
            }
            break;
        default:
            break;
        }
        if (mib != -1) {
            mapper = QTextCodec::codecForMib(mib);
            if (chars.isEmpty())
                chars.resize(1);
            chars[0] = (unsigned char) (keysym & 0xff); // get only the fourth bit for conversion later
            count = 1;
        }
    } else if (keysym >= 0x1000000 && keysym <= 0x100ffff) {
        converted = (ushort) (keysym - 0x1000000);
        mapper = 0;
    }
    if (count < (int)chars.size()-1)
        chars[count] = '\0';

    QString text;
    if (!mapper && converted.unicode() != 0x0) {
        text = converted;
    } else if (!chars.isEmpty()) {
        // convert chars (8bit) to text (unicode).
        if (mapper)
            text = mapper->toUnicode(chars.constData(), count, 0);
        if (text.isEmpty()) {
            // no mapper, or codec couldn't convert to unicode (this
            // can happen when running in the C locale or with no LANG
            // set). try converting from latin-1
            text = QString::fromLatin1(chars);
        }
    }

    if (xmodifiers & FcitxKeyState_Alt) {
        modifiers |= Qt::AltModifier;
    }

    if (xmodifiers & FcitxKeyState_Shift) {
        modifiers |= Qt::ShiftModifier;
    }

    if (xmodifiers & FcitxKeyState_Ctrl) {
        modifiers |= Qt::ControlModifier;
    }

    if (xmodifiers & FcitxKeyState_Super) {
        modifiers |= Qt::MetaModifier;
    }

    // Commentary in X11/keysymdef says that X codes match ASCII, so it
    // is safe to use the locale functions to process X codes in ISO8859-1.
    //
    // This is mainly for compatibility - applications should not use the
    // Qt keycodes between 128 and 255, but should rather use the
    // QKeyEvent::text().
    //
    if (keysym < 128 || (keysym < 256 && (!mapper || mapper->mibEnum()==4))) {
        // upper-case key, if known
        code = isprint((int)keysym) ? toupper((int)keysym) : 0;
    } else if (keysym >= FcitxKey_F1 && keysym <= FcitxKey_F35) {
        // function keys
        code = Qt::Key_F1 + ((int)keysym - FcitxKey_F1);
    } else if (keysym >= FcitxKey_KP_Space && keysym <= FcitxKey_KP_9) {
        if (keysym >= FcitxKey_KP_0) {
            // numeric keypad keys
            code = Qt::Key_0 + ((int)keysym - FcitxKey_KP_0);
        } else {
            code = translateKeySym(keysym);
        }
        modifiers |= Qt::KeypadModifier;
    } else if (text.length() == 1 && text.unicode()->unicode() > 0x1f && text.unicode()->unicode() != 0x7f && !(keysym >= FcitxKey_dead_grave && keysym <= FcitxKey_dead_horn)) {
        code = text.unicode()->toUpper().unicode();
    } else {
        // any other keys
        code = translateKeySym(keysym);

        if (code == Qt::Key_Tab && (modifiers & Qt::ShiftModifier)) {
            // map shift+tab to shift+backtab, QShortcutMap knows about it
            // and will handle it.
            code = Qt::Key_Backtab;
            text = QString();
        }
    }

    return text;
}

bool symToKeyQt(int sym, unsigned int state, int& qtcode, Qt::KeyboardModifiers& mod)
{
    QByteArray chars;
    int count = 0;

    translateKeySym(sym, state, qtcode, mod, chars, count);

    return qtcode >= 0;
}

bool keyQtToSym(int qtcode, Qt::KeyboardModifiers mod, int& sym, unsigned int& state)
{
    qEventToSym(qtcode, QString(), mod, sym, state);

    return sym >= 0;
}
