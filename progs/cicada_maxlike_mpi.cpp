//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

//
// refset format:
// 0 |||  reference translatin for source sentence 0
// 0 |||  another reference
// 1 |||  reference translation for source sentence 1
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

#include "cicada/feature/scorer.hpp"
#include "cicada/parameter.hpp"

#include "cicada/eval.hpp"

#include "cicada/operation/functional.hpp"
#include "cicada/operation/traversal.hpp"

#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/resource.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/bithack.hpp"
#include "utils/space_separator.hpp"
#include "utils/lexical_cast.hpp"
#include "utils/random_seed.hpp"
#include "utils/getline.hpp"

#include <boost/tokenizer.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/random.hpp>
#include <boost/thread.hpp>

#include "liblbfgs/lbfgs.hpp"
#include "cg_descent/cg.hpp"

#include "cicada_impl.hpp"
#include "cicada_text_impl.hpp"

#include "utils/base64.hpp"
#include "utils/mpi.hpp"
#include "utils/mpi_device.hpp"
#include "utils/mpi_device_bcast.hpp"
#include "utils/mpi_stream.hpp"
#include "utils/mpi_stream_simple.hpp"

typedef boost::filesystem::path path_type;
typedef std::vector<path_type, std::allocator<path_type> > path_set_type;

typedef cicada::Symbol   symbol_type;
typedef cicada::Vocab    vocab_type;
typedef cicada::Sentence sentence_type;

typedef cicada::HyperGraph hypergraph_type;
typedef cicada::Rule       rule_type;

typedef cicada::Model model_type;

typedef hypergraph_type::feature_set_type    feature_set_type;
typedef cicada::WeightVector<double>   weight_set_type;
typedef feature_set_type::feature_type feature_type;

typedef cicada::FeatureFunction feature_function_type;
typedef feature_function_type::feature_function_ptr_type feature_function_ptr_type;

typedef std::vector<hypergraph_type, std::allocator<hypergraph_type> > hypergraph_set_type;


typedef std::vector<feature_function_ptr_type, std::allocator<feature_function_ptr_type> > feature_function_ptr_set_type;

typedef cicada::SentenceVector sentence_set_type;
typedef std::vector<sentence_set_type, std::allocator<sentence_set_type> > sentence_document_type;

typedef cicada::eval::Scorer         scorer_type;
typedef cicada::eval::ScorerDocument scorer_document_type;

typedef scorer_type::score_ptr_type  score_ptr_type;
typedef std::vector<score_ptr_type, std::allocator<score_ptr_type> > score_ptr_set_type;


path_set_type tstset_files;
path_set_type refset_files;
path_type     output_file = "-";

std::string scorer_name = "bleu:order=4,exact=true";

int iteration = 100;
bool regularize_l1 = false;
bool regularize_l2 = false;
double C = 1.0;

double loss_scale = 1.0;
bool oracle_loss = false;
bool apply_exact = false;
int cube_size = 200;
bool softmax_margin = false;

bool learn_sgd = false;
bool learn_lbfgs = false;
bool learn_mira = false;
bool learn_arow = false;
bool learn_cw = false;

double margin_beam = 1e-4;
int margin_kbest = 1;

bool mix_optimized = false;

int debug = 0;

void read_tstset(const path_set_type& files,
		 hypergraph_set_type& graphs,
		 const sentence_document_type& sentences,
		 feature_function_ptr_set_type& features);
void read_refset(const path_set_type& file,
		 scorer_document_type& scorers,
		 sentence_document_type& sentences);
void compute_oracles(const hypergraph_set_type& graphs,
		     const feature_function_ptr_set_type& features,
		     const scorer_document_type& scorers);

template <typename Optimize>
double optimize_batch(const hypergraph_set_type& graphs,
		      const feature_function_ptr_set_type& features,
		      const scorer_document_type& scorers,
		      weight_set_type& weights);
template <typename Optimize, typename Generator>
double optimize_online(const hypergraph_set_type& graphs,
		       const feature_function_ptr_set_type& features,
		       const scorer_document_type& scorers,
		       weight_set_type& weights,
		       Optimize& optimizer,
		       Generator& generator);

#include "cicada_maxlike_impl.hpp"

struct OptimizeLBFGS;

void bcast_sentences(sentence_set_type& sentences);
void bcast_weights(const int rank, weight_set_type& weights);
void send_weights(const weight_set_type& weights);
void reduce_weights(weight_set_type& weights);
void options(int argc, char** argv);


int main(int argc, char ** argv)
{
  utils::mpi_world mpi_world(argc, argv);
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  try {
    options(argc, argv);
    
    if (int(learn_lbfgs) + learn_sgd + learn_mira + learn_arow + learn_cw > 1)
      throw std::runtime_error("eitehr learn-{lbfgs,sgd,mira,arow,cw}");
    if (int(learn_lbfgs) + learn_sgd + learn_mira + learn_arow + learn_cw == 0)
      learn_lbfgs = true;
    
    if (regularize_l1 && regularize_l2)
      throw std::runtime_error("you cannot use both of L1 and L2...");

    // read test set
    if (tstset_files.empty())
      throw std::runtime_error("no test set?");
    
    // read reference set
    scorer_document_type   scorers(scorer_name);
    sentence_document_type sentences;
    
    read_refset(refset_files, scorers, sentences);
    
    if (mpi_rank == 0 && debug)
      std::cerr << "# of references: " << sentences.size() << std::endl;
    
    if (mpi_rank == 0 && debug)
      std::cerr << "reading hypergraphs" << std::endl;
    
    hypergraph_set_type       graphs(sentences.size());
    feature_function_ptr_set_type features(sentences.size());
    
    read_tstset(tstset_files, graphs, sentences, features);

    if (mpi_rank == 0 && debug)
      std::cerr << "# of features: " << feature_type::allocated() << std::endl;
    
    if (oracle_loss)
      compute_oracles(graphs, features, scorers);
    
    boost::mt19937 generator;
    generator.seed(utils::random_seed());
    
    weight_set_type weights;
    
    double objective = 0.0;

    if (learn_sgd) {
      if (regularize_l1) {
	OptimizerSGDL1 optimizer(graphs, features);
	
	objective = optimize_online(graphs, features, scorers, weights, optimizer, generator);
      } else {
	OptimizerSGDL2 optimizer(graphs, features);
	
	objective = optimize_online(graphs, features, scorers, weights, optimizer, generator);
      }
    } else if (learn_mira) {
      OptimizerMIRA optimizer(graphs, features, margin_beam, margin_kbest);
      
      objective = optimize_online(graphs, features, scorers, weights, optimizer, generator);
    } else if (learn_arow) {
      OptimizerAROW optimizer(graphs, features, margin_beam, margin_kbest);
      
      objective = optimize_online(graphs, features, scorers, weights, optimizer, generator);
    } else if (learn_cw) {
      OptimizerCW optimizer(graphs, features, margin_beam, margin_kbest);
      
      objective = optimize_online(graphs, features, scorers, weights, optimizer, generator);
    } else 
      objective = optimize_batch<OptimizeLBFGS>(graphs, features, scorers, weights);
    
    if (mpi_rank == 0 && debug)
      std::cerr << "objective: " << objective << std::endl;

    
    if (mpi_rank == 0) {
      utils::compress_ostream os(output_file);
      os.precision(10);
      os << weights;
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
  sentence_tag,
  gradients_tag,
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

struct OptimizeLBFGS
{

  OptimizeLBFGS(const hypergraph_set_type&           __graphs,
		const feature_function_ptr_set_type& __features,
		weight_set_type&                      __weights)
    : graphs(__graphs),
      features(__features),
      weights(__weights) {}

  double operator()()
  {
    lbfgs_parameter_t param;
    lbfgs_parameter_init(&param);
    
    param.linesearch = LBFGS_LINESEARCH_BACKTRACKING;
    
    if (regularize_l1)
      param.orthantwise_c = C;
    else
      param.orthantwise_c = 0.0;
    
    param.max_iterations = iteration;
    
    double objective = 0.0;
    weights.clear();
    weights.allocate();
    
    lbfgs(weights.size(), &(*weights.begin()), &objective, OptimizeLBFGS::evaluate, 0, this, &param);
    
    return objective;
  }

  
  struct Task
  {
    Task(const hypergraph_set_type&           __graphs,
	 const feature_function_ptr_set_type& __features,
	 const weight_set_type&               __weights)
      : graphs(__graphs),
	features(__features),
	weights(__weights) {}
    
    typedef cicada::semiring::Log<double> weight_type;
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > gradient_type;
    typedef cicada::WeightVector<weight_type >  expectation_type;
    
    typedef std::vector<weight_type, std::allocator<weight_type> > inside_set_type;

    typedef cicada::semiring::Tropical<double> scorer_weight_type;
    typedef std::vector<scorer_weight_type, std::allocator<scorer_weight_type> > scorer_weight_set_type;
    
    struct gradient_set_type
    {
      typedef gradient_type value_type;
      
      template <typename Index>
      gradient_type& operator[](Index)
      {
	return gradient;
      }

      void clear() { gradient.clear(); }
      
      gradient_type gradient;
    };


    struct weight_set_function
    {
      typedef cicada::semiring::Logprob<double> value_type;

      weight_set_function(const weight_set_type& __weights)
	: weights(__weights) {}

      const weight_set_type& weights;
      
      value_type operator()(const feature_set_type& x) const
      {
	return cicada::semiring::traits<value_type>::exp(cicada::dot_product(x, weights));
      }
    };

    struct weight_function
    {
      typedef weight_type value_type;

      weight_function(const weight_set_type& __weights)
	: weights(__weights) {}

      const weight_set_type& weights;

      template <typename Edge>
      value_type operator()(const Edge& edge) const
      {
	// p_e
	return cicada::semiring::traits<value_type>::exp(cicada::dot_product(edge.features, weights));
      }
    };

    struct weight_max_function
    {
      typedef weight_type value_type;

      weight_max_function(const weight_set_type& __weights, const scorer_weight_set_type& __scorers, const scorer_weight_type& __max_scorer)
	: weights(__weights), scorers(__scorers), max_scorer(__max_scorer) {}

      const weight_set_type&      weights;
      const scorer_weight_set_type& scorers;
      const scorer_weight_type      max_scorer;

      template <typename Edge>
      value_type operator()(const Edge& edge) const
      {
	// p_e
	if (log(max_scorer) - log(scorers[edge.id]) >= 1e-4)
	  return value_type();
	else
	  return cicada::semiring::traits<value_type>::exp(cicada::dot_product(edge.features, weights));
      }
    };

    struct feature_function
    {
      typedef gradient_type value_type;

      feature_function(const weight_set_type& __weights) : weights(__weights) {}

      template <typename Edge>
      value_type operator()(const Edge& edge) const
      {
	// p_e r_e
	gradient_type grad;
	
	const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(edge.features, weights));
	
	feature_set_type::const_iterator fiter_end = edge.features.end();
	for (feature_set_type::const_iterator fiter = edge.features.begin(); fiter != fiter_end; ++ fiter)
	  if (fiter->second != 0.0)
	    grad[fiter->first] = weight_type(fiter->second) * weight;
	
	return grad;
      }
      
      const weight_set_type& weights;
    };

    struct feature_max_function
    {
      typedef gradient_type value_type;

      feature_max_function(const weight_set_type& __weights, const scorer_weight_set_type& __scorers, const scorer_weight_type& __max_scorer)
	: weights(__weights), scorers(__scorers), max_scorer(__max_scorer) {}

      template <typename Edge>
      value_type operator()(const Edge& edge) const
      {
	// p_e r_e
	if (log(max_scorer) - log(scorers[edge.id]) >= 1e-4)
	  return gradient_type();

	gradient_type grad;
	
	const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(edge.features, weights));
	
	feature_set_type::const_iterator fiter_end = edge.features.end();
	for (feature_set_type::const_iterator fiter = edge.features.begin(); fiter != fiter_end; ++ fiter)
	  if (fiter->second != 0.0)
	    grad[fiter->first] = weight_type(fiter->second) * weight;
	
	return grad;
      }
      
      const weight_set_type&      weights;
      const scorer_weight_set_type& scorers;
      const scorer_weight_type      max_scorer;
    };
    
    void operator()()
    {
      g.clear();
      objective = 0.0;

      weight_set_type weights_reward(weights);
      weight_set_type weights_penalty(weights);
      weight_set_type weights_scorer;

      weight_set_type::feature_type feature_scorer;
      for (size_t i = 0; i != features.size(); ++ i)
	if (features[i]) {
	  feature_scorer = features[i]->feature_name();
	  break;
	}
      
      weights_reward[feature_scorer]  =  loss_scale;
      weights_penalty[feature_scorer] = -loss_scale;
      weights_scorer[feature_scorer]    =  loss_scale;
      
      hypergraph_type graph_reward;
      hypergraph_type graph_penalty;
      
      scorer_weight_set_type scorers_inside;
      scorer_weight_set_type scorers_inside_outside;
      
      inside_set_type   inside;
      gradient_set_type gradients;
      
      inside_set_type   inside_correct;
      gradient_set_type gradients_correct;
      
      expectation_type  feature_expectations;
      
      for (size_t id = 0; id != graphs.size(); ++ id)
	if (features[id] && graphs[id].is_valid()) {
	
	  model_type model;
	  model.push_back(features[id]);

	  if (! apply_exact) {
	    cicada::apply_cube_prune(model, graphs[id], graph_reward, weight_set_function(weights_reward), cube_size);
	    cicada::apply_cube_prune(model, graphs[id], graph_penalty, weight_set_function(weights_penalty), cube_size);
	    
	    graph_reward.unite(graph_penalty);
	    graph_penalty.clear();
	  }
	  
	  const hypergraph_type& graph = (apply_exact ? graphs[id] : graph_reward);
	
	  // compute inside/outside by scorer using tropical semiring...
	  scorers_inside.clear();
	  scorers_inside_outside.clear();
	
	  scorers_inside.resize(graph.nodes.size());
	  scorers_inside_outside.resize(graph.edges.size());

	  cicada::inside_outside(graph,
				 scorers_inside,
				 scorers_inside_outside,
				 cicada::operation::weight_function<scorer_weight_type >(weights_scorer),
				 cicada::operation::weight_function<scorer_weight_type >(weights_scorer));
	
	  const scorer_weight_type scorer_max = *std::max_element(scorers_inside_outside.begin(), scorers_inside_outside.end());
	
	  // then, inside/outside to collect potentials...

	  inside.clear();
	  inside_correct.clear();
	
	  gradients.clear();
	  gradients_correct.clear();
	
	  inside.resize(graph.nodes.size());
	  inside_correct.resize(graph.nodes.size());
	
	  if (softmax_margin) {
	    cicada::inside_outside(graph, inside, gradients, weight_function(weights_penalty), feature_function(weights_penalty));
	  
	    cicada::inside_outside(graph, inside_correct, gradients_correct,
				   weight_max_function(weights_penalty, scorers_inside_outside, scorer_max),
				   feature_max_function(weights_penalty, scorers_inside_outside, scorer_max));
	  } else {
	    cicada::inside_outside(graph, inside, gradients, weight_function(weights), feature_function(weights));
	  
	    cicada::inside_outside(graph, inside_correct, gradients_correct,
				   weight_max_function(weights, scorers_inside_outside, scorer_max),
				   feature_max_function(weights, scorers_inside_outside, scorer_max));
	  }
	  
	
	  gradient_type& gradient = gradients.gradient;
	  weight_type& Z = inside.back();
	
	  gradient_type& gradient_correct = gradients_correct.gradient;
	  weight_type& Z_correct = inside_correct.back();
	  
	  gradient /= Z;
	  gradient_correct /= Z_correct;
	  
	  feature_expectations -= gradient_correct;
	  feature_expectations += gradient;
	  
	  const double margin = log(Z_correct) - log(Z);
	  
	  objective -= margin;
	  
	  if (debug >= 3)
	    std::cerr << "id: " << id
		      << " scorer: " << log(scorer_max)
		      << " correct: " << log(Z_correct)
		      << " partition: " << log(Z)
		      << " margin: " << margin
		      << std::endl;
	}
      
      // transform feature_expectations into g...
      g.allocate();
      
      std::copy(feature_expectations.begin(), feature_expectations.end(), g.begin());
      
      g[feature_scorer] = 0.0;
    }
    
    const hypergraph_set_type&           graphs;
    const feature_function_ptr_set_type& features;
    const weight_set_type&               weights;
    
    double          objective;
    weight_set_type g;
  };
  
  static lbfgsfloatval_t evaluate(void *instance,
				  const lbfgsfloatval_t *x,
				  lbfgsfloatval_t *g,
				  const int n,
				  const lbfgsfloatval_t step)
  {
    typedef Task                  task_type;
    
    const int mpi_rank = MPI::COMM_WORLD.Get_rank();
    const int mpi_size = MPI::COMM_WORLD.Get_size();
    
    OptimizeLBFGS& optimizer = *((OptimizeLBFGS*) instance);

    weight_set_type::feature_type feature_scorer;
    for (size_t i = 0; i != optimizer.features.size(); ++ i)
      if (optimizer.features[i]) {
	feature_scorer = optimizer.features[i]->feature_name();
	break;
      }
    optimizer.weights[feature_scorer] = 0.0;
    
    // send notification!
    for (int rank = 1; rank < mpi_size; ++ rank)
      MPI::COMM_WORLD.Send(0, 0, MPI::INT, rank, notify_tag);
    
    bcast_weights(0, optimizer.weights);

    task_type task(optimizer.graphs, optimizer.features, optimizer.weights);
	
    task();
    
    reduce_weights(task.g);
    
    std::fill(g, g + n, 0.0);
    std::transform(task.g.begin(), task.g.end(), g, g, std::plus<double>());
    
    // collect all the objective and gradients...
    double objective = 0.0;
    MPI::COMM_WORLD.Reduce(&task.objective, &objective, 1, MPI::DOUBLE, MPI::SUM, 0);
    
    // L2...
    if (regularize_l2) {
      double norm = 0.0;
      for (int i = 0; i < n; ++ i) {
	g[i] += C * x[i];
	norm += x[i] * x[i];
      }
      objective += 0.5 * C * norm;
    }
    
    if (mpi_rank == 0 && debug >= 2)
      std::cerr << "objective: " << objective << std::endl;
    
    return objective;
  }
  
  const hypergraph_set_type&           graphs;
  const feature_function_ptr_set_type& features;
  weight_set_type&                     weights;
};


template <typename Optimizer, typename Generator>
double optimize_online(const hypergraph_set_type& graphs,
		       const feature_function_ptr_set_type& features,
		       const scorer_document_type& scorers,
		       weight_set_type& weights,
		       Optimizer& optimizer,
		       Generator& generator)
{
  typedef std::vector<int, std::allocator<int> > id_set_type;

  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  id_set_type ids(graphs.size());
  for (size_t id = 0; id != ids.size(); ++ id)
    ids[id] = id;
  
  if (mpi_rank == 0) {

    double objective = 0.0;
    
    for (int iter = 0; iter < iteration; ++ iter) {
      
      for (int rank = 1; rank < mpi_size; ++ rank)
	MPI::COMM_WORLD.Send(0, 0, MPI::INT, rank, notify_tag);
      
      bcast_weights(0, optimizer.weights);
      
      optimizer.initialize();
      
      for (size_t id = 0; id != ids.size(); ++ id)
	if (graphs[ids[id]].is_valid())
	  optimizer(ids[id]);
      
      optimizer.finalize();
      
      boost::random_number_generator<Generator> gen(generator);
      std::random_shuffle(ids.begin(), ids.end(), gen);
      
      optimizer.weights *= optimizer.samples;
      reduce_weights(optimizer.weights);
      
      objective = 0.0;
      MPI::COMM_WORLD.Reduce(&optimizer.objective, &objective, 1, MPI::DOUBLE, MPI::SUM, 0);
      
      int samples = 0;
      int samples_local = optimizer.samples;
      MPI::COMM_WORLD.Reduce(&samples_local, &samples, 1, MPI::INT, MPI::SUM, 0);
      
      optimizer.weights *= (1.0 / samples);

      if (debug >= 2)
	std::cerr << "objective: " << objective << std::endl;
    }

    // send termination!
    for (int rank = 1; rank < mpi_size; ++ rank)
      MPI::COMM_WORLD.Send(0, 0, MPI::INT, rank, termination_tag);

    weights.swap(optimizer.weights);
    
    return objective;
    
  } else {
    enum {
      NOTIFY = 0,
      TERMINATION,
    };
    
    MPI::Prequest requests[2];
    
    requests[NOTIFY]      = MPI::COMM_WORLD.Recv_init(0, 0, MPI::INT, 0, notify_tag);
    requests[TERMINATION] = MPI::COMM_WORLD.Recv_init(0, 0, MPI::INT, 0, termination_tag);
    
    for (int i = 0; i < 2; ++ i)
      requests[i].Start();

    while (1) {
      if (MPI::Request::Waitany(2, requests))
	break;
      else {
	requests[NOTIFY].Start();

	bcast_weights(0, optimizer.weights);
	
	optimizer.initialize();
	
	for (size_t id = 0; id != ids.size(); ++ id)
	  if (graphs[ids[id]].is_valid())
	    optimizer(ids[id]);
	
	optimizer.finalize();
	
	boost::random_number_generator<Generator> gen(generator);
	std::random_shuffle(ids.begin(), ids.end(), gen);
	
	optimizer.weights *= optimizer.samples;
	send_weights(optimizer.weights);
	
	double objective = 0.0;
	MPI::COMM_WORLD.Reduce(&optimizer.objective, &objective, 1, MPI::DOUBLE, MPI::SUM, 0);

	int samples = 0;
	int samples_local = optimizer.samples;
	MPI::COMM_WORLD.Reduce(&samples_local, &samples, 1, MPI::INT, MPI::SUM, 0);
      }
    }
    
    
    if (requests[NOTIFY].Test())
      requests[NOTIFY].Cancel();
    
    return 0.0;
  }
}


template <typename Optimize>
double optimize_batch(const hypergraph_set_type& graphs,
		      const feature_function_ptr_set_type& features,
		      const scorer_document_type& scorers,
		      weight_set_type& weights)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  if (mpi_rank == 0) {
    const double objective = Optimize(graphs, features, weights)();
    
    // send termination!
    for (int rank = 1; rank < mpi_size; ++ rank)
      MPI::COMM_WORLD.Send(0, 0, MPI::INT, rank, termination_tag);
    
    return objective;
  } else {
    enum {
      NOTIFY = 0,
      TERMINATION,
    };
    
    MPI::Prequest requests[2];

    requests[NOTIFY]      = MPI::COMM_WORLD.Recv_init(0, 0, MPI::INT, 0, notify_tag);
    requests[TERMINATION] = MPI::COMM_WORLD.Recv_init(0, 0, MPI::INT, 0, termination_tag);
    
    for (int i = 0; i < 2; ++ i)
      requests[i].Start();
    
    while (1) {
      if (MPI::Request::Waitany(2, requests))
	break;
      else {
	typedef typename Optimize::Task task_type;
	
	requests[NOTIFY].Start();
	
	bcast_weights(0, weights);
	
	task_type task(graphs, features, weights);
	
	task();
	
	send_weights(task.g);
	
	double objective = 0.0;
	MPI::COMM_WORLD.Reduce(&task.objective, &objective, 1, MPI::DOUBLE, MPI::SUM, 0);
      }
    }
    
    if (requests[NOTIFY].Test())
      requests[NOTIFY].Cancel();
    
    return 0.0;
  }
}






struct TaskOracle
{
  TaskOracle(const hypergraph_set_type&           __graphs,
	     const feature_function_ptr_set_type& __features,
	     const scorer_document_type&          __scorers,
	     score_ptr_set_type&                  __scores,
	     sentence_set_type&                   __sentences)
    : graphs(__graphs),
      features(__features),
      scorers(__scorers),
      scores(__scores),
      sentences(__sentences)
  {
    score_optimum.reset();
    
    score_ptr_set_type::const_iterator siter_end = scores.end();
    for (score_ptr_set_type::const_iterator siter = scores.begin(); siter != siter_end; ++ siter) 
      if (*siter) {
	if (! score_optimum)
	  score_optimum = (*siter)->clone();
	else
	  *score_optimum += *(*siter);
      } 
  }
  
  void operator()()
  {
    // we will try maximize    
    const bool error_metric = scorers.error_metric();
    const double score_factor = (error_metric ? - 1.0 : 1.0);
    
    weight_set_type::feature_type feature_scorer;
    for (size_t i = 0; i != features.size(); ++ i)
      if (features[i]) {
	feature_scorer = features[i]->feature_name();
	break;
      }
    
    double objective_optimum = (score_optimum
				? score_optimum->score() * score_factor
				: - std::numeric_limits<double>::infinity());

    hypergraph_type graph_oracle;
    
    for (size_t id = 0; id != graphs.size(); ++ id) {
      if (! graphs[id].is_valid()) continue;
      
      score_ptr_type score_curr;
      if (score_optimum)
	score_curr = score_optimum->clone();
      
      if (scores[id])
	*score_curr -= *scores[id];
      
      cicada::feature::Scorer* __scorer = dynamic_cast<cicada::feature::Scorer*>(features[id].get());
      
      if (__scorer)
	__scorer->assign(score_curr);

      typedef cicada::semiring::Logprob<double> weight_type;
      
      model_type model;
      model.push_back(features[id]);
      
      cicada::apply_cube_prune(model, graphs[id], graph_oracle, cicada::operation::single_scaled_function<weight_type>(feature_scorer, score_factor), cube_size);
      
      weight_type weight;
      sentence_type sentence;
      cicada::viterbi(graph_oracle, sentence, weight, cicada::operation::sentence_traversal(), cicada::operation::single_scaled_function<weight_type>(feature_scorer, score_factor));
      
      score_ptr_type score_sample = scorers[id]->score(sentence);
      if (score_curr)
	*score_curr += *score_sample;
      else
	score_curr = score_sample;
      
      const double objective = score_curr->score() * score_factor;
      
      if (objective > objective_optimum || ! scores[id]) {
	score_optimum = score_curr;
	objective_optimum = objective;
	sentences[id] = sentence;
	scores[id] = score_sample;
      }
    }
  }
  
  score_ptr_type score_optimum;
  
  const hypergraph_set_type&           graphs;
  const feature_function_ptr_set_type& features;
  const scorer_document_type&          scorers;
  score_ptr_set_type&                  scores;
  sentence_set_type&                   sentences;
};


void compute_oracles(const hypergraph_set_type& graphs,
		     const feature_function_ptr_set_type& features,
		     const scorer_document_type& scorers)
{
  typedef TaskOracle task_type;

  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  score_ptr_set_type scores(graphs.size());
  sentence_set_type  sentences(graphs.size());

  score_ptr_type score_optimum;
  double objective_optimum = - std::numeric_limits<double>::infinity();
  
  const bool error_metric = scorers.error_metric();
  const double score_factor = (error_metric ? - 1.0 : 1.0);
  
  for (int iter = 0; iter < 5; ++ iter) {
    
    task_type(graphs, features, scorers, scores, sentences)();

    bcast_sentences(sentences);
    
    score_optimum.reset();
    for (size_t id = 0; id != sentences.size(); ++ id)
      if (! sentences[id].empty()) {
	scores[id] = scorers[id]->score(sentences[id]);
	
	if (! score_optimum)
	  score_optimum = scores[id]->clone();
	else
	  *score_optimum += *scores[id];
      }
    
    const double objective = score_optimum->score() * score_factor;
    if (mpi_rank == 0 && debug)
      std::cerr << "oracle score: " << objective << std::endl;

    int terminate = (objective <= objective_optimum);
    
    MPI::COMM_WORLD.Bcast(&terminate, 1, MPI::INT, 0);
    
    if (terminate) break;
    
    objective_optimum = objective;
  }
  
  for (size_t id = 0; id != graphs.size(); ++ id)
    if (features[id]) {
      if (! scores[id])
	throw std::runtime_error("no scores?");
      
      score_ptr_type score_curr = score_optimum->clone();
      *score_curr -= *scores[id];
      
      cicada::feature::Scorer* __scorer = dynamic_cast<cicada::feature::Scorer*>(features[id].get());
      
      if (__scorer)
	__scorer->assign(score_curr);

      if (apply_exact && ! oracle_loss) {
	model_type model;
	model.push_back(features[id]);
	
	hypergraph_type graph_reward;
	
	cicada::apply_exact(model, graphs[id], graph_reward);
	
	const_cast<hypergraph_type&>(graphs[id]).swap(graph_reward);
      }
    }

}


void read_tstset(const path_set_type& files,
		 hypergraph_set_type& graphs,
		 const sentence_document_type& sentences,
		 feature_function_ptr_set_type& features)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  std::string line;

  path_set_type::const_iterator titer_end = tstset_files.end();
  for (path_set_type::const_iterator titer = tstset_files.begin(); titer != titer_end; ++ titer) {
    
    if (mpi_rank == 0 && debug)
      std::cerr << "file: " << *titer << std::endl;
      
    if (boost::filesystem::is_directory(*titer)) {
      size_t id;
      hypergraph_type hypergraph;
      
      for (size_t i = mpi_rank; /**/; i += mpi_size) {
	const path_type path = (*titer) / (utils::lexical_cast<std::string>(i) + ".gz");

	if (! boost::filesystem::exists(path)) break;
	
	utils::compress_istream is(path, 1024 * 1024);
		
	if (! utils::getline(is, line))
	  throw std::runtime_error("no line in file-no: " + utils::lexical_cast<std::string>(i));
	
	std::string::const_iterator iter = line.begin();
	std::string::const_iterator end  = line.end();
	
	if (! parse_id(id, iter, end))
	  throw std::runtime_error("invalid id input: " + path.string());
	if (id != i)
	  throw std::runtime_error("id mismatch: "  + path.string());
	if (static_cast<int>(id % mpi_size) != mpi_rank)
	  throw std::runtime_error("difference it?");
	
	if (! hypergraph.assign(iter, end))
	  throw std::runtime_error("invalid graph format" + path.string());
	if (iter != end)
	  throw std::runtime_error("invalid id ||| graph format" + path.string());
	
	graphs[id].unite(hypergraph);
      }
    } else {
      const path_type& path = *titer;
      
      utils::compress_istream is(*titer, 1024 * 1024);
      
      size_t id;
      hypergraph_type hypergraph;
      
      while (utils::getline(is, line)) {
	std::string::const_iterator iter = line.begin();
	std::string::const_iterator end  = line.end();
	
	if (! parse_id(id, iter, end))
	  throw std::runtime_error("invalid id input: " + path.string());
	if (id >= graphs.size())
	  throw std::runtime_error("tstset size exceeds refset size?" + utils::lexical_cast<std::string>(id) + ": " + path.string());
	
	if (static_cast<int>(id % mpi_size) != mpi_rank) continue;
	
	if (! hypergraph.assign(iter, end))
	  throw std::runtime_error("invalid graph format" + path.string());
	if (iter != end)
	  throw std::runtime_error("invalid id ||| graph format" + path.string());
	
	graphs[id].unite(hypergraph);
      }
    }
  }

  if (debug && mpi_rank == 0)
    std::cerr << "assign SCORER scorer" << std::endl;
    
  for (size_t id = 0; id != graphs.size(); ++ id) 
    if (static_cast<int>(id % mpi_size) == mpi_rank) {
      if (graphs[id].goal == hypergraph_type::invalid)
	std::cerr << "invalid graph at: " << id << std::endl;
      else {
	features[id] = feature_function_type::create(scorer_name);
	
	cicada::feature::Scorer*       __scorer = dynamic_cast<cicada::feature::Scorer*>(features[id].get());
	
	if (! __scorer)
	  throw std::runtime_error("invalid scorer feature function...");

	static const cicada::Lattice       __lattice;
	static const cicada::SpanVector    __spans;
	static const cicada::NGramCountSet __ngram_counts;
	
	__scorer->assign(id, graphs[id], __lattice, __spans, sentences[id], __ngram_counts);
	
	if (apply_exact) {
	  model_type model;
	  model.push_back(features[id]);
	  
	  hypergraph_type graph_reward;
	  
	  cicada::apply_exact(model, graphs[id], graph_reward);
	  
	  const_cast<hypergraph_type&>(graphs[id]).swap(graph_reward);
	}
      }
    }
  
  // collect weights...
  for (int rank = 0; rank < mpi_size; ++ rank) {
    weight_set_type weights;
    weights.allocate();
    
    for (feature_type::id_type id = 0; id != feature_type::allocated(); ++ id)
      if (! feature_type(id).empty())
	weights[feature_type(id)] = 1.0;
    
    bcast_weights(rank, weights);
  }
}

void read_refset(const path_set_type& files,
		 scorer_document_type& scorers,
		 sentence_document_type& sentences)
{
  typedef boost::spirit::istream_iterator iter_type;
  typedef cicada_sentence_parser<iter_type> parser_type;
  
  if (files.empty())
    throw std::runtime_error("no reference files?");

  parser_type parser;
  id_sentence_type id_sentence;
  
  for (path_set_type::const_iterator fiter = files.begin(); fiter != files.end(); ++ fiter) {
    
    if (! boost::filesystem::exists(*fiter) && *fiter != "-")
      throw std::runtime_error("no reference file: " + fiter->string());

    utils::compress_istream is(*fiter, 1024 * 1024);
    is.unsetf(std::ios::skipws);
    
    iter_type iter(is);
    iter_type iter_end;
    
    while (iter != iter_end) {
      id_sentence.second.clear();
      if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, id_sentence))
	if (iter != iter_end)
	  throw std::runtime_error("refset parsing failed");
      
      const int& id = id_sentence.first;
      
      if (id >= static_cast<int>(scorers.size()))
	scorers.resize(id + 1);
      if (id >= static_cast<int>(sentences.size()))
	sentences.resize(id + 1);
      if (! scorers[id])
	scorers[id] = scorers.create();
      
      sentences[id].push_back(id_sentence.second);
      scorers[id]->insert(sentences[id].back());
    }
  }
}

void bcast_sentences(sentence_set_type& sentences)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  for (int rank = 0; rank < mpi_size; ++ rank) {
    if (rank == mpi_rank) {
      boost::iostreams::filtering_ostream os;
      os.push(boost::iostreams::zlib_compressor());
      os.push(utils::mpi_device_bcast_sink(rank, 4096));
      
      for (size_t id = 0; id != sentences.size(); ++ id)
	if (static_cast<int>(id % mpi_size) == mpi_rank)
	  os << id << " ||| " << sentences[id] << '\n';
      
    } else {
      boost::iostreams::filtering_istream is;
      is.push(boost::iostreams::zlib_decompressor());
      is.push(utils::mpi_device_bcast_source(rank, 4096));
      
      int id;
      std::string sep;
      sentence_type sentence;
      
      while (is >> id >> sep >> sentence) {
	if (sep != "|||")
	  throw std::runtime_error("invalid sentence format");
	
	sentences[id] = sentence;
      }
    }
  }
}


void reduce_weights(weight_set_type& weights)
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
  
  device_ptr_set_type device(mpi_size);
  stream_ptr_set_type stream(mpi_size);

  for (int rank = 1; rank < mpi_size; ++ rank) {
    device[rank].reset(new device_type(rank, weights_tag, 4096));
    stream[rank].reset(new stream_type());
    
    stream[rank]->push(boost::iostreams::zlib_decompressor());
    stream[rank]->push(*device[rank]);
  }

  std::string line;
  
  int non_found_iter = 0;
  while (1) {
    bool found = false;
    
    for (int rank = 1; rank < mpi_size; ++ rank)
      while (stream[rank] && device[rank] && device[rank]->test()) {
	if (std::getline(*stream[rank], line)) {
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
	  stream[rank].reset();
	  device[rank].reset();
	}
	found = true;
      }
    
    if (std::count(device.begin(), device.end(), device_ptr_type()) == mpi_size) break;
    
    non_found_iter = loop_sleep(found, non_found_iter);
  }
}

void send_weights(const weight_set_type& weights)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  boost::iostreams::filtering_ostream os;
  os.push(boost::iostreams::zlib_compressor());
  os.push(utils::mpi_device_sink(0, weights_tag, 4096));
  
  for (feature_type::id_type id = 0; id < weights.size(); ++ id)
    if (! feature_type(id).empty() && weights[id] != 0.0) {
      os << feature_type(id) << ' ';
      utils::encode_base64(weights[id], std::ostream_iterator<char>(os));
      os << '\n';
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
  namespace po = boost::program_options;

  po::options_description opts_config("configuration options");
  
  opts_config.add_options()
    ("tstset",  po::value<path_set_type>(&tstset_files)->multitoken(), "test set file(s) (in hypergraph format)")
    ("refset",  po::value<path_set_type>(&refset_files)->multitoken(), "reference set file(s)")
    
    ("output", po::value<path_type>(&output_file)->default_value(output_file), "output file")
    
    ("scorer",      po::value<std::string>(&scorer_name)->default_value(scorer_name), "error metric")
    
    ("iteration",          po::value<int>(&iteration),          "# of mert iteration")

    ("learn-lbfgs",  po::bool_switch(&learn_lbfgs),  "batch LBFGS algorithm")
    ("learn-sgd",    po::bool_switch(&learn_sgd),    "online SGD algorithm")
    ("learn-mira",   po::bool_switch(&learn_mira),   "online MIRA algorithm")
    ("learn-arow",   po::bool_switch(&learn_arow),   "online AROW algorithm")
    ("learn-cw",     po::bool_switch(&learn_cw),     "online CW algorithm")
    
    ("regularize-l1", po::bool_switch(&regularize_l1), "regularization via L1")
    ("regularize-l2", po::bool_switch(&regularize_l2), "regularization via L2")
    ("C"            , po::value<double>(&C),           "regularization constant")
    
    ("loss-scale",  po::value<double>(&loss_scale), "scaling for loss function")
    ("oracle-loss", po::bool_switch(&oracle_loss),  "loss from oracle translations")
    ("apply-exact", po::bool_switch(&apply_exact),  "exact feature applicatin w/o pruning")
    ("cube-size",   po::value<int>(&cube_size),     "cube-pruning size")

    ("margin-beam",  po::value<double>(&margin_beam), "margin by beam")
    ("margin-kbest", po::value<int>(&margin_kbest),   "margin by kbest")

    ("softmax-margin", po::bool_switch(&softmax_margin), "softmax-margin")
    ("mix-optimized",  po::bool_switch(& mix_optimized), "optimized weights mixing")
    ;
  
  po::options_description opts_command("command line options");
  opts_command.add_options()
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");
  
  po::options_description desc_config;
  po::options_description desc_command;
  
  desc_config.add(opts_config);
  desc_command.add(opts_config).add(opts_command);
  
  po::variables_map variables;

  po::store(po::parse_command_line(argc, argv, desc_command, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);

  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc_command << std::endl;
    exit(0);
  }
}
