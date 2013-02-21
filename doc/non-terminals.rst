Non-terminals are represented by [.+] with special flags:

   if a non-terminal contains '@' it is a symbol-refixed non-terminal, and "deciam number" is
   assigned immediately aftr @.
   The refinements are indicated by "bits." See Symbol::annotate() and Symbol::annotated().
   
   if a non-terminal contains '^', it is a binarized non-terminal so that we can erase
   it to reproduce original hyperedge. See Symbol::binarized().
      
   '~' are used to indicate left-binarized label sequence after '^'
   '_' are used to indicate right-binarized label sequence after '^'
   
   '^L' indicates dependency left-binarization
   '^R' indicates dependency right-binarization

   '+' singed indicate concatenation of labels, usually indicating binarized and merged labels.
   '/' indicates right substitution
   '\' indidates left substitution


There exists special curly bracketed "terminal" symbols, {.+}, indicating dependency-head.