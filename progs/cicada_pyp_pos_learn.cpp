//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

//
// PYP-IHMM!
//

//
// @InProceedings{vangael-vlachos-ghahramani:2009:EMNLP,
//   author    = {Van Gael, Jurgen  and  Vlachos, Andreas  and  Ghahramani, Zoubin},
//   title     = {The infinite {HMM} for unsupervised {PoS} tagging},
//   booktitle = {Proceedings of the 2009 Conference on Empirical Methods in Natural Language Processing},
//   month     = {August},
//   year      = {2009},
//   address   = {Singapore},
//   publisher = {Association for Computational Linguistics},
//   pages     = {678--687},
//   url       = {http://www.aclweb.org/anthology/D/D09/D09-1071}
// }
//

#include <map>
#include <iterator>
#include <numeric>

#include <cicada/sentence.hpp>
#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>
#include <cicada/semiring/logprob.hpp>

#include "utils/vector2.hpp"
#include "utils/chunk_vector.hpp"
#include "utils/resource.hpp"
#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/mathop.hpp"
#include "utils/bithack.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/restaurant.hpp"
#include "utils/restaurant_vector.hpp"
#include "utils/stick_break.hpp"
#include "utils/pyp_parameter.hpp"
#include "utils/sampler.hpp"
#include "utils/repository.hpp"
#include "utils/packed_device.hpp"
#include "utils/packed_vector.hpp"
#include "utils/succinct_vector.hpp"
#include "utils/simple_vector.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/fusion/tuple.hpp>

typedef cicada::Symbol    word_type;
typedef cicada::Sentence  sentence_type;
typedef cicada::Vocab     vocab_type;

struct PYP
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  typedef uint32_t  id_type;
};

struct PYPPOS
{
  typedef PYP::size_type       size_type;
  typedef PYP::difference_type difference_type;
  typedef PYP::id_type         id_type;
  
  typedef utils::restaurant_vector< > table_transition_type;
  typedef utils::restaurant<word_type, boost::hash<word_type>, std::equal_to<word_type>, std::allocator<word_type > > table_emission_type;
			    
  typedef utils::chunk_vector<table_transition_type, 4096/sizeof(table_transition_type), std::allocator<table_transition_type> > transition_type;
  typedef utils::chunk_vector<table_emission_type, 4096/sizeof(table_emission_type), std::allocator<table_emission_type> >       emission_type;

  typedef utils::stick_break< > beta_type;

  typedef utils::pyp_parameter parameter_type;
  
  template <typename Sampler>
  void increment(const id_type prev, const id_type next, const word_type& word, Sampler& sampler, const double temperature=1.0)
  {
    // emission
    if (next >= phi.size())
      phi.resize(next + 1);
    
    if (phi[next].increment(word, phi0.prob(word, h), sampler, temperature))
      if (phi0.increment(word, h, sampler, temperature))
	++ h_counts;
    
    // transition
    if (prev >= pi.size())
      pi.resize(prev + 1);
    
    // break sticks further...
    while (next >= beta.size() || prev >= beta.size())
      beta.increment(sampler);
    
    if (pi[prev].increment(next, beta[next], sampler, temperature))
      if (pi0.increment(next, alpha0, sampler, temperature))
	++ counts0;
  }
  
  template <typename Sampler>
  void decrement(const id_type prev, const id_type next, const word_type& word, Sampler& sampler)
  {
    // emission
    if (phi[next].decrement(word, sampler))
      if (phi0.decrement(word, sampler))
	-- h_counts;
    
    // transition
    if (pi[prev].decrement(next, sampler))
      if (pi0.decrement(next, sampler))
	-- counts0;
  }
  
  double prob_emission(const id_type next, const word_type& word) const
  {
    const double p0 = phi0.prob(word, h);
    
    return (next < phi.size() ? phi[next].prob(word, p0) : p0);
  }
  
  double prob_transition(const id_type prev, const id_type next) const
  {
    const double p0 = beta[next];
    
    return (prev < pi.size() ? pi[prev].prob(next, p0) : p0);
  }
  
  double prob_transition(const id_type next) const
  {
    return beta[next];
  }
  
  double log_likelihood() const
  {
    double logprob = std::log(h) * h_counts + std::log(alpha0) * counts0;
    
    logprob += phi0.log_likelihood() + transition0.log_likelihood();
    logprob += pi0.log_likelihood()  + emission0.log_likelihood();
    
    logprob += transition.log_likelihood();
    for (size_type i = 0; i != phi.size(); ++ i)
      logprob += phi[i].log_likelihood();
    
    logprob += emission.log_likelihood();
    for (size_type i = 0; i != pi.size(); ++ i)
      logprob += pi[i].log_likelihood();
    
    return logprob;
  }

  struct greater_table
  {
    greater_table(const table_transition_type& __pi0) : pi0(__pi0) {}

    bool operator()(const size_type& x, const size_type& y) const
    {
      return pi0.size_table(x) > pi0.size_table(y);
    }
    
    const table_transition_type& pi0;
  };
    
  template <typename Mapping>
  void permute(Mapping& mapping)
  {
    // we will sort id by the counts...
    mapping.clear();
    for (size_type i = 0; i != pi0.size(); ++ i)
      mapping.push_back(i);
    
    // we will always "fix" zero for bos/eos
    std::sort(mapping.begin() + 1, mapping.end(), greater_table(pi0));
    
    // re-map ids....
    // actually, the mapping data will be used to re-map the training data...
    
    // re-map for emission...
    emission_type phi_new(phi.size());
    for (size_type i = 0; i != phi.size(); ++ i)
      phi_new[i].swap(phi[mapping[i]]);
    phi_new.swap(phi);
    
    // re-map for transition...
    pi0.permute(mapping);
    pi0.truncate();
    for (size_type i = 0; i != pi.size(); ++ i) {
      pi[i].permute(mapping);
      pi[i].truncate();
    }
    
    emission_type pi_new(pi.size());
    for (size_type i = 0; i != pi.size(); ++ i)
      pi_new[i].swap(pi[mapping[i]]);
    pi.swap(pi);
  }
  
  template <typename Sampler>
  void sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    for (int iter = 0; iter != num_loop; ++ iter) {
      emission0.strength = sample_strength(&phi0, &phi0 + 1, sampler, emission0);
      emission0.discount = sample_discount(&phi0, &phi0 + 1, sampler, emission0);
      
      emission.strength = sample_strength(phi.begin(), phi.end(), sampler, emission);
      emission.discount = sample_discount(phi.begin(), phi.end(), sampler, emission);
      
      transition0.strength = sample_strength(&pi0, &pi0 + 1, sampler, transition0);
      transition0.discount = sample_discount(&pi0, &pi0 + 1, sampler, transition0);
      
      transition.strength = sample_strength(pi.begin(), pi.end(), sampler, transition);
      transition.discount = sample_discount(pi.begin(), pi.end(), sampler, transition);      
    }
    
    phi0.strength() = emission0.strength;
    phi0.discount() = emission0.discount;
    
    for (size_type i = 0; i != phi.size(); ++ i) {
      phi[i].strength() = emission.strength;
      phi[i].discount() = emission.discount;
    }
    
    pi0.strength() = transition0.strength;
    pi0.discount() = transition0.discount;
    
    for (size_type i = 0; i != pi.size(); ++ i) {
      pi[i].strength() = transition.strength;
      pi[i].discount() = transition.discount;
    }
    
    // the transition base... alpha0
    alpha0 = 1.0 / (pi0.size() + 1);
    
    // sample beta from pi0 and alpha0
    std::vector<double, std::allocator<double> > probs(pi0.size());
    for (id_type state = 0; state != pi0.size(); ++ state)
      probs[state] = pi0.prob(state, alpha0);
    
    beta.assign(probs.begin(), probs.end());
  }

  template <typename Iterator, typename Sampler>
  double sample_strength(Iterator first, Iterator last, Sampler& sampler, const parameter_type& param) const
  {
    double x = 0.0;
    double y = 0.0;
    
    for (/**/; first != last; ++ first) {
      x += first->sample_log_x(sampler, param.discount, param.strength);
      y += first->sample_y(sampler, param.discount, param.strength);
    }
    
    return sampler.gamma(param.strength_shape + y, param.strength_rate - x);
  }
  
  template <typename Iterator, typename Sampler>
  double sample_discount(Iterator first, Iterator last, Sampler& sampler, const parameter_type& param) const
  {
    double y = 0.0;
    double z = 0.0;
    
    for (/**/; first != last; ++ first) {
      y += first->sample_y_inv(sampler, param.discount, param.strength);
      z += first->sample_z_inv(sampler, param.discount, param.strength);
    }
    
    return sampler.beta(param.discount_alpha + y, param.discount_beta + z);
  }
  
  double                h;
  size_type             h_counts;
  table_emission_type   phi0;
  emission_type         phi;
  
  double                alpha0;
  size_type             counts0;
  table_transition_type pi0;
  beta_type             beta;
  transition_type       pi;
  
  parameter_type emission0;
  parameter_type emission;
  parameter_type transition0;
  parameter_type transition;
};


struct PYPGraph
{
  typedef PYP::size_type       size_type;
  typedef PYP::difference_type difference_type;
  typedef PYP::id_type         id_type;
  
  typedef std::vector<id_type, std::allocator<id_type> > derivation_type;
  typedef std::vector<double, std::allocator<double> >   cutoff_type;
  
  typedef cicada::semiring::Logprob<double> logprob_type;
  typedef double prob_type;
  
  typedef utils::vector2<double, std::allocator<double> > alpha_type;
  typedef std::vector<double, std::allocator<double> >    scale_type;
  
  
  typedef utils::vector2<double, std::allocator<double> >    phi_type; // emission
  typedef utils::vector2<double, std::allocator<double> >    pi_type;  // transition
  
  typedef std::vector<prob_type, std::allocator<prob_type> > prob_set_type;  
  
  template <typename Sampler>
  void prune(const sentence_type& sentence, const derivation_type& derivation, PYPPOS& model, Sampler& sampler, cutoff_type& cutoff)
  {
    // first, fill-in cutoff-values
    cutoff.clear();
    cutoff.resize(sentence.size() + 2, 0.0);
    
    // we compute threshold based on pi
    double cutoff_min = std::numeric_limits<double>::infinity();
    const size_type T = cutoff.size();
    for (size_type t = 1; t != T - 1; ++ t) {
      cutoff[t] = sampler.model.uniform(0.0, model.prob_transition(derivation[t - 1], derivation[t]));
      cutoff_min = std::min(cutoff_min, cutoff[t]);
    }
    cutoff.back() = 0.0;
    
    // second, check to see if we need more classes!
    // we check cutoff_min to see if we have enough sticks...
    const size_type K = model.beta.size();
    double pi_max = - std::numeric_limits<double>::infinity();
    for (size_type prev = 0; prev != K; ++ prev) {
      double pi_min = std::numeric_limits<double>::infinity();
      for (size_type next = 0; next != K; ++ next)
	pi_min = std::min(pi_min, model.prob_transition(prev, next));
      
      pi_max = std::max(pi_max, pi_min);
    }

    while (pi_max > cutoff_min) {
      model.beta.increment(sampler);
      
      const size_type K = model.beta.size();
      for (size_type k = 0; k != K; ++ k)
	pi_max = std::min(pi_max, model.prob_transition(k, K - 1));
    }
  }

  void initialize(const sentence_type& sentence, const PYPPOS& model)
  {
    
  }
  
  logprob_type forward(const sentence_type& sentence, const PYPPOS& model, const cutoff_type& cutoff)
  {
    const size_type T = alpha.size1();
    const size_type K = alpha.size2();

    initialize(sentence, model);
    
    logprob_type logsum;
    for (size_type t = 1; t != T; ++ t) {
      for (size_type prev = 0; prev != K; ++ prev)
	for (size_type next = 0; next != K; ++ next)
	  if (pi(prev, next) > cutoff[t])
	    alpha(t, next) += alpha(t - 1, prev) * pi(prev, next) * phi(next, t);
      
      scale[t] = std::accumulate(alpha.begin(t), alpha.end(t), 0.0);
      scale[t] = (scale[t] == 0.0 ? 1.0 : 1.0 / scale[t]);
      if (scale[t] != 1.0)
	std::transform(alpha.begin(t), alpha.end(t), alpha.begin(t), std::bind2nd(std::multiplies<double>(), scale[t]));

      logsum += scale[t];
    }
    
    return logsum;
  }
  
  template <typename Sampler>
  logprob_type backward(Sampler& sampler, derivation_type& derivation)
  {
    const size_type T = alpha.size1();
    const size_type K = alpha.size2();
    
    logprob_type logprob = cicada::semiring::traits<logprob_type>::one();
    derivation.clear();
    derivation.push_back(0);
    
    id_type state = 0;
    for (size_type t = T - 1; t > 0; -- t) {
      const bool adjust = (t - 1 != 0);
      
      probs.clear();
      for (size_type prev = adjust; prev != K; ++ prev)
	probs.push_back(alpha(t - 1, prev) * pi(prev, state) * phi(state, t));

      prob_set_type::const_iterator piter = sampler.draw(probs.begin(), probs.end());
      
      state = (piter - probs.begin()) + adjust;
      
      logprob += *piter / alpha(t - 1, state);
    }
    
    derivation.push_back(0);

    std::reverse(derivation.begin(), derivation.end());
    
    return logprob;
  }
  
  phi_type    phi;
  pi_type     pi;
  alpha_type  alpha;
  scale_type  scale;

  prob_set_type    probs;
};

typedef boost::filesystem::path path_type;
typedef std::vector<path_type, std::allocator<path_type> > path_set_type;

typedef utils::sampler<boost::mt19937> sampler_type;

path_set_type train_files;
path_set_type test_files;
path_type     output_file;

int classes = 16;

int samples = 30;
int baby_steps = 0;
int anneal_steps = 0;
int resample_rate = 1;
int resample_iterations = 2;
bool slice_sampling = false;

double emission_discount = 0.9;
double emission_strength = 1;

double emission_discount_prior_alpha = 1.0;
double emission_discount_prior_beta  = 1.0;
double emission_strength_prior_shape = 1.0;
double emission_strength_prior_rate  = 1.0;

double transition_discount = 0.9;
double transition_strength = 1;

double transition_discount_prior_alpha = 1.0;
double transition_discount_prior_beta  = 1.0;
double transition_strength_prior_shape = 1.0;
double transition_strength_prior_rate  = 1.0;

int threads = 1;
int debug = 0;

void options(int argc, char** argv);

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);
    
    threads = utils::bithack::max(threads, 1);
    
    if (samples < 0)
      throw std::runtime_error("# of samples must be positive");
            
    if (resample_rate <= 0)
      throw std::runtime_error("resample rate must be >= 1");
    
    if (train_files.empty())
      throw std::runtime_error("no training data?");
    
    if (! slice_sampling && (emission_strength < 0.0 || transition_strength < 0.0))
      throw std::runtime_error("negative strength w/o slice sampling is not supported!");

  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}


void options(int argc, char** argv)
{
  namespace po = boost::program_options;
  
  po::variables_map variables;
  
  po::options_description desc("options");
  desc.add_options()
    ("train", po::value<path_set_type>(&train_files)->multitoken(), "train file(s)")
    ("test",  po::value<path_set_type>(&test_files)->multitoken(),  "test file(s)")
    ("output", po::value<path_type>(&output_file), "output file")

    ("classes", po::value<int>(&classes)->default_value(classes), "# of initial classes")
    
    ("samples",             po::value<int>(&samples)->default_value(samples),                         "# of samples")
    ("baby-steps",          po::value<int>(&baby_steps)->default_value(baby_steps),                   "# of baby steps")
    ("anneal-steps",        po::value<int>(&anneal_steps)->default_value(anneal_steps),               "# of anneal steps")
    ("resample",            po::value<int>(&resample_rate)->default_value(resample_rate),             "hyperparameter resample rate")
    ("resample-iterations", po::value<int>(&resample_iterations)->default_value(resample_iterations), "hyperparameter resample iterations")
    
    ("slice",               po::bool_switch(&slice_sampling),                                         "slice sampling for hyperparameters")
    
    ("emission-discount",       po::value<double>(&emission_discount)->default_value(emission_discount),                         "discount ~ Beta(alpha,beta)")
    ("emission-discount-alpha", po::value<double>(&emission_discount_prior_alpha)->default_value(emission_discount_prior_alpha), "discount ~ Beta(alpha,beta)")
    ("emission-discount-beta",  po::value<double>(&emission_discount_prior_beta)->default_value(emission_discount_prior_beta),   "discount ~ Beta(alpha,beta)")

    ("emission-strength",       po::value<double>(&emission_strength)->default_value(emission_strength),                         "strength ~ Gamma(shape,rate)")
    ("emission-strength-shape", po::value<double>(&emission_strength_prior_shape)->default_value(emission_strength_prior_shape), "strength ~ Gamma(shape,rate)")
    ("emission-strength-rate",  po::value<double>(&emission_strength_prior_rate)->default_value(emission_strength_prior_rate),   "strength ~ Gamma(shape,rate)")

    ("transition-discount",       po::value<double>(&transition_discount)->default_value(transition_discount),                         "discount ~ Beta(alpha,beta)")
    ("transition-discount-alpha", po::value<double>(&transition_discount_prior_alpha)->default_value(transition_discount_prior_alpha), "discount ~ Beta(alpha,beta)")
    ("transition-discount-beta",  po::value<double>(&transition_discount_prior_beta)->default_value(transition_discount_prior_beta),   "discount ~ Beta(alpha,beta)")

    ("transition-strength",       po::value<double>(&transition_strength)->default_value(transition_strength),                         "strength ~ Gamma(shape,rate)")
    ("transition-strength-shape", po::value<double>(&transition_strength_prior_shape)->default_value(transition_strength_prior_shape), "strength ~ Gamma(shape,rate)")
    ("transition-strength-rate",  po::value<double>(&transition_strength_prior_rate)->default_value(transition_strength_prior_rate),   "strength ~ Gamma(shape,rate)")
    
    ("threads", po::value<int>(&threads), "# of threads")
    
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");

  po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);
  
  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc << std::endl;
    exit(0);
  }
}

