// -*- mode: c++ -*-
//
//  Copyright(C) 2011-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__PARSE_COARSE__HPP__
#define __CICADA__PARSE_COARSE__HPP__ 1

#include <vector>
#include <algorithm>

#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>
#include <cicada/lattice.hpp>
#include <cicada/grammar.hpp>
#include <cicada/transducer.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/semiring.hpp>
#include <cicada/compose_cky.hpp>
#include <cicada/parse_cky.hpp>

#include <utils/chunk_vector.hpp>
#include <utils/chart.hpp>
#include <utils/bithack.hpp>
#include <utils/array_power2.hpp>
#include <utils/indexed_set.hpp>
#include <utils/mulvector2.hpp>
#include <utils/unordered_map.hpp>
#include <utils/simple_vector.hpp>
#include <utils/small_vector.hpp>
#include <utils/compact_map.hpp>

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

    typedef utils::compact_map<symbol_type, score_type,
			       utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
			       boost::hash<symbol_type>, std::equal_to<symbol_type>,
			       std::allocator<std::pair<const symbol_type, score_type> > > label_score_set_type;

    typedef utils::chart<label_score_set_type, std::allocator<label_score_set_type > > label_score_chart_type;

    struct CoarseSymbol
    {
      CoarseSymbol(const int __bits) : bits(__bits) {}
      
      symbol_type operator()(const symbol_type& symbol) const
      {
	return symbol.coarse(bits);
      }
      int bits;
    };
    
    struct CoarseSimple
    {
      symbol_type operator()(const symbol_type& symbol) const
      {
	if (! symbol.is_non_terminal()) return symbol;
	
	return (symbol.binarized() ? binarized : non_binarized);
      }
      
      CoarseSimple() : binarized("[x^]"), non_binarized("[x]") {}
      
      const symbol_type binarized;
      const symbol_type non_binarized;
    };
    
    template <typename Coarser>
    struct PruneCoarse
    {
      PruneCoarse(const label_score_chart_type& __prunes,
		  const score_type& __cutoff,
		  Coarser __coarser)
	: prunes(__prunes),
	  cutoff(__cutoff),
	  coarser(__coarser) {}

      bool operator()(const int first, const int last) const
      {
	const label_score_set_type& labels = prunes(first, last);

	if (labels.empty()) return true;
	
	bool no_prune = false;
	typename label_score_set_type::const_iterator liter_end = labels.end();
	for (typename label_score_set_type::const_iterator liter = labels.begin(); liter != liter_end; ++ liter)
	  no_prune |= liter->second >= cutoff;
	
	return ! no_prune;
      }
      
      bool operator()(const int first, const int last, const symbol_type& label) const
      {
	const label_score_set_type& labels = prunes(first, last);
	
	if (labels.empty()) return true;
	
	typename label_score_set_type::const_iterator liter = labels.find(label);
	if (liter != labels.end())
	  return liter->second < cutoff;
	
	typename label_score_set_type::const_iterator citer = labels.find(coarser(label));
	return (citer == labels.end() || citer->second < cutoff);
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

      typedef int id_type;

      struct Span
      {
	int     first;
	int     last;
	id_type id;
	
	Span() : first(0), last(0), id(0) {}
	Span(const int& __first, const int& __last, const id_type& __id) : first(__first), last(__last), id(__id) {}
      };
      
      typedef Span span_type;
      typedef Span tail_type;
      typedef utils::simple_vector<tail_type, std::allocator<tail_type> > tails_type;
      typedef utils::mulvector2<tail_type, std::allocator<tail_type> >    tails_map_type;
      typedef typename tails_map_type::const_reference tails_mapped_type;
      typedef size_type tails_id_type;
      
      typedef utils::small_vector<id_type, std::allocator<id_type> > closure_type;
      typedef utils::mulvector2<id_type, std::allocator<id_type> > closure_map_type;
      typedef typename closure_map_type::const_reference closure_mapped_type;
      typedef size_type closure_id_type;
      
      struct Edge
      {
	tails_id_type tails;
	score_type   score;
	
	Edge() : tails(0), score() {}
	Edge(const score_type& __score) : tails(0), score(__score) {}
	Edge(const tails_id_type& __tails, const score_type& __score) : tails(__tails), score(__score) {}
      };

      struct UnaryEdge
      {
	id_type tail;
	closure_id_type closure;
	score_type score;
	
	UnaryEdge() : tail(id_type(-1)), closure(0), score() {}
	UnaryEdge(const id_type& __tail, const closure_id_type& __closure, const score_type& __score) : tail(__tail), closure(__closure), score(__score) {}
      };
      
      typedef Edge edge_type;
      typedef UnaryEdge unary_edge_type;
      
      typedef std::vector<edge_type, std::allocator<edge_type> > edge_set_type;
      typedef std::vector<unary_edge_type, std::allocator<unary_edge_type> > unary_edge_set_type;
      
      struct Active
      {
	Active(const transducer_type::id_type& __node,
	       const edge_type& __edge)
	  : node(__node), edge(__edge) {}
	
	transducer_type::id_type node;
	edge_type edge;
      };
      
      struct Passive
      {
	Passive() : edges() {}
	
	edge_set_type edges;
      };

      struct PassiveUnary
      {
	PassiveUnary() : edges() {}
	
	unary_edge_set_type edges;
      };
      
      struct Unary
      {
	id_type         id;
	score_type      score;
	closure_id_type closure;
	
	Unary() : id(), score(), closure(0) {}
	Unary(const id_type& __id, const score_type& __score, const closure_id_type& __closure)
	  : id(__id), score(__score), closure(__closure) {}
      };
      
      typedef Active       active_type;
      typedef Passive      passive_type;
      typedef PassiveUnary passive_unary_type;
      typedef Unary        unary_type;
      
      typedef std::vector<active_type, std::allocator<active_type> > active_set_type;
      
      typedef utils::chart<active_set_type, std::allocator<active_set_type> > active_chart_type;
      typedef std::vector<active_chart_type, std::allocator<active_chart_type> > active_chart_set_type;
      
      typedef std::vector<passive_type, std::allocator<passive_type> > passive_set_type;
      typedef utils::chart<passive_set_type, std::allocator<passive_set_type> > passive_chart_type;
      
      typedef std::vector<passive_unary_type, std::allocator<passive_unary_type> > passive_unary_set_type;
      typedef utils::chart<passive_unary_set_type, std::allocator<passive_unary_set_type> > passive_unary_chart_type;
            
      typedef std::vector<unary_type, std::allocator<unary_type> > unary_set_type;
      typedef std::deque<unary_set_type, std::allocator<unary_set_type> > unary_map_type;
      typedef std::vector<bool, std::allocator<bool> > unary_computed_type;
      
      
      struct closure_score_type
      {
	closure_type closure;
	score_type   score;
	
	closure_score_type() : closure(), score() {}
	closure_score_type(const closure_type& __closure, const score_type& __score) : closure(__closure), score(__score) {}
      };
      typedef typename utils::unordered_map<id_type, closure_score_type, boost::hash<id_type>, std::equal_to<id_type>,
					    std::allocator<std::pair<const id_type, closure_score_type> > >::type closure_set_type;
      
      typedef utils::indexed_set<symbol_type, boost::hash<symbol_type>, std::equal_to<symbol_type>, std::allocator<symbol_type> > symbol_map_type;
      
      struct InsideOutsideScore
      {
	score_type inside;
	score_type outside;
	
	score_type final_inside;
	score_type final_outside;

	score_type score;
	
	InsideOutsideScore() : inside(), outside(), final_inside(), final_outside(), score() {}
      };
      typedef InsideOutsideScore score_pair_type;
      typedef std::vector<score_pair_type, std::allocator<score_pair_type> > score_pair_set_type;
      typedef utils::chart<score_pair_set_type, std::allocator<score_pair_set_type> > score_pair_chart_type;
      
    public:
      ParseCKY(const symbol_type& __goal,
	       const grammar_type& __grammar,
	       const function_type& __function,
	       const bool __yield_source=false,
	       const bool __treebank=false,
	       const bool __pos_mode=false,
	       const bool __ordered=false,
	       const bool __frontier=false)
	: goal(__goal), grammar(__grammar), function(__function), yield_source(__yield_source), treebank(__treebank), pos_mode(__pos_mode), ordered(__ordered), frontier(__frontier)
      {
	//closure.set_empty_key(id_type(-1));
	//closure_next.set_empty_key(id_type(-1));
      }

    public:      
      
      template <typename Pruner>
      bool operator()(const lattice_type& lattice, label_score_chart_type& scores, const Pruner& pruner)
      {
	inside_outside.clear();
	scores.clear();
	
	inside_outside.reserve(lattice.size() + 1);
	scores.reserve(lattice.size() + 1);
	
	inside_outside.resize(lattice.size() + 1);
	scores.resize(lattice.size() + 1);
	
	actives.clear();
	passives.clear();
	passives_unary.clear();
	
	actives.reserve(grammar.size());
	passives.reserve(lattice.size() + 1);
	passives_unary.reserve(lattice.size() + 1);
	
	actives.resize(grammar.size(), active_chart_type(lattice.size() + 1));
	passives.resize(lattice.size() + 1);
	passives_unary.resize(lattice.size() + 1);

	tails_map.clear();
	tails_map.push_back();
	
	// symbol_map.clear();
	// closure_map.clear();
	// unaries.clear();
	// unaries_computed.clear();
	
	//symbol_map_type(symbol_map).swap(symbol_map);
	//closure_map_type(closure_map).swap(closure_map);
	
	goal_id = id_map(goal);
	
	compute_inside(lattice, pruner);

	actives.clear();
	active_chart_set_type(actives).swap(actives);
	
	const bool has_goal = (goal_id < static_cast<id_type>(inside_outside(0, lattice.size()).size())
			       && inside_outside(0, lattice.size())[goal_id].final_inside != cicada::semiring::traits<score_type>::zero());

	//std::cerr << "goal? " << has_goal << std::endl;
	
	if (has_goal) {
	  compute_outside(lattice);
	  compute_inside_outside(lattice, scores);
	}
	
	inside_outside.clear();
	score_pair_chart_type(inside_outside).swap(inside_outside);
	
	passives.clear();
	passives_unary.clear();
	
	passive_chart_type(passives).swap(passives);
	passive_unary_chart_type(passives_unary).swap(passives_unary);
	
	return has_goal;
      }
      
    private:
      void compute_inside_outside(const lattice_type& lattice, label_score_chart_type& scores)
      {
	const score_type score_sum = inside_outside(0, lattice.size())[goal_id].final_inside;
	
	// we simply enumerate chart...!
	for (size_type length = 1; length <= lattice.size(); ++ length)
	  for (size_type first = 0; first + length <= lattice.size(); ++ first) {
	    const size_type last = first + length;
	    
	    if (inside_outside(first, last).empty()) continue;
	    
	    label_score_set_type& labels_scores = scores(first, last);
	    const score_pair_set_type& inside_outside_scores   = inside_outside(first, last);

	    //std::cerr << "span: " << first << ".." << last << std::endl;
	    
	    for (id_type id = 0; id != static_cast<id_type>(inside_outside_scores.size()); ++ id) {
	      //const score_type score = std::max(inside_outside_scores[id].final_inside * inside_outside_scores[id].final_outside,
	      //                                  inside_outside_scores[id].inside * inside_outside_scores[id].outside);
	      const score_type& score = inside_outside_scores[id].score;
	      
	      //std::cerr << "\tid: " << id << " score: " << score << std::endl;
	      
	      if (score != cicada::semiring::traits<score_type>::zero())
		labels_scores[symbol_map[id]] = score / score_sum;
	    }
	  }
      }
      
      void compute_outside(const lattice_type& lattice)
      {
	// traverse back passives from TOP
	
	// find goal node out of passives_unary.
	//
	// how do we traverse back this complicated structure....
	//
	
	inside_outside(0, lattice.size())[goal_id].final_outside = cicada::semiring::traits<score_type>::one();
	
	for (size_type length = lattice.size(); length != 0; -- length)
	  for (size_type first = 0; first + length <= lattice.size(); ++ first) {
	    const size_type last = first + length;
	    
	    score_pair_set_type& scores_outside = inside_outside(first, last);
	    
	    if (scores_outside.empty()) continue;
	    
	    // first, enumerate unary rules
	    const passive_unary_set_type& unaries = passives_unary(first, last);
	    for (id_type id = 0; id != static_cast<id_type>(unaries.size()); ++ id) 
	      if (! unaries[id].edges.empty()) {
		const score_type score_head = scores_outside[id].final_outside;
		
		if (score_head == cicada::semiring::traits<score_type>::zero()) continue;
		
		typename unary_edge_set_type::const_iterator eiter_end = unaries[id].edges.end();
		for (typename unary_edge_set_type::const_iterator eiter = unaries[id].edges.begin(); eiter != eiter_end; ++ eiter) {
		  const unary_edge_type& edge = *eiter;
		  
		  score_type& outside = scores_outside[edge.tail].outside;
		  outside = std::max(outside, score_head * edge.score);
		  
		  const score_type score = score_head * edge.score * scores_outside[edge.tail].inside;
		  
		  const closure_mapped_type closure = closure_map[edge.closure];
		  typename closure_mapped_type::const_iterator citer_end = closure.end();
		  for (typename closure_mapped_type::const_iterator citer = closure.begin(); citer != citer_end; ++ citer) {
		    score_type& score_update = scores_outside[*citer].score;
		    
		    score_update = std::max(score_update, score);
		  }
		  
		  //std::cerr << "closure size: " << closure.size() << std::endl;
		}
	      }
	    
	    // second, enumerate non-unary rules
	    const passive_set_type& rules = passives(first, last);
	    for (id_type id = 0; id != static_cast<id_type>(rules.size()); ++ id) 
	      if (! rules[id].edges.empty()) {
		const score_type score_head = scores_outside[id].outside;
		
		if (score_head == cicada::semiring::traits<score_type>::zero()) continue;
		
		typename edge_set_type::const_iterator eiter_end = rules[id].edges.end();
		for (typename edge_set_type::const_iterator eiter = rules[id].edges.begin(); eiter != eiter_end; ++ eiter) {
		  const edge_type& edge = *eiter;
		  
		  const score_type score_edge = score_head * edge.score;
		  
		  const tails_mapped_type tails = tails_map[edge.tails];
		  
		  typename tails_mapped_type::const_iterator titer_end = tails.end();
		  for (typename tails_mapped_type::const_iterator titer = tails.begin(); titer != titer_end; ++ titer) {
		    score_type score_outside = score_edge;
		    for (typename tails_mapped_type::const_iterator niter = tails.begin(); niter != titer_end; ++ niter)
		      if (titer != niter)
			score_outside *= inside_outside(niter->first, niter->last)[niter->id].final_inside;
		    
		    score_type& score = inside_outside(titer->first, titer->last)[titer->id].final_outside;
		    score = std::max(score, score_outside);
		  }
		}
	    }
	  }
      }
      
      template <typename Pruner>
      void compute_inside(const lattice_type& lattice, const Pruner& pruner)
      {
	// initialize active chart...
	for (size_type table = 0; table != grammar.size(); ++ table) {
	  const transducer_type::id_type root = grammar[table].root();
	  
	  for (size_type pos = 0; pos != lattice.size(); ++ pos)
	    if (grammar[table].valid_span(pos, pos, 0))
	      actives[table](pos, pos).push_back(active_type(root, edge_type(cicada::semiring::traits<score_type>::one())));
	}
	
	// compute inside...
	for (size_type length = 1; length <= lattice.size(); ++ length)
	  for (size_type first = 0; first + length <= lattice.size(); ++ first) {
	    const size_type last = first + length;

	    // check pruning!
	    if (pruner(first, last)) continue;

	    //std::cerr << "span: " << first << ".." << last << std::endl;

	    score_pair_set_type& scores_inside = inside_outside(first, last);

	    scores_inside.reserve(symbol_map.size());
	    passives(first, last).reserve(symbol_map.size());
	    
	    for (size_t table = 0; table != grammar.size(); ++ table) {
	      const transducer_type& transducer = grammar[table];
	      
	      // we will advance active spans, but constrained by transducer's valid span
	      if (transducer.valid_span(first, last, lattice.shortest_distance(first, last))) {
		active_set_type& cell = actives[table](first, last);
		for (size_t middle = first + 1; middle < last; ++ middle) {
		  const active_set_type&  active_arcs  = actives[table](first, middle);
		  const passive_unary_set_type& passive_arcs = passives_unary(middle, last);
		  
		  extend_actives(transducer, active_arcs, passive_arcs, middle, last, cell);
		}
		
		if (! treebank || length == 1) {
		  // then, advance by terminal(s) at lattice[last - 1];
		  
		  const active_set_type&  active_arcs  = actives[table](first, last - 1);
		  const lattice_type::arc_set_type& passive_arcs = lattice[last - 1];
		  
		  typename active_set_type::const_iterator aiter_begin = active_arcs.begin();
		  typename active_set_type::const_iterator aiter_end = active_arcs.end();
		  
		  if (aiter_begin != aiter_end) {
		    if (pos_mode) {
		      lattice_type::arc_set_type::const_iterator piter_end = passive_arcs.end();
		      for (lattice_type::arc_set_type::const_iterator piter = passive_arcs.begin(); piter != piter_end; ++ piter) {
			const symbol_type terminal = piter->label.terminal();
			const score_type score_arc = function(piter->features);
			
			active_set_type& cell = actives[table](first, last - 1 + piter->distance);
			
			// handling of EPSILON rule...
			if (terminal == vocab_type::EPSILON) {
			  for (typename active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
			    cell.push_back(active_type(aiter->node, edge_type(aiter->edge.tails, aiter->edge.score * score_arc)));
			} else {
			  for (typename active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter) {
			    const transducer_type::id_type node = transducer.next(aiter->node, terminal);
			    if (node == transducer.root()) continue;
			    
			    cell.push_back(active_type(node, edge_type(aiter->edge.tails, aiter->edge.score * score_arc)));
			  }
			}
		      }
		    } else {
		      lattice_type::arc_set_type::const_iterator piter_end = passive_arcs.end();
		      for (lattice_type::arc_set_type::const_iterator piter = passive_arcs.begin(); piter != piter_end; ++ piter) {
			const symbol_type& terminal = piter->label;
			const score_type score_arc = function(piter->features);
			
			active_set_type& cell = actives[table](first, last - 1 + piter->distance);
			
			// handling of EPSILON rule...
			if (terminal == vocab_type::EPSILON) {
			  for (typename active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
			    cell.push_back(active_type(aiter->node, edge_type(aiter->edge.tails, aiter->edge.score * score_arc)));
			} else {
			  for (typename active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter) {
			    const transducer_type::id_type node = transducer.next(aiter->node, terminal);
			    if (node == transducer.root()) continue;
			    
			    cell.push_back(active_type(node, edge_type(aiter->edge.tails, aiter->edge.score * score_arc)));
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
	      
	      active_set_type&     cell          = actives[table](first, last);
	      passive_set_type&    passive_arcs  = passives(first, last);
	      
	      typename active_set_type::const_iterator citer_end = cell.end();
	      for (typename active_set_type::const_iterator citer = cell.begin(); citer != citer_end; ++ citer) {
		const transducer_type::rule_pair_set_type& rules = transducer.rules(citer->node);
		
		if (rules.empty()) continue;
		
		score_type score_tails = cicada::semiring::traits<score_type>::one();
		const tails_mapped_type tails = tails_map[citer->edge.tails];

		typename tails_mapped_type::const_iterator titer_end = tails.end();
		for (typename tails_mapped_type::const_iterator titer = tails.begin(); titer != titer_end; ++ titer)
		  score_tails *= inside_outside(titer->first, titer->last)[titer->id].final_inside;
		
		transducer_type::rule_pair_set_type::const_iterator riter_begin = rules.begin();
		transducer_type::rule_pair_set_type::const_iterator riter_end   = rules.end();
		
		for (transducer_type::rule_pair_set_type::const_iterator riter = riter_begin; riter != riter_end; ++ riter) {
		  const rule_ptr_type rule = (yield_source ? riter->source : riter->target);
		  
		  if (pruner(first, last, rule->lhs)) continue;
		  
		  const id_type lhs = id_map(rule->lhs);
		  
		  const score_type score_edge = citer->edge.score * function(riter->features);
		  
		  if (lhs >= static_cast<id_type>(passive_arcs.size()))
		    passive_arcs.resize(lhs + 1);
		  if (lhs >= static_cast<id_type>(scores_inside.size()))
		    scores_inside.resize(lhs + 1);
		  
		  passive_arcs[lhs].edges.push_back(edge_type(citer->edge.tails, score_edge));
		  scores_inside[lhs].inside = std::max(scores_inside[lhs].inside, score_tails * score_edge);
		}
	      }
	    }

	    //passive_set_type(passives(first, last)).swap(passives(first, last));
	    
	    if (! passives(first, last).empty()) {
	      // unary rules...
	      const passive_set_type& passive = passives(first, last);
	      passive_unary_set_type& passive_unary = passives_unary(first, last);
	      
	      passive_unary.reserve(symbol_map.size());
	      
	      //id_type id_closure_max = 0;
	      
	      for (id_type id = 0; id != static_cast<id_type>(passive.size()); ++ id) 
		if (! passive[id].edges.empty()) {
		  // child to parent...
		  const unary_set_type& closure = unary_closure(id);
		  const score_type score_tail = scores_inside[id].inside;
		  
		  if (closure.empty()) continue;
		  
		  if (closure.back().id >= static_cast<id_type>(passive_unary.size()))
		    passive_unary.resize(closure.back().id + 1);
		  if (closure.back().id >= static_cast<id_type>(scores_inside.size()))
		    scores_inside.resize(closure.back().id + 1);
		  
		  typename unary_set_type::const_iterator citer_end = closure.end();
		  for (typename unary_set_type::const_iterator citer = closure.begin(); citer != citer_end; ++ citer) {
		    // check pruning!
		    if (pruner(first, last, symbol_map[citer->id])) continue;
		    
		    //std::cerr << "head: " << citer->id << " tail: " << id << " score: " << citer->score << " tail: " << score_tail << std::endl;
		    
		    passive_unary[citer->id].edges.push_back(unary_edge_type(id, citer->closure, citer->score));
		    scores_inside[citer->id].final_inside = std::max(scores_inside[citer->id].final_inside, score_tail * citer->score);
		    
		    //id_closure_max = utils::bithack::max(id_closure_max, closure_map[citer->closure].back());
		  }
		}
	      
#if 0
	      if (id_closure_max >= static_cast<id_type>(scores_inside.size()))
		scores_inside.resize(id_closure_max + 1);
#endif
	      
	      //passive_unary_set_type(passive_unary).swap(passive_unary);
	    }

	    if (! passives_unary(first, last).empty()) {
	      
	      const passive_unary_set_type& passive_arcs = passives_unary(first, last);
	      
#if 0
	      std::cerr << "span: " << first << ".." << last << std::endl;
	      for (size_t id = 0; id != scores_inside.size(); ++ id)
		std::cerr << "\tid: " << id
			  << " " << scores_inside[id].final_inside
			  << " " << scores_inside[id].inside
			  << std::endl;
#endif
	      
	      // extend root with passive items at [first, last)
	      for (size_t table = 0; table != grammar.size(); ++ table) {
		const transducer_type& transducer = grammar[table];
		
		if (! transducer.valid_span(first, last, lattice.shortest_distance(first, last))) continue;
		
		const active_set_type&  active_arcs  = actives[table](first, first);
		
		active_set_type& cell = actives[table](first, last);
		
		extend_actives(transducer, active_arcs, passive_arcs, first, last, cell);
		
		//active_set_type(cell).swap(cell);
	      }
	    }
	    
	    //score_pair_set_type(scores_inside).swap(scores_inside);
	  }
      }
      
      template <typename Tp>
      struct less_id
      {
	bool operator()(const Tp& x, const Tp& y) const
	{
	  return x.id < y.id;
	}
      };
      
      const unary_set_type& unary_closure(const id_type& child)
      {
	//
	// given this child state, compute closure...
	// we do not allow cycle, and keep only max-rules
	//
	
	if (symbol_map.size() > unaries.size())
	  unaries.resize(symbol_map.size());
	if (symbol_map.size() > unaries_computed.size())
	  unaries_computed.resize(symbol_map.size(), false);
	
	if (! unaries_computed[child]) {
	  unaries_computed[child] = true;
	  
	  closure.clear();
	  closure_next.clear();
	  closure.insert(std::make_pair(child, closure_score_type(closure_type(1, child), cicada::semiring::traits<score_type>::one())));
	  
	  for (;;) {
	    bool equilibrate = true;
	    
	    closure_next = closure;
	    
	    typename closure_set_type::const_iterator citer_end = closure.end();
	    for (typename closure_set_type::const_iterator citer = closure.begin(); citer != citer_end; ++ citer) {
	      for (size_type table = 0; table != grammar.size(); ++ table) {
		const transducer_type& transducer = grammar[table];
		
		const transducer_type::id_type node = transducer.next(transducer.root(), symbol_map[citer->first]);
		if (node == transducer.root()) continue;
		
		const transducer_type::rule_pair_set_type& rules = transducer.rules(node);
		
		if (rules.empty()) continue;
		
		transducer_type::rule_pair_set_type::const_iterator riter_end = rules.end();
		for (transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter) {
		  const rule_ptr_type rule = (yield_source ? riter->source : riter->target);
		  const id_type lhs = id_map(rule->lhs);
		  
		  // we assume max-like estimate grammar!
		  if (lhs == child) continue;
		  
		  const score_type score = function(riter->features) * citer->second.score;
		  
		  std::pair<typename closure_set_type::iterator, bool> result = closure_next.insert(std::make_pair(lhs, closure_score_type(closure_type(), score)));
		  if (result.second) {
		    equilibrate = false;
		    result.first->second.closure = citer->second.closure;
		    result.first->second.closure.push_back(lhs);
		  } else if (result.first->second.score < score) {
		    equilibrate = false;
		    result.first->second.score = score;
		    result.first->second.closure = citer->second.closure;
		    result.first->second.closure.push_back(lhs);
		  }
		}
	      }
	    }
	    
	    closure.swap(closure_next);
	    closure_next.clear();
	    
	    if (equilibrate) break;
	  }
	  
	  unaries[child].clear();
	  unaries[child].reserve(closure.size());
	  
	  typename closure_set_type::iterator citer_end = closure.end();
	  for (typename closure_set_type::iterator citer = closure.begin(); citer != citer_end; ++ citer) {
#if 0
	    std::cerr << "closure: " << child << " " << citer->first << " seq: ";
	    std::copy(citer->second.closure.begin(), citer->second.closure.end(), std::ostream_iterator<id_type>(std::cerr, " "));
	    std::cerr << "score: "<< citer->second.score << std::endl;
#endif
	    
	    //std::sort(citer->second.closure.begin(), citer->second.closure.end());
	    
	    unaries[child].push_back(unary_type(citer->first, citer->second.score, closure_map.push_back(citer->second.closure.begin(), citer->second.closure.end())));
	  }
	  
	  std::sort(unaries[child].begin(), unaries[child].end(), less_id<unary_type>());
	  
	  // clear unused closures
	  closure.clear();
	  closure_next.clear();
	}
	
	return unaries[child];
      }
      
      bool extend_actives(const transducer_type& transducer,
			  const active_set_type& actives, 
			  const passive_unary_set_type& passives,
			  const int first,
			  const int last,
			  active_set_type& cell)
      {
	typename active_set_type::const_iterator aiter_begin = actives.begin();
	typename active_set_type::const_iterator aiter_end   = actives.end();
	
	bool found = false;
	
	if (! passives.empty())
	  for (typename active_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
	    if (transducer.has_next(aiter->node)) {
	      const tails_mapped_type tails_prev = tails_map[aiter->edge.tails];

	      tails_type tails(tails_prev.size() + 1);
	      std::copy(tails_prev.begin(), tails_prev.end(), tails.begin());
	      
	      for (id_type id = 0; id != static_cast<id_type>(passives.size()); ++ id)
		if (! passives[id].edges.empty()) {
		  const transducer_type::id_type node = transducer.next(aiter->node, symbol_map[id]);
		  
		  if (node == transducer.root()) continue;
		  
		  tails.back() = tail_type(first, last, id);
		  
		  cell.push_back(active_type(node, edge_type(tails_map.push_back(tails.begin(), tails.end()), aiter->edge.score)));
		  
		  found = true;
		}
	    }
	
	return found;
      }
      
      id_type id_map(const symbol_type& symbol)
      {
	symbol_map_type::iterator iter = symbol_map.insert(symbol).first;
	return iter - symbol_map.begin();
      }
      
    private:
      const symbol_type goal;
      const grammar_type& grammar;
      
      const function_type& function;
      
      const bool yield_source;
      const bool treebank;
      const bool pos_mode;
      const bool ordered;
      const bool frontier;

      symbol_map_type symbol_map;
      id_type goal_id;
      
      score_pair_chart_type inside_outside;
      
      active_chart_set_type    actives;
      passive_chart_type       passives;
      passive_unary_chart_type passives_unary;

      tails_map_type   tails_map;
      closure_map_type closure_map;
      
      unary_map_type      unaries;
      unary_computed_type unaries_computed;
      closure_set_type closure;
      closure_set_type closure_next;
    };
    
    template <typename IteratorGrammar, typename IteratorThreshold>
    ParseCoarse(const symbol_type& __goal,
		IteratorGrammar gfirst, IteratorGrammar glast,
		IteratorThreshold tfirst, IteratorThreshold tlast,
		const function_type& __function,
		const int __beam_size,
		const bool __yield_source=false,
		const bool __treebank=false,
		const bool __pos_mode=false,
		const bool __ordered=false,
		const bool __frontier=false)
      : goal(__goal),
	grammars(gfirst, glast),
	thresholds(tfirst, tlast),
	function(__function),
	beam_size(__beam_size),
	yield_source(__yield_source),
	treebank(__treebank),
	pos_mode(__pos_mode),
        ordered(__ordered),
        frontier(__frontier)
    {
      if (grammars.empty())
	throw std::runtime_error("no grammar?");
      if (thresholds.size() + 1 != grammars.size())
	throw std::runtime_error("do we have enough threshold parameters for grammars?");
    }

    template <typename Grammars, typename Thresholds>
    ParseCoarse(const symbol_type& __goal,
		const Grammars& __grammars,
		const Thresholds& __thresholds,
		const function_type& __function,
		const int __beam_size,
		const bool __yield_source=false,
		const bool __treebank=false,
		const bool __pos_mode=false,
		const bool __ordered=false,
		const bool __frontier=false)
      : goal(__goal),
	grammars(__grammars.begin(), __grammars.end()),
	thresholds(__thresholds.begin(), __thresholds.end()),
	function(__function),
	beam_size(__beam_size),
	yield_source(__yield_source),
	treebank(__treebank),
        pos_mode(__pos_mode),
        ordered(__ordered),
        frontier(__frontier)
    {
      if (grammars.empty())
	throw std::runtime_error("no grammar?");
      if (thresholds.size() + 1 != grammars.size())
	throw std::runtime_error("do we have enough threshold parameters for grammars?");
    }
    
    void operator()(const lattice_type& lattice,
		    hypergraph_type& graph)
    {
      typedef ParseCKY parser_type;
      typedef boost::shared_ptr<parser_type> parser_ptr_type;
      typedef std::vector<parser_ptr_type, std::allocator<parser_ptr_type> > parser_ptr_set_type;
      
      graph.clear();
      
      if (lattice.empty()) return;

      label_score_chart_type scores_init;
      label_score_chart_type scores;
      label_score_chart_type scores_prev;
      parser_ptr_set_type parsers(grammars.size() - 1);
      
      parsers.front().reset(new ParseCKY(goal, grammars.front(), function, yield_source, treebank, pos_mode, ordered, frontier));
      parsers.front()->operator()(lattice, scores_init, PruneNone());

      // final parsing with hypergraph construction
      //ComposeCKY composer(goal, grammars.back(), yield_source, treebank, pos_mode, ordered, false, true);
      cicada::ParseCKY<Semiring, Function> composer(goal, grammars.back(), function, beam_size, yield_source, treebank, pos_mode, ordered, frontier, true);
      
      std::vector<double, std::allocator<double> > factors(thresholds.size(), 1.0);
      
      // up to 4 iterations...
      for (size_t i = 0; i != 4; ++ i) {
	scores = scores_init;
	
	bool succeed = true;
	
	// corse-to-fine 
	size_t level = 1;
	for (/**/; level != grammars.size() - 1; ++ level) {
	  if (! parsers[level])
	    parsers[level].reset(new ParseCKY(goal, grammars[level], function, yield_source, treebank, pos_mode, ordered, frontier));

	  //std::cerr << "level: " << level << std::endl;
	  
	  scores_prev.swap(scores);
	  
	  if (level == 1)
	    succeed = parsers[level]->operator()(lattice, scores, PruneCoarse<CoarseSimple>(scores_prev,
											    thresholds[level - 1] * factors[level - 1],
											    CoarseSimple()));
	  else
	    succeed = parsers[level]->operator()(lattice, scores, PruneCoarse<CoarseSymbol>(scores_prev,
											    thresholds[level - 1] * factors[level - 1],
											    CoarseSymbol(level - 2)));
	  
	  if (! succeed) break;
	}
	
	if (! succeed) {
	  // multiply factors up-until the corresponding levels
	  for (size_t i = 0; i != level; ++ i)
	    factors[i] *= 0.1;
	  continue;
	}
	
	// we will fallback to simple tag!
	if (grammars.size() == 2)
	  composer(lattice, graph, PruneCoarse<CoarseSimple>(scores,
							     thresholds.back() * factors.back(),
							     CoarseSimple()));
	else
	  composer(lattice, graph, PruneCoarse<CoarseSymbol>(scores,
							     thresholds.back() * factors.back(),
							     CoarseSymbol(grammars.size() - 2)));
	if (graph.is_valid()) break;
	
	// multiply all the factors
	for (size_t i = 0; i != factors.size(); ++ i)
	  factors[i] *= 0.1;
      }
    }
    
  private:
    const symbol_type goal;
    grammar_set_type   grammars;
    threshold_set_type thresholds;
    
    const function_type& function;
    const int beam_size;

    const bool yield_source;
    const bool treebank;
    const bool pos_mode;
    const bool ordered;
    const bool frontier;
  };
  
  
  template <typename IteratorGrammar, typename IteratorThreshold, typename Function>
  inline
  void parse_coarse(const Symbol& goal, 
		    IteratorGrammar gfirst, IteratorGrammar glast,
		    IteratorThreshold tfirst, IteratorThreshold tlast,
		    const Function& function,
		    const Lattice& lattice,
		    HyperGraph& graph,
		    const int  beam_size,
		    const bool yield_source=false,
		    const bool treebank=false,
		    const bool pos_mode=false,
		    const bool ordered=false,
		    const bool frontier=false)
  {
    ParseCoarse<typename Function::value_type, Function>(goal, gfirst, glast, tfirst, tlast, function, beam_size, yield_source, treebank, pos_mode, ordered, frontier)(lattice, graph);
  }
  
  template <typename Grammars, typename Thresholds, typename Function>
  inline
  void parse_coarse(const Symbol& goal, 
		    const Grammars& grammars,
		    const Thresholds& thresholds,
		    const Function& function,
		    const Lattice& lattice,
		    HyperGraph& graph,
		    const int  beam_size,
		    const bool yield_source=false,
		    const bool treebank=false,
		    const bool pos_mode=false,
		    const bool ordered=false,
		    const bool frontier=false)
  {
    ParseCoarse<typename Function::value_type, Function>(goal, grammars, thresholds, function, beam_size, yield_source, treebank, pos_mode, ordered, frontier)(lattice, graph);
  }
  
};

#endif
