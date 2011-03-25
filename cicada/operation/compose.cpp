//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <iostream>

#include <cicada/operation.hpp>
#include <cicada/parameter.hpp>
#include <cicada/compose.hpp>
#include <cicada/grammar_simple.hpp>

#include <cicada/operation/compose.hpp>

#include <utils/lexical_cast.hpp>
#include <utils/resource.hpp>
#include <utils/piece.hpp>

namespace cicada
{
  namespace operation
  {
    ComposeTree::ComposeTree(const std::string& parameter,
			     const tree_grammar_type& __tree_grammar,
			     const grammar_type& __grammar,
			     const std::string& __goal,
			     const int __debug)
      : tree_grammar(__tree_grammar), grammar(__grammar),
	goal(__goal),
	yield_source(false),
	debug(__debug)
    {
      typedef cicada::Parameter param_type;
	
      param_type param(parameter);
      if (utils::ipiece(param.name()) != "compose-tree")
	throw std::runtime_error("this is not a Tree composer");
	
      bool source = false;
      bool target = false;
	
      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (utils::ipiece(piter->first) == "yield") {
	  if (utils::ipiece(piter->second) == "source")
	    source = true;
	  else if (utils::ipiece(piter->second) == "target")
	    target = true;
	  else
	    throw std::runtime_error("unknown yield: " + piter->second);
	} else
	  std::cerr << "WARNING: unsupported parameter for Tree composer: " << piter->first << "=" << piter->second << std::endl;
      }
	
      if (source && target)
	throw std::runtime_error("Tree composer can work either source or target yield");
	
      yield_source = source;
    }

    void ComposeTree::operator()(data_type& data) const
    {
      if (! data.hypergraph.is_valid()) return;

      hypergraph_type& hypergraph = data.hypergraph;
      hypergraph_type composed;

      if (debug)
	std::cerr << "compose tree: " << data.id << std::endl;
	
      utils::resource start;

      grammar.assign(hypergraph);
      tree_grammar.assign(hypergraph);
	
      cicada::compose_tree(goal, tree_grammar, grammar, hypergraph, composed, yield_source);
	
      utils::resource end;
    
      if (debug)
	std::cerr << "compose cpu time: " << (end.cpu_time() - start.cpu_time())
		  << " user time: " << (end.user_time() - start.user_time())
		  << std::endl;
      
      if (debug)
	std::cerr << "compose: " << data.id
		  << " # of nodes: " << composed.nodes.size()
		  << " # of edges: " << composed.edges.size()
		  << " valid? " << utils::lexical_cast<std::string>(composed.is_valid())
		  << std::endl;
	
      hypergraph.swap(composed);
    }
    
    ComposeEarley::ComposeEarley(const std::string& parameter,
				 const grammar_type& __grammar,
				 const std::string& __goal,
				 const int __debug)
      : grammar(__grammar),
	goal(__goal),
	yield_source(false),
	debug(__debug)
    {
      typedef cicada::Parameter param_type;
	
      param_type param(parameter);
      if (utils::ipiece(param.name()) != "compose-earley")
	throw std::runtime_error("this is not a Earley composer");
	
      bool source = false;
      bool target = false;
	
      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (utils::ipiece(piter->first) == "yield") {
	  if (utils::ipiece(piter->second) == "source")
	    source = true;
	  else if (utils::ipiece(piter->second) == "target")
	    target = true;
	  else
	    throw std::runtime_error("unknown yield: " + piter->second);
	} else
	  std::cerr << "WARNING: unsupported parameter for Earley composer: " << piter->first << "=" << piter->second << std::endl;
      }
	
      if (source && target)
	throw std::runtime_error("Earley composer can work either source or target yield");
	
      yield_source = source;
    }
    
    void ComposeEarley::operator()(data_type& data) const
    {
      if (! data.hypergraph.is_valid()) return;

      hypergraph_type& hypergraph = data.hypergraph;
      hypergraph_type composed;
    
      if (debug)
	std::cerr << "compose earley: " << data.id << std::endl;

      utils::resource start;

      grammar.assign(hypergraph);
    
      cicada::compose_earley(grammar, hypergraph, composed, yield_source);
    
      utils::resource end;
    
      if (debug)
	std::cerr << "compose cpu time: " << (end.cpu_time() - start.cpu_time())
		  << " user time: " << (end.user_time() - start.user_time())
		  << std::endl;
    
      if (debug)
	std::cerr << "compose: " << data.id
		  << " # of nodes: " << composed.nodes.size()
		  << " # of edges: " << composed.edges.size()
		  << " valid? " << utils::lexical_cast<std::string>(composed.is_valid())
		  << std::endl;
    
      hypergraph.swap(composed);
    }

    ComposeCKY::ComposeCKY(const std::string& parameter,
			   const grammar_type& __grammar,
			   const std::string& __goal,
			   const int __debug)
      : grammar(__grammar),
	goal(__goal),
	yield_source(false),
	treebank(false),
	pos_mode(false),
	unique_goal(false),
	debug(__debug)
    { 
      typedef cicada::Parameter param_type;
	
      param_type param(parameter);
      if (utils::ipiece(param.name()) != "compose-cky" && utils::ipiece(param.name()) != "compose-cyk")
	throw std::runtime_error("this is not a CKY(CYK) composer");

      bool source = false;
      bool target = false;
	
      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (utils::ipiece(piter->first) == "yield") {
	  if (utils::ipiece(piter->second) == "source")
	    source = true;
	  else if (utils::ipiece(piter->second) == "target")
	    target = true;
	  else
	    throw std::runtime_error("unknown yield: " + piter->second);
	} else if (utils::ipiece(piter->first) == "treebank")
	  treebank = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "pos")
	  pos_mode = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "unique" || utils::ipiece(piter->first) == "unique-goal")
	  unique_goal = utils::lexical_cast<bool>(piter->second);
	else
	  std::cerr << "WARNING: unsupported parameter for CKY composer: " << piter->first << "=" << piter->second << std::endl;
      }
	
      if (source && target)
	throw std::runtime_error("CKY composer can work either source or target yield");
	
      yield_source = source;
    }

    void ComposeCKY::operator()(data_type& data) const
    {
      const lattice_type& lattice = data.lattice;
      hypergraph_type& hypergraph = data.hypergraph;
      hypergraph_type composed;
      
      hypergraph.clear();
      if (lattice.empty()) return;
    
      if (debug)
	std::cerr << "compose cky: " << data.id << std::endl;

      utils::resource start;

      grammar.assign(lattice);
	
      cicada::compose_cky(goal, grammar, lattice, composed, yield_source, treebank, pos_mode, unique_goal);
    
      utils::resource end;
    
      if (debug)
	std::cerr << "compose cpu time: " << (end.cpu_time() - start.cpu_time())
		  << " user time: " << (end.user_time() - start.user_time())
		  << std::endl;
    
      if (debug)
	std::cerr << "compose: " << data.id
		  << " # of nodes: " << composed.nodes.size()
		  << " # of edges: " << composed.edges.size()
		  << " valid? " << utils::lexical_cast<std::string>(composed.is_valid())
		  << std::endl;
    
      hypergraph.swap(composed);
    }
    
    ComposeGrammar::ComposeGrammar(const std::string& parameter,
				   const grammar_type& __grammar,
				   const std::string& __goal,
				   const int __debug)
      : grammar(__grammar),
	goal(__goal),
	yield_source(false),
	debug(__debug)
    {
     
      typedef cicada::Parameter param_type;
    
      param_type param(parameter);
      if (utils::ipiece(param.name()) != "compose-grammar")
	throw std::runtime_error("this is not a grammar matching composer");

      bool source = false;
      bool target = false;
	
      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (utils::ipiece(piter->first) == "yield") {
	  if (utils::ipiece(piter->second) == "source")
	    source = true;
	  else if (utils::ipiece(piter->second) == "target")
	    target = true;
	  else
	    throw std::runtime_error("unknown yield: " + piter->second);
	} else
	  std::cerr << "WARNING: unsupported parameter for composer: " << piter->first << "=" << piter->second << std::endl;
      }
	
      if (source && target)
	throw std::runtime_error("Phrase composer can work either source or target yield");
	
      yield_source = source;
    }

    void ComposeGrammar::operator()(data_type& data) const
    {
      hypergraph_type& hypergraph = data.hypergraph;
      hypergraph_type composed;

      if (! hypergraph.is_valid()) return;
      
      if (debug)
	std::cerr << "compose grammar: " << data.id << std::endl;

      utils::resource start;

      grammar.assign(hypergraph);
      
      cicada::compose_grammar(grammar, hypergraph, composed, yield_source);
    
      utils::resource end;
    
      if (debug)
	std::cerr << "compose cpu time: " << (end.cpu_time() - start.cpu_time())
		  << " user time: " << (end.user_time() - start.user_time())
		  << std::endl;
    
      if (debug)
	std::cerr << "compose: " << data.id
		  << " # of nodes: " << composed.nodes.size()
		  << " # of edges: " << composed.edges.size()
		  << " valid? " << utils::lexical_cast<std::string>(composed.is_valid())
		  << std::endl;
    
      hypergraph.swap(composed);
    }

    
    ComposePhrase::ComposePhrase(const std::string& parameter,
				 const grammar_type& __grammar,
				 const std::string& __goal,
				 const int __debug)
      : grammar(__grammar),
	goal(__goal),
	distortion(0),
	yield_source(false),
	debug(__debug)
    { 
      typedef cicada::Parameter param_type;
    
      param_type param(parameter);
      if (utils::ipiece(param.name()) != "compose-phrase")
	throw std::runtime_error("this is not a phrase composer");

      bool source = false;
      bool target = false;
	
      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (utils::ipiece(piter->first) == "distortion")
	  distortion = utils::lexical_cast<int>(piter->second);
	else if (utils::ipiece(piter->first) == "yield") {
	  if (utils::ipiece(piter->second) == "source")
	    source = true;
	  else if (utils::ipiece(piter->second) == "target")
	    target = true;
	  else
	    throw std::runtime_error("unknown yield: " + piter->second);
	} else
	  std::cerr << "WARNING: unsupported parameter for composer: " << piter->first << "=" << piter->second << std::endl;
      }
	
      if (source && target)
	throw std::runtime_error("Phrase composer can work either source or target yield");
	
      yield_source = source;
    }

    void ComposePhrase::operator()(data_type& data) const
    {
      const lattice_type& lattice = data.lattice;
      hypergraph_type& hypergraph = data.hypergraph;
      hypergraph_type composed;

      hypergraph.clear();
      if (lattice.empty()) return;
    
      if (debug)
	std::cerr << "compose phrase: " << data.id << std::endl;

      utils::resource start;

      grammar.assign(lattice);
    
      cicada::compose_phrase(goal, grammar, lattice, distortion, composed);
    
      utils::resource end;
    
      if (debug)
	std::cerr << "compose cpu time: " << (end.cpu_time() - start.cpu_time())
		  << " user time: " << (end.user_time() - start.user_time())
		  << std::endl;
    
      if (debug)
	std::cerr << "compose: " << data.id
		  << " # of nodes: " << composed.nodes.size()
		  << " # of edges: " << composed.edges.size()
		  << " valid? " << utils::lexical_cast<std::string>(composed.is_valid())
		  << std::endl;
    
      hypergraph.swap(composed);
    }

    
    ComposeAlignment::ComposeAlignment(const std::string& parameter,
				       const grammar_type& __grammar,
				       const std::string& __goal,
				       const int __debug)
      : grammar(__grammar),
	goal(__goal),
	lattice_mode(false),
	forest_mode(false),
	debug(__debug)
    { 
      typedef cicada::Parameter param_type;
    
      param_type param(parameter);
      if (utils::ipiece(param.name()) != "compose-alignment")
	throw std::runtime_error("this is not a alignment composer");

      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (utils::ipiece(piter->first) == "lattice")
	  lattice_mode = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "forest") {
	  forest_mode = utils::lexical_cast<bool>(piter->second);
	} else
	  std::cerr << "WARNING: unsupported parameter for composer: " << piter->first << "=" << piter->second << std::endl;
      }
	
      if (lattice_mode && forest_mode)
	throw std::runtime_error("either lattice or forest");

      if (! lattice_mode && ! forest_mode)
	lattice_mode = true;
    }

    void ComposeAlignment::operator()(data_type& data) const
    {
      const lattice_type& lattice = data.lattice;
      hypergraph_type& hypergraph = data.hypergraph;
      hypergraph_type composed;
      
      if (lattice_mode) {
	if (lattice.empty()) {
	  hypergraph.clear();
	  return;
	}
      } else {
	if (! hypergraph.is_valid())
	  return;
      }
      
      lattice_type target;
      if (! data.targets.empty())
	target = lattice_type(data.targets.front());
	
      if (debug)
	std::cerr << "compose alignment: " << data.id << std::endl;
	
      utils::resource start;

      
      if (lattice_mode)
	grammar.assign(lattice, target);
      else
	grammar.assign(hypergraph, target);
	
      if (lattice_mode)
	cicada::compose_alignment(goal, grammar, lattice, target, composed);
      else
	cicada::compose_alignment(goal, grammar, hypergraph, target, composed);
    
      utils::resource end;
    
      if (debug)
	std::cerr << "compose cpu time: " << (end.cpu_time() - start.cpu_time())
		  << " user time: " << (end.user_time() - start.user_time())
		  << std::endl;
    
      if (debug)
	std::cerr << "compose: " << data.id
		  << " # of nodes: " << composed.nodes.size()
		  << " # of edges: " << composed.edges.size()
		  << " valid? " << utils::lexical_cast<std::string>(composed.is_valid())
		  << std::endl;
    
      hypergraph.swap(composed);
    }

  };
};
