#!/bin/sh
# Copyright (C) 2016 and later: Unicode, Inc. and others.
# License & terms of use: http://www.unicode.org/copyright.html
#
# Copyright (c) 2007, International Business Machines Corporation and
# others. All Rights Reserved.

# Echo shell script commands.
set -ex

PERF=test/perf/unisetperf/unisetperf
# slow Bv Bv0 B0
# --pattern [:White_Space:]

for file in udhr_eng.txt \
            udhr_deu.txt \
            udhr_fra.txt \
            udhr_rus.txt \
            udhr_tha.txt \
            udhr_jpn.txt \
            udhr_cmn.txt \
            udhr_jpn.html; do
  for type in slow Bv Bv0; do
    $PERF SpanUTF16 --type $type -f ~/udhr/$file -v -e UTF-8 --passes 3 --iterations 10000
  done
done
