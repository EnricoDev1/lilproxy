#ifndef PROXY_TERM_H
#define PROXY_TERM_H

#define RESET               "\033[0m"
 
#define BOLD                "\033[1m"
#define DIM                 "\033[2m"
#define ITALIC              "\033[3m"
#define UNDERLINE           "\033[4m"
#define BLINK               "\033[5m"
#define BLINK_FAST          "\033[6m"
#define REVERSE             "\033[7m"
#define HIDDEN              "\033[8m"
#define STRIKETHROUGH       "\033[9m"
 
#define RESET_BOLD          "\033[22m"
#define RESET_DIM           "\033[22m"
#define RESET_ITALIC        "\033[23m"
#define RESET_UNDERLINE     "\033[24m"
#define RESET_BLINK         "\033[25m"
#define RESET_REVERSE       "\033[27m"
#define RESET_HIDDEN        "\033[28m"
#define RESET_STRIKETHROUGH "\033[29m"
 
#define BLACK               "\033[30m"
#define RED                 "\033[31m"
#define GREEN               "\033[32m"
#define YELLOW              "\033[33m"
#define BLUE                "\033[34m"
#define MAGENTA             "\033[35m"
#define CYAN                "\033[36m"
#define WHITE               "\033[37m"
#define DEFAULT_FG          "\033[39m"
 
#define BRIGHT_BLACK        "\033[90m"
#define BRIGHT_RED          "\033[91m"
#define BRIGHT_GREEN        "\033[92m"
#define BRIGHT_YELLOW       "\033[93m"
#define BRIGHT_BLUE         "\033[94m"
#define BRIGHT_MAGENTA      "\033[95m"
#define BRIGHT_CYAN         "\033[96m"
#define BRIGHT_WHITE        "\033[97m"
 
#define GRAY                BRIGHT_BLACK
#define DARK_GRAY           BRIGHT_BLACK
#define LIGHT_RED           BRIGHT_RED
#define LIGHT_GREEN         BRIGHT_GREEN
#define LIGHT_YELLOW        BRIGHT_YELLOW
#define LIGHT_BLUE          BRIGHT_BLUE
#define LIGHT_MAGENTA       BRIGHT_MAGENTA
#define LIGHT_CYAN          BRIGHT_CYAN
 
#define BG_BLACK            "\033[40m"
#define BG_RED              "\033[41m"
#define BG_GREEN            "\033[42m"
#define BG_YELLOW           "\033[43m"
#define BG_BLUE             "\033[44m"
#define BG_MAGENTA          "\033[45m"
#define BG_CYAN             "\033[46m"
#define BG_WHITE            "\033[47m"
#define DEFAULT_BG          "\033[49m"
 
#define BG_BRIGHT_BLACK     "\033[100m"
#define BG_BRIGHT_RED       "\033[101m"
#define BG_BRIGHT_GREEN     "\033[102m"
#define BG_BRIGHT_YELLOW    "\033[103m"
#define BG_BRIGHT_BLUE      "\033[104m"
#define BG_BRIGHT_MAGENTA   "\033[105m"
#define BG_BRIGHT_CYAN      "\033[106m"
#define BG_BRIGHT_WHITE     "\033[107m"
 
#define FG256(n)            "\033[38;5;" #n "m"
#define BG256(n)            "\033[48;5;" #n "m"
 
#define FGRGB(r,g,b)        "\033[38;2;" #r ";" #g ";" #b "m"
#define BGRGB(r,g,b)        "\033[48;2;" #r ";" #g ";" #b "m"
 
#define CURSOR_UP(n)        "\033[" #n "A"
#define CURSOR_DOWN(n)      "\033[" #n "B"
#define CURSOR_RIGHT(n)     "\033[" #n "C"
#define CURSOR_LEFT(n)      "\033[" #n "D"
#define CURSOR_NEXT_LINE(n) "\033[" #n "E"
#define CURSOR_PREV_LINE(n) "\033[" #n "F"
#define CURSOR_COL(n)       "\033[" #n "G"
#define CURSOR_POS(r,c)     "\033[" #r ";" #c "H"
#define CURSOR_HOME         "\033[H"
#define CURSOR_SAVE         "\033[s"
#define CURSOR_RESTORE      "\033[u"
#define CURSOR_HIDE         "\033[?25l"
#define CURSOR_SHOW         "\033[?25h"
 
#define ERASE_TO_END        "\033[0J"
#define ERASE_TO_START      "\033[1J"
#define ERASE_SCREEN        "\033[2J"
#define ERASE_SAVED         "\033[3J"
#define ERASE_LINE_END      "\033[0K"
#define ERASE_LINE_START    "\033[1K"
#define ERASE_LINE          "\033[2K"
 
#define CLEAR               ERASE_SCREEN CURSOR_HOME
#define CLEAR_LINE          ERASE_LINE
 
#define SCROLL_UP(n)        "\033[" #n "S"
#define SCROLL_DOWN(n)      "\033[" #n "T"
  
#define COLOR_ERROR         RED BOLD
#define COLOR_WARN          YELLOW BOLD
#define COLOR_SUCCESS       GREEN BOLD
#define COLOR_INFO          CYAN
#define COLOR_DEBUG         DIM WHITE
#define COLOR_HEADER        BLUE BOLD UNDERLINE
#define COLOR_PROMPT        BRIGHT_GREEN BOLD

#endif
