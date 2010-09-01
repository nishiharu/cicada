
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>

#include "cicada_impl.hpp"

#include "utils/program_options.hpp"
#include <boost/program_options.hpp>

path_type input_file = "-";
path_type output_file = "-";

// input mode... use of one-line lattice input or sentence input?
void options(int argc, char** argv);

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);
    
    utils::compress_istream is(input_file, 1024 * 1024);
    

    hypergraph_type merged;
    hypergraph_type hypergraph;
    
    std::string line;
    while (std::getline(is, line)) {
      std::string::const_iterator iter = line.begin();
      std::string::const_iterator end = line.end();
      
      if (! hypergraph.assign(iter, end))
	throw std::runtime_error("invalid hypergraph format");
      
      merged.unite(hypergraph);
    }
    
    utils::compress_ostream os(output_file, 1024 * 1024);
    os << merged << '\n';
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
    ("input",  po::value<std::string>()->default_value("-"),   "input in text format")
    ("output", po::value<std::string>(), "output in binary format")
    
    ("help", "help message");

  po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);
  
  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc << std::endl;
    exit(0);
  }
}
