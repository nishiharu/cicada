(sort of) linguistic component in cicada

We have three linsuistic components, extensively used in mteval/feature computation etc.

* stemmer: stemming algorithm (used in evaluation and feature computation)

snowball: snowball stemming
	language=[language] stemming algorithm (en, de etc.)
arabic: Arabic stemming
prefix: taking prefix of letters
	size=[int] prefix size
suffix: taking suffix of letters
	size=[int] suffix size
digit: digits normalized to @
latin: romanization
lower: lower casing
nfkc:  NFKC

* tokenizer: text tokenizer (used by evaluation component)

lower: lower casing
nonascii: split non ascii characters
nist: NIST mteval style tokenization
penn: penn-treebank stye tokenization
stemmer: tokenize by stemming algorithm
	algorithm=[stemmer spec]
tokenize: use the chain of tokenization
	lower=[true|false] perform lower casing
	nonascii=[true|false] perform non ascii character splitting
	nist=[true|false] perform NIST tokenization
	penn=[true|false] perform Penn-treebank tokenization
	stemmer=[stemmer spec] perform by stemmring tokenization

* matcher: word matcher (used in feature computation, part of evaluation (TER/WER etc) and system combination)

lower: matching by lower-case
stemmer: matching by stemming algorithm
	algorithm=[stemmer spec]
wordnet: matching by wordnet synsets
	path=[path to wordnet database]

* signature: word signature (used in parse-coarse or simply monolingual components)

english: English signature
chinese: Chinese signature

* formatter: number/date formatter (primarity used as an additional grammar)

number: number formatter
date: date formatter

For number formatter, you can add an extra rule file for each side (parser and generator).
For the defails of rules, consult site.icu-project.org and see "Rule-based Number Format"
You can dump predefined rules by

    cicada_format_rbnf --locale <locale, such as "ja"> --resource
