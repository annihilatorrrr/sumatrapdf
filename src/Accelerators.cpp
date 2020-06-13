/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "resource.h"

//#define CTRL (FCONTROL | FVIRTKEY)
//#define SHIFT_CTRL (FSHIFT | FCONTROL | FVIRTKEY)

// TODO: should FVIRTKEY be set for chars like 'A'
ACCEL gAccelerators[] = {
    {FCONTROL | FVIRTKEY, 'A', IDM_SELECT_ALL},
    {FCONTROL | FVIRTKEY, 'B', IDM_FAV_ADD},
    {FCONTROL | FVIRTKEY, 'C', IDM_COPY_SELECTION},
    {FCONTROL | FVIRTKEY, 'D', IDM_PROPERTIES},
    {FCONTROL | FVIRTKEY, 'F', IDM_FIND_FIRST},
    {FCONTROL | FVIRTKEY, 'G', IDM_GOTO_PAGE},
    {FCONTROL | FVIRTKEY, 'L', IDM_VIEW_PRESENTATION_MODE},
    {FSHIFT | FCONTROL | FVIRTKEY, 'L', IDM_VIEW_FULLSCREEN},
    {FCONTROL | FVIRTKEY, 'N', IDM_NEW_WINDOW},
    {FSHIFT | FCONTROL | FVIRTKEY, 'N', IDM_DUPLICATE_IN_NEW_WINDOW},
    {FCONTROL | FVIRTKEY, 'O', IDM_OPEN},
    {FCONTROL | FVIRTKEY, 'S', IDM_SAVEAS},
    {FSHIFT | FCONTROL | FVIRTKEY, 'S', IDM_SAVEAS_BOOKMARK},
    {FCONTROL | FVIRTKEY, 'P', IDM_PRINT},
    {FCONTROL | FVIRTKEY, 'Q', IDM_EXIT},
    {FCONTROL | FVIRTKEY, 'W', IDM_CLOSE},
    {FCONTROL | FVIRTKEY, 'Y', IDM_ZOOM_CUSTOM},
    {FCONTROL | FVIRTKEY, '0', IDM_ZOOM_FIT_PAGE},
    {FCONTROL | FVIRTKEY, VK_NUMPAD0, IDM_ZOOM_FIT_PAGE},
    {FCONTROL | FVIRTKEY, '1', IDM_ZOOM_ACTUAL_SIZE},
    {FCONTROL | FVIRTKEY, VK_NUMPAD1, IDM_ZOOM_ACTUAL_SIZE},
    {FCONTROL | FVIRTKEY, '2', IDM_ZOOM_FIT_WIDTH},
    {FCONTROL | FVIRTKEY, VK_NUMPAD2, IDM_ZOOM_FIT_WIDTH},
    {FCONTROL | FVIRTKEY, '3', IDM_ZOOM_FIT_CONTENT},
    {FCONTROL | FVIRTKEY, VK_NUMPAD3, IDM_ZOOM_FIT_CONTENT},
    {FCONTROL | FVIRTKEY, '6', IDM_VIEW_SINGLE_PAGE},
    {FCONTROL | FVIRTKEY, VK_NUMPAD6, IDM_VIEW_SINGLE_PAGE},
    {FCONTROL | FVIRTKEY, '7', IDM_VIEW_FACING},
    {FCONTROL | FVIRTKEY, VK_NUMPAD7, IDM_VIEW_FACING},
    {FCONTROL | FVIRTKEY, '8', IDM_VIEW_BOOK},
    {FCONTROL | FVIRTKEY, VK_NUMPAD8, IDM_VIEW_BOOK},
    {FCONTROL | FVIRTKEY, VK_ADD, IDT_VIEW_ZOOMIN},
    {FSHIFT | FCONTROL | FVIRTKEY, VK_ADD, IDM_VIEW_ROTATE_RIGHT},
    {FCONTROL | FVIRTKEY, VK_OEM_PLUS, IDT_VIEW_ZOOMIN},
    {FSHIFT | FCONTROL | FVIRTKEY, VK_OEM_PLUS, IDM_VIEW_ROTATE_RIGHT},
    {FCONTROL | FVIRTKEY, VK_INSERT, IDM_COPY_SELECTION},
    {FVIRTKEY, VK_F2, IDM_RENAME_FILE},
    {FVIRTKEY, VK_F3, IDM_FIND_NEXT},
    {FSHIFT | FVIRTKEY, VK_F3, IDM_FIND_PREV},
    {FCONTROL | FVIRTKEY, VK_F3, IDM_FIND_NEXT_SEL},
    {FSHIFT | FCONTROL | FVIRTKEY, VK_F3, IDM_FIND_PREV_SEL},
    {FCONTROL | FVIRTKEY, VK_F4, IDM_CLOSE},
    {FVIRTKEY, VK_F5, IDM_VIEW_PRESENTATION_MODE},
    {FVIRTKEY, VK_F6, IDM_MOVE_FRAME_FOCUS},
    {FVIRTKEY, VK_F8, IDM_VIEW_SHOW_HIDE_TOOLBAR},

    {FVIRTKEY, VK_F9, IDM_VIEW_SHOW_HIDE_MENUBAR},
    {FVIRTKEY, VK_F11, IDM_VIEW_FULLSCREEN},
    {FSHIFT | FVIRTKEY, VK_F11, IDM_VIEW_PRESENTATION_MODE},
    {FVIRTKEY, VK_F12, IDM_VIEW_BOOKMARKS},
    {FCONTROL | FVIRTKEY, VK_SUBTRACT, IDT_VIEW_ZOOMOUT},
    {FSHIFT | FCONTROL | FVIRTKEY, VK_SUBTRACT, IDM_VIEW_ROTATE_LEFT},
    {FCONTROL | FVIRTKEY, VK_OEM_MINUS, IDT_VIEW_ZOOMOUT},
    {FSHIFT | FCONTROL | FVIRTKEY, VK_OEM_MINUS, IDM_VIEW_ROTATE_LEFT},
    {FALT | FVIRTKEY, VK_LEFT, IDM_GOTO_NAV_BACK},
    {FALT | FVIRTKEY, VK_RIGHT, IDM_GOTO_NAV_FORWARD},
};

HACCEL CreateSumatraAcceleratorTable() {
    int n = (int)dimof(gAccelerators);
    HACCEL res = CreateAcceleratorTableW(gAccelerators, n);
    CrashIf(res == nullptr);
    return res;
}