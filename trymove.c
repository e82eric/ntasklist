#include <stdio.h>
#include <curses.h>
#include <windows.h>

BOOL g_needRepaint = FALSE;
WINDOW *g_my_win;

WINDOW *create_newwin(int height, int width, int starty, int startx)
{
  WINDOW *local_win = newwin(height, width, starty, startx);
  box(local_win, 0, 0);
  wrefresh(local_win);

  return (local_win);
}

void repaint()
{
    delwin(g_my_win);
    wclear(stdscr);
    wrefresh(stdscr);
    resize_term(0, 0);
    g_my_win = create_newwin(LINES - 1, COLS -1, 1, 1);
    g_needRepaint = FALSE;
}

int main(int argc, char **argv)
{
  WINDOW *my_win;

  int ch;
  int x = 2;
  int y = 2;

  initscr();
  cbreak();
  keypad(stdscr, TRUE);
  noecho();

  printw("Press q to exit");
  refresh();
  g_my_win = create_newwin(10, 20, y, x);
  wmove(g_my_win, y, x);
  wrefresh(g_my_win);


  /* MSG msg; */
  /* while (GetMessage(&msg, NULL, 0, 0)) */
  /* { */
  /*     TranslateMessage(&msg); */
  /*     DispatchMessage(&msg); */
  /* }; */

  while((ch = wgetch(stdscr)) != 'q')
    {
        switch (ch)
        {
            case 'j':
                /* if(g_needRepaint) */
                /* { */
                    repaint();
                    repaint();
                    repaint();
                /* } */
                /* y++; */
                /* wresize(g_my_win, y + 15, x + 15); */
                /* box(g_my_win, 1, 1); */
                /* wrefreshg_(my_win); */
                break;
            case 'k':
                y--;
                break;
            case KEY_RESIZE:
                g_needRepaint = TRUE;
                repaint();
                repaint();
                repaint();
                /* delwin(my_win); */
                /* wclear(stdscr); */
                /* wrefresh(stdscr); */
                /* /1* wclear(my_win); *1/ */
                /* /1* wrefresh(my_win); *1/ */
                /* resize_term(0, 0); */
                /* my_win = create_newwin(LINES - 1, COLS -1, 1, 1); */
                /* wmove(my_win, 1, 1); */
                /* wresize(my_win, LINES - 2, COLS - 2); */
                /* box(my_win, 0, 0); */
                /* wrefresh(my_win); */
        }
    }

  endwin();
  return 0;
}
