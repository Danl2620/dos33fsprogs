/*
** Fancy hello world program using cc65.
**
** Ullrich von Bassewitz (ullrich@von-bassewitz.de)
**
*/



#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <joystick.h>

#define max(a,b) (a > b ? a : b)

/*****************************************************************************/
/*                                   Data                                    */
/*****************************************************************************/

static const char Text [] = "Hello world!";


/*****************************************************************************/
/*                                   Globals                                 */
/*****************************************************************************/

unsigned char XSize, YSize;

/*****************************************************************************/
/*                                   Code                                    */
/*****************************************************************************/

void draw_screen (char * status, char * buffer)
{
    int status_pos = 0;

    /* Clear the screen, put cursor in upper left corner */
    clrscr ();

    /* Bottom line */
    gotoxy(0,YSize-1);
    cputc (CH_LLCORNER);
    status_pos++;
    cputc (' ');
    while (*status != 0)
    {
        cputc(*status++);
        status_pos++;
    }
    chline (XSize - status_pos);
    cputc (CH_LRCORNER);


    /* Write the greeting in the mid of the screen */
    gotoxy (0,0);
    cprintf (buffer);
}

int main (void)
{
    unsigned int len;
    char * buffer;
    char c;

    /* Set screen colors */
    (void) textcolor (COLOR_WHITE);
    (void) bordercolor (COLOR_BLUE);
    (void) bgcolor (COLOR_BLACK);

    /* Ask for the screen size */
    screensize (&XSize, &YSize);

    len = strlen(Text);

    buffer = malloc(max(len+1,512));
    strncpy(buffer, Text, len);
    buffer[len] = 0;



    for (;;)
    {
        draw_screen("NORMAL", buffer);

        /* Get input */
        c = cgetc ();

        buffer[len++] = c;
        buffer[len] = 0;

        /* Clear the screen again */
        clrscr ();
    }
    
    /* Done */
    return EXIT_SUCCESS;
}
