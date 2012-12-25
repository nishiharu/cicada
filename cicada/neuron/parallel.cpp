//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include "cicada/neuron/parallel.hpp"

#include <stdexcept>
#include <memory>

namespace cicada
{
  namespace neuron
  {
    void Parallel::forward(const tensor_type& data_input)
    {
      sizes.resize(layers.size());
      data_output.resize(0, 0);
      
      for (size_type i = 0; i != layers.size(); ++ i) {
	if (dimension_input)
	  layers[i]->forward(data_input.col(i));
	else
	  layers[i]->forward(data_input.row(i).transpose());
	
	sizes[i] = layers[i]->data_output.rows();
	
	if (dimension_output) {
	  if (data_output.cols()) {
	    if (data_output.rows() != static_cast<int>(sizes[i]))
	      throw std::runtime_error("invalid concat");
	    
	    data_output.conservativeResize(Eigen::NoChange, data_output.cols() + 1);
	    data_output.col(data_output.cols() - 1) = data_output.col(0);
	  } else
	    data_output = layers[i]->data_output.col(0);
	} else {
	  if (data_output.rows()) {
	    data_output.conservativeResize(data_output.rows() + sizes[i], 1);
	    data_output.block(data_output.rows() - sizes[i], 0, sizes[i], 1) = data_output.col(0);
	  } else
	    data_output = layers[i]->data_output.col(0);
	}
      }

    }
    
    void Parallel::backward(const tensor_type& data_input, const tensor_type& gradient_output)
    {
      gradient_input.resizeLike(data_input);
      gradient_input.setZero();
      
      if (dimension_output) {
	for (size_type i = 0; i != sizes.size(); ++ i) {
	  if (dimension_input) {
	    layers[i]->backward(data_input.col(i), gradient_output.col(i));
	    gradient_input.col(i) += layers[i]->gradient_input;
	  } else {
	    layers[i]->backward(data_input.row(i).transpose(), gradient_output.col(i));
	    gradient_input.row(i) += layers[i]->gradient_input.transpose();
	  }
	}
      } else {
	size_type offset = 0;
	for (size_type i = 0; i != sizes.size(); ++ i) {
	  if (dimension_input) {
	    layers[i]->backward(data_input.col(i), gradient_output.block(offset, 0, sizes[i], 1));
	    gradient_input.col(i) += layers[i]->gradient_input;
	  } else {
	    layers[i]->backward(data_input.row(i).transpose(), gradient_output.block(offset, 0, sizes[i], 1));
	    gradient_input.row(i) += layers[i]->gradient_input.transpose();
	  }
	  
	  offset += sizes[i];
	}
      }
    }

    void Parallel::accumulate(const tensor_type& data_input, const tensor_type& gradient_output)
    {
      if (dimension_output) {
	for (size_type i = 0; i != sizes.size(); ++ i) {
	  if (dimension_input)
	    layers[i]->accumulate(data_input.col(i), gradient_output.col(i));
	  else
	    layers[i]->accumulate(data_input.row(i).transpose(), gradient_output.col(i));
	}
      } else {
	size_type offset = 0;
	for (size_type i = 0; i != sizes.size(); ++ i) {
	  if (dimension_input)
	    layers[i]->accumulate(data_input.col(i), gradient_output.block(offset, 0, sizes[i], 1));
	  else
	    layers[i]->accumulate(data_input.row(i).transpose(), gradient_output.block(offset, 0, sizes[i], 1));
	  
	  offset += sizes[i];
	}
      }
    }

    Parallel::layer_ptr_type Parallel::clone() const
    {
      std::auto_ptr<Parallel> cloned(new Parallel(*this));
      
      for (size_type i = 0; i != layers.size(); ++ i)
	cloned->layers[i] = layers[i]->clone();
      
      return layer_ptr_type(cloned.release());
    }
  }
}
