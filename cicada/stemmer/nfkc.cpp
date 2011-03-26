//
//  Copyright(C) 2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include "stemmer/nfkc.hpp"

#include <unicode/unistr.h>
#include <unicode/normlzr.h>

namespace cicada
{

  namespace stemmer
  {

    Stemmer::symbol_type NFKC::operator[](const symbol_type& word) const
    {
      if (word == vocab_type::EMPTY || word.is_non_terminal()) return word;
    
      const size_type word_size = word.size();
    
      // SGML-like symbols are not prefixed
      if (word_size >= 3 && word[0] == '<' && word[word_size - 1] == '>')
	return word;
      
      symbol_set_type& __cache = const_cast<symbol_set_type&>(cache);
      
      if (word.id() >= __cache.size())
	__cache.resize(word.id() + 1, vocab_type::EMPTY);
      
      if (__cache[word.id()] == vocab_type::EMPTY) {
	UnicodeString uword = UnicodeString::fromUTF8(static_cast<const std::string&>(word));
	UnicodeString uword_nfkc;
	
	UErrorCode status = U_ZERO_ERROR;
	Normalizer::normalize(uword, UNORM_NFKC, 0, uword_nfkc, status);
	if (U_FAILURE(status))
	  throw std::runtime_error("normalization failed");
	
	std::string word_nfkc;
	uword_nfkc.toUTF8String(word_nfkc);
	
	__cache[word.id()] = word_nfkc;
      }    
      return __cache[word.id()];
    }

  };
};
