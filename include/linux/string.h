#ifndef _LINUX_STRING_H
#define _LINUX_STRING_H

char *strstr (const char *string1, const char *string2);

char *strncpy (char *string1, const char *string2, size_t count);

int   strcmp (const char *string1, const char *string2);

int   strncmp (const char *string1, const char *string2, size_t count);

#endif

