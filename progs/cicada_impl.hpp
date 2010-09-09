#include <stdexcept>

#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/lexical_cast.hpp>

#include <utils/compress_stream.hpp>
#include <utils/filesystem.hpp>
#include <utils/lexical_cast.hpp>

#include "cicada/sentence.hpp"
#include "cicada/lattice.hpp"
#include "cicada/hypergraph.hpp"

#include "cicada/kbest.hpp"
#include "cicada/parameter.hpp"
#include "cicada/graphviz.hpp"

#include "cicada/model.hpp"
#include "cicada/grammar.hpp"

#include "cicada/operation_set.hpp"

#include "cicada/feature_function.hpp"
#include "cicada/weight_vector.hpp"
#include "cicada/semiring.hpp"
#include "cicada/span_vector.hpp"
#include "cicada/ngram_count_set.hpp"

#include "utils/compress_stream.hpp"
#include "utils/resource.hpp"
#include "utils/sgi_hash_map.hpp"
#include "utils/sgi_hash_set.hpp"

typedef boost::filesystem::path path_type;

typedef std::vector<std::string, std::allocator<std::string> > grammar_file_set_type;
typedef std::vector<std::string, std::allocator<std::string> > feature_parameter_set_type;

typedef cicada::Symbol          symbol_type;
typedef cicada::Vocab           vocab_type;
typedef cicada::Sentence        sentence_type;
typedef cicada::Lattice         lattice_type;
typedef cicada::Rule            rule_type;
typedef cicada::HyperGraph      hypergraph_type;
typedef cicada::Grammar         grammar_type;
typedef cicada::Model           model_type;
typedef cicada::FeatureFunction feature_function_type;


typedef cicada::OperationSet operation_set_type;

typedef feature_function_type::feature_function_ptr_type feature_function_ptr_type;

typedef rule_type::feature_set_type    feature_set_type;
typedef feature_set_type::feature_type feature_type;
typedef cicada::WeightVector<double>   weight_set_type;

typedef cicada::SpanVector span_set_type;
typedef cicada::NGramCountSet ngram_count_set_type;
typedef cicada::SentenceVector sentence_set_type;

struct source_length_function
{
  typedef cicada::semiring::Tropical<int> value_type;
  
  template <typename Edge>
  value_type operator()(const Edge& edge) const
  {
    int length = 0;
    rule_type::symbol_set_type::const_iterator siter_end = edge.rule->source.end();
    for (rule_type::symbol_set_type::const_iterator siter = edge.rule->source.begin(); siter != siter_end; ++ siter)
      length += (*siter != vocab_type::EPSILON && siter->is_terminal());
    
    // since we will "max" at operator+, we will collect negative length
    return cicada::semiring::traits<value_type>::log(- length);
  }
};

template <typename Weight>
struct weight_set_scaled_function
{
  typedef Weight value_type;
  
  weight_set_scaled_function(const weight_set_type& __weights, const double& __scale)
    : weights(__weights), scale(__scale) {}
  
  const weight_set_type& weights;
  const double scale;
  
  template <typename Edge>
  value_type operator()(const Edge& edge) const
  {
    return cicada::semiring::traits<value_type>::log(edge.features.dot(weights) * scale);
  }
  
};

struct weight_set_function
{
  typedef cicada::semiring::Logprob<double> value_type;

  weight_set_function(const weight_set_type& __weights)
    : weights(__weights) {}

  const weight_set_type& weights;

  value_type operator()(const hypergraph_type::edge_type& x) const
  {
    return cicada::semiring::traits<value_type>::log(x.features.dot(weights));
  }
  
  template <typename FeatureSet>
  value_type operator()(const FeatureSet& x) const
  {
    return cicada::semiring::traits<value_type>::log(x.dot(weights));
  }
};


struct weight_set_function_one
{
  typedef cicada::semiring::Logprob<double> value_type;

  weight_set_function_one(const weight_set_type& __weights) {}

  value_type operator()(const hypergraph_type::edge_type& x) const
  {
    return cicada::semiring::traits<value_type>::log(x.features.dot());
  }
  
  template <typename FeatureSet>
  value_type operator()(const FeatureSet& x) const
  {
    return cicada::semiring::traits<value_type>::log(x.dot());
  }
};


struct kbest_function
{
  typedef rule_type::feature_set_type feature_set_type;

  typedef cicada::semiring::Logprob<double> value_type;

  kbest_function(const weight_set_type& __weights)
    : weights(__weights) {}

  const weight_set_type& weights;
  
  template <typename Edge>
  value_type operator()(const Edge& edge) const
  {
    return cicada::semiring::traits<value_type>::log(edge.features.dot(weights));
  }

  value_type operator()(const feature_set_type& features) const
  {
    return cicada::semiring::traits<value_type>::log(features.dot(weights));
  }

};

struct kbest_function_one
{
  typedef rule_type::feature_set_type feature_set_type;

  typedef cicada::semiring::Logprob<double> value_type;
  
  kbest_function_one(const weight_set_type& __weights) {}

  template <typename Edge>
  value_type operator()(const Edge& edge) const
  {
    return cicada::semiring::traits<value_type>::log(edge.features.dot());
  }

  value_type operator()(const feature_set_type& features) const
  {
    return cicada::semiring::traits<value_type>::log(features.dot());
  }
};


template <typename Iterator>
inline
bool parse_id(size_t& id, Iterator& iter, Iterator end)
{
  namespace qi = boost::spirit::qi;
  namespace standard = boost::spirit::standard;
  namespace phoenix = boost::phoenix;
  
  using qi::phrase_parse;
  using qi::_1;
  using qi::ulong_;
  using standard::space;
  
  using phoenix::ref;
  
  return phrase_parse(iter, end, ulong_ [ref(id) = _1] >> "|||", space);
}

template <typename Iterator>
inline
bool parse_separator(Iterator& iter, Iterator end)
{
  namespace qi = boost::spirit::qi;
  namespace standard = boost::spirit::standard;
  namespace phoenix = boost::phoenix;
  
  using qi::phrase_parse;
  using standard::space;
  
  return phrase_parse(iter, end, "|||", space);
}

template <typename HyperGraph, typename Lattice, typename SentenceSet, typename Sentence>
inline
bool parse_line(const std::string& line,
		size_t& id,
		HyperGraph& hypergraph,
		Lattice& lattice,
		Lattice& target,
		SentenceSet& target_sentences,
		Sentence& sentence,
		const bool input_id,
		const bool input_lattice,
		const bool input_forest, 
		const bool input_bitext)
{
  std::string::const_iterator iter = line.begin();
  std::string::const_iterator end = line.end();
  
  if (input_id)
    if (! parse_id(id, iter, end))
      throw std::runtime_error("invalid id-prefixed format");
  
  if (input_lattice) {
    if (! lattice.assign(iter, end))
      throw std::runtime_error("invalid lattive format");
  } else if (input_forest) {
    if (! hypergraph.assign(iter, end))
	  throw std::runtime_error("invalid hypergraph format");
  } else {
    if (! sentence.assign(iter, end))
      throw std::runtime_error("invalid sentence format");
    
    lattice = Lattice(sentence);
  }
  
  if (input_bitext) {
    target_sentences.clear();
    
    while (parse_separator(iter, end)) {
      target_sentences.push_back(Sentence());
      
      if (! target_sentences.back().assign(iter, end))
	throw std::runtime_error("invalid sentence format");
    }
    
    if (target_sentences.empty())
      throw std::runtime_error("no bitext?");
    
    target = Lattice(target_sentences.front());
  }
  
  return iter == end;
}

