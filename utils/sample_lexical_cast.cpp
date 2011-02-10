
#include <iostream>

#include "utils/lexical_cast.hpp"

int main(int argc, char** argv)
{
  std::cout << utils::lexical_cast<bool>("yes") << std::endl;
  std::cout << utils::lexical_cast<bool>("Yes ") << std::endl;
  std::cout << utils::lexical_cast<bool>(" true") << std::endl;
  std::cout << utils::lexical_cast<bool>(" tRue ") << std::endl;
  std::cout << utils::lexical_cast<bool>(" 1 ") << std::endl;
  std::cout << utils::lexical_cast<bool>(" 2 ") << std::endl;

  std::cout << utils::lexical_cast<bool>("no") << std::endl;
  std::cout << utils::lexical_cast<bool>("No") << std::endl;
  std::cout << utils::lexical_cast<bool>("nil ") << std::endl;
  std::cout << utils::lexical_cast<bool>("NIL ") << std::endl;
  std::cout << utils::lexical_cast<bool>("false") << std::endl;
  std::cout << utils::lexical_cast<bool>("False") << std::endl;
  std::cout << utils::lexical_cast<bool>(" -1 ") << std::endl;
  std::cout << utils::lexical_cast<bool>(" 0 ") << std::endl;
  std::cout << utils::lexical_cast<bool>(" ") << std::endl;

  std::cout << "integers" << std::endl;
  std::cout << utils::lexical_cast<int>("567") << std::endl;
  std::cout << utils::lexical_cast<size_t>("567") << std::endl;
}
