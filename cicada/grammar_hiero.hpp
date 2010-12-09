// -*- mode: c++ -*-
//
//  Copyright(C) 2010 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__GRAMMAR_HIERO__HPP__
#define __CICADA__GRAMMAR_HIERO__HPP__ 1

// very siple mutable grammar class..

#include <string>
#include <vector>

#include <cicada/grammar_mutable.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/lattice.hpp>

#include <google/dense_hash_set>

namespace cicada
{
  
  class GrammarGlue : public GrammarMutable
  {
  public:
    GrammarGlue(const symbol_type& goal, const symbol_type& non_terminal, const bool __straight, const bool __inverted)
      : straight(__straight), inverted(__inverted)
    {
      rule_ptr_type rule_unary(rule_type::create(rule_type(goal, rule_type::symbol_set_type(1, non_terminal.non_terminal(1)))));
      
      insert(rule_unary, rule_unary);

      if (straight) {
	std::vector<symbol_type, std::allocator<symbol_type> > phrase(2);
	phrase.front() = goal.non_terminal(1);
	phrase.back()  = non_terminal.non_terminal(2);
	
	rule_ptr_type rule(rule_type::create(rule_type(goal, phrase.begin(), phrase.end())));
	
	feature_set_type features;
	features["glue-straight-penalty"] = -1;
	
	insert(rule, rule, features);
      }
      
      if (inverted) {
	std::vector<symbol_type, std::allocator<symbol_type> > phrase1(2);
	std::vector<symbol_type, std::allocator<symbol_type> > phrase2(2);
	
	phrase1.front() = goal.non_terminal(1);
	phrase1.back()  = non_terminal.non_terminal(2);
	
	phrase2.front() = non_terminal.non_terminal(2);
	phrase2.back()  = goal.non_terminal(1);
	
	rule_ptr_type rule1(rule_type::create(rule_type(goal, phrase1.begin(), phrase1.end())));
	rule_ptr_type rule2(rule_type::create(rule_type(goal, phrase2.begin(), phrase2.end())));
	
	feature_set_type features;
	features["glue-inverted-penalty"] = -1;
	
	insert(rule1, rule2, features);
      }
    }
    
    bool valid_span(int first, int last, int distance) const
    {
      return (straight && inverted ? true : first == 0);
    }
    
  private:
    bool straight;
    bool inverted;
  };

  class GrammarInsertion : public GrammarMutable
  {
  public:
    typedef Lattice    lattice_type;
    typedef HyperGraph hypergraph_type;

  public:
    GrammarInsertion(const hypergraph_type& graph, const symbol_type& non_terminal)
    {
      typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > symbol_set_type;
      
      symbol_set_type symbols;
      symbols.set_empty_key(symbol_type());

      feature_set_type features;
      features["insertion-penalty"] = - 1.0;
      
      attribute_set_type attributes;
      attributes["insertion"] = attribute_set_type::int_type(1);
      
      hypergraph_type::edge_set_type::const_iterator eiter_end = graph.edges.end();
      for (hypergraph_type::edge_set_type::const_iterator eiter = graph.edges.begin(); eiter != eiter_end; ++ eiter) 
	if (eiter->rule) {
	  const rule_type& rule = *(eiter->rule);
	  
	  rule_type::symbol_set_type::const_iterator siter_end = rule.rhs.end();
	  for (rule_type::symbol_set_type::const_iterator siter = rule.rhs.begin(); siter != siter_end; ++ siter) 
	    if (*siter != vocab_type::EPSILON && siter->is_terminal() && symbols.find(*siter) == symbols.end()) {
	      rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, *siter))));
	      
	      insert(rule, rule, features, attributes);
	      
	      symbols.insert(*siter);
	    }
	}
    }

    GrammarInsertion(const lattice_type& lattice, const symbol_type& non_terminal)
    {
      typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > symbol_set_type;
      
      symbol_set_type symbols;
      symbols.set_empty_key(symbol_type());

      feature_set_type features;
      features["insertion-penalty"] = - 1.0;

      attribute_set_type attributes;
      attributes["insertion"] = attribute_set_type::int_type(1);
      
      for (size_t first = 0; first != lattice.size(); ++ first) {
	const lattice_type::arc_set_type& arcs = lattice[first];
	
	lattice_type::arc_set_type::const_iterator aiter_end = arcs.end();
	for (lattice_type::arc_set_type::const_iterator aiter = arcs.begin(); aiter != aiter_end; ++ aiter)
	  if (aiter->label != vocab_type::EPSILON && symbols.find(aiter->label) == symbols.end()) {
	    rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, aiter->label))));
	    
	    insert(rule, rule, features, attributes);
	    
	    symbols.insert(aiter->label);
	  }
      }
    }
    
    bool valid_span(int first, int last, int distance) const
    {
      // we need this last - first == 1 when intersecting with lattice...
      return distance <= 1 || last - first == 1;
    }
  };
  
  
  class GrammarDeletion : public GrammarMutable
  {
  public:
    typedef Lattice    lattice_type;
    typedef HyperGraph hypergraph_type;
    
  public:
    GrammarDeletion(const hypergraph_type& graph, const symbol_type& non_terminal)
    {
      typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > symbol_set_type;
      
      symbol_set_type symbols;
      symbols.set_empty_key(symbol_type());
      
      feature_set_type features;
      features["deletion-penalty"] = - 1.0;

      attribute_set_type attributes;
      attributes["deletion"] = attribute_set_type::int_type(1);
      
      rule_ptr_type rule_epsilon(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, vocab_type::EPSILON))));
      
      hypergraph_type::edge_set_type::const_iterator eiter_end = graph.edges.end();
      for (hypergraph_type::edge_set_type::const_iterator eiter = graph.edges.begin(); eiter != eiter_end; ++ eiter) 
	if (eiter->rule) {
	  const rule_type& rule = *(eiter->rule);
	  
	  rule_type::symbol_set_type::const_iterator siter_end = rule.rhs.end();
	  for (rule_type::symbol_set_type::const_iterator siter = rule.rhs.begin(); siter != siter_end; ++ siter) 
	    if (*siter != vocab_type::EPSILON && siter->is_terminal() && symbols.find(*siter) == symbols.end()) {
	      rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, *siter))));
	      
	      insert(rule, rule_epsilon, features, attributes);
	      
	      symbols.insert(*siter);
	    }
	}
    }

    GrammarDeletion(const lattice_type& lattice, const symbol_type& non_terminal)
    {
      typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > symbol_set_type;
      
      symbol_set_type symbols;
      symbols.set_empty_key(symbol_type());
      
      feature_set_type features;
      features["deletion-penalty"] = - 1.0;
      
      attribute_set_type attributes;
      attributes["insertion"] = attribute_set_type::int_type(1);

      rule_ptr_type rule_epsilon(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, vocab_type::EPSILON))));
      
      for (size_t first = 0; first != lattice.size(); ++ first) {
	const lattice_type::arc_set_type& arcs = lattice[first];

	lattice_type::arc_set_type::const_iterator aiter_end = arcs.end();
	for (lattice_type::arc_set_type::const_iterator aiter = arcs.begin(); aiter != aiter_end; ++ aiter)
	  if (aiter->label != vocab_type::EPSILON && symbols.find(aiter->label) == symbols.end()) {
	    rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, aiter->label))));
	    
	    insert(rule, rule_epsilon, features, attributes);
	    
	    symbols.insert(aiter->label);
	  }
      }
    }
    
    bool valid_span(int first, int last, int distance) const
    {
      // we need this last - first == 1 when intersecting with lattice...
      return distance <= 1 || last - first == 1;
    }
  };
};

#endif
