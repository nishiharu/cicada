//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include "stemmer/upper.hpp"

#include <boost/thread.hpp>

#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/translit.h>

#include "utils/config.hpp"

namespace cicada
{
  namespace stemmer
  {
    Upper::Upper() : pimpl(0)
    {
      UErrorCode status = U_ZERO_ERROR;
      std::auto_ptr<icu::Transliterator> trans(icu::Transliterator::createInstance(icu::UnicodeString::fromUTF8("Upper"), UTRANS_FORWARD, status));
      if (U_FAILURE(status))
	throw std::runtime_error(std::string("transliterator::create_instance(): ") + u_errorName(status));
      
      pimpl = trans.release();
    }
    
    Upper::~Upper()
    {
      std::auto_ptr<icu::Transliterator> tmp(static_cast<icu::Transliterator*>(pimpl));
    }
    
    std::string Upper::operator()(const utils::piece& word) const
    {
      if (! pimpl)
	throw std::runtime_error("no upper caser?");

      if (word.empty()) return word;
    
      const size_type word_size = word.size();
    
      // SGML-like symbols are not uppered...
      if (word_size >= 3 && word[0] == '<' && word[word_size - 1] == '>')
	return word;
      
      icu::UnicodeString uword = icu::UnicodeString::fromUTF8(icu::StringPiece(word.data(), word.size()));
      
      static_cast<icu::Transliterator*>(pimpl)->transliterate(uword);
      
      std::string word_upper;
      uword.toUTF8String(word_upper);
	
      return word_upper;
    }

  };
};
