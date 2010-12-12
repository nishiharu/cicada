//
//  Copyright(C) 2010 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/fusion/adapted.hpp>

#include <cicada/cluster.hpp>

#include <utils/tempfile.hpp>
#include <utils/compress_stream.hpp>
#include <utils/sgi_hash_map.hpp>

#include <boost/thread.hpp>

namespace cicada
{
  
  void Cluster::open(const path_type& path)
  {
    typedef utils::repository repository_type;

    clear();

    file = path;
    
    if (boost::filesystem::is_directory(path)) {
      repository_type rep(path, repository_type::read);
      
      vocab.open(rep.path("vocab"));
      clusters.open(rep.path("clusters"));
    } else {
      typedef std::vector<id_type, std::allocator<id_type> > cluster_map_type;
      typedef boost::spirit::istream_iterator iterator_type;
      typedef std::pair<std::string, std::string> word_pair_type;
      
      namespace qi = boost::spirit::qi;
      namespace standard = boost::spirit::standard;
      
      qi::rule<iterator_type, std::string(), standard::blank_type>    word;
      qi::rule<iterator_type, word_pair_type(), standard::blank_type> parser; 
      
      word   %= qi::lexeme[+(standard::char_ - standard::space)];
      parser %= word >> word >> (qi::eol | qi::eoi);
      
      cluster_map_type cluster_map;
      
      utils::compress_istream is(path, 1024 * 1024);
      is.unsetf(std::ios::skipws);

      iterator_type iter(is);
      iterator_type iter_end;
      
      word_pair_type word_pair;
      
      while (iter != iter_end) {
	word_pair.first.clear();
	word_pair.second.clear();
	
	if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, standard::blank, word_pair))
	  if (iter != iter_end)
	    throw std::runtime_error("cluster parsing failed");
	
	const word_type cluster(word_pair.first);
	const word_type word(word_pair.second);
	
	if (word.id() >= cluster_map.size())
	  cluster_map.resize(word.id() + 1, 0);
	
	cluster_map[word.id()] = cluster.id() + 1;
      }
      
      const path_type tmp_dir = utils::tempfile::tmp_dir();
      const path_type path_repository = utils::tempfile::directory_name(tmp_dir / "cicada.cluster.XXXXXX");
      
      utils::tempfile::insert(path_repository);
      
      repository_type rep(path_repository, repository_type::write);
      
      {
	boost::iostreams::filtering_ostream os;
	os.push(utils::packed_sink<id_type, std::allocator<id_type> >(rep.path("clusters")), 1024 * 1024);
	
	cluster_map_type::const_iterator iter_end = cluster_map.end();
	for (cluster_map_type::const_iterator iter = cluster_map.begin(); iter != iter_end; ++ iter)
	  os.write((char*) &(*iter), sizeof(id_type));
      }
      
      word_type::write(rep.path("vocab"));
      
      vocab.open(rep.path("vocab"));
      clusters.open(rep.path("clusters"));
    }
  }

  void Cluster::write(const path_type& path) const
  {
    typedef utils::repository repository_type;

    if (empty()) return;
    
    const path_type path_curr = clusters.path().parent_path();

    // do not write at the same file
    if (path_curr == path) return;
    
    repository_type rep(path, repository_type::write);
    
    vocab.write(rep.path("vocab"));
    clusters.write(rep.path("clusters"));
  }
  
  template <typename Tp>
  struct hash_string : public utils::hashmurmur<size_t>
  {
    size_t operator()(const Tp& x) const
    {
      return utils::hashmurmur<size_t>::operator()(x.begin(), x.end(), 0);
    }
  };

#ifdef HAVE_TR1_UNORDERED_MAP
  typedef std::tr1::unordered_map<std::string, Cluster, hash_string<std::string>, std::equal_to<std::string>,
				  std::allocator<std::pair<const std::string, Cluster> > > cluster_map_type;
#else
  typedef sgi::hash_map<std::string, Cluster, hash_string<std::string>, std::equal_to<std::string>,
			std::allocator<std::pair<const std::string, Cluster> > > cluster_map_type;
#endif

  

  Cluster& Cluster::create(const path_type& path)
  {
#ifdef HAVE_TLS
    static __thread cluster_map_type* __clusters_tls = 0;
    static boost::thread_specific_ptr<cluster_map_type> __clusters;
    
    if (! __clusters_tls) {
      __clusters.reset(new cluster_map_type());
      __clusters_tls = __clusters.get();
    }
    cluster_map_type& clusters_map = *__clusters_tls;    
#else
    static boost::thread_specific_ptr<cluster_map_type> __clusters;
    
    if (! __clusters.get())
      __clusters.reset(new cluster_map_type());
    
    cluster_map_type& clusters_map = *__clusters;
#endif
    
    cluster_map_type::iterator iter = clusters_map.find(path.file_string());
    if (iter == clusters_map.end())
      iter = clusters_map.insert(std::make_pair(path.file_string(), Cluster(path))).first;
    
    return iter->second;
  }
};
