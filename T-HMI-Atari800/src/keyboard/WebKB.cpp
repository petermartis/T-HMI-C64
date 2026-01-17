/*
 Copyright (C) 2025  <uliuc@gmx.net >

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 3 of the License, or (at your
 option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 for more details.

 For the complete text of the GNU General Public License see
 http://www.gnu.org/licenses/.
*/

#include "WebKB.h"
#include "../Config.h"
#ifdef USE_WEB_KEYBOARD

#include "../ExtCmd.h"
#include "../platform/PlatformManager.h"
#include "AtariKeycodes.h"
#include "SDLKeymap.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <cctype>
#include <set>

// html/css/javascript web keyboard
#include "htmlcode.h"

static const char *TAG = "WEBKB";

// web conf portal
#define AP_SSID "T-HMI-Atari800"
#define AP_PASSWORD ""
#define DNS_PORT 53

const char *portalHTML = R"rawliteral(
	<!DOCTYPE html>
	<html>
	<head>
		<meta name="viewport" content="width=device-width, initial-scale=1">
		<meta charset="UTF-8">
		<title>C64 WLAN SETUP</title>
		<style>
			body { font-family: sans-serif; background:#f2f2f2; padding:20px; }
			h2 { text-align:center; }
			select, input, button {
			  box-sizing: border-box;	
			  width:100%; padding:12px; margin:10px 0;
			  border-radius:6px; border:1px solid #ccc;
			}
			button { background:#007bff; color:white; font-size:16px; }
		</style>
		</head>
		<body>
		<h2>SELECT WLAN:</h2>
		<form action="/save" method="POST" accept-charset="UTF-8">
			<select id="networks">%NETWORKS%</select>
			<input type="text" id="ssid" name="ssid" placeholder="NETWORK (SSID)" required>
			<input type="password" name="password" placeholder="PASSWORD">
			<button type="submit">CONNECT</button>
		</form>
		</body>
		<script>
			document.getElementById('networks').addEventListener('change', function () {
    		if (this.value) {
        		document.getElementById('ssid').value = this.value;
    		}
		});
		</script>
	</html>
)rawliteral";

const CodeTriple C64_KEYCODE_BREAK = {0x7f, 0x7f, 0x80};
const CodeTriple C64_KEYCODE_RETURN = {0xfe, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_BACKSPACE = {0xfe, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_SPACE = {0x7f, 0xef, 0x00};
const CodeTriple C64_KEYCODE_0 = {0xef, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_1 = {0x7f, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_2 = {0x7f, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_3 = {0xfd, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_4 = {0xfd, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_5 = {0xfb, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_6 = {0xfb, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_7 = {0xf7, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_8 = {0xf7, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_9 = {0xef, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_0_CTRL = {0xef, 0xf7, 0x02};
const CodeTriple C64_KEYCODE_1_CTRL = {0x7f, 0xfe, 0x02};
const CodeTriple C64_KEYCODE_2_CTRL = {0x7f, 0xf7, 0x02};
const CodeTriple C64_KEYCODE_3_CTRL = {0xfd, 0xfe, 0x02};
const CodeTriple C64_KEYCODE_4_CTRL = {0xfd, 0xf7, 0x02};
const CodeTriple C64_KEYCODE_5_CTRL = {0xfb, 0xfe, 0x02};
const CodeTriple C64_KEYCODE_6_CTRL = {0xfb, 0xf7, 0x02};
const CodeTriple C64_KEYCODE_7_CTRL = {0xf7, 0xfe, 0x02};
const CodeTriple C64_KEYCODE_8_CTRL = {0xf7, 0xf7, 0x02};
const CodeTriple C64_KEYCODE_9_CTRL = {0xef, 0xfe, 0x02};
const CodeTriple C64_KEYCODE_1_COMMODORE = {0x7f, 0xfe, 0x04};
const CodeTriple C64_KEYCODE_2_COMMODORE = {0x7f, 0xf7, 0x04};
const CodeTriple C64_KEYCODE_3_COMMODORE = {0xfd, 0xfe, 0x04};
const CodeTriple C64_KEYCODE_4_COMMODORE = {0xfd, 0xf7, 0x04};
const CodeTriple C64_KEYCODE_5_COMMODORE = {0xfb, 0xfe, 0x04};
const CodeTriple C64_KEYCODE_6_COMMODORE = {0xfb, 0xf7, 0x04};
const CodeTriple C64_KEYCODE_7_COMMODORE = {0xf7, 0xfe, 0x04};
const CodeTriple C64_KEYCODE_8_COMMODORE = {0xf7, 0xf7, 0x04};
const CodeTriple C64_KEYCODE_W = {0xfd, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_A = {0xfd, 0xfb, 0x00};
const CodeTriple C64_KEYCODE_Z = {0xfd, 0xef, 0x00};
const CodeTriple C64_KEYCODE_S = {0xfd, 0xdf, 0x00};
const CodeTriple C64_KEYCODE_E = {0xfd, 0xbf, 0x00};
const CodeTriple C64_KEYCODE_R = {0xfb, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_D = {0xfb, 0xfb, 0x00};
const CodeTriple C64_KEYCODE_C = {0xfb, 0xef, 0x00};
const CodeTriple C64_KEYCODE_F = {0xfb, 0xdf, 0x00};
const CodeTriple C64_KEYCODE_T = {0xfb, 0xbf, 0x00};
const CodeTriple C64_KEYCODE_X = {0xfb, 0x7f, 0x00};
const CodeTriple C64_KEYCODE_Y = {0xf7, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_G = {0xf7, 0xfb, 0x00};
const CodeTriple C64_KEYCODE_B = {0xf7, 0xef, 0x00};
const CodeTriple C64_KEYCODE_H = {0xf7, 0xdf, 0x00};
const CodeTriple C64_KEYCODE_U = {0xf7, 0xbf, 0x00};
const CodeTriple C64_KEYCODE_V = {0xf7, 0x7f, 0x00};
const CodeTriple C64_KEYCODE_I = {0xef, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_J = {0xef, 0xfb, 0x00};
const CodeTriple C64_KEYCODE_M = {0xef, 0xef, 0x00};
const CodeTriple C64_KEYCODE_K = {0xef, 0xdf, 0x00};
const CodeTriple C64_KEYCODE_O = {0xef, 0xbf, 0x00};
const CodeTriple C64_KEYCODE_N = {0xef, 0x7f, 0x00};
const CodeTriple C64_KEYCODE_P = {0xdf, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_L = {0xdf, 0xfb, 0x00};
const CodeTriple C64_KEYCODE_MINUS = {0xdf, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_PERIOD = {0xdf, 0xef, 0x00};
const CodeTriple C64_KEYCODE_COMMA = {0xdf, 0x7f, 0x00};
const CodeTriple C64_KEYCODE_Q = {0x7f, 0xbf, 0x00};
const CodeTriple C64_KEYCODE_DOLLAR = {0xfd, 0xf7, 0x01};
const CodeTriple C64_KEYCODE_LESS = {0xdf, 0x7f, 0x01};
const CodeTriple C64_KEYCODE_RIGHT = {0xfe, 0xfb, 0x00};
const CodeTriple C64_KEYCODE_LEFT = {0xfe, 0xfb, 0x01};
const CodeTriple C64_KEYCODE_DOWN = {0xfe, 0x7f, 0x00};
const CodeTriple C64_KEYCODE_UP = {0xfe, 0x7f, 0x01};
const CodeTriple C64_KEYCODE_F1 = {0xfe, 0xef, 0x00};
const CodeTriple C64_KEYCODE_F3 = {0xfe, 0xdf, 0x00};
const CodeTriple C64_KEYCODE_F5 = {0xfe, 0xbf, 0x00};
const CodeTriple C64_KEYCODE_F7 = {0xfe, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_F2 = {0xfe, 0xef, 0x01};
const CodeTriple C64_KEYCODE_F4 = {0xfe, 0xdf, 0x01};
const CodeTriple C64_KEYCODE_F6 = {0xfe, 0xbf, 0x01};
const CodeTriple C64_KEYCODE_F8 = {0xfe, 0xf7, 0x01};
const CodeTriple C64_KEYCODE_W_SHIFT = {0xfd, 0xfd, 0x01};
const CodeTriple C64_KEYCODE_A_SHIFT = {0xfd, 0xfb, 0x01};
const CodeTriple C64_KEYCODE_Z_SHIFT = {0xfd, 0xef, 0x01};
const CodeTriple C64_KEYCODE_S_SHIFT = {0xfd, 0xdf, 0x01};
const CodeTriple C64_KEYCODE_E_SHIFT = {0xfd, 0xbf, 0x01};
const CodeTriple C64_KEYCODE_R_SHIFT = {0xfb, 0xfd, 0x01};
const CodeTriple C64_KEYCODE_D_SHIFT = {0xfb, 0xfb, 0x01};
const CodeTriple C64_KEYCODE_C_SHIFT = {0xfb, 0xef, 0x01};
const CodeTriple C64_KEYCODE_F_SHIFT = {0xfb, 0xdf, 0x01};
const CodeTriple C64_KEYCODE_T_SHIFT = {0xfb, 0xbf, 0x01};
const CodeTriple C64_KEYCODE_X_SHIFT = {0xfb, 0x7f, 0x01};
const CodeTriple C64_KEYCODE_Y_SHIFT = {0xf7, 0xfd, 0x01};
const CodeTriple C64_KEYCODE_G_SHIFT = {0xf7, 0xfb, 0x01};
const CodeTriple C64_KEYCODE_B_SHIFT = {0xf7, 0xef, 0x01};
const CodeTriple C64_KEYCODE_H_SHIFT = {0xf7, 0xdf, 0x01};
const CodeTriple C64_KEYCODE_U_SHIFT = {0xf7, 0xbf, 0x01};
const CodeTriple C64_KEYCODE_V_SHIFT = {0xf7, 0x7f, 0x01};
const CodeTriple C64_KEYCODE_I_SHIFT = {0xef, 0xfd, 0x01};
const CodeTriple C64_KEYCODE_J_SHIFT = {0xef, 0xfb, 0x01};
const CodeTriple C64_KEYCODE_M_SHIFT = {0xef, 0xef, 0x01};
const CodeTriple C64_KEYCODE_K_SHIFT = {0xef, 0xdf, 0x01};
const CodeTriple C64_KEYCODE_O_SHIFT = {0xef, 0xbf, 0x01};
const CodeTriple C64_KEYCODE_N_SHIFT = {0xef, 0x7f, 0x01};
const CodeTriple C64_KEYCODE_P_SHIFT = {0xdf, 0xfd, 0x01};
const CodeTriple C64_KEYCODE_L_SHIFT = {0xdf, 0xfb, 0x01};
const CodeTriple C64_KEYCODE_Q_SHIFT = {0x7f, 0xbf, 0x01};
const CodeTriple C64_KEYCODE_QUOTEDBL = {0x7f, 0xf7, 0x01};
const CodeTriple C64_KEYCODE_PERCENT = {0xfb, 0xfe, 0x01};
const CodeTriple C64_KEYCODE_AMPERSAND = {0xfb, 0xf7, 0x01};
const CodeTriple C64_KEYCODE_LEFTPAREN = {0xf7, 0xf7, 0x01};
const CodeTriple C64_KEYCODE_RIGHTPAREN = {0xef, 0xfe, 0x01};
const CodeTriple C64_KEYCODE_GREATER = {0xdf, 0xef, 0x01};
const CodeTriple C64_KEYCODE_QUESTION = {0xbf, 0x7f, 0x01};
const CodeTriple C64_KEYCODE_PLUS = {0xdf, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_ASTERISK = {0xbf, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_SLASH = {0xbf, 0x7f, 0x00};
const CodeTriple C64_KEYCODE_EQUALS = {0xbf, 0xdf, 0x00};
const CodeTriple C64_KEYCODE_COLON = {0xdf, 0xdf, 0x00};
const CodeTriple C64_KEYCODE_SEMICOLON = {0xbf, 0xfb, 0x00};
const CodeTriple C64_KEYCODE_EXCLAIM = {0x7f, 0xfe, 0x01};
const CodeTriple C64_KEYCODE_AT = {0xdf, 0xbf, 0x00};
const CodeTriple C64_KEYCODE_HASH = {0xfd, 0xfe, 0x01};
const CodeTriple C64_KEYCODE_QUOTE = {0xf7, 0xfe, 0x01};
const CodeTriple C64_KEYCODE_HOME = {0xbf, 0xf7, 0x00};
const CodeTriple C64_KEYCODE_CLR = {0xbf, 0xf7, 0x01};
const CodeTriple C64_KEYCODE_DEL = {0xfe, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_INS = {0xfe, 0xfe, 0x01};
const CodeTriple C64_KEYCODE_LEFTBRACKET = {0xdf, 0xdf, 0x01};
const CodeTriple C64_KEYCODE_RIGHTBRACKET = {0xbf, 0xfb, 0x01};
const CodeTriple C64_KEYCODE_LEFTARROW = {0x7f, 0xfd, 0x00};
const CodeTriple C64_KEYCODE_POUND = {0xbf, 0xfe, 0x00};
const CodeTriple C64_KEYCODE_UPARROW = {0xbf, 0xbf, 0x00};

struct KeyMapEntry {
  const char *keyId;
  bool shift;
  bool ctrl;
  bool commodore;
  CodeTriple code;
};

// Vollständige Mapping-Tabelle nach deiner Liste
const KeyMapEntry c64KeyMap[] = {
    // ========================
    // characters
    // ========================
    {"char:a", false, false, false, {C64_KEYCODE_A}},
    {"char:A", true, false, false, {C64_KEYCODE_A_SHIFT}},
    {"char:b", false, false, false, {C64_KEYCODE_B}},
    {"char:B", true, false, false, {C64_KEYCODE_B_SHIFT}},
    {"char:c", false, false, false, {C64_KEYCODE_C}},
    {"char:C", true, false, false, {C64_KEYCODE_C_SHIFT}},
    {"char:d", false, false, false, {C64_KEYCODE_D}},
    {"char:D", true, false, false, {C64_KEYCODE_D_SHIFT}},
    {"char:e", false, false, false, {C64_KEYCODE_E}},
    {"char:E", true, false, false, {C64_KEYCODE_E_SHIFT}},
    {"char:f", false, false, false, {C64_KEYCODE_F}},
    {"char:F", true, false, false, {C64_KEYCODE_F_SHIFT}},
    {"char:g", false, false, false, {C64_KEYCODE_G}},
    {"char:G", true, false, false, {C64_KEYCODE_G_SHIFT}},
    {"char:h", false, false, false, {C64_KEYCODE_H}},
    {"char:H", true, false, false, {C64_KEYCODE_H_SHIFT}},
    {"char:i", false, false, false, {C64_KEYCODE_I}},
    {"char:I", true, false, false, {C64_KEYCODE_I_SHIFT}},
    {"char:j", false, false, false, {C64_KEYCODE_J}},
    {"char:J", true, false, false, {C64_KEYCODE_J_SHIFT}},
    {"char:k", false, false, false, {C64_KEYCODE_K}},
    {"char:K", true, false, false, {C64_KEYCODE_K_SHIFT}},
    {"char:l", false, false, false, {C64_KEYCODE_L}},
    {"char:L", true, false, false, {C64_KEYCODE_L_SHIFT}},
    {"char:m", false, false, false, {C64_KEYCODE_M}},
    {"char:M", true, false, false, {C64_KEYCODE_M_SHIFT}},
    {"char:n", false, false, false, {C64_KEYCODE_N}},
    {"char:N", true, false, false, {C64_KEYCODE_N_SHIFT}},
    {"char:o", false, false, false, {C64_KEYCODE_O}},
    {"char:O", true, false, false, {C64_KEYCODE_O_SHIFT}},
    {"char:p", false, false, false, {C64_KEYCODE_P}},
    {"char:P", true, false, false, {C64_KEYCODE_P_SHIFT}},
    {"char:q", false, false, false, {C64_KEYCODE_Q}},
    {"char:Q", true, false, false, {C64_KEYCODE_Q_SHIFT}},
    {"char:r", false, false, false, {C64_KEYCODE_R}},
    {"char:R", true, false, false, {C64_KEYCODE_R_SHIFT}},
    {"char:s", false, false, false, {C64_KEYCODE_S}},
    {"char:S", true, false, false, {C64_KEYCODE_S_SHIFT}},
    {"char:t", false, false, false, {C64_KEYCODE_T}},
    {"char:T", true, false, false, {C64_KEYCODE_T_SHIFT}},
    {"char:u", false, false, false, {C64_KEYCODE_U}},
    {"char:U", true, false, false, {C64_KEYCODE_U_SHIFT}},
    {"char:v", false, false, false, {C64_KEYCODE_V}},
    {"char:V", true, false, false, {C64_KEYCODE_V_SHIFT}},
    {"char:w", false, false, false, {C64_KEYCODE_W}},
    {"char:W", true, false, false, {C64_KEYCODE_W_SHIFT}},
    {"char:x", false, false, false, {C64_KEYCODE_X}},
    {"char:X", true, false, false, {C64_KEYCODE_X_SHIFT}},
    {"char:y", false, false, false, {C64_KEYCODE_Y}},
    {"char:Y", true, false, false, {C64_KEYCODE_Y_SHIFT}},
    {"char:z", false, false, false, {C64_KEYCODE_Z}},
    {"char:Z", true, false, false, {C64_KEYCODE_Z_SHIFT}},

    // ========================
    // numbers
    // ========================
    {"char:1", false, false, false, {C64_KEYCODE_1}},
    {"char:2", false, false, false, {C64_KEYCODE_2}},
    {"char:3", false, false, false, {C64_KEYCODE_3}},
    {"char:4", false, false, false, {C64_KEYCODE_4}},
    {"char:5", false, false, false, {C64_KEYCODE_5}},
    {"char:6", false, false, false, {C64_KEYCODE_6}},
    {"char:7", false, false, false, {C64_KEYCODE_7}},
    {"char:8", false, false, false, {C64_KEYCODE_8}},
    {"char:9", false, false, false, {C64_KEYCODE_9}},
    {"char:0", false, false, false, {C64_KEYCODE_0}},

    // shifted numbers
    {"char:!", false, false, false, {C64_KEYCODE_EXCLAIM}},    // !
    {"char:\"", false, false, false, {C64_KEYCODE_QUOTEDBL}},  // "
    {"char:#", false, false, false, {C64_KEYCODE_HASH}},       // #
    {"char:$", false, false, false, {C64_KEYCODE_DOLLAR}},     // $
    {"char:%", false, false, false, {C64_KEYCODE_PERCENT}},    // %
    {"char:&", false, false, false, {C64_KEYCODE_AMPERSAND}},  // &
    {"char:'", false, false, false, {C64_KEYCODE_QUOTE}},      // '
    {"char:(", false, false, false, {C64_KEYCODE_LEFTPAREN}},  // (
    {"char:)", false, false, false, {C64_KEYCODE_RIGHTPAREN}}, // )

    // ========================
    // special keys
    // ========================
    {"char:Enter", false, false, false, {C64_KEYCODE_RETURN}},
    {"char:Delete", false, false, false, {C64_KEYCODE_DEL}},
    {"char:Insert", false, false, false, {C64_KEYCODE_INS}},
    {"char:Backspace", false, false, false, {C64_KEYCODE_BACKSPACE}},
    {"char:F12", false, false, false, {C64_KEYCODE_CLR}},
    {"char:Home", false, false, false, {C64_KEYCODE_HOME}},
    {"char:Escape", false, false, false, {C64_KEYCODE_BREAK}},
    {"char:°", false, false, false, {C64_KEYCODE_UPARROW}},
    {"char: ", false, false, false, {C64_KEYCODE_SPACE}},
    {"char:-", false, false, false, {C64_KEYCODE_MINUS}},
    {"char:+", false, false, false, {C64_KEYCODE_PLUS}},
    {"char:=", false, false, false, {C64_KEYCODE_EQUALS}},
    {"char:[", false, false, false, {C64_KEYCODE_LEFTBRACKET}},
    {"char:]", false, false, false, {C64_KEYCODE_RIGHTBRACKET}},
    {"char:;", false, false, false, {C64_KEYCODE_SEMICOLON}},
    {"char:,", false, false, false, {C64_KEYCODE_COMMA}},
    {"char:.", false, false, false, {C64_KEYCODE_PERIOD}},
    {"char:/", false, false, false, {C64_KEYCODE_SLASH}},
    {"char:>", false, false, false, {C64_KEYCODE_GREATER}},
    {"char:<", false, false, false, {C64_KEYCODE_LESS}},
    {"char:?", false, false, false, {C64_KEYCODE_QUESTION}},
    {"char:@", false, false, false, {C64_KEYCODE_AT}},
    {"char:*", false, false, false, {C64_KEYCODE_ASTERISK}},
    {"char:£", false, false, false, {C64_KEYCODE_POUND}},
    {"char::", false, false, false, {C64_KEYCODE_COLON}},

    // ========================
    // arrow keys
    // ========================
    {"char:ArrowRight", false, false, false, {C64_KEYCODE_RIGHT}},
    {"char:ArrowLeft", false, false, false, {C64_KEYCODE_LEFT}},
    {"char:ArrowDown", false, false, false, {C64_KEYCODE_DOWN}},
    {"char:ArrowUp", false, false, false, {C64_KEYCODE_UP}},

    // ========================
    // function keys
    // ========================
    {"char:F1", false, false, false, {C64_KEYCODE_F1}},
    {"char:F2", false, false, false, {C64_KEYCODE_F2}},
    {"char:F3", false, false, false, {C64_KEYCODE_F3}},
    {"char:F4", false, false, false, {C64_KEYCODE_F4}},
    {"char:F5", false, false, false, {C64_KEYCODE_F5}},
    {"char:F6", false, false, false, {C64_KEYCODE_F6}},
    {"char:F7", false, false, false, {C64_KEYCODE_F7}},
    {"char:F8", false, false, false, {C64_KEYCODE_F8}},
};

/**
 * @brief Convert web keyboard key ID to Atari key code
 * @param keyId Key identifier (e.g., "char:A", "char:Enter")
 * @param shift Shift modifier pressed
 * @param ctrl Control modifier pressed
 * @return Atari POKEY key code (0xFF = no key)
 */
static uint8_t keyIdToAtariCode(const char *keyId, bool shift, bool ctrl) {
  if (!keyId || strncmp(keyId, "char:", 5) != 0) {
    return 0xFF;
  }
  const char *key = keyId + 5;  // Skip "char:" prefix

  uint8_t code = 0xFF;

  // Single character keys
  if (strlen(key) == 1) {
    char c = key[0];
    // Letters
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
      code = asciiToAtariKey(c);
    }
    // Numbers
    else if (c >= '0' && c <= '9') {
      code = asciiToAtariKey(c);
    }
    // Punctuation
    else {
      code = asciiToAtariKey(c);
    }
  }
  // Special keys
  else if (strcmp(key, "Enter") == 0) {
    code = ATARI_KEY_RETURN;
  }
  else if (strcmp(key, "Backspace") == 0 || strcmp(key, "Delete") == 0) {
    code = ATARI_KEY_BACKSPACE;
  }
  else if (strcmp(key, "Escape") == 0) {
    code = ATARI_KEY_ESC;
  }
  else if (strcmp(key, "Tab") == 0) {
    code = ATARI_KEY_TAB;
  }
  else if (strcmp(key, "F1") == 0) {
    code = ATARI_KEY_F1;
  }
  else if (strcmp(key, "F2") == 0) {
    code = ATARI_KEY_F2;
  }
  else if (strcmp(key, "F3") == 0) {
    code = ATARI_KEY_F3;
  }
  else if (strcmp(key, "F4") == 0) {
    code = ATARI_KEY_F4;
  }
  else if (strcmp(key, "Help") == 0 || strcmp(key, "F5") == 0) {
    code = ATARI_KEY_HELP;
  }
  else if (strcmp(key, "ArrowUp") == 0) {
    code = ATARI_KEY_MINUS | ATARI_MOD_CONTROL;  // Ctrl+Minus = Up
  }
  else if (strcmp(key, "ArrowDown") == 0) {
    code = ATARI_KEY_EQUALS | ATARI_MOD_CONTROL; // Ctrl+Equals = Down
  }
  else if (strcmp(key, "ArrowLeft") == 0) {
    code = ATARI_KEY_PLUS | ATARI_MOD_CONTROL;   // Ctrl+Plus = Left
  }
  else if (strcmp(key, "ArrowRight") == 0) {
    code = ATARI_KEY_ASTERISK | ATARI_MOD_CONTROL; // Ctrl+Asterisk = Right
  }
  else if (strcmp(key, "Capslock") == 0) {
    code = ATARI_KEY_CAPS;
  }
  else if (strcmp(key, "Home") == 0) {
    code = ATARI_KEY_LESS | ATARI_MOD_CONTROL;   // Ctrl+< = Clear screen
  }

  if (code != 0xFF && code != ATARI_KEY_NONE) {
    // Add modifiers (but don't double-add if already included)
    if (shift && !(code & ATARI_MOD_SHIFT)) {
      code |= ATARI_MOD_SHIFT;
    }
    if (ctrl && !(code & ATARI_MOD_CONTROL)) {
      code |= ATARI_MOD_CONTROL;
    }
  }

  return code;
}

WebKB::WebKB(uint16_t port) : port(port) {}

WebKB::~WebKB() {
  if (ws) {
    ws->closeAll();
    delete ws;
  }
  if (server)
    delete server;
}

struct TimerContext {
  std::function<void()> timerFunction;
  esp_timer_handle_t handle;
};

void IRAM_ATTR genericCallback(void *arg) {
  TimerContext *ctx = static_cast<TimerContext *>(arg);
  if (ctx->timerFunction) {
    ctx->timerFunction();
  }
  esp_timer_stop(ctx->handle);
  esp_timer_delete(ctx->handle);
  delete ctx;
}

void startOneShotTimer(std::function<void()> timerFunction, uint64_t delay_ms) {
  auto *ctx = new TimerContext{timerFunction, nullptr};
  esp_timer_create_args_t timerArgs = {.callback = &genericCallback,
                                       .arg = ctx,
                                       .dispatch_method = ESP_TIMER_TASK,
                                       .name = "PlatformESP32OneShot",
                                       .skip_unhandled_events = false};
  esp_timer_handle_t handle;
  ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &handle));
  ctx->handle = handle;
  uint64_t delay_us = delay_ms * 1000;
  ESP_ERROR_CHECK(esp_timer_start_once(handle, delay_us));
}

static uint8_t ipaddrbox[] =
    "\x55\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43"
    "\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x49"
    "\x42\x15\x13\x5 \x8\x14\x14\x10\x3a\x2f\x2f               \x42"
    "\x4a\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43"
    "\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x43\x4b";

void WebKB::printIPAddress() {
  extCmdBuffer[3] = 6;
  extCmdBuffer[4] = 5;
  extCmdBuffer[5] = 28;
  extCmdBuffer[6] = 3;
  extCmdBuffer[7] = 1;
  extCmdBuffer[8] = 0;
  extCmdBuffer[9] = 10;
  extCmdBuffer[10] = 0;
  extCmdBuffer[11] = 1;
  std::string ipString = std::string(WiFi.localIP().toString().c_str());
  memcpy(&ipaddrbox[40], ipString.c_str(), ipString.length());
  memcpy(&extCmdBuffer[12], ipaddrbox, 28 * 3);
  extCmdBuffer[0] = {static_cast<uint8_t>(ExtCmd::WRITEOSD)};
  gotExternalCmd = true;
}

void WebKB::init() {
  queueSem = xSemaphoreCreateBinary();
  xSemaphoreGive(queueSem);

  currentKey.dc0 = 0xff;
  currentKey.dc1 = 0xff;
  currentKey.shift = 0;
  currentKey.active = false;
  currentKey.holdTicks = 0;

  PlatformManager::getInstance().log(LOG_INFO, TAG, "Init Wifi");

  // create webserver instance
  server = new AsyncWebServer(port);

  // Event callbacks for WiFi events - set flags to defer server start
  // Server will be started from scanKeyboard() which runs in the main task context
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        PlatformManager::getInstance().log(LOG_INFO, TAG,
                                           "Wifi connected. IP address: %s",
                                           WiFi.localIP().toString());
        // Set flag to start web server from main task context
        pendingWebServerStart.store(true);
        break;
      case ARDUINO_EVENT_WIFI_AP_START:
        PlatformManager::getInstance().log(LOG_INFO, TAG,
                                           "AP started, will start captive portal server...");
        // Set flag to start captive portal from main task context
        pendingCaptivePortalStart.store(true);
        break;
      default:
        break;
    }
  });

  prefs.begin("wifi", true);
  String storedSSID = prefs.getString("ssid", "");
  String storedPASS = prefs.getString("pass", "");
  prefs.end();

  if (storedSSID.length()) {
    connectToWiFi(storedSSID, storedPASS);
  } else {
    startCaptivePortal();
  }

  // start with joystickmode 2 at startup
  extCmdBuffer[0] =
      static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::JOYSTICKMODE2);
  gotExternalCmd = true;
}

// scan for wifi networks
String WebKB::getNetworksHTML() {
  int n = WiFi.scanNetworks();
  String options;
  std::set<String> seenSSIDs;

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);

    if (ssid.length() == 0)
      continue;
    if (seenSSIDs.find(ssid) != seenSSIDs.end())
      continue;

    seenSSIDs.insert(ssid);
    options += "<option value='" + ssid + "'>" + ssid + "</option>";
  }

  return options;
}

// setup an access point - server will be started via AP_START event
void WebKB::startCaptivePortal() {
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Starting WiFi AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  // Server will be started in startCaptivePortalServer() via WIFI_AP_START event
}

// Called from WiFi AP_START event - starts the captive portal server
void WebKB::startCaptivePortalServer() {
  PlatformManager::getInstance().log(LOG_INFO, TAG,
                                     "Wifi access point ip adress: %s",
                                     WiFi.softAPIP().toString());

  dns_server.start(53, "*", WiFi.softAPIP());

  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String page = portalHTML;
    page.replace("%NETWORKS%", this->getNetworksHTML());
    request->send(200, "text/html", page);
  });

  // Android / iOS captive portal triggers
  server->on("/generate_204", HTTP_GET,
             [](AsyncWebServerRequest *r) { r->redirect("/"); });
  server->on("/hotspot-detect.html", HTTP_GET,
             [](AsyncWebServerRequest *r) { r->redirect("/"); });
  server->on("/connecttest.txt", HTTP_GET,
             [](AsyncWebServerRequest *r) { r->redirect("/"); });

  server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
    String new_ssid = request->getParam("ssid", true)->value();
    String new_pass = request->getParam("password", true)->value();
    request->send(200, "text/html", "<h3>REBOOTING AND CONNECTING...</h3>");

    // save wifi data
    prefs.begin("wifi", false);
    prefs.putString("ssid", new_ssid);
    prefs.putString("pass", new_pass);
    prefs.end();
    delay(1000);

    // reboot
    startOneShotTimer([]() { ESP.restart(); }, 2000);
  });

  server->begin();
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Captive portal server started");
}

void WebKB::connectToWiFi(const String &ssid, const String &pass) {

  PlatformManager::getInstance().log(
      LOG_INFO, TAG, "Trying to connect to ssid %s %s", ssid.c_str());

  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int attempts = 0;
  const int maxAttempts = 60; // 30 seconds
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {

    PlatformManager::getInstance().log(
        LOG_INFO, TAG, "Connection to ssid %s established.", ssid);

  } else {

    PlatformManager::getInstance().log(LOG_INFO, TAG,
                                       "Connection to ssid %s failed.", ssid);
    WiFi.mode(WIFI_OFF);
    delay(1000);
    startCaptivePortal();
  }
}

void WebKB::startWebServer() {
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Starting web server...");

  ws = new AsyncWebSocket("/ws");

  ws->onEvent([this](AsyncWebSocket *srv, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
      handleWebsocketMessage(arg, data, len);
    }
  });

  server->addHandler(ws);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", HTMLCSSKB_html, HTMLCSSKB_html_len);
  });

  // File upload endpoint for XEX/BIN/ATR files
  server->on("/upload", HTTP_POST,
    // Request handler (called after upload completes)
    [this](AsyncWebServerRequest *request) {
      if (lastUploadedFile.length() > 0) {
        String filename = lastUploadedFile;
        lastUploadedFile = "";  // Clear for next upload
        request->send(200, "application/json", "{\"status\":\"ok\",\"file\":\"" + filename + "\"}");

        // Determine action based on file extension
        bool isATR = filename.endsWith(".atr") || filename.endsWith(".ATR");

        memset(extCmdBuffer, 0, sizeof(extCmdBuffer));
        if (isATR) {
          // Mount ATR disk image
          PlatformManager::getInstance().log(LOG_INFO, TAG, "Upload complete, mounting ATR: %s", filename.c_str());
          extCmdBuffer[0] = static_cast<uint8_t>(ExtCmd::ATTACHATR);
        } else {
          // Load XEX/BIN/COM file
          PlatformManager::getInstance().log(LOG_INFO, TAG, "Upload complete, loading: %s", filename.c_str());
          extCmdBuffer[0] = static_cast<uint8_t>(ExtCmd::LOAD);
        }
        extCmdBuffer[1] = 0;
        extCmdBuffer[2] = 0x80;  // Flag to indicate command ready
        strncpy(reinterpret_cast<char*>(&extCmdBuffer[3]), filename.c_str(), 250);
        gotExternalCmd.store(true);
      } else {
        request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Upload failed\"}");
      }
    },
    // File upload handler (called for each chunk)
    [this](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      static File uploadFile;
      String filepath = "/" + filename;

      if (index == 0) {
        // First chunk - open file for writing
        PlatformManager::getInstance().log(LOG_INFO, TAG, "Upload started: %s", filename.c_str());
        uploadFile = SD_MMC.open(filepath.c_str(), FILE_WRITE);
        if (!uploadFile) {
          PlatformManager::getInstance().log(LOG_ERROR, TAG, "Failed to open file for writing: %s", filepath.c_str());
          return;
        }
      }

      if (uploadFile && len > 0) {
        uploadFile.write(data, len);
      }

      if (final) {
        if (uploadFile) {
          uploadFile.close();
          PlatformManager::getInstance().log(LOG_INFO, TAG, "Upload finished: %s (%d bytes)", filename.c_str(), index + len);
          // Store filename for the request handler
          lastUploadedFile = filepath;
        }
      }
    }
  );

  // File list endpoint
  server->on("/files", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "[";
    File root = SD_MMC.open("/");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      bool first = true;
      while (file) {
        String name = file.name();
        // Only list XEX, COM, BIN, ATR files
        if (name.endsWith(".xex") || name.endsWith(".XEX") ||
            name.endsWith(".com") || name.endsWith(".COM") ||
            name.endsWith(".bin") || name.endsWith(".BIN") ||
            name.endsWith(".atr") || name.endsWith(".ATR")) {
          if (!first) json += ",";
          json += "\"" + name + "\"";
          first = false;
        }
        file = root.openNextFile();
      }
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // Load file by name endpoint
  server->on("/load", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
      String filename = request->getParam("file")->value();
      PlatformManager::getInstance().log(LOG_INFO, TAG, "Load requested: %s", filename.c_str());

      // Trigger file load via ExtCmd mechanism
      memset(extCmdBuffer, 0, sizeof(extCmdBuffer));
      extCmdBuffer[0] = static_cast<uint8_t>(ExtCmd::LOAD);
      extCmdBuffer[1] = 0;
      extCmdBuffer[2] = 0x80;
      strncpy(reinterpret_cast<char*>(&extCmdBuffer[3]), filename.c_str(), 250);
      gotExternalCmd.store(true);

      request->send(200, "application/json", "{\"status\":\"ok\",\"file\":\"" + filename + "\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing file parameter\"}");
    }
  });

  // Mount ATR disk image endpoint
  server->on("/mount", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
      String filename = request->getParam("file")->value();
      PlatformManager::getInstance().log(LOG_INFO, TAG, "Mount ATR requested: %s", filename.c_str());

      // Trigger ATR mount via ExtCmd mechanism
      memset(extCmdBuffer, 0, sizeof(extCmdBuffer));
      extCmdBuffer[0] = static_cast<uint8_t>(ExtCmd::ATTACHATR);
      extCmdBuffer[1] = 0;
      extCmdBuffer[2] = 0x80;
      strncpy(reinterpret_cast<char*>(&extCmdBuffer[3]), filename.c_str(), 250);
      gotExternalCmd.store(true);

      request->send(200, "application/json", "{\"status\":\"ok\",\"file\":\"" + filename + "\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing file parameter\"}");
    }
  });

  // Unmount ATR disk image endpoint
  server->on("/unmount", HTTP_GET, [this](AsyncWebServerRequest *request) {
    PlatformManager::getInstance().log(LOG_INFO, TAG, "Unmount ATR requested");

    // Trigger ATR unmount via ExtCmd mechanism
    memset(extCmdBuffer, 0, sizeof(extCmdBuffer));
    extCmdBuffer[0] = static_cast<uint8_t>(ExtCmd::DETACHATR);
    extCmdBuffer[1] = 0;
    extCmdBuffer[2] = 0x80;
    gotExternalCmd.store(true);

    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server->begin();

  PlatformManager::getInstance().log(LOG_INFO, TAG, "Webserver started.");
}

// ----------------------------------------------------
// Dashboard → Emulator
// ----------------------------------------------------
void WebKB::handleWebsocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->opcode != WS_TEXT)
    return;

  // parse JSON
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err)
    return;

  // debug log
  String jsonStr;
  serializeJson(doc, jsonStr);
  PlatformManager::getInstance().log(LOG_DEBUG, TAG, jsonStr.c_str());

  // return, if type is missing
  const char *type = doc["type"] | "";
  if (strlen(type) == 0)
    return;

  if (!doc["keys"].is<JsonObject>())
    return;

  JsonObject keyObj = doc["keys"];

  // read chars
  const char *ch = keyObj["chars"];

  // check if chars exist
  if (ch != nullptr && ch[0] != '\0') {

    bool shift = keyObj["modifiers"]["shift"] | false;
    bool ctrl = keyObj["modifiers"]["ctrl"] | false;
    bool comm = keyObj["modifiers"]["commodore"] | false;

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "char:%s", ch);
    PlatformManager::getInstance().log(LOG_DEBUG, TAG, "Searching key %s",
                                       buffer);

    processSingleKey(type, buffer, shift, ctrl, comm);
  }
}

void WebKB::processSingleKey(const char *type, const char *keyId, bool shift,
                             bool ctrl, bool comm) {
  // return, if there's no char or keycode
  if (!keyId)
    return;

  CodeTriple code = {0xff, 0xff, 0xff};

  // check for shiftlock and toggle status
  if ((strcmp(keyId, "char:Capslock") == 0) &&
      (strcmp(type, "key-down") == 0)) {
    shiftlock = !shiftlock;
    return;
  }

  // check for external commands, only on key-down
  if (strcmp(type, "key-down") == 0) {
    if (strcmp(keyId, "char:RESET") == 0) {
      extCmdBuffer[0] =
          static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::RESET);
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:LOAD") == 0) {
      extCmdBuffer[0] =
          static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::LOAD);
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:SAVE") == 0) {
      extCmdBuffer[0] =
          static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::SAVE);
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:LIST") == 0) {
      extCmdBuffer[0] =
          static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::LIST);
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:PageUp") == 0) {
      extCmdBuffer[0] =
          static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::RESTORE);
      extCmdBuffer[1] = 0x00;
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:INCVOLUME") == 0) {
      extCmdBuffer[0] =
          static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::INCVOLUME);
      extCmdBuffer[1] = 10;
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:DECVOLUME") == 0) {
      extCmdBuffer[0] =
          static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::DECVOLUME);
      extCmdBuffer[1] = 10;
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:JOYMODE1") == 0) {
      extCmdBuffer[0] = static_cast<std::underlying_type<ExtCmd>::type>(
          ExtCmd::JOYSTICKMODE1);
      gotExternalCmd = true;
      return;
    }
    if (strcmp(keyId, "char:JOYMODE2") == 0) {
      extCmdBuffer[0] = static_cast<std::underlying_type<ExtCmd>::type>(
          ExtCmd::JOYSTICKMODE2);
      gotExternalCmd = true;
      return;
    }
  }

  // search in keymap
  bool found = false;
  for (const auto &entry : c64KeyMap) {
    if (strcmp(entry.keyId, keyId) == 0 && entry.shift == shift &&
        entry.ctrl == ctrl && entry.commodore == comm) {
      code = entry.code;
      found = true;
      break;
    }
  }

  // key found in keymap -> put it in queue (C64 mode)
  if (found) {

    PlatformManager::getInstance().log(LOG_DEBUG, TAG, "keycodes: %d %d",
                                       std::get<0>(code), std::get<1>(code));

    // set register for key down
    if (strcmp(type, "key-down") == 0) {

      if (xSemaphoreTake(queueSem, portMAX_DELAY) == pdTRUE) {
        uint8_t shiftcode = std::get<2>(code);
        if (shiftlock)
          shiftcode |= 0x01;
        eventQueue.emplace(std::get<1>(code), std::get<0>(code), shiftcode);
        xSemaphoreGive(queueSem);
      }

    } else if (strcmp(type, "key-up") == 0) {
      // key release → all bits to 0xff
      if (xSemaphoreTake(queueSem, portMAX_DELAY) == pdTRUE) {
        eventQueue.emplace(0xff, 0xff, 0x00);
        xSemaphoreGive(queueSem);
      }
    }
  }

  // Atari key code handling (independent of C64 keymap)
  if (strcmp(type, "key-down") == 0) {
    // Check for Atari console keys
    if (strcmp(keyId, "char:START") == 0 || strcmp(keyId, "char:F9") == 0) {
      consoleKeys.store(consoleKeys.load(std::memory_order_relaxed) | ATARI_CONSOLE_START,
                       std::memory_order_release);
      return;
    }
    if (strcmp(keyId, "char:SELECT") == 0 || strcmp(keyId, "char:F10") == 0) {
      consoleKeys.store(consoleKeys.load(std::memory_order_relaxed) | ATARI_CONSOLE_SELECT,
                       std::memory_order_release);
      return;
    }
    if (strcmp(keyId, "char:OPTION") == 0 || strcmp(keyId, "char:F11") == 0) {
      consoleKeys.store(consoleKeys.load(std::memory_order_relaxed) | ATARI_CONSOLE_OPTION,
                       std::memory_order_release);
      return;
    }
    if (strcmp(keyId, "char:BREAK") == 0 || strcmp(keyId, "char:Pause") == 0) {
      // Break key generates NMI - handled via external command
      extCmdBuffer[0] = static_cast<std::underlying_type<ExtCmd>::type>(ExtCmd::RESTORE);
      extCmdBuffer[1] = ATARI_KEY_BREAK;
      gotExternalCmd = true;
      return;
    }

    // Convert to Atari key code
    uint8_t atariCode = keyIdToAtariCode(keyId, shift || shiftlock, ctrl);
    if (atariCode != 0xFF && atariCode != ATARI_KEY_NONE) {
      atariKeyCode.store(atariCode, std::memory_order_release);
      atariKeyPressed.store(true, std::memory_order_release);
      PlatformManager::getInstance().log(LOG_DEBUG, TAG, "Atari key: 0x%02X", atariCode);
    }
  }
  else if (strcmp(type, "key-up") == 0) {
    // Clear console keys on release
    if (strcmp(keyId, "char:START") == 0 || strcmp(keyId, "char:F9") == 0) {
      consoleKeys.store(consoleKeys.load(std::memory_order_relaxed) & ~ATARI_CONSOLE_START,
                       std::memory_order_release);
      return;
    }
    if (strcmp(keyId, "char:SELECT") == 0 || strcmp(keyId, "char:F10") == 0) {
      consoleKeys.store(consoleKeys.load(std::memory_order_relaxed) & ~ATARI_CONSOLE_SELECT,
                       std::memory_order_release);
      return;
    }
    if (strcmp(keyId, "char:OPTION") == 0 || strcmp(keyId, "char:F11") == 0) {
      consoleKeys.store(consoleKeys.load(std::memory_order_relaxed) & ~ATARI_CONSOLE_OPTION,
                       std::memory_order_release);
      return;
    }

    // Clear Atari key on release
    atariKeyPressed.store(false, std::memory_order_release);
  }
}

void WebKB::scanKeyboard() {

  // Check for deferred server start (must run from main task context for TCPIP core access)
  if (!serverStarted) {
    if (pendingCaptivePortalStart.load()) {
      pendingCaptivePortalStart.store(false);
      startCaptivePortalServer();
      serverStarted = true;
    } else if (pendingWebServerStart.load()) {
      pendingWebServerStart.store(false);
      printIPAddress();
      startWebServer();
      serverStarted = true;
    }
  }

  // countdown
  if (currentKey.active && currentKey.holdTicks > 0) {
    currentKey.holdTicks--;
  }

  // if the key is not active for at least 24 ms, do nothing (3*8ms)
  if (currentKey.active && currentKey.holdTicks > 0) {
    return;
  }

  // otherwise check queue
  bool gotEvent = false;
  uint8_t _dc1, _dc0, _shift;

  if (xSemaphoreTake(queueSem, 0) == pdTRUE) {
    if (!eventQueue.empty()) {
      auto e = eventQueue.front();
      eventQueue.pop();
      gotEvent = true;
      std::tie(_dc1, _dc0, _shift) = e;
    }
    xSemaphoreGive(queueSem);
  }

  if (gotEvent) {
    if (_dc1 == 0xFF && _dc0 == 0xFF) {
      currentKey.active = false;
    } else {
      // key press
      currentKey.dc1 = _dc1;
      currentKey.dc0 = _dc0;
      currentKey.shift = _shift;
      currentKey.holdTicks = 3; // 3×8ms = 24ms
      currentKey.active = true;
    }
  }

  // set registers
  if (currentKey.active) {
    setKBcodes(currentKey.dc1, currentKey.dc0);
    shiftctrlcode.store(currentKey.shift, std::memory_order_release);
  } else {
    setKBcodes(0xFF, 0xFF);
    shiftctrlcode.store(0, std::memory_order_release);
  }
}

// ----------------------------------------------------
// external commands
// ----------------------------------------------------
uint8_t *WebKB::getExtCmdData() {
  if (gotExternalCmd) {
    gotExternalCmd = false;
    extCmdBuffer[2] = 0x80;
    return extCmdBuffer;
  }
  return nullptr;
}

void WebKB::sendExtCmdNotification(uint8_t *data, size_t size) {}

#endif
