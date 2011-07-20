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

// http://sourceforge.net/projects/process-getopt
// http://process-getopt.sourceforge.net
// http://bhepple.freeshell.org/oddmuse/wiki.cgi/process-getopt

// $Id: goption.c,v 1.1 2011/07/20 23:27:32 bhepple Exp $

// A wrapper for glib's g_option_context_parse(3) which is an extended
// version of getopt(1) - see --help below

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

static gchar *delim = " \t";
static GData *ranges = NULL;
static gboolean print_man_pagep = FALSE;

#define MAXOPTIONS 100 // who could ever want more?
static GOptionEntry entries[MAXOPTIONS];
static gint entry_num = 0;
static gchar *usage = NULL;
static gchar *prog = NULL;
static gint fd = 3;

char *goption_usage = "[--] [args]\n\
\n\
A wrapper for glib's g_option_context_parse(3) in place of getopt(1) - see\n\
http://developer.gnome.org/glib/stable/glib-Commandline-option-parser.html\n\
so you get all the goodness of getopt plus automatic help and man page\n\
generation.\n\
\n\
Normally used in a shell script as follows:\n\
\n\
******************************************************************************\n\
ARGS='\n\
PROG: my_prog\n\
#       long-name short-name flag type       arg       range     description\n\
OPTION: bool      b          -    boolean    -         -         foo the bar\n\
OPTION: int       i          -    integer    bars      2-6       number of bars to foo\n\
OPTION: string    s          -    string     things    \"a b c\"   string theory\n\
OPTION: dbl       d          -    double     km        0.1-10.0  kilometers to city\n\
USAGE:\n\
[--] [arg1 ... ]\n\
Short description of the shell script.\n\
Remainder of the description.\'\n\
exec 4>&1\n\
RES=$(echo \"$ARGS\" | ./goption \"$@\" 3>&1 1>&4 )\n\
[ \"$RES\" ] || exit 0 # probably from --help\n\
eval $RES\n\
******************************************************************************\n\
\n\
After goption has run, any options from \"$@\" have been processed into\n\
environment parameters and \"$@\" now contains any remaining arguments.\n\
\n\
Option fields are as follows:\n\
long-name : the long name of the option (mandatory)\n\
short-name: the single-letter short name of the option (use '-' if not needed)\n\
flag      : one or more of RH (Reversed or Hidden) (use '-' if not needed)\n\
type      : boolean, integer, string, filename, double (first letter is enough)\n\
arg       : the name of the argument to be used in --help (not for booleans)\n\
range     : numeric options: min-max\n\
            for string/files, a space separated list of alternates in \"...\"\n\
            or '...'\n\
\n\
The options can be followed by a line:\n\
USAGE:\n\
followed by the usage message (on one or more lines) to be printed by --help.\n\
\n\
Alternatively, USAGE can be exported from the shell as an environment \n\
parameter (but the version on stdin takes precedent).\n\
\n\
Note that a 'verbose' option will be added automatically.\n\
Also, a hidden option '--print-man-page' is provided which prints a skeleton\n\
man(1) page which can be used as the starting point for a full man page.\n\
For this, the first line of USAGE is used as the 'arguments' for the man\n\
entry and the second line is the 'Synopsis' or short description.\
";

void abend(gchar *msg) 
{
    g_printerr("%s: %s\n", prog? prog: "", msg);
    // redirect stdout to fd
    dup2(fd, 1);
    g_print("exit 1");
    exit(1);
}

// returns a list of short options that take no parameter
gchar *print_short_flags() 
{
    gchar *retval = g_strdup("");
    gchar buf[2] = " ";
    GOptionEntry *entry;

    for (entry = entries; entry->long_name; entry++) {
        if (entry->flags & G_OPTION_FLAG_HIDDEN) continue;
        if (g_ascii_isalpha(entry->short_name)) {
            buf[0] = entry->short_name;
            retval = g_strconcat(retval, buf, NULL);
        }
    }
    return(retval);
}

// prints only long options that take no parameter
gchar *print_long_flags()
{
    gchar *retval = g_strdup("");
    GOptionEntry *entry;

    for (entry = entries; entry->long_name; entry++) {
        if (entry->flags & G_OPTION_FLAG_HIDDEN) continue;
        if (entry->arg_description == NULL) {
            retval = g_strconcat(retval, "\\-\\-", entry->long_name, " ", NULL);
        }
    }
    return(retval);
}

// prints short and long options that take a parameter
void print_all_args() {
    GOptionEntry *entry;

    for (entry = entries; entry->long_name; entry++) {
        if (entry->flags & G_OPTION_FLAG_HIDDEN) continue;
        if (entry->arg_description) {
            g_print("\n [");
            if (g_ascii_isalpha(entry->short_name)) {
                g_print("\\fB\\-%c\\fP \\fI%s\\fP,",
                        entry->short_name,
                        entry->arg_description);
            }
            g_print("\\fB\\-\\-%s\\fP=\\fI%s\\fP]",
                    entry->long_name,
                    entry->arg_description);
        }
    }
}

// print the option line for a man page
void print_opt_man(GOptionEntry *entry) {
    if (entry->flags & G_OPTION_FLAG_HIDDEN) return;

    g_print(".TP\n");
    g_print(".B ");

    if (g_ascii_isalpha(entry->short_name)) {
        g_print("\\fB\\-%c\\fP", entry->short_name);
    }
    
    if (entry->arg_description) {
        g_print(" \\fI%s\\fR", entry->arg_description);
    }
    
    if (g_ascii_isalpha(entry->short_name)) {
        g_print(", ");
    }
    
    g_print("\\fB\\-\\-%s\\fP", entry->long_name);
    
    if (entry->arg_description) {
        g_print("\\fI=%s\\fR", entry->arg_description);
    }
    g_print("\n%s\n", entry->description);
}

void print_man_page() 
{
    gchar *flags;
    gchar *lflags;
    GOptionEntry *entry;
    gchar **usage_array = g_strsplit(usage, "\n", 0);
    gchar *line;
    gint i;
    
    flags = print_short_flags();
    if (flags && flags[0]) {
        flags = g_strconcat("\n.RB \"[\" \\-", flags, " \"]\"", NULL);
    }
    
    lflags = g_strconcat("\n[\n.B ", print_long_flags(), "\n]", NULL);
    
    g_print(".TH %s 1 \\\" -*- nroff -*-\n", g_ascii_strup(prog, -1));
    g_print(".SH NAME\n");
    g_print("%s \\- %s\n", prog, usage_array[1]);
    g_print(".SH SYNOPSIS\n.hy 0\n.na\n");
    g_print(".B %s%s%s", prog, flags, lflags);
    print_all_args();
    g_print(" %s\n", usage_array[0]);
    g_print(".ad b\n.hy 0\n.SH DESCRIPTION\n");
    for (i = 1; ; i++) {
        line = usage_array[i];
        if (!line) break;
        g_print("%s\n", line);
    }
    g_print(".P\nFoobar foobar foobar\n.SH OPTIONS\n");

    for (entry = entries; entry->long_name; entry++) {
        print_opt_man(entry);
    }

    g_print("\
.SH \"EXIT STATUS\"\n\
.SH \"ENVIRONMENT\"\n\
.SH \"FILES\"\n\
.SH \"EXAMPLES\"\n\
.SH \"NOTES\"\n\
.SH \"BUGS\"\n\
.SH \"SEE ALSO\"\n\
.SH \"AUTHOR\"\n\
Written by Foo Bar <foobar@foobar.org>\n\
.P\n\
.RB http://foobar.foobar.org/foobar\n\
.SH \"COPYRIGHT\"\n\
Copyright (c) 2008-2011 Robert Hepple\n\
.br\n\
This program is free software; you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation; either version 2 of the License, or\n\
(at your option) any later version.\n\
.P\n\
This program is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
GNU General Public License for more details.\n\
.P\n\
You should have received a copy of the GNU General Public License\n\
along with this program; if not, write to the Free Software\n\
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n\
");
}

void range_check_entry(GOptionEntry *entry)
{
    gchar *range_string = g_datalist_get_data(&ranges, entry->long_name);
    gint min = 0;
    gint max = 0;
    gdouble fmin = 0;
    gdouble fmax = 0;
    gchar **array;
    gchar **iter;
    
    if (range_string && *range_string) {
        switch (entry->arg) {
        case G_OPTION_ARG_INT:
            sscanf(range_string, "%d-%d", &min, &max);
            if ((*(gint *)entry->arg_data >= min) &&
                (*(gint *)entry->arg_data <= max)) {
                return;
            }
            break;

        case G_OPTION_ARG_DOUBLE:
            sscanf(range_string, "%lf-%lf", &fmin, &fmax);
           if ((*(gdouble *)entry->arg_data >= fmin) &&
                (*(gdouble *)entry->arg_data <= fmax)) {
                return;
            }
            break;

        case G_OPTION_ARG_STRING:
        case G_OPTION_ARG_FILENAME:
            array = g_strsplit_set(range_string, delim, -1);
            for (iter = array; *iter; iter++) {
                if (g_strcmp0(*iter, *(gchar **)entry->arg_data) == 0) {
                    return;
                }
            }
            break;
        default:
            return;
        }
                
        abend(g_strdup_printf("%s: value out of range", entry->long_name));
    }
    return;
}

void print_entry(GOptionEntry *entry)
{
    gchar *param = g_ascii_strup(entry->long_name, -1);
    g_print("export %s=\"", param);
    switch (entry->arg) {
    case G_OPTION_ARG_INT:
        if (entry->arg_data) g_print("%d", *(gint *)entry->arg_data);
        break;
    case G_OPTION_ARG_NONE:
        if (*(gboolean *)(entry->arg_data)) {
            g_print("yes");
        }
        break;
    case G_OPTION_ARG_STRING:
    case G_OPTION_ARG_FILENAME:
        if (entry->arg_data) g_print("%s", *(gchar **)entry->arg_data);
        break;
    case G_OPTION_ARG_DOUBLE:
        if (entry->arg_data) g_print("%lf", *(gdouble *)entry->arg_data);
        break;
    default:
        break;
    }
    g_print("\"; ");
}

gpointer new_data(GOptionArg type)
{
    gint size = sizeof(gchar *);
  
    switch (type) {
    case G_OPTION_ARG_NONE:
        size = sizeof(gboolean);
        break;
    case G_OPTION_ARG_STRING:
    case G_OPTION_ARG_FILENAME:
        size = sizeof(gchar *);
        break;
    case G_OPTION_ARG_INT:
        size = sizeof(gint);
        break;
    case G_OPTION_ARG_DOUBLE:
        size = sizeof(gdouble *);
        break;
    default:
        size = sizeof(gdouble *);
        break;
    }
    return(g_malloc0(size));
}

void set_entry(GOptionEntry *entry,
               gchar *long_name,
               gchar short_name,
               gint flags,
               GOptionArg type,
               gpointer arg_data,
               gchar *description,
               gchar *arg_description)
{
    gchar *def = getenv(g_ascii_strup(long_name, -1));
    
    entry->long_name = g_strdup(long_name);
    entry->short_name = short_name;
    entry->flags = flags;
    entry->arg = type;
    switch (type) {
    case G_OPTION_ARG_NONE:
        if (flags & G_OPTION_FLAG_REVERSE) {
            *(gboolean *)arg_data = 1;
        }
        if (def) {
            *(gboolean *)arg_data = 1;
        }
        break;
    case G_OPTION_ARG_STRING:
    case G_OPTION_ARG_FILENAME:
        *(gchar **)arg_data = "";
        if (def) {
            *(gchar **)arg_data = g_strdup(def);
        }
        break;
    case G_OPTION_ARG_INT:
        if (def) {
            *(gint *)arg_data = g_ascii_strtoll(def, NULL, 10);
        }
        break;
    case G_OPTION_ARG_DOUBLE:
        if (def) {
            *(gdouble *)arg_data = g_ascii_strtod(def, NULL);
        }
        break;
    default:
        break;
    }
    entry->arg_data = arg_data;
    entry->description = g_strdup(description);
    entry->arg_description = g_strdup(arg_description);
}

void read_option(gchar *inbuf) 
{
    gchar *tok = NULL;
    gchar *name = NULL;
    gchar *letter = g_strdup("-");
    gint flag = 0;
    gchar *type = g_strdup("b");
    gchar *arg = NULL;
    gchar *desc = NULL;
    gchar *range_string = NULL;
    GOptionArg arg_type;
    gchar **array;
    int errcode;
    gchar *c;
    gchar tick;
    gchar *buf = NULL;
    static gchar *long_name_regex = "^[[:alpha:]][[:alnum:]_-]*$";
    static regex_t long_name_regex_c;
    static regex_t *long_name_regex_p = NULL;

    g_strchug(inbuf);
    
#ifdef DEBUG
    g_printerr("inbuf = '%s'\n", inbuf);
#endif

    array = g_strsplit_set(inbuf, delim, 2);
    name = array[0];
    buf = g_strchug(array[1]);
        
#ifdef DEBUG
    g_printerr("long_name = '%s'\n", name);
#endif

    if (long_name_regex_p == NULL) {
        if (regcomp(&long_name_regex_c, long_name_regex, REG_EXTENDED|REG_NOSUB)) {
            abend("internal error - bad regex");
        }
        long_name_regex_p = &long_name_regex_c;
    }
    if ((errcode = regexec(&long_name_regex_c, name, 0, NULL, 0))) {
        abend(g_strdup_printf("%s: illegal character", name));
    }

    while (1) {
        array = g_strsplit_set(buf, delim, 2);
        letter = array[0];
        buf = g_strchug(array[1]);

        if (!letter) break;
        if (letter[0] == '-') letter[0] = 0;

#ifdef DEBUG
        g_printerr("letter='%c'\n", letter[0]);
#endif
        flag = 0;
        array = g_strsplit_set(buf, delim, 2);
        tok = array[0];
        buf = g_strchug(array[1]);

        if (!tok) break;
        
#ifdef DEBUG
        g_printerr("flag='%s'\n", tok);
#endif
        for (c = tok; *c; c++) {
            switch (g_ascii_tolower(*c)) {
            case 'h': flag |= G_OPTION_FLAG_HIDDEN; break;
            case 'r': flag |= G_OPTION_FLAG_REVERSE; break;
            case '-':
            case '0': break;
            default:
                abend(g_strdup_printf(
                          "illegal flag character in '%s'", tok));
            }
        }

        array = g_strsplit_set(buf, delim, 2);
        type = array[0];
        buf = g_strchug(array[1]);
        if (!type) break;
        
#ifdef DEBUG
        g_printerr("type='%c'\n", type[0]);
#endif
         
        switch (g_ascii_tolower(type[0])) {
        case 'i': arg_type = G_OPTION_ARG_INT;      break;
        case 's': arg_type = G_OPTION_ARG_STRING;   break;
        case 'f': arg_type = G_OPTION_ARG_FILENAME; break;
        case 'd': arg_type = G_OPTION_ARG_DOUBLE;   break;
        default:
            arg_type = G_OPTION_ARG_NONE;
            arg = NULL;
            break;
        }

        array = g_strsplit_set(buf, delim, 2);
        arg = array[0];
        buf = g_strchug(array[1]);
        if (!arg) break;
#ifdef DEBUG
        g_printerr("arg='%s'\n", arg);
#endif
        if (arg_type == G_OPTION_ARG_NONE) arg = NULL;
                        
        range_string = buf;
        if (!range_string) break;
                        
        switch (arg_type) {
        case G_OPTION_ARG_STRING:
        case G_OPTION_ARG_FILENAME:
            tick = *range_string++;
            switch (tick) {
            case '\'':
            case '"':
                for (c = range_string;
                     *c && (*c != tick);
                     c++) ;
                if (*c) *c++ = 0;
                desc = c + 1;
                desc = g_strchug(desc);
                break;
            case '-':
                break;
            default:
                abend(g_strdup_printf("%s: range for a string or filename "
                                      "must start with ' or \"", name));
            }
            break;
        default:
            break;
        }
        g_datalist_set_data(&ranges, name,
                            g_strdup(range_string));

        if (!desc) {
            array = g_strsplit_set(range_string, delim, 2);
            range_string = array[0];
            desc = g_strchug(array[1]);
        }
#ifdef DEBUG
        g_printerr("range_string='%s'\n", range_string);
        g_printerr("desc='%s'\n", desc);
#endif
        break;
    }

    set_entry(&entries[entry_num++], name, letter[0], flag, arg_type,
              new_data(arg_type), desc, arg);
}

void read_stdin() 
{
#define OPTION_LINE "OPTION:"
#define USAGE_LINE "USAGE:"
#define PROG_LINE "PROG:"
#define MAXBUF 1024
    gchar inbuf[MAXBUF];
    gchar **array;
    
    fcntl(0, F_SETFL, O_NONBLOCK);
    
    while (fgets(inbuf, MAXBUF, stdin)) {
        gchar *tok = NULL;
        gchar *line_label = NULL;

#ifdef DEBUG
        g_printerr("got: '%s'\n", inbuf);
#endif
        g_strchomp(inbuf);
        
        if ((inbuf[0] == '#') || (inbuf[0] == 0)) continue;

        array = g_strsplit_set(inbuf, delim, 2);
        tok = array[0];
        
        if (tok) {
            line_label = tok;
            if (g_strcmp0(line_label, OPTION_LINE) == 0) {
                read_option(array[1]);
            } else if (g_strcmp0(line_label, PROG_LINE) == 0) {
                prog = g_strdup(array[1]);
            } else if (g_strcmp0(line_label, USAGE_LINE) == 0) {
                // read to the end of the file and shove into "usage"
                while (fgets(inbuf, MAXBUF, stdin)) {
                    if (usage) {
                        usage = g_strconcat(usage, inbuf, NULL);
                    } else {
                        usage = g_strdup(inbuf);
                    }
                }
            }
        }
    }
}

void print_parse_error(GOptionContext *context,
                       GOptionGroup *group,
                       gpointer data,
                       GError **error) 
{
    abend((*error)->message);
}

int main (int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;
    gint i;
    GOptionEntry *entry;

    g_datalist_init(&ranges);
    
    read_stdin();
    
    if (!usage) usage = getenv("USAGE");
    
    if (!usage || entry_num == 0) {
        usage = goption_usage;
        argv[1] = "-h";
        argc = 2;
    } else {
        // everyone needs this - really! as well as (-h,--help)
        set_entry(&entries[entry_num++], "verbose", 'v', 0, G_OPTION_ARG_NONE,
                  new_data(G_OPTION_ARG_NONE), "be verbose", NULL);
        set_entry(&entries[entry_num++], "print-man-page", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE,
                  &print_man_pagep, NULL, NULL);
    }

    if (prog) argv[0] = prog;
    
    // terminate our list:
    entries[entry_num].long_name = NULL;

    g_strchomp(usage);
    
    context = g_option_context_new(usage);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_group_set_error_hook(g_option_context_get_main_group(context),
                                  print_parse_error);
    
    // g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        abend(g_strdup_printf("option parsing failed: %s", error->message));
    }

    if (print_man_pagep) {
        print_man_page();
        exit(0);
    }
    
    for (entry = entries; entry->long_name; entry++) range_check_entry(entry);
    
    // redirect stdout to fd
    dup2(fd, 1);
    
    for (entry = entries; entry->long_name; entry++) print_entry(entry);
    
    g_print("set -- ");
    for (i = 1; i < argc; i++) g_print("\"%s\" ", argv[i]);
    g_print(";\n");
    
    exit(0);
}

/*
Local Variables: 
fill-column: 70 
eval: (setq tab-width 8)
eval: (setq filename (substring buffer-file-name (string-match "[a-zA-Z0-9_.]+$" buffer-file-name))) 
eval: (setq compile-command "cc -Wall -o goption $(pkg-config --cflags --libs glib-2.0) -g goption.c")
End: 
01234567890123456789012345678901234567890123456789012345678901234567890123456789
*/
