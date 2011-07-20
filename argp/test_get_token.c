// Copyright 2011 Bob Hepple
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// $Id: test_get_token.c,v 1.1 2011/07/20 23:27:32 bhepple Exp $

// A wrapper for glibc's argp(3) which is an extended
// version of getopt(1) - see --help below

// links:
// http://developer.gnome.org/glib/2.26/
// http://www.gnu.org/s/hello/manual/libc/Argp.html

// The point of it is - you get all the goodness of getopt plus
// automatic help and man page generation plus range checking.

// This program is ephemeral and tiny - so I don't care about freeing
// things or checking malloc!!!

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>
#include <locale.h>

#include <glib.h>
#include <glib/gprintf.h>

// Simplistic parse *inbuf as bash would!! Take account of ' and " quoting as well as \' \" \<space> \\
// Doesn't expand $ parameters or `...` !!!
gchar *get_token(gchar *inbuf, gchar **next)
{
    gchar *p;
    gchar *lastp;
    gchar terminator = 0;
    gint escape = 0;
    gchar *retval = g_malloc0(strlen(inbuf) + 1);
    gchar *d;

    d = retval;
    *d = 0;
    p = inbuf;
    // skip leading spaces:
    while (*p && g_ascii_isspace(*p)) p++;
    // look for a quote ' " otherwise we use space as terminator
    for (; *p; p++) {
        if (*p == '\\') {
            if (escape || (terminator == '\'')) {
                *d++ = *p;
                escape = 0;
            } else {
                escape = 1;
            }
            continue;
        }
        if (escape) {
            escape = 0;
            *d++ = *p;
            continue;
        }
            
        if (*p == terminator) {
            // skip this char
            terminator = 0;
            continue;
        }

        if (terminator == 0) {
            if ((*p == '\'') || (*p == '"')) {
                terminator = *p;
                continue;
            }
            if (g_ascii_isspace(*p)) {
                *d = 0;
                *next = *p? p + 1: p;
                return(retval);
            }
        }
        *d++ = *p;
    }
    *d = 0;
    *next = *p? p + 1: p;
    return(retval);
}

void read_stdin() 
{
#define MAXBUF 1024
    gchar inbuf[MAXBUF];
    gchar **array;
    gchar *p;
    gchar *tok;
    
    while (fgets(inbuf, MAXBUF, stdin)) {
        g_strchomp(inbuf);
        for (p = inbuf; p && *p; ) {
            tok = get_token(p, &p);
            g_print("Got '%s'\n", tok);
        }
    }
}

int main (int argc, char *argv[])
{
    
    read_stdin();
    
    exit(0);
}

/*
Local Variables: 
fill-column: 70 
eval: (setq tab-width 8)
End: 
01234567890123456789012345678901234567890123456789012345678901234567890123456789
*/
