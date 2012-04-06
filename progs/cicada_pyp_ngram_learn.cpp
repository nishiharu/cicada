//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

// PYP-LM!

//
// @InProceedings{teh:2006:COLACL,
//   author    = {Teh, Yee Whye},
//   title     = {A Hierarchical Bayesian Language Model Based On Pitman-Yor Processes},
//   booktitle = {Proceedings of the 21st International Conference on Computational Linguistics and 44th Annual Meeting of the Association for Computational Linguistics},
//   month     = {July},
//   year      = {2006},
//   address   = {Sydney, Australia},
//   publisher = {Association for Computational Linguistics},
//   pages     = {985--992},
//   url       = {http://www.aclweb.org/anthology/P06-1124},
//   doi       = {10.3115/1220175.1220299}
// }
//

#include <map>
#include <iterator>
#include <numeric>

#include <cicada/sentence.hpp>
#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>

#include "utils/resource.hpp"
#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/mathop.hpp"
#include "utils/bithack.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/restaurant.hpp"
#include "utils/unordered_map.hpp"
#include "utils/unordered_set.hpp"
#include "utils/compact_trie_dense.hpp"
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

struct PYPLM
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  typedef uint32_t  id_type;

  typedef boost::filesystem::path path_type;

  struct Node
  {
    typedef utils::restaurant<word_type, boost::hash<word_type>, std::equal_to<word_type>,
			      std::allocator<word_type > > table_type;
  
    Node() : table(), parent(id_type(-1)), order(0)  {}
  
    table_type table;
    id_type parent;
    int     order;
  };
  typedef Node node_type;
  
  typedef utils::compact_trie_dense<word_type, node_type, boost::hash<word_type>, std::equal_to<word_type>,
				    std::allocator<std::pair<const word_type, node_type> > > trie_type;

  typedef std::vector<double, std::allocator<double> > parameter_set_type;

  typedef std::vector<id_type, std::allocator<id_type> > node_set_type;
  typedef std::vector<node_set_type, std::allocator<node_set_type> > node_map_type;

  typedef std::pair<size_type, size_type> order_count_type;
  typedef std::vector<order_count_type, std::allocator<order_count_type> > order_count_set_type;
  
  typedef std::vector<id_type, std::allocator<id_type> > history_type;
  typedef std::vector<double, std::allocator<double> > prob_set_type;
  typedef std::vector<prob_set_type, std::allocator<double> > prob_map_type;
  
  PYPLM(const int order,
	const double __p0,
	const double __discount,
	const double __strength,
	const double __discount_alpha,
	const double __discount_beta,
	const double __strength_shape,
	const double __strength_rate,
	const double __order_alpha,
	const double __order_beta,
	const bool __infinite=false)
    : trie(word_type()),
      nodes(order),
      discount(order, __discount),
      strength(order, __strength),
      discount_alpha(__discount_alpha),
      discount_beta(__discount_beta),
      strength_shape(__strength_shape),
      strength_rate(__strength_rate),
      p0(__p0),
      counts0(0),
      orders(order),
      order_alpha(__order_alpha),
      order_beta(__order_beta),
      infinite(__infinite)
  {
    // unitialize root table...
    root.parent = id_type(-1);
    root.order = 0;
    root.table = node_type::table_type(discount[0], strength[0]);
  }

  template <typename Iterator>
  id_type insert(Iterator first, Iterator last)
  {
    typedef std::reverse_iterator<Iterator> reverse_iterator;
    
    reverse_iterator begin(last);
    reverse_iterator end(first);
    
    int order = 1;
    id_type node = trie.root();
    for (reverse_iterator iter = begin; iter != end; ++ iter, ++ order) {
      const id_type node_prev = node;
      
      node = trie.insert(node, *iter);
      
      node_type& trie_node = trie[node];
      
      if (! trie_node.order) {
	trie_node.parent = node_prev;
	trie_node.order = order;
	trie_node.table = node_type::table_type(discount[order], strength[order]);
	
	nodes[order].push_back(node);
      }
    }
    
    return node;
  }
  
  template <typename Sampler>
  bool increment(const word_type& word, const id_type& node, int& order, Sampler& sampler, const double temperature=1.0)
  {    
    orders_cache.clear();
    
    if (node == trie.root()) {
      ++ orders[0].first;
      order = 1;
      
      if (root.table.increment(word, p0, sampler, temperature)) {
	++ counts0;
	return true;
      } else
	return false;
    } else {
      id_type parent = node;
      history.clear();
      probs.clear();
      
      while (parent != trie.root()) {
	history.push_back(parent);
	parent = trie[parent].parent;
      }
      history.push_back(parent);
      
      double prob_ngram = p0;
      double backoff_n = 1.0;
      
      int n = 0;
      history_type::const_reverse_iterator hiter_begin = history.rbegin();
      history_type::const_reverse_iterator hiter_end = history.rend();
      for (history_type::const_reverse_iterator hiter = hiter_begin; hiter != hiter_end; ++ hiter, ++ n) {
	if (*hiter == trie.root())
	  prob_ngram = root.table.prob(word, prob_ngram);
	else if (! trie[*hiter].table.empty())
	  prob_ngram = trie[*hiter].table.prob(word, prob_ngram);
	
	const double prob_n_denom = (orders[n].first + orders[n].second + order_alpha + order_beta);
	const double prob_n = backoff_n * (orders[n].first + order_alpha) / prob_n_denom;
	
	probs.push_back(prob_n * prob_ngram);
	
	backoff_n *= double(orders[n].second + order_beta) / prob_n_denom;
      }
      
      prob_set_type::const_iterator piter = sampler.draw(probs.begin(), probs.end(), temperature);
      
      order = (piter - probs.begin()) + 1;
      
      for (int n = 0; n < order - 1; ++ n)
	++ orders[n].second;
      ++ orders[order - 1].first;
      
      return increment(word, *(hiter_begin + order - 1), sampler, temperature);
    }
    
    return true;
  }

  template <typename Sampler>
  bool increment(const word_type& word, const id_type& node, Sampler& sampler, const double temperature=1.0)
  {
    if (node == trie.root()) {
      if (root.table.increment(word, p0, sampler, temperature))
	++ counts0;
      else
	return false;
    } else {
      const double backoff = prob(word, trie[node].parent);
      
      // we will also increment lower-order when new table is created!
      if (trie[node].table.increment(word, backoff, sampler, temperature))
	increment(word, trie[node].parent, sampler, temperature);
      else
	return false;
    }
      
    return true;
  }

  template <typename Sampler>
  bool decrement(const word_type& word, id_type node, int order, Sampler& sampler)
  {
    orders_cache.clear();
    
    // move at the right position...
    while (node != trie.root() && trie[node].order != order - 1)
      node = trie[node].parent;
    
    if (node == trie.root()) {
      // penetration count
      -- orders[0].first;
      
      if (root.table.decrement(word, sampler))
	-- counts0;
      else
	return false;
    } else {
      // penetration count
      for (int n = 0; n < order - 1; ++ n)
	-- orders[n].second;
      -- orders[order - 1].first;

      if (trie[node].table.decrement(word, sampler))
	decrement(word, trie[node].parent, sampler);
      else
	return false;
    }
    return true;
  }
  
  template <typename Sampler>
  bool decrement(const word_type& word, const id_type& node, Sampler& sampler)
  {
    if (node == trie.root()) {
      if (root.table.decrement(word, sampler))
	-- counts0;
      else
	return false;
    } else {
      if (trie[node].table.decrement(word, sampler))
	decrement(word, trie[node].parent, sampler);
      else
	return false;
    }
      
    return true;
  }
  
  double prob(const word_type& word, const id_type& node) const
  {
    if (node == trie.root())
      return root.table.prob(word, p0);
    else {
      const double p = prob(word, trie[node].parent);
      
      if (trie[node].table.empty())
	return p;
      else
	return trie[node].table.prob(word, p);
    }
  }

  template <typename Iterator>
  double prob(const word_type& word, Iterator first, Iterator last)
  {
    typedef std::reverse_iterator<Iterator> reverse_iterator;
    
    // this may happen when accessing 0-gram!
    if (! (first <= last))
      return p0;
    
    if (infinite) {
#if 1
      double p = root.table.prob(word, p0);
      
      // we will traverse from the back!
      reverse_iterator begin(last);
      reverse_iterator end(first);
      
      id_type node = trie.root();
      for (reverse_iterator iter = begin; iter != end; ++ iter) {
	node = trie.find(node, *iter);
	
	if (node == trie_type::npos() || trie[node].table.empty())
	  return p;
	else
	  p = trie[node].table.prob(word, p);
      }
      
      return p;
#endif
#if 0
      if (orders_cache.empty()) {
	orders_cache.resize(orders.size());
	orders_cache.back().resize(orders.size());
	
	double backoff_n = 1.0;
	for (size_t n = 0; n != orders.size(); ++ n) {
	  const double prob_n_denom = (orders[n].first + orders[n].second + order_alpha + order_beta);
	  const double prob_n = backoff_n * (orders[n].first + order_alpha) / prob_n_denom;
	  
	  orders_cache.back()[n] = prob_n;
	  
	  backoff_n *= double(orders[n].second + order_beta) / prob_n_denom;
	}
	
	for (size_t n = 0; n != orders.size(); ++ n) {
	  orders_cache[n] = prob_set_type(orders_cache.back().begin(), orders_cache.back().begin() + n + 1);
	  
	  const double sum = std::accumulate(orders_cache[n].begin(), orders_cache[n].end(), 0.0);
	  if (sum != 0.0)
	    std::transform(orders_cache[n].begin(), orders_cache[n].end(), orders_cache[n].begin(), std::bind2nd(std::multiplies<double>(), 1.0 / sum));
	}
      }
      
      const int n_max = std::distance(first, last);
      
      int n = 0;
      double p = root.table.prob(word, p0);
      double prob = p * orders_cache[n_max][n];
      
      // we will traverse from the back!
      reverse_iterator begin(last);
      reverse_iterator end(first);
      
      id_type node = trie.root();
      for (reverse_iterator iter = begin; iter != end; ++ iter, ++ n) {
	node = trie.find(node, *iter);
	
	if (node != trie_type::npos() && ! trie[node].table.empty())
	  p = trie[node].table.prob(word, p);
	
	prob += p * orders_cache[n_max][n];
      }
      
      return prob;
#endif
    } else {
      double p = root.table.prob(word, p0);
      
      // we will traverse from the back!
      reverse_iterator begin(last);
      reverse_iterator end(first);
      
      id_type node = trie.root();
      for (reverse_iterator iter = begin; iter != end; ++ iter) {
	node = trie.find(node, *iter);
	
	if (node == trie_type::npos() || trie[node].table.empty())
	  return p;
	else
	  p = trie[node].table.prob(word, p);
      }
      
      return p;
    }
  }
  
  double log_likelihood() const
  {
    double logprob = std::log(p0) * counts0;
    
    for (size_type order = 0; order != discount.size(); ++ order)
      logprob += log_likelihood(order, discount[order], strength[order]);
    
    return logprob;
  }
  
  double log_likelihood(const int order, const double& discount, const double& strength) const
  {
    if (strength <= - discount) return - std::numeric_limits<double>::infinity();
    
    double logprob = (utils::mathop::log_beta_density(discount, discount_alpha, discount_beta)
		      + utils::mathop::log_gamma_density(strength + discount, strength_shape, strength_rate));
    
    if (order == 0)
      return logprob + (! root.table.empty() ? root.table.log_likelihood(discount, strength) : 0.0);
    else {
      node_set_type::const_iterator niter_end = nodes[order].end();
      for (node_set_type::const_iterator niter = nodes[order].begin(); niter != niter_end; ++ niter)
	if (! trie[*niter].table.empty())
	  logprob += trie[*niter].table.log_likelihood(discount, strength);
      
      return logprob;
    }
  }
  
  struct DiscountSampler
  {
    DiscountSampler(const PYPLM& __pyplm, const int __order) : pyplm(__pyplm), order(__order) {}
    
    const PYPLM& pyplm;
    int order;
    
    double operator()(const double& proposed_discount) const
    {
      return pyplm.log_likelihood(order, proposed_discount, pyplm.strength[order]);
    }
  };
  
  struct StrengthSampler
  {
    StrengthSampler(const PYPLM& __pyplm, const int __order) : pyplm(__pyplm), order(__order) {}
    
    const PYPLM& pyplm;
    int order;
    
    double operator()(const double& proposed_strength) const
    {
      return pyplm.log_likelihood(order, pyplm.discount[order], proposed_strength);
    }
  };
  
  template <typename Sampler>
  void sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    for (size_type order = 0; order != discount.size(); ++ order) {
      
      for (int iter = 0; iter != num_loop; ++ iter) {
	strength[order] = sample_strength(order, sampler, discount[order], strength[order]);
	
	discount[order] = sample_discount(order, sampler, discount[order], strength[order]);
      }
      
      strength[order] = sample_strength(order, sampler, discount[order], strength[order]);
      
      if (order == 0) {
	root.table.discount() = discount[order];
	root.table.strength() = strength[order];

	root.table.verify_parameters();
      } else {
	node_set_type::const_iterator niter_end = nodes[order].end();
	for (node_set_type::const_iterator niter = nodes[order].begin(); niter != niter_end; ++ niter) {
	  trie[*niter].table.discount() = discount[order];
	  trie[*niter].table.strength() = strength[order];
	  
	  trie[*niter].table.verify_parameters();
	}
      }
    }
  }

  template <typename Sampler>
  double sample_strength(const int order, Sampler& sampler, const double& discount, const double& strength) const
  {
    double x = 0.0;
    double y = 0.0;

    if (order == 0) {
      x += root.table.sample_log_x(sampler, discount, strength);
      y += root.table.sample_y(sampler, discount, strength);
    } else {
      node_set_type::const_iterator niter_end = nodes[order].end();
      for (node_set_type::const_iterator niter = nodes[order].begin(); niter != niter_end; ++ niter)
	if (! trie[*niter].table.empty()) {
	  x += trie[*niter].table.sample_log_x(sampler, discount, strength);
	  y += trie[*niter].table.sample_y(sampler, discount, strength);
	}
    }
    
    return sampler.gamma(strength_shape + y, strength_rate - x);
  }
  
  template <typename Sampler>
  double sample_discount(const int order, Sampler& sampler, const double& discount, const double& strength) const
  {
    double y = 0.0;
    double z = 0.0;
    
    if (order == 0) {
      y += root.table.sample_y_inv(sampler, discount, strength);
      z += root.table.sample_z_inv(sampler, discount, strength);
    } else {
      node_set_type::const_iterator niter_end = nodes[order].end();
      for (node_set_type::const_iterator niter = nodes[order].begin(); niter != niter_end; ++ niter)
	if (! trie[*niter].table.empty()) {
	  y += trie[*niter].table.sample_y_inv(sampler, discount, strength);
	  z += trie[*niter].table.sample_z_inv(sampler, discount, strength);
	}
    }
    
    return sampler.beta(discount_alpha + y, discount_beta + z);
  }
  
  
  template <typename Sampler>
  void slice_sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    for (size_type order = 0; order != discount.size(); ++ order) {
      DiscountSampler discount_sampler(*this, order);
      StrengthSampler strength_sampler(*this, order);

      for (int iter = 0; iter != num_loop; ++ iter) {
	strength[order] = utils::slice_sampler(strength_sampler,
					       strength[order],
					       sampler,
					       - discount[order] + std::numeric_limits<double>::min(),
					       std::numeric_limits<double>::infinity(),
					       0.0,
					       num_iterations,
					       100 * num_iterations);
	
	discount[order] = utils::slice_sampler(discount_sampler,
					       discount[order],
					       sampler,
					       (strength[order] < 0.0 ? - strength[order] : 0.0) + std::numeric_limits<double>::min(),
					       1.0,
					       0.0,
					       num_iterations,
					       100 * num_iterations);
      }
      
      strength[order] = utils::slice_sampler(strength_sampler,
					     strength[order],
					     sampler,
					     - discount[order] + std::numeric_limits<double>::min(),
					     std::numeric_limits<double>::infinity(),
					     0.0,
					     num_iterations,
					     100 * num_iterations);
      
      if (order == 0) {
	root.table.discount() = discount[order];
	root.table.strength() = strength[order];

	root.table.verify_parameters();
      } else {
	node_set_type::const_iterator niter_end = nodes[order].end();
	for (node_set_type::const_iterator niter = nodes[order].begin(); niter != niter_end; ++ niter) {
	  trie[*niter].table.discount() = discount[order];
	  trie[*niter].table.strength() = strength[order];
	  
	  trie[*niter].table.verify_parameters();
	}
      }
    }
  }

  void write(const path_type& path)
  {
    typedef utils::repository repository_type;
    typedef std::vector<id_type, std::allocator<id_type> > node_set_type;

    typedef uint64_t count_type;

    typedef boost::fusion::tuple<id_type, count_type, count_type> data_type;

    typedef std::map<word_type, data_type, std::less<word_type>, std::allocator<std::pair<const word_type, data_type> >  > word_set_type;
    typedef utils::succinct_vector<std::allocator<int32_t> > position_set_type;
    typedef std::vector<count_type, std::allocator<count_type> > offset_set_type;
      
    repository_type rep(path, repository_type::write);
      
    rep["order"] = boost::lexical_cast<std::string>(discount.size());

    rep["p0"]      = boost::lexical_cast<std::string>(p0);
    rep["counts0"] = boost::lexical_cast<std::string>(counts0);
    
    rep["discount-alpha"] = boost::lexical_cast<std::string>(discount_alpha);
    rep["discount-beta"]  = boost::lexical_cast<std::string>(discount_beta);
    rep["strength-shape"] = boost::lexical_cast<std::string>(strength_shape);
    rep["strength-rate"]  = boost::lexical_cast<std::string>(strength_rate);
          
    for (size_type order = 0; order != discount.size(); ++ order) {
      rep["discount" + boost::lexical_cast<std::string>(order)] = boost::lexical_cast<std::string>(discount[order]);
      rep["strength" + boost::lexical_cast<std::string>(order)] = boost::lexical_cast<std::string>(strength[order]);
    }
    
    // we will compute on-memory for faster indexing... (and assuming small data!)
    
    boost::iostreams::filtering_ostream os_index;
    boost::iostreams::filtering_ostream os_count;
    boost::iostreams::filtering_ostream os_total;
    
    os_index.push(utils::packed_sink<word_type::id_type, std::allocator<word_type::id_type> >(rep.path("index")));
    os_count.push(utils::packed_sink<count_type, std::allocator<count_type> >(rep.path("count")));
    os_total.push(utils::packed_sink<count_type, std::allocator<count_type> >(rep.path("total")));
    
    // dump total counts!!!!!!!
      
    position_set_type positions;
    offset_set_type   offsets(1, 0);
    count_type        offset = 0;
    
    node_set_type nodes;
    node_set_type nodes_next;
    
    word_set_type words;
    
    // unigram!
    {
      const count_type count_customer = root.table.size_customer();
      const count_type count_table    = root.table.size_table();
      
      os_total.write((char*) &count_customer, sizeof(count_type));
      os_total.write((char*) &count_table, sizeof(count_type));

      node_type::table_type::const_iterator titer_end = root.table.end();
      for (node_type::table_type::const_iterator titer = root.table.begin(); titer != titer_end; ++ titer)
	words.insert(std::make_pair(titer->first, data_type(trie_type::npos(),
							    titer->second.size_customer(),
							    titer->second.size_table())));
      
      trie_type::const_iterator iter_end = trie.end();
      for (trie_type::const_iterator iter = trie.begin(); iter != iter_end; ++ iter)
	if (! trie[iter->second].table.empty()) {
	  std::pair<word_set_type::iterator, bool> result = words.insert(std::make_pair(iter->first, data_type(iter->second, 0, 0)));
	  if (! result.second)
	    boost::fusion::get<0>(result.first->second) = iter->second;
	}
      
      word_set_type::const_iterator witer_end = words.end();
      for (word_set_type::const_iterator witer = words.begin(); witer != witer_end; ++ witer) {
	nodes.push_back(boost::fusion::get<0>(witer->second));
	
	word_type::id_type word_id = witer->first.id();
	os_index.write((char*) &word_id, sizeof(word_type::id_type));
	os_count.write((char*) &boost::fusion::get<1>(witer->second), sizeof(count_type));
	os_count.write((char*) &boost::fusion::get<2>(witer->second), sizeof(count_type));
      }
      
      offset += words.size();
      offsets.push_back(offset);
    }
    
    
    for (size_type order = 1; order < discount.size(); ++ order) {
      nodes_next.clear();
      
      node_set_type::const_iterator niter_end = nodes.end();
      for (node_set_type::const_iterator niter = nodes.begin(); niter != niter_end; ++ niter) {
	if (*niter == trie_type::npos()) {
	  const count_type count_customer = 0;
	  const count_type count_table    = 0;
	  
	  os_total.write((char*) &count_customer, sizeof(count_type));
	  os_total.write((char*) &count_table, sizeof(count_type));
	  
	  positions.set(positions.size(), false); // we will set bit!
	} else {
	  const node_type& node = trie[*niter];
	  trie_type::const_iterator iter_begin = trie.begin(*niter);
	  trie_type::const_iterator iter_end   = trie.end(*niter);
	  
	  const count_type count_customer = node.table.size_customer();
	  const count_type count_table    = node.table.size_table();
	  
	  os_total.write((char*) &count_customer, sizeof(count_type));
	  os_total.write((char*) &count_table, sizeof(count_type));

	  words.clear();
	  
	  // we have an entry in the model
	  node_type::table_type::const_iterator titer_end = node.table.end();
	  for (node_type::table_type::const_iterator titer = node.table.begin(); titer != titer_end; ++ titer)
	    words.insert(std::make_pair(titer->first, data_type(trie_type::npos(),
								titer->second.size_customer(),
								titer->second.size_table())));
	  
	  // or, we have an entry with the longer contexts..
	  for (trie_type::const_iterator iter = iter_begin; iter != iter_end; ++ iter)
	    if (! trie[iter->second].table.empty()) {
	      std::pair<word_set_type::iterator, bool> result = words.insert(std::make_pair(iter->first, data_type(iter->second, 0, 0)));
	      if (! result.second)
		boost::fusion::get<0>(result.first->second) = iter->second;
	    }
	  
	  word_set_type::const_iterator witer_end = words.end();
	  for (word_set_type::const_iterator witer = words.begin(); witer != witer_end; ++ witer) {
	    nodes_next.push_back(boost::fusion::get<0>(witer->second));
	    
	    word_type::id_type word_id = witer->first.id();
	    os_index.write((char*) &word_id, sizeof(word_type::id_type));
	    os_count.write((char*) &boost::fusion::get<1>(witer->second), sizeof(count_type));
	    os_count.write((char*) &boost::fusion::get<2>(witer->second), sizeof(count_type));
	    
	    positions.set(positions.size(), true);
	  }
	  positions.set(positions.size(), false);
	  
	  offset += words.size();
	}
      }
      
      offsets.push_back(offset);
      
      nodes.swap(nodes_next);
    }
    
    // dump position
    positions.write(rep.path("position"));
    
    // dump offsets
    for (size_type order = 1; order != offsets.size(); ++ order)
      rep[boost::lexical_cast<std::string>(order) + "-gram-offset"] = boost::lexical_cast<std::string>(offsets[order]);
    
    // vocabulary...
    word_type::write(rep.path("vocab"));
  }

public: 
  trie_type trie;
  node_type root;
  node_map_type nodes;
  
  parameter_set_type discount;
  parameter_set_type strength;
  
  double discount_alpha;
  double discount_beta;
  double strength_shape;
  double strength_rate;
  
  double    p0;
  size_type counts0;
  
  order_count_set_type orders;
  prob_map_type        orders_cache;
  double order_alpha;
  double order_beta;

  // global... should be moved to thread specific...
  history_type  history;
  prob_set_type probs;
  
  
  bool infinite;
};


typedef boost::filesystem::path path_type;
typedef std::vector<path_type, std::allocator<path_type> > path_set_type;

typedef utils::sampler<boost::mt19937> sampler_type;

template <typename Tp>
struct less_data
{
  bool operator()(const Tp& x, const Tp& y) const
  {
    return (boost::fusion::get<2>(x) < boost::fusion::get<2>(y)
	    || (!(boost::fusion::get<2>(y) < boost::fusion::get<2>(x))
		&& (boost::fusion::get<1>(x) < boost::fusion::get<1>(y)
		    || (!(boost::fusion::get<1>(y) < boost::fusion::get<1>(x))
			&& boost::fusion::get<0>(x) < boost::fusion::get<0>(y)))));
  }
};

path_set_type train_files;
path_set_type test_files;
path_type     output_file;

int order = 4;
int samples = 30;
int baby_steps = 0;
int anneal_steps = 0;
int resample_rate = 1;
int resample_iterations = 1;
bool slice_sampling = false;
bool infinite = false;

double discount = 0.9;
double strength = 1;

double discount_prior_alpha = 1.0;
double discount_prior_beta  = 1.0;
double strength_prior_shape = 1.0;
double strength_prior_rate  = 1.0;

double order_alpha = 1.0;
double order_beta = 1.0;

int threads = 1;
int debug = 0;

size_t vocabulary_size(const path_set_type& files);

void options(int argc, char** argv);

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);
    
    threads = utils::bithack::max(threads, 1);
    
    if (order <= 0)
      throw std::runtime_error("order must be positive");
    
    if (samples < 0)
      throw std::runtime_error("# of samples must be positive");
            
    if (resample_rate <= 0)
      throw std::runtime_error("resample rate must be >= 1");

    if (train_files.empty())
      throw std::runtime_error("no training data?");
    
    if (! slice_sampling && strength < 0.0)
      throw std::runtime_error("negative strength w/o slice sampling is not supported!");

    sampler_type sampler;
    const size_t num_vocab = vocabulary_size(train_files);
    
    PYPLM lm(order,
	     1.0 / num_vocab,
	     discount,
	     strength,
	     discount_prior_alpha,
	     discount_prior_beta,
	     strength_prior_shape,
	     strength_prior_rate,
	     order_alpha,
	     order_beta,
	     infinite);
    
    // we will precompute <word, node> pair...
    typedef boost::fusion::tuple<word_type, PYPLM::id_type, int> data_type;
    typedef std::vector<data_type, std::allocator<data_type> > data_set_type;
    typedef std::vector<bool, std::allocator<bool> > non_oov_type;

    typedef utils::unordered_set<word_type, boost::hash<word_type>, std::equal_to<word_type>, std::allocator<word_type> >::type word_unique_type;
    typedef utils::chunk_vector<word_unique_type, 4096 / sizeof(word_unique_type), std::allocator<word_unique_type> > word_unique_set_type;
    typedef std::vector<size_t, std::allocator<size_t> > index_type;

    data_set_type        training;
    word_unique_set_type uniques;
    non_oov_type non_oov;
    
    if (vocab_type::BOS.id() >= non_oov.size())
      non_oov.resize(vocab_type::BOS.id() + 1, false);
    if (vocab_type::EOS.id() >= non_oov.size())
      non_oov.resize(vocab_type::EOS.id() + 1, false);
    
    non_oov[vocab_type::BOS.id()] = true;
    non_oov[vocab_type::EOS.id()] = true;
    
    for (path_set_type::const_iterator fiter = train_files.begin(); fiter != train_files.end(); ++ fiter) {
      utils::compress_istream is(*fiter, 1024 * 1024);
      
      sentence_type sentence;
      sentence_type ngram(1, vocab_type::BOS);
      
      while (is >> sentence) {
	ngram.resize(1);
	
	sentence_type::const_iterator siter_end = sentence.end();
	for (sentence_type::const_iterator siter = sentence.begin(); siter != siter_end; ++ siter) {
	  training.push_back(data_type(*siter, lm.insert(std::max(ngram.begin(), ngram.end() - order + 1), ngram.end()), 0));
	  
	  if (boost::fusion::get<1>(training.back()) >= uniques.size())
	    uniques.resize(boost::fusion::get<1>(training.back()) + 1);
	  uniques[boost::fusion::get<1>(training.back())].insert(boost::fusion::get<0>(training.back()));
	  
	  ngram.push_back(*siter);
	  
	  if (siter->id() >= non_oov.size())
	    non_oov.resize(siter->id() + 1, false);
	  non_oov[siter->id()] = true;
	}
	
	training.push_back(data_type(vocab_type::EOS, lm.insert(std::max(ngram.begin(), ngram.end() - order + 1), ngram.end()), 0));
	
	if (boost::fusion::get<1>(training.back()) >= uniques.size())
	  uniques.resize(boost::fusion::get<1>(training.back()) + 1);
	uniques[boost::fusion::get<1>(training.back())].insert(boost::fusion::get<0>(training.back()));
      }
    }

    if (training.empty())
      throw std::runtime_error("no training data?");
    
    data_set_type(training).swap(training);

    
    // assign rank...
    {
      data_set_type::iterator titer_end = training.end();
      for (data_set_type::iterator titer = training.begin(); titer != titer_end; ++ titer)
	boost::fusion::get<2>(*titer) = uniques[boost::fusion::get<1>(*titer)].size();
      
      uniques.clear();
    }
    
    // sort by rank
    std::sort(training.begin(), training.end(), less_data<data_type>());
    
    // compute index
    index_type index(1, 0);
    
    data_set_type::const_iterator titer_begin = training.begin();
    data_set_type::const_iterator titer_end   = training.end();
    for (data_set_type::const_iterator titer = titer_begin; titer != titer_end; ++ titer)
      if (boost::fusion::get<2>(*titer) != boost::fusion::get<2>(training[index.back()]))
	index.push_back(titer - titer_begin);
    
    index.push_back(training.size());
    
    if (debug >= 2)
      std::cerr << "# of baby step levels: " << (index.size() - 1) << std::endl;
    
    // sample parameters, first...
    if (slice_sampling)
      lm.slice_sample_parameters(sampler, resample_iterations);
    else
      lm.sample_parameters(sampler, resample_iterations);
    
    if (debug >= 2)
      for (int n = 0; n != order; ++ n)
	std::cerr << "order=" << n << " discount=" << lm.discount[n] << " strength=" << lm.strength[n] << std::endl;
    
    {
      data_set_type::iterator titer_end = training.end();
      for (data_set_type::iterator titer = training.begin(); titer != titer_end; ++ titer)
	boost::fusion::get<2>(*titer) = false;
    }
    
    size_t baby_index = 0;
    size_t baby_iter = utils::bithack::branch(baby_steps > 0, size_t(0), index.back());
    const size_t baby_last = index.back();
    const size_t baby_size = (baby_steps > 0 ? (index.back() + (baby_steps - 1)) / baby_steps : size_t(0));
    
    size_t anneal_iter = 0;
    const size_t anneal_last = utils::bithack::branch(anneal_steps > 0, anneal_steps, 0);
      
    data_set_type training_samples;
    
    if (baby_iter == baby_last)
      training_samples = training;
    else
      training_samples.reserve(training.size());
      
    bool sampling = false;
    int sample_iter = 0;
    
    // then, learn!
    for (size_t iter = 0; sample_iter != samples; ++ iter, sample_iter += sampling) {

      bool baby_finished = true;
      if (baby_iter != baby_last) {
	baby_finished = false;
	
	const size_t baby_next = utils::bithack::min(baby_iter + baby_size, baby_last);
	
	while (baby_iter < baby_next) {
	  std::copy(training.begin() + index[baby_index], training.begin() + index[baby_index + 1], std::back_inserter(training_samples));
	  
	  baby_iter = index[baby_index + 1];
	  ++ baby_index;
	}
	
	if (debug >= 2)
	  std::cerr << "baby: " << training_samples.size() << std::endl;
      }

      double temperature = 1.0;
      bool anneal_finished = true;
      if (anneal_iter != anneal_last) {
	anneal_finished = false;
	temperature = double(anneal_last - anneal_iter) + 1;
	
	++ anneal_iter;

	if (debug >= 2)
	  std::cerr << "temperature: " << temperature << std::endl;
      }
      
      sampling = baby_finished && anneal_finished;
      
      if (debug) {
	if (sampling)
	  std::cerr << "sampling iteration: " << (iter + 1) << std::endl;
	else
	  std::cerr << "burn-in iteration: " << (iter + 1) << std::endl;
      }
      
      boost::random_number_generator<sampler_type::generator_type> gen(sampler.generator());
      std::random_shuffle(training_samples.begin(), training_samples.end(), gen);

      if (infinite)  {
	data_set_type::iterator titer_end = training_samples.end();
	for (data_set_type::iterator titer = training_samples.begin(); titer != titer_end; ++ titer) {
	  if (boost::fusion::get<2>(*titer))
	    lm.decrement(boost::fusion::get<0>(*titer), boost::fusion::get<1>(*titer), boost::fusion::get<2>(*titer), sampler);
	  
	  lm.increment(boost::fusion::get<0>(*titer), boost::fusion::get<1>(*titer), boost::fusion::get<2>(*titer), sampler, temperature);
	}
	
	if (debug >= 2) {
	  std::cerr << "penetration count" << std::endl;
	  size_t total = 0;
	  for (size_t n = 0; n != lm.orders.size(); ++ n) {
	    std::cerr << "order=" << n << " a=" << lm.orders[n].first << " b=" << lm.orders[n].second << std::endl;
	    total += lm.orders[n].first;
	  }
	  std::cerr << "total=" << total << std::endl;
	}

      } else {
	data_set_type::iterator titer_end = training_samples.end();
	for (data_set_type::iterator titer = training_samples.begin(); titer != titer_end; ++ titer) {
	  if (boost::fusion::get<2>(*titer))
	    lm.decrement(boost::fusion::get<0>(*titer), boost::fusion::get<1>(*titer), sampler);
	  else
	    boost::fusion::get<2>(*titer) = true;
	  
	  lm.increment(boost::fusion::get<0>(*titer), boost::fusion::get<1>(*titer), sampler, temperature);
	}
      }
      
      if (static_cast<int>(iter) % resample_rate == resample_rate - 1) {
	if (slice_sampling)
	  lm.slice_sample_parameters(sampler, resample_iterations);
	else
	  lm.sample_parameters(sampler, resample_iterations);
	
	if (debug >= 2)
	  for (int n = 0; n != order; ++ n)
	    std::cerr << "order=" << n << " discount=" << lm.discount[n] << " strength=" << lm.strength[n] << std::endl;
      }
	
      if (debug)
	std::cerr << "log-likelihood: " << lm.log_likelihood() << std::endl;
    }
    
    // clear training data
    training.clear();
    data_set_type(training).swap(training);
    
    // we will dump LM... now, define a format!
    if (! output_file.empty())
      lm.write(output_file);
    
    // testing!
    if (! test_files.empty()) {
      sentence_type sentence;
      sentence_type ngram(1, vocab_type::BOS);

      double logprob_total = 0.0;
      size_t num_word = 0;
      size_t num_oov = 0;
      size_t num_sentence = 0;
      
      for (path_set_type::const_iterator fiter = test_files.begin(); fiter != test_files.end(); ++ fiter) {
	utils::compress_istream is(*fiter, 1024 * 1024);
	
	while (is >> sentence) {
	  ngram.resize(1);
	  
	  sentence_type::const_iterator siter_end = sentence.end();
	  for (sentence_type::const_iterator siter = sentence.begin(); siter != siter_end; ++ siter) {
	    const bool is_oov = ! (siter->id() < non_oov.size() && non_oov[siter->id()]);
	    const double prob = lm.prob(*siter, std::max(ngram.begin(), ngram.end() - order + 1), ngram.end());
	    
	    if (! is_oov)
	      logprob_total += std::log(prob);
	    
	    num_oov += is_oov;
	    
	    ngram.push_back(*siter);
	  }

	  const double prob = lm.prob(vocab_type::EOS, std::max(ngram.begin(), ngram.end() - order + 1), ngram.end());
	  logprob_total += std::log(prob);
	  
	  num_word += sentence.size();
	  ++ num_sentence;
	}
      }
      
      std::cerr << "# of sentences: " << num_sentence
		<< " # of words: " << num_word
		<< " # of OOV: " << num_oov
		<< " order: " << order
		<< std::endl;
      
      std::cerr << "logprob = " << logprob_total << std::endl;
      std::cerr << "ppl     = " << utils::mathop::exp(- logprob_total / (num_word - num_oov + num_sentence)) << std::endl;
      std::cerr << "ppl1    = " << utils::mathop::exp(- logprob_total / (num_word - num_oov)) << std::endl;
    }
    
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}

struct string_hash : public utils::hashmurmur<size_t>
{
  typedef utils::hashmurmur<size_t> hasher_type;
  
  size_t operator()(const std::string& x) const
  {
    return hasher_type::operator()(x.begin(), x.end(), 0);
  }
};

template <typename Tp>
struct greater_second
{
  bool operator()(const Tp& x, const Tp& y) const
  {
    return x.second > y.second;
  }
  
  bool operator()(const Tp* x, const Tp* y) const
  {
    return x->second > y->second;
  }
};

size_t vocabulary_size(const path_set_type& files)
{
  typedef uint64_t count_type;
  typedef utils::unordered_map<std::string, count_type, string_hash, std::equal_to<std::string>, std::allocator<std::pair<const std::string, count_type> > >::type vocab_type;

  typedef std::vector<const vocab_type::value_type*, std::allocator<const vocab_type::value_type*> > sorted_type;
  
  vocab_type vocab;
  std::string word;
  
  for (path_set_type::const_iterator fiter = files.begin(); fiter != files.end(); ++ fiter) {
    if (! boost::filesystem::exists(*fiter))
      throw std::runtime_error("no file? " + fiter->string());
    
    utils::compress_istream is(*fiter, 1024 * 1024);
    
    while (is >> word) 
      ++ vocab[word];
  }

  sorted_type sorted;
  sorted.reserve(vocab.size());
  
  vocab_type::const_iterator viter_end = vocab.end();
  for (vocab_type::const_iterator viter = vocab.begin(); viter != viter_end; ++ viter)
    sorted.push_back(&(*viter));
  
  std::sort(sorted.begin(), sorted.end(), greater_second<vocab_type::value_type>());

  sorted_type::const_iterator siter_end = sorted.end();
  for (sorted_type::const_iterator siter = sorted.begin(); siter != siter_end; ++ siter) {
    word_type __tmptmp((*siter)->first);
  }
  
  return vocab.size();
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
    
    ("order", po::value<int>(&order)->default_value(order), "max ngram order")
    
    ("samples",             po::value<int>(&samples)->default_value(samples),                         "# of samples")
    ("baby-steps",          po::value<int>(&baby_steps)->default_value(baby_steps),                   "# of baby steps")
    ("anneal-steps",        po::value<int>(&anneal_steps)->default_value(anneal_steps),               "# of anneal steps")
    ("resample",            po::value<int>(&resample_rate)->default_value(resample_rate),             "hyperparameter resample rate")
    ("resample-iterations", po::value<int>(&resample_iterations)->default_value(resample_iterations), "hyperparameter resample iterations")
    
    ("slice",               po::bool_switch(&slice_sampling),                                         "slice sampling for hyperparameters")
    ("infinite",            po::bool_switch(&infinite),                                               "infinite n-gram language model")
    
    ("discount",       po::value<double>(&discount)->default_value(discount),                         "discount ~ Beta(alpha,beta)")
    ("discount-alpha", po::value<double>(&discount_prior_alpha)->default_value(discount_prior_alpha), "discount ~ Beta(alpha,beta)")
    ("discount-beta",  po::value<double>(&discount_prior_beta)->default_value(discount_prior_beta),   "discount ~ Beta(alpha,beta)")

    ("strength",       po::value<double>(&strength)->default_value(strength),                         "strength ~ Gamma(shape,rate)")
    ("strength-shape", po::value<double>(&strength_prior_shape)->default_value(strength_prior_shape), "strength ~ Gamma(shape,rate)")
    ("strength-rate",  po::value<double>(&strength_prior_rate)->default_value(strength_prior_rate),   "strength ~ Gamma(shape,rate)")

    ("order-alpha", po::value<double>(&order_alpha)->default_value(order_alpha), "order ~ Beta(alpha,beta)")
    ("order-beta",  po::value<double>(&order_beta)->default_value(order_beta),   "order ~ Beta(alpha,beta)")
    
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
