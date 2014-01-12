// -*- mode: c++ -*-
//
//  Copyright(C) 2014 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__TREE_RNN__HPP__
#define __CICADA__TREE_RNN__HPP__ 1

// binary tree RNN model
// this is simply a place-holder for actual feature in feature directory...
// + we will share word embedding to save memory space...

#include <stdint.h>

#include <stdexcept>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/random.hpp>
#include <boost/shared_ptr.hpp>

#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>
#include <cicada/ngram_cache.hpp>

#include <utils/bithack.hpp>
#include <utils/mathop.hpp>

#include <Eigen/Core>

namespace cicada
{
  class TreeRNN
  {
  public:
    typedef Symbol                  word_type;
    typedef Vocab                   vocab_type;
    
    typedef size_t             size_type;
    typedef ptrdiff_t          difference_type;
    typedef float              logprob_type;
    typedef float              parameter_type;
    typedef double             prob_type;
    
    typedef boost::filesystem::path path_type;
    
    typedef Eigen::Matrix<parameter_type, Eigen::Dynamic, Eigen::Dynamic>    tensor_type;
    typedef Eigen::Map<tensor_type>                                          matrix_type;
    
    class Embedding
    {
    public:
      virtual
      ~Embedding() {}
    public:
      virtual
      void write(const path_type& path) const = 0;
      
      virtual
      matrix_type operator()(const word_type& word) const = 0;
    };
    
    typedef Embedding embedding_type;
    
  public:
    TreeRNN(const path_type& path)
    { open(path); }
    TreeRNN(const size_type& hidden, const size_type& embedding, const path_type& path)
    { open(hidden, embedding, path); }
    
    void write(const path_type& path) const;
    
    void open(const path_type& path);
    void open(const size_type& hidden, const size_type& embedding, const path_type& path);

    template <typename Gen>
    struct randomize
    {
      randomize(Gen& gen, const double range=0.01) : gen_(gen), range_(range) {}
      
      template <typename Tp>
      Tp operator()(const Tp& x) const
      {
	return boost::random::uniform_real_distribution<Tp>(-range_, range_)(const_cast<Gen&>(gen_));
      }
      
      Gen& gen_;
      double range_;
    };
    
    template <typename Gen>
    void random(Gen& gen)
    {
      const double range_p = std::sqrt(6.0 / (embedding_ + 1));
      const double range_u = std::sqrt(6.0 / (hidden_ + 1));
      const double range_t = std::sqrt(6.0 / (hidden_ + hidden_ + embedding_));
      const double range_n = std::sqrt(6.0 / (hidden_ + hidden_ + hidden_));
      
      Wp_.array().unaryExpr(randomize<Gen>(gen, range_p));
      Wu_.array().unaryExpr(randomize<Gen>(gen, range_u));
      Wt_.array().unaryExpr(randomize<Gen>(gen, range_t));
      Wn_.array().unaryExpr(randomize<Gen>(gen, range_n));
    }
    
  public:
    size_type hidden_;
    size_type embedding_;
    
    tensor_type Wp_;
    tensor_type Bp_;
    
    tensor_type Wu_;
    tensor_type Bu_;
    
    tensor_type Wt_;
    tensor_type Bt_;
    
    tensor_type Wn_;
    tensor_type Bn_;
    
    boost::shared_ptr<embedding_type> input_;
  };
};

#endif
