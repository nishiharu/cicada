//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include "hypergraph.hpp"
#include "grammar_static.hpp"

#include <boost/tokenizer.hpp>

#include <utils/space_separator.hpp>

typedef cicada::HyperGraph hypergraph_type;


int main(int argc, char** argv)
{
  typedef boost::tokenizer<utils::space_separator> tokenizer_type;

  if (argc == 1) {
    std::cout << argv[0] << " [indexed-grammar]" << std::endl;
    return 1;
  }
  
  cicada::GrammarStatic grammar(argv[1]);

  std::string line;
  while (std::getline(std::cin, line)) {
    tokenizer_type tokenizer(line);
    
    cicada::GrammarStatic::id_type node = grammar.root();
    
    for (tokenizer_type::iterator iter = tokenizer.begin(); iter != tokenizer.end(); ++ iter)
      node = grammar.next(node, *iter);
    
    if (! grammar.rules(node).empty()) {
      const cicada::GrammarStatic::rule_pair_set_type& rules = grammar.rules(node);
      
      for (cicada::GrammarStatic::rule_pair_set_type::const_iterator riter = rules.begin(); riter != rules.end(); ++ riter) {
	std::cout << "source: " << *(riter->source) << " target: " << *(riter->target);
	
	for (hypergraph_type::feature_set_type::const_iterator fiter = riter->features.begin(); fiter != riter->features.end(); ++ fiter)
	  std::cout << ' ' << fiter->first << '=' << fiter->second;
	
	std::cout << " attributes: " << riter->attributes;
	
	std::cout << std::endl;
      }
    }
  }
}

