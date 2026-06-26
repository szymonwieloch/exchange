#!/bin/bash
# Post-process gperf-generated header to fix two issues:
# 1. Empty entries "{""}" don't initialize the 'id' field → change to {"",0}
# 2. in_word_set() is not inline → add inline (avoids multiple-definition errors)

sed -i 's/{""}/{"",0}/g' "$1"
sed -i 's/^const struct TickerEntry \*$/inline const struct TickerEntry */' "$1"
