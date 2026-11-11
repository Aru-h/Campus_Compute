/*
 * util.c - tiny helper functions used by several modules
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "campuscompute.h"

/*
 * Convert text such as "42" into a number and check the range.
 * Returns 0 on success, -1 if the text is not a whole number or is out of range.
 * (atoi() would silently accept "abc" as 0, so we use strtol and check.)
 */
int parse_number(const char *text, long min, long max, long *out)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
        return -1;                      /* not a clean number */
    if (value < min || value > max)
        return -1;                      /* out of range */
    *out = value;
    return 0;
}

/* Print a horizontal line, for tidy output. */
void print_line(char ch, int count)
{
    int i;
    for (i = 0; i < count; i++)
        putchar(ch);
    putchar('\n');
}
