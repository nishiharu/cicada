// -*- mode: c++ -*-
//
//  Copyright(C) 2011-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__COMPOSE_TREE_CKY__HPP__
#define __CICADA__COMPOSE_TREE_CKY__HPP__ 1

#include <vector>
#include <deque>
#include <algorithm>
#include <sstream>

#include <cicada/symbol.hpp>
#include <cicada/sentence.hpp>
#include <cicada/vocab.hpp>
#include <cicada/tree_grammar.hpp>
#include <cicada/tree_transducer.hpp>
#include <cicada/grammar.hpp>
#include <cicada/transducer.hpp>
#include <cicada/lattice.hpp>
#include <cicada/hypergraph.hpp>

#include <utils/indexed_set.hpp>
#include <utils/chunk_vector.hpp>
#include <utils/chart.hpp>
#include <utils/hashmurmur3.hpp>
#include <utils/bithack.hpp>
#include <utils/compact_map.hpp>
#include <utils/compact_set.hpp>
#include <utils/unordered_map.hpp>
#include <utils/alloc_vector.hpp>

#include <boost/fusion/tuple.hpp>

//
// CFG parsing over lattice
//

namespace cicada
{
  
  struct ComposeTreeCKY
  {
    typedef Symbol symbol_type;
    typedef Vocab  vocab_type;
    
    typedef Sentence sentence_type;
    typedef Sentence phrase_type;

    typedef TreeGrammar    tree_grammar_type;
    typedef TreeTransducer tree_transducer_type;
    
    typedef Grammar    grammar_type;
    typedef Transducer transducer_type;
    
    typedef Lattice    lattice_type;
    typedef HyperGraph hypergraph_type;
    
    typedef hypergraph_type::feature_set_type   feature_set_type;
    typedef hypergraph_type::attribute_set_type attribute_set_type;

    typedef attribute_set_type::attribute_type attribute_type;
    
    typedef hypergraph_type::rule_type        rule_type;
    typedef hypergraph_type::rule_ptr_type    rule_ptr_type;
    
    typedef tree_transducer_type::rule_type          tree_rule_type;
    typedef tree_transducer_type::rule_ptr_type      tree_rule_ptr_type;
    
    ComposeTreeCKY(const symbol_type& __goal,
		   const tree_grammar_type& __tree_grammar,
		   const grammar_type& __grammar,
		   const bool __yield_source,
		   const bool __frontier,
		   const bool __unique_goal)
      : goal(__goal),
	tree_grammar(__tree_grammar), 
	grammar(__grammar),
	yield_source(__yield_source),
	frontier_attribute(__frontier),
	unique_goal(__unique_goal),
	attr_internal_node("internal-node"),
	attr_span_first("span-first"),
	attr_span_last("span-last"),
	attr_tree_fallback("tree-fallback"),
	attr_glue_tree(__grammar.empty() ? "" : "glue-tree"),
	attr_glue_tree_fallback("glue-tree-fallback"),
	attr_frontier_source(__frontier ? "frontier-source" : ""),
        attr_frontier_target(__frontier ? "frontier-target" : "")
    {  
      goal_rule = rule_type::create(rule_type(vocab_type::GOAL, rule_type::symbol_set_type(1, goal.non_terminal())));
    }
    
    struct ActiveTree
    {
      tree_transducer_type::id_type             node;
      hypergraph_type::edge_type::node_set_type tails;
      feature_set_type   features;
      attribute_set_type attributes;
      
      ActiveTree(const tree_transducer_type::id_type& __node)
	: node(__node), tails(), features(), attributes() {}
      ActiveTree(const tree_transducer_type::id_type& __node,
		 const hypergraph_type::edge_type::node_set_type& __tails,
		 const feature_set_type& __features,
		 const attribute_set_type& __attributes)
	: node(__node), tails(__tails), features(__features), attributes(__attributes) {}
    };
    
    struct ActiveRule
    {
      transducer_type::id_type                  node;
      hypergraph_type::edge_type::node_set_type tails;
      feature_set_type   features;
      attribute_set_type attributes;
      
      ActiveRule(const transducer_type::id_type& __node)
	: node(__node), tails(), features(), attributes() {}
      ActiveRule(const transducer_type::id_type& __node,
		 const hypergraph_type::edge_type::node_set_type& __tails,
		 const feature_set_type& __features,
		 const attribute_set_type& __attributes)
	: node(__node), tails(__tails), features(__features), attributes(__attributes) {}
    };

    typedef ActiveTree active_tree_type;
    typedef ActiveRule active_rule_type;
    
    typedef utils::chunk_vector<active_tree_type, 4096 / sizeof(active_tree_type), std::allocator<active_tree_type> > active_tree_set_type;
    typedef utils::chunk_vector<active_rule_type, 4096 / sizeof(active_rule_type), std::allocator<active_rule_type> > active_rule_set_type;
    
    typedef utils::chart<active_tree_set_type, std::allocator<active_tree_set_type> > active_tree_chart_type;
    typedef utils::chart<active_rule_set_type, std::allocator<active_rule_set_type> > active_rule_chart_type;
    
    typedef std::vector<active_tree_chart_type, std::allocator<active_tree_chart_type> > active_tree_chart_set_type;
    typedef std::vector<active_rule_chart_type, std::allocator<active_rule_chart_type> > active_rule_chart_set_type;
    
    typedef hypergraph_type::id_type passive_type;
    typedef std::vector<passive_type, std::allocator<passive_type> > passive_set_type;
    typedef utils::chart<passive_set_type, std::allocator<passive_set_type> > passive_chart_type;
    
    typedef std::pair<symbol_type, int> symbol_level_type;
    
    struct symbol_level_hash : public utils::hashmurmur3<size_t>
    {
      typedef utils::hashmurmur3<size_t> hasher_type;
      
      size_t operator()(const symbol_level_type& x) const
      {
	return hasher_type::operator()(x.first, x.second);
      }
    };

    struct symbol_level_unassigned : utils::unassigned<symbol_type>
    {
      symbol_level_type operator()() const
      {
	return symbol_level_type(utils::unassigned<symbol_type>::operator()(), -1);
      }
    };

    typedef utils::compact_map<symbol_level_type, hypergraph_type::id_type,
			       symbol_level_unassigned, symbol_level_unassigned,
			       symbol_level_hash, std::equal_to<symbol_level_type>,
			       std::allocator<std::pair<const symbol_level_type, hypergraph_type::id_type> > > node_map_type;
    
    typedef utils::compact_map<symbol_type, hypergraph_type::id_type,
			       utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
			       boost::hash<symbol_type>, std::equal_to<symbol_type>,
			       std::allocator<std::pair<const symbol_type, hypergraph_type::id_type> > > node_set_type;
    
    typedef utils::chunk_vector<node_set_type, 4096 / sizeof(node_set_type), std::allocator<node_set_type> > node_graph_type;
    typedef std::vector<symbol_type, std::allocator<symbol_type> > non_terminal_set_type;
    
    typedef utils::compact_map<symbol_type, int,
			       utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
			       boost::hash<symbol_type>, std::equal_to<symbol_type>,
			       std::allocator<std::pair<const symbol_type, int> > > closure_level_type;
    typedef utils::compact_set<symbol_type,
			       utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
			       boost::hash<symbol_type>, std::equal_to<symbol_type>,
			       std::allocator<symbol_type> > closure_type;
    
    typedef hypergraph_type::edge_type::node_set_type tail_set_type;
    typedef rule_type::symbol_set_type                symbol_set_type;
    
    template <typename Seq>
    struct hash_sequence : utils::hashmurmur3<size_t>
    {
      typedef utils::hashmurmur3<size_t> hasher_type;
      
      size_t operator()(const Seq& x) const
      {
	return hasher_type::operator()(x.begin(), x.end(), 0);
      }
    };
    
    typedef utils::indexed_set<tail_set_type, hash_sequence<tail_set_type>, std::equal_to<tail_set_type>,
			       std::allocator<tail_set_type> > internal_tail_set_type;
    typedef utils::indexed_set<symbol_set_type, hash_sequence<symbol_set_type>, std::equal_to<symbol_set_type>,
			       std::allocator<symbol_set_type> > internal_symbol_set_type;
    
    typedef boost::fusion::tuple<internal_tail_set_type::index_type, internal_symbol_set_type::index_type, symbol_type> internal_label_type;
    typedef boost::fusion::tuple<int, internal_symbol_set_type::index_type, symbol_type> terminal_label_type;
    
    template <typename Tp>
    struct unassigned_key : utils::unassigned<symbol_type>
    {
      Tp operator()() const { return Tp(-1, -1, utils::unassigned<symbol_type>::operator()()); }
    };
    
    typedef utils::compact_map<internal_label_type, hypergraph_type::id_type,
			       unassigned_key<internal_label_type>, unassigned_key<internal_label_type>,
			       utils::hashmurmur3<size_t>, std::equal_to<internal_label_type>,
			       std::allocator<std::pair<const internal_label_type, hypergraph_type::id_type> > > internal_label_map_type;
    typedef utils::compact_map<terminal_label_type, hypergraph_type::id_type,
			       unassigned_key<terminal_label_type>, unassigned_key<terminal_label_type>,
			       utils::hashmurmur3<size_t>, std::equal_to<terminal_label_type>,
			       std::allocator<std::pair<const terminal_label_type, hypergraph_type::id_type> > > terminal_label_map_type;

    typedef utils::alloc_vector<terminal_label_map_type, std::allocator<terminal_label_map_type> > terminal_label_map_set_type;
    
    typedef std::vector<bool, std::allocator<bool> > connected_type;
    
    template <typename Tp>
    struct ptr_hash
    {
      size_t operator()(const boost::shared_ptr<Tp>& x) const
      {
	return (x ? hash_value(*x) : size_t(0));
      }
      
      size_t operator()(const Tp* x) const
      {
	return (x ? hash_value(*x) : size_t(0));
      }
    };
    
    template <typename Tp>
    struct ptr_equal_to
    {
      bool operator()(const boost::shared_ptr<Tp>& x, const boost::shared_ptr<Tp>& y) const
      {
	return x == y ||(x && y && *x == *y);
      }
      
      bool operator()(const Tp* x, const Tp* y) const
      {
	return x == y ||(x && y && *x == *y);
      }
    };
    
    typedef utils::unordered_map<const rule_type*, rule_ptr_type, ptr_hash<rule_type>, ptr_equal_to<rule_type>,
				 std::allocator<std::pair<const rule_type* const, rule_ptr_type> > >::type rule_cache_type;

    typedef utils::unordered_map<rule_ptr_type, std::string, ptr_hash<rule_type>, ptr_equal_to<rule_type>,
				 std::allocator<std::pair<const rule_ptr_type, std::string> > >::type frontier_set_type;
    
    typedef utils::unordered_map<tree_rule_ptr_type, std::string, ptr_hash<tree_rule_type>, ptr_equal_to<tree_rule_type>,
				 std::allocator<std::pair<const tree_rule_ptr_type, std::string> > >::type tree_frontier_set_type;
    
    struct less_non_terminal
    {
      less_non_terminal(const non_terminal_set_type& __non_terminals) : non_terminals(__non_terminals) {}
      
      bool operator()(const hypergraph_type::id_type& x, const hypergraph_type::id_type& y) const
      {
	return non_terminals[x] < non_terminals[y] || (non_terminals[x] == non_terminals[y] && x < y);
      }
      
      const non_terminal_set_type& non_terminals;
    };

    struct ExtractTreeLHS
    {
      template <typename RulePair>
      const symbol_type& operator()(const RulePair& rule_pair) const
      {
	return rule_pair.source->label;
      }
    };
    
    struct ExtractRuleLHS
    {
      template <typename RulePair>
      const symbol_type& operator()(const RulePair& rule_pair) const
      {
	return rule_pair.source->lhs;
      }
    };
    
    struct VerifyNone
    {
      template <typename Transducer>
      bool operator()(const Transducer& transducer, const size_t first, const size_t last, const size_t distance) const
      {
	return true;
      }
    };
    
    struct VerifySpan
    {
      template <typename Transducer>
      bool operator()(const Transducer& transducer, const size_t first, const size_t last, const size_t distance) const
      {
	return transducer.valid_span(first, last, distance);
      }
    };

    void operator()(const lattice_type& lattice, hypergraph_type& graph)
    {
      graph.clear();
      
      if (lattice.empty())
	return;
      
      // initialize internal structure...
      actives_tree.clear();
      actives_rule.clear();
      passives.clear();
      
      tail_map.clear();
      symbol_map.clear();
      symbol_map_terminal.clear();
      label_map.clear();
      terminal_map_local.clear();
      terminal_map_global.clear();

      rule_cache.clear();
      
      node_map.clear();
      node_graph_tree.clear();
      node_graph_rule.clear();
      node_graph_glue.clear();
      non_terminals.clear();
      
      actives_tree.reserve(tree_grammar.size());
      actives_rule.reserve(grammar.size());
      
      actives_tree.resize(tree_grammar.size(), active_tree_chart_type(lattice.size() + 1));
      actives_rule.resize(grammar.size(), active_rule_chart_type(lattice.size() + 1));
      
      passives.reserve(lattice.size() + 1);
      passives.resize(lattice.size() + 1);
      
      frontiers_source.clear();
      frontiers_target.clear();
      tree_frontiers_source.clear();
      tree_frontiers_target.clear();

      connected.clear();

      // initialize active chart
      for (size_t table = 0; table != tree_grammar.size(); ++ table) {
	const tree_transducer_type::id_type root = tree_grammar[table].root();
	
	for (size_t pos = 0; pos != lattice.size(); ++ pos)
	  actives_tree[table](pos, pos).push_back(active_tree_type(root));
      }
      
      for (size_t table = 0; table != grammar.size(); ++ table) {
	const transducer_type::id_type root = grammar[table].root();
	
	for (size_t pos = 0; pos != lattice.size(); ++ pos)
	  if (grammar[table].valid_span(pos, pos, 0))
	    actives_rule[table](pos, pos).push_back(active_rule_type(root));
      }
      
      for (size_t length = 1; length <= lattice.size(); ++ length)
	for (size_t first = 0; first + length <= lattice.size(); ++ first) {
	  const size_t last = first + length;

	  //std::cerr << "span: " << first << ".." << last << std::endl;
	  
	  terminal_map_local.clear();
	  
	  node_map.clear();
	  
	  extend_actives(first, last, lattice, grammar,      actives_rule, VerifySpan());
	  extend_actives(first, last, lattice, tree_grammar, actives_tree, VerifySpan());
	  
	  complete_actives(first, last, grammar,      actives_rule, graph, ExtractRuleLHS());
	  complete_actives(first, last, tree_grammar, actives_tree, graph, ExtractTreeLHS());

	  //std::cerr << "passives size: " << passives(first, last).size() << std::endl;
	  
	  // handle unary rules...
	  // TODO: handle unary rules both for tree-grammar and grammar!!!!
	  if (! passives(first, last).empty()) {
	    //std::cerr << "closure from passives: " << passives(first, last).size() << std::endl;
	    
	    passive_set_type& passive_arcs = passives(first, last);
	    
	    size_t passive_first = 0;
	    
	    // initialize closure..
	    closure.clear();
	    passive_set_type::const_iterator piter_end = passive_arcs.end();
	    for (passive_set_type::const_iterator piter = passive_arcs.begin(); piter != piter_end; ++ piter)
	      closure[non_terminals[*piter]] = 0;

	    hypergraph_type::edge_type::node_set_type tails(1);
	    
	    int unary_loop = 0;
	    for (;;) {
	      const size_t passive_size = passive_arcs.size();
	      const size_t closure_size = closure.size();
	      
	      closure_head.clear();
	      closure_tail.clear();
	      
	      for (size_t table = 0; table != tree_grammar.size(); ++ table) {
		const tree_transducer_type& transducer = tree_grammar[table];
		
		for (size_t p = passive_first; p != passive_size; ++ p) {
		  const symbol_type non_terminal = non_terminals[passive_arcs[p]];
		  
		  const tree_transducer_type::id_type node = transducer.next(transducer.root(), non_terminal);
		  if (node == transducer.root()) continue;
		  
		  const tree_transducer_type::rule_pair_set_type& rules = transducer.rules(node);
		  
		  if (rules.empty()) continue;
		  
		  closure_tail.insert(non_terminal);
		  
		  tree_transducer_type::rule_pair_set_type::const_iterator riter_end = rules.end();
		  for (tree_transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter) {
		    const tree_rule_ptr_type& rule = (yield_source ? riter->source : riter->target);
		    const symbol_type& lhs = rule->label;
		    
		    closure_level_type::const_iterator citer = closure.find(lhs);
		    const int level = (citer != closure.end() ? citer->second : 0);
		    
		    closure_head.insert(lhs);
		    
		    tails.front() = passive_arcs[p];
		    
		    if (frontier_attribute)
		      apply_rule(riter->source->label,
				 rule,
				 riter->features,
				 riter->attributes + frontier_attributes(riter->source, riter->target),
				 rule_internal_size(*(riter->source)),
				 tails, 
				 passive_arcs,
				 graph,
				 first,
				 last,
				 level + 1);
		    else
		      apply_rule(riter->source->label,
				 rule,
				 riter->features,
				 riter->attributes,
				 rule_internal_size(*(riter->source)),
				 tails, 
				 passive_arcs,
				 graph,
				 first,
				 last,
				 level + 1);
		  }
		}
	      }

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
		    const rule_ptr_type& rule = (yield_source ? riter->source : riter->target);
		    const symbol_type& lhs = rule->lhs;
		    
		    closure_level_type::const_iterator citer = closure.find(lhs);
		    const int level = (citer != closure.end() ? citer->second : 0);
		    
		    closure_head.insert(lhs);
		    
		    tails.front() = passive_arcs[p];
		    
		    if (frontier_attribute)
		      apply_rule(riter->source->lhs,
				 rule,
				 riter->features,
				 riter->attributes + frontier_attributes(riter->source, riter->target),
				 rule_internal_size(*(riter->source)),
				 tails,
				 passive_arcs,
				 graph,
				 first,
				 last,
				 level + 1);
		    else
		      apply_rule(riter->source->lhs,
				 rule,
				 riter->features,
				 riter->attributes,
				 rule_internal_size(*(riter->source)),
				 tails,
				 passive_arcs,
				 graph,
				 first,
				 last,
				 level + 1);
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
	      
	      // 2 iterations
	      if (unary_loop == 2) break;
	    }
	  }
	  
	  // sort passives at passives(first, last) wrt non-terminal label in non_terminals
	  {
	    passive_set_type& passive_arcs = passives(first, last);
	    
	    passive_set_type(passive_arcs).swap(passive_arcs);
	    std::sort(passive_arcs.begin(), passive_arcs.end(), less_non_terminal(non_terminals));
	  }

	  //std::cerr << "finished unary-loop passives size: " << passives(first, last).size() << std::endl;
	  
	  // extend root with passive items at [first, last)
	  // we need to do this for simple transducers, also...
	  for (size_t table = 0; table != tree_grammar.size(); ++ table) {
	    const tree_transducer_type& transducer = tree_grammar[table];
	    
	    const active_tree_set_type& active_arcs  = actives_tree[table](first, first);
	    const passive_set_type&     passive_arcs = passives(first, last);
	    
	    active_tree_set_type& cell = actives_tree[table](first, last);
	    
	    extend_actives(transducer, active_arcs, passive_arcs, cell);
	  }
	  
	  for (size_t table = 0; table != grammar.size(); ++ table) {
	    const transducer_type& transducer = grammar[table];
	    
	    const active_rule_set_type& active_arcs  = actives_rule[table](first, first);
	    const passive_set_type&     passive_arcs = passives(first, last);
	    
	    active_rule_set_type& cell = actives_rule[table](first, last);
	    
	    extend_actives(transducer, active_arcs, passive_arcs, cell);
	  }
	}
      
      //
      // final patch work..
      //
      // we will connect node_graph_rule into node_graph_tree
      //
      //std::cerr << "patch work: tree-graph size: " << node_graph_tree.size() << std::endl;
      
      for (size_t node_id = 0; node_id != node_graph_tree.size(); ++ node_id) {
	const node_set_type& node_set_tree = node_graph_tree[node_id];
	const node_set_type& node_set_rule = node_graph_rule[node_id];

	if (node_set_tree.empty() || node_set_rule.empty()) continue;

	node_set_type::const_iterator citer_begin = node_set_rule.begin();
	node_set_type::const_iterator citer_end   = node_set_rule.end();

	node_set_type::const_iterator piter_begin = node_set_tree.begin();
	node_set_type::const_iterator piter_end   = node_set_tree.end();
	
	for (node_set_type::const_iterator citer = citer_begin; citer != citer_end; ++ citer) {
	  
	  if (citer->second >= connected.size())
	    connected.resize(citer->second + 1, false);
	  
	  //if (connected[citer->second]) continue;
	  
	  for (node_set_type::const_iterator piter = piter_begin; piter != piter_end; ++ piter) 
	    if (piter->second != citer->second) {
	      hypergraph_type::edge_type& edge = graph.add_edge(&(citer->second), &(citer->second) + 1);
	      
	      edge.rule = rule_type::create(rule_type(piter->first, &(citer->first), &(citer->first) + 1));
	      edge.attributes[attr_glue_tree] = attribute_set_type::int_type(1);
	      
	      graph.connect_edge(edge.id, piter->second);
	    }
	  
	  connected[citer->second] = true;
	}
      }
      
      // final goal assignment...
      if (unique_goal) {
	passive_set_type& passive_arcs = passives(0, lattice.size());
	for (size_t p = 0; p != passive_arcs.size(); ++ p) {
	  const node_set_type& node_set = node_graph_tree[passive_arcs[p]];

	  node_set_type::const_iterator giter = node_set.find(goal);
	  if (giter != node_set.end()) {
	    if (graph.is_valid())
	      throw std::runtime_error("multiple goal? " + boost::lexical_cast<std::string>(graph.goal) + " " + boost::lexical_cast<std::string>(passive_arcs[p]));
	    
	    graph.goal = giter->second;
	    
	    if (giter->second >= connected.size())
	      connected.resize(giter->second + 1, false);
	    
	    connected[giter->second] = true;
	  }
	}
	
      } else {
	passive_set_type& passive_arcs = passives(0, lattice.size());
	for (size_t p = 0; p != passive_arcs.size(); ++ p) {
	  const node_set_type& node_set = node_graph_tree[passive_arcs[p]];
	  
	  node_set_type::const_iterator giter = node_set.find(goal);
	  if (giter == node_set.end()) continue;
	  
	  hypergraph_type::edge_type& edge = graph.add_edge(&(giter->second), &(giter->second) + 1);
	  edge.rule = goal_rule;
	  edge.attributes[attr_span_first] = attribute_set_type::int_type(0);
	  edge.attributes[attr_span_last]  = attribute_set_type::int_type(lattice.size());
	  
	  if (! graph.is_valid())
	    graph.goal = graph.add_node().id;
	  
	  graph.connect_edge(edge.id, graph.goal);

	  if (giter->second >= connected.size())
	    connected.resize(giter->second + 1, false);
	  
	  connected[giter->second] = true;
	}
      }
      
      // final glue work!
      if (graph.is_valid())
	for (size_t node_id = 0; node_id != node_graph_tree.size(); ++ node_id) {
	  const node_set_type& node_set_tree = node_graph_tree[node_id];
	  const node_set_type& node_set_glue = node_graph_glue[node_id];
	  
	  if (node_set_tree.empty() || node_set_glue.empty()) continue;

	  node_set_type::const_iterator citer_begin = node_set_tree.begin();
	  node_set_type::const_iterator citer_end   = node_set_tree.end();
	  
	  node_set_type::const_iterator piter_begin = node_set_glue.begin();
	  node_set_type::const_iterator piter_end   = node_set_glue.end();
	  
	  for (node_set_type::const_iterator piter = piter_begin; piter != piter_end; ++ piter)
	    if (connected[piter->second]) 
	      for (node_set_type::const_iterator citer = citer_begin; citer != citer_end; ++ citer)
		if (! connected[citer->second] && node_set_glue.find(citer->first) == piter_end) {
		  hypergraph_type::edge_type& edge = graph.add_edge(&(citer->second), &(citer->second) + 1);
		  
		  edge.rule = rule_type::create(rule_type(piter->first, &(citer->first), &(citer->first) + 1));
		  edge.attributes[attr_glue_tree_fallback] = attribute_set_type::int_type(1);
		  
		  graph.connect_edge(edge.id, piter->second);
		}
	}
      
      
      // we will sort to remove unreachable nodes......
      if (graph.is_valid())
	graph.topologically_sort();
    }
    
    template <typename Transducers, typename Actives, typename ExtractLHS>
    void complete_actives(const size_t first,
			  const size_t last,
			  const Transducers& transducers,
			  const Actives& actives,
			  hypergraph_type& graph,
			  ExtractLHS extract_lhs)
    {
      typedef typename Actives::value_type::value_type active_set_type;
      typedef typename Transducers::transducer_type transducer_type;

      for (size_t table = 0; table != transducers.size(); ++ table) {
	const transducer_type& transducer = transducers[table];
	
	const active_set_type&  cell         = actives[table](first, last);
	passive_set_type&       passive_arcs = passives(first, last);
	
	typename active_set_type::const_iterator citer_end = cell.end();
	for (typename active_set_type::const_iterator citer = cell.begin(); citer != citer_end; ++ citer) {
	  const typename transducer_type::rule_pair_set_type& rules = transducer.rules(citer->node);
	  
	  if (rules.empty()) continue;

#if 0
	  std::cerr << "# of rules: " << rules.size() << " tails: ";
	  std::copy(citer->tails.begin(), citer->tails.end(), std::ostream_iterator<int>(std::cerr, " "));
	  std::cerr << std::endl;
#endif
	  
	  if (frontier_attribute) {
	    typename transducer_type::rule_pair_set_type::const_iterator riter_end  = rules.end();
	    for (typename transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter)
	      apply_rule(extract_lhs(*riter),
			 yield_source ? riter->source : riter->target,
			 riter->features + citer->features,
			 riter->attributes + citer->attributes + frontier_attributes(riter->source, riter->target),
			 rule_internal_size(*(riter->source)),
			 citer->tails, 
			 passive_arcs,
			 graph,
			 first, 
			 last);
	  } else {
	    typename transducer_type::rule_pair_set_type::const_iterator riter_end  = rules.end();
	    for (typename transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter)
	      apply_rule(extract_lhs(*riter),
			 yield_source ? riter->source : riter->target,
			 riter->features + citer->features,
			 riter->attributes + citer->attributes,
			 rule_internal_size(*(riter->source)),
			 citer->tails, 
			 passive_arcs,
			 graph,
			 first, 
			 last);
	  }
	}
      }
    }
    
    
    template <typename Transducers, typename Actives, typename Verify>
    void extend_actives(const size_t first,
			const size_t last,
			const lattice_type& lattice,
			const Transducers& transducers,
			Actives& actives,
			Verify verify)
    {
      typedef typename Actives::value_type::value_type active_set_type;
      typedef typename Transducers::transducer_type transducer_type;
      
      for (size_t table = 0; table != transducers.size(); ++ table) {
	const transducer_type& transducer = transducers[table];
	
	if (! verify(transducer, first, last, lattice.shortest_distance(first, last))) continue;

	//std::cerr << "table: " << table << " span: " << first << ".." << last << std::endl;
	
	active_set_type& cell = actives[table](first, last);
	for (size_t middle = first + 1; middle < last; ++ middle)
	  extend_actives(transducers[table], actives[table](first, middle), passives(middle, last), cell);
	
	// then, advance by terminal(s) at lattice[last - 1];
	const active_set_type&            active_arcs  = actives[table](first, last - 1);
	const lattice_type::arc_set_type& passive_arcs = lattice[last - 1];
	
	typename active_set_type::const_iterator aiter_begin = active_arcs.begin();
	typename active_set_type::const_iterator aiter_end   = active_arcs.end();
	
	if (aiter_begin == aiter_end) continue;
	
	lattice_type::arc_set_type::const_iterator piter_end = passive_arcs.end();
	for (lattice_type::arc_set_type::const_iterator piter = passive_arcs.begin(); piter != piter_end; ++ piter) {
	  const symbol_type& terminal = piter->label;
	  
	  active_set_type& cell = actives[table](first, last - 1 + piter->distance);
	  
	  // handling of EPSILON rule...
	  if (terminal == vocab_type::EPSILON) {
	    for (typename active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
	      cell.push_back(typename active_set_type::value_type(aiter->node, aiter->tails, aiter->features + piter->features, aiter->attributes));
	  } else {
	    for (typename active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter) {
	      const typename transducer_type::id_type node = transducer.next(aiter->node, terminal);
	      if (node == transducer.root()) continue;
	      
	      cell.push_back(typename active_set_type::value_type(node, aiter->tails, aiter->features + piter->features, aiter->attributes));
	    }
	  }
	}
      }
    }
    
    template <typename Transducer, typename Actives>
    bool extend_actives(const Transducer& transducer,
			const Actives& actives, 
			const passive_set_type& passives,
			Actives& cell)
    {
      typename Actives::const_iterator aiter_begin = actives.begin();
      typename Actives::const_iterator aiter_end   = actives.end();
      
      passive_set_type::const_iterator piter_begin = passives.begin();
      passive_set_type::const_iterator piter_end   = passives.end();
      
      bool found = false;
      
      if (piter_begin != piter_end)
	for (typename Actives::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
	  if (transducer.has_next(aiter->node)) {
	    symbol_type label;
	    typename Transducer::id_type node = transducer.root();
	    
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
	      cell.push_back(typename Actives::value_type(node, tails, aiter->features, aiter->attributes));
	      
	      found = true;
	    }
	  }
      
      return found;
    }
    
    
    void apply_rule(const symbol_type& lhs,
		    const rule_ptr_type& rule,
		    const feature_set_type& features,
		    const attribute_set_type& attributes,
		    const attribute_set_type::int_type& internal_size,
		    const hypergraph_type::edge_type::node_set_type& frontier,
		    passive_set_type& passives,
		    hypergraph_type& graph,
		    const int lattice_first,
		    const int lattice_last,
		    const int level = 0)
    {
      //
      // we need to transform the source-frontier into target-frontier!
      //
      
      hypergraph_type::edge_type::node_set_type tails(frontier.size());
      
#if 0
      std::cerr << "rule: " << *rule << std::endl;
      std::cerr << "rule frontier: ";
      std::copy(frontier.begin(), frontier.end(), std::ostream_iterator<int>(std::cerr, " "));
      std::cerr << std::endl;
#endif
      
      if (! frontier.empty()) {
	int non_terminal_pos = 0;
	rule_type::symbol_set_type::const_iterator riter_end = rule->rhs.end();
	for (rule_type::symbol_set_type::const_iterator riter = rule->rhs.begin(); riter != riter_end; ++ riter)
	  if (riter->is_non_terminal()) {
	    const int __non_terminal_index = riter->non_terminal_index();
	    const int non_terminal_index = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	    ++ non_terminal_pos;
	    
	    node_set_type& node_set = node_graph_rule[frontier[non_terminal_index]];
	    std::pair<node_set_type::iterator, bool> result = node_set.insert(std::make_pair(riter->non_terminal(), 0));
	    if (result.second)
	      result.first->second = graph.add_node().id;
	    
	    tails[non_terminal_index] = result.first->second;
	  }
      }
      
      hypergraph_type::edge_type& edge = graph.add_edge(tails.begin(), tails.end());
      edge.rule = rule;
      edge.features   = features;
      edge.attributes = attributes;
      
      // assign metadata...
      edge.attributes[attr_span_first] = attribute_set_type::int_type(lattice_first);
      edge.attributes[attr_span_last]  = attribute_set_type::int_type(lattice_last);
      
      const int cat_level = utils::bithack::branch(unique_goal && rule->lhs == goal, 0, level);
      //const int& cat_level = level;
      
      // source lhs
      std::pair<node_map_type::iterator, bool> result = node_map.insert(std::make_pair(std::make_pair(lhs, cat_level), 0));
      if (result.second) {
	result.first->second = node_graph_tree.size();
	non_terminals.push_back(lhs);
	passives.push_back(node_graph_tree.size());
	node_graph_tree.resize(node_graph_tree.size() + 1);
	node_graph_rule.resize(node_graph_rule.size() + 1);
	node_graph_glue.resize(node_graph_glue.size() + 1);
      }
      
      // projected lhs
      std::pair<node_set_type::iterator, bool> result_mapped = node_graph_rule[result.first->second].insert(std::make_pair(rule->lhs, 0));
      if (result_mapped.second)
	result_mapped.first->second = graph.add_node().id;
      
      graph.connect_edge(edge.id, result_mapped.first->second);
      
      hypergraph_type::edge_type::node_set_type::const_iterator titer_end = tails.end();
      for (hypergraph_type::edge_type::node_set_type::const_iterator titer = tails.begin(); titer != titer_end; ++ titer) {
	if (*titer >= connected.size())
	  connected.resize(*titer + 1, false);
	
	connected[*titer] = true;
      }
    }
    
    void apply_rule(const symbol_type& lhs,
		    const tree_rule_ptr_type& rule,
		    const feature_set_type& features,
		    const attribute_set_type& attributes,
		    const attribute_set_type::int_type& internal_size,
		    const hypergraph_type::edge_type::node_set_type& frontier,
		    passive_set_type& passives,
		    hypergraph_type& graph,
		    const int lattice_first,
		    const int lattice_last,
		    const int level = 0)
    {
      const int cat_level = utils::bithack::branch(unique_goal && rule->label == goal, 0, level);
      //const int& cat_level = level;

      //std::cerr << "lhs: " << lhs << ":" << cat_level << " " << *rule << std::endl;
      
      // source lhs
      std::pair<node_map_type::iterator, bool> result = node_map.insert(std::make_pair(std::make_pair(lhs, cat_level), 0));
      if (result.second) {
	result.first->second = node_graph_tree.size();
	non_terminals.push_back(lhs);
	passives.push_back(node_graph_tree.size());
	node_graph_tree.resize(node_graph_tree.size() + 1);
	node_graph_rule.resize(node_graph_rule.size() + 1);
	node_graph_glue.resize(node_graph_glue.size() + 1);
      }
      
      // projected lhs
      std::pair<node_set_type::iterator, bool> result_mapped = node_graph_tree[result.first->second].insert(std::make_pair(rule->label, 0));
      if (result_mapped.second)
	result_mapped.first->second = graph.add_node().id;

      const bool is_fallback = is_tree_fallback(attributes);
      
      const hypergraph_type::id_type root_id = result_mapped.first->second;

#if 0
      std::cerr << "tree-rule: " << *rule << std::endl;
      std::cerr << "tree-rule frontier: ";
      std::copy(frontier.begin(), frontier.end(), std::ostream_iterator<int>(std::cerr, " "));
      std::cerr << std::endl;
#endif 
      
      int non_terminal_pos = 0;
      hypergraph_type::id_type node_prev = hypergraph_type::invalid;
      int relative_pos = 0;
      
      const hypergraph_type::id_type edge_id = construct_graph(*rule, root_id, frontier, graph, non_terminal_pos, node_prev, relative_pos, is_fallback);
      
      graph.edges[edge_id].features   = features;
      graph.edges[edge_id].attributes = attributes;
      
      // assign metadata only for the root edge...?????
      graph.edges[edge_id].attributes[attr_span_first] = attribute_set_type::int_type(lattice_first);
      graph.edges[edge_id].attributes[attr_span_last]  = attribute_set_type::int_type(lattice_last);
      
      if (internal_size)
	graph.edges[edge_id].attributes[attr_internal_node] = internal_size;
    }

    attribute_set_type::int_type rule_internal_size(const tree_rule_type& rule) const
    {
      return rule.size_internal();
    }
    
    attribute_set_type::int_type rule_internal_size(const rule_type& rule) const
    {
      return 0;
    }
    
    hypergraph_type::id_type construct_graph(const tree_rule_type& rule,
					     hypergraph_type::id_type root,
					     const hypergraph_type::edge_type::node_set_type& frontiers,
					     hypergraph_type& graph,
					     int& non_terminal_pos,
					     hypergraph_type::id_type& node_prev,
					     int& relative_pos,
					     const bool is_fallback)
    {
      typedef std::vector<symbol_type, std::allocator<symbol_type> > rhs_type;
      typedef std::vector<hypergraph_type::id_type, std::allocator<hypergraph_type::id_type> > tails_type;
      
      rhs_type rhs;
      tails_type tails;
      
      tree_rule_type::const_iterator aiter_end = rule.end();
      for (tree_rule_type::const_iterator aiter = rule.begin(); aiter != aiter_end; ++ aiter)
	if (aiter->label.is_non_terminal()) {
	  if (aiter->antecedents.empty()) {
	    const int __non_terminal_index = aiter->label.non_terminal_index();
	    const int non_terminal_index = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	    ++ non_terminal_pos;
	    
	    if (non_terminal_index >= static_cast<int>(frontiers.size()))
	      throw std::runtime_error("non-terminal index exceeds frontier size");
	    
	    node_set_type& node_set = node_graph_tree[frontiers[non_terminal_index]];
	    std::pair<node_set_type::iterator, bool> result = node_set.insert(std::make_pair(aiter->label.non_terminal(), 0));
	    if (result.second)
	      result.first->second = graph.add_node().id;
	    
	    if (is_fallback)
	      node_graph_glue[frontiers[non_terminal_index]].insert(*result.first);
	    
	    // transform into frontier of the translational forest
	    tails.push_back(result.first->second);
	    
	    node_prev = tails.back();
	    relative_pos = 0;
	  } else {
	    const hypergraph_type::id_type edge_id = construct_graph(*aiter,
								     hypergraph_type::invalid,
								     frontiers,
								     graph,
								     non_terminal_pos,
								     node_prev,
								     relative_pos,
								     is_fallback);
	    const hypergraph_type::id_type node_id = graph.edges[edge_id].head;
	    
	    tails.push_back(node_id);
	  }
	  
	  rhs.push_back(aiter->label.non_terminal());
	} else {
	  rhs.push_back(aiter->label);
	}
      
      hypergraph_type::id_type edge_id;
     
      if (root == hypergraph_type::invalid) {
	// we will share internal nodes
	
	if (! tails.empty()) {
	  internal_tail_set_type::iterator   titer = tail_map.insert(tail_set_type(tails.begin(), tails.end())).first;
	  internal_symbol_set_type::iterator siter = symbol_map.insert(symbol_set_type(rhs.begin(), rhs.end())).first;
	  
	  typedef std::pair<internal_label_map_type::iterator, bool> result_type;
	  
	  result_type result = label_map.insert(std::make_pair(internal_label_type(titer - tail_map.begin(),
										   siter - symbol_map.begin(),
										   rule.label), 0));
	  if (result.second) {
	    edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	    root = graph.add_node().id;
	    
	    graph.edges[edge_id].rule = construct_rule(rule_type(rule.label, rhs.begin(), rhs.end()));
	    graph.connect_edge(edge_id, root);
	    
	    result.first->second = edge_id;

	    tails_type::const_iterator titer_end = tails.end();
	    for (tails_type::const_iterator titer = tails.begin(); titer != titer_end; ++ titer) {
	      if (*titer >= connected.size())
		connected.resize(*titer + 1, false);
	      
	      connected[*titer] = true;
	    }
	  } else {
	    edge_id = result.first->second;
	    root = graph.edges[edge_id].head;
	  }
	} else {
	  internal_symbol_set_type::iterator siter = symbol_map_terminal.insert(symbol_set_type(rhs.begin(), rhs.end())).first;
	  
	  if (node_prev != hypergraph_type::invalid && node_prev >= terminal_map_global.size())
	    terminal_map_global.resize(node_prev + 1);
	  
	  terminal_label_map_type& terminal_map = (node_prev == hypergraph_type::invalid
						   ? terminal_map_local
						   : terminal_map_global[node_prev]);
	  
	  typedef std::pair<terminal_label_map_type::iterator, bool> result_type;
	  
	  result_type result = terminal_map.insert(std::make_pair(terminal_label_type(relative_pos ++,
										      siter - symbol_map_terminal.begin(),
										      rule.label), 0));
	  
	  if (result.second) {
	    edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	    root = graph.add_node().id;
	    
	    graph.edges[edge_id].rule = construct_rule(rule_type(rule.label, rhs.begin(), rhs.end()));
	    graph.connect_edge(edge_id, root);
	    
	    result.first->second = edge_id;
	  } else {
	    edge_id = result.first->second;
	    root = graph.edges[edge_id].head;
	  }
	}
      } else {
	edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	
	graph.edges[edge_id].rule = construct_rule(rule_type(rule.label, rhs.begin(), rhs.end()));
	graph.connect_edge(edge_id, root);

	tails_type::const_iterator titer_end = tails.end();
	for (tails_type::const_iterator titer = tails.begin(); titer != titer_end; ++ titer) {
	  if (*titer >= connected.size())
	    connected.resize(*titer + 1, false);
	  
	  connected[*titer] = true;
	}
      }
      
      return edge_id;
    }

    rule_ptr_type construct_rule(const rule_type& rule)
    {
      rule_cache_type::iterator riter = rule_cache.find(&rule);
      if (riter == rule_cache.end()) {
	const rule_ptr_type rule_ptr(rule_type::create(rule));
	
	riter = rule_cache.insert(std::make_pair(rule_ptr.get(), rule_ptr)).first;
      }

      return riter->second;
    }
    
    attribute_set_type frontier_attributes(const rule_ptr_type& rule_source, const rule_ptr_type& rule_target)
    {
      attribute_set_type attributes;
      
      if (rule_source) {
	frontier_set_type::iterator siter = frontiers_source.find(rule_source);
	if (siter == frontiers_source.end()) {
	  std::ostringstream os;
	  os << rule_source->rhs;
	  
	  siter = frontiers_source.insert(std::make_pair(rule_source, os.str())).first;
	}
	
	attributes[attr_frontier_source] = siter->second;
      }

      if (rule_target) {
	frontier_set_type::iterator titer = frontiers_target.find(rule_target);
	if (titer == frontiers_target.end()) {
	  std::ostringstream os;
	  os << rule_target->rhs;
	  
	  titer = frontiers_target.insert(std::make_pair(rule_target, os.str())).first;
	}
	
	attributes[attr_frontier_target] = titer->second;
      }
      
      return attributes;
    }

    struct FrontierIterator
    {
      FrontierIterator(std::string& __buffer) : buffer(__buffer) {}
      
      FrontierIterator& operator=(const std::string& value)
      {
	if (! buffer.empty())
	  buffer += ' ';
	buffer += value;
	return *this;
      }
      
      FrontierIterator& operator*() { return *this; }
      FrontierIterator& operator++() { return *this; }
      
      std::string& buffer;
    };
    
    attribute_set_type frontier_attributes(const tree_rule_ptr_type& rule_source, const tree_rule_ptr_type& rule_target)
    {
      attribute_set_type attributes;
      
      if (rule_source) {
	tree_frontier_set_type::iterator siter = tree_frontiers_source.find(rule_source);
	if (siter == tree_frontiers_source.end()) {
	  std::string frontier;
	  rule_source->frontier(FrontierIterator(frontier));
	  
	  siter = tree_frontiers_source.insert(std::make_pair(rule_source, frontier)).first;
	}
	
	attributes[attr_frontier_source] = siter->second;
      }

      if (rule_target) {
	tree_frontier_set_type::iterator titer = tree_frontiers_target.find(rule_target);
	if (titer == tree_frontiers_target.end()) {
	  std::string frontier;
	  rule_target->frontier(FrontierIterator(frontier));
	  
	  titer = tree_frontiers_target.insert(std::make_pair(rule_target, frontier)).first;
	}
	
	attributes[attr_frontier_target] = titer->second;
      }
      
      return attributes;
    }

    struct __tree_fallback : public boost::static_visitor<bool>
    {
      bool operator()(const attribute_set_type::int_type& x) const { return x; }
      template <typename Tp>
      bool operator()(const Tp& x) const { return false; }
    };

    bool is_tree_fallback(const attribute_set_type& attrs) const
    {
      attribute_set_type::const_iterator aiter = attrs.find(attr_tree_fallback);
      
      return aiter != attrs.end() && boost::apply_visitor(__tree_fallback(), aiter->second);
    }

  private:
    const symbol_type goal;
    const tree_grammar_type& tree_grammar;
    const grammar_type& grammar;
    const bool yield_source;
    const bool frontier_attribute;
    const bool unique_goal;
    
    const attribute_type attr_internal_node;
    const attribute_type attr_span_first;
    const attribute_type attr_span_last;
    const attribute_type attr_tree_fallback;
    const attribute_type attr_glue_tree;
    const attribute_type attr_glue_tree_fallback;
    const attribute_type attr_frontier_source;
    const attribute_type attr_frontier_target;
    
    rule_ptr_type goal_rule;
    
    active_tree_chart_set_type actives_tree;
    active_rule_chart_set_type actives_rule;
    passive_chart_type         passives;

    connected_type connected;
    
    closure_level_type    closure;
    closure_type          closure_head;
    closure_type          closure_tail;
    
    node_map_type         node_map;
    node_graph_type       node_graph_tree;
    node_graph_type       node_graph_rule;
    node_graph_type       node_graph_glue;
    non_terminal_set_type non_terminals;

    internal_tail_set_type   tail_map;
    internal_symbol_set_type symbol_map;
    internal_symbol_set_type symbol_map_terminal;
    internal_label_map_type  label_map;
    
    terminal_label_map_type     terminal_map_local;
    terminal_label_map_set_type terminal_map_global;

    rule_cache_type rule_cache;

    frontier_set_type frontiers_source;
    frontier_set_type frontiers_target;

    tree_frontier_set_type tree_frontiers_source;
    tree_frontier_set_type tree_frontiers_target;
  };
  
  
  inline
  void compose_tree_cky(const Symbol& goal, const TreeGrammar& tree_grammar, const Grammar& grammar, const Lattice& lattice, HyperGraph& graph, const bool yield_source=false, const bool frontier=false, const bool unique_goal=false)
  {
    ComposeTreeCKY __composer(goal, tree_grammar, grammar, yield_source, frontier, unique_goal);
    __composer(lattice, graph);
  }
};

#endif
