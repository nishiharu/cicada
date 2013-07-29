#!/bin/sh

cicada=../../../..

exec $cicada/scripts/cicada-extract.py \
	--f ../../data/train.ja.bz2 \
	--e ../../data/train.en.bz2 \
	--a ../../alignment/model/aligned.posterior-itg \
	--fe ../data/train.tree.en.gz \
	\
	--root-dir . \
	--model-dir . \
	\
	--lexicon-variational \
	\
	--ghkm \
	--constrained \
	--max-scope 2 \
	\
	--threads 4 \
	--max-malloc 16

