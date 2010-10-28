#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE

#include <boost/numeric/conversion/bounds.hpp>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/phoenix_object.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/fusion/adapted.hpp>

#include <boost/thread.hpp>

#include "utils/config.hpp"

#include "tree_rule.hpp"

BOOST_FUSION_ADAPT_STRUCT(
			  cicada::TreeRule,
			  (cicada::TreeRule::label_type, label)
			  (cicada::TreeRule::antecedent_set_type, antecedents)
			  )

struct treebank_type
{
  typedef std::vector<treebank_type, std::allocator<treebank_type> > antecedents_type;
  
  std::string label;
  antecedents_type antecedents;
  
  treebank_type() {}
  treebank_type(const std::string& __label) : label(__label) {}
  
  void clear()
  {
    label.clear();
    antecedents.clear();
  }
  
  void transform(cicada::TreeRule& x) const
  {
    // pre-order traversal...
    x.label = label;
    x.antecedents = cicada::TreeRule::antecedent_set_type(antecedents.size());
    
    for (size_t i = 0; i != x.antecedents.size(); ++ i)
      antecedents[i].transform(x.antecedents[i]);
  }
  
};


BOOST_FUSION_ADAPT_STRUCT(
			  treebank_type,
			  (std::string, label)
			  (treebank_type::antecedents_type, antecedents)
			  )

namespace cicada
{
  
  template <typename Iterator>
  struct tree_rule_parser_grammar : boost::spirit::qi::grammar<Iterator, treebank_type(), boost::spirit::standard::space_type>
  {
    tree_rule_parser_grammar() : tree_rule_parser_grammar::base_type(tree_rule)
    {
      namespace qi = boost::spirit::qi;
      namespace standard = boost::spirit::standard;
      namespace phoenix = boost::phoenix;
    
      using qi::lit;
      using qi::lexeme;
      using qi::hold;
      using qi::repeat;
      using qi::attr;
      using standard::char_;
      using standard::space;
      
      escaped_char.add
	("\\\\", '\\')
	("\\(", '(')
	("\\)", ')');
      
      label %= lexeme[+(escaped_char | (char_ - space - '(' - ')' - '\\'))];
      tree_rule %= '(' >> label >> *tree_rule >> ')';
    }
    
    boost::spirit::qi::symbols<char, char>        escaped_char;
    boost::spirit::qi::rule<Iterator, std::string(), boost::spirit::standard::space_type>   label;
    boost::spirit::qi::rule<Iterator, treebank_type(), boost::spirit::standard::space_type> tree_rule;
  };

  template <typename Iterator>
  struct tree_rule_generator_grammar : boost::spirit::karma::grammar<Iterator, TreeRule()>
  {
    tree_rule_generator_grammar() : tree_rule_generator_grammar::base_type(tree_rule)
    {
      namespace karma = boost::spirit::karma;
      namespace standard = boost::spirit::standard;
      namespace phoenix = boost::phoenix;
    
      using karma::lit;
      using karma::buffer;
      using karma::repeat;
      using standard::char_;
      using standard::space;
      
      escaped_char.add
	('\\', "\\\\")
	('(',  "\\(")
	(')',  "\\)");
      
      label %= +(escaped_char | char_);
      tree_rule %= '(' << label << (buffer[' ' << +tree_rule] | repeat(0)[tree_rule]) << ')';
    }
    
    boost::spirit::karma::symbols<char, const char*>        escaped_char;
    boost::spirit::karma::rule<Iterator, TreeRule::label_type()>  label;
    boost::spirit::karma::rule<Iterator, TreeRule()>              tree_rule;
  };
  

  bool TreeRule::assign(std::string::const_iterator& iter, std::string::const_iterator end)
  {
    typedef tree_rule_parser_grammar<std::string::const_iterator> grammar_type;
    
#ifdef HAVE_TLS
    static __thread grammar_type* __grammar_tls = 0;
    static boost::thread_specific_ptr<grammar_type > __grammar;
    
    if (! __grammar_tls) {
      __grammar.reset(new grammar_type());
      __grammar_tls = __grammar.get();
    }
    
    grammar_type& grammar = *__grammar_tls;
#else
    static boost::thread_specific_ptr<grammar_type > __grammar;
    if (! __grammar.get())
      __grammar.reset(new grammar_type());
    
    grammar_type& grammar = *__grammar;
#endif
    
    clear();

    treebank_type treebank;
    
    if (boost::spirit::qi::phrase_parse(iter, end, grammar, boost::spirit::standard::space, treebank)) {
      treebank.transform(*this);
      return true;
    } else
      return false;
  }
 
  void TreeRule::assign(const std::string& x)
  {
    clear();
    
    if (x.empty()) return;

    std::string::const_iterator iter = x.begin();
    std::string::const_iterator end = x.end();
    
    const bool result = assign(iter, end);
    if (! result || iter != end)
      throw std::runtime_error("tree rule format parsing failed..." + x);
  }
  
  std::istream& operator>>(std::istream& is, TreeRule& x)
  {
    x.clear();
    
    std::string line;
    if (std::getline(is, line) && ! line.empty())
      x.assign(line);
    
    return is;
  }

  std::ostream& operator<<(std::ostream& os, const TreeRule& x)
  {
    typedef std::ostream_iterator<char> iterator_type;
    typedef tree_rule_generator_grammar<iterator_type> grammar_type;
    
#ifdef HAVE_TLS
    static __thread grammar_type* __grammar_tls = 0;
    static boost::thread_specific_ptr<grammar_type > __grammar;
    
    if (! __grammar_tls) {
      __grammar.reset(new grammar_type());
      __grammar_tls = __grammar.get();
    }
    
    grammar_type& grammar = *__grammar_tls;
#else
    static boost::thread_specific_ptr<grammar_type > __grammar;
    if (! __grammar.get())
      __grammar.reset(new grammar_type());
    
    grammar_type& grammar = *__grammar;
#endif
    
    iterator_type iter(os);
    
    if (! boost::spirit::karma::generate(iter, grammar, x))
      throw std::runtime_error("failed tree-rule generation!");
    
    return os;
  }
};
