#!/bin/bash

# Copyright 2008-2011 Bob Hepple
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

# http://sourceforge.net/projects/process-getopt
# http://process-getopt.sourceforge.net
# http://bhepple.freeshell.org/oddmuse/wiki.cgi/process-getopt

# $Id: runtests.sh,v 1.5 2011/04/21 01:22:11 bhepple Exp $

# error on first failed command or unreferencing a undefined variable:
set -eu

check_and_process_opts() {
    local NO_OPTS TEMP PG

    # avoid the overhead of process-getopt if there are no options:
    NO_OPTS="yes"
    for i in "$@"; do
        case "$i" in
            -*)
                NO_OPTS=""
                break
                ;;
            *)
                [ "${STOP_ON_FIRST_NON_OPT:-""}" ] && break
                ;;
        esac
    done
    [ "$NO_OPTS" ] && return 1

    source ../process-getopt

    STOP_func() { [ "${1:-""}" ] && STOP=; }
    add_opt STOP "don't stop on first error" s "" stop

    # define the standard options --help etc:
    add_std_opts 

    # delete any standard options you don't want:
    del_opt QUIET
    del_opt END_OPTIONS

    # process the command line, callback get called here:
    TEMP=$(call_getopt "$@") || exit 1
    eval set -- "$TEMP"
    process_opts "$@" || shift "$?"

    # return the new command line (stripped of options):
    NEW_ARGS=( "$@" )
    return 0
}

##########################
#         M A I N        #
##########################
BASH_CANDIDATES="bash-2.04 bash-2.05 bash-4.0"
PROG=$(basename $0)
DIR=$(dirname $0)
VERSION='$Revision: 1.5 $' # CUSTOMISE
VERBOSE=""
ARGUMENTS="[tests ...]" # CUSTOMISE
SHORT_DESC="Run regression tests on process-getopt. " # CUSTOMISE
USAGE="If no tests are given on the command line, run all test*. \
Tests against $BASH_CANDIDATES if they are available." # CUSTOMISE
STOP=yes
NUM_PASSED=0
NUM_FAILED=0

NEW_ARGS=( )
check_and_process_opts "$@" && {
    if [ ${#NEW_ARGS[@]} -gt 0 ]; then
        set -- "${NEW_ARGS[@]}"
    else
        set --
    fi
}

# At this point, all the options have been processed and removed from
# the arg list. We can now process $@ as arguments to the program.

rm -f *~
if [ "$@" ]; then
    TESTS="$@"
else
    TESTS=`ls test*`
fi

mkdir -p data

BASH_VARIANTS="bash "
for VARIANT in $BASH_CANDIDATES; do
    if type $VARIANT &>/dev/null; then
        BASH_VARIANTS="$BASH_VARIANTS $VARIANT"
    fi
done

export PROG=test_body
export VERSION=1
echo "BASH_VERSION=$BASH_VERSION"
export VARIANT=""
for TEST_NAME in $TESTS; do
    unset TEST_TITLE TEST_ARGS TEST_IGNORE_STDOUT TEST_IGNORE_STDERR 
    unset TEST_IGNORE_VALUE TEST_EXP_VAL
    TEST_EXP_STDOUT=data/exp_stdout.$TEST_NAME
    TEST_EXP_STDERR=data/exp_stderr.$TEST_NAME
    source $TEST_NAME
    test_setup
    for VARIANT in $BASH_VARIANTS; do
        TEST_ACT_STDOUT=data/act_stdout.$VARIANT.$TEST_NAME
        TEST_ACT_STDERR=data/act_stderr.$VARIANT.$TEST_NAME
        printf "%-17s: %s" "$TEST_NAME/$VARIANT" "$TEST_TITLE: "
        FAILED=""
        TEST_ACT_VAL=0
        $VARIANT -c "source $TEST_NAME; test_body $TEST_ARGS" > $TEST_ACT_STDOUT 2> $TEST_ACT_STDERR || TEST_ACT_VAL=$?
        if [ -z "$TEST_IGNORE_STDOUT" ]; then
            if ! cmp $TEST_EXP_STDOUT $TEST_ACT_STDOUT > /dev/null; then
				echo
				echo "*************************************************************"
                echo "Error: in stdout: diff $TEST_EXP_STDOUT $TEST_ACT_STDOUT:"
				diff $TEST_EXP_STDOUT $TEST_ACT_STDOUT || :
                FAILED="yes"
            fi
        fi
        
        if [ -z "$TEST_IGNORE_STDERR" ]; then
            if ! cmp $TEST_EXP_STDERR $TEST_ACT_STDERR > /dev/null; then
				echo
				echo "*************************************************************"
                echo "Error: in stderr: diff  $TEST_EXP_STDERR $TEST_ACT_STDERR"
				diff $TEST_EXP_STDERR $TEST_ACT_STDERR || :
                FAILED="yes"
            fi
        fi

        if [ -z "$TEST_IGNORE_VALUE" ]; then
            if [ $TEST_EXP_VAL -ne $TEST_ACT_VAL ]; then
				echo
				echo "*************************************************************"
                echo "Error: returned $TEST_ACT_VAL instead of $TEST_EXP_VAL"
                FAILED="yes"
            fi
        fi
        if [ "$FAILED" ]; then
			echo "*************************************************************"
            echo "$TEST_NAME failed"
            NUM_FAILED=$(($NUM_FAILED + 1))
            [ "$STOP" ] && exit 1
        else
            echo "passed"
            NUM_PASSED=$(($NUM_PASSED + 1))
        fi
    done
done
echo "Passed=$NUM_PASSED, failed=$NUM_FAILED"
exit $NUM_FAILED
