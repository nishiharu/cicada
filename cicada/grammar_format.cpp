//
//  Copyright(C) 2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <cicada/sentence.hpp>

#include "grammar_format.hpp"

namespace cicada
{
  GrammarFormat::id_type GrammarFormat::next(const id_type& node, const symbol_type& symbol) const
  {
    // check!
    id_type node_next = base_type::next(node, symbol);
    if (node_next == root()) {
      visited_type& __visited = const_cast<visited_type&>(visited);
      
      if (__visited.insert(id_symbol_type(node, symbol)).second) {
	// the combination of node/symbol has not been visited!
	
	const std::string context = (node == root()
				     ? static_cast<const std::string&>(symbol)
				     : prefix[node] + ' ' + static_cast<const std::string&>(symbol));
	
	format_type::phrase_set_type phrases;
	format->operator()(context, phrases);
	
	if (! phrases.empty()) {
	  typedef cicada::Sentence phrase_type;
	  
	  feature_set_type features;
	  features[feature] = -1;
	  
	  // construct source-side lhs from context
	  const phrase_type phrase_source = phrase_type(utils::piece(context));
	  const rule_ptr_type rule_source = rule_type::create(rule_type(non_terminal, phrase_source.begin(), phrase_source.end()));
	  
	  format_type::phrase_set_type::const_iterator piter_end = phrases.end();
	  for (format_type::phrase_set_type::const_iterator piter = phrases.begin(); piter != piter_end; ++ piter) {
	    // construct target-side lhs from *piter;
	    const phrase_type phrase_target = phrase_type(utils::piece(*piter));
	    const rule_ptr_type rule_target = rule_type::create(rule_type(non_terminal, phrase_target.begin(), phrase_target.end()));
	    
	    const_cast<base_type&>(static_cast<const base_type&>(*this)).insert(rule_source, rule_target, features);
	  }
	  
	  node_next = base_type::next(node, symbol);
	}
      }
    }
    
    return node_next;
  }

};
