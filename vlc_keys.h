/*****************************************************************************
 * vlc_keys.h: keycode defines
 *****************************************************************************
 * Copyright (C) 2003-2009 VLC authors and VideoLAN
 * $Id: c46b19ca410a754443d7e766290766e6d52563fc $
 *
 * Authors: Sigmund Augdal Helberg <dnumgis@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_KEYS_H
#define VLC_KEYS_H 1

/**
 * \file
 * This file defines keys and functions
 */

#define KEY_MODIFIER         0xFF000000
#define KEY_MODIFIER_ALT     0x01000000
#define KEY_MODIFIER_SHIFT   0x02000000
#define KEY_MODIFIER_CTRL    0x04000000
#define KEY_MODIFIER_META    0x08000000
#define KEY_MODIFIER_COMMAND 0x10000000

#define KEY_UNSET            0x00000000
#define KEY_BACKSPACE              0x08
#define KEY_TAB                    0x09
#define KEY_ENTER                  0x0D
#define KEY_ESC                    0x1B
/* End of Unicode range:     0x0010FFFF */
#define KEY_LEFT             0x00210000
#define KEY_RIGHT            0x00220000
#define KEY_UP               0x00230000
#define KEY_DOWN             0x00240000
#define KEY_F1               0x00270000
#define KEY_F2               0x00280000
#define KEY_F3               0x00290000
#define KEY_F4               0x002A0000
#define KEY_F5               0x002B0000
#define KEY_F6               0x002C0000
#define KEY_F7               0x002D0000
#define KEY_F8               0x002E0000
#define KEY_F9               0x002F0000
#define KEY_F10              0x00300000
#define KEY_F11              0x00310000
#define KEY_F12              0x00320000
#define KEY_HOME             0x00330000
#define KEY_END              0x00340000
#define KEY_INSERT           0x00350000
#define KEY_DELETE           0x00360000
#define KEY_MENU             0x00370000
#define KEY_PAGEUP           0x00390000
#define KEY_PAGEDOWN         0x003A0000

#define KEY_BROWSER_BACK     0x003F0000
#define KEY_BROWSER_FORWARD  0x00400000
#define KEY_BROWSER_REFRESH  0x00410000
#define KEY_BROWSER_STOP     0x00420000
#define KEY_BROWSER_SEARCH   0x00430000
#define KEY_BROWSER_FAVORITES 0x00440000
#define KEY_BROWSER_HOME     0x00450000
#define KEY_VOLUME_MUTE      0x00460000
#define KEY_VOLUME_DOWN      0x00470000
#define KEY_VOLUME_UP        0x00480000
#define KEY_MEDIA_NEXT_TRACK 0x00490000
#define KEY_MEDIA_PREV_TRACK 0x004A0000
#define KEY_MEDIA_STOP       0x004B0000
#define KEY_MEDIA_PLAY_PAUSE 0x004C0000

#define KEY_MOUSEWHEELUP     0x00F00000
#define KEY_MOUSEWHEELDOWN   0x00F10000
#define KEY_MOUSEWHEELLEFT   0x00F20000
#define KEY_MOUSEWHEELRIGHT  0x00F30000

typedef enum vlc_action {
    ACTIONID_NONE = 0,
    ACTIONID_QUIT,
    ACTIONID_PLAY_PAUSE,
    ACTIONID_PLAY,
    ACTIONID_PAUSE,
    ACTIONID_STOP,
    ACTIONID_PREV,
    ACTIONID_NEXT,
    ACTIONID_SLOWER,
    ACTIONID_FASTER,
    ACTIONID_TOGGLE_FULLSCREEN,
    ACTIONID_VOL_UP,
    ACTIONID_VOL_DOWN,
    ACTIONID_NAV_ACTIVATE,
    ACTIONID_NAV_UP,
    ACTIONID_NAV_DOWN,
    ACTIONID_NAV_LEFT,
    ACTIONID_NAV_RIGHT,
    ACTIONID_JUMP_BACKWARD_EXTRASHORT,
    ACTIONID_JUMP_FORWARD_EXTRASHORT,
    ACTIONID_JUMP_BACKWARD_SHORT,
    ACTIONID_JUMP_FORWARD_SHORT,
    ACTIONID_JUMP_BACKWARD_MEDIUM,
    ACTIONID_JUMP_FORWARD_MEDIUM,
    ACTIONID_JUMP_BACKWARD_LONG,
    ACTIONID_JUMP_FORWARD_LONG,
    ACTIONID_FRAME_NEXT,
    ACTIONID_POSITION,
    ACTIONID_VOL_MUTE,
/* let ACTIONID_SET_BOOMARK* and ACTIONID_PLAY_BOOKMARK* be contiguous */
    ACTIONID_SET_BOOKMARK1,
    ACTIONID_SET_BOOKMARK2,
    ACTIONID_SET_BOOKMARK3,
    ACTIONID_SET_BOOKMARK4,
    ACTIONID_SET_BOOKMARK5,
    ACTIONID_SET_BOOKMARK6,
    ACTIONID_SET_BOOKMARK7,
    ACTIONID_SET_BOOKMARK8,
    ACTIONID_SET_BOOKMARK9,
    ACTIONID_SET_BOOKMARK10,
    ACTIONID_PLAY_BOOKMARK1,
    ACTIONID_PLAY_BOOKMARK2,
    ACTIONID_PLAY_BOOKMARK3,
    ACTIONID_PLAY_BOOKMARK4,
    ACTIONID_PLAY_BOOKMARK5,
    ACTIONID_PLAY_BOOKMARK6,
    ACTIONID_PLAY_BOOKMARK7,
    ACTIONID_PLAY_BOOKMARK8,
    ACTIONID_PLAY_BOOKMARK9,
    ACTIONID_PLAY_BOOKMARK10,
    /* end of contiguous zone */
    ACTIONID_SUBDELAY_UP,
    ACTIONID_SUBDELAY_DOWN,
    ACTIONID_SUBPOS_UP,
    ACTIONID_SUBPOS_DOWN,
    ACTIONID_AUDIO_TRACK,
    ACTIONID_SUBTITLE_TRACK,
    ACTIONID_INTF_TOGGLE_FSC,
    ACTIONID_INTF_HIDE,
    ACTIONID_INTF_BOSS,
    /* chapter and title navigation */
    ACTIONID_TITLE_PREV,
    ACTIONID_TITLE_NEXT,
    ACTIONID_CHAPTER_PREV,
    ACTIONID_CHAPTER_NEXT,
    /* end of chapter and title navigation */
    ACTIONID_AUDIODELAY_UP,
    ACTIONID_AUDIODELAY_DOWN,
    ACTIONID_SNAPSHOT,
    ACTIONID_RECORD,
    ACTIONID_DISC_MENU,
    ACTIONID_ASPECT_RATIO,
    ACTIONID_CROP,
    ACTIONID_DEINTERLACE,
    ACTIONID_ZOOM,
    ACTIONID_UNZOOM,
    ACTIONID_CROP_TOP,
    ACTIONID_UNCROP_TOP,
    ACTIONID_CROP_LEFT,
    ACTIONID_UNCROP_LEFT,
    ACTIONID_CROP_BOTTOM,
    ACTIONID_UNCROP_BOTTOM,
    ACTIONID_CROP_RIGHT,
    ACTIONID_UNCROP_RIGHT,
    ACTIONID_RANDOM,
    ACTIONID_LOOP,
    ACTIONID_WALLPAPER,
    ACTIONID_LEAVE_FULLSCREEN,
    ACTIONID_MENU_ON,
    ACTIONID_MENU_OFF,
    ACTIONID_MENU_RIGHT,
    ACTIONID_MENU_LEFT,
    ACTIONID_MENU_UP,
    ACTIONID_MENU_DOWN,
    ACTIONID_MENU_SELECT,
    /* Zoom */
    ACTIONID_ZOOM_QUARTER,
    ACTIONID_ZOOM_HALF,
    ACTIONID_ZOOM_ORIGINAL,
    ACTIONID_ZOOM_DOUBLE,
    /* Cycle Through Audio Devices */
    ACTIONID_AUDIODEVICE_CYCLE,
    /* scaling */
    ACTIONID_TOGGLE_AUTOSCALE,
    ACTIONID_SCALE_UP,
    ACTIONID_SCALE_DOWN,
    /* */
    ACTIONID_RATE_NORMAL,
    ACTIONID_RATE_SLOWER_FINE,
    ACTIONID_RATE_FASTER_FINE,

} vlc_action_t;

#endif
