// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__GRAMMAR_SIMPLE__HPP__
#define __CICADA__GRAMMAR_SIMPLE__HPP__ 1

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
    template <typename Iterator>
    GrammarGlue(const symbol_type& goal, const symbol_type& non_terminal, Iterator first, Iterator last, const bool __straight, const bool __inverted)
      : straight(__straight), inverted(__inverted)
    {
      typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > non_terminal_set_type;
      
      non_terminal_set_type non_terminals;
      non_terminals.set_empty_key(symbol_type());
      non_terminals.insert(first, last);
      non_terminals.insert(non_terminal);

      non_terminal_set_type::const_iterator niter_end = non_terminals.end();
      for (non_terminal_set_type::const_iterator niter = non_terminals.begin(); niter != niter_end; ++ niter)
	construct(goal, *niter);
    }
    
    GrammarGlue(const symbol_type& goal, const symbol_type& non_terminal, const bool __straight, const bool __inverted)
      : straight(__straight), inverted(__inverted)
    {
      construct(goal, non_terminal);
    }
    
    bool valid_span(int first, int last, int distance) const
    {
      return (straight && inverted ? true : first == 0);
    }
    
  private:
    
    void construct(const symbol_type& goal, const symbol_type& non_terminal)
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
    
  private:
    bool straight;
    bool inverted;
  };

  class GrammarPair : public GrammarMutable
  {
  public:
    typedef Lattice    lattice_type;
    typedef HyperGraph hypergraph_type;
    
  public:
    GrammarPair(const hypergraph_type& source, const lattice_type& target, const symbol_type& non_terminal)
      : GrammarMutable(1)
    {
      typedef std::pair<symbol_type, symbol_type> symbol_pair_type;
      typedef google::dense_hash_set<symbol_pair_type, boost::hash<symbol_pair_type>, std::equal_to<symbol_pair_type> > symbol_pair_set_type;
      
      symbol_pair_set_type symbols;
      symbols.set_empty_key(symbol_pair_type());
      
      feature_set_type features;
      
      attribute_set_type attributes;
      attributes["pair"] = attribute_set_type::int_type(1);
      
      hypergraph_type::edge_set_type::const_iterator eiter_end = source.edges.end();
      for (hypergraph_type::edge_set_type::const_iterator eiter = source.edges.begin(); eiter != eiter_end; ++ eiter) 
	if (eiter->rule) {
	  const rule_type& rule = *(eiter->rule);
	  
	  rule_type::symbol_set_type::const_iterator siter_end = rule.rhs.end();
	  for (rule_type::symbol_set_type::const_iterator siter = rule.rhs.begin(); siter != siter_end; ++ siter) 
	    if (*siter != vocab_type::EPSILON && siter->is_terminal()) {
	      
	      if (symbols.insert(std::make_pair(*siter, vocab_type::EPSILON)).second) {
		rule_ptr_type rule_source(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, *siter))));
		rule_ptr_type rule_target(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, vocab_type::EPSILON))));
		
		// inverted!
		insert(rule_target, rule_source, features, attributes);
	      }

	      for (size_t trg = 0; trg != target.size(); ++ trg) {
		const lattice_type::arc_set_type& arcs_target = target[trg];
		
		lattice_type::arc_set_type::const_iterator titer_end = arcs_target.end();
		for (lattice_type::arc_set_type::const_iterator titer = arcs_target.begin(); titer != titer_end; ++ titer)
		  if (titer->label != vocab_type::EPSILON && symbols.insert(std::make_pair(*siter, titer->label)).second) {
		    rule_ptr_type rule_source(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, *siter))));
		    rule_ptr_type rule_target(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, titer->label))));
		    
		    // inverted!
		    insert(rule_target, rule_source, features, attributes);
		  }
	      }
	    }
	}
    }

    GrammarPair(const lattice_type& source, const lattice_type& target, const symbol_type& non_terminal)
      : GrammarMutable(1)
    {
      typedef std::pair<symbol_type, symbol_type> symbol_pair_type;
      typedef google::dense_hash_set<symbol_pair_type, boost::hash<symbol_pair_type>, std::equal_to<symbol_pair_type> > symbol_pair_set_type;
      
      symbol_pair_set_type symbols;
      symbols.set_empty_key(symbol_pair_type());

      feature_set_type features;
      
      attribute_set_type attributes;
      attributes["pair"] = attribute_set_type::int_type(1);
      
      for (size_t src = 0; src != source.size(); ++ src) {
	const lattice_type::arc_set_type& arcs_source = source[src];
	
	lattice_type::arc_set_type::const_iterator siter_end = arcs_source.end();
	for (lattice_type::arc_set_type::const_iterator siter = arcs_source.begin(); siter != siter_end; ++ siter)
	  if (siter->label != vocab_type::EPSILON) {
	    
	    if (symbols.insert(std::make_pair(siter->label, vocab_type::EPSILON)).second) {
	      rule_ptr_type rule_source(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, siter->label))));
	      rule_ptr_type rule_target(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, vocab_type::EPSILON))));
	      
	      // inverted!
	      insert(rule_target, rule_source, features, attributes);
	    }
	    
	    for (size_t trg = 0; trg != target.size(); ++ trg) {
	      const lattice_type::arc_set_type& arcs_target = target[trg];
	      
	      lattice_type::arc_set_type::const_iterator titer_end = arcs_target.end();
	      for (lattice_type::arc_set_type::const_iterator titer = arcs_target.begin(); titer != titer_end; ++ titer)
		if (titer->label != vocab_type::EPSILON && symbols.insert(std::make_pair(siter->label, titer->label)).second) {
		  rule_ptr_type rule_source(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, siter->label))));
		  rule_ptr_type rule_target(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, titer->label))));
		  
		  // inverted!
		  insert(rule_target, rule_source, features, attributes);
		}
	    }
	  }
      }
    }
  };

  class GrammarPOS : public GrammarMutable
  {
  public:
    typedef Lattice    lattice_type;
    
  public:
    GrammarPOS(const lattice_type& lattice)
      : GrammarMutable(1)
    {
      typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > symbol_set_type;
      
      symbol_set_type symbols;
      symbols.set_empty_key(symbol_type());
      
      feature_set_type features;
      features["pos"] = - 1.0;
      
      attribute_set_type attributes;
      attributes["pos"] = attribute_set_type::int_type(1);
      
      for (size_t first = 0; first != lattice.size(); ++ first) {
	const lattice_type::arc_set_type& arcs = lattice[first];
	
	lattice_type::arc_set_type::const_iterator aiter_end = arcs.end();
	for (lattice_type::arc_set_type::const_iterator aiter = arcs.begin(); aiter != aiter_end; ++ aiter)
	  if (aiter->label != vocab_type::EPSILON && symbols.insert(aiter->label).second) {
	    const symbol_type terminal = aiter->label.terminal();
	    const symbol_type pos = aiter->label.pos();
	    
	    if (pos == symbol_type())
	      throw std::runtime_error("no pos? " + static_cast<const std::string&>(aiter->label));
	    
	    rule_ptr_type rule(rule_type::create(rule_type(pos, rule_type::symbol_set_type(1, terminal))));
	    
	    insert(rule, rule, features, attributes);
	  }
      }
    }
  };


  class GrammarInsertion : public GrammarMutable
  {
  public:
    typedef Lattice    lattice_type;
    typedef HyperGraph hypergraph_type;

  public:
    GrammarInsertion(const hypergraph_type& graph, const symbol_type& non_terminal)
      : GrammarMutable(1)
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
	    if (*siter != vocab_type::EPSILON && siter->is_terminal() && symbols.insert(*siter).second) {
	      rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, *siter))));
	      
	      insert(rule, rule, features, attributes);
	    }
	}
    }

    GrammarInsertion(const lattice_type& lattice, const symbol_type& non_terminal)
      : GrammarMutable(1)
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
	  if (aiter->label != vocab_type::EPSILON && symbols.insert(aiter->label).second) {
	    rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, aiter->label))));
	    
	    insert(rule, rule, features, attributes);
	  }
      }
    }
  };
  
  
  class GrammarDeletion : public GrammarMutable
  {
  public:
    typedef Lattice    lattice_type;
    typedef HyperGraph hypergraph_type;
    
  public:
    GrammarDeletion(const hypergraph_type& graph, const symbol_type& non_terminal)
      : GrammarMutable(1)
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
	    if (*siter != vocab_type::EPSILON && siter->is_terminal() && symbols.insert(*siter).second) {
	      rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, *siter))));
	      
	      insert(rule, rule_epsilon, features, attributes);
	    }
	}
    }

    GrammarDeletion(const lattice_type& lattice, const symbol_type& non_terminal)
      : GrammarMutable(1)
    {
      typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > symbol_set_type;
      
      symbol_set_type symbols;
      symbols.set_empty_key(symbol_type());
      
      feature_set_type features;
      features["deletion-penalty"] = - 1.0;
      
      attribute_set_type attributes;
      attributes["deletion"] = attribute_set_type::int_type(1);

      rule_ptr_type rule_epsilon(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, vocab_type::EPSILON))));
      
      for (size_t first = 0; first != lattice.size(); ++ first) {
	const lattice_type::arc_set_type& arcs = lattice[first];

	lattice_type::arc_set_type::const_iterator aiter_end = arcs.end();
	for (lattice_type::arc_set_type::const_iterator aiter = arcs.begin(); aiter != aiter_end; ++ aiter)
	  if (aiter->label != vocab_type::EPSILON && symbols.insert(aiter->label).second) {
	    rule_ptr_type rule(rule_type::create(rule_type(non_terminal, rule_type::symbol_set_type(1, aiter->label))));
	    
	    insert(rule, rule_epsilon, features, attributes);
	  }
      }
    }
  };
};

#endif
