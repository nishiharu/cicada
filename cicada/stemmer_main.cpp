//
//  Copyright(C) 2010-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include "stemmer.hpp"

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::cout << argv[0] << " stemmer-spec" << std::endl;
    std::cout << cicada::Stemmer::lists();
    return 1;
  }
  
  try {
    cicada::Stemmer& stemmer(cicada::Stemmer::create(argv[1]));
    
    std::string word;
    while (std::cin >> word)
      std::cout << "word: " << word << " stemmed: " << stemmer(word) << std::endl;
  }
  catch (std::exception& err) {
    std::cerr << "erro: " << err.what() << std::endl;
    return 1;
  }
}
