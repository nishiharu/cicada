//
//  Copyright(C) 2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

// kbest learner with MPI

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <deque>

#include "cicada_impl.hpp"
#include "cicada_kbest_impl.hpp"

#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/resource.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/base64.hpp"
#include "utils/mpi.hpp"
#include "utils/mpi_device.hpp"
#include "utils/mpi_device_bcast.hpp"
#include "utils/mpi_stream.hpp"
#include "utils/mpi_stream_simple.hpp"
#include "utils/space_separator.hpp"
#include "utils/piece.hpp"
#include "utils/lexical_cast.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/tokenizer.hpp>
#include <boost/random.hpp>

#include "lbfgs.h"

typedef std::vector<path_type, std::allocator<path_type> > path_set_type;

path_set_type kbest_path;
path_set_type oracle_path;
path_type weights_path;
path_type output_path = "-";
path_type output_objective_path;

int iteration = 100;
bool learn_lbfgs = false;
bool learn_sgd = false;
bool learn_mira = false;
bool learn_arow = false;
bool learn_cw = false;
bool regularize_l1 = false;
bool regularize_l2 = false;
double C = 1.0;

bool unite_kbest = false;

int debug = 0;

void options(int argc, char** argv);

void read_kbest(const path_set_type& kbest_path,
		const path_set_type& oracle_path,
		hypothesis_map_type& kbests,
		hypothesis_map_type& oracles);

template <typename Optimize>
double optimize_batch(const hypothesis_map_type& kbests,
		      const hypothesis_map_type& oracles,
		      weight_set_type& weights);

struct OptimizeLBFGS;

void bcast_weights(const int rank, weight_set_type& weights);
void send_weights(const weight_set_type& weights);
void reduce_weights(weight_set_type& weights);


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
      throw std::runtime_error("either L1 or L2 regularization");

    if (kbest_path.empty())
      throw std::runtime_error("no kbest?");
    if (oracle_path.empty())
      throw std::runtime_error("no oracke kbest?");
    
    hypothesis_map_type kbests;
    hypothesis_map_type oracles;
    
    read_kbest(kbest_path, oracle_path, kbests, oracles);
    
    if (debug && mpi_rank == 0)
      std::cerr << "# of features: " << feature_type::allocated() << std::endl;

    weight_set_type weights;
    if (mpi_rank ==0 && ! weights_path.empty()) {
      if (! boost::filesystem::exists(weights_path))
	throw std::runtime_error("no path? " + weights_path.string());
      
      utils::compress_istream is(weights_path, 1024 * 1024);
      is >> weights;
    }
    
    weights.allocate();

    double objective = 0.0;

    boost::mt19937 generator;
    generator.seed(time(0) * getpid());
    
    objective = optimize_batch<OptimizeLBFGS>(kbests, oracles, weights);

    if (debug && mpi_rank == 0)
      std::cerr << "objective: " << objective << std::endl;
    
    if (mpi_rank == 0) {
      utils::compress_ostream os(output_path, 1024 * 1024);
      os.precision(20);
      os << weights;
      
      if (! output_objective_path.empty()) {
	utils::compress_ostream os(output_objective_path, 1024 * 1024);
	os.precision(20);
	os << objective << '\n';
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

  OptimizeLBFGS(const hypothesis_map_type& __kbests,
		const hypothesis_map_type& __oracles,
		weight_set_type& __weights)
    : kbests(__kbests),
      oracles(__oracles),
      weights(__weights) {}

  double operator()()
  {
    lbfgs_parameter_t param;
    lbfgs_parameter_init(&param);
    
    if (regularize_l1) {
      param.orthantwise_c = C;
      param.linesearch = LBFGS_LINESEARCH_BACKTRACKING;
    } else
      param.orthantwise_c = 0.0;
    
    param.max_iterations = iteration;
    
    double objective = 0.0;
    
    lbfgs(weights.size(), &(*weights.begin()), &objective, OptimizeLBFGS::evaluate, 0, this, &param);
    
    return objective;
  }

  
  struct Task
  {
    typedef cicada::semiring::Log<double> weight_type;
    typedef cicada::WeightVector<weight_type, std::allocator<weight_type> > expectation_type;
    
    Task(const weight_set_type& __weights,
	 const hypothesis_map_type& __kbests,
	 const hypothesis_map_type& __oracles)
      : weights(__weights),
	kbests(__kbests),
	oracles(__oracles)
    {}
    
    void operator()()
    {
      g.clear();
      objective = 0.0;

      expectation_type  expectations;
      
      expectations.allocate();
      expectations.clear();
      
      const size_t id_max = utils::bithack::min(kbests.size(), oracles.size());

      for (size_t id = 0; id != id_max; ++ id)
	if (! kbests.empty() && ! oracles.empty()) {
	  
	  weight_type Z_oracle;
	  weight_type Z_kbest;
	  
	  hypothesis_set_type::const_iterator oiter_end = oracles[id].end();
	  for (hypothesis_set_type::const_iterator oiter = oracles[id].begin(); oiter != oiter_end; ++ oiter)
	    Z_oracle += cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, oiter->features.begin(), oiter->features.end(), 0.0));
	
	  hypothesis_set_type::const_iterator kiter_end = kbests[id].end();
	  for (hypothesis_set_type::const_iterator kiter = kbests[id].begin(); kiter != kiter_end; ++ kiter)
	    Z_kbest += cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, kiter->features.begin(), kiter->features.end(), 0.0));
	
	  for (hypothesis_set_type::const_iterator oiter = oracles[id].begin(); oiter != oiter_end; ++ oiter) {
	    const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, oiter->features.begin(), oiter->features.end(), 0.0)) / Z_oracle;
	  
	    hypothesis_type::feature_set_type::const_iterator fiter_end = oiter->features.end();
	    for (hypothesis_type::feature_set_type::const_iterator fiter = oiter->features.begin(); fiter != fiter_end; ++ fiter)
	      expectations[fiter->first] -= weight_type(fiter->second) * weight;
	  }
	
	  for (hypothesis_set_type::const_iterator kiter = kbests[id].begin(); kiter != kiter_end; ++ kiter) {
	    const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, kiter->features.begin(), kiter->features.end(), 0.0)) / Z_kbest;
	  
	    hypothesis_type::feature_set_type::const_iterator fiter_end = kiter->features.end();
	    for (hypothesis_type::feature_set_type::const_iterator fiter = kiter->features.begin(); fiter != fiter_end; ++ fiter)
	      expectations[fiter->first] += weight_type(fiter->second) * weight;
	  }
	
	  const double margin = log(Z_oracle) - log(Z_kbest);
	  objective -= margin;
	
	  if (debug >= 3)
	    std::cerr << " margin: " << margin << std::endl;
	}
      
      // transform feature_expectations into g...
      g.allocate();
      
      std::copy(expectations.begin(), expectations.end(), g.begin());
    }
    
    const weight_set_type& weights;

    const hypothesis_map_type& kbests;
    const hypothesis_map_type& oracles;
    
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
    
    // send notification!
    for (int rank = 1; rank < mpi_size; ++ rank)
      MPI::COMM_WORLD.Send(0, 0, MPI::INT, rank, notify_tag);
    
    bcast_weights(0, optimizer.weights);
    
    task_type task(optimizer.weights, optimizer.kbests, optimizer.oracles);
    task();
    
    // collect all the objective and gradients...
    
    reduce_weights(task.g);
    
    std::fill(g, g + n, 0.0);
    std::transform(task.g.begin(), task.g.end(), g, g, std::plus<double>());
    
    double objective = 0.0;
    MPI::COMM_WORLD.Reduce(&task.objective, &objective, 1, MPI::DOUBLE, MPI::SUM, 0);
    
    const double objective_unregularized = objective;
    
    // L2...
    if (regularize_l2) {
      double norm = 0.0;
      for (int i = 0; i < n; ++ i) {
	g[i] += C * x[i];
	norm += x[i] * x[i];
      }
      objective += 0.5 * C * norm;
    }
    
    if (debug >= 2)
      std::cerr << "objective: " << objective << " non-regularized: " << objective_unregularized << std::endl;
    
    return objective;
  }
    
  const hypothesis_map_type& kbests;
  const hypothesis_map_type& oracles;
  
  weight_set_type& weights;
};

template <typename Optimize>
double optimize_batch(const hypothesis_map_type& kbests,
		      const hypothesis_map_type& oracles,
		      weight_set_type& weights)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  if (mpi_rank == 0) {
    const double objective = Optimize(kbests, oracles, weights)();
    
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
	
	task_type task(weights, kbests, oracles);
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

void unique_kbest(hypothesis_map_type& kbests)
{
#ifdef HAVE_TR1_UNORDERED_SET
  typedef std::tr1::unordered_set<hypothesis_type, boost::hash<hypothesis_type>, std::equal_to<hypothesis_type>,
				  std::allocator<hypothesis_type> > hypothesis_unique_type;
#else
  typedef sgi::hash_set<hypothesis_type, boost::hash<hypothesis_type>, std::equal_to<hypothesis_type>,
			std::allocator<hypothesis_type> > hypothesis_unique_type;
#endif
  
  hypothesis_unique_type uniques;
  
  for (size_t id = 0; id != kbests.size(); ++ id) 
    if (! kbests[id].empty()) {
      uniques.clear();
      uniques.insert(kbests[id].begin(), kbests[id].end());
      
      kbests[id].clear();
      hypothesis_set_type(kbests[id]).swap(kbests[id]);
      
      kbests[id].reserve(uniques.size());
      kbests[id].insert(kbests[id].end(), uniques.begin(), uniques.end());
    }
}

void read_kbest(const path_set_type& kbest_path,
		const path_set_type& oracle_path,
		hypothesis_map_type& kbests,
		hypothesis_map_type& oracles)
{  
  typedef boost::spirit::istream_iterator iter_type;
  typedef kbest_feature_parser<iter_type> parser_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  parser_type parser;
  kbest_feature_type kbest;
  
  if (unite_kbest) {
    size_t id_kbest;
    size_t id_oracle;
    
    for (path_set_type::const_iterator piter = kbest_path.begin(); piter != kbest_path.end(); ++ piter) {
      if (mpi_rank == 0 && debug)
	std::cerr << "reading kbest: " << piter->string() << std::endl;
      
      for (size_t i = mpi_rank; /**/; i += mpi_size) {
	const std::string file_name = utils::lexical_cast<std::string>(i) + ".gz";
	
	const path_type path_kbest = (*piter) / file_name;
	
	if (! boost::filesystem::exists(path_kbest)) break;

	if (i >= kbests.size())
	  kbests.resize(i + 1);
	
	utils::compress_istream is(path_kbest, 1024 * 1024);
	is.unsetf(std::ios::skipws);
	
	iter_type iter(is);
	iter_type iter_end;
	
	while (iter != iter_end) {
	  boost::fusion::get<1>(kbest).clear();
	  boost::fusion::get<2>(kbest).clear();
	  
	  if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, kbest))
	    if (iter != iter_end)
	      throw std::runtime_error("kbest parsing failed");
	  
	  const size_t& id = boost::fusion::get<0>(kbest);

	  if (id != i)
	    throw std::runtime_error("different id: " + utils::lexical_cast<std::string>(id));
	  	  
	  kbests[i].push_back(hypothesis_type(kbest));
	}
      }
    }
    
    oracles.resize(kbests.size());
    
    for (path_set_type::const_iterator piter = oracle_path.begin(); piter != oracle_path.end(); ++ piter) {
      if (mpi_rank == 0 && debug)
	std::cerr << "reading oracles: " << piter->string() << std::endl;
      
      for (size_t i = mpi_rank; i < oracles.size(); i += mpi_size) {
	const std::string file_name = utils::lexical_cast<std::string>(i) + ".gz";
	
	const path_type path_oracle = (*piter) / file_name;
	
	if (! boost::filesystem::exists(path_oracle)) continue;
	
	utils::compress_istream is(path_oracle, 1024 * 1024);
	is.unsetf(std::ios::skipws);
	
	iter_type iter(is);
	iter_type iter_end;
	
	while (iter != iter_end) {
	  boost::fusion::get<1>(kbest).clear();
	  boost::fusion::get<2>(kbest).clear();
	  
	  if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, kbest))
	    if (iter != iter_end)
	      throw std::runtime_error("kbest parsing failed");
	  
	  if (boost::fusion::get<0>(kbest) != i)
	    throw std::runtime_error("different id: " + utils::lexical_cast<std::string>(boost::fusion::get<0>(kbest)));
	  
	  oracles[i].push_back(hypothesis_type(kbest));
	}
      }
    }
    
  } else {
    // synchronous reading...
    if (kbest_path.size() != oracle_path.size())
      throw std::runtime_error("# of kbests does not match");
    
    for (size_t pos = 0; pos != kbest_path.size(); ++ pos) {
      if (mpi_rank == 0 && debug)
	std::cerr << "reading kbest: " << kbest_path[pos].string() << " with " << oracle_path[pos].string() << std::endl;
      
      for (size_t i = mpi_rank; /**/; i += mpi_size) {
	const std::string file_name = utils::lexical_cast<std::string>(i) + ".gz";
	
	const path_type path_kbest  = kbest_path[pos] / file_name;
	const path_type path_oracle = oracle_path[pos] / file_name;
	
	if (! boost::filesystem::exists(path_kbest)) break;
	if (! boost::filesystem::exists(path_oracle)) continue;

	kbests.resize(kbests.size() + 1);
	oracles.resize(oracles.size() + 1);
	
	{
	  utils::compress_istream is(path_kbest, 1024 * 1024);
	  is.unsetf(std::ios::skipws);
	  
	  iter_type iter(is);
	  iter_type iter_end;
	  
	  while (iter != iter_end) {
	    boost::fusion::get<1>(kbest).clear();
	    boost::fusion::get<2>(kbest).clear();
	    
	    if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, kbest))
	      if (iter != iter_end)
		throw std::runtime_error("kbest parsing failed");

	    if (boost::fusion::get<0>(kbest) != i)
	      throw std::runtime_error("different id: " + utils::lexical_cast<std::string>(boost::fusion::get<0>(kbest)));
	    
	    kbests.back().push_back(hypothesis_type(kbest));
	  }
	}

	{
	  utils::compress_istream is(path_oracle, 1024 * 1024);
	  is.unsetf(std::ios::skipws);
	  
	  iter_type iter(is);
	  iter_type iter_end;
	  
	  while (iter != iter_end) {
	    boost::fusion::get<1>(kbest).clear();
	    boost::fusion::get<2>(kbest).clear();
	    
	    if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, kbest))
	      if (iter != iter_end)
		throw std::runtime_error("kbest parsing failed");

	    if (boost::fusion::get<0>(kbest) != i)
	      throw std::runtime_error("different id: " + utils::lexical_cast<std::string>(boost::fusion::get<0>(kbest)));
	    
	    oracles.back().push_back(hypothesis_type(kbest));
	  }
	}
      }
    }
  }

  // uniques...
  unique_kbest(kbests);
  unique_kbest(oracles);
  
  // collect features...
  for (int rank = 0; rank < mpi_size; ++ rank) {
    weight_set_type weights;
    weights.allocate();
    
    for (feature_type::id_type id = 0; id != feature_type::allocated(); ++ id)
      if (! feature_type(id).empty())
	weights[feature_type(id)] = 1.0;
    
    bcast_weights(rank, weights);
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
    device[rank].reset(new device_type(rank, weights_tag, 1024 * 1024));
    stream[rank].reset(new stream_type());
    
    stream[rank]->push(boost::iostreams::gzip_decompressor());
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
  os.push(boost::iostreams::gzip_compressor());
  os.push(utils::mpi_device_sink(0, weights_tag, 1024 * 1024));
  
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
    os.push(utils::mpi_device_bcast_sink(rank, 1024));
    
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
    is.push(utils::mpi_device_bcast_source(rank, 1024));
    
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
  
  po::options_description opts_command("command line options");
  opts_command.add_options()
    ("kbest",   po::value<path_set_type>(&kbest_path)->multitoken(),  "kbest path")
    ("oracle",  po::value<path_set_type>(&oracle_path)->multitoken(), "oracle kbest path")
    ("weights", po::value<path_type>(&weights_path),      "initial parameter")
    ("output",  po::value<path_type>(&output_path),       "output parameter")
    
    ("output-objective", po::value<path_type>(&output_objective_path), "output final objective")
    
    ("iteration", po::value<int>(&iteration)->default_value(iteration), "max # of iterations")
    
    ("learn-lbfgs",  po::bool_switch(&learn_lbfgs),  "batch LBFGS algorithm")
    ("learn-sgd",    po::bool_switch(&learn_sgd),    "online SGD algorithm")
    ("learn-mira",   po::bool_switch(&learn_mira),   "online MIRA algorithm")
    ("learn-arow",   po::bool_switch(&learn_arow),   "online AROW algorithm")
    ("learn-cw",     po::bool_switch(&learn_cw),     "online CW algorithm")
    
    ("regularize-l1", po::bool_switch(&regularize_l1), "L1-regularization")
    ("regularize-l2", po::bool_switch(&regularize_l2), "L2-regularization")
    ("C"            , po::value<double>(&C),           "regularization constant")

    ("unite",    po::bool_switch(&unite_kbest), "unite kbest sharing the same id")
    
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");
  
  po::options_description desc_command;
  desc_command.add(opts_command);
  
  po::variables_map variables;
  po::store(po::parse_command_line(argc, argv, desc_command, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);

  if (variables.count("help")) {

    if (mpi_rank == 0)
      std::cout << argv[0] << " [options] [operations]\n"
		<< opts_command << std::endl;

    MPI::Finalize();
    exit(0);
  }
}
