// -*- mode: c++ -*-
//
//  Copyright(C) 2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__PARSE_COARSE__HPP__
#define __CICADA__PARSE_COARSE__HPP__ 1

#include <vector>
#include <algorithm>
#include <set>

#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>
#include <cicada/lattice.hpp>
#include <cicada/grammar.hpp>
#include <cicada/transducer.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/semiring.hpp>
#include <cicada/span_vector.hpp>

#include <utils/chunk_vector.hpp>
#include <utils/chart.hpp>
#include <utils/hashmurmur.hpp>
#include <utils/sgi_hash_map.hpp>
#include <utils/sgi_hash_set.hpp>
#include <utils/b_heap.hpp>
#include <utils/std_heap.hpp>
#include <utils/bithack.hpp>
#include <utils/array_power2.hpp>

#include <google/dense_hash_map>
#include <google/dense_hash_set>

namespace cicada
{
  // coarse-to-fine parsing
  // input is a set of grammars, or use iterators
  // 
  // vector<grammar_type> grammar_set_type;
  //
  // Actually, we need different implementation....
  // How to handle this in the same framework.... separate this into different project...?
  //
  
  template <typename Semiring, typename Function>
  struct ParseCoarse
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

    typedef Semiring semiring_type;
    typedef Semiring score_type;
    
    typedef Function function_type;
    
    typedef std::vector<grammar_type, std::allocator<grammar_type> > grammar_set_type;
    typedef std::vector<double, std::allocator<double> > threshold_set_type;

    class LabelScoreSet : public google::dense_hash_map<symbol_type, score_type, boost::hash<symbol_type>, std::equal_to<symbol_type> >
    {
    public:
      typedef google::dense_hash_map<symbol_type, score_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > label_score_set_type;
      
      LabelScoreSet() : label_score_set_type() { label_score_set_type::set_empty_key(symbol_type()); }
    };
    typedef LabelScoreSet label_score_set_type;
    typedef utils::chart<label_score_set_type, std::allocator<label_score_set_type > > label_score_chart_type;

    struct CoarseSymbol
    {
      CoarseSymbol(const int __bits) : bits(__bits) {}
      
      symbol_type operator()(const symbol_type& symbol) const
      {
	if (! symbol.is_non_terminal()) return symbol;
	
	const size_t cache_pos = hash_value(symbol) & (caches.size() - 1);
	cache_type& cache = const_cast<cache_type&>(caches[cache_pos]);
	if (cache.symbol != symbol) {
	  cache.symbol = symbol;
	  cache.coarse = symbol.coarse(bits);
	}
	return cache.coarse;
      }
      
      struct Cache
      {
	symbol_type symbol;
	symbol_type coarse;
	
	Cache() : symbol(), coarse() {}
      };
      typedef Cache cache_type;
      typedef utils::array_power2<cache_type, 1024 * 4, std::allocator<cache_type> > cache_set_type;
      
      cache_set_type caches;
      int bits;
    };
    
    struct CoarseSimple
    {
      symbol_type operator()(const symbol_type& symbol) const
      {
	if (! symbol.is_non_terminal()) return symbol;
	
	return (symbol.binarized() ? "[x^]" : "[x]");
      }
    };
    
    template <typename Coarser>
    struct PruneCoarse
    {
      PruceCoarse(Coarser __coarser) : coarser(__coarser) {}

      bool operator()(const int first, const int last) const
      {
	return prunes(first, last).empty();
      }
      
      bool operator()(const int first, const int last, const symbol_type& label) const
      {
	if (prunes(first, last).empty()) return true;
	
	const label_score_set_type& labels = prunes(first, last);
	
	label_score_set_type::const_iterator liter = labels.find(label);
	if (liter != labels.end())
	  return liter->second < cutoff;
	
	label_score_set_type::const_iterator citer = labels.find(coarser(label));
	return (citr == labels.end() || citer->second < cutoff);
      }
      
      const label_score_chart_type& prunes;
      const score_type cutoff;
      Coarser coarser;
    };
    
    struct PruneNone
    {
      bool operator()(const int first, const int last) const
      {
	return false;
      }

      bool operator()(const int first, const int last, const symbol_type& label) const
      {
	return false;
      }
    };
    
    struct ParseCKY
    {
      //
      // CKY parser... but we will not a construct hypergraph, but a tabular structure...
      //
      // we keep two-passives, passives for keeping unary chain result (including zero-unary!)
      // and passives for keeping after non-unary...
      //
      
      typedef cicada::SpanVector::span_type span_type;
      typedef cicada::SpanVector::span_type tail_type;
      typedef utils::simple_vector<tail_type, std::allocator<tail_type> > tail_set_type;

      struct Edge
      {
	tail_set_type tails;
	score_type score;
	
	Edge() : tails(), score(cicada::semiring::traits<score_type>::one()) {}
	Edge(const tail_set_type& __tails, const score_type& __score) : tails(__tails), score(__score) {}
      };
      
      typedef Edge edge_type;
      typedef std::vector<edge_type, std::allocator<edge_type> > edge_set_type;
      
      struct Active
      {
	Actice(const transducer_type::id_type& __node)
	  : node(__node), edge() {}
	Actice(const transducer_type::id_type& __node,
	       const edge_type& __edge)
	  : node(__node), edge(__edge) {}
	
	transducer_type::id_type node;
	edge_type edge;
      };
      
      struct Passive
      {
	Passive(const span_type& __span) : span(__span), edges() {}
	
	span_type     span;
	edge_set_type edges;
      };
      
      struct Unary
      {
	symbol_type label;
	score_type  score;
	
	Unary() : label(), score(cicada::semiring::traits<score_type>::one()) {}
	Unary(const symbol_type& __label, const score_type& __score)
	  : label(__label), score(__score) {}
	template <typename Label, typename Score>
	Unary(const std::pair<Label, Score>& x)
	  : label(x.first), score(x.second) {}
      };
      
      typedef Active  active_type;
      typedef Passive passive_type;
      typedef Unary   unary_type;
      
      typedef utils::chunk_vector<active_type, 4096 / sizeof(active_type), std::allocator<active_type> > active_set_type;
      
      typedef utils::chart<active_set_type, std::allocator<active_set_type> > active_chart_type;
      typedef std::vector<active_chart_type, std::allocator<active_chart_type> > active_chart_set_type;
      
      typedef utils::chunk_vector<passive_type, 4096 / sizeof(passive_type), std::allocator<passive_type> > passive_set_type;
      typedef utils::chart<passive_set_type, std::allocator<passive_set_type> > passive_chart_type;
      
      typedef google::dense_hash_map<symbol_type, int, boost::hash<symbol_type>, std::equal_to<symbol_type> > node_map_type;
      
      typedef std::vector<unary_type, std::allocator<unary_type> > unary_set_type;
#ifdef HAVE_TR1_UNORDERED_MAP
      typedef std::tr1::unordered_map<symbol_type, unary_set_type, boost::hash<symbol_type>, std::equal_to<symbol_type>,
				      std::allocator<std::pair<const symbol_type, unary_set_type> > > unary_map_type;
#else
      typedef sgi::hash_map<symbol_type, unary_set_type, boost::hash<symbol_type>, std::equal_to<symbol_type>,
			    std::allocator<std::pair<const symbol_type, unary_set_type> > > unary_map_type;
#endif
      typedef google::dense_hash_map<symbol_type, score_type, boost::hash<symbol_type>, std::equal_to<symbol_type> > closure_set_type;
      
      ParseCKY(const symbol_type& __goal,
	       const grammar_type& __grammar,
	       const bool __yield_source=false,
	       const bool __treebank=false,
	       const bool __pos_mode=false)
	: goal(__goal), grammar(__grammar), threshold(__threshold), yield_source(__yield_source), treebank(__treebank), pos_mode(__pos_mode)
      {
	node_map.set_empty_key(symbol_type());
      }
      
      template <typename Pruner>
      void operator()(const lattice_type& lattice, label_score_chart_type& scores, const Pruner& pruner)
      {
	if (lattice.empty())
	  return;
	
	scores.clear();
	actives.clear();
	passives.clear();
	passives_unary.clear();
	goal_node = hypergraph_type::invalid;

	unaries.clear();
	
	actives.resize(grammar.size(), active_chart_type(lattice.size() + 1));
	passives.resize(lattice.size() + 1);
	passives_unary.resize(lattice.size() + 1);
	scores.resize(lattice.size() + 1);
	

	compute_inside(lattice, pruner);
	compute_outside(lattice);
	compute_inside_outside(lattice, scores);
      }
      
      void compute_inside_outside(const lattice_type& lattice, label_score_chart_type& scores)
      {
	
	
      }
      
      void compute_outside(const lattice_type& lattice)
      {
	// traverse back passives from TOP
	
	// find goal node out of passives_unary.
	//
	// how do we traverse back this complicated structure....
	//
	
	
      }

      template <typename Pruner>
      void compute_inside(const lattice_type& lattice, const Pruner& pruner)
      {
	// initialize active chart...
	for (size_type table = 0; table != grammar.size(); ++ table) {
	  const transducer_type::id_type root = grammar[table].root();
	  
	  for (size_type pos = 0; pos != lattice.size(); ++ pos)
	    if (grammar[table].valid_span(pos, pos, 0))
	      actives[table](pos, pos), push_back(active_type(root));
	}
	
	// compute inside...
	for (size_type length = 1; length <= lattice.size(); ++ length)
	  for (size_type first = 0; first + length <= lattice.size(); ++ first) {
	    const size_type last = first + length;

	    // check pruning!
	    if (pruner(first, last)) continue;
	    
	    node_map.clear();
	    
	    for (size_t table = 0; table != grammar.size(); ++ table) {
	      const transducer_type& transducer = grammar[table];
	      
	      // we will advance active spans, but constrained by transducer's valid span
	      if (transducer.valid_span(first, last, lattice.shortest_distance(first, last))) {
		active_set_type& cell = actives[table](first, last);
		for (size_t middle = first + 1; middle < last; ++ middle) {
		  const active_set_type&  active_arcs  = actives[table](first, middle);
		  const passive_set_type& passive_arcs = passives_unary(middle, last);
		  
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
			    cell.push_back(active_type(aiter->node, edge_type(aiter->edge.tails, aiter->edge.score + function(piter->features))));
			} else {
			  for (active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter) {
			    const transducer_type::id_type node = transducer.next(aiter->node, terminal);
			    if (node == transducer.root()) continue;
			    
			    cell.push_back(active_type(node, edge_type(aiter->edge.tails, aiter->edge.score * function(piter->features))));
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
			    cell.push_back(active_type(aiter->node, edge_type(aiter->edge.tails, aiter->edge.score + function(piter->features))));
			} else {
			  for (active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter) {
			    const transducer_type::id_type node = transducer.next(aiter->node, terminal);
			    if (node == transducer.root()) continue;
			    
			    cell.push_back(active_type(node, edge_type(aiter->edge.tails, aiter->edge.score * function(piter->features))));
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
		  const rule_ptr_type rule = (yield_source ? riter->source : riter->target);
		  const symbol_type& lhs = rule->lhs;
		  
		  // check pruning!
		  if (pruner(first, last, lhs)) continue;
		  
		  std::pair<node_map_type::iterator, bool> result = node_map.insert(std::make_pair(lhs, passive_arcs.size()));
		  if (result.second)
		    passive_arcs.push_back(span_type(first, last, lhs));
		  
		  passive_arcs[result.first->second].edges.push_back(edge_type(citer->edge.tails, citer->edge.score * function(iter->features)));
		}
	      }
	    }
	    
	    if (! passives(first, last).empty()) {
	      // unary rules...
	      // we will cache unary rule application by caching closure...
	      // how to avoid cycles...?
	      //
	      // compute closure from the list of possives...
	      // we assume that the rules are maximum-likely estimated, meaning negative scores..
	      // 
	      // we will simply perform max-like computation...
	      
	      // the obvious unary chain is zero-rules...
	      // simply insert a single-tail edge, with log-score of zero (or prob of 1)
	      //

	      node_map.clear();

	      passive_set_type& passive_unary = passives_unary(first, last);
	      
	      passive_set_type::const_iterator piter_end = passives(first, last).end();
	      for (passive_set_type::const_iterator piter = passives(first, last).begin(); piter != piter_end; ++ piter) {
		// child to parent...
		const unary_set_type& closure = unary_closure(piter->span.label);
		
		unary_set_type::const_iterator citer_end = closure.end();
		for (unary_set_type::const_iterator citer = closure.begin(); citer != citer_end; ++ citer) {
		  // check pruning!
		  if (pruner(first, last, citer->label)) continue;
		  
		  std::pair<node_map_type::iterator, bool> result = node_map.insert(std::make_pair(citer->label, passive_unary.size()));
		  if (result.second) {
		    if (length == lattice.size() && citer->label == goal)
		      goal_node = passive_unary.size();
		    
		    passive_unary.push_back(span_type(first, last, citer->label));
		  }
		  
		  passive_unary[result.first->second].edges.push_back(edge_type(tail_set_type(1, piter->span), citer->score));
		}
	      }
	    }
	    
	    // extend root with passive items at [first, last)
	    for (size_t table = 0; table != grammar.size(); ++ table) {
	      const transducer_type& transducer = grammar[table];
	      
	      if (! transducer.valid_span(first, last, lattice.shortest_distance(first, last))) continue;
	      
	      const active_set_type&  active_arcs  = actives[table](first, first);
	      const passive_set_type& passive_arcs = passives_unary(first, last);
	      
	      active_set_type& cell = actives[table](first, last);
	      
	      extend_actives(transducer, active_arcs, passive_arcs, cell);
	    }
	  }
      }
      
      const unary_set_type& unary_closure(const symbol_type& child)
      {
	
	//
	// given this child state, compute closure...
	// we do not allow cycle, and keep only max-rules
	//

	unary_map_type::iterator uiter = unaries.find(child);
	if (uiter == unaries.end()) {
	  closure.clear();
	  closure_next.clear();
	  closure.insert(std::make_pair(child, cicada::semiring::traits<score_type>::one()));
	  
	  for (;;) {
	    bool equilibrate = true;

	    closure_next = closure;
	    
	    closure_set_type::const_iterator citer_end = closure.end();
	    for (closure_set_type::const_iterator citer = closure.begin(); citer != citer_end; ++ citer) {
	      for (size_type table = 0; table != grammar.size(); ++ table) {
		const transducer_type& transducer = grammar[table];
		
		const transducer_type::id_type node = transducer.next(transducer.root(), citer->first);
		if (node == transducer.root()) continue;
		
		const transducer_type::rule_pair_set_type& rules = transducer.rules(node);
		
		if (rules.empty()) continue;
		
		transducer_type::rule_pair_set_type::const_iterator riter_end = rules.end();
		for (transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter) {
		  const rule_ptr_type rule = (yield_source ? riter->source : riter->target);
		  const symbol_type& lhs = rule->lhs;
		  
		  // we assume max-like estimate grammar!
		  if (lhs == child) continue;
		  
		  const score_type score = function(riter->features) * citer->second;
		  
		  std::pair<closure_set_type::iterator, bool> result = closure_next.insert(std::make_pair(lhs, score));
		  if (result.second)
		    equilibrate = false;
		  else if (result.first->second < score) {
		    equilibrate = false;
		    result.first->second = score;
		  }
		}
	      }
	    }
	    
	    closure.swap(closure_next);
	    closure_next.clear();
	    
	    if (equilibrate) break;
	  }
	  
	  uiter = unaries.insert(std::make_pair(child, unary_set_type(closure.begin(), closure.end()))).second;
	}
	return uiter->second;
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
	      
	      tail_set_type tails(aiter->edge.tails.size() + 1);
	      std::copy(aiter->edge.tails.begin(), aiter->edge.tails.end(), tails.begin());
	      
	      for (passive_set_type::const_iterator piter = piter_begin; piter != piter_end; ++ piter) {
		const symbol_type& non_terminal = piter->span.label;
		
		if (label != non_terminal) {
		  node = transducer.next(aiter->node, non_terminal);
		  label = non_terminal;
		}
		if (node == transducer.root()) continue;
		
		tails.back() = piter->span;
		cell.push_back(active_type(node, edge_type(tails, aiter->edge.score)));
		
		found = true;
	      }
	    }
      
	return found;
      }
      
      const symbol_type goal;
      const grammar_type& grammar;
      const bool yield_source;
      const bool treebank;
      const bool pos_mode;

      node_map_type node_map;
    };
    
    template <typename IteratorGrammar, typename IteratorThreshold>
    ParseCoarse(const symbol_type& __goal,
		IteratorGrammar gfirst, IteratorGrammar glast,
		IteratorThreshold tfirst, IteratorThreshold tlast,
		const function_type& __function,
		const bool __yield_source=false,
		const bool __treebank=false,
		const bool __pos_mode=false)
      : goal(__goal),
	grammars(gfirst, glast),
	thresholds(tfirst, tlast),
	function(__function),
	yield_source(__yield_source),
	treebank(__treebank),
	pos_mode(__pos_mode),
	attr_span_first("span-first"),
	attr_span_last("span-last")
    {
      if (grammars.empty())
	throw std::runtime_error("no grammar?");
      if (thresholds.size() + 1 != grammar.size())
	throw std::runtime_error("do we have enough threshold parameters for grammars?");
    }
    
    void operator()(const lattice_type& lattice,
		    hypergraph_type& graph)
    {
      graph.clear();
      
      if (lattice.empty()) return;
      
      
      
    }
    
  private:
    const symbol_type goal;
    grammar_set_type   grammars;
    threshold_set_type thresholds;
    
    const function_type& function;

    const bool yield_source;
    const bool treebank;
    const bool pos_mode;
    const attribute_type attr_span_first;
    const attribute_type attr_span_last;
  };
  
  
  
  
};

#endif
