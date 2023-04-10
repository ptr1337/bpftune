#!/usr/bin/bash
#
# SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
#
# Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.

# run sysctl test

. ./test_lib.sh


SLEEPTIME=10


test_start "$0|sample legacy test: does sample tuner appear, trigger event and disappear?"

test_setup "true"

rm -f /usr/local/lib64/bpftune/sample_tuner.so
sleep 1
test_run_cmd_local "$BPFTUNE -dsL &" true
sleep $SETUPTIME
cd ../sample_tuner ; make install
sleep $SLEEPTIME
# trigger event
sysctl kernel.core_pattern
sleep $SLEEPTIME
grep -E "event .* for tuner sample" $TESTLOG_LAST
# remove tuner
rm /usr/local/lib64/bpftune/sample_tuner.so
sleep $SLEEPTIME
grep "fini tuner sample" $TESTLOG_LAST
test_pass
test_cleanup
test_exit
