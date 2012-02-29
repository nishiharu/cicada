//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

//
// online learning using the margin between forest
//

#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <stdexcept>
#include <numeric>
#include <algorithm>

#include "cicada/sentence.hpp"
#include "cicada/lattice.hpp"
#include "cicada/hypergraph.hpp"
#include "cicada/inside_outside.hpp"

#include "cicada/feature_function.hpp"
#include "cicada/weight_vector.hpp"
#include "cicada/semiring.hpp"
#include "cicada/viterbi.hpp"

#include "cicada/apply.hpp"
#include "cicada/model.hpp"

#include "cicada/feature/bleu.hpp"
#include "cicada/feature/bleu_linear.hpp"
#include "cicada/parameter.hpp"

#include "cicada/operation/traversal.hpp"

#include "cicada/eval.hpp"

#include "cicada_impl.hpp"
#include "cicada_learn_online_impl.hpp"

#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/resource.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/bithack.hpp"
#include "utils/space_separator.hpp"
#include "utils/piece.hpp"
#include "utils/lexical_cast.hpp"
#include "utils/random_seed.hpp"

#include <boost/tokenizer.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/random.hpp>

#include "utils/base64.hpp"
#include "utils/mpi.hpp"
#include "utils/mpi_device.hpp"
#include "utils/mpi_device_bcast.hpp"
#include "utils/mpi_stream.hpp"
#include "utils/mpi_stream_simple.hpp"

typedef std::string op_type;
typedef std::vector<op_type, std::allocator<op_type> > op_set_type;

path_type input_file = "-";
path_type output_file = "-";
path_set_type refset_files;
path_set_type oracle_files;

bool input_id_mode = false;
bool input_bitext_mode = false;
bool input_sentence_mode = false;
bool input_lattice_mode = false;
bool input_forest_mode = false;
bool input_span_mode = false;
bool input_alignment_mode = false;
bool input_dependency_mode = false;
bool input_directory_mode = false;

path_type weights_file;

std::string symbol_goal         = vocab_type::S;

grammar_file_set_type grammar_files;
bool grammar_list = false;

grammar_file_set_type tree_grammar_files;
bool tree_grammar_list = false;

feature_parameter_set_type feature_parameters;
bool feature_list = false;

op_set_type ops;
bool op_list = false;

std::string scorer_name = "bleu:order=4,exact=false";

std::string algorithm = "mira";

bool learn_optimized = false;
bool learn_regression = false;
bool learn_factored = false;

int iteration = 100;
bool regularize_l1 = false;
bool regularize_l2 = false;
double C = 1.0;

bool loss_document = false;
bool loss_segment = false;
double loss_scale = 100;

double tolerance_objective = 1e-4;
double tolerance_solver = 1e-4;

double loss_margin = 0.01;
double score_margin = 0.01;
int    loss_kbest = 0;
int    score_kbest = 0;

int batch_size = 1;
bool reranking = false;
bool asynchronous_vectors = false;
bool mix_weights = false;
bool mix_weights_optimized = false;
bool dump_weights = false;

bool apply_exact = false;
int cube_size = 200;

int debug = 0;

template <typename Optimizer, typename Generator>
void optimize(const sample_set_type& samples,
	      operation_set_type& operations,
	      scorer_document_type& scorers,
	      hypergraph_set_type& hypergraph_oracles,
	      feature_function_ptr_set_type& bleus,
	      model_type& model,
	      weight_set_type& weights,
	      weight_set_type& weights_average,
	      Generator& generator);

void compute_oracles(const scorer_document_type& scorers,
		     hypergraph_set_type& graphs,
		     score_ptr_type& score,
		     score_ptr_set_type& scores,
		     feature_function_ptr_set_type& bleus);

void bcast_weights(const int rank, weight_set_type& weights);
void reduce_weights(weight_set_type& weights);
template <typename Optimizer>
void reduce_weights_optimized(weight_set_type& weights, Optimizer& optimizer);
void options(int argc, char** argv);

int main(int argc, char ** argv)
{
  utils::mpi_world mpi_world(argc, argv);
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  try {
    options(argc, argv);
    
    if (regularize_l1 && regularize_l2)
      throw std::runtime_error("you cannot use both of L1 and L2...");

    if (int(learn_regression) + learn_factored > 1)
      throw std::runtime_error("you can learn one of learn-regression|learn-factored");
    if (int(learn_regression) + learn_factored == 0)
      learn_regression = true;


    if (feature_list) {
      if (mpi_rank == 0)
	std::cout << cicada::FeatureFunction::lists();
      return 0;
    }

    if (op_list) {
      if (mpi_rank == 0)
	std::cout << operation_set_type::lists();
      return 0;
    }
    
    if (grammar_list) {
      if (mpi_rank == 0)
	std::cout << grammar_type::lists();
      return 0;
    }

    if (tree_grammar_list) {
      if (mpi_rank == 0)
	std::cout << tree_grammar_type::lists();
      return 0;
    }

    if (boost::filesystem::exists(input_file)) {
      if (boost::filesystem::is_directory(input_file))
	input_directory_mode = true;
      else if (input_directory_mode)
	throw std::runtime_error("non directory input: " + input_file.string());
    }
    
    // read grammars...
    grammar_type grammar(grammar_files.begin(), grammar_files.end());
    if (debug && mpi_rank == 0)
      std::cerr << "grammar: " << grammar.size() << std::endl;

    tree_grammar_type tree_grammar(tree_grammar_files.begin(), tree_grammar_files.end());
    if (mpi_rank == 0 && debug)
      std::cerr << "tree grammar: " << tree_grammar.size() << std::endl;
    
    // read features...
    model_type model;
    for (feature_parameter_set_type::const_iterator piter = feature_parameters.begin(); piter != feature_parameters.end(); ++ piter)
      model.push_back(feature_function_type::create(*piter));
    model.initialize();
    
    if (debug && mpi_rank == 0)
      std::cerr << "feature functions: " << model.size() << std::endl;

    // read refset... we do not shard data...
    scorer_document_type          scorers(scorer_name);
    feature_function_ptr_set_type bleus;
    
    read_refset(refset_files, scorer_name, scorers, bleus);
    
    // read samples.... we will shard data...
    sample_set_type samples(scorers.size());
    read_sample(input_file, samples, input_directory_mode, input_id_mode, mpi_rank, mpi_size);
    
    // read oracle hypergraphs, when used...
    hypergraph_set_type graphs_oracle;
    score_ptr_type      score_oracle;
    score_ptr_set_type  scores_oracle;
    
    if (! oracle_files.empty()) {
      graphs_oracle.resize(scorers.size());
      
      read_oracle(oracle_files, graphs_oracle, mpi_rank, mpi_size);
      
      compute_oracles(scorers, graphs_oracle, score_oracle, scores_oracle, bleus);
    }

    operation_set_type operations(ops.begin(), ops.end(),
				  model,
				  grammar,
				  tree_grammar,
				  symbol_goal,
				  true,
				  input_sentence_mode,
				  input_lattice_mode,
				  input_forest_mode,
				  input_span_mode,
				  input_alignment_mode,
				  input_dependency_mode,
				  input_bitext_mode,
				  false,
				  debug);
    
    weight_set_type weights;
    weight_set_type weights_average;
    
    if (boost::filesystem::exists(weights_file)) {
      utils::compress_istream is(weights_file);
      is >> weights;
    }

    boost::mt19937 generator;
    generator.seed(utils::random_seed());
    
    if (utils::ipiece(algorithm) == "mira")
      ::optimize<OptimizeMIRA>(samples, operations, scorers, graphs_oracle, bleus, model, weights, weights_average, generator);
    else if (utils::ipiece(algorithm) == "cp")
      ::optimize<OptimizeCP>(samples, operations, scorers, graphs_oracle, bleus, model, weights, weights_average, generator);
    else
      throw std::runtime_error("unsupported learning algorithm: " + algorithm);
    
    if (mpi_rank == 0) {
      {
	utils::compress_ostream os(output_file);
	os.precision(20);
	os << weights;
      }

      {
	utils::compress_ostream os(add_suffix(output_file, ".average"));
	os.precision(20);
	os << weights_average;
      }
    }
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    MPI::COMM_WORLD.Abort(1);
    return 1;
  }
  return 0;
}


enum {
  weights_tag = 1000,
  sample_tag,
  vector_tag,
  notify_tag,
  termination_tag,
};

inline
int loop_sleep(bool found, int non_found_iter)
{
  if (! found) {
    boost::thread::yield();
    ++ non_found_iter;
  } else
    non_found_iter = 0;
    
  if (non_found_iter >= 64) {
    struct timespec tm;
    tm.tv_sec = 0;
    tm.tv_nsec = 2000001; // above 2ms
    nanosleep(&tm, NULL);
    
    non_found_iter = 0;
  }
  return non_found_iter;
}


void compute_oracles(const scorer_document_type& scorers,
		     hypergraph_set_type& graphs,
		     score_ptr_type& score,
		     score_ptr_set_type& scores,
		     feature_function_ptr_set_type& bleus)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  score.reset();
  scores.clear();
  scores.resize(scorers.size());
  
  const bool error_metric = scorers.error_metric();
  const double score_factor = (error_metric ? - 1.0 : 1.0);
  
  weight_set_type::feature_type feature_bleu;
  for (size_t i = 0; i != bleus.size(); ++ i)
    if (bleus[i]) {
      feature_bleu = bleus[i]->feature_name();
      break;
    }
  
  double objective_optimum = - std::numeric_limits<double>::infinity();
  
  hypergraph_type graph_oracle;
  
  for (size_t id = 0; id != graphs.size(); ++ id) {
    if (! graphs[id].is_valid()) continue;
    
    score_ptr_type score_curr;
    if (score)
      score_curr = score->clone();
    
    if (scores[id])
      *score_curr -= *scores[id];
    
    dynamic_cast<cicada::feature::Bleu*>(bleus[id].get())->assign(score_curr);
    
    model_type model;
    model.push_back(bleus[id]);
    
    graph_oracle.clear();
      
    cicada::apply_exact(model, graphs[id], graph_oracle);
    
    typedef cicada::semiring::Logprob<double> bleu_weight_type;
    
    bleu_weight_type weight;
    sentence_type sentence;
    cicada::viterbi(graph_oracle, sentence, weight, cicada::operation::sentence_traversal(), cicada::operation::single_scaled_function<bleu_weight_type>(feature_bleu, score_factor));
    
    score_ptr_type score_sample = scorers[id]->score(sentence);
    if (score_curr)
      *score_curr += *score_sample;
    else
      score_curr = score_sample;
    
    const double objective = score_curr->score() * score_factor;
    if (objective > objective_optimum || ! scores[id]) {
      score = score_curr;
      objective_optimum = objective;
      scores[id] = score_sample;
    }
  }
  
  // exchange score...
  score_ptr_type score_bcast;
  if (score)
    score_bcast = score->clone();

  for (int rank = 0; rank != mpi_size; ++ rank) {
    if (rank == mpi_rank) {
      // bcast current bleu-score...
      
      boost::iostreams::filtering_ostream os;
      os.push(utils::mpi_device_bcast_sink(rank, 1024));
      
      if (score_bcast)
	os << score_bcast->encode();
      
    } else {
      boost::iostreams::filtering_istream is;
      is.push(utils::mpi_device_bcast_source(rank, 1024));
      
      std::string encoded;
      is >> encoded;
      
      if (! encoded.empty()) {
	score_ptr_type score_recv = scorer_type::score_type::decode(encoded);
	if (score)
	  *score += *score_recv;
	else
	  score = score_recv;
      }
    }
  }

  if (debug && mpi_rank == 0 && score)
    std::cerr << "oracle score: " << *score << std::endl;
  
  // re-assign adjusted bleu score...
  for (size_t id = 0; id != graphs.size(); ++ id) {
    if (! graphs[id].is_valid()) continue;
    
    // we will keep adjust scores...
    score_ptr_type score_curr = score->clone();
    *score_curr -= *scores[id];
    scores[id] = score_curr;
    
    dynamic_cast<cicada::feature::Bleu*>(bleus[id].get())->assign(scores[id]);
    
    model_type model;
    model.push_back(bleus[id]);
    
    graph_oracle.clear();
    
    cicada::apply_exact(model, graphs[id], graph_oracle);
    
    graphs[id].swap(graph_oracle);
  }
}

template <typename Optimizer, typename Generator>
struct Task
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;

  typedef Optimizer optimizer_type;
  typedef Generator generator_type;
  
  typedef utils::lockfree_list_queue<std::string, std::allocator<std::string> > queue_type;
  
  typedef std::deque<hypergraph_type, std::allocator<hypergraph_type> > hypergraph_set_type;

  typedef std::vector<size_type, std::allocator<size_type> > sample_map_type;

  Task(const sample_set_type& __samples,
       queue_type& __queue_send,
       queue_type& __queue_recv,
       operation_set_type& __operations,
       scorer_document_type& __scorers,
       hypergraph_set_type& __hypergraph_oracles,
       feature_function_ptr_set_type& __bleus,
       model_type& __model,
       optimizer_type& __optimizer,
       generator_type& __generator)
    : samples(__samples), queue_send(__queue_send), queue_recv(__queue_recv),
      operations(__operations),
      scorers(__scorers),
      hypergraph_oracles(__hypergraph_oracles),
      bleus(__bleus),
      model(__model),
      optimizer(__optimizer),
      generator(__generator),
      batch_current(0),
      score_1best(), score(), scores(), norm(0.0)
  {
    optimize_fixed_oracle = ! hypergraph_oracles.empty();

    sample_map.clear();
    
    for (size_type i = 0; i != samples.size(); ++ i)
      if (! samples[i].empty())
	sample_map.push_back(i);
  }

  

  const sample_set_type& samples;
  queue_type&        queue_send;
  queue_type&        queue_recv;
  
  operation_set_type&   operations;
  scorer_document_type& scorers;
  
  hypergraph_set_type&  hypergraph_oracles;
  feature_function_ptr_set_type& bleus;
  model_type&        model;
  
  optimizer_type&    optimizer;
  generator_type&    generator;
  
  int batch_current;
  
  score_ptr_type     score_1best;
  score_ptr_type     score;
  score_ptr_set_type scores;
  double norm;
  std::vector<int, std::allocator<int> > norms;

  bool optimize_fixed_oracle;

  sample_map_type sample_map;
  
  typedef cicada::semiring::Logprob<double> weight_type;
  

  struct count_function
  {
    typedef cicada::semiring::Log<double> value_type;
    
    template <typename Edge>
    value_type operator()(const Edge& x) const
    {
      return cicada::semiring::traits<value_type>::exp(0.0);
    }
  };

  struct feature_count_function
  {
    typedef cicada::semiring::Log<double> weight_type;
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > accumulated_type;
    typedef accumulated_type value_type;
    
    template <typename Edge>
    value_type operator()(const Edge& edge) const
    {
      accumulated_type accumulated;
      
      feature_set_type::const_iterator fiter_end = edge.features.end();
      for (feature_set_type::const_iterator fiter = edge.features.begin(); fiter != fiter_end; ++ fiter)
	if (fiter->second != 0.0)
	  accumulated[fiter->first] = weight_type(fiter->second);
      
      return accumulated;
    }
  };
  
  struct accumulated_set_unique_type
  {
    typedef cicada::semiring::Log<double> weight_type;
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > accumulated_type;
    typedef accumulated_type value_type;
    
    accumulated_type& operator[](size_t index)
    {
      return accumulated;
    }
    
    
    void clear() { accumulated.clear(); }
    
    value_type accumulated;
  };
  
  struct accumulated_set_type
  {
    typedef cicada::semiring::Log<double> weight_type;
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > accumulated_type;
    typedef accumulated_type value_type;
    
    typedef std::vector<value_type, std::allocator<value_type> > value_set_type;

    accumulated_set_type(size_t x) : accumulated(x) {}
    
    accumulated_type& operator[](size_t index)
    {
      return accumulated[index];
    }
    
    void resize(size_t x) { accumulated.resize(x); }
    
    void clear() { accumulated.clear(); }

    size_t size() const { return accumulated.size(); }
    
    value_set_type accumulated;
  };

  typedef std::vector<size_t, std::allocator<size_t> > id_collection_type;
  typedef std::vector<double, std::allocator<double> > label_collection_type;
  typedef std::vector<double, std::allocator<double> > margin_collection_type;
  typedef std::vector<feature_set_type, std::allocator<feature_set_type> > feature_collection_type;
  
  typedef std::vector<typename count_function::value_type, std::allocator<typename count_function::value_type> > count_set_type;
  typedef boost::tuple<sentence_type, feature_set_type> yield_type;
  
  
  void prune_hypergraph(model_type& model_bleu,
			model_type& model_sparse,
			const hypergraph_type& hypergraph,
			const lattice_type& lattice,
			const span_set_type& spans,
			hypergraph_type& modified,
			yield_type& yield, 
			const weight_set_type& weights,
			const weight_set_type& weights_prune,
			const double margin,
			const int  kbest,
			const bool invert=false)
  {
    cicada::apply_cube_prune(model_bleu, hypergraph, modified, cicada::operation::weight_scaled_function<weight_type>(weights, invert ? - 1.0 : 1.0), cube_size);
    
    if (kbest > 0)
      cicada::prune_kbest(modified, cicada::operation::weight_scaled_function<cicada::semiring::Tropical<double> >(weights_prune, 1.0), kbest);
    else
      cicada::prune_beam(modified, cicada::operation::weight_scaled_function<cicada::semiring::Tropical<double> >(weights_prune, 1.0), margin);
    
    if (! model_sparse.empty()) {
      static const size_type __id = 0;
      static const sentence_set_type __targets;
      static const ngram_count_set_type __ngram_counts;
      
      model_sparse.assign(__id, modified, lattice, spans, __targets, __ngram_counts);
      
      model_sparse.apply_feature(true);
      
      cicada::apply_exact(model_sparse, modified);
      
      model_sparse.apply_feature(false);
    }
    
    weight_type weight;
    
    cicada::viterbi(modified, yield, weight, cicada::operation::sentence_feature_traversal(), cicada::operation::weight_scaled_function<weight_type>(weights, invert ? - 1.0 : 1.0));
  }

  void add_support_vectors_regression(const size_t& id,
				      const hypergraph_type& hypergraph_reward,
				      const hypergraph_type& hypergraph_penalty,
				      const feature_type& feature_name,
				      id_collection_type& ids,
				      label_collection_type& labels,
				      margin_collection_type& margins,
				      feature_collection_type& features)
  {
    count_set_type counts_reward(hypergraph_reward.nodes.size());
    count_set_type counts_penalty(hypergraph_penalty.nodes.size());
    
    accumulated_set_unique_type accumulated_reward_unique;
    accumulated_set_unique_type accumulated_penalty_unique;
    
    cicada::inside_outside(hypergraph_reward,  counts_reward,  accumulated_reward_unique,  count_function(), feature_count_function());
    cicada::inside_outside(hypergraph_penalty, counts_penalty, accumulated_penalty_unique, count_function(), feature_count_function());
    
    feature_set_type features_reward(accumulated_reward_unique.accumulated.begin(), accumulated_reward_unique.accumulated.end());
    feature_set_type features_penalty(accumulated_penalty_unique.accumulated.begin(), accumulated_penalty_unique.accumulated.end());
    
    features_reward  *= (1.0 / double(counts_reward.back()));
    features_penalty *= (1.0 / double(counts_penalty.back()));
    
    features.push_back(features_reward - features_penalty);
    
    const double bleu_score = features.back()[feature_name];
    
    //features.back()["bias"] = 1.0;
    features.back().erase(feature_name);
    
    ids.push_back(id);
    labels.push_back(1.0);
    margins.push_back(bleu_score * norm * loss_scale);
  }
  
  void add_support_vectors_factored(const size_t& id,
				    const hypergraph_type& hypergraph_reward,
				    const hypergraph_type& hypergraph_penalty,
				    const feature_type& feature_name,
				    id_collection_type& ids,
				    label_collection_type& labels,
				    margin_collection_type& margins,
				    feature_collection_type& features)
  {
    typedef cicada::semiring::Tropical<double> bleu_weight_type;
    typedef std::vector<bleu_weight_type, std::allocator<bleu_weight_type> > bleu_set_type;

    count_set_type counts_reward(hypergraph_reward.nodes.size());
    count_set_type counts_penalty(hypergraph_penalty.nodes.size());

    accumulated_set_type accumulated_reward(hypergraph_reward.edges.size());
    accumulated_set_type accumulated_penalty(hypergraph_penalty.edges.size());
	  
    cicada::inside_outside(hypergraph_reward,  counts_reward,  accumulated_reward,  count_function(), feature_count_function());
    cicada::inside_outside(hypergraph_penalty, counts_penalty, accumulated_penalty, count_function(), feature_count_function());

    bleu_set_type bleu_reward(hypergraph_reward.nodes.size());
    bleu_set_type bleu_penalty(hypergraph_penalty.nodes.size());
    
    bleu_set_type bleu_edge_reward(hypergraph_reward.edges.size());
    bleu_set_type bleu_edge_penalty(hypergraph_penalty.edges.size());
    
    cicada::inside_outside(hypergraph_reward,  bleu_reward,  bleu_edge_reward,  cicada::operation::single_scaled_function<bleu_weight_type>(feature_name,   1.0), cicada::operation::single_scaled_function<bleu_weight_type>(feature_name,   1.0));
    cicada::inside_outside(hypergraph_penalty, bleu_penalty, bleu_edge_penalty, cicada::operation::single_scaled_function<bleu_weight_type>(feature_name, - 1.0), cicada::operation::single_scaled_function<bleu_weight_type>(feature_name, - 1.0));
    
    const double factor_reward  = 1.0 / double(counts_reward.back());
    const double factor_penalty = 1.0 / double(counts_penalty.back());

    for (size_t i = 0; i != accumulated_reward.size(); ++ i) {
      features.push_back(feature_set_type());
      features.back().assign(accumulated_reward[i].begin(), accumulated_reward[i].end());
      
      features.back() *= factor_reward;
      features.back()["bias"] = 1.0;
      
      features.back().erase(feature_name);

      if (features.back().size() == 1) {
	features.pop_back();
	continue;
      }
      
      ids.push_back(id);
      labels.push_back(1.0);
      //margins.push_back(bleu_edge_reward[i] * norm * loss_scale);
      margins.push_back(1.0);
    }
    
    for (size_t i = 0; i != accumulated_penalty.size(); ++ i) {
      features.push_back(feature_set_type());
      features.back().assign(accumulated_penalty[i].begin(), accumulated_penalty[i].end());
      
      features.back() *= factor_penalty;
      features.back()["bias"] = 1.0;
      
      features.back().erase(feature_name);
      
      if (features.back().size() == 1) {
	features.pop_back();
	continue;
      }
      
      ids.push_back(id);
      labels.push_back(-1.0);
      //margins.push_back(bleu_edge_penalty[i] * norm * loss_scale);
      margins.push_back(1.0);
    }
  }

  
  void operator()()
  {
    optimizer.scorers = scorers;
    
    weight_set_type weights_bleu;
    
    for (size_t seg = 0; seg != bleus.size(); ++ seg) {
      cicada::feature::Bleu* __bleu = dynamic_cast<cicada::feature::Bleu*>(bleus[seg].get());
      if (! __bleu) continue;
      
      weights_bleu[__bleu->feature_name()] = 1.0;
      break;
    }
    
    if (weights_bleu.empty())
      throw std::runtime_error("no bleu scorer?");
    
    model_type model_sparse;
    for (model_type::const_iterator iter = model.begin(); iter != model.end(); ++ iter)
      if ((*iter)->sparse_feature())
	model_sparse.push_back(*iter);

    weight_set_type& weights = optimizer.weights;
    
    if (! reranking)
      operations.assign(weights);
    
    hypergraph_type hypergraph_reward;
    hypergraph_type hypergraph_penalty;
    
    yield_type  yield_viterbi;
    weight_type weight_viterbi;
    
    yield_type  yield_reward;
    weight_type weight_reward;
    
    yield_type  yield_penalty;
    weight_type weight_penalty;

    id_collection_type      ids;
    label_collection_type   labels;
    margin_collection_type  margins;
    feature_collection_type features;

    bool terminated_merge = false;
    bool terminated_sample = false;
    
    sample_map_type::const_iterator siter     = sample_map.begin();
    sample_map_type::const_iterator siter_end = sample_map.end();

    int non_found_iter = 0;
    
    std::string buffer;
    while (! terminated_merge || ! terminated_sample) {
      bool found = false;
      
      while (! terminated_merge && queue_recv.pop_swap(buffer, true)) {
	found = true;

	if (buffer.empty()) {
	  terminated_merge = true;
	  break;
	} else {
	  
	  size_t id = size_t(-1);
	  decode_support_vectors(buffer, id, boost::get<0>(yield_viterbi), hypergraph_reward, hypergraph_penalty);

	  if (id == size_t(-1))
	    throw std::runtime_error("invalid encoded feature vector");

	  if (id >= scorers.size())
	    throw std::runtime_error("id exceed scorer size");

	  if (id >= scores.size())
	    scores.resize(id + 1);
	  
	  if (loss_document) {
	    if (score && scores[id])
	      *score -= *scores[id];

	    norm = 1;
	  } else if (! loss_segment) {
	    if (score)
	      *score *= 0.9;
	    
	    norm *= 0.9;
	    norm += 1;
	  }

	  if (loss_segment || optimize_fixed_oracle)
	    norm = 1;
	  
	  if (score_1best && scores[id])
	    *score_1best -= *scores[id];
	  
	  scorer_ptr_type scorer = scorers[id];
	  cicada::feature::Bleu* __bleu = dynamic_cast<cicada::feature::Bleu*>(bleus[id].get());
	  
	  if (! optimize_fixed_oracle && ! loss_segment) {
	    __bleu->assign(score);
	    
	    model_type model_bleu(bleus[id]);
	    
	    hypergraph_type hypergraph_reward_rescored;
	    hypergraph_type hypergraph_penalty_rescored;
	    
	    cicada::apply_exact(model_bleu, hypergraph_reward, hypergraph_reward_rescored);
	    cicada::apply_exact(model_bleu, hypergraph_penalty, hypergraph_penalty_rescored);
	    
	    hypergraph_reward.swap(hypergraph_reward_rescored);
	    hypergraph_penalty.swap(hypergraph_penalty_rescored);
	  }
	  
	  if (learn_optimized || mix_weights_optimized) {
	    if (id >= optimizer.hypergraphs.size())
	      optimizer.hypergraphs.resize(id + 1);
	    
	    optimizer.hypergraphs[id].clear();
	    optimizer.hypergraphs[id].unite(hypergraph_reward);
	    optimizer.hypergraphs[id].unite(hypergraph_penalty);
	  }
	  
	  scores[id] = scorer->score(boost::get<0>(yield_viterbi));
	  if (! score)
	    score = scores[id]->clone();
	  else
	    *score += *scores[id];
	  
	  if (! score_1best)
	    score_1best = scores[id]->clone();
	  else
	    *score_1best += *scores[id];
	  
	  if (debug >= 2)
	    std::cerr << "1best: " << (*score_1best) << std::endl
		      << "viterbi: " << (*score) << std::endl;
	  
	  if (! optimize_fixed_oracle) {
	    if (id >= hypergraph_oracles.size())
	      hypergraph_oracles.resize(id + 1);
	    
	    hypergraph_oracles[id] = hypergraph_reward;
	  }

	  if (learn_factored)
	    add_support_vectors_factored(id, hypergraph_reward, hypergraph_penalty, __bleu->feature_name(), ids, labels, margins, features);
	  else
	    add_support_vectors_regression(id, hypergraph_reward, hypergraph_penalty, __bleu->feature_name(), ids, labels, margins, features);
	  
	  ++ batch_current;
	  
	  if (batch_current >= batch_size && ! labels.empty()) {
	    if (debug)
	      std::cerr << "# of support vectors: " << labels.size() << std::endl;

	    optimizer(ids, labels, margins, features, learn_optimized);
	    
	    batch_current = 0;
	    ids.clear();
	    labels.clear();
	    margins.clear();
	    features.clear();
	  }
	}
      }
      
      if (terminated_sample) {
	non_found_iter = loop_sleep(found, non_found_iter);
	continue;
      }
      
      if (siter == siter_end) {
	terminated_sample = true;
	queue_send.push(std::string());
	continue;
      }
      
      model.apply_feature(false);
      
      operations(samples[*siter]);
      ++ siter;
      
      // operations.hypergraph contains result...
      const size_t& id = operations.get_data().id;
      const lattice_type& lattice = operations.get_data().lattice;
      const span_set_type& spans = operations.get_data().spans;
      const hypergraph_type& hypergraph = operations.get_data().hypergraph;
      
      if (debug)
	std::cerr << "id: " << id << std::endl;
      
      // collect max-feature from hypergraph
      cicada::viterbi(hypergraph, yield_viterbi, weight_viterbi, cicada::operation::sentence_feature_traversal(), cicada::operation::weight_scaled_function<weight_type>(weights, 1.0));
      
      if (id >= scorers.size())
	throw std::runtime_error("id exceed scorer size");

      // update scores...
      if (id >= scores.size())
	scores.resize(id + 1);
      
      if (loss_document) {
	if (score && scores[id])
	  *score -= *scores[id];
	
	norm = 1;
      } else if (! loss_segment) {
	if (score)
	  *score *= 0.9;
	
	norm *= 0.9;
	norm += 1;
      }
      
      if (loss_segment || optimize_fixed_oracle)
	norm = 1;
      
      scorer_ptr_type scorer = scorers[id];
      cicada::feature::Bleu* __bleu = dynamic_cast<cicada::feature::Bleu*>(bleus[id].get());
      
      model_type model_bleu(bleus[id]);
      
      if (! optimize_fixed_oracle && ! loss_segment)
	__bleu->assign(score);
      
      // compute bleu-rewarded instance

      if (optimize_fixed_oracle) {
	// we will search for the smallest derivation(s) with the best BLEU
	weights[__bleu->feature_name()] =  - loss_scale * norm;
	
	prune_hypergraph(model_bleu, model_sparse, hypergraph_oracles[id], lattice, spans, hypergraph_reward, yield_reward, weights, weights_bleu, loss_margin, loss_kbest, true);
      } else {
	weights[__bleu->feature_name()] =  loss_scale * norm;
	
	prune_hypergraph(model_bleu, model_sparse, hypergraph, lattice, spans, hypergraph_reward, yield_reward, weights, weights_bleu, loss_margin, loss_kbest);
      }
      
      if (! optimize_fixed_oracle) {
	// we will check if we have better oracles, already... if so, use the old oracles...
	
	if (id >= hypergraph_oracles.size())
	  hypergraph_oracles.resize(id + 1);
	
	if (hypergraph_oracles[id].is_valid()) {
	  typedef cicada::semiring::Tropical<double> bleu_weight_type;
	  typedef std::vector<bleu_weight_type, std::allocator<bleu_weight_type> > bleu_set_type;
	  
	  hypergraph_type hypergraph_oracle;
	  
	  cicada::apply_exact(model_bleu, hypergraph_oracles[id], hypergraph_oracle);
	  
	  bleu_set_type bleu_curr(hypergraph_reward.nodes.size());
	  bleu_set_type bleu_prev(hypergraph_oracle.nodes.size());
	  
	  cicada::inside(hypergraph_reward, bleu_curr, cicada::operation::single_scaled_function<bleu_weight_type>(__bleu->feature_name(), 1.0));
	  cicada::inside(hypergraph_oracle, bleu_prev, cicada::operation::single_scaled_function<bleu_weight_type>(__bleu->feature_name(), 1.0));
	  
	  if (bleu_curr.back() >= bleu_prev.back())
	    hypergraph_oracles[id] = hypergraph_reward;
	  else {
	    hypergraph_oracles[id] = hypergraph_oracle;
	    hypergraph_reward.swap(hypergraph_oracle);
	    
	    weight_type weight;
	    cicada::viterbi(hypergraph_reward, yield_reward, weight, cicada::operation::sentence_feature_traversal(), cicada::operation::weight_scaled_function<weight_type>(weights, 1.0));
	  }
	}
      }
	
      // compute bleu-penalty hypergraph
      weights[__bleu->feature_name()] = - loss_scale * norm;
      
      prune_hypergraph(model_bleu, model_sparse, hypergraph, lattice, spans, hypergraph_penalty, yield_penalty, weights, weights, score_margin, score_kbest);
      
      // erase unused weights...
      weights.erase(__bleu->feature_name());

      if (learn_optimized || mix_weights_optimized) {
	if (id >= optimizer.hypergraphs.size())
	  optimizer.hypergraphs.resize(id + 1);

	
	optimizer.hypergraphs[id].clear();
	optimizer.hypergraphs[id].unite(hypergraph_reward);
	optimizer.hypergraphs[id].unite(hypergraph_penalty);
      }
      
      score_ptr_type score_reward  = scorer->score(boost::get<0>(yield_reward));
      score_ptr_type score_penalty = scorer->score(boost::get<0>(yield_penalty));
      if (score) {
	*score_reward  += *score;
	*score_penalty += *score;
      }
      
      if (score_1best && scores[id])
	*score_1best -= *scores[id];
      
      scores[id] = scorer->score(boost::get<0>(yield_viterbi));
      if (! score)
	score = scores[id]->clone();
      else
	*score += *scores[id];

      if (! score_1best)
	score_1best = scores[id]->clone();
      else
	*score_1best += *scores[id];
      
      if (debug) {
	std::cerr << "viterbi: " << boost::get<0>(yield_viterbi) << std::endl;
	
	std::cerr << "hypergraph density:"
		  << " oracle: " << (double(hypergraph_reward.edges.size()) / hypergraph_reward.nodes.size())
		  << " violated: " << (double(hypergraph_penalty.edges.size()) / hypergraph_penalty.nodes.size())
		  << std::endl;
	
	std::cerr << "1best: " << (*score_1best) << std::endl
		  << "viterbi: " << (*score) << std::endl
		  << "oracle: " << (*score_reward) << std::endl
		  << "violated: " << (*score_penalty) << std::endl;
      }

      if (learn_factored)
	add_support_vectors_factored(id, hypergraph_reward, hypergraph_penalty, __bleu->feature_name(), ids, labels, margins, features);
      else
	add_support_vectors_regression(id, hypergraph_reward, hypergraph_penalty, __bleu->feature_name(), ids, labels, margins, features);
      
      ++ batch_current;
      
      if (batch_current >= batch_size && ! labels.empty()) {
	if (debug)
	  std::cerr << "# of support vectors: " << labels.size() << std::endl;
	
	optimizer(ids, labels, margins, features, learn_optimized);
	
	batch_current = 0;
	ids.clear();
	labels.clear();
	margins.clear();
	features.clear();
      }
      
      if (asynchronous_vectors) {
	encode_support_vectors(buffer, id, boost::get<0>(yield_viterbi), hypergraph_reward, hypergraph_penalty);
	
	queue_send.push_swap(buffer);
      }
    }

    if (! labels.empty()) {
      if (debug)
	std::cerr << "# of support vectors: " << labels.size() << std::endl;
      
      optimizer(ids, labels, margins, features, learn_optimized);
      
      batch_current = 0;
      ids.clear();
      labels.clear();
      margins.clear();
      features.clear();
    }

    boost::random_number_generator<generator_type> gen(generator);
    std::random_shuffle(sample_map.begin(), sample_map.end(), gen);
    
    // final function call...
    optimizer.finalize();
  }
};

template <typename Iterator, typename Queue>
inline
bool reduce_vectors(Iterator first, Iterator last, Queue& queue)
{
  bool found = false;
  std::string buffer;
  
  for (/**/; first != last; ++ first)
    if (*first && (*first)->test()) {
      if ((*first)->read(buffer))
	queue.push_swap(buffer);
      else
	first->reset();
      
      buffer.clear();
      
      found = true;
    }
  return found;
}

template <typename Iterator, typename BufferIterator, typename Queue>
bool bcast_vectors(Iterator first, Iterator last, BufferIterator bfirst, Queue& queue)
{
  typedef boost::shared_ptr<std::string> buffer_ptr_type;

  std::string buffer;
  bool found = false;
  
  if (queue.pop_swap(buffer, true)) {
    buffer_ptr_type buffer_ptr;
    if (! buffer.empty()) {
      buffer_ptr.reset(new std::string());
      buffer_ptr->swap(buffer);
    }
    
    BufferIterator biter = bfirst;
    for (Iterator iter = first; iter != last; ++ iter, ++ biter)
      if (*iter)
	biter->push_back(buffer_ptr);

    found = true;
  }
  
  BufferIterator biter = bfirst;
  for (Iterator iter = first; iter != last; ++ iter, ++ biter)
    if (*iter && (*iter)->test() && ! biter->empty()) {
      if (! biter->front()) {
	if (! (*iter)->terminated())
	  (*iter)->terminate();
	else {
	  iter->reset();
	  biter->erase(biter->begin());
	}
      } else {
	(*iter)->write(*(biter->front()));
	biter->erase(biter->begin());
      }
      
      found = true;
    }
  
  return found;
}

template <typename Optimizer, typename Generator>
void optimize(const sample_set_type& samples,
	      operation_set_type& operations,
	      scorer_document_type& scorers,
	      hypergraph_set_type& hypergraph_oracles,
	      feature_function_ptr_set_type& bleus,
	      model_type& model,
	      weight_set_type& weights,
	      weight_set_type& weights_average,
	      Generator& generator)
{
  typedef Optimizer optimizer_type;
  typedef Generator generator_type;
  typedef Task<optimizer_type, generator_type>  task_type;
  typedef typename task_type::queue_type queue_type;
  
  typedef boost::shared_ptr<utils::mpi_ostream_simple> ostream_ptr_type;
  typedef boost::shared_ptr<utils::mpi_istream_simple> istream_ptr_type;
  
  typedef std::vector<ostream_ptr_type, std::allocator<ostream_ptr_type> > ostream_ptr_set_type;
  typedef std::vector<istream_ptr_type, std::allocator<istream_ptr_type> > istream_ptr_set_type;

  typedef boost::shared_ptr<std::string> buffer_ptr_type;
  typedef std::deque<buffer_ptr_type, std::allocator<buffer_ptr_type> > buffer_set_type;
  typedef std::vector<buffer_set_type, std::allocator<buffer_set_type> > buffer_map_type;

  typedef Dumper dumper_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  queue_type queue_reduce;
  queue_type queue_bcast;

  optimizer_type optimizer(weights, C, tolerance_solver, debug);
  
  task_type task(samples, queue_reduce, queue_bcast, operations, scorers, hypergraph_oracles, bleus, model, optimizer, generator);

  dumper_type::queue_type queue_dumper;
  std::auto_ptr<boost::thread> thread_dumper(new boost::thread(dumper_type(queue_dumper)));

  weight_set_type weights_mixed;
  weight_set_type weights_accumulated;
  double norm_accumulated = 0;
  
  for (int iter = 0; iter < iteration; ++ iter) {

    if (mpi_rank == 0 && debug)
      std::cerr << "iteration: " << (iter + 1) << std::endl;

    boost::thread thread(boost::ref(task));
    
    buffer_map_type buffers(mpi_size);
    
    ostream_ptr_set_type ostreams(mpi_size);
    istream_ptr_set_type istreams(mpi_size);
    
    for (int rank = 0; rank < mpi_size; ++ rank)
      if (rank != mpi_rank) {
	ostreams[rank].reset(new utils::mpi_ostream_simple(rank, vector_tag, 4096));
	istreams[rank].reset(new utils::mpi_istream_simple(rank, vector_tag, 4096));
      }
    
    int non_found_iter = 0;
    while (1) {
      bool found = false;
      
      found |= reduce_vectors(istreams.begin(), istreams.end(), queue_bcast);
      
      found |= bcast_vectors(ostreams.begin(), ostreams.end(), buffers.begin(), queue_reduce);
      
      if (std::count(istreams.begin(), istreams.end(), istream_ptr_type()) == mpi_size
	  && std::count(ostreams.begin(), ostreams.end(), ostream_ptr_type()) == mpi_size)
	break;
      
      non_found_iter = loop_sleep(found, non_found_iter);
    }
    
    queue_bcast.push(std::string());
    
    thread.join();
    
    // merge weights...
    weights_mixed = optimizer.weights;
    
    if (mix_weights_optimized)
      reduce_weights_optimized(weights_mixed, optimizer);
    else {
      weights_mixed *= optimizer.updated;
      reduce_weights(weights_mixed);
    }
    
    bcast_weights(0, weights_mixed);
    
    reduce_weights(optimizer.accumulated);
    weights_accumulated += optimizer.accumulated;
    
    long updated_accumulated = 0;
    MPI::COMM_WORLD.Allreduce(&optimizer.updated, &updated_accumulated, 1, MPI::LONG, MPI::SUM);
    norm_accumulated += updated_accumulated;
    
    if (! mix_weights_optimized)
      weights_mixed *= (1.0 / updated_accumulated);
    
    double objective_max = - std::numeric_limits<double>::infinity();
    double objective_min =   std::numeric_limits<double>::infinity();
    MPI::COMM_WORLD.Allreduce(&optimizer.objective_max, &objective_max, 1, MPI::DOUBLE, MPI::MAX);
    MPI::COMM_WORLD.Allreduce(&optimizer.objective_min, &objective_min, 1, MPI::DOUBLE, MPI::MIN);
    
    if (mpi_rank == 0 && dump_weights) {
      
      queue_dumper.push(std::make_pair(add_suffix(output_file, "." + utils::lexical_cast<std::string>(iter + 1)), weights_mixed));
      
      weights_average = weights_accumulated;
      weights_average /= norm_accumulated;
      
      queue_dumper.push(std::make_pair(add_suffix(output_file, "." + utils::lexical_cast<std::string>(iter + 1) + ".average"), weights_average));
    }
    
    if (mix_weights)
      optimizer.weights = weights_mixed;
    
    optimizer.initialize();
    
    if (updated_accumulated == mpi_size) break;
    if (objective_max - objective_min < tolerance_objective) break;
  }

  queue_dumper.push(std::make_pair(path_type(), weight_set_type()));
  
  weights = weights_mixed;
  
  weights_average = weights_accumulated;
  weights_average /= norm_accumulated;

  thread_dumper->join();
}

void send_weights(const int rank, const weight_set_type& weights)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  boost::iostreams::filtering_ostream os;
  os.push(boost::iostreams::zlib_compressor());
  os.push(utils::mpi_device_sink(rank, weights_tag, 4096));
  
  for (feature_type::id_type id = 0; id < weights.size(); ++ id)
    if (! feature_type(id).empty() && weights[id] != 0.0) {
      os << feature_type(id) << ' ';
      utils::encode_base64(weights[id], std::ostream_iterator<char>(os));
      os << '\n';
    }
}

void reduce_weights(const int rank, weight_set_type& weights)
{
  typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  boost::iostreams::filtering_istream is;
  is.push(boost::iostreams::zlib_decompressor());
  is.push(utils::mpi_device_source(rank, weights_tag, 4096));
  
  std::string line;
  
  while (std::getline(is, line)) {
    const utils::piece line_piece(line);
    tokenizer_type tokenizer(line_piece);
    
    tokenizer_type::iterator iter = tokenizer.begin();
    if (iter == tokenizer.end()) continue;
    const utils::piece feature = *iter;
    ++ iter;
    if (iter == tokenizer.end()) continue;
    const utils::piece value = *iter;
    
    weights[feature] += utils::decode_base64<double>(value);
  }
}

template <typename Iterator>
void reduce_weights(Iterator first, Iterator last, weight_set_type& weights)
{
  typedef utils::mpi_device_source            device_type;
  typedef boost::iostreams::filtering_istream stream_type;

  typedef boost::shared_ptr<device_type> device_ptr_type;
  typedef boost::shared_ptr<stream_type> stream_ptr_type;
  
  typedef std::vector<device_ptr_type, std::allocator<device_ptr_type> > device_ptr_set_type;
  typedef std::vector<stream_ptr_type, std::allocator<stream_ptr_type> > stream_ptr_set_type;
  
  typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  device_ptr_set_type device;
  stream_ptr_set_type stream;
  
  for (/**/; first != last; ++ first) {
    device.push_back(device_ptr_type(new device_type(*first, weights_tag, 4096)));
    stream.push_back(stream_ptr_type(new stream_type()));
    
    stream.back()->push(boost::iostreams::zlib_decompressor());
    stream.back()->push(*device.back());
  }
  
  std::string line;
  
  int non_found_iter = 0;
  while (1) {
    bool found = false;
    
    for (size_t i = 0; i != device.size(); ++ i)
      while (stream[i] && device[i] && device[i]->test()) {
	if (std::getline(*stream[i], line)) {
	  const utils::piece line_piece(line);
	  tokenizer_type tokenizer(line_piece);
	  
	  tokenizer_type::iterator iter = tokenizer.begin();
	  if (iter == tokenizer.end()) continue;
	  const utils::piece feature = *iter;
	  ++ iter;
	  if (iter == tokenizer.end()) continue;
	  const utils::piece value = *iter;
	  
	  weights[feature] += utils::decode_base64<double>(value);
	} else {
	  stream[i].reset();
	  device[i].reset();
	}
	found = true;
      }
    
    if (std::count(device.begin(), device.end(), device_ptr_type()) == static_cast<int>(device.size())) break;
    
    non_found_iter = loop_sleep(found, non_found_iter);
  }
}


void reduce_weights(weight_set_type& weights)
{
  typedef std::vector<int, std::allocator<int> > rank_set_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  rank_set_type ranks;
  int merge_size = mpi_size;
  
  while (merge_size > 1 && mpi_rank < merge_size) {
    const int reduce_size = (merge_size / 2 == 0 ? 1 : merge_size / 2);
    
    if (mpi_rank < reduce_size) {
      ranks.clear();
      for (int i = reduce_size; i < merge_size; ++ i)
	if (i % reduce_size == mpi_rank)
	  ranks.push_back(i);
      
      if (ranks.empty()) continue;
      
      if (ranks.size() == 1)
	reduce_weights(ranks.front(), weights);
      else
	reduce_weights(ranks.begin(), ranks.end(), weights);
      
    } else
      send_weights(mpi_rank % reduce_size, weights);
    
    merge_size = reduce_size;
  }
}

template <typename Optimizer>
void reduce_weights_optimized(const int rank, weight_set_type& weights, Optimizer& optimizer)
{
  typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  boost::iostreams::filtering_istream is;
  is.push(boost::iostreams::zlib_decompressor());
  is.push(utils::mpi_device_source(rank, weights_tag, 4096));
  
  std::string line;

  weight_set_type direction;
  
  while (std::getline(is, line)) {
    const utils::piece line_piece(line);
    tokenizer_type tokenizer(line_piece);
    
    tokenizer_type::iterator iter = tokenizer.begin();
    if (iter == tokenizer.end()) continue;
    const utils::piece feature = *iter;
    ++ iter;
    if (iter == tokenizer.end()) continue;
    const utils::piece value = *iter;
    
    direction[feature] += utils::decode_base64<double>(value);
  }
  
  direction -= weights;
  
  const double update = optimizer.line_search(optimizer.hypergraphs, optimizer.scorers, weights, direction, 1e-4, 1.0 - 1e-4);
  if (update == 0.0)
    direction *= 0.5;
  else
    direction *= update;
  
  weights += direction;
}

template <typename Iterator, typename Optimizer>
void reduce_weights_optimized(Iterator first, Iterator last, weight_set_type& weights, Optimizer& optimizer)
{
  for (/**/; first != last; ++ first)
    reduce_weights_optimized(*first, weights, optimizer);
}

template <typename Optimizer>
void reduce_weights_optimized(weight_set_type& weights, Optimizer& optimizer)
{
  typedef std::vector<int, std::allocator<int> > rank_set_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  rank_set_type ranks;
  int merge_size = mpi_size;
  
  while (merge_size > 1 && mpi_rank < merge_size) {
    const int reduce_size = (merge_size / 2 == 0 ? 1 : merge_size / 2);
    
    if (mpi_rank < reduce_size) {
      ranks.clear();
      for (int i = reduce_size; i < merge_size; ++ i)
	if (i % reduce_size == mpi_rank)
	  ranks.push_back(i);
      
      if (ranks.empty()) continue;
      
      if (ranks.size() == 1)
	reduce_weights_optimized(ranks.front(), weights, optimizer);
      else
	reduce_weights_optimized(ranks.begin(), ranks.end(), weights, optimizer);
      
    } else
      send_weights(mpi_rank % reduce_size, weights);
    
    merge_size = reduce_size;
  }
}


void bcast_weights(const int rank, weight_set_type& weights)
{
  typedef std::vector<char, std::allocator<char> > buffer_type;

  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  if (mpi_rank == rank) {
    boost::iostreams::filtering_ostream os;
    os.push(boost::iostreams::zlib_compressor());
    os.push(utils::mpi_device_bcast_sink(rank, 4096));
    
    static const weight_set_type::feature_type __empty;
    
    weight_set_type::const_iterator witer_begin = weights.begin();
    weight_set_type::const_iterator witer_end = weights.end();
    
    for (weight_set_type::const_iterator witer = witer_begin; witer != witer_end; ++ witer)
      if (*witer != 0.0) {
	const weight_set_type::feature_type feature(witer - witer_begin);
	if (feature != __empty) {
	  os << feature << ' ';
	  utils::encode_base64(*witer, std::ostream_iterator<char>(os));
	  os << '\n';
	}
      }
  } else {
    weights.clear();
    weights.allocate();
    
    boost::iostreams::filtering_istream is;
    is.push(boost::iostreams::zlib_decompressor());
    is.push(utils::mpi_device_bcast_source(rank, 4096));
    
    std::string feature;
    std::string value;
    
    while ((is >> feature) && (is >> value))
      weights[feature] = utils::decode_base64<double>(value);
  }
}


void options(int argc, char** argv)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  namespace po = boost::program_options;

  po::options_description opts_config("configuration options");
  
  opts_config.add_options()
    ("input",  po::value<path_type>(&input_file)->default_value(input_file),   "input file")
    ("output", po::value<path_type>(&output_file)->default_value(output_file), "output file")

    ("refset", po::value<path_set_type>(&refset_files)->multitoken(), "refset file(s)")
    ("oracle", po::value<path_set_type>(&oracle_files)->multitoken(), "oracle forest file(s)")
    
    // options for input/output format
    ("input-id",         po::bool_switch(&input_id_mode),         "id-prefixed input")
    ("input-bitext",     po::bool_switch(&input_bitext_mode),     "target sentence prefixed input")
    ("input-sentence",   po::bool_switch(&input_sentence_mode),   "sentence input")
    ("input-lattice",    po::bool_switch(&input_lattice_mode),    "lattice input")
    ("input-forest",     po::bool_switch(&input_forest_mode),     "forest input")
    ("input-span",       po::bool_switch(&input_span_mode),       "span input")
    ("input-alignment",  po::bool_switch(&input_alignment_mode),  "alignment input")
    ("input-dependency", po::bool_switch(&input_dependency_mode), "dependency input")
    ("input-directory",  po::bool_switch(&input_directory_mode),  "input in directory")

    ("weights", po::value<path_type>(&weights_file), "initial weights")
    
    // grammar
    ("goal",              po::value<std::string>(&symbol_goal)->default_value(symbol_goal),    "goal symbol")
    ("grammar",           po::value<grammar_file_set_type >(&grammar_files)->composing(),      "grammar specification(s)")
    ("grammar-list",      po::bool_switch(&grammar_list),                                      "list of available grammar specifications")
    ("tree-grammar",      po::value<grammar_file_set_type >(&tree_grammar_files)->composing(), "tree grammar specification(s)")
    ("tree-grammar-list", po::bool_switch(&tree_grammar_list),                                 "list of available grammar specifications")
    
    // models...
    ("feature-function",      po::value<feature_parameter_set_type >(&feature_parameters)->composing(), "feature function(s)")
    ("feature-function-list", po::bool_switch(&feature_list),                                           "list of available feature function(s)")

    //operatins...
    ("operation",      po::value<op_set_type>(&ops)->composing(), "operations")
    ("operation-list", po::bool_switch(&op_list),                 "list of available operation(s)")

    // learning related..
    ("scorer",      po::value<std::string>(&scorer_name)->default_value(scorer_name), "error metric")

    ("algorithm", po::value<std::string>(&algorithm)->default_value(algorithm), "optimization algorithm (MIRA or CP)")

    ("learn-optimized",  po::bool_switch(&learn_optimized),  "learn by line-search optimization")
    ("learn-regression", po::bool_switch(&learn_regression), "learn by regression")
    ("learn-factored",   po::bool_switch(&learn_factored),   "learn by edge-factored linear classification")
    
    ("iteration",          po::value<int>(&iteration),          "# of mert iteration")
    
    ("regularize-l1", po::bool_switch(&regularize_l1), "regularization via L1")
    ("regularize-l2", po::bool_switch(&regularize_l2), "regularization via L2")
    ("C"            , po::value<double>(&C),           "regularization constant")

    ("loss-document", po::bool_switch(&loss_document),                           "document-wise loss")
    ("loss-segment",  po::bool_switch(&loss_segment),                            "segment-wise loss")
    ("loss-scale",    po::value<double>(&loss_scale)->default_value(loss_scale), "loss scaling")
    
    ("tolerance-objective",     po::value<double>(&tolerance_objective)->default_value(tolerance_objective), "tolerance threshold for primal objective")
    ("tolerance-solver",        po::value<double>(&tolerance_solver)->default_value(tolerance_solver),       "tolerance threshold for QP solver")
    
    ("loss-margin",   po::value<double>(&loss_margin)->default_value(loss_margin),   "loss margin for oracle forest")
    ("score-margin",  po::value<double>(&score_margin)->default_value(score_margin), "score margin for hypothesis forest")

    ("loss-kbest",    po::value<int>(&loss_kbest)->default_value(loss_kbest),   "loss kbest for oracle forest")
    ("score-kbest",   po::value<int>(&score_kbest)->default_value(score_kbest), "score kbest for hypothesis forest")
    
    
    ("batch-size",            po::value<int>(&batch_size)->default_value(batch_size), "batch size")
    ("reranking",             po::bool_switch(&reranking),                            "learn by forest reranking")
    ("asynchronous-vectors",  po::bool_switch(&asynchronous_vectors),                 "asynchrounsly merge support vectors")
    ("mix-weights",           po::bool_switch(&mix_weights),                          "mixing weight vectors at every epoch")
    ("mix-weights-optimized", po::bool_switch(&mix_weights_optimized),                "mixing weight vectors by line-search optimization")
    ("dump-weights",          po::bool_switch(&dump_weights),                         "dump weight vectors at every epoch")
    
    ("apply-exact", po::bool_switch(&apply_exact), "exact feature applicatin w/o pruning")
    ("cube-size",   po::value<int>(&cube_size),    "cube-pruning size")
    ;
  
  po::options_description opts_command("command line options");
  opts_command.add_options()
    ("config", po::value<path_type>(), "configuration file")
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");
  
  po::options_description desc_config;
  po::options_description desc_command;
  
  desc_config.add(opts_config);
  desc_command.add(opts_config).add(opts_command);
  
  po::variables_map variables;

  po::store(po::parse_command_line(argc, argv, desc_command, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  if (variables.count("config")) {
    utils::compress_istream is(variables["config"].as<path_type>());
    po::store(po::parse_config_file(is, desc_config), variables);
  }
  
  po::notify(variables);

  if (variables.count("help")) {
    if (mpi_rank == 0)
      std::cout << argv[0] << " [options]\n"
		<< desc_command << std::endl;

    MPI::Finalize();
    exit(0);
  }
}