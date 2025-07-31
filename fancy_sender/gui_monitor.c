// gui_monitor.c

#include <ncurses.h>
#include <stdlib.h>

static int gui_enabled = 0;

void gui_init() {
    initscr();
    cbreak();
    noecho();
    timeout(0);
    gui_enabled = 1;
}

void gui_update(const char *format, int buffer_count) {
    if (!gui_enabled) return;

    clear();
    mvprintw(1, 2, "🎧 GStreamer 송신 상태 모니터");
    mvprintw(3, 4, "현재 포맷      : %s", format);
    mvprintw(4, 4, "버퍼 전송 횟수 : %d", buffer_count);
    mvprintw(6, 2, "[q]를 누르면 종료됩니다.");
    refresh();

    int ch = getch();
    if (ch == 'q') {
        endwin();
        exit(0);
    }
}

void gui_close() {
    if (gui_enabled) {
        endwin();
        gui_enabled = 0;
    }
}
