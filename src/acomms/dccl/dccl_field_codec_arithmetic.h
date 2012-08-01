// Copyright 2009-2012 Toby Schneider (https://launchpad.net/~tes)
//                     Massachusetts Institute of Technology (2007-)
//                     Woods Hole Oceanographic Institution (2007-)
//                     Goby Developers Team (https://launchpad.net/~goby-dev)
// 
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

// This code is adapted from the reference code provided by Witten et al. "Arithmetic Coding for Data Compression," Communications of the ACM, June 1987, Vol 30, Number 6


#ifndef DCCLFIELDCODECARITHMETIC20120726H
#define DCCLFIELDCODECARITHMETIC20120726H

#include <limits>
#include <algorithm>

#include "goby/acomms/dccl/dccl_field_codec_typed.h"
#include "goby/acomms/protobuf/dccl.pb.h"

namespace goby
{
    namespace acomms
    {
        template<typename FieldType = double>   
            class DCCLArithmeticFieldCodecBase : public DCCLRepeatedTypedFieldCodec<double, FieldType>
            {
              private:
              struct Model;
                
              public:
              
              typedef double WireType;
              typedef uint32 freq_type;
              typedef int symbol_type; // google protobuf RepeatedField size type
            
              static const int CODE_VALUE_BITS = 32;
              static const int FREQUENCY_BITS = CODE_VALUE_BITS - 2;
              
              static const freq_type MAX_FREQUENCY = (1 << FREQUENCY_BITS) - 1;

              static const symbol_type OUT_OF_RANGE_SYMBOL = -1;
              static const symbol_type EOF_SYMBOL = -2;
              static const symbol_type MIN_SYMBOL = EOF_SYMBOL;

              static const uint64 TOP_VALUE = (static_cast<uint64>(1) << CODE_VALUE_BITS) - 1; // 11111111...
              static const uint64 HALF = (static_cast<uint64>(1) << CODE_VALUE_BITS-1);        // 10000000...
              static const uint64 FIRST_QTR = HALF >> 1;                                       // 01000000...
              static const uint64 THIRD_QTR = HALF+FIRST_QTR;                                  // 11000000...
            
              Bitset encode_repeated(const std::vector<WireType>& wire_value)
              {
                  using goby::glog;
                  using namespace goby::common::logger;
                  
                  const Model& model = current_model();

                  uint64 low = 0; // lowest code value (0.0 in decimal version)
                  uint64 high = TOP_VALUE; // highest code value (1.0 in decimal version)
                  int bits_to_follow = 0; // bits to follow with after expanding around half
                  Bitset bits;

                  
                  for(unsigned value_index = 0, n = max_repeat(); value_index < n; ++value_index)
                  {
                      symbol_type symbol =
                          (wire_value.size() > value_index) ?
                          model.value_to_symbol(wire_value[value_index]) :
                          EOF_SYMBOL;
                      

                      glog.is(DEBUG3) &&
                          glog << "(DCCLArithmeticFieldCodec) value is : " << wire_value[value_index] << std::endl;
                      
                      glog.is(DEBUG3) &&
                          glog << "(DCCLArithmeticFieldCodec) symbol is : " << symbol << std::endl;
                      
                      glog.is(DEBUG3) &&
                          glog << "(DCCLArithmeticFieldCodec) current interval: [" << (double)low / TOP_VALUE  << ","
                               << (double)high / TOP_VALUE << ")" << std::endl;

                      
                      uint64 range = (high-low)+1;
                      
                      std::pair<freq_type, freq_type> c_freq_range =
                          model.symbol_to_cumulative_freq(symbol);

                      glog.is(DEBUG3) &&
                          glog << "(DCCLArithmeticFieldCodec) input symbol (" << symbol
                               << ") cumulative freq: ["<< c_freq_range.first << "," << c_freq_range.second << ")" << std::endl;
                      
                      high = low + (range*c_freq_range.second)/model.total_freq-1;
                      low += (range*c_freq_range.first)/model.total_freq;
                      
                      glog.is(DEBUG3) &&
                          glog << "(DCCLArithmeticFieldCodec) input symbol (" << symbol << ") interval: ["
                               << (double)low / TOP_VALUE  << "," << (double)high / TOP_VALUE << ")" << std::endl;


                      
                      glog.is(DEBUG3) &&
                          glog << "(DCCLArithmeticFieldCodec) Q1: " << Bitset(CODE_VALUE_BITS, FIRST_QTR)
                               << ", Q2: " <<  Bitset(CODE_VALUE_BITS, HALF)
                               << ", Q3 : " << Bitset(CODE_VALUE_BITS, THIRD_QTR)
                               << ", top: " << Bitset(CODE_VALUE_BITS, TOP_VALUE) << std::endl;

                      glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) low:  " << Bitset(CODE_VALUE_BITS, low).to_string() << std::endl;
                      glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) high: " << Bitset(CODE_VALUE_BITS, high).to_string() << std::endl;

                      for(;;)
                      {
                          if(high<HALF)
                          {
                              bit_plus_follow(&bits, &bits_to_follow, 0);
                              glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec): completely in [0, 0.5): EXPAND" << std::endl;
                          }
                          else if(low>=HALF)
                          {
                              bit_plus_follow(&bits, &bits_to_follow, 1);
                              low -= HALF;
                              high -= HALF;
                              glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec): completely in [0.5, 1): EXPAND" << std::endl;
                          }
                          else if(low>=FIRST_QTR && high < THIRD_QTR)
                          {
                              glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec): straddle middle [0.25, 0.75): EXPAND" << std::endl;
                              
                              bits_to_follow += 1;
                              low -= FIRST_QTR;
                              high -= FIRST_QTR;
                          }
                          else break;

                          low <<= 1;
                          high <<= 1;
                          high += 1;                          

                          glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) low:  " << Bitset(CODE_VALUE_BITS, low).to_string() << std::endl;
                          glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) high: " << Bitset(CODE_VALUE_BITS, high).to_string() << std::endl;

                          
                          glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) current interval: [" << (double)low / TOP_VALUE  << "," << (double)high / TOP_VALUE << ")" << std::endl;
                          
                      }

                      // nothing more to do, we're encoding all the data and an EOF
                      if(value_index == wire_value.size())
                          break;
                  }

                  // output exactly the number of bits required to store the final range's state
                  bits_to_follow += 1;
                  bit_plus_follow(&bits, &bits_to_follow, (low<FIRST_QTR) ? 0 : 1);

                  return bits;
              }

              void bit_plus_follow(Bitset* bits, int* bits_to_follow, bool bit)
              {
                  bits->push_back(bit);
                  goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): emitted bit: " << bit << std::endl;
                  
                  while(*bits_to_follow)
                  {
                      goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): emitted bit (from follow): " << !bit << std::endl;

                      bits->push_back(!bit);
                      (*bits_to_follow) -= 1;
                  }
              }
              
              std::vector<WireType> decode_repeated(Bitset* bits)
              {
                  using goby::glog;
                  using namespace goby::common::logger;
                  
                  std::vector<WireType> values;

                  const Model& model = current_model();

                  
                  uint64 value = 0;
                  uint64 low = 0;
                  uint64 high = TOP_VALUE;

                  // offset from `bits` to currently examined `value`
                  int bit_stream_offset = CODE_VALUE_BITS - bits->size();
                  
                  for(int i = 0, n = CODE_VALUE_BITS; i < n; ++i)
                  {
                      if(i >= bit_stream_offset)
                          value |= (static_cast<uint64>((*bits)[bits->size()-(i-bit_stream_offset)-1]) << i);
                  }

                  glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec): starting value: " << Bitset(CODE_VALUE_BITS, value).to_string() << std::endl;
                              
                  
                  
                  for(unsigned value_index = 0, n = max_repeat(); value_index < n; ++value_index)
                  {
                      uint64 range = (high-low)+1;

                      symbol_type symbol = bits_to_symbol(bits, value, bit_stream_offset, low, range);
                      
                      glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) symbol is: " << symbol << std::endl;
                      values.push_back(model.symbol_to_value(symbol));
                      glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) value is: " << values.back() << std::endl;

                      glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) bits is: " << *bits << std::endl;
                      
                      std::pair<freq_type, freq_type> c_freq_range =
                          model.symbol_to_cumulative_freq(symbol);

                      glog.is(DEBUG3) && glog << "(DCCLArithmeticFieldCodec) input symbol (" << symbol << ") cumulative freq: ["<< c_freq_range.first << "," << c_freq_range.second << ")" << std::endl;
                      
                      high = low + (range*c_freq_range.second)/model.total_freq-1;
                      low += (range*c_freq_range.first)/model.total_freq;
                      
                      
                      for(;;)
                      {
                          if(high < HALF)
                          {
                              // nothing
                          }
                          else if(low >= HALF)
                          {
                              value -= HALF;
                              low -= HALF;
                              high -= HALF;
                          }
                          else if(low >= FIRST_QTR
                                  && high < THIRD_QTR)
                          {
                              value -= FIRST_QTR;
                              low -= FIRST_QTR;
                              high -= FIRST_QTR;
                          }
                          else break;

                          low <<= 1;
                          high <<= 1;
                          high += 1;
                          value <<= 1;
                          bit_stream_offset +=1;
                      }                      
                      
                      if(symbol == EOF_SYMBOL)
                          break;
                  }
                  return values;
                  
              }


              unsigned size_repeated(const std::vector<WireType>& wire_values)
              {
                  // we should really cache this for efficiency
                  return encode_repeated(wire_values).size();
              }
            

              // this maximum size will be upper bounded by: ceil(log_2(1/P)) + 1 where P is the
              // probability of this least probable set of symbols
              unsigned max_size_repeated()
              {
                  using goby::util::log2;
                  
                  const Model& model = current_model();
                  std::cout << model.user_model.DebugString() << std::endl;
                  
                  
                  double lowest_frequency = std::min((model.user_model.out_of_range_frequency() == 0) ? MAX_FREQUENCY : model.user_model.out_of_range_frequency(),
                                                     *std::min_element(model.user_model.frequency().begin(), model.user_model.frequency().end()));
                  
                  // full of least probable symbols
                  unsigned size_least_probable = std::ceil(max_repeat()*(log2(model.total_freq)-log2(lowest_frequency)));

                  goby::glog.is(common::logger::DEBUG2) && goby::glog << "(DCCLArithmeticFieldCodec) size_least_probable: " << size_least_probable << std::endl;

                  
                  // almost full of least probable symbols plus EOF
                  unsigned size_least_probable_plus_eof = std::ceil(max_repeat()*log2(model.total_freq)-(max_repeat()-1)*log2(lowest_frequency)-model.user_model.eof_frequency());

                  goby::glog.is(common::logger::DEBUG2) && goby::glog << "(DCCLArithmeticFieldCodec) size_least_probable_plus_eof: " << size_least_probable_plus_eof << std::endl;

                  return std::max(size_least_probable_plus_eof, size_least_probable) + 1;
              }
            
              
              
              unsigned min_size_repeated()
              {
                  using goby::util::log2;

                  // just EOF
                  const Model& model = current_model();

                  unsigned size_empty = std::ceil(log2(model.total_freq)-log2(model.user_model.eof_frequency()));
                  
                  goby::glog.is(common::logger::DEBUG2) && goby::glog << "(DCCLArithmeticFieldCodec) size_empty: " << size_empty << std::endl;
                  
                  // full with most probable symbol
                  double highest_frequency = std::max(model.user_model.out_of_range_frequency(),
                                                      *std::max_element(model.user_model.frequency().begin(), model.user_model.frequency().end()));
                  
                  unsigned size_most_probable = std::ceil(max_repeat()*(log2(model.total_freq)-log2(highest_frequency)));

                  goby::glog.is(common::logger::DEBUG2) && goby::glog << "(DCCLArithmeticFieldCodec) size_most_probable: " << size_most_probable << std::endl;

                  
                  return std::min(size_empty, size_most_probable);
              }
          
              void validate()
              {
                  DCCLFieldCodecBase::require(DCCLFieldCodecBase::dccl_field_options().has_arithmetic(),
                                              "missing (goby.field).dccl.arithmetic");

                  std::string model_name = DCCLFieldCodecBase::dccl_field_options().arithmetic().model();
                  DCCLFieldCodecBase::require(models_.count(model_name),
                                              "no such (goby.field).dccl.arithmetic.model called \"" + model_name + "\" loaded.");
              
              }


              // end inherited methods

              symbol_type bits_to_symbol(Bitset* bits,
                                         uint64& value,
                                         int& bit_stream_offset,
                                         uint64 low,
                                         uint64 range)
              {
                  const Model& model = current_model();
                  
                  for(;;)
                  {
                      uint64 value_high = (bit_stream_offset > 0) ?
                          value + (static_cast<uint64>(1) << bit_stream_offset):
                          value;
                      
                      
                      goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): value range: [" << Bitset(CODE_VALUE_BITS, value) << "," << Bitset(CODE_VALUE_BITS, value_high) << ")" << std::endl;

                      
                      freq_type cumulative_freq = ((value-low+1)*model.total_freq-1)/range;
                      freq_type cumulative_freq_high = ((value_high-low+1)*model.total_freq-1)/range;
                      
                      goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): c_freq: " << cumulative_freq << ", c_freq_high: " << cumulative_freq_high << std::endl;

                      
                      symbol_type symbol;
                      goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): c_freq(" << -2 << "): " << model.cumulative_freq.find(-2)->second << std::endl;

                      for(symbol = model.cumulative_freq.begin()->first;
                          model.cumulative_freq.find(symbol)->second <=cumulative_freq;
                          ++symbol)
                      {
                          goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): c_freq(" << symbol << "): " << model.cumulative_freq.find(symbol)->second << std::endl;
                      }
                      
                      goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): c_freq(" << symbol << "): " << model.cumulative_freq.find(symbol)->second << std::endl;
                      
                      if(symbol == model.cumulative_freq.rbegin()->first)
                      {
                          return symbol; // last symbol can't be ambiguous on the low end
                      }
                      else if(model.cumulative_freq.find(symbol)->second > cumulative_freq_high)
                      {
                          return symbol; // unambiguously this symbol
                      }

                      // add another bit to disambiguate
                      bits->get_more_bits(1);

                      goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): bits: " << *bits << std::endl;

                      --bit_stream_offset;
                      value |= static_cast<uint64>(bits->back()) << bit_stream_offset;

                      goby::glog.is(common::logger::DEBUG3) && goby::glog << "(DCCLArithmeticFieldCodec): ambiguous (symbol could be " << symbol << " or " << symbol+1 << ")" << std::endl;

                      
                  }
                  
                  
                  
                  
                  return 0;
              }
              
              
              goby::int32 max_repeat()
              { return DCCLFieldCodecBase::dccl_field_options().max_repeat(); }

              const Model& current_model() const
              {
                  std::string name = DCCLFieldCodecBase::dccl_field_options().arithmetic().model();
                  typename std::map<std::string, Model>::const_iterator it = models_.find(name);
                  if(it == models_.end())
                      throw(DCCLException("Cannot find model called: " + name));
                  else
                      return it->second;
                  
              }
              
              
          
              static void set_model(const std::string& name, const protobuf::ArithmeticModel& model)
              {
                  Model new_model;
                  new_model.user_model = model;
                  create_and_validate_model(&new_model);
                  models_[name] = new_model;
              }

              static void create_and_validate_model(Model* model)
              {
                  if(!model->user_model.IsInitialized())
                  {
                      throw(DCCLException("Invalid model: " +
                                          model->user_model.DebugString() +
                                          "Missing fields: " + model->user_model.InitializationErrorString()));
                  }

                  freq_type cumulative_freq = 0;
                  for(symbol_type symbol = MIN_SYMBOL, n = model->user_model.frequency_size(); symbol < n; ++symbol)
                  {
                      freq_type freq;
                      if(symbol == EOF_SYMBOL)
                          freq = model->user_model.eof_frequency();
                      else if(symbol == OUT_OF_RANGE_SYMBOL)                          
                          freq = model->user_model.out_of_range_frequency();
                      else
                          freq = model->user_model.frequency(symbol);

                      if(freq == 0 && symbol != OUT_OF_RANGE_SYMBOL)
                      {
                          throw(DCCLException("Invalid model: " +
                                              model->user_model.DebugString() +
                                              "All frequencies must be nonzero."));
                      }                      
                      cumulative_freq += freq;
                      model->cumulative_freq[symbol] = cumulative_freq;
                      
                  }
                  
                  
                  model->total_freq = cumulative_freq;
                  if(model->total_freq > MAX_FREQUENCY)
                  {
                      throw(DCCLException("Invalid model: " +
                                          model->user_model.DebugString() +
                                          "Sum of all frequencies must be less than " +
                                          goby::util::as<std::string>(MAX_FREQUENCY) +
                                          " in order to use 64 bit arithmetic"));
                  }

                  if(model->user_model.value_bound_size() != model->user_model.frequency_size() + 1)
                  {
                      throw(DCCLException("Invalid model: " +
                                          model->user_model.DebugString() +
                                          "`value_bound` size must be exactly 1 more than number of symbols (= size of `frequency`)."));
                  }


// is `value_bound` repeated field strictly monotonically increasing?
                  if(std::adjacent_find (model->user_model.value_bound().begin(),
                                         model->user_model.value_bound().end(),
                                         std::greater_equal<WireType>()) !=  model->user_model.value_bound().end())
                  {
                      throw(DCCLException("Invalid model: " +
                                          model->user_model.DebugString() +
                                          "`value_bound` must be monotonically increasing."));
                  }
              }
          
            
              private:

              struct Model
              {
                  protobuf::ArithmeticModel user_model;
                  symbol_type value_to_symbol(WireType value) const
                      {
                      
                          symbol_type symbol =
                              std::upper_bound(user_model.value_bound().begin(),
                                               user_model.value_bound().end(),
                                               value) - 1 - user_model.value_bound().begin();

                          return (symbol < 0 || symbol >= user_model.frequency_size()) ?
                              OUT_OF_RANGE_SYMBOL :
                              symbol;
                          
                      }
              
                  
                  WireType symbol_to_value(symbol_type symbol) const
                      {
                          
                          WireType value = (symbol == OUT_OF_RANGE_SYMBOL) ?
                          std::numeric_limits<WireType>::quiet_NaN() :
                          user_model.value_bound(symbol);

                          return value;
                      }
              

                  symbol_type total_symbols() // EOF and OUT_OF_RANGE plus all user defined
                      { return cumulative_freq.size();  }
                      
                  std::pair<freq_type, freq_type> symbol_to_cumulative_freq(symbol_type symbol) const
                      {
                          std::map<symbol_type, freq_type>::const_iterator c_freq_it = cumulative_freq.find(symbol);
                          std::pair<freq_type, freq_type> c_freq_range;
                          c_freq_range.second = c_freq_it->second;
                          if(c_freq_it == cumulative_freq.begin())
                          {
                              c_freq_range.first  = 0;
                          }
                          else
                          {
                              c_freq_it--;
                              c_freq_range.first = c_freq_it->second;
                          }
                          return c_freq_range;
                          
                      }
                  
                  
                  std::map<symbol_type, freq_type> cumulative_freq;
                  freq_type total_freq;
              };
            
            
              static std::map<std::string, Model> models_;
            };

        // constant integer definitions
        template<typename WireType> const int DCCLArithmeticFieldCodecBase<WireType>::CODE_VALUE_BITS;
        template<typename WireType> const int DCCLArithmeticFieldCodecBase<WireType>::FREQUENCY_BITS;
        template<typename WireType> const typename DCCLArithmeticFieldCodecBase<WireType>::freq_type DCCLArithmeticFieldCodecBase<WireType>::MAX_FREQUENCY;
        template<typename WireType> const typename DCCLArithmeticFieldCodecBase<WireType>::symbol_type DCCLArithmeticFieldCodecBase<WireType>::OUT_OF_RANGE_SYMBOL;
        template<typename WireType> const typename DCCLArithmeticFieldCodecBase<WireType>::symbol_type DCCLArithmeticFieldCodecBase<WireType>::EOF_SYMBOL;
        template<typename WireType> const typename DCCLArithmeticFieldCodecBase<WireType>::symbol_type DCCLArithmeticFieldCodecBase<WireType>::MIN_SYMBOL;
        template<typename WireType> const uint64 DCCLArithmeticFieldCodecBase<WireType>::TOP_VALUE; 
        template<typename WireType> const uint64 DCCLArithmeticFieldCodecBase<WireType>::FIRST_QTR;
        template<typename WireType> const uint64 DCCLArithmeticFieldCodecBase<WireType>::HALF;
        template<typename WireType> const uint64 DCCLArithmeticFieldCodecBase<WireType>::THIRD_QTR;
          
        template<typename WireType>
            std::map<std::string, typename DCCLArithmeticFieldCodecBase<WireType>::Model > DCCLArithmeticFieldCodecBase<WireType>::models_;
        
        template<typename FieldType>   
            class DCCLArithmeticFieldCodec : public DCCLArithmeticFieldCodecBase<FieldType>
        {
            double pre_encode(const FieldType& field_value)
            { return static_cast<double>(field_value); }
            
            FieldType post_decode(const double& wire_value)
            { return static_cast<FieldType>(wire_value); }
        };

        
        template <>
            class DCCLArithmeticFieldCodec<const google::protobuf::EnumValueDescriptor*> : public DCCLArithmeticFieldCodecBase<const google::protobuf::EnumValueDescriptor*>
        {
          public:
            double pre_encode(const google::protobuf::EnumValueDescriptor* const& field_value)
            { return field_value->index(); }
            
            const google::protobuf::EnumValueDescriptor* post_decode(const double& wire_value)
            {
                const google::protobuf::EnumDescriptor* e = DCCLFieldCodecBase::this_field()->enum_type();
                const google::protobuf::EnumValueDescriptor* return_value = e->value(wire_value);
                
                if(return_value)
                    return return_value;
                else
                    throw(DCCLNullValueException());
            }

        };
        
        
        
    }
}


#endif