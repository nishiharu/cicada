// -*- mode: c++ -*-
//
//  Copyright(C) 2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__QUERY_CKY__HPP__
#define __CICADA__QUERY_CKY__HPP__ 1

#include <vector>
#include <algorithm>
#include <set>

#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>
#include <cicada/lattice.hpp>
#include <cicada/grammar.hpp>
#include <cicada/transducer.hpp>
#include <cicada/hypergraph.hpp>

#include <utils/chunk_vector.hpp>
#include <utils/chart.hpp>
#include <utils/hashmurmur.hpp>
#include <utils/sgi_hash_map.hpp>
#include <utils/indexed_set.hpp>

#include <google/dense_hash_map>
#include <google/dense_hash_set>

namespace cicada
{
  
  struct QueryCKY
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;

    typedef Symbol symbol_type;
    typedef Vocab  vocab_type;

    typedef Lattice    lattice_type;
    typedef Grammar    grammar_type;
    typedef Transducer transducer_type;
    typedef HyperGraph hypergraph_type;
    
    typedef hypergraph_type::feature_set_type   feature_set_type;
    typedef hypergraph_type::attribute_set_type attribute_set_type;

    typedef attribute_set_type::attribute_type attribute_type;
    
    typedef hypergraph_type::rule_type     rule_type;
    typedef hypergraph_type::rule_ptr_type rule_ptr_type;

    
    QueryCKY(const grammar_type& __grammar,
	     const bool __treebank=false,
	     const bool __pos_mode=false)
      : grammar(__grammar), treebank(__treebank), pos_mode(__pos_mode)
    {
      node_map.set_empty_key(symbol_level_type());
      closure.set_empty_key(symbol_type());
      closure_head.set_empty_key(symbol_type());
      closure_tail.set_empty_key(symbol_type());
    }
    
    struct Active
    {
      Active(const transducer_type::id_type& __node,
	     const hypergraph_type::edge_type::node_set_type& __tails)
	: node(__node),
	  tails(__tails) {}
      Active(const transducer_type::id_type& __node)
	: node(__node),
	  tails() {}
      
      transducer_type::id_type                  node;
      hypergraph_type::edge_type::node_set_type tails;
    };
    
    typedef Active active_type;
    typedef utils::chunk_vector<active_type, 4096 / sizeof(active_type), std::allocator<active_type> > active_set_type;

    typedef utils::chart<active_set_type, std::allocator<active_set_type> > active_chart_type;
    typedef std::vector<active_chart_type, std::allocator<active_chart_type> > active_chart_set_type;

    typedef hypergraph_type::id_type passive_type;
    typedef std::vector<passive_type, std::allocator<passive_type> > passive_set_type;
    typedef utils::chart<passive_set_type, std::allocator<passive_set_type> > passive_chart_type;

    typedef std::pair<symbol_type, int> symbol_level_type;
    
    struct symbol_level_hash : public utils::hashmurmur<size_t>
    {
      typedef utils::hashmurmur<size_t> hasher_type;
      
      size_t operator()(const symbol_level_type& x) const
      {
	return hasher_type::operator()(x.first, x.second);
      }
    };

    typedef google::dense_hash_map<symbol_level_type, hypergraph_type::id_type, symbol_level_hash, std::equal_to<symbol_level_type> > node_map_type;
    
    typedef google::dense_hash_map<symbol_type, int, boost::hash<symbol_type>, std::equal_to<symbol_type> > closure_level_type;
    typedef google::dense_hash_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > closure_type;
    
    typedef std::vector<symbol_type, std::allocator<symbol_type> > non_terminal_set_type;
    
    struct less_non_terminal
    {
      less_non_terminal(const non_terminal_set_type& __non_terminals) : non_terminals(__non_terminals) {}

      bool operator()(const hypergraph_type::id_type& x, const hypergraph_type::id_type& y) const
      {
	return non_terminals[x] < non_terminals[y] || (non_terminals[x] == non_terminals[y] && x < y);
      }
      
      const non_terminal_set_type& non_terminals;
    };


    template <typename IteratorRule>
    void operator()(const lattice_type& lattice,
		    IteratorRule rule_iter)
    {
      graph.clear();
      
      if (lattice.empty())
	return;
      
      // initialize internal structure...
      actives.clear();
      passives.clear();
      non_terminals.clear();
      
      actives.reserve(grammar.size());
      passives.reserve(lattice.size() + 1);
      
      actives.resize(grammar.size(), active_chart_type(lattice.size() + 1));
      passives.resize(lattice.size() + 1);
      
      // initialize active chart
      for (size_t table = 0; table != grammar.size(); ++ table) {
	const transducer_type::id_type root = grammar[table].root();
	
	for (size_t pos = 0; pos != lattice.size(); ++ pos)
	  if (grammar[table].valid_span(pos, pos, 0))
	    actives[table](pos, pos).push_back(active_type(root));
      }
      
      for (size_t length = 1; length <= lattice.size(); ++ length)
	for (size_t first = 0; first + length <= lattice.size(); ++ first) {
	  const size_t last = first + length;

	  node_map.clear();
	  
	  //std::cerr << "span: " << first << ".." << last << " distance: " << lattice.shortest_distance(first, last) << std::endl;
	  
	  for (size_t table = 0; table != grammar.size(); ++ table) {
	    const transducer_type& transducer = grammar[table];
	    
	    // we will advance active spans, but constrained by transducer's valid span
	    if (transducer.valid_span(first, last, lattice.shortest_distance(first, last))) {
	      // advance dots....
	      
	      // first, extend active items...
	      active_set_type& cell = actives[table](first, last);
	      for (size_t middle = first + 1; middle < last; ++ middle) {
		const active_set_type&  active_arcs  = actives[table](first, middle);
		const passive_set_type& passive_arcs = passives(middle, last);
		
		extend_actives(transducer, active_arcs, passive_arcs, cell);
	      }

	      if (! treebank || length == 1) {
		// then, advance by terminal(s) at lattice[last - 1];
		const active_set_type&  active_arcs  = actives[table](first, last - 1);
		const lattice_type::arc_set_type& passive_arcs = lattice[last - 1];
		
		active_set_type::const_iterator aiter_begin = active_arcs.begin();
		active_set_type::const_iterator aiter_end = active_arcs.end();
		
		if (aiter_begin != aiter_end) {
		  if (pos_mode) {
		    lattice_type::arc_set_type::const_iterator piter_end = passive_arcs.end();
		    for (lattice_type::arc_set_type::const_iterator piter = passive_arcs.begin(); piter != piter_end; ++ piter) {
		      const symbol_type terminal = piter->label.terminal();
		      
		      active_set_type& cell = actives[table](first, last - 1 + piter->distance);
		      
		      // handling of EPSILON rule...
		      if (terminal == vocab_type::EPSILON) {
			for (active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
			  cell.push_back(active_type(aiter->node, aiter->tails));
		      } else {
			for (active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter) {
			  const transducer_type::id_type node = transducer.next(aiter->node, terminal);
			  if (node == transducer.root()) continue;
			  
			  cell.push_back(active_type(node, aiter->tails));
			}
		      }
		    }
		  } else {
		    lattice_type::arc_set_type::const_iterator piter_end = passive_arcs.end();
		    for (lattice_type::arc_set_type::const_iterator piter = passive_arcs.begin(); piter != piter_end; ++ piter) {
		      const symbol_type& terminal = piter->label;
		      
		      active_set_type& cell = actives[table](first, last - 1 + piter->distance);
		      
		      // handling of EPSILON rule...
		      if (terminal == vocab_type::EPSILON) {
			for (active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
			  cell.push_back(active_type(aiter->node, aiter->tails));
		      } else {
			for (active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter) {
			  const transducer_type::id_type node = transducer.next(aiter->node, terminal);
			  if (node == transducer.root()) continue;
			  
			  cell.push_back(active_type(node, aiter->tails));
			}
		      }
		    }
		  }
		}
	      }
	    }
	    
	    // complete active items if possible... The active items may be created from child span due to the
	    // lattice structure...
	    // apply rules on actives at [first, last)
	    
	    active_set_type&  cell         = actives[table](first, last);
	    passive_set_type& passive_arcs = passives(first, last);
	    
	    active_set_type::const_iterator citer_end = cell.end();
	    for (active_set_type::const_iterator citer = cell.begin(); citer != citer_end; ++ citer) {
	      const transducer_type::rule_pair_set_type& rules = transducer.rules(citer->node);
	      
	      if (rules.empty()) continue;
	      
	      transducer_type::rule_pair_set_type::const_iterator riter_begin = rules.begin();
	      transducer_type::rule_pair_set_type::const_iterator riter_end   = rules.end();
	      
	      for (transducer_type::rule_pair_set_type::const_iterator riter = riter_begin; riter != riter_end; ++ riter) {
		*rule_iter = *riter;
		++ rule_iter;

		const rule_ptr_type& rule = riter->source;
		
		apply_rule(rule, citer->tails.begin(), citer->tails.end(), passive_arcs, graph,
			   first, last);
	      }
	    }
	  }
	  
	  if (! passives(first, last).empty()) {
	    //std::cerr << "closure from passives: " << passives(first, last).size() << std::endl;

	    passive_set_type& passive_arcs = passives(first, last);
	    
	    size_t passive_first = 0;
	    
	    closure.clear();
	    passive_set_type::const_iterator piter_end = passive_arcs.end();
	    for (passive_set_type::const_iterator piter = passive_arcs.begin(); piter != piter_end; ++ piter)
	      closure[non_terminals[*piter]] = 0;
	    
	    int unary_loop = 0;
	    for (;;) {
	      const size_t passive_size = passive_arcs.size();
	      const size_t closure_size = closure.size();
	      
	      closure_head.clear();
	      closure_tail.clear();
	      
	      for (size_t table = 0; table != grammar.size(); ++ table) {
		const transducer_type& transducer = grammar[table];
		
		if (! transducer.valid_span(first, last, lattice.shortest_distance(first, last))) continue;
		
		for (size_t p = passive_first; p != passive_size; ++ p) {
		  const symbol_type non_terminal = non_terminals[passive_arcs[p]];
		  
		  const transducer_type::id_type node = transducer.next(transducer.root(), non_terminal);
		  if (node == transducer.root()) continue;
		  
		  const transducer_type::rule_pair_set_type& rules = transducer.rules(node);
		  
		  if (rules.empty()) continue;
		  
		  // passive_arcs "MAY" be modified!
		  
		  closure_tail.insert(non_terminal);
		  
		  transducer_type::rule_pair_set_type::const_iterator riter_end = rules.end();
		  for (transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter) {
		    *rule_iter = *riter;
		    ++ rule_iter;
		    
		    const rule_ptr_type& rule = riter->source;
		    const symbol_type& lhs = rule->lhs;

		    closure_level_type::const_iterator citer = closure.find(lhs);
		    const int level = (citer != closure.end() ? citer->second : 0);
		    
		    closure_head.insert(lhs);
		    
		    apply_rule(rule, &passive_arcs[p], (&passive_arcs[p]) + 1, passive_arcs, graph,
			       first, last, level + 1);
		  }
		}
	      }
	      
	      if (passive_size == passive_arcs.size()) break;
	      
	      passive_first = passive_size;
	      
	      // we use level-one, that is the label assigned for new-lhs!
	      closure_type::const_iterator hiter_end = closure_head.end();
	      for (closure_type::const_iterator hiter = closure_head.begin(); hiter != hiter_end; ++ hiter)
		closure.insert(std::make_pair(*hiter, 1));
	      
	      // increment non-terminal level when used as tails...
	      closure_type::const_iterator titer_end = closure_tail.end();
	      for (closure_type::const_iterator titer = closure_tail.begin(); titer != titer_end; ++ titer)
		++ closure[*titer];
	      
	      if (closure_size != closure.size())
		unary_loop = 0;
	      else
		++ unary_loop;
	      
	      // 4 iterations
	      if (unary_loop == 4) break;
	    }
	  }
	  
	  {
	    // sort passives at passives(first, last) wrt non-terminal label in non_terminals
	    passive_set_type& passive_arcs = passives(first, last);
	    
	    passive_set_type(passive_arcs).swap(passive_arcs);
	    std::sort(passive_arcs.begin(), passive_arcs.end(), less_non_terminal(non_terminals));
	  }
	    
	  //std::cerr << "span: " << first << ".." << last << " passives: " << passives(first, last).size() << std::endl;
	  
	  // extend root with passive items at [first, last)
	  for (size_t table = 0; table != grammar.size(); ++ table) {
	    const transducer_type& transducer = grammar[table];
	    
	    if (! transducer.valid_span(first, last, lattice.shortest_distance(first, last))) continue;
	    
	    const active_set_type&  active_arcs  = actives[table](first, first);
	    const passive_set_type& passive_arcs = passives(first, last);
	    
	    active_set_type& cell = actives[table](first, last);
	    
	    extend_actives(transducer, active_arcs, passive_arcs, cell);
	  }
	}

      graph.clear();
      
      actives.clear();
      passives.clear();
      non_terminals.clear();
    }
    
  private:
    
    template <typename Iterator>
    void apply_rule(const rule_ptr_type& rule,
		    Iterator first,
		    Iterator last,
		    passive_set_type& passives,
		    hypergraph_type& graph,
		    const int lattice_first,
		    const int lattice_last,
		    const int level = 0)
    {
      //std::cerr << "rule: " << *rule << std::endl;

      hypergraph_type::edge_type& edge = graph.add_edge(first, last);
      edge.rule = rule;

      const int cat_level = level;
      
      std::pair<node_map_type::iterator, bool> result = node_map.insert(std::make_pair(std::make_pair(rule->lhs, cat_level), 0));
      if (result.second) {
	hypergraph_type::node_type& node = graph.add_node();
	non_terminals.push_back(rule->lhs);
	passives.push_back(node.id);
	result.first->second = node.id;
      }
      
      graph.connect_edge(edge.id, result.first->second);

#if 0
      std::cerr << "new rule: " << *(edge.rule)
		<< " head: " << edge.head
		<< ' ';
      std::copy(edge.tails.begin(), edge.tails.end(), std::ostream_iterator<int>(std::cerr, " "));
      std::cerr << std::endl;
#endif
      
    }
    
    bool extend_actives(const transducer_type& transducer,
			const active_set_type& actives, 
			const passive_set_type& passives,
			active_set_type& cell)
    {
      active_set_type::const_iterator aiter_begin = actives.begin();
      active_set_type::const_iterator aiter_end   = actives.end();
      
      passive_set_type::const_iterator piter_begin = passives.begin();
      passive_set_type::const_iterator piter_end   = passives.end();

      bool found = false;
      
      if (piter_begin != piter_end)
	for (active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
	  if (transducer.has_next(aiter->node)) {
	    symbol_type label;
	    transducer_type::id_type node = transducer.root();
	    
	    hypergraph_type::edge_type::node_set_type tails(aiter->tails.size() + 1);
	    std::copy(aiter->tails.begin(), aiter->tails.end(), tails.begin());
	    
	    for (passive_set_type::const_iterator piter = piter_begin; piter != piter_end; ++ piter) {
	      const symbol_type& non_terminal = non_terminals[*piter];
	      
	      if (label != non_terminal) {
		node = transducer.next(aiter->node, non_terminal);
		label = non_terminal;
	      }
	      if (node == transducer.root()) continue;
	      
	      tails.back() = *piter;
	      cell.push_back(active_type(node, tails));
	      
	      found = true;
	    }
	  }
      
      return found;
    }
    
  private:
    const grammar_type& grammar;
    const bool treebank;
    const bool pos_mode;

    hypergraph_type graph;
    
    active_chart_set_type  actives;
    passive_chart_type     passives;
    
    node_map_type         node_map;
    closure_level_type    closure;
    closure_type          closure_head;
    closure_type          closure_tail;
    non_terminal_set_type non_terminals;
  };
  
  template <typename Iterator>
  inline
  void query_cky(const Grammar& grammar, const Lattice& lattice, Iterator iter, const bool treebank=false, const bool pos_mode=false)
  {
    QueryCKY(grammar, treebank, pos_mode)(lattice, iter);
  }
};

#endif