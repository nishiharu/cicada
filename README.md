cicada
======

A statistical machine translation toolkit based on a semiring parsing framework[1].
Based on the generic framework, we can

   - learn model(s): tree-to-string, string-to-tree, string-to-string (with or without latent tree),
     word alignment, grammar for parsing
   - translate sentence, lattice and/or parsed tree
   - align between {hypergraph,lattice}-to-lattice (currently, we assume penn-treebank style hypergraph
     and sentence-like lattice)
   - align two sentences using symmetized IBM Model1/HMM/IBM Model4.
   - parse lattices (or sentences)
   - dependency parse sentences (or lattices)
   - analyze forest/tree/lattice

Remark: cicada is 蝉(CJK UNIFIED IDEOGRAPH-8749), (or セミ) in Japanese, pronounced "SEMI"

Basically, we have four distinct structures:

   - lattice: a representation of graph implemented as a two-dimentional array.
   - grammar: a collection of WFST implemented as a trie structure.
   - tree-grammar: a collectin of WFSTT (tree-transducer) implemented as a (nested) trie structure.
   - hypergraph: a compact representation of set of trees (or forest).

Translation/parsing can be carried out by:

   - A lattice (or sentence) is composed with a grammar, generating a hypergraph [2,24].
   - A lattice (or sentence) is composed with a tree-grammar, generating a hypergraph [27].
   - A lattice (or sentence) is composed with a phrasal grammar, generating a phrasal hypergraph [4].
   - A hypergraph/forest (or parse-tree) is composed with a phrasal grammar, generating another hypergraph [3].
   - A hypergraph/forest (or parse-tree) is composed with a tree grammar, generating another hypergraph [4].

Alignment can be carried out by:

   - A lattice is composed with dictionary, generating alignment hypergraph, or
   - A hypergraph is composed with dictinary, generating alignment hypergraph [20].
     
   - In order to support word alignment training, we can learn Model1/HMM/Model4 lexicon model
     by symmetized learning [22] or symmetric posterior constrained learning [23] with smoothing
     via variational Bayes or via L0 prior.
     Final combined alignment can be generated either by heuristic (AKA grow-diag-final-and etc.) or by ITG or
     max-matching from posterior probabilities.
     Also, lexicon model can be discriminatively trained [28].

Dependency parsing can be carried out by:

   - A lattice is dependency parsed by compose-dependency-{arc-standard, arc-eager, hybrid, degree2}, generating
     derivation hypergraph
   - Forests are rescored by dependency features (TODO)
   
     We support dependency projection [32] with Model1/HMM posterior probabilies so that we can train arbitrary
     dependency parses after projections.

After the hypergraph generation, you can:

   - Additional features are evaluated to generate another hypergraph [4].
      * cicada implementes cube-pruning [4], cube-growing [4], incremental [18]
        and exact (and stateless-inside-algorithm) methods
      * cube-growing employs coarse-heuristics [11], such as lower-order ngrams etc.
      * cube-pruning implements algorithm 2 of faster cube pruning [31].
   - Perform variational decoding for hypergraph [10].
   - Perform MBR decoding for hypergraph [12].
      * Above two computations rely on expected ngram-counts over forest [13].
   - K-best sentences are generated from hypergraph [5].
   - Generate oracle translations (BLEU only).

Or, you can combine outputs from multiple systems by [29]:

   - Perform parsing over nbests (use your favorite parser, such as Berkeley parser/Stanford parser etc.)
   - Generate context-free confusion forest by combining trees (not confusion network!)
      * It is performed by collecting rules from parse trees, and generate by Earley algorithm
   - Generate k-best translations after feature application etc.

   Or, a conventional strategy of [14]:
   - Create lattice from n-best list by incremental merging
   - Construct hypergraph by linear grammar (grammar-glue-straight + grammar-insertion)
   - Generate k-best translations after feature application etc.

Monolingual grammar learning is implemented:

   - A simple PCFG by simply extracting rules.
   - Learn latent annotated PCFG by split/merge process with an EM algorihtm [25].
   - Also, learn coarse grammars from the latent annotated PCFG for coarse-to-fine parsing [26].

Phrase/synchronou-rule/tree-to-string/string-to-tree extraction/scoring are implemented:

   - A conventional phrase extract algorithm in Moses
   - A conventional hierarchical phrase extraction algorithm in Hiero
      + syntax augmented rule extraction is also supported [15]
   - Tree-to-string/strint-to-tree extractin from forest [16,27]
   - Tree-to-tree rule extraction from forest [17] (experimental)
   - max-scope constraints to limit the grammar size [34]
   
   - After count extraction, you can perform map/reduce to compute model scores [19]

Various learning components are implemented:

   - Large feature set from input lattice/hypergraph on large training data via MaxEnt (optimized by LBFGS) [3]
   - Large/small featuer set from kbests on large/small traning data via MaxEnt (LBFGS)/liblinear [30]
   - Large feature set on small devset with MIRA [6,7], but with hypergraph
   - Small feature set on small devset learned by hypergraph-MERT [8]
   - Small/large feature set on small devset learned by hypergraph-MaxEnt (optimized by LBFGS or SGD)
     + softmax-margin [9]
   - Small/large feature set learned by iteratively construncting training samples with rank-learning.
     + optimization by LBFGS/liblinear etc. (similar to [33], but differ in kbest handling)
     + larger batching with optimized updates [37]
     + we have a script-based implementation + single-binary implementation for efficiency
   - xBLEU objective learned either by L-BFGS or SGD, which directly maximize expected-BLEU (not BLEU expectaiton) [35]
     + Now, this is a recommended optimization method (either kbest or hypergraph learning)
   - We support feature selection by kbest-feature merging [36]

Word clustering tool is also included to support word alignment learning + translation [20]

References
----------

[1]
@InProceedings{li-eisner:2009:EMNLP,
  author    = {Li, Zhifei  and  Eisner, Jason},
  title     = {First- and Second-Order Expectation Semirings with Applications to Minimum-Risk Training on Translation Forests},
  booktitle = {Proceedings of the 2009 Conference on Empirical Methods in Natural Language Processing},
  month     = {August},
  year      = {2009},
  address   = {Singapore},
  publisher = {Association for Computational Linguistics},
  pages     = {40--51},
  url       = {http://www.aclweb.org/anthology/D/D09/D09-1005}
}

[2]
@InProceedings{dyer-muresan-resnik:2008:ACLMain,
  author    = {Dyer, Christopher  and  Muresan, Smaranda  and  Resnik, Philip},
  title     = {Generalizing Word Lattice Translation},
  booktitle = {Proceedings of ACL-08: HLT},
  month     = {June},
  year      = {2008},
  address   = {Columbus, Ohio},
  publisher = {Association for Computational Linguistics},
  pages     = {1012--1020},
  url       = {http://www.aclweb.org/anthology/P/P08/P08-1115}
}

[3]
@InProceedings{dyer-resnik:2010:NAACLHLT,
  author    = {Dyer, Chris  and  Resnik, Philip},
  title     = {Context-free reordering, finite-state translation},
  booktitle = {Human Language Technologies: The 2010 Annual Conference of the North American Chapter of the Association for Computational Linguistics},
  month     = {June},
  year      = {2010},
  address   = {Los Angeles, California},
  publisher = {Association for Computational Linguistics},
  pages     = {858--866},
  url       = {http://www.aclweb.org/anthology/N10-1128}
}

[4]
@InProceedings{huang-chiang:2007:ACLMain,
  author    = {Huang, Liang  and  Chiang, David},
  title     = {Forest Rescoring: Faster Decoding with Integrated Language Models},
  booktitle = {Proceedings of the 45th Annual Meeting of the Association of Computational Linguistics},
  month     = {June},
  year      = {2007},
  address   = {Prague, Czech Republic},
  publisher = {Association for Computational Linguistics},
  pages     = {144--151},
  url       = {http://www.aclweb.org/anthology/P07-1019}
}

[5]
@InProceedings{huang-chiang:2005:IWPT,
  author    = {Huang, Liang  and  Chiang, David},
  title     = {Better k-best Parsing},
  booktitle = {Proceedings of the Ninth International Workshop on Parsing Technology},
  month     = {October},
  year      = {2005},
  address   = {Vancouver, British Columbia},
  publisher = {Association for Computational Linguistics},
  pages     = {53--64},
  url       = {http://www.aclweb.org/anthology/W/W05/W05-1506}
}

[6]
@InProceedings{chiang-knight-wang:2009:NAACLHLT09,
  author    = {Chiang, David  and  Knight, Kevin  and  Wang, Wei},
  title     = {11,001 New Features for Statistical Machine Translation},
  booktitle = {Proceedings of Human Language Technologies: The 2009 Annual Conference of the North American Chapter of the Association for Computational Linguistics},
  month     = {June},
  year      = {2009},
  address   = {Boulder, Colorado},
  publisher = {Association for Computational Linguistics},
  pages     = {218--226},
  url       = {http://www.aclweb.org/anthology/N/N09/N09-1025}
}

[7]
@InProceedings{watanabe-EtAl:2007:EMNLP-CoNLL2007,
  author    = {Watanabe, Taro  and  Suzuki, Jun  and  Tsukada, Hajime  and  Isozaki, Hideki},
  title     = {Online Large-Margin Training for Statistical Machine Translation},
  booktitle = {Proceedings of the 2007 Joint Conference on Empirical Methods in Natural Language Processing and Computational Natural Language Learning (EMNLP-CoNLL)},
  month     = {June},
  year      = {2007},
  address   = {Prague, Czech Republic},
  publisher = {Association for Computational Linguistics},
  pages     = {764--773},
  url       = {http://www.aclweb.org/anthology/D/D07/D07-1080}
}

[8]
@InProceedings{kumar-EtAl:2009:ACLIJCNLP,
  author    = {Kumar, Shankar  and  Macherey, Wolfgang  and  Dyer, Chris  and  Och, Franz},
  title     = {Efficient Minimum Error Rate Training and Minimum Bayes-Risk Decoding for Translation Hypergraphs and Lattices},
  booktitle = {Proceedings of the Joint Conference of the 47th Annual Meeting of the ACL and the 4th International Joint Conference on Natural Language Processing of the AFNLP},
  month     = {August},
  year      = {2009},
  address   = {Suntec, Singapore},
  publisher = {Association for Computational Linguistics},
  pages     = {163--171},
  url       = {http://www.aclweb.org/anthology/P/P09/P09-1019}
}

[9]
@InProceedings{gimpel-smith:2010:NAACLHLT,
  author    = {Gimpel, Kevin  and  Smith, Noah A.},
  title     = {Softmax-Margin CRFs: Training Log-Linear Models with Cost Functions},
  booktitle = {Human Language Technologies: The 2010 Annual Conference of the North American Chapter of the Association for Computational Linguistics},
  month     = {June},
  year      = {2010},
  address   = {Los Angeles, California},
  publisher = {Association for Computational Linguistics},
  pages     = {733--736},
  url       = {http://www.aclweb.org/anthology/N10-1112}
}

[10]
@InProceedings{li-eisner-khudanpur:2009:ACLIJCNLP,
  author    = {Li, Zhifei  and  Eisner, Jason  and  Khudanpur, Sanjeev},
  title     = {Variational Decoding for Statistical Machine Translation},
  booktitle = {Proceedings of the Joint Conference of the 47th Annual Meeting of the ACL and the 4th International Joint Conference on Natural Language Processing of the AFNLP},
  month     = {August},
  year      = {2009},
  address   = {Suntec, Singapore},
  publisher = {Association for Computational Linguistics},
  pages     = {593--601},
  url       = {http://www.aclweb.org/anthology/P/P09/P09-1067}
}

[11]
@InProceedings { vilar09:coarseHeuristic,
   author= {Vilar, David and Ney, Hermann},
   title= {On LM Heuristics for the Cube Growing Algorithm},
   booktitle= {Annual Conference of the European Association for Machine Translation},
   year= 2009,
   pages= {242-249},
   address= {Barcelona, Spain},
   month= may,
   booktitlelink= {http://www.talp.cat/eamt09/},
   pdf = {http://www-i6.informatik.rwth-aachen.de/publications/downloader.php?id=617&row=pdf}
}

[12]
@InProceedings{denero-chiang-knight:2009:ACLIJCNLP,
  author    = {DeNero, John  and  Chiang, David  and  Knight, Kevin},
  title     = {Fast Consensus Decoding over Translation Forests},
  booktitle = {Proceedings of the Joint Conference of the 47th Annual Meeting of the ACL and the 4th International Joint Conference on Natural Language Processing of the AFNLP},
  month     = {August},
  year      = {2009},
  address   = {Suntec, Singapore},
  publisher = {Association for Computational Linguistics},
  pages     = {567--575},
  url       = {http://www.aclweb.org/anthology/P/P09/P09-1064}
}

[13]
@InProceedings{denero-EtAl:2010:NAACLHLT,
  author    = {DeNero, John  and  Kumar, Shankar  and  Chelba, Ciprian  and  Och, Franz},
  title     = {Model Combination for Machine Translation},
  booktitle = {Human Language Technologies: The 2010 Annual Conference of the North American Chapter of the Association for Computational Linguistics},
  month     = {June},
  year      = {2010},
  address   = {Los Angeles, California},
  publisher = {Association for Computational Linguistics},
  pages     = {975--983},
  url       = {http://www.aclweb.org/anthology/N10-1141}
}

[14]
@InProceedings{rosti-EtAl:2009:WMT-09,
  author    = {Rosti, Antti-Veikko  and  Zhang, Bing  and  Matsoukas, Spyros  and  Schwartz, Richard},
  title     = {Incremental Hypothesis Alignment with Flexible Matching for Building Confusion Networks: {BBN} System Description for {WMT}09 System Combination Task},
  booktitle = {Proceedings of the Fourth Workshop on Statistical Machine Translation},
  month     = {March},
  year      = {2009},
  address   = {Athens, Greece},
  publisher = {Association for Computational Linguistics},
  pages     = {61--65},
  url       = {http://www.aclweb.org/anthology/W/W09/W09-0409}
}

[15]
@InProceedings{zollmann-vogel:2010:SSST,
  author    = {Zollmann, Andreas  and  Vogel, Stephan},
  title     = {New Parameterizations and Features for PSCFG-Based Machine Translation},
  booktitle = {Proceedings of the 4th Workshop on Syntax and Structure in Statistical Translation},
  month     = {August},
  year      = {2010},
  address   = {Beijing, China},
  publisher = {Coling 2010 Organizing Committee},
  pages     = {110--117},
  url       = {http://www.aclweb.org/anthology/W10-3814}
}

[16]
@InProceedings{mi-huang:2008:EMNLP,
  author    = {Mi, Haitao  and  Huang, Liang},
  title     = {Forest-based Translation Rule Extraction},
  booktitle = {Proceedings of the 2008 Conference on Empirical Methods in Natural Language Processing},
  month     = {October},
  year      = {2008},
  address   = {Honolulu, Hawaii},
  publisher = {Association for Computational Linguistics},
  pages     = {206--214},
  url       = {http://www.aclweb.org/anthology/D08-1022}
}

[17]
@InProceedings{liu-lu-liu:2009:ACLIJCNLP,
  author    = {Liu, Yang  and  L{\"{u}}, Yajuan  and  Liu, Qun},
  title     = {Improving Tree-to-Tree Translation with Packed Forests},
  booktitle = {Proceedings of the Joint Conference of the 47th Annual Meeting of the ACL and the 4th International Joint Conference on Natural Language Processing of the AFNLP},
  month     = {August},
  year      = {2009},
  address   = {Suntec, Singapore},
  publisher = {Association for Computational Linguistics},
  pages     = {558--566},
  url       = {http://www.aclweb.org/anthology/P/P09/P09-1063}
}

[18]
@InProceedings{huang-mi:2010:EMNLP,
  author    = {Huang, Liang  and  Mi, Haitao},
  title     = {Efficient Incremental Decoding for Tree-to-String Translation},
  booktitle = {Proceedings of the 2010 Conference on Empirical Methods in Natural Language Processing},
  month     = {October},
  year      = {2010},
  address   = {Cambridge, MA},
  publisher = {Association for Computational Linguistics},
  pages     = {273--283},
  url       = {http://www.aclweb.org/anthology/D10-1027}
}

[19]
@InProceedings{dyer-EtAl:2008:WMT,
  author    = {Dyer, Chris  and  Cordova, Aaron  and  Mont, Alex  and  Lin, Jimmy},
  title     = {Fast, Easy, and Cheap: Construction of Statistical Machine Translation Models with {MapReduce}},
  booktitle = {Proceedings of the Third Workshop on Statistical Machine Translation},
  month     = {June},
  year      = {2008},
  address   = {Columbus, Ohio},
  publisher = {Association for Computational Linguistics},
  pages     = {199--207},
  url       = {http://www.aclweb.org/anthology/W/W08/W08-0333}
}

[20]
@InProceedings{riesa-marcu:2010:ACL,
  author    = {Riesa, Jason  and  Marcu, Daniel},
  title     = {Hierarchical Search for Word Alignment},
  booktitle = {Proceedings of the 48th Annual Meeting of the Association for Computational Linguistics},
  month     = {July},
  year      = {2010},
  address   = {Uppsala, Sweden},
  publisher = {Association for Computational Linguistics},
  pages     = {157--166},
  url       = {http://www.aclweb.org/anthology/P10-1017}
}

[21]
@InProceedings{uszkoreit-brants:2008:ACLMain,
  author    = {Uszkoreit, Jakob  and  Brants, Thorsten},
  title     = {Distributed Word Clustering for Large Scale Class-Based Language Modeling in Machine Translation},
  booktitle = {Proceedings of ACL-08: HLT},
  month     = {June},
  year      = {2008},
  address   = {Columbus, Ohio},
  publisher = {Association for Computational Linguistics},
  pages     = {755--762},
  url       = {http://www.aclweb.org/anthology/P/P08/P08-1086}
}

[22]
@InProceedings{liang-taskar-klein:2006:HLT-NAACL06-Main,
  author    = {Liang, Percy  and  Taskar, Ben  and  Klein, Dan},
  title     = {Alignment by Agreement},
  booktitle = {Proceedings of the Human Language Technology Conference of the NAACL, Main Conference},
  month     = {June},
  year      = {2006},
  address   = {New York City, USA},
  publisher = {Association for Computational Linguistics},
  pages     = {104--111},
  url       = {http://www.aclweb.org/anthology/N/N06/N06-1014}
}

[23]
@InProceedings{ganchev-gracca-taskar:2008:ACLMain,
  author    = {Ganchev, Kuzman  and  Gra\c{c}a, Jo\~{a}o V.  and  Taskar, Ben},
  title     = {Better Alignments = Better Translations?},
  booktitle = {Proceedings of ACL-08: HLT},
  month     = {June},
  year      = {2008},
  address   = {Columbus, Ohio},
  publisher = {Association for Computational Linguistics},
  pages     = {986--993},
  url       = {http://www.aclweb.org/anthology/P/P08/P08-1112}
}

[24]
@INPROCEEDINGS{Klein01parsingand,
    author = {Dan Klein and Christopher D. Manning},
    title = {Parsing and Hypergraphs},
    booktitle = {IN IWPT},
    year = {2001},
    pages = {123--134},
    publisher = {}
}

[25]
@InProceedings{petrov-EtAl:2006:COLACL,
  author    = {Petrov, Slav  and  Barrett, Leon  and  Thibaux, Romain  and  Klein, Dan},
  title     = {Learning Accurate, Compact, and Interpretable Tree Annotation},
  booktitle = {Proceedings of the 21st International Conference on Computational Linguistics and 44th Annual Meeting of the Association for Computational Linguistics},
  month     = {July},
  year      = {2006},
  address   = {Sydney, Australia},
  publisher = {Association for Computational Linguistics},
  pages     = {433--440},
  url       = {http://www.aclweb.org/anthology/P06-1055},
  doi       = {10.3115/1220175.1220230}
}

[26]
@InProceedings{petrov-klein:2007:main,
  author    = {Petrov, Slav  and  Klein, Dan},
  title     = {Improved Inference for Unlexicalized Parsing},
  booktitle = {Human Language Technologies 2007: The Conference of the North American Chapter of the Association for Computational Linguistics; Proceedings of the Main Conference},
  month     = {April},
  year      = {2007},
  address   = {Rochester, New York},
  publisher = {Association for Computational Linguistics},
  pages     = {404--411},
  url       = {http://www.aclweb.org/anthology/N/N07/N07-1051}
}

[27]
@inproceedings{galley-EtAl:2004:HLTNAACL,
  author    = {Galley, Michel  and  Hopkins, Mark  and  Knight, Kevin  and  Marcu, Daniel},
  title     = {What's in a translation rule?},
  booktitle = {HLT-NAACL 2004: Main Proceedings },
  editor = {Susan Dumais, Daniel Marcu and Salim Roukos},
  year      = 2004,
  month     = {May 2 - May 7},
  address   = {Boston, Massachusetts, USA},
  publisher = {Association for Computational Linguistics},
  pages     = {273--280}
}

[28]
@InProceedings{mauser-hasan-ney:2009:EMNLP,
  author    = {Mauser, Arne  and  Hasan, Sa{\v{s}}a  and  Ney, Hermann},
  title     = {Extending Statistical Machine Translation with Discriminative and Trigger-Based Lexicon Models},
  booktitle = {Proceedings of the 2009 Conference on Empirical Methods in Natural Language Processing},
  month     = {August},
  year      = {2009},
  address   = {Singapore},
  publisher = {Association for Computational Linguistics},
  pages     = {210--218},
  url       = {http://www.aclweb.org/anthology/D/D09/D09-1022}
}

[29]
@InProceedings{watanabe-sumita:2011:ACL-HLT2011,
  author    = {Watanabe, Taro  and  Sumita, Eiichiro},
  title     = {Machine Translation System Combination by Confusion Forest},
  booktitle = {Proceedings of the 49th Annual Meeting of the Association for Computational Linguistics: Human Language Technologies},
  month     = {June},
  year      = {2011},
  address   = {Portland, Oregon, USA},
  publisher = {Association for Computational Linguistics},
  pages     = {1249--1257},
  url       = {http://www.aclweb.org/anthology/P11-1125}
}

[30]
@Article{REF08a,
  author =	 {Rong-En Fan and Kai-Wei Chang and Cho-Jui Hsieh and Xiang-Rui Wang and Chih-Jen Lin},
  title = 	  {{LIBLINEAR}: A Library for Large Linear Classification},
  journal = 	   {Journal of Machine Learning Research},
  year =   {2008},
  volume =  {9},
  pages =    {1871--1874}
}

[31]
@inproceedings{iwslt10:TP:gesmundo,
  author = {Andrea Gesmundo and James Henderson},
  editor = {Marcello Federico and Ian Lane and Michael Paul and Fran\c{c}ois Yvon},
  title = {{Faster Cube Pruning}},
  booktitle = {Proceedings of the seventh International Workshop on Spoken Language Translation (IWSLT)},
  year = {2010},
  pages = {267--274},
  location = {Paris, France}
}

[32]
@InProceedings{jiang-liu:2010:ACL,
  author    = {Jiang, Wenbin  and  Liu, Qun},
  title     = {Dependency Parsing and Projection Based on Word-Pair Classification},
  booktitle = {Proceedings of the 48th Annual Meeting of the Association for Computational Linguistics},
  month     = {July},
  year      = {2010},
  address   = {Uppsala, Sweden},
  publisher = {Association for Computational Linguistics},
  pages     = {12--20},
  url       = {http://www.aclweb.org/anthology/P10-1002}
}

[33]
@InProceedings{hopkins-may:2011:EMNLP,
  author    = {Hopkins, Mark  and  May, Jonathan},
  title     = {Tuning as Ranking},
  booktitle = {Proceedings of the 2011 Conference on Empirical Methods in Natural Language Processing},
  month     = {July},
  year      = {2011},
  address   = {Edinburgh, Scotland, UK.},
  publisher = {Association for Computational Linguistics},
  pages     = {1352--1362},
  url       = {http://www.aclweb.org/anthology/D11-1125}
}

[34]
@InProceedings{hopkins-langmead:2010:EMNLP,
  author    = {Hopkins, Mark  and  Langmead, Greg},
  title     = {{SCFG} Decoding Without Binarization},
  booktitle = {Proceedings of the 2010 Conference on Empirical Methods in Natural Language Processing},
  month     = {October},
  year      = {2010},
  address   = {Cambridge, MA},
  publisher = {Association for Computational Linguistics},
  pages     = {646--655},
  url       = {http://www.aclweb.org/anthology/D10-1063}
}

[35]
@InProceedings{rosti-EtAl:2011:WMT,
  author    = {Rosti, Antti-Veikko  and  Zhang, Bing  and  Matsoukas, Spyros  and  Schwartz, Richard},
  title     = {Expected BLEU Training for Graphs: BBN System Description for WMT11 System Combination Task},
  booktitle = {Proceedings of the Sixth Workshop on Statistical Machine Translation},
  month     = {July},
  year      = {2011},
  address   = {Edinburgh, Scotland},
  publisher = {Association for Computational Linguistics},
  pages     = {159--165},
  url       = {http://www.aclweb.org/anthology/W11-2119}
}

[36]
@InProceedings{simianer-riezler-dyer:2012:ACL2012,
  author    = {Simianer, Patrick  and  Riezler, Stefan  and  Dyer, Chris},
  title     = {Joint Feature Selection in Distributed Stochastic Learning for Large-Scale Discriminative Training in SMT},
  booktitle = {Proceedings of the 50th Annual Meeting of the Association for Computational Linguistics (Volume 1: Long Papers)},
  month     = {July},
  year      = {2012},
  address   = {Jeju Island, Korea},
  publisher = {Association for Computational Linguistics},
  pages     = {11--21},
  url       = {http://www.aclweb.org/anthology/P12-1002}
}

[37]
@InProceedings{watanabe:2012:NAACL-HLT,
  author    = {Watanabe, Taro},
  title     = {Optimized Online Rank Learning for Machine Translation},
  booktitle = {Proceedings of the 2012 Conference of the North American Chapter of the Association for Computational Linguistics: Human Language Technologies},
  month     = {June},
  year      = {2012},
  address   = {Montr\'{e}al, Canada},
  publisher = {Association for Computational Linguistics},
  pages     = {253--262},
  url       = {http://www.aclweb.org/anthology/N12-1026}
}
