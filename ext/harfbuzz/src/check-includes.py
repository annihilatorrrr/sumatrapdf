#!/usr/bin/env python3

import sys, os, re

os.chdir (os.getenv ('srcdir', os.path.dirname (__file__)))

HBHEADERS = [os.path.basename (x) for x in os.getenv ('HBHEADERS', '').split ()] or \
	[x for x in os.listdir ('.') if x.startswith ('hb') and x.endswith ('.h')]
HBSOURCES = [os.path.basename (x) for x in os.getenv ('HBSOURCES', '').split ()] or \
	[x for x in os.listdir ('.') if x.startswith ('hb') and x.endswith (('.cc', '.hh'))]

stat = 0

print ('Checking that public header files #include "hb-common.h" or "hb.h" first (or none)')
for x in HBHEADERS:
	if x in ['hb.h', 'hb-common.h']: continue
	with open (x, 'r', encoding='utf-8') as f: content = f.read ()
	first = re.findall (r'#.*include.*', content)[0]
	if first not in ['#include "hb.h"', '#include "hb-common.h"']:
		print(f'failure on {x}')
		stat = 1

print ('Checking that source files #include a private header first (or none)')
for x in HBSOURCES:
	with open (x, 'r', encoding='utf-8') as f: content = f.read ()
	if includes := re.findall(r'#.*include.*', content):
		if not len (re.findall (r'"hb.*\.hh"', includes[0])):
			print(f'failure on {x}')
			stat = 1

print ('Checking that there is no #include <hb-*.h>')
for x in HBHEADERS + HBSOURCES:
	with open (x, 'r', encoding='utf-8') as f: content = f.read ()
	if re.findall ('#.*include.*<.*hb', content):
		print(f'failure on {x}')
		stat = 1

sys.exit (stat)
