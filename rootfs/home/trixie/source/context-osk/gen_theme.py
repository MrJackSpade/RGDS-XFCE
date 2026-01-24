#!/usr/bin/env python3

# Layout Configuration
# Base Unit: 34px (32px key + 2px gap)
UNIT = 34
KEY_W_BASE = 32
GAP = 2
START_X = 2
START_Y = 2
HEIGHT = 32

# Keycodes (from xmodmap -pke on trixie)
# Function
K_ESC = 9
K_F1 = 67; K_F2 = 68; K_F3 = 69; K_F4 = 70
K_F5 = 71; K_F6 = 72; K_F7 = 73; K_F8 = 74
K_F9 = 75; K_F10 = 76; K_F11 = 95; K_F12 = 96
K_PRTSC = 107
K_SCRLK = 78
K_PAUSE = 127

# Row 1
K_GRAVE = 49
K_1 = 10; K_2 = 11; K_3 = 12; K_4 = 13; K_5 = 14
K_6 = 15; K_7 = 16; K_8 = 17; K_9 = 18; K_0 = 19
K_MINUS = 20
K_EQUAL = 21
K_BKSP = 22
K_INS = 118; K_HOME = 110; K_PGUP = 112

# Row 2
K_TAB = 23
K_Q = 24; K_W = 25; K_E = 26; K_R = 27; K_T = 28
K_Y = 29; K_U = 30; K_I = 31; K_O = 32; K_P = 33
K_LBRACKET = 34
K_RBRACKET = 35
K_BACKSLASH = 51
K_DEL = 119; K_END = 115; K_PGDN = 117

# Row 3
K_CAPS = 66
K_A = 38; K_S = 39; K_D = 40; K_F = 41; K_G = 42
K_H = 43; K_J = 44; K_K = 45; K_L = 46
K_SEMICOLON = 47
K_QUOTE = 48
K_ENTER = 36

# Row 4
K_LSHIFT = 50
K_Z = 52; K_X = 53; K_C = 54; K_V = 55; K_B = 56
K_N = 57; K_M = 58
K_COMMA = 59
K_PERIOD = 60
K_SLASH = 61
K_RSHIFT = 62
K_UP = 111

# Row 5
K_LCTRL = 37
K_SUPER = 133
K_LALT = 64
K_SPACE = 65
K_RALT = 108
K_MENU = 135
K_RCTRL = 105
K_LEFT = 113
K_DOWN = 116
K_RIGHT = 114

# Define Rows
# (Label, Keycode, WidthUnits, ForceColOffset?, IsToggle?)
tkl_layout = [
    # Row 0
    [("Esc", K_ESC, 1), ("GAP", 0, 1), 
     ("F1", K_F1, 1), ("F2", K_F2, 1), ("F3", K_F3, 1), ("F4", K_F4, 1), ("GAP", 0, 0.5),
     ("F5", K_F5, 1), ("F6", K_F6, 1), ("F7", K_F7, 1), ("F8", K_F8, 1), ("GAP", 0, 0.5),
     ("F9", K_F9, 1), ("F10", K_F10, 1), ("F11", K_F11, 1), ("F12", K_F12, 1), ("GAP", 0, 0.5),
     ("PrtSc", K_PRTSC, 1), ("ScrLk", K_SCRLK, 1), ("Pause", K_PAUSE, 1)],

    # Row 1
    [("`", K_GRAVE, 1), ("1", K_1, 1), ("2", K_2, 1), ("3", K_3, 1), ("4", K_4, 1), ("5", K_5, 1),
     ("6", K_6, 1), ("7", K_7, 1), ("8", K_8, 1), ("9", K_9, 1), ("0", K_0, 1),
     ("-", K_MINUS, 1), ("=", K_EQUAL, 1), ("Bksp", K_BKSP, 2), ("GAP", 0, 0.5),
     ("Ins", K_INS, 1), ("Home", K_HOME, 1), ("PgUp", K_PGUP, 1)],

    # Row 2
    [("Tab", K_TAB, 1.5), ("Q", K_Q, 1), ("W", K_W, 1), ("E", K_E, 1), ("R", K_R, 1), ("T", K_T, 1),
     ("Y", K_Y, 1), ("U", K_U, 1), ("I", K_I, 1), ("O", K_O, 1), ("P", K_P, 1),
     ("[", K_LBRACKET, 1), ("]", K_RBRACKET, 1), ("\\", K_BACKSLASH, 1.5), ("GAP", 0, 0.5),
     ("Del", K_DEL, 1), ("End", K_END, 1), ("PgDn", K_PGDN, 1)],

    # Row 3
    [("Caps", K_CAPS, 1.75), ("A", K_A, 1), ("S", K_S, 1), ("D", K_D, 1), ("F", K_F, 1), ("G", K_G, 1),
     ("H", K_H, 1), ("J", K_J, 1), ("K", K_K, 1), ("L", K_L, 1), (";", K_SEMICOLON, 1), ("'", K_QUOTE, 1),
     ("Enter", K_ENTER, 2.25)],

    # Row 4
    [("Shift", K_LSHIFT, 2.25, None, True), ("Z", K_Z, 1), ("X", K_X, 1), ("C", K_C, 1), ("V", K_V, 1),
     ("B", K_B, 1), ("N", K_N, 1), ("M", K_M, 1), (",", K_COMMA, 1), (".", K_PERIOD, 1), ("/", K_SLASH, 1),
     ("Shift", K_RSHIFT, 2.75, None, True), ("GAP", 0, 1.5), ("Up", K_UP, 1)],

    # Row 5
    # Adjusted to 15u width: Ctrl(1.5), Super(1.25), Alt(1.25), Space(7), Alt(1.25), Menu(1.25), Ctrl(1.5)
    [("Ctrl", K_LCTRL, 1.5, None, True), ("Win", K_SUPER, 1.25, None, True), ("Alt", K_LALT, 1.25, None, True), 
     ("Space", K_SPACE, 7), 
     ("Alt", K_RALT, 1.25, None, True), ("Menu", K_MENU, 1.25, None, True), ("Ctrl", K_RCTRL, 1.5, None, True), ("GAP", 0, 0.5),
     ("Left", K_LEFT, 1), ("Down", K_DOWN, 1), ("Right", K_RIGHT, 1)]
]

print("[General]")
print(f"width={ int(19 * UNIT) }") # Approx
print(f"height={ int(6 * UNIT + 4) }") # Approx
print("background_color=#0A0A0A")

for r_idx, row in enumerate(tkl_layout):
    current_x_units = 0
    y_pos = START_Y + r_idx * UNIT
    
    for item in row:
        label = item[0]
        width_units = item[2]
        
        if label == "GAP":
            current_x_units += width_units
            continue
            
        keycode = item[1]
        
        # Check for toggle property
        is_toggle = False
        if len(item) > 4 and item[4]:
            is_toggle = True
        
        x_pos = int(START_X + current_x_units * UNIT)
        w_pos = int(width_units * UNIT - GAP)
        
        # Section name can be anything unique
        safe_label = label.replace("`", "Backtick").replace("\\", "Backslash").replace("[", "LBr").replace("]", "RBr").replace("=", "Equal").replace(";", "Colon").replace("'", "Quote").replace(",", "Comma").replace(".", "Dot").replace("/", "Slash").replace("-", "Dash")
        # Ensure Uniqueness if duplicate names exist (like Shift, Alt, Ctrl)
        # We'll just append random suffix or use counters?
        # Simpler: Use Keycode in name?
        section_name = f"Key_{safe_label}_{keycode}"
        
        print(f"\n[{section_name}]")
        print(f"x={x_pos}")
        print(f"y={y_pos}")
        print(f"width={w_pos}")
        print(f"height={HEIGHT}")
        print(f"keycode={keycode}")
        if is_toggle:
            print("toggle=true")
        print(f"label={label}")
        
        current_x_units += width_units
