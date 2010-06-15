// -*- mode: c++ -*-

#ifndef __CICADA__FEATURE_VECTOR__HPP__
#define __CICADA__FEATURE_VECTOR__HPP__ 1

#include <map>
#include <utility>
#include <algorithm>
#include <iterator>
#include <iostream>

#include <cicada/feature.hpp>
#include <utils/space_separator.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/tokenizer.hpp>

namespace cicada
{
  
  // forward declaration...
  template <typename Tp, typename Alloc >
  class WeightVector;

  template <typename Tp, typename Alloc=std::allocator<Tp> >
  class FeatureVector
  {
  public:
    typedef cicada::Feature feature_type;
    typedef cicada::Feature key_type;
    typedef Tp mapped_type;
    typedef Tp data_type;
    
    typedef std::pair<const feature_type, data_type> value_type;
    
  private:
    typedef typename Alloc::template rebind<value_type>::other alloc_type;
    typedef std::map<key_type, data_type, std::less<key_type>, alloc_type> feature_vector_type;
    typedef FeatureVector<Tp, Alloc> self_type;

  public:
    typedef typename feature_vector_type::size_type       size_type;
    typedef typename feature_vector_type::difference_type difference_type;
    
    typedef typename feature_vector_type::const_iterator  const_iterator;
    typedef typename feature_vector_type::iterator        iterator;
    typedef typename feature_vector_type::const_reference const_reference;
    typedef typename feature_vector_type::reference       reference;
    
  public:
    FeatureVector() {}
    FeatureVector(const FeatureVector& x) : __values(x.__values) {}
    template <typename Iterator>
    FeatureVector(Iterator first, Iterator last) : __values(first, last) { }

  public:
    template <typename Iterator>
    void insert(Iterator first, Iterator last)
    {
      __values.insert(first, last);
    }
    
    template <typename Iterator>
    void insert(Iterator iter, const value_type& x)
    {
      if (x.second != 0.0)
	__values[x.first] += x.second;
    }

    size_type size() const { return __values.size(); }
    bool empty() const { return __values.empty(); }

    void reserve(size_type x) { }
    
    void clear() { __values.clear(); }

    Tp operator[](const key_type& x) const
    {
      const_iterator iter = find(x);
      return (iter == end() ? Tp() : iter->second);
    }
    
    Tp& operator[](const key_type& x)
    {
      return __values[x];
    }
    
    const_iterator find(const key_type& x) const
    {
      return __values.find(x);
    }
    
    iterator find(const key_type& x)
    {
      return __values.find(x);
    }
    
    void erase(const key_type& x)
    {
      __values.erase(x);
    }
    
    const_iterator begin() const { return __values.begin(); }
    iterator begin() { return __values.begin(); }
    const_iterator end() const { return __values.end(); }
    iterator end() { return __values.end(); }
    
    const_reference front() const { return *__values.begin(); }
    reference front() { return *__values.begin(); }
    
    const_reference back() const { return *(-- __values.end());}
    reference back() { return *(-- __values.end());}
    
    void swap(FeatureVector& x) { __values.swap(x.__values); }
    
    Tp dot() const
    {
      Tp sum = Tp();
      const_iterator iter_end = end();
      for (const_iterator iter = begin(); iter != iter_end; ++ iter)
	sum += iter->second;
      return sum;
    }

    template <typename T, typename A>
    Tp dot(const WeightVector<T,A>& x) const
    {
      Tp sum = Tp();
      const_iterator iter_end = end();
      for (const_iterator iter = begin(); iter != iter_end; ++ iter)
	sum += iter->second * x[iter->first];
      return sum;
    }
    
    template <typename T, typename A>
    Tp dot(const FeatureVector<T,A>& x) const
    {
      return dot(x.begin(), x.end());
    }
    
    template <typename Iterator>
    Tp dot(Iterator first, Iterator last) const
    {
      const_iterator iter1 = begin();
      const_iterator iter1_end = end();

      Tp sum = Tp();
      
      while (iter1 != iter1_end && first != last) {
	if (iter1->first < first->first)
	  ++ iter1;
	else if (first->first < iter1->first)
	  ++ first;
	else {
	  sum += iter1->second * first->second;
	  
	  ++ iter1;
	  ++ first;
	}
      }
      
      return sum;
    }

  public:
    // comparison
    friend
    bool operator==(const FeatureVector& x, const FeatureVector& y)
    {
      return x.__values == y.__values;
    }

    friend
    bool operator!=(const FeatureVector& x, const FeatureVector& y)
    {
      return x.__values != y.__values;
    }

    friend
    bool operator<(const FeatureVector& x, const FeatureVector& y)
    {
      return x.__values < y.__values;
    }

    friend
    bool operator<=(const FeatureVector& x, const FeatureVector& y)
    {
      return x.__values <= y.__values;
    }

    friend
    bool operator>(const FeatureVector& x, const FeatureVector& y)
    {
      return x.__values > y.__values;
    }

    friend
    bool operator>=(const FeatureVector& x, const FeatureVector& y)
    {
      return x.__values >= y.__values;
    }


  public:
    
    template <typename T, typename A>
    friend
    std::ostream& operator<<(std::ostream& os, const FeatureVector<T,A>& x);
    
    template <typename T, typename A>
    friend
    std::istream& operator>>(std::istream& is, FeatureVector<T,A>& x);

  private:
    template <typename O, typename T>
    struct __apply_unary : public O, public T
    {
      __apply_unary(const T& x) : T(x) {}
      
      template <typename Value>
      void operator()(Value& value) const
      {
	value.second = O::operator()(value.second, static_cast<const T&>(*this));
      }
    };
    
  public:
    // operators...
    template <typename T>
    self_type& operator+=(const T& x)
    { 
      std::for_each(begin(), end(), __apply_unary<std::plus<Tp>, T>(x));
    }

    template <typename T>
    self_type& operator-=(const T& x)
    { 
      std::for_each(begin(), end(), __apply_unary<std::minus<Tp>, T>(x));
    }
    
    template <typename T>
    self_type& operator*=(const T& x)
    { 
      if (x == T())
	__values.clear();
      else
	std::for_each(begin(), end(), __apply_unary<std::multiplies<Tp>, T>(x));
    }
    
    template <typename T>
    self_type& operator/=(const T& x)
    {
      std::for_each(begin(), end(), __apply_unary<std::divides<Tp>, T>(x));
    }
    
    template <typename T, typename A>
    self_type& operator+=(const FeatureVector<T,A>& x)
    {
      typedef FeatureVector<T,A> another_type;
      
      typename another_type::const_iterator iter2_end = x.end();
      for (typename another_type::const_iterator iter2 = x.begin(); iter2 != iter2_end; ++ iter2) {
	iterator iter1 = __values.find(iter2->first);
	if (iter1 == __values.end())
	  __values.insert(*iter2);
	else {
	  iter1->second += iter2->second;
	  if (iter1->second == Tp())
	    __values.erase(iter1);
	}
      }
      
      return *this;
    }

    template <typename T, typename A>
    self_type& operator-=(const FeatureVector<T,A>& x)
    {
      typedef FeatureVector<T,A> another_type;
      
      typename another_type::const_iterator iter2_end = x.end();
      for (typename another_type::const_iterator iter2 = x.begin(); iter2 != iter2_end; ++ iter2) {
	iterator iter1 = __values.find(iter2->first);
	if (iter1 == __values.end())
	  __values.insert(std::make_pair(iter2->first, - Tp(iter2->second)));
	else {
	  iter1->second -= iter2->second;
	  if (iter1->second == Tp())
	    __values.erase(iter1);
	}
      }
      
      return *this;
    }
    
    template <typename T, typename A>
    self_type& operator*=(const FeatureVector<T,A>& x)
    {
      typedef FeatureVector<T,A> another_type;
      
      self_type features;
      
      const_iterator iter1 = begin();
      const_iterator iter1_end = end();
      
      typename another_type::const_iterator iter2 = x.begin();
      typename another_type::const_iterator iter2_end = x.end();
      
      while (iter1 != iter1_end && iter2 != iter2_end) {
	if (iter1->first < iter2->first)
	  ++ iter1;
	else if (iter2->first < iter1->first)
	  ++ iter2;
	else {
	  const Tp value = iter1->second * iter2->second;
	  if (value != Tp())
	    features.__values.insert(std::make_pair(iter1->first, value));
	  
	  ++ iter1;
	  ++ iter2;
	}
      }
      
      __values.swap(features.__values);
      
      return *this;
    }
    
    
    template <typename T1, typename A1, typename T2, typename A2>
    friend
    FeatureVector<T1,A1> operator+(const FeatureVector<T1,A1>& x, const FeatureVector<T2,A2>& y);

    
    template <typename T1, typename A1, typename T2, typename A2>
    friend
    FeatureVector<T1,A1> operator-(const FeatureVector<T1,A1>& x, const FeatureVector<T2,A2>& y);

    
    template <typename T1, typename A1, typename T2, typename A2>
    friend
    FeatureVector<T1,A1> operator*(const FeatureVector<T1,A1>& x, const FeatureVector<T2,A2>& y);
    
  private:
    feature_vector_type __values;
  };
  
  template <typename T1, typename A1, typename T2>
  inline
  FeatureVector<T1,A1> operator+(const FeatureVector<T1,A1>& x, const T2& y)
  {
    FeatureVector<T1,A1> features(x);
    features += y;
    return features;
  }

  template <typename T2, typename T1, typename A1>
  inline
  FeatureVector<T1,A1> operator+(const T2& x, const FeatureVector<T1,A1>& y)
  {
    FeatureVector<T1,A1> features(y);
    features += x;
    return features;
  }

  template <typename T1, typename A1, typename T2>
  inline
  FeatureVector<T1,A1> operator*(const FeatureVector<T1,A1>& x, const T2& y)
  {
    if (y == T2()) return FeatureVector<T1,A1>();
    
    FeatureVector<T1,A1> features(x);
    features *= y;
    return features;
  }

  template <typename T2, typename T1, typename A1>
  inline
  FeatureVector<T1,A1> operator*(const T2& x, const FeatureVector<T1,A1>& y)
  {
    if (x == T2()) return FeatureVector<T1,A1>();
    
    FeatureVector<T1,A1> features(y);
    features *= x;
    return features;
  }

  template <typename T1, typename A1, typename T2>
  inline
  FeatureVector<T1,A1> operator-(const FeatureVector<T1,A1>& x, const T2& y)
  {
    FeatureVector<T1,A1> features(x);
    features -= y;
    return features;
  }
  
  template <typename T1, typename A1, typename T2>
  inline
  FeatureVector<T1,A1> operator/(const FeatureVector<T1,A1>& x, const T2& y)
  {
    FeatureVector<T1,A1> features(x);
    features /= y;
    return features;
  }

  
  template <typename T1, typename A1, typename T2, typename A2>
  inline
  FeatureVector<T1,A1> operator+(const FeatureVector<T1,A1>& x, const FeatureVector<T2,A2>& y)
  {
    typedef FeatureVector<T1,A1> left_type;
    typedef FeatureVector<T2,A2> right_type;

    left_type features(x);
    features += y;
    return features;
  }

  template <typename T1, typename A1, typename T2, typename A2>
  inline
  FeatureVector<T1,A1> operator-(const FeatureVector<T1,A1>& x, const FeatureVector<T2,A2>& y)
  {
    typedef FeatureVector<T1,A1> left_type;
    typedef FeatureVector<T2,A2> right_type;
    
    left_type features(x);
    features -= y;
    return features;
  }

  template <typename T1, typename A1, typename T2, typename A2>
  inline
  FeatureVector<T1,A1> operator*(const FeatureVector<T1,A1>& x, const FeatureVector<T2,A2>& y)
  {
    typedef FeatureVector<T1,A1> left_type;
    typedef FeatureVector<T2,A2> right_type;

    left_type features;
    
    typename left_type::const_iterator iter1 = x.begin();
    typename left_type::const_iterator iter1_end = x.end();

    typename right_type::const_iterator iter2 = y.begin();
    typename right_type::const_iterator iter2_end = y.end();
    
    while (iter1 != iter1_end && iter2 != iter2_end) {
      if (iter1->first < iter2->first)
	++ iter1;
      else if (iter2->first < iter1->first)
	++ iter2;
      else {
	const T1 value = iter1->second * iter2->second;
	if (value != T1())
	  features.__values.insert(std::make_pair(iter1->first, value));
	
	++ iter1;
	++ iter2;
      }
    }
    
    return features;
  }


  template <typename T, typename A>
  inline
  std::ostream& operator<<(std::ostream& os, const FeatureVector<T,A>& x)
  {
    typename FeatureVector<T,A>::const_iterator iter_end = x.end();
    for (typename FeatureVector<T,A>::const_iterator iter = x.begin(); iter != iter_end; ++ iter)
      os << iter->first << ' ' << iter->second << '\n';
    
    return os;
  }

  template <typename T, typename A>
  inline
  std::istream& operator>>(std::istream& is, FeatureVector<T,A>& x)
  {
    typedef boost::tokenizer<utils::space_separator> tokenizer_type;
    
    x.clear();
    
    std::string feature;
    T value;
    while ((is >> feature) && (is >> value))
      x[feature] = value;
    
    return is;
  }
  

};

namespace std
{
  template <typename T, typename A>
  inline
  void swap(cicada::FeatureVector<T,A>& x, cicada::FeatureVector<T, A>& y)
  {
    x.swap(y);
  }
};

#include <cicada/weight_vector.hpp>

#endif

