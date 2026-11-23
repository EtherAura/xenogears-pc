#ifndef PC_BACKLOG_H
#define PC_BACKLOG_H
#ifdef PLATFORM_PC
/*
 * The dialogue backlog's capture (port/src/system/backlog.c; xg-htn.57),
 * called by the game's text printer (src/main/dialog.c, engine/src/main/
 * dialog.c). Weak: programs that link the printer without the backlog.
 * Nothing here writes game state.
 *
 *   pcBacklogText  updateAndRenderTextForDialogWindow takes a window's next
 *                  text from its queue (a new text starts printing)
 *   pcBacklogLine  a glyph printed into the window's line image: the image
 *                  (stride halfwords a row, 13 rows), the glyph's x and width
 *                  (4-pixel units), its line and whether that line is the odd
 *                  one of its pair (bits 2-3)
 */
void pcBacklogText(const void* window) __attribute__((weak));
void pcBacklogLine(const void* window, const void* image, int stride, int x, int width, int line, int odd)
    __attribute__((weak));
/* A window of the port's own (the battle's Retry prompt, a versus battle's
 * prompt) whose text is not the game's: never kept. */
void pcBacklogIgnore(const void* window) __attribute__((weak));
#endif
#endif
