#!/bin/sh

. ./grade-functions.sh

quicktest 'file_flush/file_truncate/file rewrite [fs]' \
	'file_flush is good' \
	'file_truncate is good' \
	'file rewrite is good' \
