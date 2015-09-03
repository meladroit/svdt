#define TOP_WIDTH    50  // text columns
#define BOTTOM_WIDTH 40  // text columns
#define WIDTH        BOTTOM_WIDTH
#define HEIGHT       30  // text rows

enum colour
{
    BLACK,    RED,      GREEN,    BROWN,    INDIGO,   PURPLE,   TEAL,     GREY,
    DARKGREY, SALMON,   NEONGREEN,YELLOW,   BLUE,     MAGENTA,  CYAN,     WHITE
};  // notice that 0-7 are normal intensity, 8-15 are bold intensity

void gotoxy(int x, int y);
void textcolour(enum colour c);
int lastSpace(char *tocut);
void wordwrap(char *towrap, int width);
