/**
 * @file constants.h
 * @brief UI component layout and color constants for the menu system.
 * @ingroup ui_components
 *
 * This header defines all layout, sizing, and color constants used by UI components.
 */

#ifndef COMPONENTS_CONSTANTS_H__
#define COMPONENTS_CONSTANTS_H__

#include "../theme.h"

/**
 * @def TAB_HEIGHT
 * @brief Height of the tabs in the main menu (pixels).
 */
#define TAB_HEIGHT                      (20)
#define TAB_SELECTED_LIFT               (5)

/**
 * @def BORDER_THICKNESS
 * @brief Thickness of UI borders (pixels).
 */
#define BORDER_THICKNESS                (4)

/**
 * @def DISPLAY_WIDTH
 * @brief Width of the display (pixels).
 */
#define DISPLAY_WIDTH                   (640)
/**
 * @def DISPLAY_HEIGHT
 * @brief Height of the display (pixels).
 */
#define DISPLAY_HEIGHT                  (480)

/**
 * @def DISPLAY_CENTER_X
 * @brief Center X coordinate of the display.
 */
#define DISPLAY_CENTER_X                (DISPLAY_WIDTH / 2)
/**
 * @def DISPLAY_CENTER_Y
 * @brief Center Y coordinate of the display.
 */
#define DISPLAY_CENTER_Y                (DISPLAY_HEIGHT / 2)

/**
 * @def OVERSCAN_WIDTH
 * @brief Overscan margin on the X axis (pixels).
 */
#define OVERSCAN_WIDTH                  (32)
/**
 * @def OVERSCAN_HEIGHT
 * @brief Overscan margin on the Y axis (pixels).
 */
#define OVERSCAN_HEIGHT                 (24)

/**
 * @def VISIBLE_AREA_X0
 * @brief Start X coordinate of the visible display area.
 */
#define VISIBLE_AREA_X0                 (OVERSCAN_WIDTH)
/**
 * @def VISIBLE_AREA_Y0
 * @brief Start Y coordinate of the visible display area.
 */
#define VISIBLE_AREA_Y0                 (OVERSCAN_HEIGHT)
/**
 * @def VISIBLE_AREA_X1
 * @brief End X coordinate of the visible display area.
 */
#define VISIBLE_AREA_X1                 (DISPLAY_WIDTH - OVERSCAN_WIDTH)
/**
 * @def VISIBLE_AREA_Y1
 * @brief End Y coordinate of the visible display area.
 */
#define VISIBLE_AREA_Y1                 (DISPLAY_HEIGHT - OVERSCAN_HEIGHT)

/**
 * @def VISIBLE_AREA_WIDTH
 * @brief Width of the visible display area (pixels).
 */
#define VISIBLE_AREA_WIDTH              (VISIBLE_AREA_X1 - VISIBLE_AREA_X0)
/**
 * @def VISIBLE_AREA_HEIGHT
 * @brief Height of the visible display area (pixels).
 */
#define VISIBLE_AREA_HEIGHT             (VISIBLE_AREA_Y1 - VISIBLE_AREA_Y0)

/**
 * @def LAYOUT_ACTIONS_SEPARATOR_Y
 * @brief Y coordinate for the layout actions separator.
 */
#define LAYOUT_ACTIONS_SEPARATOR_Y      (400)

/**
 * @def SEEKBAR_HEIGHT
 * @brief Height of the seek bar (pixels).
 */
#define SEEKBAR_HEIGHT                  (24)
/**
 * @def SEEKBAR_WIDTH
 * @brief Width of the seek bar (pixels).
 */
#define SEEKBAR_WIDTH                   (524)
/**
 * @def SEEKBAR_X
 * @brief X coordinate of the seek bar.
 */
#define SEEKBAR_X                       (DISPLAY_CENTER_X - (SEEKBAR_WIDTH / 2))
/**
 * @def SEEKBAR_Y
 * @brief Y coordinate of the seek bar.
 */
#define SEEKBAR_Y                       (VISIBLE_AREA_Y1 - SEEKBAR_HEIGHT - 80)

/**
 * @def LOADER_WIDTH
 * @brief Width of the loader bar (pixels).
 */
#define LOADER_WIDTH                    (320)
/**
 * @def LOADER_HEIGHT
 * @brief Height of the loader bar (pixels).
 */
#define LOADER_HEIGHT                   (24)
/**
 * @def LOADER_X
 * @brief X coordinate of the loader bar.
 */
#define LOADER_X                        (DISPLAY_CENTER_X - (LOADER_WIDTH / 2))
/**
 * @def LOADER_Y
 * @brief Y coordinate of the loader bar (accounts for message height).
 */
#define LOADER_Y                        (DISPLAY_CENTER_Y - (LOADER_HEIGHT / 2) - 8)

/**
 * @def MESSAGEBOX_MAX_WIDTH
 * @brief Maximum width of a message box (pixels).
 */
#define MESSAGEBOX_MAX_WIDTH            (360)
/**
 * @def MESSAGEBOX_MARGIN
 * @brief Margin around a message box (pixels).
 */
#define MESSAGEBOX_MARGIN               (32)

/**
 * @def TEXT_MARGIN_HORIZONTAL
 * @brief Horizontal margin for text (pixels).
 */
#define TEXT_MARGIN_HORIZONTAL          (10)
/**
 * @def TEXT_MARGIN_VERTICAL
 * @brief Vertical margin for text (pixels).
 */
#define TEXT_MARGIN_VERTICAL            (6)
/**
 * @def TEXT_OFFSET_VERTICAL
 * @brief Vertical offset for text (pixels).
 */
#define TEXT_OFFSET_VERTICAL            (1)
/**
 * @def TEXT_LINE_SPACING_ADJUST
 * @brief Adjustment for text line spacing (pixels).
 */
#define TEXT_LINE_SPACING_ADJUST        (0)

/**
 * @def BOXART_WIDTH
 * @brief Width of the boxart image (pixels).
 */
#define BOXART_WIDTH                    (158)
/**
 * @def BOXART_HEIGHT
 * @brief Height of the boxart image (pixels).
 */
#define BOXART_HEIGHT                   (112)

/**
 * @def BOXART_WIDTH_DD
 * @brief Width of the 64DD boxart image (pixels).
 */
#define BOXART_WIDTH_DD                 (129)
/**
 * @def BOXART_HEIGHT_DD
 * @brief Height of the 64DD boxart image (pixels).
 */
#define BOXART_HEIGHT_DD                (112)

/**
 * @def BOXART_WIDTH_MAX
 * @brief Maximum width of the boxart image (pixels).
 */
#define BOXART_WIDTH_MAX                (158)
/**
 * @def BOXART_HEIGHT_MAX
 * @brief Maximum height of the boxart image (pixels).
 */
#define BOXART_HEIGHT_MAX               (158)

/**
 * @def BOXART_X
 * @brief X coordinate for boxart image.
 */
#define BOXART_X                        (VISIBLE_AREA_X1 - BOXART_WIDTH - 24)
/**
 * @def BOXART_Y
 * @brief Y coordinate for boxart image.
 */
#define BOXART_Y                        (LAYOUT_ACTIONS_SEPARATOR_Y - BOXART_HEIGHT - 24)
/**
 * @def BOXART_X_JP
 * @brief X coordinate for Japanese boxart image.
 */
#define BOXART_X_JP                     (VISIBLE_AREA_X1 - BOXART_WIDTH_MAX + 21)
/**
 * @def BOXART_Y_JP
 * @brief Y coordinate for Japanese boxart image.
 */
#define BOXART_Y_JP                     (LAYOUT_ACTIONS_SEPARATOR_Y - BOXART_HEIGHT_MAX - 24)

/**
 * @def BOXART_X_DD
 * @brief X coordinate for 64DD boxart image.
 */
#define BOXART_X_DD                     (VISIBLE_AREA_X1 - BOXART_WIDTH_DD - 23)
/**
 * @def BOXART_Y_DD
 * @brief Y coordinate for 64DD boxart image.
 */
#define BOXART_Y_DD                     (LAYOUT_ACTIONS_SEPARATOR_Y - BOXART_HEIGHT_DD - 24)

/**
 * @def LIST_SCROLLBAR_WIDTH
 * @brief Width of the list scrollbar (pixels).
 */
#define LIST_SCROLLBAR_WIDTH            (12)
/**
 * @def LIST_SCROLLBAR_HEIGHT
 * @brief Height of the list scrollbar (pixels).
 */
#define LIST_SCROLLBAR_HEIGHT           (LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - TAB_HEIGHT - BORDER_THICKNESS)
/**
 * @def LIST_SCROLLBAR_X
 * @brief X coordinate of the list scrollbar.
 */
#define LIST_SCROLLBAR_X                (VISIBLE_AREA_X1 - LIST_SCROLLBAR_WIDTH)
/**
 * @def LIST_SCROLLBAR_Y
 * @brief Y coordinate of the list scrollbar.
 */
#define LIST_SCROLLBAR_Y                (VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS)

/**
 * @def SETTINGS_RAIL_X0
 * @brief X coordinate of the settings category rail.
 */
#define SETTINGS_RAIL_X0                (VISIBLE_AREA_X0 + 4)
/**
 * @def SETTINGS_RAIL_X1
 * @brief X coordinate of the divider between the category rail and the pane.
 */
#define SETTINGS_RAIL_X1                (VISIBLE_AREA_X0 + 136)
/**
 * @def SETTINGS_PANE_X0
 * @brief X coordinate of the left edge of the settings pane.
 */
#define SETTINGS_PANE_X0                (SETTINGS_RAIL_X1 + 8)
/**
 * @def SETTINGS_PANE_X1
 * @brief X coordinate of the right edge of the settings pane.
 */
#define SETTINGS_PANE_X1                (VISIBLE_AREA_X1 - 4)
/**
 * @def SETTINGS_PANE_Y0
 * @brief Y coordinate of the top edge of the settings pane.
 */
#define SETTINGS_PANE_Y0                (VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS + 8)
/**
 * @def SETTINGS_PANE_Y1
 * @brief Y coordinate of the bottom edge of the settings pane.
 */
#define SETTINGS_PANE_Y1                (LAYOUT_ACTIONS_SEPARATOR_Y - 6)
/**
 * @def SETTINGS_ROW_HEIGHT
 * @brief Height of a single label/value row in a settings pane (pixels).
 */
#define SETTINGS_ROW_HEIGHT             (28)
/**
 * @def SETTINGS_ROWS
 * @brief Maximum number of label/value rows visible in a settings pane.
 */
#define SETTINGS_ROWS                   ((SETTINGS_PANE_Y1 - SETTINGS_PANE_Y0) / SETTINGS_ROW_HEIGHT)

/**
 * @def LIST_ENTRIES
 * @brief Maximum number of file list entries.
 */
#define LIST_ENTRIES                    (19)
/**
 * @def FILE_LIST_MAX_WIDTH
 * @brief Maximum width for a file list entry (pixels).
 */
#define FILE_LIST_MAX_WIDTH             (480)
/**
 * @def FILE_LIST_HIGHLIGHT_WIDTH
 * @brief Width of the file list highlight (pixels).
 */
#define FILE_LIST_HIGHLIGHT_WIDTH       (VISIBLE_AREA_X1 - VISIBLE_AREA_X0 - LIST_SCROLLBAR_WIDTH)
/**
 * @def FILE_LIST_HIGHLIGHT_X
 * @brief X coordinate of the file list highlight.
 */
#define FILE_LIST_HIGHLIGHT_X           (VISIBLE_AREA_X0)

/**
 * @def BACKGROUND_EMPTY_COLOR
 * @brief Color used when no background image is present (RGBA8888).
 */
#define BACKGROUND_EMPTY_COLOR          (theme_get()->background)
/**
 * @def BACKGROUND_OVERLAY_COLOR
 * @brief Overlay color for the background (RGBA8888, semi-transparent).
 */
#define BACKGROUND_OVERLAY_COLOR        RGBA32(0x00, 0x00, 0x00, 0xA0)

/**
 * @def BORDER_COLOR
 * @brief Color of UI borders (RGBA8888).
 */
#define BORDER_COLOR                    (theme_get()->border)

/**
 * @def PROGRESSBAR_BG_COLOR
 * @brief Background color of the progress bar (RGBA8888).
 */
#define PROGRESSBAR_BG_COLOR            (theme_get()->panel)
/**
 * @def PROGRESSBAR_DONE_COLOR
 * @brief Color of the completed portion of the progress bar (RGBA8888).
 */
#define PROGRESSBAR_DONE_COLOR          (theme_get()->accent)

/**
 * @def SCROLLBAR_BG_COLOR
 * @brief Background color of the scrollbar (RGBA8888).
 */
#define SCROLLBAR_BG_COLOR              (theme_get()->panel)
/**
 * @def SCROLLBAR_INACTIVE_COLOR
 * @brief Inactive color of the scrollbar (RGBA8888).
 */
#define SCROLLBAR_INACTIVE_COLOR        (theme_get()->tab_inactive)
/**
 * @def SCROLLBAR_POSITION_COLOR
 * @brief Color of the scrollbar position indicator (RGBA8888).
 */
#define SCROLLBAR_POSITION_COLOR        (theme_get()->accent)

/**
 * @def DIALOG_BG_COLOR
 * @brief Background color for dialog boxes (RGBA8888).
 */
#define DIALOG_BG_COLOR                 (theme_get()->panel)

/**
 * @def BOXART_LOADING_COLOR
 * @brief Color used while boxart is loading (RGBA8888).
 */
#define BOXART_LOADING_COLOR            (theme_get()->background)

/**
 * @def FILE_LIST_HIGHLIGHT_COLOR
 * @brief Highlight color for file list entries (RGBA8888).
 */
#define FILE_LIST_HIGHLIGHT_COLOR       (theme_get()->highlight)

/**
 * @def CONTEXT_MENU_HIGHLIGHT_COLOR
 * @brief Highlight color for context menu entries (RGBA8888).
 */
#define CONTEXT_MENU_HIGHLIGHT_COLOR    (theme_get()->highlight)

/**
 * @def TAB_INACTIVE_BORDER_COLOR
 * @brief Border color for inactive tabs (RGBA8888).
 */
#define TAB_INACTIVE_BORDER_COLOR       (theme_get()->muted_text)
/**
 * @def TAB_ACTIVE_BORDER_COLOR
 * @brief Border color for active tabs (RGBA8888).
 */
#define TAB_ACTIVE_BORDER_COLOR         (theme_get()->border)
/**
 * @def TAB_INACTIVE_BACKGROUND_COLOR
 * @brief Background color for inactive tabs (RGBA8888).
 */
#define TAB_INACTIVE_BACKGROUND_COLOR   (theme_get()->tab_inactive)
/**
 * @def TAB_ACTIVE_BACKGROUND_COLOR
 * @brief Background color for active tabs (RGBA8888).
 */
#define TAB_ACTIVE_BACKGROUND_COLOR     (theme_get()->tab_active)

#endif /* COMPONENTS_CONSTANTS_H__ */
