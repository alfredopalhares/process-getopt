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

// $Id: argp.c,v 1.1 2011/07/20 23:27:32 bhepple Exp $

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
#include <error.h>
#include <argp.h>

static gchar *delim = " \t";

// argp uses an array of struct which obviously can't be extended (if
// only they'd use an array of struct *!!!). So put extra per-option
// information in this structure indexed by option long-name:
static GData *ext_entries = NULL;
static gboolean print_man_pagep = FALSE;

// used for the non-zero but unprintable key value for options that
// don't have a short_name:
#define USAGE_KEY 0x300000
static gint anon_key = USAGE_KEY + 1;

typedef enum ARG_TYPE {
    ARG_TYPE_BOOLEAN = 0,
    ARG_TYPE_INT,
    ARG_TYPE_DOUBLE,
    ARG_TYPE_STRING,
    ARG_TYPE_ARRAY
} ARG_TYPE;

typedef struct ext_entry
{
    ARG_TYPE type;
    gpointer arg_data;
    gchar *range_string;
} Ext_entry;
    
#define MAXOPTIONS 100 // who could ever want more?
typedef struct argp_option Argp_option;
static Argp_option entries[MAXOPTIONS];
const char *argp_program_version = "1.0";
const char *argp_program_bug_address = "bhepple@freeshell.org";

static gint entry_num = 0;
static gchar *usage = NULL;
static gchar *arguments = NULL;
static gchar *prog = NULL;
static gint fd = 3;
static int new_argc = 0;
static char *new_argv[1000] = { NULL }; // who could ever want more?

char *argp_internal_usage = "[--] [args]\n\
\n\
A wrapper for glibc's argp(3) in place of getopt(1) - see\n\
http://www.gnu.org/s/hello/manual/libc/Argp.html\n\
... so you get all the goodness of getopt plus automatic help and man page\n\
generation.\n\
\n\
Normally used in a shell script as follows:\n\
\n\
******************************************************************************\n\
ARGS='\n\
PROG: my_prog\n\
add_opt name \"description\" short-name arg long-name arg type \"range\"\n\
USAGE:\n\
[--] [arg1 ... ]\n\
Short description of the shell script.\n\
Remainder of the description.\'\n\
exec 4>&1\n\
RES=$(echo \"$ARGS\" | ./arpg \"$@\" 3>&1 1>&4 )\n\
[ \"$RES\" ] || exit 0 # probably from --help\n\
eval $RES\n\
******************************************************************************\n\
\n\
After argp has run, any options from \"$@\" have been processed into\n\
environment parameters and \"$@\" now contains any remaining arguments.\n\
\n\
add_opt fields are provided for compatibility with process-getopt(1) as follows:\n\
name      : will be ignored but must be present!\n\
descrip   : leave empty for hidden options\n\
short-name: the single-letter short name of the option (use '-' if not needed)\n\
short-arg : the name of the argument to be used in --help (not for booleans)\n\
long-name : the long name of the option (mandatory)\n\
long-arg  : same as short-arg\n\
type      : boolean, integer, string or array, double (first letter is enough)\n\
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
    Argp_option *entry;

    for (entry = entries; entry->name; entry++) {
        if (entry->flags & OPTION_HIDDEN) continue;
        if (g_ascii_isalpha(entry->key)) {
            buf[0] = entry->key;
            retval = g_strconcat(retval, buf, NULL);
        }
    }
    return(retval);
}

// prints only long options that take no parameter
gchar *print_long_flags()
{
    gchar *retval = g_strdup("");
    Argp_option *entry;

    for (entry = entries; entry->name; entry++) {
        if (entry->flags & OPTION_HIDDEN) continue;
        if (entry->arg == NULL) {
            retval = g_strconcat(retval, "\\-\\-", entry->name, " ", NULL);
        }
    }
    return(retval);
}

// prints short and long options that take a parameter
void print_all_args() {
    Argp_option *entry;

    for (entry = entries; entry->name; entry++) {
        if (entry->flags & OPTION_HIDDEN) continue;
        if (entry->arg) {
            g_print("\n [");
            if (g_ascii_isalpha(entry->key)) {
                g_print("\\fB\\-%c\\fP \\fI%s\\fP,",
                        entry->key,
                        entry->arg);
            }
            g_print("\\fB\\-\\-%s\\fP=\\fI%s\\fP]",
                    entry->name,
                    entry->arg);
        }
    }
}

// print the option line for a man page
void print_opt_man(Argp_option *entry) {
    if (entry->flags & OPTION_HIDDEN) return;

    g_print(".TP\n");
    g_print(".B ");

    if (g_ascii_isalpha(entry->key)) {
        g_print("\\fB\\-%c\\fP", entry->key);
    }
    
    if (entry->arg) {
        g_print(" \\fI%s\\fR", entry->arg);
    }
    
    if (g_ascii_isalpha(entry->key)) {
        g_print(", ");
    }
    
    g_print("\\fB\\-\\-%s\\fP", entry->name);
    
    if (entry->arg) {
        g_print("\\fI=%s\\fR", entry->arg);
    }
    g_print("\n%s\n", entry->doc);
}

void print_man_page() 
{
    gchar *flags;
    gchar *lflags;
    Argp_option *entry;
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

    for (entry = entries; entry->name; entry++) {
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

    exit(0);
}

void range_check_entry(Argp_option *entry)
{
    Ext_entry *ext_entry =
        (Ext_entry *) g_datalist_get_data(&ext_entries, entry->name);
    gint min = 0;
    gint max = 0;
    gdouble fmin = 0;
    gdouble fmax = 0;
    gchar **array;
    gchar **iter;
    gchar *ptr = NULL;
    
    if (ext_entry) {
        gchar *range_string = ext_entry->range_string;
        if (range_string && *range_string) {
            switch (ext_entry->type) {
            case ARG_TYPE_INT:
                sscanf(range_string, "%d-%d", &min, &max);
                if ((*(gint *)ext_entry->arg_data >= min) &&
                    (*(gint *)ext_entry->arg_data <= max)) {
                    return;
                }
                break;

            case ARG_TYPE_DOUBLE:
                sscanf(range_string, "%lf-%lf", &fmin, &fmax);
                if ((*(gdouble *)ext_entry->arg_data >= fmin) &&
                    (*(gdouble *)ext_entry->arg_data <= fmax)) {
                    return;
                }
                break;

            case ARG_TYPE_STRING:
            {
                static regex_t regex_c;

                ptr = *(gchar **)ext_entry->arg_data;
                if (regcomp(&regex_c, range_string, REG_EXTENDED|REG_NOSUB)) {
                    abend("internal error - bad regex");
                }
                if (regexec(&regex_c, *(gchar **)ext_entry->arg_data, 0, NULL, 0) == 0) {
                    return;
                }
            }
            break;
                
            case ARG_TYPE_ARRAY:
                ptr = *(gchar **)ext_entry->arg_data;
                array = g_strsplit_set(range_string, delim, -1);
                for (iter = array; *iter; iter++) {
                    if (g_strcmp0(*iter, *(gchar **)ext_entry->arg_data) == 0) {
                        return;
                    }
                }
                break;
            default:
                return;
            }
                
            abend(g_strdup_printf("option %s: value '%s' out of range ('%s')", entry->name, ptr, range_string));
        }
    }
    return;
}

void print_entry(Argp_option *entry)
{
    Ext_entry *ext_entry =
        (Ext_entry *) g_datalist_get_data(&ext_entries, entry->name);
    gchar *param = g_ascii_strup(entry->name, -1);
    {
        gchar *t;
        for (t = param; *t; t++) if (!g_ascii_isalnum(*t)) *t = '_';
    }
    
    g_print("export %s=\"", param);
    switch (ext_entry->type) {
    case ARG_TYPE_INT:
        if (ext_entry->arg_data) g_print("%d", *(gint *)ext_entry->arg_data);
        break;
    case ARG_TYPE_BOOLEAN:
        if (*(gboolean *)(ext_entry->arg_data)) {
            g_print("yes");
        }
        break;
    case ARG_TYPE_STRING:
    case ARG_TYPE_ARRAY:
        if (ext_entry->arg_data) g_print("%s", *(gchar **)ext_entry->arg_data);
        break;
    case ARG_TYPE_DOUBLE:
        if (ext_entry->arg_data) g_print("%lf", *(gdouble *)ext_entry->arg_data);
        break;
    default:
        break;
    }
    g_print("\"; ");
}

gpointer new_data(ARG_TYPE type)
{
    gint size = sizeof(gchar *);
  
    switch (type) {
    case ARG_TYPE_BOOLEAN:
        size = sizeof(gboolean);
        break;
    case ARG_TYPE_STRING:
    case ARG_TYPE_ARRAY:
        size = sizeof(gchar *);
        break;
    case ARG_TYPE_INT:
        size = sizeof(gint);
        break;
    case ARG_TYPE_DOUBLE:
        size = sizeof(gdouble *);
        break;
    default:
        size = sizeof(gdouble *);
        break;
    }
    return(g_malloc0(size));
}

void set_entry(Argp_option *entry,
               gchar *name,
               gint key,
               gint flags,
               ARG_TYPE type,
               gpointer arg_data,
               gchar *doc,
               gchar *arg,
               gchar *range_string)
{
    gchar *def = getenv(g_ascii_strup(name, -1));
    Ext_entry *ext_entry = g_malloc0(sizeof(Ext_entry));

    if (entry_num >= MAXOPTIONS) abend("Too many options.");

    if (g_datalist_get_data(&ext_entries, name)) {
        abend(g_strdup_printf("duplicate option name '%s'", name));
    }
       
    entry->name = g_strdup(name);
    entry->key = key;
    entry->flags = flags;
    ext_entry->type = type;
    switch (type) {
    case ARG_TYPE_BOOLEAN:
        if (def && *def) {
            *(gboolean *)arg_data = 1;
        }
        break;
    case ARG_TYPE_ARRAY:
    case ARG_TYPE_STRING:
        *(gchar **)arg_data = "";
        if (def) {
            *(gchar **)arg_data = g_strdup(def);
        }
        break;
    case ARG_TYPE_INT:
        if (def) {
            *(gint *)arg_data = g_ascii_strtoll(def, NULL, 10);
        }
        break;
    case ARG_TYPE_DOUBLE:
        if (def) {
            *(gdouble *)arg_data = g_ascii_strtod(def, NULL);
        }
        break;
    default:
        break;
    }
    entry->doc = g_strdup(doc);
    if (arg && !*arg) arg = NULL;
    entry->arg = g_strdup(arg);
    ext_entry->arg_data = arg_data;
    ext_entry->range_string = g_strdup(range_string);
#ifdef DEBUG
    g_printerr("set_entry: adding '%s' key=%d flags=%d type=%d doc='%s' arg='%s' range='%s'",
               name, key, flags, type, doc, arg, range_string);
#endif
    g_datalist_set_data(&ext_entries, name, ext_entry);
}

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

void read_add_opt(gchar *inbuf)
{
    gchar *next = inbuf;
    gchar *name = NULL;
    gchar *desc = NULL;
    gchar *short_name = NULL;
    gchar *short_arg = NULL;
    gchar *long_name = NULL;
    gchar *long_arg = NULL;
    gchar *type = NULL;
    gchar *range = NULL;
    gint flag = 0;
    ARG_TYPE arg_type;
    
    while (1) {
        name = get_token(next, &next);
        if (!*next) break;
        desc = get_token(next, &next);
        if (!*next) break;
        short_name = get_token(next, &next);
        if (!*next) break;
        short_arg = get_token(next, &next);
        if (!*next) break;
        long_name = get_token(next, &next);
        if (!*next) break;
        long_arg = get_token(next, &next);
        if (!*next) break;
        type = get_token(next, &next);
        if (!*next) break;
        range = get_token(next, &next);
        break;
    }
#ifdef DEBUG
    g_printerr("read_add_opt: name='%s'\n", name);
    g_printerr("read_add_opt: desc='%s'\n", desc);
    g_printerr("read_add_opt: short_name='%s'\n", short_name);
    g_printerr("read_add_opt: short_arg='%s'\n", short_arg);
    g_printerr("read_add_opt: long_name='%s'\n", long_name);
    g_printerr("read_add_opt: long_arg='%s'\n", long_arg);
    g_printerr("read_add_opt: type='%s'\n", type);
    g_printerr("read_add_opt: range='%s'\n", range);
#endif

    if (!short_name || !short_name[0]) {
        short_name = g_strdup(" ");
        short_name[0] = anon_key++;
    }
    
    if (!long_name || !*long_name) long_name = g_ascii_strdown(name, -1);
    
    if (long_arg && *long_arg && (!short_arg || !*short_arg)) short_arg = long_arg;

    if (!range || !*range) range = g_strdup("");
    
    // convert to our 'types' (ranges should be the same)
    if (!type) type = g_strdup("b");
    
    switch (g_ascii_tolower(type[0])) {
    case 'u':
    {
        regex_t regex_c;
                
        range = g_strdup("^(nfs|http|https|ftp|file)://[[:alnum:]_.-]*[^[:space:]]*$");
        if (regcomp(&regex_c, range, REG_EXTENDED|REG_NOSUB)) {
            abend(g_strdup_printf("%s: internal error: illegal regex ('%s') for url", long_name, range));
        }
        arg_type = ARG_TYPE_STRING;
    }
    break;
    case 'f':
        arg_type = ARG_TYPE_DOUBLE;
        break;
    case 'i':
        arg_type = ARG_TYPE_INT;
        break;
    case 's':
        arg_type = ARG_TYPE_STRING;
        if (range && *range) {
            regex_t regex_c;
                
            if (regcomp(&regex_c, range, REG_EXTENDED|REG_NOSUB)) {
                abend(g_strdup_printf("%s: illegal regex ('%s')", long_name, range));
            }
        }
        break;
    case 'a':
        arg_type = ARG_TYPE_ARRAY;
        break;
    default:
        arg_type = ARG_TYPE_BOOLEAN;
        short_arg = long_arg = NULL;
        break;
    }
    
    if (!*desc) flag = OPTION_HIDDEN;
    set_entry(&entries[entry_num++], long_name, short_name[0], flag, arg_type,
              new_data(arg_type), desc, short_arg, range);
}

void read_stdin() 
{
#define ADD_OPT_LINE "add_opt"
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
        g_strstrip(inbuf);
        
        if ((inbuf[0] == '#') || (inbuf[0] == 0)) continue;

        array = g_strsplit_set(inbuf, delim, 2);
        tok = array[0];
        
        if (tok) {
            line_label = tok;
            if (g_strcmp0(line_label, ADD_OPT_LINE) == 0) {
                read_add_opt(array[1]);
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

/* Parse a single option. */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
    Argp_option *entry;
    Ext_entry *ext_entry;
    error_t retval = 0;

#ifdef DEBUG
    g_printerr("parse_opt: got %x (%c) '%s'\n", key, g_ascii_isprint(key)? key: ' ', arg);
#endif
    
    switch(key) {
    case ARGP_KEY_INIT:
    case ARGP_KEY_END:
    case ARGP_KEY_NO_ARGS:
    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_FINI:
        goto hell;
     
    case ARGP_KEY_ARG:
        if (arg) {
            new_argv[new_argc++] = arg;
        }
        goto hell;

    // for some reason the GNU people don't like '-h' anymore
    // although they retain '-?'. Go figure.
    case 'h':
        argp_help(state->root_argp, stdout, ARGP_HELP_STD_HELP, prog);
        exit(0);

#if 0
    // something weird about re-defining 'usage' option - it plain doesn't work
    case USAGE_KEY:
        argp_help(state->root_argp, stdout, ARGP_HELP_USAGE, prog);
        exit(0);
#endif
        
    default:
        break;
    }

    for (entry = entries; entry->name; entry++) {
        if (entry->key == key) break;
    }
    if (!entry->name) {
        retval = ARGP_ERR_UNKNOWN;
        goto hell;
    }

    ext_entry = (Ext_entry *) g_datalist_get_data(&ext_entries, entry->name);
    if (!ext_entry) {
        retval = ARGP_ERR_UNKNOWN;
        goto hell;
    }

    switch (ext_entry->type) {
    case ARG_TYPE_INT:
        *(gint *)ext_entry->arg_data = g_ascii_strtoll(arg, NULL, 10);
        break;

    case ARG_TYPE_DOUBLE:
        *(gdouble *)ext_entry->arg_data = g_ascii_strtod(arg, NULL);
        break;

    case ARG_TYPE_ARRAY:
    case ARG_TYPE_STRING:
        *(gchar **)ext_entry->arg_data = g_strdup(arg);
        break;

    case ARG_TYPE_BOOLEAN:
        *(gboolean *)ext_entry->arg_data = 1;
        break;

    default:
        retval = ARGP_ERR_UNKNOWN;
    }

hell:
    return 0;
}

int main (int argc, char *argv[])
{
    static struct argp argp = { entries, parse_opt, NULL, NULL };

    new_argv[new_argc++] = argv[0];
    
    g_datalist_init(&ext_entries);
    
    read_stdin();
    
    if (!usage) usage = getenv("USAGE");
    
    if (!usage || entry_num == 0) {
        // nothing read in from stdin: assume they need help!
        usage = argp_internal_usage;
        argv[1] = "--help";
        argc = 2;
    }
    
    set_entry(&entries[entry_num++], "help", 'h', 0, ARG_TYPE_BOOLEAN,
              new_data(ARG_TYPE_BOOLEAN), "print this help message", NULL, NULL);
    set_entry(&entries[entry_num++], "version", 'V', 0, ARG_TYPE_BOOLEAN,
              new_data(ARG_TYPE_BOOLEAN), g_strconcat("version ", argp_program_version, NULL), NULL, NULL);
#if 0
    set_entry(&entries[entry_num++], "usage", USAGE_KEY, 0, ARG_TYPE_BOOLEAN,
              new_data(ARG_TYPE_BOOLEAN), "print a usage message", NULL, NULL);
#endif
    // everyone needs this - really! - as well as (-h,--help)
    set_entry(&entries[entry_num++], "verbose", 'v', 0, ARG_TYPE_BOOLEAN,
              new_data(ARG_TYPE_BOOLEAN), "be verbose", NULL, NULL);
    set_entry(&entries[entry_num++], "print-man-page", anon_key++, OPTION_HIDDEN, ARG_TYPE_BOOLEAN,
              &print_man_pagep, NULL, NULL, NULL);
    
    if (prog) argv[0] = prog;
    
    {
        gchar **array;
        
        array = g_strsplit(usage, "\n", 2);
        argp.args_doc = array[0];
        argp.doc = array[1];
    }
    
    // terminate our list:
    entries[entry_num].name = NULL;
    entries[entry_num].key = 0;

    g_strchomp(usage);

    {
        error_t err = argp_parse(&argp, argc, argv, ARGP_NO_HELP, 0, NULL);
        if (err) abend(g_strdup_printf("argp_parse returned error %d", err));
    }
    
    if (print_man_pagep) print_man_page();

    {
        Argp_option *entry;

        for (entry = entries; entry->name; entry++) range_check_entry(entry);
    
        // redirect stdout to fd
        dup2(fd, 1);
    
        for (entry = entries; entry->name; entry++) print_entry(entry);
    }
        
    {
        gint i;
        
        g_print("set -- ");
        for (i = 1; i < new_argc; i++) g_print("\"%s\" ", new_argv[i]);
        g_print(";\n");
    }
    
    exit(0);
}

/*
Local Variables: 
fill-column: 70 
eval: (setq tab-width 8)
End: 
01234567890123456789012345678901234567890123456789012345678901234567890123456789
*/
