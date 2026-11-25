// Build-time settings.
#pragma once

// Display orientation, degrees clockwise: 0, 90, 180 or 270. Rotates the LCD
// (MADCTL) and the touch coordinates together.
#ifndef UI_ROTATION
#define UI_ROTATION 90
#endif

// Touch panel axes relative to the LCD at rotation 0 (set once per board
// type, verified with the "touch:" lines in the log).
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif
#ifndef TOUCH_MIRROR_X
#define TOUCH_MIRROR_X 0
#endif
#ifndef TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y 0
#endif

// log every touch down/up with coordinates
#ifndef TOUCH_LOG
#define TOUCH_LOG 1
#endif
