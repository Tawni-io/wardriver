#pragma once

// Thin wrapper over LilyGO board_config — pin source of truth for this project.
// Upstream: https://github.com/Xinyuan-LilyGO/T-Display-C5/blob/master/include/board_config.h

#include "board_config.h"

// Native panel cells. Logical cabin size comes from display_width()/height().
// Portrait = LCD_WIDTH × LCD_HEIGHT (170×320). Landscape = swapped (320×170).
