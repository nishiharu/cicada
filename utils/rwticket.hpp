// -*- mode: c++ -*-
//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __UTILS__RWTICKET__HPP__
#define __UTILS__RWTICKET__HPP__ 1

#include <stdexcept>

#include <boost/thread.hpp>

#include <utils/atomicop.hpp>

namespace utils
{
  class rwticket : private boost::noncopyable
  {
  private:
    typedef union 
    {
      uint32_t u;
      uint16_t us;
      struct {
	uint8_t write;
	uint8_t read;
	uint8_t users;
      } s;
    } ticket_type;
    
  public:
    
    struct scoped_writer_lock
    {
      scoped_writer_lock(rwticket& x) : lock(x) { lock.lock_writer(); }
      ~scoped_writer_lock() { lock.unlock_writer(); }
      
    private:
      rwticket& lock;
    };
    
    struct scoped_reader_lock
    {
      scoped_reader_lock(rwticket& x) : lock(x) { lock.lock_reader(); }
      ~scoped_reader_lock() { lock.unlock_reader(); }
      
    private:
      rwticket& lock;
    };
    
    rwticket() 
    {
      ticket_.u = 0;
    }

    
  public:
    void lock_writer()
    {
      const uint32_t me = utils::atomicop::fetch_and_add(ticket_.u, uint32_t(1 << 16));
      const uint8_t  val = me >> 16;

      std::cerr << "writer: " << int(val) << std::endl;
      
      while (val != ticket_.s.write)
	boost::thread::yield();
    }
    
    void unlock_writer()
    {
      ticket_type t = ticket_;
      
      utils::atomicop::memory_barrier();
      
      ++ t.s.write;
      ++ t.s.read;
      
      ticket_.us = t.us;
    }
    
    void lock_reader()
    {
      const uint32_t me = utils::atomicop::fetch_and_add(ticket_.u, uint32_t(1 << 16));
      const uint8_t  val = me >> 16;

      std::cerr << "reader: " << int(val) << std::endl;
      
      while (val != ticket_.s.read)
	boost::thread::yield();
      
      __sync_add_and_fetch(&ticket_.s.read, uint8_t(1));
      //utils::atomicop::fetch_and_add(ticket_.s.read, uint8_t(1));
    }

    void unlock_reader()
    {
      __sync_add_and_fetch(&ticket_.s.write, uint8_t(1));
      //utils::atomicop::fetch_and_add(ticket_.s.write, uint8_t(1));
    }
    
    
  private:
    ticket_type ticket_;
  };
};

#endif
