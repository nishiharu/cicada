#include <iostream>
#include <string>
#include <map>

#include <utils/compact_map.hpp>

int main(int argc, char** argv)
{
  typedef std::map<std::string, int>          map_map_type;
  typedef utils::compact_map<std::string, int> vec_map_type;

  std::cerr << "size: " << sizeof(vec_map_type) << std::endl;

  map_map_type map_map;
  vec_map_type vec_map;

  vec_map.set_empty_key("");
  vec_map.set_deleted_key("This is not allowed!");

  std::string token;
  while (std::cin >> token) {
    ++ map_map[token];
    ++ vec_map[token];
  }
  
  std::cerr << "map size: " << map_map.size() << std::endl
	    << "vec size: " << vec_map.size() << std::endl;
  
  {
    for (map_map_type::const_iterator miter = map_map.begin(); miter != map_map.end(); ++ miter) {
      vec_map_type::const_iterator viter = vec_map.find(miter->first);
      
      if (viter == vec_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }

    // inverse...
    for (vec_map_type::const_iterator viter = vec_map.begin(); viter != vec_map.end(); ++ viter) {
      map_map_type::const_iterator miter = map_map.find(viter->first);
      
      if (miter == map_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }
    
  }
  
  {
    for (map_map_type::const_iterator miter = map_map.begin(); miter != map_map.end(); ++ miter) {
      vec_map_type::iterator viter = vec_map.find(miter->first);
      if (viter == vec_map.end())
	throw std::runtime_error("not found?");
      
      vec_map.erase(viter);
      
      {
	vec_map_type::iterator viter = vec_map.find(miter->first);
	if (viter != vec_map.end())
	  throw std::runtime_error("found?");
      }
    }
  }

  std::cerr << "erased vec map" << std::endl
	    << "map size: " << map_map.size() << std::endl
	    << "vec size: " << vec_map.size() << std::endl;
  
  vec_map.insert(map_map.begin(), map_map.end());
  
  std::cerr << "map size: " << map_map.size() << std::endl
	    << "vec size: " << vec_map.size() << std::endl;
  
  {
    for (map_map_type::const_iterator miter = map_map.begin(); miter != map_map.end(); ++ miter) {
      vec_map_type::const_iterator viter = vec_map.find(miter->first);
      
      if (viter == vec_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }
    // inverse...
    for (vec_map_type::const_iterator viter = vec_map.begin(); viter != vec_map.end(); ++ viter) {
      map_map_type::const_iterator miter = map_map.find(viter->first);
      
      if (miter == map_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }
  }

  std::cerr << "clear + insert" << std::endl;

  vec_map.clear();
  {
    for (map_map_type::const_iterator miter = map_map.begin(); miter != map_map.end(); ++ miter)
      vec_map.insert(*miter);
  }

  std::cerr << "map size: " << map_map.size() << std::endl
	    << "vec size: " << vec_map.size() << std::endl;
  
  {
    for (map_map_type::const_iterator miter = map_map.begin(); miter != map_map.end(); ++ miter) {
      vec_map_type::const_iterator viter = vec_map.find(miter->first);
      
      if (viter == vec_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }
    // inverse...
    for (vec_map_type::const_iterator viter = vec_map.begin(); viter != vec_map.end(); ++ viter) {
      map_map_type::const_iterator miter = map_map.find(viter->first);
      
      if (miter == map_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }
  }

  vec_map.insert(map_map.begin(), map_map.end());
  
  std::cerr << "map size: " << map_map.size() << std::endl
	    << "vec size: " << vec_map.size() << std::endl;
  
  for (vec_map_type::const_iterator viter = vec_map.begin(); viter != vec_map.end(); /**/)
    vec_map.erase(viter ++);
  
  std::cerr << "incrementally erased vec map" << std::endl
	    << "map size: " << map_map.size() << std::endl
	    << "vec size: " << vec_map.size() << std::endl;

  vec_map.insert(map_map.begin(), map_map.end());
  
  std::cerr << "map size: " << map_map.size() << std::endl
	    << "vec size: " << vec_map.size() << std::endl;
  
  {
    for (map_map_type::const_iterator miter = map_map.begin(); miter != map_map.end(); ++ miter) {
      vec_map_type::const_iterator viter = vec_map.find(miter->first);
      
      if (viter == vec_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }
    // inverse...
    for (vec_map_type::const_iterator viter = vec_map.begin(); viter != vec_map.end(); ++ viter) {
      map_map_type::const_iterator miter = map_map.find(viter->first);
      
      if (miter == map_map.end())
	std::cerr << "differ?"
		  << "\tmap: " << miter->first << ": " << miter->second << std::endl;
    }
  }

}
