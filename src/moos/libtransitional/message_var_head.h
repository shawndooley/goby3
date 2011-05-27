// copyright 2008, 2009 t. schneider tes@mit.edu
// 
// this file is part of the Dynamic Compact Control Language (DCCL),
// the goby-acomms codec. goby-acomms is a collection of libraries 
// for acoustic underwater networking
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software.  If not, see <http://www.gnu.org/licenses/>.

#ifndef MESSAGE_VAR_HEAD20100317H
#define MESSAGE_VAR_HEAD20100317H

#include "message_var.h"

#include "goby/util/time.h"
#include "goby/util/sci.h"

namespace goby
{
    namespace transitional
    {    
        class DCCLMessageVarHead : public DCCLMessageVarInt
        {
          public:
          DCCLMessageVarHead(const std::string& default_name, int bit_size)
              : DCCLMessageVarInt(pow(2, bit_size)-1, 0),
                bit_size_(bit_size),
                default_name_(default_name)
                { set_name(default_name); }
            
        
          private:
            
            void initialize_specific()
            {
                if(default_name_ == name_) source_var_.clear();
            }

             virtual void set_defaults_specific(DCCLMessageVal& v, unsigned modem_id, unsigned id)
            { } 

          protected:
            int bit_size_;
            std::string default_name_;
        };
    
        class DCCLMessageVarTime : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarTime():
            DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_TIME],
                               transitional::HEAD_TIME_SIZE)
            { }

            void set_defaults_specific(DCCLMessageVal& v, unsigned modem_id, unsigned id)
            {
                double d;
                v = (!v.empty() && v.get(d)) ? v : DCCLMessageVal(util::ptime2unix_double(util::goby_time()));
            }

            void pre_encode(DCCLMessageVal& v)
            {
                double d;
                v = (!v.empty() && v.get(d)) ? DCCLMessageVal(goby::util::as<std::string>(goby::util::unix_double2ptime(d))) : v;
            }
            
            void post_decode(DCCLMessageVal& v)
            {
                std::string s;
                v = (!v.empty() && v.get(s)) ? DCCLMessageVal(goby::util::ptime2unix_double(goby::util::as<boost::posix_time::ptime>(s))) : v;
            }
            
        
            void write_schema_to_dccl2(std::ofstream* proto_file,
                                               int sequence_number)
            {
                *proto_file << "\t" << "optional string " << name() << " = " << sequence_number << " [(dccl.codec)=\"_time\", (dccl.in_head)=true, (queue.is_time)=true];" << std::endl;
            }
            

            
        };

        class DCCLMessageVarCCLID : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarCCLID():
            DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_CCL_ID],
                               transitional::HEAD_CCL_ID_SIZE) { }        

            void set_defaults_specific(DCCLMessageVal& v, unsigned modem_id, unsigned id)
            {
                v = long(acomms::DCCL_CCL_HEADER);
            }        
        };
    
        class DCCLMessageVarDCCLID : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarDCCLID()
              : DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_DCCL_ID],
                                   transitional::HEAD_DCCL_ID_SIZE)
            { }
        
            void set_defaults_specific(DCCLMessageVal& v, unsigned modem_id, unsigned id)
            {
                v = (!v.empty()) ? v : DCCLMessageVal(long(id));
            }
        };

        class DCCLMessageVarSrc : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarSrc()
              : DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_SRC_ID],
                                   transitional::HEAD_SRC_ID_SIZE)
            { }
        
            void set_defaults_specific(DCCLMessageVal& v, unsigned modem_id, unsigned id)
            {
                v = (!v.empty()) ? v : DCCLMessageVal(long(modem_id));
            }

            virtual std::string additional_option_extensions()
            { return "(dccl.in_head)=true, (queue.is_src)=true"; }

            
        };

        class DCCLMessageVarDest : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarDest():
            DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_DEST_ID],
                               transitional::HEAD_DEST_ID_SIZE) { }

            void set_defaults_specific(DCCLMessageVal& v, unsigned modem_id, unsigned id)
            {
                v = (!v.empty()) ? v : DCCLMessageVal(long(acomms::BROADCAST_ID));
            }

            virtual std::string additional_option_extensions()
            { return "(dccl.in_head)=true, (queue.is_dest)=true"; }
            
        };

        class DCCLMessageVarMultiMessageFlag : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarMultiMessageFlag():
            DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_MULTIMESSAGE_FLAG],
                               transitional::HEAD_FLAG_SIZE) { }
        
        };
    
        class DCCLMessageVarBroadcastFlag : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarBroadcastFlag():
            DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_BROADCAST_FLAG],
                               transitional::HEAD_FLAG_SIZE) { }


        };

        class DCCLMessageVarUnused : public DCCLMessageVarHead
        {
          public:
          DCCLMessageVarUnused():
            DCCLMessageVarHead(transitional::DCCL_HEADER_NAMES[transitional::HEAD_UNUSED],
                               transitional::HEAD_UNUSED_SIZE) { }


        };

    }
}
#endif
